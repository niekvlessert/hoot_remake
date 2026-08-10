#include "retro_renderer.h"

#include "core/utf8_util.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <utility>

#if defined(HOOT_HAVE_SDL3_TTF)
#include <SDL3_ttf/SDL_ttf.h>
#endif

namespace hootgui {
namespace {
constexpr Uint8 kBlack[3] = {3, 4, 8};
constexpr Uint8 kNavy[3] = {16, 18, 44};
constexpr Uint8 kBlue[3] = {104, 132, 255};
constexpr Uint8 kBrightBlue[3] = {137, 159, 255};
constexpr Uint8 kRed[3] = {235, 39, 26};
constexpr Uint8 kMeter[3] = {185, 74, 179};
constexpr Uint8 kMeterDim[3] = {72, 38, 78};
constexpr Uint8 kWhite[3] = {226, 229, 238};
constexpr Uint8 kGrey[3] = {76, 81, 98};
constexpr float kCharW = 8.0f;

std::string mmss(uint64_t frames, uint32_t rate)
{
    const uint64_t total = rate ? frames / rate : 0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02llu:%02llu",
                  static_cast<unsigned long long>(total / 60),
                  static_cast<unsigned long long>(total % 60));
    return buf;
}

bool key_active(const HootVisualChannel& c, int note)
{
    if (note < 0 || note > 127) return false;
    if (note < 64) return (c.key_mask_lo & (uint64_t{1} << note)) != 0;
    return (c.key_mask_hi & (uint64_t{1} << (note - 64))) != 0;
}

bool black_key(int note)
{
    switch (note % 12) {
    case 1: case 3: case 6: case 8: case 10: return true;
    default: return false;
    }
}

std::string note_text(int midi_note)
{
    if (midi_note < 0 || midi_note > 127) return {};
    static constexpr const char* names[12] = {
        "c", "c+", "d", "d+", "e", "f", "f+", "g", "g+", "a", "a+", "b"
    };
    // Match classic Hoot's keycode display (octave = keycode / 12), not the
    // scientific-pitch MIDI convention where middle C is called C4.
    const int octave = midi_note / 12;
    return "o" + std::to_string(octave) + names[midi_note % 12];
}

std::vector<std::string> font_candidates(const std::string& requested)
{
    std::vector<std::string> paths;
    auto add = [&](std::string path) {
        if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end())
            paths.push_back(std::move(path));
    };

    add(requested);
    if (const char* env = std::getenv("HOOT_UI_FONT")) add(env);
#ifdef HOOT_DEFAULT_UI_FONT
    add(HOOT_DEFAULT_UI_FONT);
#endif

#if defined(_WIN32)
    const char* windir = std::getenv("WINDIR");
    const std::filesystem::path fonts = windir && windir[0]
        ? std::filesystem::path(windir) / "Fonts"
        : std::filesystem::path("C:/Windows/Fonts");
    add((fonts / "YuGothM.ttc").string());
    add((fonts / "meiryo.ttc").string());
    add((fonts / "msgothic.ttc").string());
#elif defined(__APPLE__)
    // Apple has renamed/reorganised the Japanese system fonts several times.
    // Try the traditional well-known paths first, then scan all normal macOS
    // font roots. Glyph validation below decides which file is actually usable.
    add("/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc");
    add("/System/Library/Fonts/ヒラギノ角ゴシック W6.ttc");
    add("/System/Library/Fonts/ヒラギノ丸ゴ ProN W4.ttc");
    add("/System/Library/Fonts/ヒラギノ明朝 ProN.ttc");
    add("/System/Library/Fonts/Supplemental/Arial Unicode.ttf");
    add("/Library/Fonts/Arial Unicode.ttf");

    auto scan_font_root = [&](const std::filesystem::path& root) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec)) return;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, ec);
        const std::filesystem::recursive_directory_iterator end;
        for (; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (ext == ".ttf" || ext == ".ttc" || ext == ".otf") add(it->path().string());
        }
    };
    scan_font_root("/System/Library/Fonts");
    scan_font_root("/Library/Fonts");
    if (const char* home = std::getenv("HOME"); home && home[0])
        scan_font_root(std::filesystem::path(home) / "Library/Fonts");
#else
    add("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc");
    add("/usr/share/fonts/opentype/noto/NotoSansCJKjp-Regular.otf");
    add("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc");
    add("/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf");
    add("/usr/share/fonts/truetype/fonts-japanese-gothic.ttf");
#endif
    return paths;
}

#if defined(HOOT_HAVE_SDL3_TTF)
TTF_Font* open_japanese_font(const std::string& requested, float point_size, std::string& selected)
{
    for (const auto& candidate : font_candidates(requested)) {
        if (candidate.empty()) continue;
        TTF_Font* font = TTF_OpenFont(candidate.c_str(), point_size);
        if (!font) continue;
        // Validate both Hiragana and a common CJK ideograph so a Latin-only
        // font can never satisfy the Hoot UI requirement by accident.
        if (TTF_FontHasGlyph(font, 0x3042) && TTF_FontHasGlyph(font, 0x65e5)) {
            selected = candidate;
            return font;
        }
        TTF_CloseFont(font);
    }
    return nullptr;
}
#endif

} // namespace

struct RetroRenderer::TextState {
    std::string status = "Unicode text renderer not initialized";
#if defined(HOOT_HAVE_SDL3_TTF)
    bool ttf_initialized = false;
    TTF_Font* font = nullptr;
    TTF_TextEngine* engine = nullptr;
    TTF_Text* scratch = nullptr;
#endif
};

RetroRenderer::RetroRenderer(SDL_Renderer* renderer, std::string font_path)
    : renderer_(renderer), text_state_(std::make_unique<TextState>())
{
#if defined(HOOT_HAVE_SDL3_TTF)
    if (!TTF_Init()) {
        text_state_->status = std::string("SDL3_ttf initialization failed: ") + SDL_GetError();
        return;
    }
    text_state_->ttf_initialized = true;

    std::string selected;
    text_state_->font = open_japanese_font(font_path, 12.0f, selected);

    if (!text_state_->font) {
        text_state_->status = "No Japanese-capable UI font found";
        return;
    }

    text_state_->engine = TTF_CreateRendererTextEngine(renderer_);
    if (!text_state_->engine) {
        text_state_->status = std::string("SDL3_ttf renderer engine failed: ") + SDL_GetError();
        TTF_CloseFont(text_state_->font);
        text_state_->font = nullptr;
        return;
    }
    text_state_->scratch = TTF_CreateText(text_state_->engine, text_state_->font, "", 0);
    if (!text_state_->scratch) {
        text_state_->status = std::string("SDL3_ttf text object failed: ") + SDL_GetError();
        TTF_DestroyRendererTextEngine(text_state_->engine);
        text_state_->engine = nullptr;
        TTF_CloseFont(text_state_->font);
        text_state_->font = nullptr;
        return;
    }
    TTF_SetTextColor(text_state_->scratch, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2], 255);
    text_state_->status = "UTF-8 UI font: " + selected;
#else
    (void)font_path;
#endif
}

bool RetroRenderer::probe_japanese_font(const std::string& requested, std::string& status)
{
#if defined(HOOT_HAVE_SDL3_TTF)
    if (!TTF_Init()) {
        status = std::string("SDL3_ttf initialization failed: ") + SDL_GetError();
        return false;
    }
    std::string selected;
    TTF_Font* font = open_japanese_font(requested, 12.0f, selected);
    if (!font) {
        status = "No Japanese-capable UI font found";
        TTF_Quit();
        return false;
    }
    TTF_CloseFont(font);
    TTF_Quit();
    status = "Japanese UTF-8 font: " + selected;
    return true;
#else
    (void)requested;
    status = "SDL3_ttf support was not compiled into hootui";
    return false;
#endif
}

RetroRenderer::~RetroRenderer()
{
#if defined(HOOT_HAVE_SDL3_TTF)
    if (text_state_) {
        if (text_state_->scratch) TTF_DestroyText(text_state_->scratch);
        if (text_state_->engine) TTF_DestroyRendererTextEngine(text_state_->engine);
        if (text_state_->font) TTF_CloseFont(text_state_->font);
        if (text_state_->ttf_initialized) TTF_Quit();
    }
#endif
}

bool RetroRenderer::unicode_text_available() const
{
#if defined(HOOT_HAVE_SDL3_TTF)
    return text_state_ && text_state_->scratch != nullptr;
#else
    return false;
#endif
}

const std::string& RetroRenderer::text_status() const
{
    return text_state_->status;
}

void RetroRenderer::fill(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    const SDL_FRect box{x, y, w, h};
    SDL_RenderFillRect(renderer_, &box);
}

void RetroRenderer::line(float x1, float y1, float x2, float y2, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    SDL_RenderLine(renderer_, x1, y1, x2, y2);
}

void RetroRenderer::rect(float x, float y, float w, float h, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
    SDL_SetRenderDrawColor(renderer_, r, g, b, a);
    const SDL_FRect box{x, y, w, h};
    SDL_RenderRect(renderer_, &box);
}

void RetroRenderer::text(float x, float y, const std::string& value, float scale)
{
    if (value.empty()) return;
    SDL_SetRenderDrawColor(renderer_, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2], 255);
#if defined(HOOT_HAVE_SDL3_TTF)
    if (text_state_ && text_state_->scratch) {
        TTF_SetTextString(text_state_->scratch, value.c_str(), value.size());
        SDL_SetRenderScale(renderer_, scale, scale);
        TTF_DrawRendererText(text_state_->scratch, x / scale, y / scale);
        SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
        return;
    }
#endif
    const auto fallback = hoot::utf8::debug_ascii_fallback(value);
    SDL_SetRenderScale(renderer_, scale, scale);
    SDL_RenderDebugText(renderer_, x / scale, y / scale, fallback.c_str());
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
}

void RetroRenderer::clipped_text(float x, float y, float max_width, const std::string& value, float scale)
{
#if defined(HOOT_HAVE_SDL3_TTF)
    if (text_state_ && text_state_->font && text_state_->scratch) {
        const int available = std::max(0, static_cast<int>(std::floor(max_width / scale)));
        int width = 0;
        int height = 0;
        if (TTF_GetStringSize(text_state_->font, value.c_str(), value.size(), &width, &height) && width <= available) {
            text(x, y, value, scale);
            return;
        }

        if (available <= 0) return;
        constexpr const char* ellipsis = "...";
        int ellipsis_width = 0;
        if (!TTF_GetStringSize(text_state_->font, ellipsis, 3, &ellipsis_width, &height)) ellipsis_width = 0;

        int measured_width = 0;
        std::size_t measured_bytes = 0;
        if (ellipsis_width > 0 && available <= ellipsis_width) {
            if (available == ellipsis_width) {
                text(x, y, ellipsis, scale);
                return;
            }
            if (!TTF_MeasureString(text_state_->font, value.c_str(), value.size(), available,
                                   &measured_width, &measured_bytes)) return;
            measured_bytes = hoot::utf8::safe_prefix_bytes(value, std::min(measured_bytes, value.size()));
            text(x, y, value.substr(0, measured_bytes), scale);
            return;
        }

        const int text_width = std::max(1, available - ellipsis_width);
        if (!TTF_MeasureString(text_state_->font, value.c_str(), value.size(), text_width,
                               &measured_width, &measured_bytes)) return;
        measured_bytes = hoot::utf8::safe_prefix_bytes(value, std::min(measured_bytes, value.size()));
        std::string clipped = value.substr(0, measured_bytes);
        if (measured_bytes < value.size() && ellipsis_width > 0) clipped += ellipsis;
        text(x, y, clipped, scale);
        return;
    }
#endif
    const size_t chars = static_cast<size_t>(std::max(0.0f, std::floor(max_width / (kCharW * scale))));
    auto fallback = hoot::utf8::debug_ascii_fallback(value);
    if (fallback.size() > chars) {
        if (chars >= 3) fallback = fallback.substr(0, chars - 3) + "...";
        else fallback.resize(chars);
    }
    text(x, y, fallback, scale);
}


float RetroRenderer::measure_text_width(const std::string& value, float scale) const
{
    if (value.empty()) return 0.0f;
#if defined(HOOT_HAVE_SDL3_TTF)
    if (text_state_ && text_state_->font) {
        int width = 0;
        int height = 0;
        if (TTF_GetStringSize(text_state_->font, value.c_str(), value.size(), &width, &height))
            return static_cast<float>(width) * scale;
    }
#endif
    return static_cast<float>(hoot::utf8::debug_ascii_fallback(value).size()) * kCharW * scale;
}

void RetroRenderer::wrapped_text(float x, float y, float max_width, const std::string& value,
                                 float scale, float line_height, int max_lines)
{
    if (value.empty() || max_lines <= 0) return;

    auto fits = [&](const std::string& line) {
#if defined(HOOT_HAVE_SDL3_TTF)
        if (text_state_ && text_state_->font) {
            int width = 0, height = 0;
            if (TTF_GetStringSize(text_state_->font, line.c_str(), line.size(), &width, &height))
                return static_cast<float>(width) * scale <= max_width;
        }
#endif
        return static_cast<float>(hoot::utf8::debug_ascii_fallback(line).size()) * kCharW * scale <= max_width;
    };

    int line_index = 0;
    size_t paragraph_start = 0;
    while (paragraph_start <= value.size() && line_index < max_lines) {
        const size_t paragraph_end = value.find('\n', paragraph_start);
        const std::string paragraph = value.substr(
            paragraph_start,
            paragraph_end == std::string::npos ? std::string::npos : paragraph_end - paragraph_start);

        std::istringstream words(paragraph);
        std::string word;
        std::string current;
        while (words >> word) {
            const std::string candidate = current.empty() ? word : current + " " + word;
            if (current.empty() || fits(candidate)) {
                current = candidate;
                continue;
            }
            clipped_text(x, y + line_index * line_height, max_width, current, scale);
            ++line_index;
            if (line_index >= max_lines) return;
            current = word;
        }
        if (!current.empty() && line_index < max_lines) {
            clipped_text(x, y + line_index * line_height, max_width, current, scale);
            ++line_index;
        } else if (paragraph.empty() && line_index < max_lines) {
            ++line_index;
        }

        if (paragraph_end == std::string::npos) break;
        paragraph_start = paragraph_end + 1;
    }
}

void RetroRenderer::draw_keyboard(const HootVisualChannel& c, float x, float y, float width, float height)
{
    constexpr int first_note = 21;  // A0
    constexpr int last_note = 108;  // C8
    constexpr int note_count = last_note - first_note + 1;
    const float key_w = width / static_cast<float>(note_count);

    fill(x, y, width, height, kWhite[0], kWhite[1], kWhite[2]);
    for (int note = first_note; note <= last_note; ++note) {
        const float kx = x + (note - first_note) * key_w;
        if (!black_key(note)) {
            line(kx, y, kx, y + height, 66, 68, 78);
            if (key_active(c, note) || (c.active && c.midi_note == note))
                fill(kx + 0.5f, y + height * 0.53f, std::max(1.0f, key_w - 1.0f), height * 0.45f,
                     kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
        }
    }
    for (int note = first_note; note <= last_note; ++note) {
        if (!black_key(note)) continue;
        const float kx = x + (note - first_note) * key_w;
        fill(kx - key_w * 0.22f, y, std::max(1.0f, key_w * 0.62f), height * 0.58f, 8, 9, 11);
        if (key_active(c, note) || (c.active && c.midi_note == note))
            fill(kx - key_w * 0.20f, y + 1.0f, std::max(1.0f, key_w * 0.55f), height * 0.52f,
                 kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    }
    rect(x, y, width, height, 92, 96, 110);
}

void RetroRenderer::draw_channel_row(const HootVisualChannel& channel, int ordinal, float y, float width, bool muted, bool solo)
{
    // Original-Hoot style: keyboard on top, then channel description, a
    // centre-origin stereo activity meter and the current note/tone beneath it.
    // Keep the left edge compact like classic Hoot: channel number plus a
    // persistent mute/solo marker.  Muting never stops the guest driver clock.
    const Uint8* marker = solo ? kBrightBlue : (muted ? kRed : kBlue);
    SDL_SetRenderDrawColor(renderer_, marker[0], marker[1], marker[2], 255);
    char idx[8];
    if (solo) std::snprintf(idx, sizeof(idx), "S%02X", ordinal & 0xff);
    else if (muted) std::snprintf(idx, sizeof(idx), "M%02X", ordinal & 0xff);
    else std::snprintf(idx, sizeof(idx), " %02X", ordinal & 0xff);
    SDL_RenderDebugText(renderer_, 8.0f, y + 8.0f, idx);

    const float keyboard_x = 40.0f;
    const float keyboard_y = y + 1.0f;
    const float keyboard_w = width - 50.0f;
    draw_keyboard(channel, keyboard_x, keyboard_y, keyboard_w, 13.0f);

    constexpr float info_y_offset = 17.0f;
    constexpr float label_w = 142.0f;
    constexpr float note_w = 54.0f;
    clipped_text(keyboard_x, y + info_y_offset, label_w, channel.label, 0.85f);
    if (muted) {
        text(keyboard_x + label_w - 35.0f, y + info_y_offset, "M", 0.78f);
    } else if (solo) {
        text(keyboard_x + label_w - 35.0f, y + info_y_offset, "S", 0.78f);
    }

    const std::size_t meter_index = static_cast<std::size_t>(
        std::clamp(ordinal, 0, HOOT_VISUAL_CHANNELS_MAX - 1));
    const float shown = channel_meter_level_[meter_index];

    const float meter_x = keyboard_x + label_w + 8.0f;
    const float meter_w = std::max(40.0f, keyboard_w - label_w - note_w - 18.0f);
    const float meter_y = y + 21.0f;
    const float centre = meter_x + meter_w * 0.5f;
    const float half = meter_w * 0.5f - 4.0f;

    // A faint rail makes the decay readable even at low levels. The actual
    // activity grows outwards from the middle. Pan determines whether energy
    // is shown on both sides, left only, right only, or proportionally between.
    fill(meter_x, meter_y, meter_w, 4.0f, kMeterDim[0], kMeterDim[1], kMeterDim[2]);
    const float pan = channel.pan < 0
        ? static_cast<float>(channel.pan) / 64.0f
        : static_cast<float>(channel.pan) / 63.0f;
    const float left_gain = pan <= 0.0f ? 1.0f : 1.0f - pan;
    const float right_gain = pan >= 0.0f ? 1.0f : 1.0f + pan;
    const float left_extent = half * shown * std::clamp(left_gain, 0.0f, 1.0f);
    const float right_extent = half * shown * std::clamp(right_gain, 0.0f, 1.0f);
    const Uint8 meter_r = muted ? static_cast<Uint8>(kMeterDim[0] + 18) : kMeter[0];
    const Uint8 meter_g = muted ? static_cast<Uint8>(kMeterDim[1] + 10) : kMeter[1];
    const Uint8 meter_b = muted ? static_cast<Uint8>(kMeterDim[2] + 18) : kMeter[2];
    if (left_extent > 0.5f)
        fill(centre - left_extent - 2.0f, meter_y, left_extent, 4.0f, meter_r, meter_g, meter_b);
    if (right_extent > 0.5f)
        fill(centre + 2.0f, meter_y, right_extent, 4.0f, meter_r, meter_g, meter_b);
    fill(centre - 1.0f, meter_y - 1.0f, 2.0f, 6.0f, kBlue[0], kBlue[1], kBlue[2]);

    std::string tone = note_text(channel.midi_note);
    if (tone.empty() && channel.instrument >= 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "T%02X", channel.instrument & 0xff);
        tone = buf;
    }
    clipped_text(keyboard_x + keyboard_w - note_w + 4.0f, y + info_y_offset, note_w - 4.0f, tone, 0.85f);
}

void RetroRenderer::draw_spectrum(const std::array<float, SpectrumAnalyzer::kBins>& bins,
                                  float x, float y, float width, float height, const char* label)
{
    text(x, y - 12.0f, label);
    rect(x, y, width, height, kBlue[0], kBlue[1], kBlue[2]);
    const float bw = width / static_cast<float>(bins.size());
    for (size_t i = 0; i < bins.size(); ++i) {
        const float bh = std::round(std::clamp(bins[i], 0.0f, 1.0f) * (height - 4.0f) / 4.0f) * 4.0f;
        if (bh <= 0.0f) continue;
        fill(x + i * bw + 1.0f, y + height - bh - 2.0f, std::max(1.0f, bw - 2.0f), bh,
             kBlue[0], kBlue[1], kBlue[2]);
    }
}

void RetroRenderer::draw_right_panel(const UiModel& model, float x, float y, float width, float height)
{
    (void)height;
    text(x + 12, y + 8, "hoot... - Sound Hardware Emulator");
    line(x, y + 27, x + width, y + 27, 62, 69, 110);

    text(x + 12, y + 38, "Architecture");
    text(x + 12, y + 54, "Driver : " + std::string(model.visual.driver));
    text(x + 12, y + 68, "CPU    : " + std::string(model.visual.cpu));
    text(x + 12, y + 82, "Device : " + std::string(model.visual.device));
    text(x + 12, y + 96, "System : " + std::string(model.visual.architecture));

    const float spec_x = x + width * 0.45f;
    text(spec_x, y + 38, "Spectrum Analyzer");
    const float spec_w = (width * 0.53f - 18.0f) / 2.0f;
    draw_spectrum(model.spectrum_left, spec_x, y + 65, spec_w, 76, "L");
    draw_spectrum(model.spectrum_right, spec_x + spec_w + 14, y + 65, spec_w, 76, "R");

    const float regs_y = y + 158;
    text(x + 12, regs_y, "CPU Registers");
    const int cols = 2;
    const int rows = 10;
    for (uint32_t i = 0; i < model.visual.register_count && i < static_cast<uint32_t>(cols * rows); ++i) {
        const int col = static_cast<int>(i) / rows;
        const int row = static_cast<int>(i) % rows;
        std::string s = std::string(model.visual.registers[i].label) + ": " + model.visual.registers[i].value;
        clipped_text(x + 12 + col * 128.0f, regs_y + 17 + row * 13.0f, 124.0f, s);
    }

    const float work_x = x + 278.0f;
    const float work_y = regs_y;
    text(work_x, work_y, "Driver Work");
    const bool has_work = model.visual.driver_work_size != 0;
    const bool any_work_data = has_work && std::any_of(
        model.visual.driver_work,
        model.visual.driver_work + std::min<uint32_t>(model.visual.driver_work_size, HOOT_VISUAL_DRIVER_WORK_MAX),
        [](uint8_t value) { return value != 0; });
    if (!has_work) {
        clipped_text(work_x, work_y + 20, width - 290.0f,
                     "No live workspace is published by this driver.", 0.85f);
        clipped_text(work_x, work_y + 38, width - 290.0f,
                     "Playback/chip state is shown in the channel and register views.", 0.75f);
    } else if (!any_work_data) {
        clipped_text(work_x, work_y + 20, width - 290.0f,
                     "Published workspace is currently all 00.", 0.85f);
        clipped_text(work_x, work_y + 38, width - 290.0f,
                     "This does not mean that playback is silent or broken.", 0.75f);
    } else {
        std::ostringstream hdr;
        hdr << std::hex << std::uppercase << std::setfill('0');
        hdr << "      0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F";
        clipped_text(work_x, work_y + 15, width - 290.0f, hdr.str(), 0.75f);
        const int lines = std::min<int>(22, static_cast<int>((model.visual.driver_work_size + 15) / 16));
        for (int row = 0; row < lines; ++row) {
            std::ostringstream os;
            os << std::hex << std::uppercase << std::setfill('0')
               << std::setw(4) << ((model.visual.driver_work_base + row * 16) & 0xffff) << " ";
            for (int col = 0; col < 16; ++col) {
                const int idx = row * 16 + col;
                if (idx < static_cast<int>(model.visual.driver_work_size))
                    os << std::setw(2) << static_cast<unsigned>(model.visual.driver_work[idx]) << ' ';
                else os << "   ";
            }
            clipped_text(work_x, work_y + 28 + row * 11.0f, width - 290.0f, os.str(), 0.75f);
        }
    }
}

void RetroRenderer::draw_playlist(const UiModel& model, float x, float y, float width, float height)
{
    fill(x, y, width, height, kNavy[0], kNavy[1], kNavy[2]);
    rect(x, y, width, height, kGrey[0], kGrey[1], kGrey[2]);

    // The original UI kept the current song moving underneath the channel
    // meters. Use the actual selected track title (not merely the pack title)
    // and keep it in a clipped single-line marquee.
    std::string track_title;
    if (model.selected_track >= 0 && model.selected_track < static_cast<int>(model.tracks.size()))
        track_title = model.tracks[static_cast<std::size_t>(model.selected_track)].title;
    if (track_title.empty()) track_title = model.entry.title;

    std::string now = "NOW PLAYING  :)  :: " + track_title;
    if (model.entry.title[0] && track_title != model.entry.title)
        now += "  [" + std::string(model.entry.title) + "]";
    if (now != marquee_text_) {
        marquee_text_ = now;
        marquee_x_ = x + 8.0f;
    }

    const float marquee_width = measure_text_width(marquee_text_);
    marquee_x_ += 38.0f * frame_dt_seconds_;
    if (marquee_x_ > x + width - 8.0f)
        marquee_x_ = x + 8.0f - marquee_width;

    SDL_Rect old_clip{};
    SDL_GetRenderClipRect(renderer_, &old_clip);
    const bool had_clip = SDL_RenderClipEnabled(renderer_);
    const SDL_Rect marquee_clip{
        static_cast<int>(std::floor(x + 4.0f)),
        static_cast<int>(std::floor(y + 2.0f)),
        static_cast<int>(std::ceil(width - 8.0f)),
        19
    };
    SDL_SetRenderClipRect(renderer_, &marquee_clip);
    text(marquee_x_, y + 6.0f, marquee_text_);
    SDL_SetRenderClipRect(renderer_, had_clip ? &old_clip : nullptr);
    line(x, y + 23, x + width, y + 23, 66, 72, 112);

    const float row_h = 17.0f;
    const int visible = std::max(1, static_cast<int>((height - 31.0f) / row_h));
    const int start = std::clamp(model.playlist_scroll, 0, std::max(0, static_cast<int>(model.tracks.size()) - visible));

    for (int row = 0; row < visible && start + row < static_cast<int>(model.tracks.size()); ++row) {
        const int index = start + row;
        const float ry = y + 28.0f + row * row_h;
        if (index == model.selected_track) fill(x + 1, ry - 2, width - 2, row_h, 40, 48, 124);
        char number[32];
        std::snprintf(number, sizeof(number), "%c %03d : ", index == model.selected_track ? '>' : ' ', index + 1);
        text(x + 8, ry, number);
        clipped_text(x + 74, ry, width - 92, model.tracks[static_cast<size_t>(index)].title);
    }
}


void RetroRenderer::draw_settings(const SettingsView& settings)
{
    if (!settings.document) return;

    constexpr float px = 110.0f;
    constexpr float py = 56.0f;
    constexpr float pw = 1220.0f;
    constexpr float ph = 788.0f;
    constexpr float tabs_x = px + 16.0f;
    constexpr float tabs_w = 176.0f;
    constexpr float content_x = px + 210.0f;
    constexpr float content_w = pw - 226.0f;
    constexpr float rows_y = py + 96.0f;
    constexpr float row_h = 40.0f;
    constexpr int visible_rows = 13;

    // Modal scrim + crisp retro dialog.
    fill(0, 0, kLogicalWidth, kLogicalHeight, 0, 0, 0, 185);
    fill(px, py, pw, ph, 11, 14, 31);
    rect(px, py, pw, ph, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    fill(px + 1, py + 1, pw - 2, 52, 22, 28, 62);
    text(px + 18, py + 14, "Settings", 1.5f);
    clipped_text(px + 172, py + 18, pw - 190, settings.config_path, 0.9f);
    line(px, py + 53, px + pw, py + 53, 78, 88, 146);

    static const std::pair<const char*, const char*> tabs[] = {
        {"general", "General"}, {"player", "Player"}, {"gui", "Interface"},
        {"midi", "MIDI"}, {"x68k", "X68000"}, {"psg", "PSG / SSG"},
        {"pc98", "PC-98"}, {"pc88", "PC-88"}, {"environment", "Environment"}
    };
    for (size_t i = 0; i < std::size(tabs); ++i) {
        const float ty = py + 72.0f + static_cast<float>(i) * 42.0f;
        const bool active = settings.section == tabs[i].first;
        if (active) fill(tabs_x, ty - 5, tabs_w, 31, 35, 46, 103);
        rect(tabs_x, ty - 5, tabs_w, 31, active ? kBrightBlue[0] : kGrey[0],
             active ? kBrightBlue[1] : kGrey[1], active ? kBrightBlue[2] : kGrey[2]);
        text(tabs_x + 12, ty + 4, tabs[i].second);
    }

    text(content_x, py + 70, "Override");
    text(content_x + 92, py + 70, "Setting");
    text(content_x + 355, py + 70, "Value");
    line(content_x, py + 91, content_x + content_w, py + 91, 64, 72, 118);

    if (settings.section == "environment") {
        text(content_x, rows_y + 8, "Advanced environment pass-through");
        text(content_x, rows_y + 34, "Existing NAME=value entries are preserved when settings are saved.");
        text(content_x, rows_y + 60, "For arbitrary new variables, edit the [environment] section directly.");
        int row = 0;
        for (const auto& pair : settings.document->environment) {
            if (row >= 10) break;
            clipped_text(content_x, rows_y + 98 + row * 26.0f, content_w - 10,
                         pair.first + " = " + pair.second);
            ++row;
        }
    } else {
        std::vector<int> indices;
        for (size_t i = 0; i < settings.document->values.size(); ++i) {
            const auto& item = settings.document->values[i];
            if (item.spec && settings.section == item.spec->section) indices.push_back(static_cast<int>(i));
        }
        const int max_scroll = std::max(0, static_cast<int>(indices.size()) - visible_rows);
        const int scroll = std::clamp(settings.scroll, 0, max_scroll);
        for (int row = 0; row < visible_rows && scroll + row < static_cast<int>(indices.size()); ++row) {
            const int index = indices[static_cast<size_t>(scroll + row)];
            const auto& item = settings.document->values[static_cast<size_t>(index)];
            const float ry = rows_y + row * row_h;
            if (index == settings.selected) fill(content_x - 4, ry - 4, content_w, row_h - 2, 28, 37, 82);

            const float bx = content_x + 18;
            rect(bx, ry + 5, 18, 18, 98, 109, 165);
            if (item.enabled) {
                fill(bx + 4, ry + 9, 10, 10, kRed[0], kRed[1], kRed[2]);
            }
            clipped_text(content_x + 92, ry + 7, 245, item.spec->label);

            const float vx = content_x + 355;
            const float vw = content_w - 367;
            fill(vx, ry + 1, vw, 28, item.enabled ? 18 : 13, item.enabled ? 24 : 17, item.enabled ? 50 : 34);
            rect(vx, ry + 1, vw, 28, 65, 74, 122);
            std::string shown = item.value.empty() ? item.spec->default_value : item.value;
            if (item.spec->kind == hoot::HootSettingKind::Boolean)
                shown = (shown == "1" || shown == "true" || shown == "yes" || shown == "on") ? "ON" : "OFF";
            if (!item.enabled) {
                const std::string reference = shown.empty() ? std::string{} : ("; reference " + shown);
                shown = "AUTO / catalogue / built-in" + reference;
            }
            clipped_text(vx + 8, ry + 8, vw - 16, shown);
            if (index == settings.selected && settings.editing)
                rect(vx - 2, ry - 1, vw + 4, 32, kRed[0], kRed[1], kRed[2]);
        }

        if (settings.selected >= 0 && settings.selected < static_cast<int>(settings.document->values.size())) {
            const auto& selected = settings.document->values[static_cast<size_t>(settings.selected)];
            if (selected.spec && settings.section == selected.spec->section) {
                fill(content_x, py + 628, content_w - 2, 74, 14, 18, 40);
                rect(content_x, py + 628, content_w - 2, 74, 54, 63, 108);
                text(content_x + 10, py + 638,
                     std::string(selected.spec->section) + "." + selected.spec->key);
                clipped_text(content_x + 10, py + 660, content_w - 22, selected.spec->description);
                if (selected.spec->kind == hoot::HootSettingKind::Choice && selected.spec->choices[0])
                    clipped_text(content_x + 10, py + 680, content_w - 22,
                                 std::string("Choices: ") + selected.spec->choices, 0.85f);
            }
        }
    }

    const float footer_y = py + ph - 66.0f;
    line(px, footer_y - 12, px + pw, footer_y - 12, 67, 76, 126);
    text(px + 18, footer_y + 2, settings.dirty ? "Unsaved changes" : "No unsaved changes");
    if (!settings.message.empty()) clipped_text(px + 190, footer_y + 2, 610, settings.message);
    fill(px + pw - 210, footer_y - 8, 86, 32, 29, 43, 91);
    rect(px + pw - 210, footer_y - 8, 86, 32, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    text(px + pw - 188, footer_y + 2, "Save");
    fill(px + pw - 112, footer_y - 8, 86, 32, 38, 32, 47);
    rect(px + pw - 112, footer_y - 8, 86, 32, 122, 103, 127);
    text(px + pw - 91, footer_y + 2, "Cancel");
    text(px + 18, py + ph - 22,
         "Override OFF = do not force this setting; catalogue/autodetection/built-in behavior wins. Reference values are not automatically active overrides.", 0.82f);
}



void RetroRenderer::draw_library(const LibraryView& library)
{
    if (!library.rows) return;

    constexpr float px = kLibraryX;
    constexpr float py = kLibraryY;
    constexpr float pw = kLibraryWidth;
    constexpr float ph = kLibraryHeight;
    constexpr float header_h = 62.0f;
    constexpr float search_y = py + 76.0f;
    constexpr float list_y = py + 126.0f;
    constexpr float row_h = 21.0f;
    constexpr float footer_y = py + ph - 68.0f;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(0, 0, kLogicalWidth, kLogicalHeight, 0, 0, 0, 176);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    fill(px, py, pw, ph, 9, 12, 28);
    rect(px, py, pw, ph, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    fill(px + 1, py + 1, pw - 2, header_h, 20, 27, 61);
    text(px + 20, py + 15, "Hoot Library", 1.55f);
    library_breadcrumb_hits_.clear();
    if (library.breadcrumb_parts.empty()) {
        clipped_text(px + 190, py + 20, pw - 250, library.breadcrumb, 0.95f);
    } else {
        float breadcrumb_x = px + 190.0f;
        for (std::size_t i = 0; i < library.breadcrumb_parts.size(); ++i) {
            const auto& part = library.breadcrumb_parts[i];
            if (i > 0) {
                const std::string separator = i == 1 ? " " : " / ";
                text(breadcrumb_x, py + 20, separator, 0.95f);
                breadcrumb_x += measure_text_width(separator, 0.95f);
            }
            const float part_width = measure_text_width(part.label, 0.95f);
            if (part.clickable)
                line(breadcrumb_x, py + 45, breadcrumb_x + part_width, py + 45, 104, 132, 255);
            text(breadcrumb_x, py + 20, part.label, 0.95f);
            library_breadcrumb_hits_.push_back({breadcrumb_x, py + 8,
                                                 part.clickable ? part_width : 0.0f, 42});
            breadcrumb_x += part_width;
        }
    }

    if (library.can_edit) {
        fill(px + pw - 170, py + 13, 108, 32, 29, 43, 91);
        rect(px + pw - 170, py + 13, 108, 32, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
        text(px + pw - 147, py + 23, "Edit entry");
    }
    // Close X.
    fill(px + pw - 47, py + 13, 32, 32, 36, 31, 46);
    rect(px + pw - 47, py + 13, 32, 32, 109, 107, 143);
    text(px + pw - 36, py + 23, "X");

    text(px + 20, search_y + 7, "Search:");
    constexpr float filter_x = px + pw - 216.0f;
    constexpr float search_w = pw - 318.0f;
    fill(px + 88, search_y, search_w, 31, library.search_editing ? 23 : 15,
         library.search_editing ? 31 : 20, library.search_editing ? 67 : 43);
    rect(px + 88, search_y, search_w, 31,
         library.search_editing ? kBrightBlue[0] : kGrey[0],
         library.search_editing ? kBrightBlue[1] : kGrey[1],
         library.search_editing ? kBrightBlue[2] : kGrey[2]);
    clipped_text(px + 98, search_y + 9, search_w - 20,
                 library.search.empty() ? (library.search_editing ? "" : "Press F or Ctrl+F to filter") : library.search);

    fill(filter_x, search_y, 196, 31, library.available_only ? 25 : 15,
         library.available_only ? 47 : 20, library.available_only ? 48 : 43);
    rect(filter_x, search_y, 196, 31,
         library.available_only ? 86 : kGrey[0],
         library.available_only ? 151 : kGrey[1],
         library.available_only ? 104 : kGrey[2]);
    rect(filter_x + 8, search_y + 7, 17, 17,
         library.available_only ? 116 : 80,
         library.available_only ? 190 : 86,
         library.available_only ? 128 : 96);
    if (library.available_only) text(filter_x + 11, search_y + 9, "x", 0.82f);
    text(filter_x + 33, search_y + 9, "Available only", 0.90f);

    // Column headings.
    text(px + 20, list_y - 18, "Name");
    text(px + 755, list_y - 18, "Archive / code");
    text(px + 975, list_y - 18, "Status");
    line(px + 16, list_y - 4, px + pw - 16, list_y - 4, 67, 76, 126);

    const auto& rows = *library.rows;
    const int max_scroll = std::max(0, static_cast<int>(rows.size()) - kLibraryVisibleRows);
    const int scroll = std::clamp(library.scroll, 0, max_scroll);
    for (int row = 0; row < kLibraryVisibleRows && scroll + row < static_cast<int>(rows.size()); ++row) {
        const int index = scroll + row;
        const auto& item = rows[static_cast<std::size_t>(index)];
        const float ry = list_y + row * row_h;
        if (index == library.selected) fill(px + 12, ry - 2, pw - 24, row_h, 34, 44, 101);

        std::string prefix;
        switch (item.kind) {
        case LibraryRowKind::Parent: prefix = "< "; break;
        case LibraryRowKind::Folder: prefix = "> "; break;
        case LibraryRowKind::Entry: prefix = "  "; break;
        case LibraryRowKind::Track: prefix = "  "; break;
        }
        clipped_text(px + 20, ry + 3, 720, prefix + item.label);
        clipped_text(px + 755, ry + 3, 205, item.detail, 0.88f);

        if (!item.status.empty()) {
            const float sx = px + 975;
            if (item.kind == LibraryRowKind::Entry) {
                fill(sx - 5, ry, 145, 17, item.available ? 20 : 52, item.available ? 54 : 33, item.available ? 40 : 33);
                rect(sx - 5, ry, 145, 17, item.available ? 73 : 124, item.available ? 124 : 78, item.available ? 91 : 78);
            }
            clipped_text(sx, ry + 3, 138, item.status, 0.80f);
        }
    }

    if (rows.empty()) text(px + 22, list_y + 12, "No matching items.");

    line(px + 16, footer_y - 10, px + pw - 16, footer_y - 10, 67, 76, 126);
    if (!library.message.empty())
        clipped_text(px + 20, footer_y + 2, pw - 40, library.message, 0.90f);
    else
        text(px + 20, footer_y + 2,
             "Enter: open/play   Space: play + next   E: edit   A: available only   Backspace/Esc: back   F: search", 0.83f);
    if (!library.pack_location.empty())
        clipped_text(px + 20, py + ph - 24, pw - 40, "Packs: " + library.pack_location, 0.80f);
    else
        text(px + 20, py + ph - 24,
             "Based on original Hoot: driver family / subtype / game / tracks. Folder positions are remembered.", 0.80f);
}

int RetroRenderer::library_breadcrumb_at(float x, float y) const
{
    for (std::size_t i = 0; i < library_breadcrumb_hits_.size(); ++i) {
        const auto& hit = library_breadcrumb_hits_[i];
        if (hit.w > 0.0f && x >= hit.x && x < hit.x + hit.w && y >= hit.y && y < hit.y + hit.h)
            return static_cast<int>(i);
    }
    return -1;
}


void RetroRenderer::draw_catalog_editor(const CatalogEditorView& editor)
{
    if (!editor.entry) return;
    constexpr float px = 110.0f, py = 56.0f, pw = 1220.0f, ph = 788.0f;
    constexpr float tabs_x = px + 16.0f, tabs_w = 176.0f;
    constexpr float content_x = px + 210.0f, content_w = pw - 226.0f;
    constexpr float rows_y = py + 112.0f;
    constexpr float row_h = 34.0f;
    static const char* tabs[] = {"general","tracks","options","assets","hardware","raw"};

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(0, 0, kLogicalWidth, kLogicalHeight, 0, 0, 0, 190);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    fill(px, py, pw, ph, 9, 12, 28);
    rect(px, py, pw, ph, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    fill(px + 1, py + 1, pw - 2, 58, 20, 27, 61);
    text(px + 20, py + 15, "Catalog Editor", 1.55f);
    clipped_text(px + 210, py + 19, pw - 330, editor.entry->id + "  -  " + editor.entry->title, 0.95f);
    if (editor.locally_modified) text(px + pw - 208, py + 20, "LOCAL OVERRIDE", 0.82f);

    fill(px + pw - 47, py + 13, 32, 32, 36, 31, 46);
    rect(px + pw - 47, py + 13, 32, 32, 109, 107, 143);
    text(px + pw - 36, py + 23, "X");

    for (int i = 0; i < 6; ++i) {
        const float ty = py + 82.0f + i * 43.0f;
        if (editor.tab == tabs[i]) fill(tabs_x - 4, ty - 5, tabs_w, 31, 31, 43, 94);
        text(tabs_x + 10, ty + 3, tabs[i]);
    }

    auto field = [&](int row, const std::string& label, const std::string& value) {
        const float ry = rows_y + row * 46.0f;
        if (row == editor.selected) fill(content_x - 4, ry - 4, content_w, 38, 28, 37, 82);
        clipped_text(content_x + 8, ry + 8, 250, label);
        fill(content_x + 278, ry, content_w - 292, 31, 16, 21, 46);
        rect(content_x + 278, ry, content_w - 292, 31, 63, 73, 121);
        std::string shown = value;
        if (row == editor.selected && editor.editing) shown = editor.edit_buffer;
        clipped_text(content_x + 288, ry + 8, content_w - 312, shown);
    };

    if (editor.tab == "general") {
        field(0, "Title", editor.entry->title);
        field(1, "Archive", editor.entry->archive);
        field(2, "Driver family", editor.entry->driver_name);
        field(3, "Driver subtype", editor.entry->driver_type);
        field(4, "Sample rate", std::to_string(editor.entry->default_sample_rate));
        field(5, "Refresh Hz", std::to_string(editor.entry->refresh_hz));
        text(content_x + 8, py + 430, "The entry id is stable and is not changed by the editor.", 0.85f);
    } else if (editor.tab == "tracks") {
        text(content_x + 8, rows_y - 27, "# / code");
        text(content_x + 150, rows_y - 27, "Title");
        text(content_x + 720, rows_y - 27, "Voice bank");
        const int scroll = std::max(0, editor.scroll);
        for (int row = 0; row < 16 && scroll + row < static_cast<int>(editor.entry->tracks.size()); ++row) {
            const int index = scroll + row;
            const auto& t = editor.entry->tracks[static_cast<size_t>(index)];
            const float ry = rows_y + row * row_h;
            if (index == editor.selected) fill(content_x - 4, ry - 2, content_w, row_h - 2, 28, 37, 82);
            char code[32]; std::snprintf(code, sizeof(code), "%3d  0x%X", index + 1, t.code);
            clipped_text(content_x + 8, ry + 7, 125, (index == editor.selected && editor.editing && editor.edit_column == 0) ? editor.edit_buffer : code, 0.88f);
            clipped_text(content_x + 150, ry + 7, 550, (index == editor.selected && editor.editing && editor.edit_column == 1) ? editor.edit_buffer : t.title, 0.88f);
            clipped_text(content_x + 720, ry + 7, 235, (index == editor.selected && editor.editing && editor.edit_column == 2) ? editor.edit_buffer : t.voice_bank, 0.83f);
        }
        text(content_x + 8, py + 690, "Enter: edit title   C: code   V: voice bank   Insert: add track   Delete: remove   Ctrl+V in title edits supports Japanese", 0.80f);
    } else if (editor.tab == "options") {
        text(content_x + 8, rows_y - 27, "Option");
        text(content_x + 560, rows_y - 27, "Value");
        int n = 0;
        const int scroll = std::max(0, editor.scroll);
        for (auto it = editor.entry->options.begin(); it != editor.entry->options.end(); ++it, ++n) {
            if (n < scroll || n >= scroll + 16) continue;
            const int row = n - scroll;
            const float ry = rows_y + row * row_h;
            if (n == editor.selected) fill(content_x - 4, ry - 2, content_w, row_h - 2, 28, 37, 82);
            clipped_text(content_x + 8, ry + 7, 520, (n == editor.selected && editor.editing && editor.edit_column == 0) ? editor.edit_buffer : it->first, 0.88f);
            clipped_text(content_x + 560, ry + 7, 220, (n == editor.selected && editor.editing && editor.edit_column == 1) ? editor.edit_buffer : std::to_string(it->second), 0.88f);
        }
        text(content_x + 8, py + 690, "Enter: edit value   N: rename option   Insert: add option   Delete: remove", 0.80f);
    } else if (editor.tab == "assets") {
        text(content_x + 8, rows_y - 27, "Type"); text(content_x + 150, rows_y - 27, "Path");
        text(content_x + 665, rows_y - 27, "Transform"); text(content_x + 845, rows_y - 27, "Offset");
        const int scroll = std::max(0, editor.scroll);
        for (int row = 0; row < 16 && scroll + row < static_cast<int>(editor.entry->assets.size()); ++row) {
            const int index = scroll + row;
            const auto& a = editor.entry->assets[static_cast<size_t>(index)];
            const float ry = rows_y + row * row_h;
            if (index == editor.selected) fill(content_x - 4, ry - 2, content_w, row_h - 2, 28, 37, 82);
            clipped_text(content_x + 8, ry + 7, 125, (index == editor.selected && editor.editing && editor.edit_column == 0) ? editor.edit_buffer : a.type, 0.82f);
            clipped_text(content_x + 150, ry + 7, 495, (index == editor.selected && editor.editing && editor.edit_column == 1) ? editor.edit_buffer : a.path, 0.82f);
            clipped_text(content_x + 665, ry + 7, 160, (index == editor.selected && editor.editing && editor.edit_column == 2) ? editor.edit_buffer : a.transform, 0.82f);
            const std::string off = "0x" + [&](){ std::ostringstream o; o << std::hex << a.offset; return o.str(); }();
            clipped_text(content_x + 845, ry + 7, 115, (index == editor.selected && editor.editing && editor.edit_column == 3) ? editor.edit_buffer : off, 0.82f);
        }
        text(content_x + 8, py + 690, "Enter: path   T: type   R: transform   O: offset   Insert: add asset   Delete: remove", 0.80f);
    } else if (editor.tab == "hardware") {
        text(content_x + 8, rows_y, "MIDI / module target", 1.05f);
        int type = -1;
        if (auto it = editor.entry->options.find("midiout_type"); it != editor.entry->options.end()) type = it->second;
        const char* names[] = {"Catalog/default", "MT-32 emulation", "MT-32", "CM-64", "GS / SC-55", "Korg M1", "Vermouth", "SC-88", "General MIDI"};
        int ni = type < 1 || type > 8 ? 0 : type;
        fill(content_x + 8, rows_y + 38, content_w - 24, 42, 18, 25, 55);
        rect(content_x + 8, rows_y + 38, content_w - 24, 42, 74, 91, 153);
        text(content_x + 24, rows_y + 52, std::string("<   ") + names[ni] + "   >", 1.05f);
        text(content_x + 8, rows_y + 112, "Left/Right cycles Hoot midiout_type. Runtime ROM/backend warnings still come from libhoot.", 0.88f);
        text(content_x + 8, rows_y + 140, "CM-64 uses CM-32L + CM-32P; SC-55 can use Nuked-SC55; SC-88/M1 may use compatibility rendering.", 0.84f);
    } else {
        text(content_x + 8, rows_y, "Local override file", 1.05f);
        wrapped_text(content_x + 8, rows_y + 34, content_w - 30, editor.override_path, 0.90f, 22.0f, 3);
        text(content_x + 8, rows_y + 118, "Format: hoot-user-overrides / version 1", 0.90f);
        text(content_x + 8, rows_y + 148, "The base XML/JSON/SQLite catalog is never modified. This file is applied after the base catalog and hoot-overrides.xml.", 0.86f);
        text(content_x + 8, rows_y + 180, "It is ordinary UTF-8 JSON and can also be version-controlled or edited externally.", 0.86f);
    }

    const float footer_y = py + ph - 66.0f;
    line(px, footer_y - 12, px + pw, footer_y - 12, 67, 76, 126);
    text(px + 18, footer_y + 2, editor.dirty ? "Unsaved changes" : (editor.locally_modified ? "Local override active" : "Base catalog entry"));
    if (!editor.message.empty()) clipped_text(px + 210, footer_y + 2, 570, editor.message, 0.84f);
    fill(px + pw - 430, footer_y - 8, 90, 32, 43, 36, 35); rect(px + pw - 430, footer_y - 8, 90, 32, 148, 101, 84); text(px + pw - 410, footer_y + 2, "Reset");
    fill(px + pw - 326, footer_y - 8, 104, 32, 31, 39, 72); rect(px + pw - 326, footer_y - 8, 104, 32, 104, 116, 173); text(px + pw - 310, footer_y + 2, "Duplicate");
    fill(px + pw - 208, footer_y - 8, 80, 32, 29, 43, 91); rect(px + pw - 208, footer_y - 8, 80, 32, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]); text(px + pw - 188, footer_y + 2, "Save");
    fill(px + pw - 116, footer_y - 8, 88, 32, 38, 32, 47); rect(px + pw - 116, footer_y - 8, 88, 32, 122, 103, 127); text(px + pw - 96, footer_y + 2, "Cancel");
    text(px + 18, py + ph - 22, "Ctrl+S: save   D: duplicate variant   Esc: close/cancel edit   Changes are applied immediately after Save.", 0.80f);
}

void RetroRenderer::draw_warning_overlay(const UiModel& model)
{
    if (model.warning.empty() || !model.warning_overlay_visible) return;

    constexpr float px = kWarningX;
    constexpr float py = kWarningY;
    constexpr float pw = kWarningWidth;
    constexpr float ph = kWarningHeight;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(0, 0, kLogicalWidth, kLogicalHeight, 0, 0, 0, 190);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    fill(px, py, pw, ph, 14, 17, 34);
    rect(px, py, pw, ph, 235, 160, 64);
    fill(px + 1, py + 1, pw - 2, 58, 67, 39, 18);
    text(px + 22, py + 17, "Playback hardware / resource notice", 1.35f);

    // Dismiss X in the title bar.
    fill(px + pw - 48, py + 12, 34, 34, 45, 31, 29);
    rect(px + pw - 48, py + 12, 34, 34, 176, 116, 72);
    text(px + pw - 36, py + 22, "X");

    text(px + 26, py + 84, "This track requested resources that are not fully available.");
    text(px + 26, py + 108, "Playback can continue, but sound may be incomplete or less authentic.", 0.95f);

    fill(px + 26, py + 143, pw - 52, 138, 25, 24, 34);
    rect(px + 26, py + 143, pw - 52, 138, 106, 83, 57);
    text(px + 40, py + 156, "Hoot reports:", 0.95f);
    wrapped_text(px + 40, py + 180, pw - 80, model.warning, 0.92f, 21.0f, 4);

    fill(px + 26, py + 299, pw - 52, 104, 18, 24, 47);
    rect(px + 26, py + 299, pw - 52, 104, 61, 77, 128);
    text(px + 40, py + 312, "What to do", 0.95f);
    wrapped_text(px + 40, py + 337, pw - 80, model.warning_help, 0.90f, 21.0f, 3);

    text(px + 26, py + 414,
         "Default paths are created automatically under ~/.hoot. Restart after changing ROM/backend settings.", 0.83f);

    const float button_y = py + ph - 64.0f;
    fill(px + 26, button_y, 156, 36, 29, 43, 91);
    rect(px + 26, button_y, 156, 36, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    text(px + 47, button_y + 11, "Open Settings");

    fill(px + pw - 126, button_y, 100, 36, 45, 35, 43);
    rect(px + pw - 126, button_y, 100, 36, 130, 111, 122);
    text(px + pw - 94, button_y + 11, "Close");
    text(px + 204, button_y + 12, "S: Settings    Esc/Enter: Close", 0.85f);
}


void RetroRenderer::draw_top_menu(TopMenu top_menu, const UiModel& model)
{
    auto menu_label = [&](float x, float w, const char* label, bool active) {
        if (active) fill(x, kTopMenuY, w, kTopMenuHeight, 32, 39, 83);
        text(x + 8.0f, 10.0f, label);
    };
    menu_label(kEditMenuX, kEditMenuWidth, "Edit", top_menu == TopMenu::Edit);
    menu_label(kPlaybackMenuX, kPlaybackMenuWidth, "Playback", top_menu == TopMenu::Playback);
    menu_label(kViewMenuX, kViewMenuWidth, "View", top_menu == TopMenu::View);
    menu_label(kHelpMenuX, kHelpMenuWidth, "Help", top_menu == TopMenu::Help);

    if (top_menu == TopMenu::Edit) {
        const float x = kEditMenuX;
        const float y = kMenuPopupY;
        const float w = 244.0f;
        const float h = 44.0f;
        fill(x, y, w, h, 17, 21, 44);
        rect(x, y, w, h, 91, 101, 158);
        text(x + 12, y + 14, "Fullscreen");
        text(x + 151, y + 14, "Alt+Enter", 0.86f);
    } else if (top_menu == TopMenu::Playback) {
        const float x = kPlaybackMenuX;
        const float y = kMenuPopupY;
        const float w = kPlaybackPopupWidth;
        const float h = kPlaybackRowHeight * static_cast<float>(kPlaybackRowCount);
        fill(x, y, w, h, 17, 21, 44);
        rect(x, y, w, h, 91, 101, 158);
        const char* labels[kPlaybackRowCount] = {
            model.stopped || model.paused ? "Play" : "Pause",
            "Stop",
            "Restart Track",
            "Previous Track",
            "Next Track",
            model.muted_all ? "Unmute Master" : "Mute Master",
            "Clear Channel Mutes / Solo",
            model.recording ? "Stop WAV Recording" : "Record WAV..."
        };
        const char* shortcuts[kPlaybackRowCount] = {
            "Space", "Ctrl+S", "Ctrl+R", "Left / P", "Right / N", "M", "U", "Ctrl+Shift+R"
        };
        for (int i = 0; i < kPlaybackRowCount; ++i) {
            const float ry = y + i * kPlaybackRowHeight;
            if ((i == 0 || i == 1 || i == 2 || i == 5 || i == 6) && !model.tracks.size()) {
                text(x + 12, ry + 10, labels[i], 0.92f);
            } else {
                text(x + 12, ry + 10, labels[i]);
            }
            text(x + w - 112, ry + 10, shortcuts[i], 0.78f);
            if (i + 1 < kPlaybackRowCount) line(x + 7, ry + kPlaybackRowHeight, x + w - 7, ry + kPlaybackRowHeight, 43, 50, 83);
        }
    } else if (top_menu == TopMenu::Help) {
        const float x = kHelpMenuX;
        const float y = kMenuPopupY;
        const float w = 210.0f;
        const float h = 44.0f;
        fill(x, y, w, h, 17, 21, 44);
        rect(x, y, w, h, 91, 101, 158);
        text(x + 12, y + 14, "About hoot...");
    }
}

void RetroRenderer::draw_about_overlay()
{
    constexpr float px = kAboutX;
    constexpr float py = kAboutY;
    constexpr float pw = kAboutWidth;
    constexpr float ph = kAboutHeight;

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    fill(0, 0, kLogicalWidth, kLogicalHeight, 0, 0, 0, 185);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);

    fill(px, py, pw, ph, 14, 17, 34);
    rect(px, py, pw, ph, 104, 132, 255);
    fill(px + 1, py + 1, pw - 2, 52, 22, 28, 63);
    text(px + 22, py + 17, "About hoot", 1.35f);

    fill(px + pw - 48, py + 10, 34, 32, 36, 32, 47);
    rect(px + pw - 48, py + 10, 34, 32, 122, 103, 127);
    text(px + pw - 36, py + 20, "X");

    text(px + 28, py + 82, "hoot... - Sound Hardware Emulator", 1.30f);
    text(px + 28, py + 116, "Original Hoot copyright (C) 1999-2001 DMP SOFT.", 0.96f);
    text(px + 28, py + 144, "Original site: http://dmpsoft.virtualave.net/", 0.92f);
    line(px + 28, py + 178, px + pw - 28, py + 178, 64, 75, 122);
    text(px + 28, py + 202, "Cross platform port & update: Niek Vlessert", 1.02f);
    text(px + 28, py + 236, "This port keeps Hoot's catalog/driver model while adding modern", 0.86f);
    text(px + 28, py + 256, "cross-platform lib, CLI, SDL UI and Unicode/Japanese support.", 0.86f);

    const float by = py + ph - 50.0f;
    fill(px + pw - 116, by, 88, 30, 29, 43, 91);
    rect(px + pw - 116, by, 88, 30, kBrightBlue[0], kBrightBlue[1], kBrightBlue[2]);
    text(px + pw - 86, by + 9, "OK");
}

void RetroRenderer::draw(const UiModel& model, const SettingsView* settings, const LibraryView* library, const CatalogEditorView* editor, TopMenu top_menu, bool about_open)
{
    const Uint64 tick = SDL_GetTicks();
    if (last_frame_tick_ != 0 && tick >= last_frame_tick_) {
        const float elapsed = static_cast<float>(tick - last_frame_tick_) / 1000.0f;
        frame_dt_seconds_ = std::clamp(elapsed, 1.0f / 240.0f, 0.10f);
    }
    last_frame_tick_ = tick;
    if (meter_track_ != model.selected_track) {
        channel_meter_level_.fill(0.0f);
        meter_track_ = model.selected_track;
    }
    // Fast attack, deliberately slower release. Update every channel even if
    // it is currently scrolled out of view, so returning to a channel bank can
    // never resurrect a stale meter value.
    const std::size_t meter_count = std::min<std::size_t>(model.visual.channel_count, HOOT_VISUAL_CHANNELS_MAX);
    for (std::size_t i = 0; i < meter_count; ++i) {
        const float target = std::clamp(model.visual.channels[i].level, 0.0f, 1.0f);
        float& shown = channel_meter_level_[i];
        if (target >= shown) {
            shown = target;
            channel_meter_hold_until_[i] = tick + 70;
        } else if (tick >= channel_meter_hold_until_[i]) {
            // Classic Hoot's meters drop visibly slower than their attack.
            // Keep a short peak hold and then release smoothly.
            shown = std::max(target, shown - frame_dt_seconds_ * 1.12f);
        }
    }

    SDL_SetRenderDrawColor(renderer_, kBlack[0], kBlack[1], kBlack[2], 255);
    SDL_RenderClear(renderer_);

    // Custom, platform-neutral title/menu area. The dense instrument view below
    // deliberately keeps the original Hoot information hierarchy.
    fill(0, 0, kLogicalWidth, 34, 14, 20, 48);
    text(12, 7, "hoot...  Sound Hardware Emulator", 1.5f);

    auto top_button = [&](float x, float y, float w, float h, const std::string& label) {
        fill(x, y, w, h, 21, 27, 63);
        rect(x, y, w, h, 101, 110, 173);
        const float label_w = measure_text_width(label);
        text(x + std::max(8.0f, (w - label_w) * 0.5f), y + 7.0f, label);
    };
    top_button(kOpenButtonX, kOpenButtonY, kOpenButtonWidth, kOpenButtonHeight, "Open...");
    top_button(kLibraryButtonX, kLibraryButtonY, kLibraryButtonWidth, kLibraryButtonHeight, "Library");
    top_button(kSettingsButtonX, kSettingsButtonY, kSettingsButtonWidth, kSettingsButtonHeight, "Settings");
    line(0, 34, kLogicalWidth, 34, 72, 82, 136);

    constexpr float bottom_h = 255.0f;
    constexpr float status_h = 28.0f;
    const float content_y = kContentY;
    const float content_h = kLogicalHeight - content_y - bottom_h - status_h;
    const float left_w = kLeftPanelWidth;
    const float right_x = left_w + 2.0f;

    fill(0, content_y, left_w, content_h, kBlack[0], kBlack[1], kBlack[2]);
    rect(0, content_y, left_w, content_h, kGrey[0], kGrey[1], kGrey[2]);
    const float row_h = kChannelRowHeight;
    const int visible_channels = std::max(1, static_cast<int>((content_h - 6.0f) / row_h));
    const int max_scroll = std::max(0, static_cast<int>(model.visual.channel_count) - visible_channels);
    const int start = std::clamp(model.channel_scroll, 0, max_scroll);
    for (int row = 0; row < visible_channels && start + row < static_cast<int>(model.visual.channel_count); ++row) {
        const int ordinal = start + row;
        draw_channel_row(model.visual.channels[ordinal], ordinal, content_y + 4.0f + row * row_h, left_w,
                         model.channel_muted[static_cast<size_t>(ordinal)], model.solo_channel == ordinal);
    }

    fill(right_x, content_y, kLogicalWidth - right_x, content_h, kBlack[0], kBlack[1], kBlack[2]);
    rect(right_x, content_y, kLogicalWidth - right_x, content_h, kGrey[0], kGrey[1], kGrey[2]);
    draw_right_panel(model, right_x, content_y, kLogicalWidth - right_x, content_h);

    const float playlist_y = kLogicalHeight - bottom_h - status_h;
    draw_playlist(model, 0, playlist_y, kLogicalWidth, bottom_h);

    const float status_y = kLogicalHeight - status_h;
    fill(0, status_y, kLogicalWidth, status_h, 28, 31, 43);
    line(0, status_y, kLogicalWidth, status_y, 101, 106, 127);
    const std::string state = model.stopped ? "Stopped" : (model.paused ? "Paused" : "Playing");
    text(8, status_y + 8, "State: " + state);
    if (model.muted_all) text(126, status_y + 8, "MUTED", 0.82f);
    if (model.recording) text(174, status_y + 8, "REC", 0.82f);
    text(210, status_y + 8, "Time: " + mmss(model.visual.rendered_frames, model.visual.sample_rate));
    char track[64]; std::snprintf(track, sizeof(track), "Track: %d/%d", model.selected_track + 1, static_cast<int>(model.tracks.size()));
    text(390, status_y + 8, track);
    char rate[64]; std::snprintf(rate, sizeof(rate), "%u Hz  16-bit Stereo", model.visual.sample_rate);
    text(1110, status_y + 8, rate);
    if (!model.notice.empty()) {
        fill(610, status_y + 3, 480, 21, 44, 32, 24);
        clipped_text(616, status_y + 8, 465, model.notice);
    }

    // Draw menu labels/popups after the main content so dropdowns are not
    // overwritten by the channel pane below the title bar.
    draw_top_menu(top_menu, model);

    if (library && !settings) draw_library(*library);

    // Playback requirements are a dismissible modal in the native UI. stderr
    // remains a diagnostic mirror for terminal users and CI logs. It sits on
    // top of the Library too, because a newly opened game may immediately
    // require MT-32/CM-64/SC-55 resources.
    if (!settings && !editor && model.warning_overlay_visible) draw_warning_overlay(model);

    if (settings) draw_settings(*settings);
    if (editor) draw_catalog_editor(*editor);
    if (about_open) draw_about_overlay();

    SDL_RenderPresent(renderer_);
}

} // namespace hootgui
