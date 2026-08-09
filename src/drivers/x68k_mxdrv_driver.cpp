#include "drivers/x68k_mxdrv_driver.h"
#include "core/utf8_util.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "io/zip_archive.h"
#include "core/visual_state_util.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace hoot {
namespace {

// mdxmini 2.x public ABI. The pointed-to MDX/PDX/song structures are private to
// the library, so the public context can be represented without importing its
// GPL header into Hoot's source tree.
struct MdxMiniContext {
    int samples;
    int channels;
    void* mdx;
    void* pdx;
    void* self;
    void* songdata;
    int nlg_tempo;
};

#if defined(_WIN32)
using LibraryHandle = HMODULE;
LibraryHandle open_library(const char* n) { return LoadLibraryA(n); }
void close_library(LibraryHandle h) { if (h) FreeLibrary(h); }
void* load_symbol(LibraryHandle h, const char* n) { return reinterpret_cast<void*>(GetProcAddress(h, n)); }
#elif defined(__EMSCRIPTEN__)
using LibraryHandle = void*;
LibraryHandle open_library(const char*) { return nullptr; }
void close_library(LibraryHandle) {}
void* load_symbol(LibraryHandle, const char*) { return nullptr; }
#else
using LibraryHandle = void*;
LibraryHandle open_library(const char* n) { return dlopen(n, RTLD_NOW | RTLD_LOCAL); }
void close_library(LibraryHandle h) { if (h) dlclose(h); }
void* load_symbol(LibraryHandle h, const char* n) { return dlsym(h, n); }
#endif

template <typename T> bool bind(LibraryHandle h, const char* n, T& out)
{
    out = reinterpret_cast<T>(load_symbol(h, n));
    return out != nullptr;
}

template <size_t N> void copy_c_string(char (&dst)[N], const std::string& s)
{
    hoot::utf8::copy_c_string(dst, s);
}

std::atomic<uint64_t> temp_serial{0};

} // namespace

struct X68kMxdrvDriver::Impl {
    LibraryHandle library = nullptr;
    MdxMiniContext ctx{};
    bool song_open = false;
    using SetRate = void (*)(int);
    using Open = int (*)(MdxMiniContext*, char*, char*);
    using Close = void (*)(MdxMiniContext*);
    using CalcSample = int (*)(MdxMiniContext*, short*, int);
    using SetMaxLoop = void (*)(MdxMiniContext*, int);
    SetRate set_rate = nullptr;
    Open open = nullptr;
    Close close = nullptr;
    CalcSample calc_sample = nullptr;
    SetMaxLoop set_max_loop = nullptr;
};

X68kMxdrvDriver::X68kMxdrvDriver() : impl_(std::make_unique<Impl>()) {}
X68kMxdrvDriver::~X68kMxdrvDriver()
{
    close_song();
    if (impl_->library) close_library(impl_->library);
    cleanup_temp();
}

void X68kMxdrvDriver::close_song()
{
    if (impl_->song_open && impl_->close) impl_->close(&impl_->ctx);
    impl_->song_open = false;
    impl_->ctx = {};
}

void X68kMxdrvDriver::cleanup_temp()
{
    if (!temp_dir_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
        temp_dir_.clear();
    }
    track_files_.clear();
}

HootResult X68kMxdrvDriver::load(const HootEntry& entry, const std::string& packs_path,
                                 int sample_rate, std::string& error)
{
    close_song();
    cleanup_temp();
    sample_rate_ = sample_rate > 0 ? sample_rate : 44100;
    selected_track_ = -1;
    rendered_frames_ = 0;
    warning_.clear();

    if (entry.driver_name != "x68k/mxdrv") {
        error = "x68k mxdrv backend received a non-mxdrv entry";
        return HOOT_ERROR_UNSUPPORTED;
    }

    if (!impl_->library) {
        if (const char* p = std::getenv("HOOT_MDXMINI_LIBRARY"); p && *p) impl_->library = open_library(p);
#if defined(_WIN32)
        const char* libs[] = {"mdxmini.dll", "libmdxmini.dll"};
#elif defined(__APPLE__)
        const char* libs[] = {"libmdxmini.dylib", "/opt/homebrew/lib/libmdxmini.dylib", "/usr/local/lib/libmdxmini.dylib"};
#else
        const char* libs[] = {"libmdxmini.so.2", "libmdxmini.so"};
#endif
        if (!impl_->library) for (const char* lib : libs) if ((impl_->library = open_library(lib))) break;
        if (!impl_->library) {
            error = "mdxmini runtime not found; install libmdxmini (e.g. Homebrew: brew install mdxmini) or set HOOT_MDXMINI_LIBRARY";
            return HOOT_ERROR_UNSUPPORTED;
        }
        bool ok = bind(impl_->library, "mdx_set_rate", impl_->set_rate)
               && bind(impl_->library, "mdx_open", impl_->open)
               && bind(impl_->library, "mdx_close", impl_->close)
               && bind(impl_->library, "mdx_calc_sample", impl_->calc_sample);
        bind(impl_->library, "mdx_set_max_loop", impl_->set_max_loop);
        if (!ok) {
            error = "mdxmini runtime is missing required API symbols";
            close_library(impl_->library); impl_->library = nullptr;
            return HOOT_ERROR_UNSUPPORTED;
        }
    }
    impl_->set_rate(sample_rate_);

    ZipArchive archive;
    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    if (!archive.open(archive_path, error)) return HOOT_ERROR_IO;

    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    temp_dir_ = std::filesystem::temp_directory_path() /
        ("hoot_mxdrv_" + std::to_string(stamp) + "_" + std::to_string(temp_serial.fetch_add(1)));
    std::error_code ec;
    std::filesystem::create_directories(temp_dir_, ec);
    if (ec) { error = "unable to create temporary MDX directory: " + ec.message(); return HOOT_ERROR_IO; }

    size_t extracted = 0;
    for (const auto& asset : entry.assets) {
        if (asset.type != "data") continue;
        auto bytes = archive.read(asset.path, error);
        if (!error.empty()) { cleanup_temp(); return HOOT_ERROR_IO; }
        auto leaf = std::filesystem::path(asset.path).filename();
        auto out_path = temp_dir_ / leaf;
        std::ofstream out(out_path, std::ios::binary);
        if (!out || (!bytes.empty() && !out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))) {
            error = "unable to extract MXDRV asset: " + asset.path; cleanup_temp(); return HOOT_ERROR_IO;
        }
        ++extracted;
        std::string ext = leaf.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if (ext == ".mdx") track_files_[asset.offset] = out_path;
    }
    if (track_files_.empty()) {
        error = "x68k/mxdrv entry contains no MDX data assets";
        cleanup_temp();
        return HOOT_ERROR_NOT_FOUND;
    }
    (void)extracted;
    return HOOT_OK;
}

HootResult X68kMxdrvDriver::select_track(const HootEntry& entry, int track_index, std::string& error)
{
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index out of range"; return HOOT_ERROR_INVALID_ARGUMENT;
    }
    close_song();
    auto it = track_files_.find(entry.tracks[track_index].code);
    if (it == track_files_.end()) {
        error = "no MDX asset for track code " + std::to_string(entry.tracks[track_index].code);
        return HOOT_ERROR_NOT_FOUND;
    }
    auto mdx = it->second.string();
    auto dir = temp_dir_.string();
    std::vector<char> mdx_c(mdx.begin(), mdx.end()); mdx_c.push_back(0);
    std::vector<char> dir_c(dir.begin(), dir.end()); dir_c.push_back(0);
    impl_->ctx = {};
    if (impl_->open(&impl_->ctx, mdx_c.data(), dir_c.data()) != 0) {
        error = "mdxmini failed to open " + it->second.filename().string();
        return HOOT_ERROR_PARSE;
    }
    impl_->song_open = true;
    if (impl_->set_max_loop) impl_->set_max_loop(&impl_->ctx, 3);
    selected_track_ = track_index;
    rendered_frames_ = 0;
    return HOOT_OK;
}

void X68kMxdrvDriver::reset()
{
    close_song();
    selected_track_ = -1;
    rendered_frames_ = 0;
}

int X68kMxdrvDriver::render_s16(int16_t* out, int frames)
{
    if (!out || frames <= 0) return 0;
    std::fill(out, out + static_cast<size_t>(frames) * 2, int16_t{0});
    if (!impl_->song_open) return frames;
    int done = 0;
    while (done < frames) {
        const int chunk = std::min(frames - done, 4096);
        // mdxmini defines buffer_size in bytes. Hoot renders stereo s16, so
        // one output frame is two 16-bit samples = four bytes.
        const int active = impl_->calc_sample(&impl_->ctx,
            out + static_cast<size_t>(done) * 2, chunk * 2 * static_cast<int>(sizeof(int16_t)));
        done += chunk;
        if (active == 0) {
            std::fill(out + static_cast<size_t>(done) * 2,
                      out + static_cast<size_t>(frames) * 2, int16_t{0});
            break;
        }
    }
    rendered_frames_ += frames;
    return frames;
}

int X68kMxdrvDriver::render_float(float* out, int frames)
{
    if (!out || frames <= 0) return 0;
    std::vector<int16_t> tmp(static_cast<size_t>(frames) * 2);
    const int n = render_s16(tmp.data(), frames);
    for (size_t i = 0; i < tmp.size(); ++i) out[i] = static_cast<float>(tmp[i]) / 32768.0f;
    return n;
}

void X68kMxdrvDriver::fill_visual_state(const HootEntry&, int, HootVisualState& out) const
{
    out.abi_version = HOOT_VISUAL_ABI_VERSION;
    out.struct_size = sizeof(out);
    visual::copy(out.architecture, "X68000");
    visual::copy(out.cpu, "MXDRV / mdxmini");
    visual::copy(out.device, "YM2151 + PCM8");
    visual::copy(out.driver, name());

    // mdxmini's stable public ABI intentionally hides its internal per-voice
    // structures. Keep the original Hoot channel topology visible even when
    // a particular mdxmini build cannot publish note/key-on telemetry. The
    // generic X68000 backend provides live note state; this adapter can be
    // upgraded without changing the public Hoot visual ABI if mdxmini grows a
    // public voice-status API.
    for (int ch = 0; ch < 8; ++ch)
        visual::add_channel(out, HOOT_VISUAL_CHANNEL_FM, ch, "YM2151 FM#" + std::to_string(ch));
    for (int ch = 0; ch < 8; ++ch)
        visual::add_channel(out, HOOT_VISUAL_CHANNEL_PCM, ch, "PCM8 #" + std::to_string(ch));

    visual::add_register(out, "RATE", static_cast<uint64_t>(sample_rate_), 8);
    visual::add_register(out, "TRACK", static_cast<uint64_t>(selected_track_ < 0 ? 0 : selected_track_), 4);
}

void X68kMxdrvDriver::fill_track_info(const HootEntry& entry, int track_index, HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    copy_c_string(out.driver, name());
    copy_c_string(out.warning, warning_);
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) copy_c_string(out.title, entry.tracks[track_index].title);
    else copy_c_string(out.title, entry.title);
    out.debug_pcm8_rendered_voice_frames = rendered_frames_;
}

} // namespace hoot
