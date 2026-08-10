#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "config/hoot_settings.h"
#include "config/hoot_user_overrides.h"
#include "core/hoot_library_info.h"
#include "core/hoot_track_info.h"
#include "core/hoot_visual_state.h"
#include "spectrum.h"

namespace hootgui {

enum class TopMenu { None, Edit, Playback, View, Help };

enum class LibraryRowKind { Folder, Entry, Track };

struct LibraryRow {
    LibraryRowKind kind = LibraryRowKind::Folder;
    std::string label;
    std::string detail;
    std::string status;
    int payload = -1;
    bool available = true;
};

struct LibraryView {
    const std::vector<LibraryRow>* rows = nullptr;
    std::string breadcrumb;
    std::string search;
    std::string message;
    std::string pack_location;
    int selected = 0;
    int scroll = 0;
    bool search_editing = false;
    bool can_edit = false;
    bool available_only = false;
};

struct CatalogEditorView {
    const hoot::HootEntryOverride* entry = nullptr;
    std::string tab = "general";
    int selected = 0;
    int scroll = 0;
    int edit_column = 0;
    std::string edit_buffer;
    bool editing = false;
    bool dirty = false;
    bool locally_modified = false;
    std::string override_path;
    std::string message;
};

struct SettingsView {
    const hoot::HootSettingsDocument* document = nullptr;
    std::string section = "player";
    int scroll = 0;
    int selected = -1;
    bool editing = false;
    bool dirty = false;
    std::string config_path;
    std::string message;
};

struct UiModel {
    HootEntryInfo entry{};
    HootTrackInfo track_info{};
    HootVisualState visual{};
    std::vector<HootCatalogTrackInfo> tracks;
    std::array<float, SpectrumAnalyzer::kBins> spectrum_left{};
    std::array<float, SpectrumAnalyzer::kBins> spectrum_right{};
    int selected_track = 0;
    int playlist_scroll = 0;
    int channel_scroll = 0;
    bool paused = false;
    bool stopped = false;
    bool muted_all = false;
    bool recording = false;
    std::array<bool, HOOT_VISUAL_CHANNELS_MAX> channel_muted{};
    int solo_channel = -1;
    float gain = 1.0f;
    std::string notice;
    std::string warning;
    std::string warning_help;
    bool warning_overlay_visible = false;
};

class RetroRenderer {
public:
    static constexpr int kLogicalWidth = 1440;
    static constexpr int kLogicalHeight = 900;
    static constexpr float kContentY = 36.0f;
    static constexpr float kLeftPanelWidth = 690.0f;
    static constexpr float kChannelRowHeight = 31.0f;
    static constexpr float kChannelFirstY = kContentY + 4.0f;
    static constexpr float kEditMenuX = 560.0f;
    static constexpr float kEditMenuWidth = 54.0f;
    static constexpr float kPlaybackMenuX = 624.0f;
    static constexpr float kPlaybackMenuWidth = 92.0f;
    static constexpr float kViewMenuX = 726.0f;
    static constexpr float kViewMenuWidth = 54.0f;
    static constexpr float kHelpMenuX = 790.0f;
    static constexpr float kHelpMenuWidth = 54.0f;
    static constexpr float kTopMenuY = 4.0f;
    static constexpr float kTopMenuHeight = 26.0f;
    static constexpr float kMenuPopupY = 33.0f;
    static constexpr float kPlaybackPopupWidth = 304.0f;
    static constexpr float kPlaybackRowHeight = 32.0f;
    static constexpr int kPlaybackRowCount = 8;
    static constexpr float kAboutX = 410.0f;
    static constexpr float kAboutY = 250.0f;
    static constexpr float kAboutWidth = 620.0f;
    static constexpr float kAboutHeight = 330.0f;
    static constexpr float kWarningX = 250.0f;
    static constexpr float kWarningY = 170.0f;
    static constexpr float kWarningWidth = 940.0f;
    static constexpr float kWarningHeight = 500.0f;
    static constexpr float kOpenButtonX = 1006.0f;
    static constexpr float kOpenButtonY = 5.0f;
    static constexpr float kOpenButtonWidth = 116.0f;
    static constexpr float kOpenButtonHeight = 24.0f;
    static constexpr float kLibraryButtonX = 1132.0f;
    static constexpr float kLibraryButtonY = 5.0f;
    static constexpr float kLibraryButtonWidth = 130.0f;
    static constexpr float kLibraryButtonHeight = 24.0f;
    static constexpr float kSettingsButtonX = 1272.0f;
    static constexpr float kSettingsButtonY = 5.0f;
    static constexpr float kSettingsButtonWidth = 152.0f;
    static constexpr float kSettingsButtonHeight = 24.0f;
    static constexpr float kLibraryX = 145.0f;
    static constexpr float kLibraryY = 72.0f;
    static constexpr float kLibraryWidth = 1150.0f;
    static constexpr float kLibraryHeight = 758.0f;
    static constexpr int kLibraryVisibleRows = 25;

    explicit RetroRenderer(SDL_Renderer* renderer, std::string font_path = {});
    ~RetroRenderer();

    RetroRenderer(const RetroRenderer&) = delete;
    RetroRenderer& operator=(const RetroRenderer&) = delete;

    void draw(const UiModel& model, const SettingsView* settings = nullptr, const LibraryView* library = nullptr,
              const CatalogEditorView* editor = nullptr, TopMenu top_menu = TopMenu::None, bool about_open = false);
    bool unicode_text_available() const;
    const std::string& text_status() const;
    static bool probe_japanese_font(const std::string& requested, std::string& status);

private:
    struct TextState;

    void text(float x, float y, const std::string& value, float scale = 1.0f);
    void clipped_text(float x, float y, float max_width, const std::string& value, float scale = 1.0f);
    void fill(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);
    void line(float x1, float y1, float x2, float y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);
    void rect(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);
    void draw_channel_row(const HootVisualChannel& channel, int ordinal, float y, float width, bool muted, bool solo);
    void draw_keyboard(const HootVisualChannel& channel, float x, float y, float width, float height);
    void draw_spectrum(const std::array<float, SpectrumAnalyzer::kBins>& bins,
                       float x, float y, float width, float height, const char* label);
    void draw_right_panel(const UiModel& model, float x, float y, float width, float height);
    void draw_playlist(const UiModel& model, float x, float y, float width, float height);
    float measure_text_width(const std::string& value, float scale = 1.0f) const;
    void draw_settings(const SettingsView& settings);
    void draw_library(const LibraryView& library);
    void draw_catalog_editor(const CatalogEditorView& editor);
    void draw_warning_overlay(const UiModel& model);
    void draw_top_menu(TopMenu top_menu, const UiModel& model);
    void draw_about_overlay();
    void wrapped_text(float x, float y, float max_width, const std::string& value, float scale = 1.0f, float line_height = 20.0f, int max_lines = 8);

    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<TextState> text_state_;
    std::array<float, HOOT_VISUAL_CHANNELS_MAX> channel_meter_level_{};
    std::array<Uint64, HOOT_VISUAL_CHANNELS_MAX> channel_meter_hold_until_{};
    Uint64 last_frame_tick_ = 0;
    float frame_dt_seconds_ = 1.0f / 60.0f;
    int meter_track_ = -1;
    std::string marquee_text_;
    float marquee_x_ = 0.0f;
};

} // namespace hootgui
