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
#endif
#if defined(HOOT_HAVE_SDL3)
#include <SDL3/SDL.h>
#endif
#include <chrono>
#if defined(_WIN32)
#include <conio.h>
#include <windows.h>
#else
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "config/hootplay_config.h"
#include "config/hoot_app_paths.h"
#include "core/console_utf8.h"
#include "core/hoot_api.h"
#include "../hoot2wav/wav_writer.h"

namespace {

constexpr size_t kFramesPerBuffer = 2048;

std::atomic<bool> g_quit(false);
std::atomic<bool> g_stop_track(false);
std::atomic<bool> g_paused(false);
std::atomic<int> g_nav_delta(0);
HootContext* g_context = nullptr;

struct Options {
    std::string catalog = "catalog/hoot.sqlite.zst";
    std::string config_path;
    std::string loaded_config_path;
    std::string packs = ".";
    std::string entry_or_archive;
    int rate = 44100;
    bool list = false;
    bool packs_explicit = false;
    bool mute_percussion = false;
    bool mute_percussion_explicit = false;
    std::string channels;
    bool channels_explicit = false;
    std::string wav_path;
    int wav_seconds = 0;
    int track = 1;
};

void usage(const char* argv0)
{
    std::fprintf(stderr,
                 "usage: %s [--config hootplay.ini] [options] [archive-or-entry-or-zip]\n"
                 "       %s --list [--config hootplay.ini]\n"
                 "\n"
                 "Normal runtime settings belong in hootplay.ini. Command-line options\n"
                 "remain supported as one-shot overrides for backwards compatibility.\n"
                 "\n"
                 "Overrides: --catalog file, --packs dir, --rate hz, --channels 3|2-5,\n"
                 "           --mute-percussion, --track n, --wav file --seconds n\n"
                 "Config search: --config, HOOTPLAY_CONFIG, ~/.hoot/hootplay.ini, ./hootplay.ini\n"
                 "\n"
                 "Example: %s fz68snd\n"
                 "         %s --config /path/to/hootplay.ini asuka68snd-gs\n"
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

std::string explicit_config_path(int argc, char** argv, bool& supplied, bool& valid)
{
    supplied = false;
    valid = true;
    std::string result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" || arg == "-f") {
            supplied = true;
            if (!need_value(argc, argv, i)) {
                valid = false;
                return {};
            }
            result = argv[++i];
        }
    }
    return result;
}

std::string discover_config_path(int argc, char** argv, const hoot::HootAppPaths& app_paths, bool& explicit_or_env, bool& valid)
{
    bool cli_supplied = false;
    auto path = explicit_config_path(argc, argv, cli_supplied, valid);
    if (!valid) return {};
    if (cli_supplied) {
        explicit_or_env = true;
        return path;
    }
    if (const char* env = std::getenv("HOOTPLAY_CONFIG"); env != nullptr && env[0] != '\0') {
        explicit_or_env = true;
        return env;
    }

    explicit_or_env = false;
    if (std::filesystem::is_regular_file(app_paths.config)) return app_paths.config.string();
    const std::filesystem::path cwd_config = "hootplay.ini";
    if (std::filesystem::is_regular_file(cwd_config)) {
        return cwd_config.string();
    }
    if (argc > 0 && argv[0] != nullptr && argv[0][0] != '\0') {
        std::error_code ec;
        auto executable = std::filesystem::absolute(argv[0], ec);
        if (!ec) {
            const auto beside_executable = executable.parent_path() / "hootplay.ini";
            if (std::filesystem::is_regular_file(beside_executable)) {
                return beside_executable.string();
            }
        }
    }
    return {};
}

void apply_file_config(const hoot::HootplayFileConfig& file, Options& options)
{
    if (file.has_catalog) options.catalog = file.catalog;
    if (file.has_packs) options.packs = file.packs;
    if (file.has_entry) options.entry_or_archive = file.entry;
    if (file.has_rate) options.rate = file.rate;
    if (file.has_list) options.list = file.list;
    if (file.has_mute_percussion) {
        options.mute_percussion = file.mute_percussion;
        options.mute_percussion_explicit = true;
    }
    if (file.has_channels) {
        options.channels = file.channels;
        options.channels_explicit = true;
    }
    if (file.has_wav_path) options.wav_path = file.wav_path;
    if (file.has_wav_seconds) options.wav_seconds = file.wav_seconds;
    if (file.has_track) options.track = file.track;
}

void set_environment_value(const std::string& name, const std::string& value)
{
#if defined(_WIN32)
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

void clear_environment_value(const std::string& name)
{
#if defined(_WIN32)
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
}

bool parse_options(int argc, char** argv, Options& options)
{
    bool positional_seen = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "-f") && need_value(argc, argv, i)) {
            options.config_path = argv[++i];
        } else if ((arg == "--catalog" || arg == "-c") && need_value(argc, argv, i)) {
            options.catalog = argv[++i];
        } else if ((arg == "--packs" || arg == "-p") && need_value(argc, argv, i)) {
            options.packs = argv[++i];
            options.packs_explicit = true;
        } else if ((arg == "--rate" || arg == "-r") && need_value(argc, argv, i)) {
            options.rate = std::atoi(argv[++i]);
        } else if (arg == "--mute-percussion") {
            options.mute_percussion = true;
            options.mute_percussion_explicit = true;
        } else if (arg == "--channels" && need_value(argc, argv, i)) {
            options.channels = argv[++i];
            options.channels_explicit = true;
        } else if (arg == "--wav" && need_value(argc, argv, i)) {
            options.wav_path = argv[++i];
        } else if (arg == "--seconds" && need_value(argc, argv, i)) {
            options.wav_seconds = std::atoi(argv[++i]);
        } else if (arg == "--track" && need_value(argc, argv, i)) {
            options.track = std::atoi(argv[++i]);
        } else if (arg == "--list" || arg == "-l") {
            options.list = true;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return false;
        } else if (!positional_seen) {
            options.entry_or_archive = arg;
            positional_seen = true;
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

bool is_supported_probe(const HootDriverProbe& probe)
{
    return probe.status >= HOOT_SUPPORT_EXPERIMENTAL;
}

int list_entries(HootContext* ctx)
{
    const int count = hoot_get_entry_count(ctx);
    for (int i = 0; i < count; ++i) {
        HootEntryInfo entry{};
        if (hoot_get_entry_info(ctx, i, &entry) != HOOT_OK) continue;
        HootDriverProbe probe{};
        if (hoot_probe_entry(ctx, entry.id, &probe) != HOOT_OK || !is_supported_probe(probe)) continue;
        std::printf("%s\tarchive=%s\tdriver=%s\tstatus=%s\thost=%s\t%s\n",
                    entry.id,
                    entry.archive,
                    entry.driver,
                    hoot_support_status_name(probe.status),
                    probe.driver_id,
                    entry.title);
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

#if defined(_WIN32)
class TerminalRawMode {
public:
    TerminalRawMode() = default;
};

void keyboard_thread()
{
#if !defined(_WIN32)
    TerminalRawMode raw;
#endif
    while (!g_quit) {
        if (_kbhit()) handle_command_char(static_cast<char>(_getch()));
        else Sleep(20);
    }
}
#else
class TerminalRawMode {
public:
    TerminalRawMode()
    {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &saved_) != 0) return;
        termios raw = saved_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        enabled_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }
    ~TerminalRawMode() { if (enabled_) tcsetattr(STDIN_FILENO, TCSANOW, &saved_); }
private:
    bool enabled_ = false;
    termios saved_{};
};

void keyboard_thread()
{
    TerminalRawMode raw;
    while (!g_quit) {
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO, &fds);
        timeval tv{}; tv.tv_usec = 50000;
        const int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char c = 0;
            if (read(STDIN_FILENO, &c, 1) == 1) handle_command_char(c);
        }
    }
}
#endif

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

bool record_current_track(const std::string& path, int seconds, uint32_t sample_rate)
{
    if (seconds <= 0) {
        std::fprintf(stderr, "hootplay: seconds must be positive when WAV output is configured\n");
        return false;
    }

    size_t frames_remaining = static_cast<size_t>(seconds) * sample_rate;
    std::vector<int16_t> capture;
    capture.reserve(frames_remaining * 2);
    while (frames_remaining != 0 && !g_quit) {
        std::vector<int16_t> pcm;
        size_t rendered = 0;
        const size_t request = std::min(frames_remaining, kFramesPerBuffer);
        if (!render_frames(pcm, request, &rendered) || rendered == 0) {
            return false;
        }
        capture.insert(capture.end(), pcm.begin(), pcm.begin() + static_cast<std::ptrdiff_t>(rendered * 2));
        frames_remaining -= rendered;
    }
    std::string error;
    if (!write_wav_s16(path, capture.data(), static_cast<int>(capture.size() / 2),
                       static_cast<int>(sample_rate), error)) {
        std::fprintf(stderr, "hootplay: %s\n", error.c_str());
        return false;
    }
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
#elif defined(HOOT_HAVE_SDL3)
bool play_current_track(uint32_t sample_rate)
{
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "hootplay: SDL audio init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(sample_rate);
    SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        std::fprintf(stderr, "hootplay: SDL audio open failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream);
    constexpr int bytes_per_frame = 4;
    while (!g_quit && !g_stop_track) {
        int queued = SDL_GetAudioStreamQueued(stream);
        if (queued < 0) queued = 0;
        if (queued < static_cast<int>(kFramesPerBuffer * bytes_per_frame * 2)) {
            std::vector<int16_t> pcm;
            size_t rendered = 0;
            if (!render_frames(pcm, kFramesPerBuffer, &rendered) && rendered == 0) break;
            if (!SDL_PutAudioStreamData(stream, pcm.data(), static_cast<int>(rendered * bytes_per_frame))) {
                std::fprintf(stderr, "hootplay: SDL audio queue failed: %s\n", SDL_GetError());
                break;
            }
        } else {
            SDL_Delay(2);
        }
    }
    SDL_DestroyAudioStream(stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return true;
}
#else
bool play_current_track(uint32_t sample_rate)
{
    std::fprintf(stderr, "hootplay: SDL3 not available; rendering silently\n");
    while (!g_quit && !g_stop_track) {
        std::vector<int16_t> pcm;
        size_t rendered = 0;
        if (!render_frames(pcm, kFramesPerBuffer, &rendered) && rendered == 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(rendered * 1000 / sample_rate));
    }
    return true;
}
#endif

void print_now_playing(const HootEntryInfo& entry,
                       const std::vector<HootCatalogTrackInfo>& tracks,
                       int track)
{
    const char* title = (track >= 0 && static_cast<size_t>(track) < tracks.size())
        ? tracks[static_cast<size_t>(track)].title
        : entry.title;
    std::printf("Playing %d/%zu: %s [%s]\n",
                track + 1,
                tracks.size(),
                title,
                entry.archive);
    std::printf("Controls: Space pause/resume, N next, P previous, Q quit\n");
    std::fflush(stdout);
}

bool help_requested(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    hoot::enable_utf8_console();
    hoot::HootAppPaths app_paths;
    std::string home_error;
    if (!hoot::bootstrap_hoot_home(app_paths, home_error)) {
        std::fprintf(stderr, "hootplay: %s\n", home_error.c_str());
        return 1;
    }
    hoot::apply_hoot_home_resource_defaults(app_paths);
    if (help_requested(argc, argv)) {
        usage(argv[0]);
        return 0;
    }

    Options options;
    if (std::filesystem::is_regular_file(app_paths.default_catalog)) options.catalog = app_paths.default_catalog.string();
    hoot::HootplayFileConfig file_config;
    bool config_required = false;
    bool config_path_valid = true;
    const auto config_path = discover_config_path(argc, argv, app_paths, config_required, config_path_valid);
    if (!config_path_valid) {
        usage(argv[0]);
        return 2;
    }
    if (!config_path.empty()) {
        std::string config_error;
        if (!hoot::load_hootplay_config(config_path, file_config, config_error)) {
            std::fprintf(stderr, "hootplay: %s\n", config_error.c_str());
            return config_required ? 2 : 1;
        }
        options.loaded_config_path = std::filesystem::absolute(config_path).lexically_normal().string();
        apply_file_config(file_config, options);
        hoot::apply_hootplay_environment(file_config);
    }
    if (!parse_options(argc, argv, options)) {
        usage(argv[0]);
        return 2;
    }
    if (options.rate <= 0) {
        std::fprintf(stderr, "sample rate must be positive\n");
        return 2;
    }
    if (options.track <= 0) {
        std::fprintf(stderr, "track must be 1 or higher\n");
        return 2;
    }
    if (options.mute_percussion_explicit) {
        set_environment_value("HOOT_X68K_MUTE_PERCUSSION", options.mute_percussion ? "1" : "0");
    }
    if (options.channels_explicit) {
        if (options.channels.empty()) clear_environment_value("HOOT_X68K_CHANNELS");
        else set_environment_value("HOOT_X68K_CHANNELS", options.channels);
    }

    EntryLookup lookup{};
    if (!options.list) {
        if (options.entry_or_archive.empty()) {
            usage(argv[0]);
            return 2;
        }
        lookup = normalize_entry_lookup(options);
        if (lookup.name.empty()) {
            std::fprintf(stderr, "unable to derive archive name from zip path: %s\n",
                         options.entry_or_archive.c_str());
            return 1;
        }
    }

    HootConfig config{};
    config.sample_rate = options.rate;
    config.packs_path = options.packs.c_str();
    std::unique_ptr<HootContext, decltype(&hoot_destroy)> ctx(hoot_create(&config), hoot_destroy);
    if (!ctx) {
        std::fprintf(stderr, "unable to create Hoot context\n");
        return 1;
    }
    if (hoot_load_catalog(ctx.get(), options.catalog.c_str()) != HOOT_OK) {
        std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
        return 1;
    }
    if (options.list) return list_entries(ctx.get());

    HootEntryInfo entry{};
    if (hoot_find_entry(ctx.get(), lookup.name.c_str(), &entry) != HOOT_OK) {
        if (lookup.from_zip_path) {
            std::fprintf(stderr,
                         "no catalog entry found in %s for archive \"%s\" from zip path: %s\n",
                         options.catalog.c_str(), lookup.name.c_str(), options.entry_or_archive.c_str());
        } else {
            std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
        }
        return 1;
    }
    HootDriverProbe probe{};
    if (hoot_probe_entry(ctx.get(), entry.id, &probe) != HOOT_OK || !is_supported_probe(probe)) {
        std::fprintf(stderr, "unsupported driver for %s: %s\n", entry.id, entry.driver);
        return 1;
    }
    if (entry.track_count <= 0) {
        std::fprintf(stderr, "entry has no tracks: %s\n", entry.id);
        return 1;
    }
    if (hoot_load_entry(ctx.get(), entry.id) != HOOT_OK) {
        std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
        return 1;
    }
    std::vector<HootCatalogTrackInfo> tracks(static_cast<size_t>(entry.track_count));
    for (int i = 0; i < entry.track_count; ++i) {
        if (hoot_get_catalog_track_info(ctx.get(), i, &tracks[static_cast<size_t>(i)]) != HOOT_OK) {
            std::fprintf(stderr, "unable to read track %d: %s\n", i + 1, hoot_last_error(ctx.get()));
            return 1;
        }
    }

    std::printf("==================================================\n");
    if (!options.loaded_config_path.empty()) {
        std::printf("Config:       %s\n", options.loaded_config_path.c_str());
    }
    std::printf("Game Title:   %s\n", entry.title);
    std::printf("ID:           %s\n", entry.id);
    std::printf("Driver:       %s\n", entry.driver);
    std::printf("Replay Host:  %s (%s)\n", probe.driver_id, hoot_support_status_name(probe.status));
    std::printf("Archive:      %s\n", entry.archive);
    std::printf("==================================================\n\n");

    int track = options.track - 1;
    if (track < 0 || static_cast<size_t>(track) >= tracks.size()) {
        std::fprintf(stderr, "hootplay: configured track is outside the catalog track list\n");
        return 1;
    }

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    std::thread controls(keyboard_thread);
    g_context = ctx.get();
    while (!g_quit) {
        if (hoot_select_track(ctx.get(), track) != HOOT_OK) {
            std::fprintf(stderr, "%s\n", hoot_last_error(ctx.get()));
            break;
        }

        HootTrackInfo info{};
        if (hoot_get_track_info(ctx.get(), &info) == HOOT_OK && info.warning[0] != '\0') {
            std::fprintf(stderr, "hootplay: warning: %s\n", info.warning);
        }

        g_stop_track = false;
        g_paused = false;
        g_nav_delta = 0;
        print_now_playing(entry, tracks, track);
        const bool played = options.wav_path.empty()
            ? play_current_track(static_cast<uint32_t>(options.rate))
            : record_current_track(options.wav_path,
                                   options.wav_seconds,
                                   static_cast<uint32_t>(options.rate));
        if (!played) {
            break;
        }
        if (!options.wav_path.empty()) {
            break;
        }

        const int delta = g_nav_delta.exchange(0);
        if (g_quit) {
            break;
        }
        if (delta < 0) {
            track = track == 0 ? static_cast<int>(tracks.size() - 1) : track - 1;
        } else {
            track = (track + 1) % static_cast<int>(tracks.size());
        }
    }
    g_context = nullptr;
    g_quit = true;
    if (controls.joinable()) {
        controls.join();
    }
    return 0;
}
