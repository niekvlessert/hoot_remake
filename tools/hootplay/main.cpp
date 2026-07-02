#include <algorithm>
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#else
#include <chrono>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "config/hoot_catalog.h"
#include "config/hoot_xml_loader.h"
#include "core/hoot_api.h"

namespace {

constexpr size_t kFramesPerBuffer = 2048;

std::atomic<bool> g_quit(false);
std::atomic<bool> g_stop_track(false);
std::atomic<bool> g_paused(false);
std::atomic<int> g_nav_delta(0);
HootContext* g_context = nullptr;

struct Options {
    std::string catalog = "hoot.xml";
    std::string packs = ".";
    std::string entry_or_archive;
    int rate = 44100;
    bool list = false;
    bool packs_explicit = false;
};

void usage(const char* argv0)
{
    std::fprintf(stderr,
                 "usage: %s [--catalog hoot.xml] [--packs dir] [--rate hz] <archive-or-entry-or-zip>\n"
                 "       %s --list [--catalog hoot.xml]\n"
                 "\n"
                 "Example: %s --packs packs fz68snd\n"
                 "         %s --catalog packs/hoot20251231/hoot.xml packs/fz68snd.zip\n"
                 "Controls: Space pause/resume, N next, P previous, Q quit\n",
                 argv0,
                 argv0,
                 argv0,
                 argv0);
}

bool need_value(int argc, char** argv, int index)
{
    if (index + 1 < argc) {
        return true;
    }
    std::fprintf(stderr, "missing value for %s\n", argv[index]);
    return false;
}

bool parse_options(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--catalog" || arg == "-c") && need_value(argc, argv, i)) {
            options.catalog = argv[++i];
        } else if ((arg == "--packs" || arg == "-p") && need_value(argc, argv, i)) {
            options.packs = argv[++i];
            options.packs_explicit = true;
        } else if ((arg == "--rate" || arg == "-r") && need_value(argc, argv, i)) {
            options.rate = std::atoi(argv[++i]);
        } else if (arg == "--list" || arg == "-l") {
            options.list = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        } else if (options.entry_or_archive.empty()) {
            options.entry_or_archive = arg;
        } else {
            std::fprintf(stderr, "unexpected argument: %s\n", arg.c_str());
            return false;
        }
    }
    return true;
}

bool has_zip_extension(const std::filesystem::path& path)
{
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension == ".zip";
}

struct EntryLookup {
    std::string name;
    bool from_zip_path = false;
};

EntryLookup normalize_entry_lookup(Options& options)
{
    EntryLookup lookup{options.entry_or_archive, false};
    const std::filesystem::path pack_path(options.entry_or_archive);
    if (!has_zip_extension(pack_path)) {
        return lookup;
    }

    lookup.from_zip_path = true;
    lookup.name = pack_path.stem().string();
    if (!options.packs_explicit) {
        const auto parent = pack_path.parent_path();
        options.packs = parent.empty() ? "." : parent.string();
    }
    return lookup;
}

bool is_supported_driver(const hoot::HootEntry& entry)
{
    if (entry.driver_name == "x68k/generic") {
        return true;
    }
    if (entry.driver_alias == "microcabin/pc88"
        || (entry.driver_name == "pc88/opn" && entry.archive == "xak2_98")) {
        return true;
    }
    if (entry.driver_name == "pc98dos/opn" && entry.driver_alias.find("MICROCABIN") != std::string::npos) {
        return true;
    }
    return false;
}

const hoot::HootEntry* resolve_entry(const hoot::HootCatalog& catalog, const std::string& name)
{
    if (const auto* by_id = catalog.find(name)) {
        return by_id;
    }
    const hoot::HootEntry* first_archive_match = nullptr;
    for (const auto& entry : catalog.entries()) {
        if (entry.archive == name) {
            if (first_archive_match == nullptr) {
                first_archive_match = &entry;
            }
            if (is_supported_driver(entry)) {
                return &entry;
            }
        }
    }
    return first_archive_match;
}

int list_entries(const hoot::HootCatalog& catalog)
{
    for (const auto& entry : catalog.entries()) {
        if (!is_supported_driver(entry)) {
            continue;
        }
        std::printf("%s\tarchive=%s\tdriver=%s\t%s\n",
                    entry.id.c_str(),
                    entry.archive.c_str(),
                    entry.driver_name.c_str(),
                    entry.title.c_str());
    }
    return 0;
}

void handle_signal(int)
{
    g_quit = true;
    g_stop_track = true;
}

void request_next()
{
    g_nav_delta = 1;
    g_stop_track = true;
}

void request_previous()
{
    g_nav_delta = -1;
    g_stop_track = true;
}

void toggle_pause()
{
    const bool paused = !g_paused.load();
    g_paused = paused;
    std::fprintf(stderr, paused ? "\nPaused\n" : "\nResumed\n");
}

void handle_command_char(char c)
{
    if (c == 'n' || c == 'N') {
        request_next();
    } else if (c == 'p' || c == 'P') {
        request_previous();
    } else if (c == ' ') {
        toggle_pause();
    } else if (c == 'q' || c == 'Q') {
        g_quit = true;
        g_stop_track = true;
    }
}

class TerminalRawMode {
public:
    TerminalRawMode()
    {
        if (!isatty(STDIN_FILENO)) {
            return;
        }
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) {
            return;
        }
        termios raw = saved_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        enabled_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }

    ~TerminalRawMode()
    {
        if (enabled_) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
        }
    }

private:
    bool enabled_ = false;
    termios saved_{};
};

void keyboard_thread()
{
    TerminalRawMode raw;
    while (!g_quit) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv{};
        tv.tv_usec = 50000;
        const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char c = 0;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                handle_command_char(c);
            }
        }
    }
}

bool render_frames(std::vector<int16_t>& pcm, size_t frames, size_t* rendered)
{
    pcm.assign(frames * 2, 0);
    *rendered = frames;
    if (g_paused) {
        return true;
    }
    const int count = hoot_render_s16(g_context, pcm.data(), static_cast<int>(frames));
    if (count <= 0) {
        std::fprintf(stderr, "hootplay: render failed\n");
        g_stop_track = true;
        *rendered = 0;
        return false;
    }
    *rendered = static_cast<size_t>(count);
    return true;
}

#if defined(__APPLE__)
AudioQueueRef g_queue = nullptr;

void fill_audioqueue_buffer(AudioQueueBufferRef buffer)
{
    std::vector<int16_t> pcm;
    size_t rendered = 0;
    render_frames(pcm, kFramesPerBuffer, &rendered);
    buffer->mAudioDataByteSize = static_cast<UInt32>(rendered * 4);
    if (rendered != 0) {
        std::memcpy(buffer->mAudioData, pcm.data(), rendered * 4);
    }
}

void audioqueue_callback(void*, AudioQueueRef, AudioQueueBufferRef buffer)
{
    if (g_quit || g_stop_track) {
        return;
    }
    fill_audioqueue_buffer(buffer);
    if (!g_quit && !g_stop_track) {
        AudioQueueEnqueueBuffer(g_queue, buffer, 0, nullptr);
    }
}

bool play_current_track(uint32_t sample_rate)
{
    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = sample_rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    fmt.mBytesPerFrame = 4;
    fmt.mBytesPerPacket = 4;
    fmt.mFramesPerPacket = 1;

    OSStatus err = AudioQueueNewOutput(&fmt, audioqueue_callback, nullptr, nullptr, nullptr, 0, &g_queue);
    if (err != noErr) {
        std::fprintf(stderr, "hootplay: AudioQueueNewOutput failed: %d\n", static_cast<int>(err));
        return false;
    }

    AudioQueueBufferRef buffers[3]{};
    for (auto& buffer : buffers) {
        err = AudioQueueAllocateBuffer(g_queue, static_cast<UInt32>(kFramesPerBuffer * 4), &buffer);
        if (err != noErr) {
            AudioQueueDispose(g_queue, true);
            g_queue = nullptr;
            return false;
        }
        fill_audioqueue_buffer(buffer);
        AudioQueueEnqueueBuffer(g_queue, buffer, 0, nullptr);
    }

    AudioQueueStart(g_queue, nullptr);
    while (!g_quit && !g_stop_track) {
        usleep(10000);
    }
    AudioQueueStop(g_queue, true);
    AudioQueueDispose(g_queue, true);
    g_queue = nullptr;
    return true;
}
#else
bool play_current_track(uint32_t sample_rate)
{
    std::fprintf(stderr, "hootplay: no native audio backend built; rendering silently\n");
    while (!g_quit && !g_stop_track) {
        std::vector<int16_t> pcm;
        size_t rendered = 0;
        if (!render_frames(pcm, kFramesPerBuffer, &rendered) && rendered == 0) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(rendered * 1000 / sample_rate));
    }
    return true;
}
#endif

void print_now_playing(const hoot::HootEntry& entry, int track)
{
    const auto title = track >= 0 && static_cast<size_t>(track) < entry.tracks.size()
        ? entry.tracks[track].title
        : entry.title;
    std::printf("Playing %d/%zu: %s [%s]\n",
                track + 1,
                entry.tracks.size(),
                title.c_str(),
                entry.archive.c_str());
    std::printf("Controls: Space pause/resume, N next, P previous, Q quit\n");
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage(argv[0]);
        return 2;
    }
    if (options.rate <= 0) {
        std::fprintf(stderr, "--rate must be positive\n");
        return 2;
    }

    hoot::HootCatalog catalog;
    hoot::HootXmlLoader loader;
    std::string error;
    if (!loader.load_file(options.catalog, catalog, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    if (options.list) {
        return list_entries(catalog);
    }
    if (options.entry_or_archive.empty()) {
        usage(argv[0]);
        return 2;
    }
    const auto lookup = normalize_entry_lookup(options);
    if (lookup.name.empty()) {
        std::fprintf(stderr, "unable to derive archive name from zip path: %s\n",
                     options.entry_or_archive.c_str());
        return 1;
    }

    const auto* entry = resolve_entry(catalog, lookup.name);
    if (entry == nullptr) {
        if (lookup.from_zip_path) {
            std::fprintf(stderr,
                         "no supported catalog entry found in %s for archive \"%s\" from zip path: %s\n",
                         options.catalog.c_str(),
                         lookup.name.c_str(),
                         options.entry_or_archive.c_str());
        } else {
            std::fprintf(stderr, "entry/archive not found in %s: %s\n",
                         options.catalog.c_str(),
                         options.entry_or_archive.c_str());
        }
        return 1;
    }
    if (!is_supported_driver(*entry)) {
        if (lookup.from_zip_path) {
            std::fprintf(stderr,
                         "no supported catalog entry found in %s for archive \"%s\" from zip path: %s\n",
                         options.catalog.c_str(),
                         lookup.name.c_str(),
                         options.entry_or_archive.c_str());
        } else {
            std::fprintf(stderr, "unsupported driver for %s: %s\n",
                         entry->id.c_str(),
                         entry->driver_name.c_str());
        }
        return 1;
    }
    if (entry->tracks.empty()) {
        std::fprintf(stderr, "entry has no tracks: %s\n", entry->id.c_str());
        return 1;
    }

    HootConfig config{};
    config.sample_rate = options.rate;
    config.packs_path = options.packs.c_str();
    std::unique_ptr<HootContext, decltype(&hoot_destroy)> ctx(hoot_create(&config), hoot_destroy);
    if (!ctx) {
        std::fprintf(stderr, "unable to create Hoot context\n");
        return 1;
    }
    if (hoot_load_xml(ctx.get(), options.catalog.c_str()) != HOOT_OK) {
        std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
        return 1;
    }
    if (hoot_load_entry(ctx.get(), entry->id.c_str()) != HOOT_OK) {
        std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::thread controls(keyboard_thread);
    g_context = ctx.get();
    int track = 0;
    while (!g_quit) {
        if (hoot_select_track(ctx.get(), track) != HOOT_OK) {
            std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
            break;
        }

        g_stop_track = false;
        g_paused = false;
        g_nav_delta = 0;
        print_now_playing(*entry, track);
        if (!play_current_track(static_cast<uint32_t>(options.rate))) {
            break;
        }

        const int delta = g_nav_delta.exchange(0);
        if (g_quit) {
            break;
        }
        if (delta < 0) {
            track = track == 0 ? static_cast<int>(entry->tracks.size() - 1) : track - 1;
        } else {
            track = (track + 1) % static_cast<int>(entry->tracks.size());
        }
    }
    g_context = nullptr;
    g_quit = true;
    if (controls.joinable()) {
        controls.join();
    }
    return 0;
}
