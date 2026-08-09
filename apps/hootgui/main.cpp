#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "config/hootplay_config.h"
#include "config/hoot_app_paths.h"
#include "config/hoot_settings.h"
#include "config/hoot_user_overrides.h"
#include "core/console_utf8.h"
#include "core/hoot_api.h"
#include "retro_renderer.h"
#include "spectrum.h"
#include "wav_recorder.h"

namespace {

enum class LibraryLevel { Root, Major, Games, Variants, Tracks };

enum class CatalogEditorField {
    None,
    GeneralTitle, GeneralArchive, GeneralDriverName, GeneralDriverType, GeneralSampleRate, GeneralRefresh,
    TrackCode, TrackTitle, TrackVoice,
    OptionName, OptionValue,
    AssetType, AssetPath, AssetTransform, AssetOffset
};


struct LibraryPosition {
    int selected = 0;
    int scroll = 0;
};

struct Options {
    std::string catalog = "catalog/hoot.sqlite.zst";
    std::string packs = ".";
    std::string entry;
    std::string loaded_config_path;
    std::string font;
    int rate = 44100;
    int track = 1;
    bool check_font = false;
};

struct App {
    HootContext* hoot = nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_AudioStream* audio = nullptr;
    std::unique_ptr<hootgui::RetroRenderer> ui;
    hootgui::SpectrumAnalyzer spectrum;
    hootgui::UiModel model;
    std::vector<int16_t> audio_buffer;
    int sample_rate = 44100;
    bool running = true;
    bool pack_loaded = false;
    bool master_muted = false;
    bool stopped = false;
    hootgui::WavRecorder recorder;
    Uint64 last_visual_tick = 0;
    Uint64 last_spectrum_tick = 0;
    std::mutex pending_mutex;
    std::string pending_open_path;
    std::string pending_record_path;
    std::string pending_notice;
    std::unordered_set<std::string> dismissed_warnings;

    bool library_open = false;
    bool library_search_editing = false;
    bool library_available_only = false;
    LibraryLevel library_level = LibraryLevel::Root;
    std::string library_major;
    std::string library_subtype;
    std::string library_variant_archive;
    int library_entry_index = -1;
    int library_selected = 0;
    int library_scroll = 0;
    std::string library_search;
    std::string library_message;
    std::vector<HootEntryInfo> catalog_entries;
    std::vector<hootgui::LibraryRow> library_rows;
    std::unordered_map<std::string, LibraryPosition> library_positions;
    Uint64 library_last_click_tick = 0;
    int library_last_click_index = -1;
    std::unordered_map<std::string, std::filesystem::path> pack_index;
    std::vector<std::filesystem::path> pack_directories;
    std::unordered_map<std::string, HootDriverProbe> probe_cache;

    HootDriverProbe current_probe{};
    bool current_probe_valid = false;
    Uint64 track_started_tick = 0;
    bool track_audio_seen = false;
    bool silence_warning_shown = false;

    bool catalog_editor_open = false;
    bool catalog_editor_editing = false;
    bool catalog_editor_dirty = false;
    bool catalog_editor_locally_modified = false;
    std::string catalog_editor_tab = "general";
    int catalog_editor_selected = 0;
    int catalog_editor_scroll = 0;
    int catalog_editor_edit_column = 0;
    CatalogEditorField catalog_editor_field = CatalogEditorField::None;
    std::string catalog_editor_buffer;
    std::string catalog_editor_message;
    int catalog_editor_entry_index = -1;
    hoot::HootEntryOverride catalog_editor_entry;
    hoot::HootUserOverrides user_overrides;
    std::filesystem::path user_overrides_path;
    std::string catalog_path;

    std::string settings_config_path;
    std::filesystem::path roms_dir;
    std::filesystem::path ui_state_path;
    std::string last_pack_directory;
    hoot::HootSettingsDocument settings_document;
    bool settings_open = false;
    bool settings_dirty = false;
    bool settings_editing = false;
    hootgui::TopMenu top_menu = hootgui::TopMenu::None;
    bool about_open = false;
    std::string settings_section = "player";
    int settings_scroll = 0;
    int settings_selected = -1;
    std::string settings_message;

    explicit App(int rate) : spectrum(rate), audio_buffer(2048 * 2), sample_rate(rate) {}
};

#ifdef __EMSCRIPTEN__
App* g_web_app = nullptr;
#endif

void usage(const char* argv0)
{
    std::fprintf(stderr,
        "usage: %s [--config file] [--catalog file] [--packs dir] [--font file] [--check-font] [--rate hz] [--track n] [archive-or-entry-or-zip]\n"
        "controls: O open pack, Space play/pause, Ctrl+S stop, Ctrl+R restart, Left/P previous, Right/N next,\n"
        "          M mute/unmute, Ctrl+Shift+R WAV record, Up/Down playlist, PageUp/PageDown channel bank,\n"
        "          L Library, Alt+Enter fullscreen, Q/Escape quit; drop a ZIP to load it\n", argv0);
}

bool need_value(int argc, char** argv, int index)
{
    if (index + 1 < argc) return true;
    std::fprintf(stderr, "missing value for %s\n", argv[index]);
    return false;
}

std::string discover_config_path(int argc, char** argv, const hoot::HootAppPaths& app_paths, bool& required, bool& valid)
{
    required = false;
    valid = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" || arg == "-f") {
            required = true;
            if (!need_value(argc, argv, i)) { valid = false; return {}; }
            return argv[i + 1];
        }
    }
    if (const char* env = std::getenv("HOOTPLAY_CONFIG"); env && env[0]) {
        required = true;
        return env;
    }
#ifndef __EMSCRIPTEN__
    if (std::filesystem::is_regular_file(app_paths.config)) return app_paths.config.string();
    const std::filesystem::path cwd = "hootplay.ini";
    if (std::filesystem::is_regular_file(cwd)) return cwd.string();
    if (argc > 0 && argv[0] && argv[0][0]) {
        std::error_code ec;
        const auto executable = std::filesystem::absolute(argv[0], ec);
        if (!ec) {
            const auto beside = executable.parent_path() / "hootplay.ini";
            if (std::filesystem::is_regular_file(beside)) return beside.string();
        }
    }
#endif
    return {};
}

void apply_file_config(const hoot::HootplayFileConfig& file, Options& o)
{
    if (file.has_catalog) o.catalog = file.catalog;
    if (file.has_packs) o.packs = file.packs;
    if (file.has_entry) o.entry = file.entry;
    if (file.has_rate) o.rate = file.rate;
    if (file.has_track) o.track = file.track;
    if (file.has_ui_font) o.font = file.ui_font;
}

bool parse_options(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& target) -> bool {
            if (i + 1 >= argc) return false;
            target = argv[++i]; return true;
        };
        if (arg == "--config" || arg == "-f") { std::string ignored; if (!value(ignored)) return false; }
        else if (arg == "--catalog") { if (!value(o.catalog)) return false; }
        else if (arg == "--packs") { if (!value(o.packs)) return false; }
        else if (arg == "--font") { if (!value(o.font)) return false; }
        else if (arg == "--check-font") { o.check_font = true; }
        else if (arg == "--rate") { if (i + 1 >= argc) return false; o.rate = std::atoi(argv[++i]); }
        else if (arg == "--track") { if (i + 1 >= argc) return false; o.track = std::max(1, std::atoi(argv[++i])); }
        else if (arg == "--help" || arg == "-h") { usage(argv[0]); std::exit(0); }
        else if (!arg.empty() && arg[0] == '-') return false;
        else o.entry = arg;
    }
    return true;
}


std::string lower_ascii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string warning_help(const std::string& warning, const std::filesystem::path& roms_dir)
{
    const std::string lower = lower_ascii(warning);
    const std::filesystem::path roms = roms_dir.empty() ? std::filesystem::path("~/.hoot/roms") : roms_dir;
    auto rom_path = [&](const char* name) { return (roms / name).string() + std::filesystem::path::preferred_separator; };
    const bool rom_related = lower.find("rom") != std::string::npos
        || lower.find("ic18") != std::string::npos || lower.find("ic19") != std::string::npos
        || lower.find("ic20") != std::string::npos || lower.find("sn-u110") != std::string::npos;
    std::vector<std::string> tips;
    auto add = [&](const std::string& tip) {
        if (std::find(tips.begin(), tips.end(), tip) == tips.end()) tips.push_back(tip);
    };

    if (rom_related) {
        if (lower.find("mt-32") != std::string::npos || lower.find("mt32") != std::string::npos)
            add("MT-32 ROMs: " + rom_path("mt32"));
        if (lower.find("cm-32l") != std::string::npos || lower.find("cm32l") != std::string::npos ||
            lower.find("cm-64") != std::string::npos || lower.find("cm64") != std::string::npos)
            add("CM-32L/CM-64 LA ROMs: " + rom_path("cm32l"));
        if (lower.find("cm-32p") != std::string::npos || lower.find("cm32p") != std::string::npos ||
            lower.find("ic18") != std::string::npos || lower.find("ic19") != std::string::npos ||
            lower.find("ic20") != std::string::npos || lower.find("sn-u110") != std::string::npos ||
            lower.find("cm-64") != std::string::npos || lower.find("cm64") != std::string::npos)
            add("CM-32P PCM/card ROMs: " + rom_path("cm32p"));
        if (lower.find("sc-55") != std::string::npos || lower.find("sc55") != std::string::npos ||
            lower.find("soundcanvas") != std::string::npos || lower.find("sound canvas") != std::string::npos)
            add("SC-55 ROMs: " + rom_path("sc55"));
        if (tips.empty()) add("ROMs can be stored below " + roms.string() + std::string(1, std::filesystem::path::preferred_separator) + ".");
        add("Settings > MIDI can override every default ROM path. Hoot does not include copyrighted ROM dumps.");
    }

    if (lower.find("nuked-sc55") != std::string::npos || lower.find("clap plugin") != std::string::npos)
        add("Nuked-SC55 plugin: configure its CLAP path in Settings > MIDI.");
    if (lower.find("fluidsynth") != std::string::npos || lower.find("soundfont") != std::string::npos)
        add("FluidSynth/SoundFont: configure the backend and SoundFont in Settings > MIDI.");
    if (lower.find("vermouth") != std::string::npos)
        add("Vermouth: configure the runtime library/backend in Settings > MIDI.");
    if (lower.find("munt") != std::string::npos && !rom_related)
        add("Munt/mt32emu: install/configure the runtime library in Settings > MIDI.");

    if (tips.empty()) {
        if (lower.find("backend") != std::string::npos || lower.find("synth") != std::string::npos ||
            lower.find("midi") != std::string::npos) {
            add("Open Settings > MIDI to select or configure the requested sound backend.");
        } else {
            add("The track can continue with the current setup, but the result may be incomplete or less authentic.");
        }
    }

    std::string result;
    for (size_t i = 0; i < tips.size(); ++i) {
        if (i) result += "\n";
        result += tips[i];
    }
    return result;
}


std::vector<std::filesystem::path> icon_candidates(const char* argv0)
{
    std::vector<std::filesystem::path> paths;
    auto add = [&](const std::filesystem::path& p) {
        if (!p.empty() && std::find(paths.begin(), paths.end(), p) == paths.end())
            paths.push_back(p);
    };
    add("apps/hootgui/hootui_icon.bmp");
#ifndef __EMSCRIPTEN__
    std::error_code ec;
    if (argv0 && argv0[0]) {
        const auto exe = std::filesystem::absolute(argv0, ec);
        if (!ec) {
            const auto dir = exe.parent_path();
            add(dir / "hootui_icon.bmp");
            add(dir / "../share/hoot/hootui_icon.bmp");
            add(dir / "../Resources/hootui_icon.bmp");
        }
    }
#endif
    return paths;
}

void apply_window_icon(SDL_Window* window, const char* argv0)
{
#ifndef __EMSCRIPTEN__
    if (!window) return;
    for (const auto& candidate : icon_candidates(argv0)) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        if (SDL_Surface* surface = SDL_LoadBMP(candidate.string().c_str())) {
            SDL_SetWindowIcon(window, surface);
            SDL_DestroySurface(surface);
            return;
        }
    }
#else
    (void)window;
    (void)argv0;
#endif
}

bool load_reference(App& app, const std::string& ref, int requested_track);
bool switch_track(App& app, int index);
void add_pack_directory(App& app, std::filesystem::path dir);
void refresh_pack_index(App& app);
void save_library_position(App& app);
void stop_library_search(App& app);
void rebuild_library_rows(App& app);

void sync_web_persistence()
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.hootPersist) Module.hootPersist(); });
#endif
}

std::string safe_recording_stem(const App& app)
{
    std::string stem = app.pack_loaded && app.model.entry.archive[0]
        ? std::string(app.model.entry.archive) : std::string("hoot");
    for (char& c : stem) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '-' || c == '_')) c = '_';
    }
    if (stem.empty()) stem = "hoot";
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_track%03d.wav", app.model.selected_track + 1);
    return stem + suffix;
}

bool stop_recording(App& app, bool offer_download = true)
{
    if (!app.recorder.active()) return true;
    const std::string path = app.recorder.path();
    std::string error;
    const bool ok = app.recorder.stop(error);
    app.model.recording = false;
    if (!ok) {
        app.model.notice = "WAV recording: " + error;
        return false;
    }
    app.model.notice = "WAV saved: " + path;
#ifdef __EMSCRIPTEN__
    sync_web_persistence();
    if (offer_download) {
        EM_ASM({
            const p = UTF8ToString($0);
            if (Module.hootDownloadFile) Module.hootDownloadFile(p);
        }, path.c_str());
    }
#else
    (void)offer_download;
#endif
    return true;
}

bool start_recording_path(App& app, const std::string& path)
{
    if (!app.pack_loaded) {
        app.model.notice = "Load a pack before starting WAV recording.";
        return false;
    }
    if (app.recorder.active()) stop_recording(app, false);
    std::string error;
    if (!app.recorder.start(path, app.sample_rate, error)) {
        app.model.notice = "WAV recording: " + error;
        return false;
    }
    app.model.recording = true;
    app.model.notice = "Recording WAV: " + path;
    return true;
}

void set_paused(App& app, bool paused)
{
    if (!app.pack_loaded) return;
    if (app.stopped && !paused) {
        // Play after Stop restarts the selected track, matching normal player
        // semantics rather than resuming from an arbitrary buffered position.
        const int track = app.model.selected_track;
        app.stopped = false;
        if (!switch_track(app, track)) return;
    }
    app.model.paused = paused;
    app.model.stopped = app.stopped;
    if (app.audio) {
        if (paused || app.stopped) SDL_PauseAudioStreamDevice(app.audio);
        else SDL_ResumeAudioStreamDevice(app.audio);
    }
}

void stop_playback(App& app)
{
    if (!app.pack_loaded) return;
    stop_recording(app);
    app.stopped = true;
    app.model.stopped = true;
    app.model.paused = false;
    if (app.audio) {
        SDL_PauseAudioStreamDevice(app.audio);
        SDL_ClearAudioStream(app.audio);
    }
    app.spectrum.reset(app.sample_rate);
    app.model.notice = "Stopped";
}

void restart_playback(App& app)
{
    if (!app.pack_loaded) return;
    stop_recording(app);
    app.stopped = false;
    if (switch_track(app, app.model.selected_track)) {
        app.model.paused = false;
        app.model.stopped = false;
        if (app.audio) SDL_ResumeAudioStreamDevice(app.audio);
    }
}

void toggle_master_mute(App& app)
{
    app.master_muted = !app.master_muted;
    app.model.muted_all = app.master_muted;
    app.model.notice = app.master_muted ? "All output muted" : "All output unmuted";
}

void update_visual(App& app)
{
    if (app.pack_loaded) hoot_get_visual_state(app.hoot, &app.model.visual);
    if (app.model.visual.sample_rate == 0) app.model.visual.sample_rate = static_cast<uint32_t>(app.sample_rate);
    app.model.spectrum_left = app.spectrum.left();
    app.model.spectrum_right = app.spectrum.right();
    app.model.stopped = app.stopped;
    app.model.muted_all = app.master_muted;
    app.model.recording = app.recorder.active();
}

bool switch_track(App& app, int index)
{
    if (!app.pack_loaded || app.model.tracks.empty()) return false;
    if (app.recorder.active()) stop_recording(app);
    app.stopped = false;
    app.model.stopped = false;
    index = std::clamp(index, 0, static_cast<int>(app.model.tracks.size()) - 1);
    if (hoot_select_track(app.hoot, index) != HOOT_OK) {
        app.model.warning.clear();
        app.model.notice = hoot_last_error(app.hoot);
        return false;
    }
    app.model.selected_track = index;
    app.track_started_tick = SDL_GetTicks();
    app.track_audio_seen = false;
    app.silence_warning_shown = false;
    // Keep the selected row and mouse hit-testing on the same playlist page.
    // The current 1440x900 layout has room for 13 rows in the bottom pane.
    constexpr int visible_rows = 13;
    const int max_scroll = std::max(0, static_cast<int>(app.model.tracks.size()) - visible_rows);
    if (index < app.model.playlist_scroll) app.model.playlist_scroll = index;
    else if (index >= app.model.playlist_scroll + visible_rows)
        app.model.playlist_scroll = index - visible_rows + 1;
    app.model.playlist_scroll = std::clamp(app.model.playlist_scroll, 0, max_scroll);
    app.spectrum.reset(app.sample_rate);
    if (app.audio) SDL_ClearAudioStream(app.audio);
    app.model.warning.clear();
    app.model.warning_help.clear();
    app.model.warning_overlay_visible = false;
    if (hoot_get_track_info(app.hoot, &app.model.track_info) == HOOT_OK) {
        app.model.warning = app.model.track_info.warning;
        if (!app.model.warning.empty()) {
            app.model.warning_help = warning_help(app.model.warning, app.roms_dir);
            app.model.warning_overlay_visible = app.dismissed_warnings.find(app.model.warning) == app.dismissed_warnings.end();
            std::fprintf(stderr, "hootui: warning: %s\n", app.model.warning.c_str());
        }
    }
    update_visual(app);
    return true;
}

bool load_reference(App& app, const std::string& ref, int requested_track)
{
    app.model.warning.clear();
    app.model.warning_help.clear();
    app.model.warning_overlay_visible = false;
    app.dismissed_warnings.clear();
    std::filesystem::path path(ref);
    std::string lookup = ref;
    const bool opened_archive = lower_ascii(path.extension().string()) == ".zip";
    if (opened_archive) {
        const std::string parent = path.has_parent_path() ? path.parent_path().string() : ".";
        hoot_set_packs_path(app.hoot, parent.c_str());
        lookup = path.stem().string();

        // One physical Hoot archive can deliberately expose several catalogue
        // entries for different target hardware (OPM, MT-32, CM-64, GS, GM,
        // Vermouth, ...).  The old direct-open path silently selected the first
        // matching archive entry, which made those variants impossible to pick.
        std::vector<int> variants;
        const std::string archive_key = lower_ascii(lookup);
        for (std::size_t i = 0; i < app.catalog_entries.size(); ++i) {
            if (lower_ascii(app.catalog_entries[i].archive) == archive_key)
                variants.push_back(static_cast<int>(i));
        }
        if (variants.size() > 1) {
            refresh_pack_index(app);
            save_library_position(app);
            stop_library_search(app);
            app.library_open = true;
            app.library_level = LibraryLevel::Variants;
            app.library_variant_archive = lookup;
            app.library_major.clear();
            app.library_subtype.clear();
            app.library_entry_index = -1;
            app.library_search.clear();
            app.library_selected = 0;
            app.library_scroll = 0;
            app.library_message = "This pack has multiple hardware variants. Choose one; playback will start after selection.";
            rebuild_library_rows(app);
            return true;
        }
    }

    HootEntryInfo entry{};
    if (hoot_find_entry(app.hoot, lookup.c_str(), &entry) != HOOT_OK) {
        app.model.notice = std::string("Pack not found in catalogue: ") + lookup;
        return false;
    }
    std::memset(&app.current_probe, 0, sizeof(app.current_probe));
    app.current_probe_valid = hoot_probe_entry(app.hoot, entry.id, &app.current_probe) == HOOT_OK;
    if (hoot_load_entry(app.hoot, entry.id) != HOOT_OK) {
        app.model.notice = hoot_last_error(app.hoot);
        return false;
    }

    app.pack_loaded = true;
    app.model.entry = entry;
    app.model.tracks.clear();
    const int count = hoot_get_track_count(app.hoot);
    app.model.tracks.resize(static_cast<size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i)
        hoot_get_catalog_track_info(app.hoot, i, &app.model.tracks[static_cast<size_t>(i)]);
    app.model.playlist_scroll = 0;
    app.model.channel_scroll = 0;
    return switch_track(app, std::clamp(requested_track, 0, std::max(0, count - 1)));
}

void pump_audio(App& app)
{
    if (!app.pack_loaded || app.model.paused || app.stopped || !app.audio) return;
    constexpr int bytes_per_frame = static_cast<int>(sizeof(int16_t) * 2);
    constexpr int target_frames = 4096;
    int queued = SDL_GetAudioStreamQueued(app.audio);
    if (queued < 0) queued = 0;
    while (queued < target_frames * bytes_per_frame) {
        const int frames = static_cast<int>(app.audio_buffer.size() / 2);
        const int rendered = hoot_render_s16(app.hoot, app.audio_buffer.data(), frames);
        if (rendered <= 0) break;
        app.spectrum.push_s16(app.audio_buffer.data(), static_cast<size_t>(rendered));
        if (app.recorder.active()) {
            std::string record_error;
            if (!app.recorder.append(app.audio_buffer.data(), rendered, record_error)) {
                stop_recording(app);
                app.model.notice = "WAV recording stopped: " + record_error;
            }
        }
        if (!app.track_audio_seen) {
            const size_t sample_count = static_cast<size_t>(rendered) * 2u;
            for (size_t i = 0; i < sample_count; ++i) {
                if (std::abs(static_cast<int>(app.audio_buffer[i])) > 16) {
                    app.track_audio_seen = true;
                    break;
                }
            }
        }
        if (app.master_muted)
            std::fill(app.audio_buffer.begin(), app.audio_buffer.begin() + static_cast<std::ptrdiff_t>(rendered * 2), int16_t{0});
        if (!SDL_PutAudioStreamData(app.audio, app.audio_buffer.data(), rendered * bytes_per_frame)) {
            app.model.notice = std::string("SDL audio: ") + SDL_GetError();
            break;
        }
        queued += rendered * bytes_per_frame;
    }
}

void maybe_warn_silent_playback(App& app, Uint64 now)
{
    if (!app.pack_loaded || app.model.paused || app.stopped || !app.audio || app.track_audio_seen
        || app.silence_warning_shown || app.track_started_tick == 0
        || now < app.track_started_tick || now - app.track_started_tick < 5000) {
        return;
    }
    app.silence_warning_shown = true;
    const std::string warning = "Playback is running, but no audio activity was detected for 5 seconds.";
    std::string help = "The archive and track were accepted, but successful loading does not guarantee that the replay driver is compatible.";
    if (app.current_probe_valid) {
        help += "\nDriver support: ";
        help += hoot_support_status_name(app.current_probe.status);
        if (app.current_probe.driver_id[0]) help += " (" + std::string(app.current_probe.driver_id) + ")";
        if (app.current_probe.reason[0]) help += ".\n" + std::string(app.current_probe.reason);
    }
    help += "\nTry another track first. If the whole pack stays silent, this replay path needs real-pack validation/fixing rather than a ROM setting.";
    app.model.warning = warning;
    app.model.warning_help = help;
    app.model.warning_overlay_visible = app.dismissed_warnings.find(warning) == app.dismissed_warnings.end();
    std::fprintf(stderr, "hootui: warning: %s\n%s\n", warning.c_str(), help.c_str());
}

void load_ui_state(App& app)
{
    app.last_pack_directory.clear();
    if (app.ui_state_path.empty()) return;
    std::ifstream in(app.ui_state_path, std::ios::binary);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        constexpr const char* key = "last_pack_directory=";
        constexpr const char* available_key = "library_available_only=";
        if (line.rfind(key, 0) == 0) {
            const std::string value = line.substr(std::char_traits<char>::length(key));
            std::error_code ec;
            if (!value.empty() && std::filesystem::is_directory(std::filesystem::path(value), ec))
                app.last_pack_directory = value;
        } else if (line.rfind(available_key, 0) == 0) {
            const std::string value = lower_ascii(line.substr(std::char_traits<char>::length(available_key)));
            app.library_available_only = value == "1" || value == "true" || value == "yes" || value == "on";
        }
    }
}

void save_ui_state(const App& app)
{
    if (app.ui_state_path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(app.ui_state_path.parent_path(), ec);
    if (ec) return;
    const auto tmp = app.ui_state_path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << "[ui]\n";
        out << "last_pack_directory=" << app.last_pack_directory << "\n";
        out << "library_available_only=" << (app.library_available_only ? "true" : "false") << "\n";
        if (!out) return;
    }
    std::filesystem::rename(tmp, app.ui_state_path, ec);
    if (ec) {
        std::filesystem::remove(app.ui_state_path, ec);
        ec.clear();
        std::filesystem::rename(tmp, app.ui_state_path, ec);
    }
    sync_web_persistence();
}

void remember_pack_directory(App& app, const std::string& opened_path)
{
    std::error_code ec;
    std::filesystem::path path(opened_path);
    if (path.empty()) return;
    if (!path.is_absolute()) path = std::filesystem::absolute(path, ec);
    if (ec) return;
    auto directory = std::filesystem::is_directory(path, ec) ? path : path.parent_path();
    if (ec || directory.empty() || !std::filesystem::is_directory(directory, ec)) return;
    app.last_pack_directory = directory.lexically_normal().string();
    add_pack_directory(app, directory);
    if (app.hoot) hoot_set_packs_path(app.hoot, app.last_pack_directory.c_str());
    refresh_pack_index(app);
    if (app.library_open) rebuild_library_rows(app);
    save_ui_state(app);
}

#ifndef __EMSCRIPTEN__
void SDLCALL open_pack_dialog_callback(void* userdata, const char* const* filelist, int)
{
    auto* app = static_cast<App*>(userdata);
    if (!app) return;
    std::lock_guard<std::mutex> lock(app->pending_mutex);
    if (!filelist) {
        app->pending_notice = std::string("Open dialog failed: ") + SDL_GetError();
    } else if (filelist[0]) {
        app->pending_open_path = filelist[0];
    }
}

void request_open_pack_dialog(App& app)
{
    static const SDL_DialogFileFilter filters[] = {
        {"Hoot pack ZIP", "zip"},
        {"All files", "*"}
    };
    const char* default_location = app.last_pack_directory.empty()
        ? nullptr : app.last_pack_directory.c_str();
    SDL_ShowOpenFileDialog(open_pack_dialog_callback, &app, app.window,
                           filters, 2, default_location, false);
}

void SDLCALL record_wav_dialog_callback(void* userdata, const char* const* filelist, int)
{
    auto* app = static_cast<App*>(userdata);
    if (!app) return;
    std::lock_guard<std::mutex> lock(app->pending_mutex);
    if (!filelist) {
        app->pending_notice = std::string("Save dialog failed: ") + SDL_GetError();
    } else if (filelist[0]) {
        app->pending_record_path = filelist[0];
    }
}

void request_record_wav_dialog(App& app)
{
    if (!app.pack_loaded) {
        app.model.notice = "Load a pack before starting WAV recording.";
        return;
    }
    if (app.recorder.active()) {
        stop_recording(app);
        return;
    }
    static const SDL_DialogFileFilter filters[] = {{"WAV audio", "wav"}};
    std::filesystem::path suggested = app.last_pack_directory.empty()
        ? std::filesystem::current_path() / safe_recording_stem(app)
        : std::filesystem::path(app.last_pack_directory) / safe_recording_stem(app);
    const std::string location = suggested.string();
    SDL_ShowSaveFileDialog(record_wav_dialog_callback, &app, app.window,
                           filters, 1, location.c_str());
}
#endif

void request_wav_recording(App& app)
{
#ifdef __EMSCRIPTEN__
    if (app.recorder.active()) {
        stop_recording(app);
        return;
    }
    start_recording_path(app, std::string("/hoot/recordings/") + safe_recording_stem(app));
#else
    request_record_wav_dialog(app);
#endif
}

void request_open_pack(App& app)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({ if (Module.hootOpenPackPicker) Module.hootOpenPackPicker(); });
#else
    request_open_pack_dialog(app);
#endif
}

void process_pending_open(App& app)
{
    std::string path;
    std::string record_path;
    std::string notice;
    {
        std::lock_guard<std::mutex> lock(app.pending_mutex);
        path.swap(app.pending_open_path);
        record_path.swap(app.pending_record_path);
        notice.swap(app.pending_notice);
    }
    if (!notice.empty()) app.model.notice = notice;
    if (!record_path.empty()) {
        if (std::filesystem::path(record_path).extension().empty()) record_path += ".wav";
        start_recording_path(app, record_path);
    }
    if (!path.empty()) {
        remember_pack_directory(app, path);
        load_reference(app, path, 0);
    }
}


std::pair<std::string, std::string> split_library_driver(const HootEntryInfo& entry)
{
    std::string driver = entry.driver;
    const auto slash = driver.find('/');
    if (slash == std::string::npos)
        return {driver.empty() ? std::string("other") : driver, std::string("generic")};
    std::string major = driver.substr(0, slash);
    std::string subtype = driver.substr(slash + 1);
    if (major.empty()) major = "other";
    if (subtype.empty()) subtype = "generic";
    return {major, subtype};
}

std::string library_node_key(const App& app)
{
    switch (app.library_level) {
    case LibraryLevel::Root: return "root";
    case LibraryLevel::Major: return "major:" + app.library_major;
    case LibraryLevel::Games: return "games:" + app.library_major + "/" + app.library_subtype;
    case LibraryLevel::Variants: return "variants:" + app.library_variant_archive;
    case LibraryLevel::Tracks:
        if (app.library_entry_index >= 0 && app.library_entry_index < static_cast<int>(app.catalog_entries.size()))
            return "tracks:" + std::string(app.catalog_entries[static_cast<std::size_t>(app.library_entry_index)].id);
        return "tracks:";
    }
    return "root";
}

void save_library_position(App& app)
{
    app.library_positions[library_node_key(app)] = {app.library_selected, app.library_scroll};
}

void restore_library_position(App& app)
{
    const auto it = app.library_positions.find(library_node_key(app));
    if (it == app.library_positions.end()) {
        app.library_selected = 0;
        app.library_scroll = 0;
    } else {
        app.library_selected = it->second.selected;
        app.library_scroll = it->second.scroll;
    }
}

std::string library_special_target(const HootEntryInfo& entry)
{
    const std::string title = lower_ascii(entry.title);
    if (title.find("cm-64") != std::string::npos || title.find("cm64") != std::string::npos) return "CM-64";
    if (title.find("mt-32") != std::string::npos || title.find("mt32") != std::string::npos) return "MT-32";
    if (title.find("sc-88") != std::string::npos || title.find("sc88") != std::string::npos) return "SC-88";
    if (title.find("sc-55") != std::string::npos || title.find("sc55") != std::string::npos) return "SC-55";
    if (title.find("vermouth") != std::string::npos) return "Vermouth";
    if (title.find("korg m1") != std::string::npos || title.find("(m1)") != std::string::npos) return "M1";
    if (title.find("(gs)") != std::string::npos) return "GS";
    if (title.find("(gm)") != std::string::npos) return "GM";
    return {};
}

void add_pack_directory(App& app, std::filesystem::path dir)
{
    if (dir.empty()) return;
    std::error_code ec;
    if (!dir.is_absolute()) dir = std::filesystem::absolute(dir, ec);
    if (ec || !std::filesystem::is_directory(dir, ec)) return;
    dir = dir.lexically_normal();
    if (std::find(app.pack_directories.begin(), app.pack_directories.end(), dir) == app.pack_directories.end())
        app.pack_directories.push_back(std::move(dir));
}

void refresh_pack_index(App& app)
{
    app.pack_index.clear();
    if (!app.last_pack_directory.empty()) add_pack_directory(app, app.last_pack_directory);

    // Prefer the directory the user most recently opened. The configured
    // packs= directory remains a fallback and can coexist with it.
    std::vector<std::filesystem::path> scan_dirs;
    if (!app.last_pack_directory.empty()) scan_dirs.emplace_back(app.last_pack_directory);
    for (const auto& dir : app.pack_directories) {
        if (std::find(scan_dirs.begin(), scan_dirs.end(), dir) == scan_dirs.end()) scan_dirs.push_back(dir);
    }
    for (const auto& dir : scan_dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) continue;

        // A packs root commonly contains platform/publisher/category folders.
        // Scan the complete tree so e.g. packs/x68000/konami/ad68snd.zip is
        // discovered exactly like packs/ad68snd.zip. We deliberately do not
        // enable follow_directory_symlink, which avoids symlink recursion loops.
        // Root precedence is preserved by scan_dirs and the first matching pack
        // name wins, so the remembered Open directory still overrides packs=.
        std::filesystem::recursive_directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) {
                // A denied/broken child must not abort the rest of the root.
                ec.clear();
                continue;
            }
            const auto path = it->path();
            if (it->is_directory(ec)) {
                if (ec) ec.clear();
                continue; // recurse into it, but a directory is not itself a Hoot pack
            }
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec)) { if (ec) ec.clear(); continue; }
            if (ec) { ec.clear(); continue; }
            if (lower_ascii(path.extension().string()) != ".zip") continue;

            const std::string key = lower_ascii(path.stem().string());
            if (!key.empty() && app.pack_index.find(key) == app.pack_index.end())
                app.pack_index.emplace(key, path);
        }
    }
}

const std::filesystem::path* library_pack_path(const App& app, const HootEntryInfo& entry)
{
    const auto it = app.pack_index.find(lower_ascii(entry.archive));
    return it == app.pack_index.end() ? nullptr : &it->second;
}

const HootDriverProbe* library_probe(App& app, const HootEntryInfo& entry)
{
    const std::string key = entry.id;
    const auto found = app.probe_cache.find(key);
    if (found != app.probe_cache.end()) return &found->second;
    HootDriverProbe probe{};
    if (hoot_probe_entry(app.hoot, entry.id, &probe) != HOOT_OK) return nullptr;
    return &app.probe_cache.emplace(key, probe).first->second;
}

std::string library_support_label(App& app, const HootEntryInfo& entry)
{
    if (const auto* probe = library_probe(app, entry)) {
        std::string label = hoot_support_status_name(probe->status);
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return label;
    }
    return "UNKNOWN";
}

bool library_search_match(const std::string& query, const std::string& a, const std::string& b = {})
{
    if (query.empty()) return true;
    const std::string needle = lower_ascii(query);
    return lower_ascii(a).find(needle) != std::string::npos || lower_ascii(b).find(needle) != std::string::npos;
}

bool library_entry_visible(const App& app, const HootEntryInfo& entry)
{
    return !app.library_available_only || library_pack_path(app, entry) != nullptr;
}

void clamp_library_cursor(App& app)
{
    const int size = static_cast<int>(app.library_rows.size());
    if (size <= 0) {
        app.library_selected = 0;
        app.library_scroll = 0;
        return;
    }
    app.library_selected = std::clamp(app.library_selected, 0, size - 1);
    const int max_scroll = std::max(0, size - hootgui::RetroRenderer::kLibraryVisibleRows);
    app.library_scroll = std::clamp(app.library_scroll, 0, max_scroll);
    if (app.library_selected < app.library_scroll) app.library_scroll = app.library_selected;
    else if (app.library_selected >= app.library_scroll + hootgui::RetroRenderer::kLibraryVisibleRows)
        app.library_scroll = app.library_selected - hootgui::RetroRenderer::kLibraryVisibleRows + 1;
}

void rebuild_library_rows(App& app)
{
    app.library_rows.clear();
    const std::string query = app.library_search;

    if (app.library_level == LibraryLevel::Root) {
        int total = 0;
        std::unordered_map<std::string, int> counts;
        for (const auto& entry : app.catalog_entries) {
            if (!library_entry_visible(app, entry)) continue;
            ++total;
            ++counts[split_library_driver(entry).first];
        }
        if (library_search_match(query, "- all -"))
            app.library_rows.push_back({hootgui::LibraryRowKind::Folder, "- all -", std::to_string(total) + " games", "folder", -1, true});
        std::vector<std::string> majors;
        majors.reserve(counts.size());
        for (const auto& pair : counts) majors.push_back(pair.first);
        std::sort(majors.begin(), majors.end());
        for (const auto& major : majors) {
            if (!library_search_match(query, major)) continue;
            app.library_rows.push_back({hootgui::LibraryRowKind::Folder, major,
                                        std::to_string(counts[major]) + " games", "folder", -1, true});
        }
    } else if (app.library_level == LibraryLevel::Major) {
        std::unordered_map<std::string, int> counts;
        int total = 0;
        for (const auto& entry : app.catalog_entries) {
            if (!library_entry_visible(app, entry)) continue;
            const auto parts = split_library_driver(entry);
            if (parts.first != app.library_major) continue;
            ++total;
            ++counts[parts.second];
        }
        if (library_search_match(query, "- all -"))
            app.library_rows.push_back({hootgui::LibraryRowKind::Folder, "- all -", std::to_string(total) + " games", "folder", -1, true});
        std::vector<std::string> types;
        for (const auto& pair : counts) types.push_back(pair.first);
        std::sort(types.begin(), types.end());
        for (const auto& type : types) {
            if (!library_search_match(query, type)) continue;
            app.library_rows.push_back({hootgui::LibraryRowKind::Folder, type,
                                        std::to_string(counts[type]) + " games", "folder", -1, true});
        }
    } else if (app.library_level == LibraryLevel::Games) {
        for (std::size_t i = 0; i < app.catalog_entries.size(); ++i) {
            const auto& entry = app.catalog_entries[i];
            if (!library_entry_visible(app, entry)) continue;
            const auto parts = split_library_driver(entry);
            if (!app.library_major.empty() && parts.first != app.library_major) continue;
            if (app.library_subtype != "*" && parts.second != app.library_subtype) continue;
            if (!library_search_match(query, entry.title, entry.archive)) continue;
            const auto* pack = library_pack_path(app, entry);
            const bool available = pack != nullptr;
            std::string status = available ? "PACK FOUND" : "missing pack";
            status += " · " + library_support_label(app, entry);
            if (const auto special = library_special_target(entry); !special.empty()) status += " · " + special;
            if (app.user_overrides.entries.find(entry.id) != app.user_overrides.entries.end()) status += " · EDITED";
            app.library_rows.push_back({hootgui::LibraryRowKind::Entry, entry.title,
                                        std::string(entry.archive) + ".zip", status,
                                        static_cast<int>(i), available});
        }
    } else if (app.library_level == LibraryLevel::Variants) {
        const std::string archive_key = lower_ascii(app.library_variant_archive);
        for (std::size_t i = 0; i < app.catalog_entries.size(); ++i) {
            const auto& entry = app.catalog_entries[i];
            if (lower_ascii(entry.archive) != archive_key) continue;
            if (!library_entry_visible(app, entry)) continue;
            if (!library_search_match(query, entry.title, entry.id)) continue;
            const auto* pack = library_pack_path(app, entry);
            const bool available = pack != nullptr;
            std::string status = library_support_label(app, entry);
            if (const auto special = library_special_target(entry); !special.empty()) status += " · " + special;
            else status += " · default hardware";
            if (app.user_overrides.entries.find(entry.id) != app.user_overrides.entries.end()) status += " · EDITED";
            app.library_rows.push_back({hootgui::LibraryRowKind::Entry, entry.title,
                                        entry.id, status, static_cast<int>(i), available});
        }
    } else if (app.library_level == LibraryLevel::Tracks) {
        for (std::size_t i = 0; i < app.model.tracks.size(); ++i) {
            const auto& track = app.model.tracks[i];
            if (!library_search_match(query, track.title)) continue;
            char code[32];
            std::snprintf(code, sizeof(code), "$%04X", static_cast<unsigned>(track.code));
            std::string label = (static_cast<int>(i) == app.model.selected_track ? "* " : "  ")
                + std::to_string(i + 1) + ". " + std::string(track.title);
            app.library_rows.push_back({hootgui::LibraryRowKind::Track, label, code,
                                        static_cast<int>(i) == app.model.selected_track ? "PLAYING" : "track",
                                        static_cast<int>(i), true});
        }
    }
    clamp_library_cursor(app);
}

std::string library_breadcrumb(const App& app)
{
    switch (app.library_level) {
    case LibraryLevel::Root: return "/";
    case LibraryLevel::Major: return "/ " + app.library_major;
    case LibraryLevel::Games:
        if (app.library_major.empty()) return "/ - all -";
        return "/ " + app.library_major + " / " + (app.library_subtype == "*" ? "- all -" : app.library_subtype);
    case LibraryLevel::Variants:
        return "/ Choose playback variant / " + app.library_variant_archive + ".zip";
    case LibraryLevel::Tracks:
        if (app.library_entry_index >= 0 && app.library_entry_index < static_cast<int>(app.catalog_entries.size()))
            return "/ " + app.library_major + " / " + (app.library_subtype == "*" ? "- all -" : app.library_subtype)
                + " / " + std::string(app.catalog_entries[static_cast<std::size_t>(app.library_entry_index)].title);
        return "/ tracks";
    }
    return "/";
}

void stop_library_search(App& app)
{
    if (!app.library_search_editing) return;
    app.library_search_editing = false;
    if (app.window) SDL_StopTextInput(app.window);
}

void start_library_search(App& app)
{
    app.library_search_editing = true;
    if (app.window) SDL_StartTextInput(app.window);
}

void open_library(App& app)
{
    if (app.settings_open) return;
    refresh_pack_index(app);
    app.library_open = true;
    app.library_message.clear();
    app.library_search.clear();
    restore_library_position(app);
    rebuild_library_rows(app);
}

void toggle_library_available_only(App& app)
{
    app.library_available_only = !app.library_available_only;
    app.library_selected = 0;
    app.library_scroll = 0;
    rebuild_library_rows(app);
#ifndef __EMSCRIPTEN__
    save_ui_state(app);
#endif
}

void close_library(App& app)
{
    stop_library_search(app);
    if (!app.library_search.empty()) {
        app.library_search.clear();
        rebuild_library_rows(app);
    }
    save_library_position(app);
    app.library_open = false;
    app.library_message.clear();
}

void set_library_level(App& app, LibraryLevel level, std::string major = {}, std::string subtype = {})
{
    save_library_position(app);
    stop_library_search(app);
    app.library_search.clear();
    app.library_message.clear();
    app.library_level = level;
    if (level == LibraryLevel::Root) {
        app.library_major.clear();
        app.library_subtype.clear();
        app.library_variant_archive.clear();
        app.library_entry_index = -1;
    } else {
        if (!major.empty() || level == LibraryLevel::Games) app.library_major = std::move(major);
        if (!subtype.empty() || level == LibraryLevel::Games) app.library_subtype = std::move(subtype);
        if (level != LibraryLevel::Tracks) app.library_entry_index = -1;
    }
    restore_library_position(app);
    rebuild_library_rows(app);
}

void library_back(App& app)
{
    if (app.library_search_editing) { stop_library_search(app); return; }
    if (app.library_level == LibraryLevel::Tracks) {
        if (!app.library_variant_archive.empty()) {
            save_library_position(app);
            app.library_level = LibraryLevel::Variants;
            app.library_entry_index = -1;
            restore_library_position(app);
            rebuild_library_rows(app);
        } else {
            set_library_level(app, LibraryLevel::Games, app.library_major, app.library_subtype);
        }
    } else if (app.library_level == LibraryLevel::Variants) {
        close_library(app);
        app.library_level = LibraryLevel::Root;
        app.library_variant_archive.clear();
        app.library_major.clear();
        app.library_subtype.clear();
        app.library_entry_index = -1;
    } else if (app.library_level == LibraryLevel::Games) {
        if (app.library_major.empty()) set_library_level(app, LibraryLevel::Root);
        else set_library_level(app, LibraryLevel::Major, app.library_major);
    } else if (app.library_level == LibraryLevel::Major) {
        set_library_level(app, LibraryLevel::Root);
    } else {
        close_library(app);
    }
}

void move_library_selection(App& app, int delta)
{
    if (app.library_rows.empty()) return;
    app.library_selected = std::clamp(app.library_selected + delta, 0, static_cast<int>(app.library_rows.size()) - 1);
    clamp_library_cursor(app);
}

void activate_library_row(App& app, bool play_and_advance)
{
    if (app.library_rows.empty() || app.library_selected < 0 || app.library_selected >= static_cast<int>(app.library_rows.size())) return;
    const auto row = app.library_rows[static_cast<std::size_t>(app.library_selected)];
    if (app.library_level == LibraryLevel::Root && row.kind == hootgui::LibraryRowKind::Folder) {
        if (row.label == "- all -") set_library_level(app, LibraryLevel::Games, "", "*");
        else set_library_level(app, LibraryLevel::Major, row.label);
        return;
    }
    if (app.library_level == LibraryLevel::Major && row.kind == hootgui::LibraryRowKind::Folder) {
        set_library_level(app, LibraryLevel::Games, app.library_major, row.label == "- all -" ? "*" : row.label);
        return;
    }
    if ((app.library_level == LibraryLevel::Games || app.library_level == LibraryLevel::Variants) &&
        row.kind == hootgui::LibraryRowKind::Entry) {
        if (row.payload < 0 || row.payload >= static_cast<int>(app.catalog_entries.size())) return;
        const auto& entry = app.catalog_entries[static_cast<std::size_t>(row.payload)];
        if (const auto* pack = library_pack_path(app, entry)) {
            const auto parent = std::filesystem::is_directory(*pack) ? pack->parent_path() : pack->parent_path();
            if (!parent.empty()) hoot_set_packs_path(app.hoot, parent.string().c_str());
        }
        if (!load_reference(app, entry.id, 0)) {
            app.library_message = row.available
                ? app.model.notice
                : ("Pack not found. Put " + std::string(entry.archive) + ".zip in the configured/last-used pack directory or use Open...");
            return;
        }
        save_library_position(app);
        app.library_level = LibraryLevel::Tracks;
        app.library_entry_index = row.payload;
        app.library_search.clear();
        restore_library_position(app);
        rebuild_library_rows(app);
        return;
    }
    if (app.library_level == LibraryLevel::Tracks && row.kind == hootgui::LibraryRowKind::Track) {
        if (switch_track(app, row.payload)) {
            app.library_message.clear();
            rebuild_library_rows(app);
            // Classic Hoot: Space plays and advances the selector one item.
            if (play_and_advance) move_library_selection(app, 1);
        } else {
            app.library_message = app.model.notice;
        }
    }
}




void refresh_catalog_entries(App& app)
{
    app.probe_cache.clear();
    const int count = hoot_get_entry_count(app.hoot);
    app.catalog_entries.assign(static_cast<std::size_t>(std::max(0, count)), HootEntryInfo{});
    for (int i = 0; i < count; ++i)
        hoot_get_entry_info(app.hoot, i, &app.catalog_entries[static_cast<std::size_t>(i)]);
}

bool build_editor_entry(App& app, int entry_index, hoot::HootEntryOverride& out, std::string& error)
{
    HootEntryInfo info{};
    if (hoot_get_entry_info(app.hoot, entry_index, &info) != HOOT_OK) {
        error = "Unable to read catalog entry";
        return false;
    }
    out = {};
    out.id = info.id;
    out.has_title = true; out.title = info.title;
    out.has_archive = true; out.archive = info.archive;
    const auto parts = split_library_driver(info);
    out.has_driver_name = true; out.driver_name = parts.first;
    out.has_driver_type = true; out.driver_type = parts.second == "generic" && std::string(info.driver).find('/') == std::string::npos ? "" : parts.second;
    HootEntryDriverInfo driver_extra{};
    if (hoot_get_entry_driver_info(app.hoot, entry_index, &driver_extra) == HOOT_OK) {
        out.has_driver_alias = true;
        out.driver_alias = driver_extra.alias;
    }
    out.has_default_sample_rate = true; out.default_sample_rate = info.default_sample_rate;
    out.has_refresh_hz = true; out.refresh_hz = info.refresh_hz;
    out.replace_options = true;
    const int option_count = hoot_get_entry_option_count(app.hoot, entry_index);
    for (int i = 0; i < option_count; ++i) {
        HootEntryOptionInfo option{};
        if (hoot_get_entry_option_info(app.hoot, entry_index, i, &option) == HOOT_OK)
            out.options[option.name] = option.value;
    }
    out.replace_assets = true;
    const int asset_count = hoot_get_entry_asset_count(app.hoot, entry_index);
    for (int i = 0; i < asset_count; ++i) {
        HootEntryAssetInfo asset{};
        if (hoot_get_entry_asset_info(app.hoot, entry_index, i, &asset) != HOOT_OK) continue;
        hoot::HootAssetRef item;
        item.type = asset.type; item.path = asset.path; item.transform = asset.transform;
        item.offset = asset.offset; item.crc32 = asset.crc32; item.has_crc32 = asset.has_crc32 != 0;
        out.assets.push_back(std::move(item));
    }
    out.replace_tracks = true;
    for (int i = 0; i < info.track_count; ++i) {
        HootEntryTrackInfo track{};
        if (hoot_get_entry_catalog_track_info(app.hoot, entry_index, i, &track) != HOOT_OK) continue;
        out.tracks.push_back({track.code, track.title, track.voice_bank});
    }
    return true;
}

void stop_catalog_editor_edit(App& app, bool commit = true)
{
    if (!app.catalog_editor_editing) return;
    if (commit) {
        auto parse_int = [&](int& target) {
            char* end = nullptr;
            const long value = std::strtol(app.catalog_editor_buffer.c_str(), &end, 0);
            if (end != app.catalog_editor_buffer.c_str() && *end == '\0') target = static_cast<int>(value);
            else app.catalog_editor_message = "Invalid integer: " + app.catalog_editor_buffer;
        };
        const int row = app.catalog_editor_selected;
        switch (app.catalog_editor_field) {
        case CatalogEditorField::GeneralTitle: app.catalog_editor_entry.title = app.catalog_editor_buffer; break;
        case CatalogEditorField::GeneralArchive: app.catalog_editor_entry.archive = app.catalog_editor_buffer; break;
        case CatalogEditorField::GeneralDriverName: app.catalog_editor_entry.driver_name = app.catalog_editor_buffer; break;
        case CatalogEditorField::GeneralDriverType: app.catalog_editor_entry.driver_type = app.catalog_editor_buffer; break;
        case CatalogEditorField::GeneralSampleRate: parse_int(app.catalog_editor_entry.default_sample_rate); break;
        case CatalogEditorField::GeneralRefresh: parse_int(app.catalog_editor_entry.refresh_hz); break;
        case CatalogEditorField::TrackCode:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.tracks.size())) {
                int value = static_cast<int>(app.catalog_editor_entry.tracks[static_cast<size_t>(row)].code); parse_int(value);
                app.catalog_editor_entry.tracks[static_cast<size_t>(row)].code = static_cast<uint32_t>(std::max(0, value));
            } break;
        case CatalogEditorField::TrackTitle:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.tracks.size())) app.catalog_editor_entry.tracks[static_cast<size_t>(row)].title = app.catalog_editor_buffer;
            break;
        case CatalogEditorField::TrackVoice:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.tracks.size())) app.catalog_editor_entry.tracks[static_cast<size_t>(row)].voice_bank = app.catalog_editor_buffer;
            break;
        case CatalogEditorField::OptionValue: {
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.options.size())) {
                auto it = app.catalog_editor_entry.options.begin(); std::advance(it, row);
                int value = it->second; parse_int(value); it->second = value;
            }
            break;
        }
        case CatalogEditorField::OptionName: {
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.options.size()) && !app.catalog_editor_buffer.empty()) {
                auto it = app.catalog_editor_entry.options.begin(); std::advance(it, row);
                const int value = it->second;
                app.catalog_editor_entry.options.erase(it);
                app.catalog_editor_entry.options[app.catalog_editor_buffer] = value;
            }
            break;
        }
        case CatalogEditorField::AssetType:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.assets.size())) app.catalog_editor_entry.assets[static_cast<size_t>(row)].type = app.catalog_editor_buffer;
            break;
        case CatalogEditorField::AssetPath:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.assets.size())) app.catalog_editor_entry.assets[static_cast<size_t>(row)].path = app.catalog_editor_buffer;
            break;
        case CatalogEditorField::AssetTransform:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.assets.size())) app.catalog_editor_entry.assets[static_cast<size_t>(row)].transform = app.catalog_editor_buffer;
            break;
        case CatalogEditorField::AssetOffset:
            if (row >= 0 && row < static_cast<int>(app.catalog_editor_entry.assets.size())) {
                int value = static_cast<int>(app.catalog_editor_entry.assets[static_cast<size_t>(row)].offset); parse_int(value);
                app.catalog_editor_entry.assets[static_cast<size_t>(row)].offset = static_cast<uint32_t>(std::max(0, value));
            } break;
        default: break;
        }
        app.catalog_editor_dirty = true;
    }
    app.catalog_editor_editing = false;
    app.catalog_editor_field = CatalogEditorField::None;
    app.catalog_editor_buffer.clear();
    if (app.window) SDL_StopTextInput(app.window);
}

void begin_catalog_editor_edit(App& app, CatalogEditorField field, std::string value, int column = 0)
{
    stop_catalog_editor_edit(app, true);
    app.catalog_editor_field = field;
    app.catalog_editor_buffer = std::move(value);
    app.catalog_editor_edit_column = column;
    app.catalog_editor_editing = true;
    if (app.window) SDL_StartTextInput(app.window);
}

void open_catalog_editor(App& app, int entry_index)
{
    if (entry_index < 0 || entry_index >= static_cast<int>(app.catalog_entries.size())) return;
    stop_library_search(app);
    std::string error;
    if (!hoot::load_hoot_user_overrides(app.user_overrides_path, app.user_overrides, error)) {
        app.library_message = error; return;
    }
    if (!build_editor_entry(app, entry_index, app.catalog_editor_entry, error)) {
        app.library_message = error; return;
    }
    app.catalog_editor_entry_index = entry_index;
    app.catalog_editor_open = true;
    app.catalog_editor_dirty = false;
    app.catalog_editor_editing = false;
    app.catalog_editor_tab = "general";
    app.catalog_editor_selected = 0;
    app.catalog_editor_scroll = 0;
    app.catalog_editor_message.clear();
    const auto existing = app.user_overrides.entries.find(app.catalog_editor_entry.id);
    app.catalog_editor_locally_modified = existing != app.user_overrides.entries.end();
    if (existing != app.user_overrides.entries.end()) {
        // A locally created variant must keep create=true on every subsequent
        // save; otherwise the next catalogue reload would correctly reject it
        // as an override of an unknown upstream id.
        app.catalog_editor_entry.create = existing->second.create;
        if (existing->second.has_driver_alias) {
            app.catalog_editor_entry.has_driver_alias = true;
            app.catalog_editor_entry.driver_alias = existing->second.driver_alias;
        }
    }
}

void close_catalog_editor(App& app)
{
    stop_catalog_editor_edit(app, false);
    app.catalog_editor_open = false;
    app.catalog_editor_message.clear();
}

bool reload_catalog_after_edit(App& app, const std::string& entry_id, int preferred_track)
{
    const std::string playing_id = app.pack_loaded ? std::string(app.model.entry.id) : std::string{};
    const int playing_track = app.model.selected_track;
    if (hoot_load_catalog(app.hoot, app.catalog_path.c_str()) != HOOT_OK) {
        app.catalog_editor_message = std::string("Catalog reload failed: ") + hoot_last_error(app.hoot);
        return false;
    }
    refresh_catalog_entries(app);
    refresh_pack_index(app);
    HootEntryInfo refreshed{};
    if (hoot_find_entry(app.hoot, entry_id.c_str(), &refreshed) == HOOT_OK)
        app.catalog_editor_entry_index = refreshed.index;

    // hoot_load_catalog deliberately clears the active driver. Restore whatever
    // the user was listening to, even when they edited a different Library
    // entry, so saving metadata never silently stops playback.
    if (!playing_id.empty()) {
        HootEntryInfo playing_info{};
        if (hoot_find_entry(app.hoot, playing_id.c_str(), &playing_info) != HOOT_OK) {
            app.pack_loaded = false;
            app.model.tracks.clear();
            app.model.warning.clear();
            app.model.warning_help.clear();
            app.model.warning_overlay_visible = false;
            app.model.notice = "The previously playing local entry was removed from the catalog.";
        } else {
            const int track = playing_id == entry_id ? preferred_track : playing_track;
            if (!load_reference(app, playing_id, std::max(0, track))) {
                app.catalog_editor_message = "Catalog saved, but the previously playing entry could not be reopened: " + app.model.notice;
            }
        }
    }
    rebuild_library_rows(app);
    return true;
}

void save_catalog_editor(App& app)
{
    stop_catalog_editor_edit(app, true);
    auto& e = app.catalog_editor_entry;
    if (e.title.empty() || e.driver_name.empty() || e.default_sample_rate <= 0 || e.refresh_hz <= 0) {
        app.catalog_editor_message = "Title, driver and positive sample/refresh rates are required.";
        return;
    }
    app.user_overrides.entries[e.id] = e;
    std::string error;
    if (!hoot::save_hoot_user_overrides(app.user_overrides_path, app.user_overrides, error)) {
        app.catalog_editor_message = error; return;
    }
    sync_web_persistence();
    const int previous_track = app.model.selected_track;
    if (!reload_catalog_after_edit(app, e.id, previous_track)) return;
    app.catalog_editor_dirty = false;
    app.catalog_editor_locally_modified = true;
    app.catalog_editor_message = "Saved to local override and applied immediately.";
    HootDriverProbe probe{};
    if (hoot_probe_entry(app.hoot, e.id.c_str(), &probe) == HOOT_OK && probe.status == HOOT_SUPPORT_UNSUPPORTED)
        app.catalog_editor_message += std::string(" Driver warning: ") + probe.reason;
}


void duplicate_catalog_editor(App& app)
{
    stop_catalog_editor_edit(app, true);
    auto duplicate = app.catalog_editor_entry;
    const std::string stem = duplicate.id + "-user";
    std::string id = stem;
    int suffix = 2;
    auto exists = [&](const std::string& candidate) {
        if (app.user_overrides.entries.find(candidate) != app.user_overrides.entries.end()) return true;
        return std::any_of(app.catalog_entries.begin(), app.catalog_entries.end(), [&](const HootEntryInfo& info) {
            return candidate == info.id;
        });
    };
    while (exists(id)) id = stem + "-" + std::to_string(suffix++);
    duplicate.id = id;
    duplicate.create = true;
    duplicate.hidden = false;
    duplicate.title += " (User variant)";
    app.catalog_editor_entry = std::move(duplicate);
    app.catalog_editor_entry_index = -1;
    app.catalog_editor_dirty = true;
    app.catalog_editor_locally_modified = false;
    app.catalog_editor_selected = 0;
    app.catalog_editor_scroll = 0;
    app.catalog_editor_tab = "general";
    app.catalog_editor_message = "New local variant. Edit it and press Save to add it to the Library.";
}

void reset_catalog_editor(App& app)
{
    stop_catalog_editor_edit(app, false);
    if (app.catalog_editor_entry.create && !app.catalog_editor_locally_modified) {
        close_catalog_editor(app);
        app.library_message = "Unsaved duplicate discarded.";
        return;
    }
    const bool removing_local_variant = app.catalog_editor_entry.create;
    const std::string id = app.catalog_editor_entry.id;
    app.user_overrides.entries.erase(id);
    std::string error;
    if (!hoot::save_hoot_user_overrides(app.user_overrides_path, app.user_overrides, error)) {
        app.catalog_editor_message = error; return;
    }
    sync_web_persistence();
    if (!reload_catalog_after_edit(app, id, app.model.selected_track)) return;
    if (removing_local_variant) {
        close_catalog_editor(app);
        app.library_message = "Local variant removed; the upstream catalog was not changed.";
        return;
    }
    HootEntryInfo info{};
    if (hoot_find_entry(app.hoot, id.c_str(), &info) != HOOT_OK || !build_editor_entry(app, info.index, app.catalog_editor_entry, error)) {
        app.catalog_editor_message = "Reset saved, but entry could not be reloaded."; return;
    }
    app.catalog_editor_entry_index = info.index;
    app.catalog_editor_dirty = false;
    app.catalog_editor_locally_modified = false;
    app.catalog_editor_message = "Reset to base catalog entry.";
}

void cycle_hardware_target(App& app, int delta)
{
    static const int values[] = {-1, 1, 2, 3, 4, 5, 6, 7, 8};
    int current = -1;
    if (auto it = app.catalog_editor_entry.options.find("midiout_type"); it != app.catalog_editor_entry.options.end()) current = it->second;
    int pos = 0;
    for (int i = 0; i < 9; ++i) if (values[i] == current) { pos = i; break; }
    pos = (pos + delta + 9) % 9;
    if (values[pos] < 0) app.catalog_editor_entry.options.erase("midiout_type");
    else app.catalog_editor_entry.options["midiout_type"] = values[pos];
    app.catalog_editor_dirty = true;
}

void editor_select_tab(App& app, const std::string& tab)
{
    stop_catalog_editor_edit(app, true);
    app.catalog_editor_tab = tab;
    app.catalog_editor_selected = 0;
    app.catalog_editor_scroll = 0;
}

int editor_row_count(const App& app)
{
    if (app.catalog_editor_tab == "general") return 6;
    if (app.catalog_editor_tab == "tracks") return static_cast<int>(app.catalog_editor_entry.tracks.size());
    if (app.catalog_editor_tab == "options") return static_cast<int>(app.catalog_editor_entry.options.size());
    if (app.catalog_editor_tab == "assets") return static_cast<int>(app.catalog_editor_entry.assets.size());
    if (app.catalog_editor_tab == "hardware") return 1;
    return 0;
}

void clamp_editor_selection(App& app)
{
    const int count = editor_row_count(app);
    if (count <= 0) { app.catalog_editor_selected = 0; app.catalog_editor_scroll = 0; return; }
    app.catalog_editor_selected = std::clamp(app.catalog_editor_selected, 0, count - 1);
    const int visible = app.catalog_editor_tab == "general" ? 6 : 16;
    const int max_scroll = std::max(0, count - visible);
    app.catalog_editor_scroll = std::clamp(app.catalog_editor_scroll, 0, max_scroll);
    if (app.catalog_editor_selected < app.catalog_editor_scroll) app.catalog_editor_scroll = app.catalog_editor_selected;
    if (app.catalog_editor_selected >= app.catalog_editor_scroll + visible) app.catalog_editor_scroll = app.catalog_editor_selected - visible + 1;
}

std::vector<int> settings_indices(const App& app)
{
    std::vector<int> result;
    for (size_t i = 0; i < app.settings_document.values.size(); ++i) {
        const auto& item = app.settings_document.values[i];
        if (item.spec && app.settings_section == item.spec->section) result.push_back(static_cast<int>(i));
    }
    return result;
}

void stop_settings_edit(App& app)
{
    if (!app.settings_editing) return;
    app.settings_editing = false;
    if (app.window) SDL_StopTextInput(app.window);
}

void select_first_setting(App& app)
{
    const auto rows = settings_indices(app);
    app.settings_selected = rows.empty() ? -1 : rows.front();
    app.settings_scroll = 0;
}

void open_settings(App& app)
{
    stop_settings_edit(app);
    std::string error;
    if (!hoot::load_hoot_settings_document(app.settings_config_path, app.settings_document, error)) {
        hoot::reset_hoot_settings(app.settings_document);
        app.settings_message = error;
    } else {
        app.settings_message.clear();
    }
    app.settings_open = true;
    app.settings_dirty = false;
    app.settings_section = "player";
    select_first_setting(app);
}

void close_settings(App& app)
{
    stop_settings_edit(app);
    app.settings_open = false;
    app.settings_dirty = false;
    app.settings_message.clear();
}

void save_settings(App& app)
{
    stop_settings_edit(app);
    std::string error;
    if (!hoot::save_hoot_settings_document(app.settings_config_path, app.settings_document, error)) {
        app.settings_message = error;
        return;
    }
    app.settings_dirty = false;
    sync_web_persistence();
    app.settings_message = "Saved. Restart to apply runtime/backend changes.";
}

void pop_utf8(std::string& value)
{
    if (value.empty()) return;
    size_t pos = value.size() - 1;
    while (pos > 0 && (static_cast<unsigned char>(value[pos]) & 0xc0u) == 0x80u) --pos;
    value.erase(pos);
}

bool setting_bool_value(const std::string& value)
{
    std::string text = value;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

void cycle_setting_value(hoot::HootSettingValue& item, int delta = 1)
{
    if (!item.spec) return;
    if (item.spec->kind == hoot::HootSettingKind::Boolean) {
        item.value = setting_bool_value(item.value) ? "false" : "true";
        return;
    }
    if (item.spec->kind != hoot::HootSettingKind::Choice || !item.spec->choices[0]) return;
    std::vector<std::string> choices;
    std::string all = item.spec->choices;
    size_t start = 0;
    while (start <= all.size()) {
        const auto sep = all.find('|', start);
        choices.push_back(all.substr(start, sep == std::string::npos ? std::string::npos : sep - start));
        if (sep == std::string::npos) break;
        start = sep + 1;
    }
    if (choices.empty()) return;
    auto it = std::find(choices.begin(), choices.end(), item.value);
    int index = it == choices.end() ? 0 : static_cast<int>(it - choices.begin());
    index = (index + delta) % static_cast<int>(choices.size());
    if (index < 0) index += static_cast<int>(choices.size());
    item.value = choices[static_cast<size_t>(index)];
}

void begin_setting_edit(App& app, int index)
{
    if (index < 0 || index >= static_cast<int>(app.settings_document.values.size())) return;
    auto& item = app.settings_document.values[static_cast<size_t>(index)];
    if (!item.spec || item.spec->kind == hoot::HootSettingKind::Boolean || item.spec->kind == hoot::HootSettingKind::Choice) return;
    app.settings_selected = index;
    app.settings_editing = true;
    item.enabled = true;
    app.settings_dirty = true;
    if (app.window) SDL_StartTextInput(app.window);
}

void move_setting_selection(App& app, int delta)
{
    const auto rows = settings_indices(app);
    if (rows.empty()) { app.settings_selected = -1; return; }
    auto it = std::find(rows.begin(), rows.end(), app.settings_selected);
    int position = it == rows.end() ? 0 : static_cast<int>(it - rows.begin());
    position = std::clamp(position + delta, 0, static_cast<int>(rows.size()) - 1);
    app.settings_selected = rows[static_cast<size_t>(position)];
    constexpr int visible = 13;
    if (position < app.settings_scroll) app.settings_scroll = position;
    else if (position >= app.settings_scroll + visible) app.settings_scroll = position - visible + 1;
}

bool handle_settings_event(App& app, const SDL_Event& e)
{
    if (!app.settings_open) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT && app.settings_editing && app.settings_selected >= 0) {
        auto& item = app.settings_document.values[static_cast<size_t>(app.settings_selected)];
        if (e.text.text) item.value += e.text.text;
        app.settings_dirty = true;
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_WHEEL && app.settings_section != "environment") {
        const auto rows = settings_indices(app);
        const int max_scroll = std::max(0, static_cast<int>(rows.size()) - 13);
        app.settings_scroll = std::clamp(app.settings_scroll - static_cast<int>(e.wheel.y), 0, max_scroll);
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float x = 0.0f, y = 0.0f;
        if (!SDL_RenderCoordinatesFromWindow(app.renderer, e.button.x, e.button.y, &x, &y)) return true;
        constexpr float px = 110.0f, py = 56.0f, pw = 1220.0f, ph = 788.0f;
        constexpr float tabs_x = px + 16.0f, tabs_w = 176.0f;
        constexpr float content_x = px + 210.0f, content_w = pw - 226.0f;
        constexpr float rows_y = py + 96.0f, row_h = 40.0f;
        static const char* tabs[] = {"general","player","gui","midi","x68k","psg","pc98","pc88","environment"};

        if (x >= tabs_x && x < tabs_x + tabs_w) {
            for (int i = 0; i < 9; ++i) {
                const float ty = py + 72.0f + i * 42.0f;
                if (y >= ty - 5.0f && y < ty + 26.0f) {
                    stop_settings_edit(app);
                    app.settings_section = tabs[i];
                    select_first_setting(app);
                    return true;
                }
            }
        }

        const float footer_y = py + ph - 66.0f;
        if (y >= footer_y - 8 && y < footer_y + 24) {
            if (x >= px + pw - 210 && x < px + pw - 124) { save_settings(app); return true; }
            if (x >= px + pw - 112 && x < px + pw - 26) { close_settings(app); return true; }
        }

        if (app.settings_section == "environment") return true;
        if (y >= rows_y && y < rows_y + 13 * row_h) {
            const auto rows = settings_indices(app);
            const int row = static_cast<int>((y - rows_y) / row_h);
            const int local = app.settings_scroll + row;
            if (local < 0 || local >= static_cast<int>(rows.size())) return true;
            const int index = rows[static_cast<size_t>(local)];
            auto& item = app.settings_document.values[static_cast<size_t>(index)];
            stop_settings_edit(app);
            app.settings_selected = index;
            if (x >= content_x + 8 && x < content_x + 55) {
                item.enabled = !item.enabled;
                app.settings_dirty = true;
                return true;
            }
            if (x >= content_x + 345 && x < content_x + content_w) {
                item.enabled = true;
                if (item.spec->kind == hoot::HootSettingKind::Boolean || item.spec->kind == hoot::HootSettingKind::Choice) {
                    cycle_setting_value(item);
                    app.settings_dirty = true;
                } else begin_setting_edit(app, index);
                return true;
            }
        }
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return true;
    const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
    if (app.settings_editing && app.settings_selected >= 0) {
        auto& item = app.settings_document.values[static_cast<size_t>(app.settings_selected)];
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            stop_settings_edit(app);
        } else if (e.key.key == SDLK_BACKSPACE) {
            pop_utf8(item.value); app.settings_dirty = true;
        } else if (ctrl && e.key.key == SDLK_A) {
            item.value.clear(); app.settings_dirty = true;
        } else if (ctrl && e.key.key == SDLK_V) {
            char* clip = SDL_GetClipboardText();
            if (clip) { item.value += clip; SDL_free(clip); app.settings_dirty = true; }
        }
        return true;
    }

    if (ctrl && e.key.key == SDLK_S) { save_settings(app); return true; }
    switch (e.key.key) {
    case SDLK_ESCAPE: close_settings(app); break;
    case SDLK_UP: move_setting_selection(app, -1); break;
    case SDLK_DOWN: move_setting_selection(app, 1); break;
    case SDLK_TAB: move_setting_selection(app, (e.key.mod & SDL_KMOD_SHIFT) ? -1 : 1); break;
    case SDLK_SPACE:
        if (app.settings_selected >= 0) {
            auto& item = app.settings_document.values[static_cast<size_t>(app.settings_selected)];
            item.enabled = !item.enabled; app.settings_dirty = true;
        }
        break;
    case SDLK_LEFT:
    case SDLK_RIGHT:
        if (app.settings_selected >= 0) {
            auto& item = app.settings_document.values[static_cast<size_t>(app.settings_selected)];
            if (item.spec && (item.spec->kind == hoot::HootSettingKind::Boolean || item.spec->kind == hoot::HootSettingKind::Choice)) {
                item.enabled = true;
                cycle_setting_value(item, e.key.key == SDLK_LEFT ? -1 : 1);
                app.settings_dirty = true;
            }
        }
        break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (app.settings_selected >= 0) {
            auto& item = app.settings_document.values[static_cast<size_t>(app.settings_selected)];
            if (item.spec && (item.spec->kind == hoot::HootSettingKind::Boolean || item.spec->kind == hoot::HootSettingKind::Choice)) {
                item.enabled = true; cycle_setting_value(item); app.settings_dirty = true;
            } else begin_setting_edit(app, app.settings_selected);
        }
        break;
    default: break;
    }
    return true;
}



void begin_default_editor_edit(App& app)
{
    const int row = app.catalog_editor_selected;
    if (app.catalog_editor_tab == "general") {
        switch (row) {
        case 0: begin_catalog_editor_edit(app, CatalogEditorField::GeneralTitle, app.catalog_editor_entry.title); break;
        case 1: begin_catalog_editor_edit(app, CatalogEditorField::GeneralArchive, app.catalog_editor_entry.archive); break;
        case 2: begin_catalog_editor_edit(app, CatalogEditorField::GeneralDriverName, app.catalog_editor_entry.driver_name); break;
        case 3: begin_catalog_editor_edit(app, CatalogEditorField::GeneralDriverType, app.catalog_editor_entry.driver_type); break;
        case 4: begin_catalog_editor_edit(app, CatalogEditorField::GeneralSampleRate, std::to_string(app.catalog_editor_entry.default_sample_rate)); break;
        case 5: begin_catalog_editor_edit(app, CatalogEditorField::GeneralRefresh, std::to_string(app.catalog_editor_entry.refresh_hz)); break;
        default: break;
        }
    } else if (app.catalog_editor_tab == "tracks" && row >= 0 && row < static_cast<int>(app.catalog_editor_entry.tracks.size())) {
        begin_catalog_editor_edit(app, CatalogEditorField::TrackTitle, app.catalog_editor_entry.tracks[static_cast<size_t>(row)].title, 1);
    } else if (app.catalog_editor_tab == "options" && row >= 0 && row < static_cast<int>(app.catalog_editor_entry.options.size())) {
        auto it = app.catalog_editor_entry.options.begin(); std::advance(it, row);
        begin_catalog_editor_edit(app, CatalogEditorField::OptionValue, std::to_string(it->second), 1);
    } else if (app.catalog_editor_tab == "assets" && row >= 0 && row < static_cast<int>(app.catalog_editor_entry.assets.size())) {
        begin_catalog_editor_edit(app, CatalogEditorField::AssetPath, app.catalog_editor_entry.assets[static_cast<size_t>(row)].path, 1);
    }
}

bool handle_catalog_editor_event(App& app, const SDL_Event& e)
{
    if (!app.catalog_editor_open) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT && app.catalog_editor_editing) {
        if (e.text.text) app.catalog_editor_buffer += e.text.text;
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        const int count = editor_row_count(app);
        const int visible = app.catalog_editor_tab == "general" ? 6 : 16;
        app.catalog_editor_scroll = std::clamp(app.catalog_editor_scroll - static_cast<int>(e.wheel.y) * 3, 0, std::max(0, count - visible));
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float x=0,y=0;
        if (!SDL_RenderCoordinatesFromWindow(app.renderer, e.button.x, e.button.y, &x, &y)) return true;
        constexpr float px=110.0f, py=56.0f, pw=1220.0f, ph=788.0f;
        constexpr float tabs_x=px+16.0f, tabs_w=176.0f, content_x=px+210.0f, content_w=pw-226.0f;
        constexpr float rows_y=py+112.0f;
        static const char* tabs[] = {"general","tracks","options","assets","hardware","raw"};
        if (x >= px+pw-47 && x < px+pw-15 && y >= py+13 && y < py+45) { close_catalog_editor(app); return true; }
        if (x >= tabs_x && x < tabs_x+tabs_w) {
            for (int i=0;i<6;++i) {
                const float ty=py+82.0f+i*43.0f;
                if (y>=ty-5 && y<ty+26) { editor_select_tab(app,tabs[i]); return true; }
            }
        }
        const float footer_y=py+ph-66.0f;
        if (y>=footer_y-8 && y<footer_y+24) {
            if (x>=px+pw-430 && x<px+pw-340) { reset_catalog_editor(app); return true; }
            if (x>=px+pw-326 && x<px+pw-222) { duplicate_catalog_editor(app); return true; }
            if (x>=px+pw-208 && x<px+pw-128) { save_catalog_editor(app); return true; }
            if (x>=px+pw-116 && x<px+pw-28) { close_catalog_editor(app); return true; }
        }
        if (x>=content_x-4 && x<content_x+content_w) {
            int clicked=-1;
            if (app.catalog_editor_tab=="general") {
                clicked=static_cast<int>((y-rows_y)/46.0f);
                if (y>=rows_y && clicked>=0 && clicked<6) { app.catalog_editor_selected=clicked; begin_default_editor_edit(app); return true; }
            } else if (app.catalog_editor_tab=="tracks" || app.catalog_editor_tab=="options" || app.catalog_editor_tab=="assets") {
                clicked=app.catalog_editor_scroll+static_cast<int>((y-rows_y)/34.0f);
                if (y>=rows_y && clicked>=0 && clicked<editor_row_count(app)) {
                    app.catalog_editor_selected=clicked; clamp_editor_selection(app);
                    if (app.catalog_editor_tab=="tracks") {
                        auto& t=app.catalog_editor_entry.tracks[static_cast<size_t>(clicked)];
                        if (x<content_x+140) begin_catalog_editor_edit(app,CatalogEditorField::TrackCode,std::to_string(t.code),0);
                        else if (x<content_x+710) begin_catalog_editor_edit(app,CatalogEditorField::TrackTitle,t.title,1);
                        else begin_catalog_editor_edit(app,CatalogEditorField::TrackVoice,t.voice_bank,2);
                    } else if (app.catalog_editor_tab=="options") {
                        auto it=app.catalog_editor_entry.options.begin(); std::advance(it,clicked);
                        if (x<content_x+540) begin_catalog_editor_edit(app,CatalogEditorField::OptionName,it->first,0);
                        else begin_catalog_editor_edit(app,CatalogEditorField::OptionValue,std::to_string(it->second),1);
                    } else {
                        auto& a=app.catalog_editor_entry.assets[static_cast<size_t>(clicked)];
                        if (x<content_x+140) begin_catalog_editor_edit(app,CatalogEditorField::AssetType,a.type,0);
                        else if (x<content_x+655) begin_catalog_editor_edit(app,CatalogEditorField::AssetPath,a.path,1);
                        else if (x<content_x+835) begin_catalog_editor_edit(app,CatalogEditorField::AssetTransform,a.transform,2);
                        else begin_catalog_editor_edit(app,CatalogEditorField::AssetOffset,std::to_string(a.offset),3);
                    }
                    return true;
                }
            } else if (app.catalog_editor_tab=="hardware" && y>=rows_y+30 && y<rows_y+90) {
                cycle_hardware_target(app, x < content_x+content_w/2 ? -1 : 1); return true;
            }
        }
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return true;
    const bool ctrl=(e.key.mod & SDL_KMOD_CTRL)!=0;
    if (app.catalog_editor_editing) {
        if (e.key.key==SDLK_ESCAPE) { stop_catalog_editor_edit(app,false); }
        else if (e.key.key==SDLK_RETURN || e.key.key==SDLK_KP_ENTER) { stop_catalog_editor_edit(app,true); }
        else if (e.key.key==SDLK_BACKSPACE) pop_utf8(app.catalog_editor_buffer);
        else if (ctrl && e.key.key==SDLK_A) app.catalog_editor_buffer.clear();
        else if (ctrl && e.key.key==SDLK_V) { char* clip=SDL_GetClipboardText(); if (clip) { app.catalog_editor_buffer+=clip; SDL_free(clip); } }
        return true;
    }
    if (ctrl && e.key.key==SDLK_S) { save_catalog_editor(app); return true; }
    if (e.key.key==SDLK_D) { duplicate_catalog_editor(app); return true; }
    if (e.key.key==SDLK_ESCAPE) { close_catalog_editor(app); return true; }
    if (e.key.key==SDLK_UP) { --app.catalog_editor_selected; clamp_editor_selection(app); return true; }
    if (e.key.key==SDLK_DOWN) { ++app.catalog_editor_selected; clamp_editor_selection(app); return true; }
    if (e.key.key==SDLK_PAGEUP) { app.catalog_editor_selected-=16; clamp_editor_selection(app); return true; }
    if (e.key.key==SDLK_PAGEDOWN) { app.catalog_editor_selected+=16; clamp_editor_selection(app); return true; }
    if (e.key.key==SDLK_RETURN || e.key.key==SDLK_KP_ENTER) { begin_default_editor_edit(app); return true; }

    const int row=app.catalog_editor_selected;
    if (app.catalog_editor_tab=="tracks") {
        if (e.key.key==SDLK_C && row>=0 && row<static_cast<int>(app.catalog_editor_entry.tracks.size()))
            begin_catalog_editor_edit(app,CatalogEditorField::TrackCode,std::to_string(app.catalog_editor_entry.tracks[static_cast<size_t>(row)].code),0);
        else if (e.key.key==SDLK_V && row>=0 && row<static_cast<int>(app.catalog_editor_entry.tracks.size()))
            begin_catalog_editor_edit(app,CatalogEditorField::TrackVoice,app.catalog_editor_entry.tracks[static_cast<size_t>(row)].voice_bank,2);
        else if (e.key.key==SDLK_INSERT) {
            hoot::CatalogTrack t; t.code=app.catalog_editor_entry.tracks.empty()?0:app.catalog_editor_entry.tracks.back().code+1; t.title="New track";
            const auto pos=std::min(static_cast<size_t>(row+1),app.catalog_editor_entry.tracks.size()); app.catalog_editor_entry.tracks.insert(app.catalog_editor_entry.tracks.begin()+static_cast<std::ptrdiff_t>(pos),t);
            app.catalog_editor_selected=static_cast<int>(pos); app.catalog_editor_dirty=true; clamp_editor_selection(app);
        } else if (e.key.key==SDLK_DELETE && row>=0 && row<static_cast<int>(app.catalog_editor_entry.tracks.size())) {
            app.catalog_editor_entry.tracks.erase(app.catalog_editor_entry.tracks.begin()+row); app.catalog_editor_dirty=true; clamp_editor_selection(app);
        }
    } else if (app.catalog_editor_tab=="options") {
        if (e.key.key==SDLK_N && row>=0 && row<static_cast<int>(app.catalog_editor_entry.options.size())) {
            auto it=app.catalog_editor_entry.options.begin(); std::advance(it,row); begin_catalog_editor_edit(app,CatalogEditorField::OptionName,it->first,0);
        } else if (e.key.key==SDLK_INSERT) {
            std::string name="new_option"; int n=1; while(app.catalog_editor_entry.options.count(name)) name="new_option_"+std::to_string(n++);
            app.catalog_editor_entry.options[name]=0; app.catalog_editor_dirty=true; app.catalog_editor_selected=static_cast<int>(std::distance(app.catalog_editor_entry.options.begin(),app.catalog_editor_entry.options.find(name))); clamp_editor_selection(app);
        } else if (e.key.key==SDLK_DELETE && row>=0 && row<static_cast<int>(app.catalog_editor_entry.options.size())) {
            auto it=app.catalog_editor_entry.options.begin(); std::advance(it,row); app.catalog_editor_entry.options.erase(it); app.catalog_editor_dirty=true; clamp_editor_selection(app);
        }
    } else if (app.catalog_editor_tab=="assets") {
        if (row>=0 && row<static_cast<int>(app.catalog_editor_entry.assets.size())) {
            auto& a=app.catalog_editor_entry.assets[static_cast<size_t>(row)];
            if (e.key.key==SDLK_T) begin_catalog_editor_edit(app,CatalogEditorField::AssetType,a.type,0);
            else if (e.key.key==SDLK_R) begin_catalog_editor_edit(app,CatalogEditorField::AssetTransform,a.transform,2);
            else if (e.key.key==SDLK_O) begin_catalog_editor_edit(app,CatalogEditorField::AssetOffset,std::to_string(a.offset),3);
            else if (e.key.key==SDLK_DELETE) { app.catalog_editor_entry.assets.erase(app.catalog_editor_entry.assets.begin()+row); app.catalog_editor_dirty=true; clamp_editor_selection(app); }
        }
        if (e.key.key==SDLK_INSERT) { hoot::HootAssetRef a; a.type="file"; a.path="NEW.DAT"; app.catalog_editor_entry.assets.push_back(a); app.catalog_editor_selected=static_cast<int>(app.catalog_editor_entry.assets.size())-1; app.catalog_editor_dirty=true; clamp_editor_selection(app); }
    } else if (app.catalog_editor_tab=="hardware") {
        if (e.key.key==SDLK_LEFT) cycle_hardware_target(app,-1);
        else if (e.key.key==SDLK_RIGHT) cycle_hardware_target(app,1);
    }
    return true;
}

bool handle_library_event(App& app, const SDL_Event& e)
{
    if (!app.library_open) return false;

    if (e.type == SDL_EVENT_TEXT_INPUT && app.library_search_editing) {
        if (e.text.text) app.library_search += e.text.text;
        app.library_selected = 0;
        app.library_scroll = 0;
        rebuild_library_rows(app);
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        move_library_selection(app, -static_cast<int>(e.wheel.y) * 3);
        return true;
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float x = 0.0f, y = 0.0f;
        if (!SDL_RenderCoordinatesFromWindow(app.renderer, e.button.x, e.button.y, &x, &y)) return true;
        constexpr float px = hootgui::RetroRenderer::kLibraryX;
        constexpr float py = hootgui::RetroRenderer::kLibraryY;
        constexpr float pw = hootgui::RetroRenderer::kLibraryWidth;
        constexpr float search_y = py + 76.0f;
        constexpr float list_y = py + 126.0f;
        constexpr float row_h = 21.0f;

        if (x >= px + pw - 170.0f && x < px + pw - 62.0f && y >= py + 13.0f && y < py + 45.0f) {
            if (app.library_level == LibraryLevel::Tracks && app.library_entry_index >= 0) {
                open_catalog_editor(app, app.library_entry_index);
            } else if ((app.library_level == LibraryLevel::Games || app.library_level == LibraryLevel::Variants) && !app.library_rows.empty()
                       && app.library_selected >= 0 && app.library_selected < static_cast<int>(app.library_rows.size())) {
                const auto& row = app.library_rows[static_cast<size_t>(app.library_selected)];
                if (row.kind == hootgui::LibraryRowKind::Entry) open_catalog_editor(app, row.payload);
            }
            return true;
        }
        if (x >= px + pw - 47.0f && x < px + pw - 15.0f && y >= py + 13.0f && y < py + 45.0f) {
            close_library(app);
            return true;
        }
        if (x >= px + pw - 216.0f && x < px + pw - 20.0f && y >= search_y && y < search_y + 31.0f) {
            toggle_library_available_only(app);
            return true;
        }
        if (x >= px + 88.0f && x < px + pw - 230.0f && y >= search_y && y < search_y + 31.0f) {
            start_library_search(app);
            return true;
        }
        if (x >= px + 12.0f && x < px + pw - 12.0f && y >= list_y
            && y < list_y + hootgui::RetroRenderer::kLibraryVisibleRows * row_h) {
            const int row = static_cast<int>((y - list_y) / row_h);
            const int index = app.library_scroll + row;
            if (index >= 0 && index < static_cast<int>(app.library_rows.size())) {
                const Uint64 now = SDL_GetTicks();
                const bool double_click = app.library_last_click_index == index
                    && now >= app.library_last_click_tick && now - app.library_last_click_tick <= 420;
                app.library_selected = index;
                clamp_library_cursor(app);
                app.library_last_click_index = index;
                app.library_last_click_tick = now;
                if (double_click) activate_library_row(app, false);
            }
            return true;
        }
        return true;
    }

    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return true;
    const bool ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
    if (app.library_search_editing) {
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            stop_library_search(app);
        } else if (e.key.key == SDLK_BACKSPACE) {
            pop_utf8(app.library_search);
            app.library_selected = 0;
            app.library_scroll = 0;
            rebuild_library_rows(app);
        } else if (ctrl && e.key.key == SDLK_A) {
            app.library_search.clear();
            app.library_selected = 0;
            app.library_scroll = 0;
            rebuild_library_rows(app);
        } else if (ctrl && e.key.key == SDLK_V) {
            char* clip = SDL_GetClipboardText();
            if (clip) {
                app.library_search += clip;
                SDL_free(clip);
                app.library_selected = 0;
                app.library_scroll = 0;
                rebuild_library_rows(app);
            }
        }
        return true;
    }

    if ((ctrl && e.key.key == SDLK_F) || e.key.key == SDLK_F) {
        start_library_search(app);
        return true;
    }
    switch (e.key.key) {
    case SDLK_A: toggle_library_available_only(app); break;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: library_back(app); break;
    case SDLK_UP: move_library_selection(app, -1); break;
    case SDLK_DOWN: move_library_selection(app, 1); break;
    case SDLK_PAGEUP: move_library_selection(app, -hootgui::RetroRenderer::kLibraryVisibleRows); break;
    case SDLK_PAGEDOWN: move_library_selection(app, hootgui::RetroRenderer::kLibraryVisibleRows); break;
    case SDLK_HOME:
        app.library_selected = 0; clamp_library_cursor(app); break;
    case SDLK_END:
        if (!app.library_rows.empty()) app.library_selected = static_cast<int>(app.library_rows.size()) - 1;
        clamp_library_cursor(app); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: activate_library_row(app, false); break;
    case SDLK_SPACE: activate_library_row(app, true); break;
    case SDLK_E:
        if ((app.library_level == LibraryLevel::Games || app.library_level == LibraryLevel::Variants) && !app.library_rows.empty()
            && app.library_selected >= 0 && app.library_selected < static_cast<int>(app.library_rows.size())) {
            const auto& row = app.library_rows[static_cast<size_t>(app.library_selected)];
            if (row.kind == hootgui::LibraryRowKind::Entry) open_catalog_editor(app, row.payload);
        } else if (app.library_level == LibraryLevel::Tracks && app.library_entry_index >= 0) {
            open_catalog_editor(app, app.library_entry_index);
        }
        break;
    case SDLK_O:
        close_library(app);
        request_open_pack(app);
        break;
    case SDLK_L: close_library(app); break;
    default: break;
    }
    return true;
}


void toggle_fullscreen(App& app)
{
    if (!app.window) return;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(app.window);
    const bool fullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(app.window, !fullscreen)) {
        app.model.notice = std::string("Fullscreen: ") + SDL_GetError();
    }
}

bool point_in(float x, float y, float rx, float ry, float rw, float rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

void playback_menu_action(App& app, int row)
{
    switch (row) {
    case 0:
        if (!app.pack_loaded) { app.model.notice = "Load a pack first."; break; }
        if (app.stopped || app.model.paused) set_paused(app, false);
        else set_paused(app, true);
        break;
    case 1: stop_playback(app); break;
    case 2: restart_playback(app); break;
    case 3:
        if (app.pack_loaded) switch_track(app, app.model.selected_track - 1);
        break;
    case 4:
        if (app.pack_loaded) switch_track(app, app.model.selected_track + 1);
        break;
    case 5: toggle_master_mute(app); break;
    case 6: request_wav_recording(app); break;
    default: break;
    }
}

bool handle_about_event(App& app, const SDL_Event& e)
{
    if (!app.about_open) return false;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
            app.about_open = false;
        }
        return true;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float x = 0.0f, y = 0.0f;
        if (SDL_RenderCoordinatesFromWindow(app.renderer, e.button.x, e.button.y, &x, &y)) {
            constexpr float px = hootgui::RetroRenderer::kAboutX;
            constexpr float py = hootgui::RetroRenderer::kAboutY;
            constexpr float pw = hootgui::RetroRenderer::kAboutWidth;
            constexpr float ph = hootgui::RetroRenderer::kAboutHeight;
            const bool close_x = point_in(x, y, px + pw - 48.0f, py + 10.0f, 34.0f, 32.0f);
            const bool ok = point_in(x, y, px + pw - 116.0f, py + ph - 50.0f, 88.0f, 30.0f);
            if (close_x || ok) app.about_open = false;
        }
        return true;
    }
    return true;
}

bool handle_top_menu_mouse(App& app, float x, float y)
{
    using R = hootgui::RetroRenderer;
    if (point_in(x, y, R::kEditMenuX, R::kTopMenuY, R::kEditMenuWidth, R::kTopMenuHeight)) {
        app.top_menu = app.top_menu == hootgui::TopMenu::Edit ? hootgui::TopMenu::None : hootgui::TopMenu::Edit;
        return true;
    }
    if (point_in(x, y, R::kPlaybackMenuX, R::kTopMenuY, R::kPlaybackMenuWidth, R::kTopMenuHeight)) {
        app.top_menu = app.top_menu == hootgui::TopMenu::Playback ? hootgui::TopMenu::None : hootgui::TopMenu::Playback;
        return true;
    }
    if (point_in(x, y, R::kViewMenuX, R::kTopMenuY, R::kViewMenuWidth, R::kTopMenuHeight)) {
        app.top_menu = app.top_menu == hootgui::TopMenu::View ? hootgui::TopMenu::None : hootgui::TopMenu::View;
        return true;
    }
    if (point_in(x, y, R::kHelpMenuX, R::kTopMenuY, R::kHelpMenuWidth, R::kTopMenuHeight)) {
        app.top_menu = app.top_menu == hootgui::TopMenu::Help ? hootgui::TopMenu::None : hootgui::TopMenu::Help;
        return true;
    }
    if (app.top_menu == hootgui::TopMenu::Edit && point_in(x, y, R::kEditMenuX, R::kMenuPopupY, 244.0f, 44.0f)) {
        app.top_menu = hootgui::TopMenu::None;
        toggle_fullscreen(app);
        return true;
    }
    if (app.top_menu == hootgui::TopMenu::Playback &&
        point_in(x, y, R::kPlaybackMenuX, R::kMenuPopupY, R::kPlaybackPopupWidth,
                 R::kPlaybackRowHeight * R::kPlaybackRowCount)) {
        const int row = std::clamp(static_cast<int>((y - R::kMenuPopupY) / R::kPlaybackRowHeight),
                                   0, R::kPlaybackRowCount - 1);
        app.top_menu = hootgui::TopMenu::None;
        playback_menu_action(app, row);
        return true;
    }
    if (app.top_menu == hootgui::TopMenu::Help && point_in(x, y, R::kHelpMenuX, R::kMenuPopupY, 210.0f, 44.0f)) {
        app.top_menu = hootgui::TopMenu::None;
        app.about_open = true;
        return true;
    }
    if (app.top_menu != hootgui::TopMenu::None) {
        app.top_menu = hootgui::TopMenu::None;
    }
    return false;
}

void handle_event(App& app, const SDL_Event& e)
{
    if (e.type == SDL_EVENT_QUIT) { app.running = false; return; }
    if (handle_about_event(app, e)) return;

    // Playback requirement warnings are modal in the GUI. They intentionally
    // consume input so a click used to dismiss the message cannot also change
    // tracks underneath it. The same warning remains available on stderr for
    // diagnostics, but no terminal is required for normal users.
    if (app.model.warning_overlay_visible && !app.settings_open && !app.catalog_editor_open) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            float lx = 0.0f, ly = 0.0f;
            if (SDL_RenderCoordinatesFromWindow(app.renderer, e.button.x, e.button.y, &lx, &ly)) {
                constexpr float px = hootgui::RetroRenderer::kWarningX;
                constexpr float py = hootgui::RetroRenderer::kWarningY;
                constexpr float pw = hootgui::RetroRenderer::kWarningWidth;
                constexpr float ph = hootgui::RetroRenderer::kWarningHeight;
                const float button_y = py + ph - 64.0f;
                // Top-right X and Close button.
                if ((lx >= px + pw - 48.0f && lx < px + pw - 14.0f && ly >= py + 12.0f && ly < py + 46.0f) ||
                    (lx >= px + pw - 126.0f && lx < px + pw - 26.0f && ly >= button_y && ly < button_y + 36.0f)) {
                    app.dismissed_warnings.insert(app.model.warning);
                    app.model.warning_overlay_visible = false;
                    return;
                }
                // Open Settings directly on the MIDI/hardware page.
                if (lx >= px + 26.0f && lx < px + 182.0f && ly >= button_y && ly < button_y + 36.0f) {
                    app.dismissed_warnings.insert(app.model.warning);
                    app.model.warning_overlay_visible = false;
                    open_settings(app);
                    app.settings_section = "midi";
                    select_first_setting(app);
                    return;
                }
            }
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) {
                app.dismissed_warnings.insert(app.model.warning);
                app.model.warning_overlay_visible = false;
            } else if (e.key.key == SDLK_S) {
                app.dismissed_warnings.insert(app.model.warning);
                app.model.warning_overlay_visible = false;
                open_settings(app);
                app.settings_section = "midi";
                select_first_setting(app);
            }
            return;
        }
        return;
    }

    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
        (e.key.mod & SDL_KMOD_ALT) && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) {
        app.top_menu = hootgui::TopMenu::None;
        toggle_fullscreen(app);
        return;
    }

    if (handle_catalog_editor_event(app, e)) return;
    if (handle_settings_event(app, e)) return;
    if (handle_library_event(app, e)) return;
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
        remember_pack_directory(app, e.drop.data);
        load_reference(app, e.drop.data, 0);
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        float lx = 0.0f, ly = 0.0f;
        if (SDL_RenderCoordinatesFromWindow(app.renderer, e.button.x, e.button.y, &lx, &ly)) {
            if (handle_top_menu_mouse(app, lx, ly)) return;
            if (ly >= hootgui::RetroRenderer::kOpenButtonY &&
                ly < hootgui::RetroRenderer::kOpenButtonY + hootgui::RetroRenderer::kOpenButtonHeight &&
                lx >= hootgui::RetroRenderer::kOpenButtonX &&
                lx < hootgui::RetroRenderer::kOpenButtonX + hootgui::RetroRenderer::kOpenButtonWidth) {
                request_open_pack(app);
                return;
            }
            if (ly >= hootgui::RetroRenderer::kLibraryButtonY &&
                ly < hootgui::RetroRenderer::kLibraryButtonY + hootgui::RetroRenderer::kLibraryButtonHeight &&
                lx >= hootgui::RetroRenderer::kLibraryButtonX &&
                lx < hootgui::RetroRenderer::kLibraryButtonX + hootgui::RetroRenderer::kLibraryButtonWidth) {
                open_library(app);
                return;
            }
            if (ly >= hootgui::RetroRenderer::kSettingsButtonY &&
                ly < hootgui::RetroRenderer::kSettingsButtonY + hootgui::RetroRenderer::kSettingsButtonHeight &&
                lx >= hootgui::RetroRenderer::kSettingsButtonX &&
                lx < hootgui::RetroRenderer::kSettingsButtonX + hootgui::RetroRenderer::kSettingsButtonWidth) {
                open_settings(app);
                return;
            }
            if (!app.pack_loaded) return;
            constexpr float playlist_y = hootgui::RetroRenderer::kLogicalHeight - 255.0f - 28.0f;
            constexpr float first_row_y = playlist_y + 28.0f;
            constexpr float row_h = 17.0f;
            if (ly >= first_row_y && ly < hootgui::RetroRenderer::kLogicalHeight - 28.0f) {
                const int row = static_cast<int>((ly - first_row_y) / row_h);
                const int index = app.model.playlist_scroll + row;
                if (index >= 0 && index < static_cast<int>(app.model.tracks.size())) switch_track(app, index);
            }
        }
        return;
    }
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return;
    if (app.top_menu != hootgui::TopMenu::None && e.key.key == SDLK_ESCAPE) { app.top_menu = hootgui::TopMenu::None; return; }
    if ((e.key.mod & SDL_KMOD_CTRL) && e.key.key == SDLK_COMMA) { open_settings(app); return; }
    if ((e.key.mod & SDL_KMOD_CTRL) && (e.key.mod & SDL_KMOD_SHIFT) && e.key.key == SDLK_R) {
        request_wav_recording(app); return;
    }
    if ((e.key.mod & SDL_KMOD_CTRL) && e.key.key == SDLK_R) { restart_playback(app); return; }
    if ((e.key.mod & SDL_KMOD_CTRL) && e.key.key == SDLK_S) { stop_playback(app); return; }
    switch (e.key.key) {
    case SDLK_ESCAPE: case SDLK_Q: app.running = false; break;
    case SDLK_O: request_open_pack(app); break;
    case SDLK_L: open_library(app); break;
    case SDLK_M: toggle_master_mute(app); break;
    case SDLK_SPACE:
        if (app.stopped || app.model.paused) set_paused(app, false);
        else set_paused(app, true);
        break;
    case SDLK_RIGHT: case SDLK_N: switch_track(app, app.model.selected_track + 1); break;
    case SDLK_LEFT: case SDLK_P: switch_track(app, app.model.selected_track - 1); break;
    case SDLK_DOWN: app.model.playlist_scroll = std::min(app.model.playlist_scroll + 1, std::max(0, static_cast<int>(app.model.tracks.size()) - 1)); break;
    case SDLK_UP: app.model.playlist_scroll = std::max(0, app.model.playlist_scroll - 1); break;
    case SDLK_PAGEDOWN: app.model.channel_scroll += 8; break;
    case SDLK_PAGEUP: app.model.channel_scroll = std::max(0, app.model.channel_scroll - 8); break;
    default: break;
    }
}

void frame(void* opaque)
{
    auto& app = *static_cast<App*>(opaque);
    SDL_Event e{};
    while (SDL_PollEvent(&e)) handle_event(app, e);
    process_pending_open(app);
    if (!app.running) {
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return;
    }
    pump_audio(app);
    const Uint64 now = SDL_GetTicks();
    maybe_warn_silent_playback(app, now);
    if (now - app.last_spectrum_tick >= 33) {
        app.spectrum.calculate();
        app.last_spectrum_tick = now;
    }
    if (now - app.last_visual_tick >= 16) {
        update_visual(app);
        hootgui::SettingsView settings_view;
        const hootgui::SettingsView* settings_ptr = nullptr;
        if (app.settings_open) {
            settings_view.document = &app.settings_document;
            settings_view.section = app.settings_section;
            settings_view.scroll = app.settings_scroll;
            settings_view.selected = app.settings_selected;
            settings_view.editing = app.settings_editing;
            settings_view.dirty = app.settings_dirty;
            settings_view.config_path = app.settings_config_path;
            settings_view.message = app.settings_message;
            settings_ptr = &settings_view;
        }
        hootgui::LibraryView library_view;
        const hootgui::LibraryView* library_ptr = nullptr;
        if (app.library_open) {
            library_view.rows = &app.library_rows;
            library_view.breadcrumb = library_breadcrumb(app);
            library_view.search = app.library_search;
            library_view.message = app.library_message;
            library_view.available_only = app.library_available_only;
            if (!app.last_pack_directory.empty()) library_view.pack_location = app.last_pack_directory;
            else if (!app.pack_directories.empty()) library_view.pack_location = app.pack_directories.front().string();
            library_view.selected = app.library_selected;
            library_view.scroll = app.library_scroll;
            library_view.search_editing = app.library_search_editing;
            library_view.can_edit = app.library_level == LibraryLevel::Tracks;
            if ((app.library_level == LibraryLevel::Games || app.library_level == LibraryLevel::Variants) && !app.library_rows.empty()
                && app.library_selected >= 0 && app.library_selected < static_cast<int>(app.library_rows.size()))
                library_view.can_edit = app.library_rows[static_cast<size_t>(app.library_selected)].kind == hootgui::LibraryRowKind::Entry;
            library_ptr = &library_view;
        }
        hootgui::CatalogEditorView editor_view;
        const hootgui::CatalogEditorView* editor_ptr = nullptr;
        if (app.catalog_editor_open) {
            editor_view.entry = &app.catalog_editor_entry;
            editor_view.tab = app.catalog_editor_tab;
            editor_view.selected = app.catalog_editor_selected;
            editor_view.scroll = app.catalog_editor_scroll;
            editor_view.edit_column = app.catalog_editor_edit_column;
            editor_view.edit_buffer = app.catalog_editor_buffer;
            editor_view.editing = app.catalog_editor_editing;
            editor_view.dirty = app.catalog_editor_dirty;
            editor_view.locally_modified = app.catalog_editor_locally_modified;
            editor_view.override_path = app.user_overrides_path.string();
            editor_view.message = app.catalog_editor_message;
            editor_ptr = &editor_view;
        }
        app.ui->draw(app.model, settings_ptr, library_ptr, editor_ptr, app.top_menu, app.about_open);
        app.last_visual_tick = now;
    }
#ifndef __EMSCRIPTEN__
    SDL_Delay(2);
#endif
}

#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE int hoot_web_open_pack(const char* virtual_zip_path)
{
    if (!g_web_app || !virtual_zip_path) return 0;
    remember_pack_directory(*g_web_app, virtual_zip_path);
    refresh_pack_index(*g_web_app);
    if (g_web_app->library_open) rebuild_library_rows(*g_web_app);
    sync_web_persistence();
    if (g_web_app->audio) SDL_ResumeAudioStreamDevice(g_web_app->audio);
    return load_reference(*g_web_app, virtual_zip_path, 0) ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE void hoot_web_refresh_packs()
{
    if (!g_web_app) return;
    refresh_pack_index(*g_web_app);
    if (g_web_app->library_open) rebuild_library_rows(*g_web_app);
}

extern "C" EMSCRIPTEN_KEEPALIVE void hoot_web_resume_audio()
{
    if (g_web_app && g_web_app->audio && !g_web_app->model.paused && !g_web_app->stopped)
        SDL_ResumeAudioStreamDevice(g_web_app->audio);
}
#endif

} // namespace

int main(int argc, char** argv)
{
    hoot::enable_utf8_console();
    hoot::HootAppPaths app_paths;
#ifndef __EMSCRIPTEN__
    std::string home_error;
    if (!hoot::bootstrap_hoot_home(app_paths, home_error)) {
        std::fprintf(stderr, "hootui: %s\n", home_error.c_str());
        return 1;
    }
    hoot::apply_hoot_home_resource_defaults(app_paths);
#else
    // /hoot is mounted as IDBFS by the web shell before main() runs. Keep all
    // mutable browser state there while the immutable catalogue/font stay in
    // the preloaded MEMFS bundle.
    app_paths.home = "/hoot";
    app_paths.config = "/hoot/hootplay.ini";
    app_paths.catalog_dir = "/catalog";
    app_paths.default_catalog = "/catalog/hoot.sqlite";
    app_paths.user_overrides = "/hoot/catalog/user-overrides.json";
    app_paths.roms_dir = "/hoot/roms";
    std::error_code web_ec;
    std::filesystem::create_directories("/hoot/packs", web_ec);
    web_ec.clear();
    std::filesystem::create_directories("/hoot/catalog", web_ec);
    web_ec.clear();
    std::filesystem::create_directories("/hoot/recordings", web_ec);
    if (!std::filesystem::is_regular_file(app_paths.config)) {
        std::ofstream web_ini(app_paths.config, std::ios::binary | std::ios::trunc);
        if (web_ini) web_ini << hoot::default_hootplay_ini();
        sync_web_persistence();
    }
    hoot::apply_hoot_home_resource_defaults(app_paths);
#endif
    Options options;
#ifdef __EMSCRIPTEN__
    options.packs = "/hoot/packs";
#endif
    if (std::filesystem::is_regular_file(app_paths.default_catalog)) options.catalog = app_paths.default_catalog.string();
    hoot::HootplayFileConfig file_config;
    bool config_required = false;
    bool config_valid = true;
    const auto config_path = discover_config_path(argc, argv, app_paths, config_required, config_valid);
    if (!config_valid) { usage(argv[0]); return 2; }
    if (!config_path.empty()) {
        std::string config_error;
        if (!hoot::load_hootplay_config(config_path, file_config, config_error)) {
            std::fprintf(stderr, "hootui: %s\n", config_error.c_str());
            return config_required ? 2 : 1;
        }
        options.loaded_config_path = std::filesystem::absolute(config_path).lexically_normal().string();
        apply_file_config(file_config, options);
        hoot::apply_hootplay_environment(file_config);
    }
    if (!parse_options(argc, argv, options)) { usage(argv[0]); return 2; }
#ifdef __EMSCRIPTEN__
    // The browser bundle ships one immutable uncompressed catalogue. Mutable
    // per-user changes are layered through /hoot/catalog/user-overrides.json.
    options.catalog = "/catalog/hoot.sqlite";
    if (options.packs.empty() || options.packs == "." || options.packs == "packs")
        options.packs = "/hoot/packs";
#endif
    if (options.rate <= 0 || options.track <= 0) {
        std::fprintf(stderr, "hootui: sample rate and track must be positive\n");
        return 2;
    }
    if (options.check_font) {
        std::string font_status;
        const bool ok = hootgui::RetroRenderer::probe_japanese_font(options.font, font_status);
        std::fprintf(ok ? stdout : stderr, "hootui: %s\n", font_status.c_str());
        return ok ? 0 : 1;
    }

    auto app = std::make_unique<App>(options.rate);
    app->roms_dir = app_paths.roms_dir;
    app->catalog_path = options.catalog;
    if (const char* override_path = std::getenv("HOOT_USER_OVERRIDES"); override_path && override_path[0])
        app->user_overrides_path = override_path;
    else
        app->user_overrides_path = app_paths.user_overrides;
#ifdef __EMSCRIPTEN__
    setenv("HOOT_USER_OVERRIDES", app->user_overrides_path.string().c_str(), 0);
#endif
    app->ui_state_path = app_paths.home / "hootui-state.ini";
    load_ui_state(*app);
    app->settings_config_path = !options.loaded_config_path.empty()
        ? options.loaded_config_path
        : std::filesystem::absolute(app_paths.config).lexically_normal().string();
    HootConfig cfg{};
    cfg.sample_rate = options.rate;
    cfg.packs_path = options.packs.c_str();
    app->hoot = hoot_create(&cfg);
    if (!app->hoot) { std::fprintf(stderr, "unable to create Hoot context\n"); return 1; }
    if (hoot_load_catalog(app->hoot, options.catalog.c_str()) != HOOT_OK) {
        std::fprintf(stderr, "catalog: %s\n", hoot_last_error(app->hoot));
        hoot_destroy(app->hoot); return 1;
    }

    refresh_catalog_entries(*app);
    {
        std::string override_error;
        if (!hoot::load_hoot_user_overrides(app->user_overrides_path, app->user_overrides, override_error))
            std::fprintf(stderr, "hootui: user overrides: %s\n", override_error.c_str());
    }
    add_pack_directory(*app, options.packs);
    if (!app->last_pack_directory.empty()) add_pack_directory(*app, app->last_pack_directory);
    refresh_pack_index(*app);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); hoot_destroy(app->hoot); return 1;
    }
    if (!SDL_CreateWindowAndRenderer("hoot... - Sound Hardware Emulator",
                                     hootgui::RetroRenderer::kLogicalWidth,
                                     hootgui::RetroRenderer::kLogicalHeight,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &app->window, &app->renderer)) {
        std::fprintf(stderr, "SDL window: %s\n", SDL_GetError()); SDL_Quit(); hoot_destroy(app->hoot); return 1;
    }
    SDL_SetRenderLogicalPresentation(app->renderer,
                                     hootgui::RetroRenderer::kLogicalWidth,
                                     hootgui::RetroRenderer::kLogicalHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    apply_window_icon(app->window, argc > 0 ? argv[0] : nullptr);
    app->ui = std::make_unique<hootgui::RetroRenderer>(app->renderer, options.font);
    std::fprintf(stderr, "hootui: %s\n", app->ui->text_status().c_str());
    if (!app->ui->unicode_text_available()) {
        const std::string message =
            "Hoot UI requires Japanese-capable Unicode text rendering.\n\n"
            + app->ui->text_status()
            + "\n\nInstall SDL3_ttf and a Japanese-capable system font, or set --font / "
              "[gui] font / HOOT_UI_FONT to a suitable TTF, OTF or TTC file.";
        std::fprintf(stderr, "hootui: fatal: %s\n", message.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "hootui - Unicode font required",
                                 message.c_str(), app->window);
        app->ui.reset();
        SDL_DestroyRenderer(app->renderer);
        SDL_DestroyWindow(app->window);
        SDL_Quit();
        hoot_destroy(app->hoot);
        return 1;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = options.rate;
    app->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!app->audio) app->model.notice = std::string("Audio disabled: ") + SDL_GetError();
    else SDL_ResumeAudioStreamDevice(app->audio);

    if (!options.entry.empty()) {
        if (!load_reference(*app, options.entry, options.track - 1))
            std::fprintf(stderr, "pack: %s\n", app->model.notice.c_str());
    } else {
        app->model.notice = "Drop a Hoot pack ZIP here to start playback";
        if (!options.loaded_config_path.empty())
            app->model.notice += " | config: " + options.loaded_config_path;
    }
    update_visual(*app);

#ifdef __EMSCRIPTEN__
    g_web_app = app.release();
    emscripten_set_main_loop_arg(frame, g_web_app, 0, true);
#else
    while (app->running) frame(app.get());
    if (app->recorder.active()) stop_recording(*app, false);
    if (app->audio) SDL_DestroyAudioStream(app->audio);
    app->ui.reset();
    if (app->renderer) SDL_DestroyRenderer(app->renderer);
    if (app->window) SDL_DestroyWindow(app->window);
    SDL_Quit();
    hoot_destroy(app->hoot);
#endif
    return 0;
}
