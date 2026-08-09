#include "drivers/pc98_dos_driver.h"
#include "core/utf8_util.h"
#include "core/visual_state_util.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>

#include "io/zip_archive.h"
#include "sound/fluidsynth_midi_synth.h"
#include "sound/cm32p_midi_synth.h"
#include "sound/cm64_midi_synth.h"
#include "sound/mt32emu_midi_synth.h"
#include "sound/nuked_sc55_clap_midi_synth.h"

namespace {

std::string cm32p_card_hint(const std::string& title)
{
    const auto lower = [&]() {
        std::string value = title;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }();
    if (lower.find("sn-u110-07") != std::string::npos) return "07";
    if (lower.find("sn-u110-10") != std::string::npos) return "10";
    return {};
}

std::string cm32p_card_name(const std::string& hint)
{
    if (hint == "07") return "SN-U110-07";
    if (hint == "10") return "SN-U110-10";
    return hint.empty() ? std::string{} : ("SN-U110-" + hint);
}

std::string cm32p_missing_card_warning(const std::string& hint)
{
    const auto card = cm32p_card_name(hint);
    if (card.empty()) return {};
    return "This pack expects " + card + ". Put " + card +
        ".bin in ~/.hoot/roms/cm32p (the default) or set [midi] cm32p_card_rom_" + hint +
        "=roms/cm32p/" + card +
        ".bin in hootplay.ini. Playback continues without expansion-card sounds.";
}

std::string cm64_la_only_warning(const std::string& hint)
{
    const auto card = cm32p_card_name(hint);
    if (!card.empty()) {
        return "CM-64 + " + card + " needs IC18/IC19/IC20 and " + card +
            ".bin. Put them in ~/.hoot/roms/cm32p (the default); alternatively set [midi] cm32p_rom_path and "
            "cm32p_card_rom_" + hint + "=roms/cm32p/" + card +
            ".bin in hootplay.ini. LA-only fallback: PCM/card sounds missing.";
    }
    return "CM-64 needs CM-32P PCM ROMs IC18/IC19/IC20. Put the three 512 KiB dumps in "
        "~/.hoot/roms/cm32p (the default), or set [midi] cm32p_rom_path in hootplay.ini. "
        "LA-only fallback: CM-32P instruments missing; less authentic playback.";
}


template <size_t N>
void copy_c_string(char (&dest)[N], const std::string& source)
{
    hoot::utf8::copy_c_string(dest, source);
}

bool has_negative_offset(uint32_t offset)
{
    return offset == UINT32_MAX;
}

std::string hex_slot(uint32_t slot)
{
    std::ostringstream out;
    out << "0x" << std::hex << slot;
    return out.str();
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string first_token(const std::string& command)
{
    const auto start = command.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = command.find_first_of(" \t\r\n", start);
    return command.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

std::vector<std::string> shell_executable_candidates(const std::string& shell)
{
    auto name = to_lower(first_token(shell));
    if (name.empty()) {
        return {};
    }
    if (!std::filesystem::path(name).extension().empty()) {
        return {name};
    }
    // Human68k/Hoot catalog shell commands traditionally omit extensions.
    // DOS searches executable extensions; prefer COM for compatibility, then EXE.
    return {name + ".com", name + ".exe", name};
}

uint16_t read_le16(const std::vector<uint8_t>& data, size_t offset)
{
    if (offset + 1 >= data.size()) {
        return 0;
    }
    return static_cast<uint16_t>(data[offset] | (static_cast<uint16_t>(data[offset + 1]) << 8));
}

std::vector<uint8_t> parse_hex_bytes(const std::string& text)
{
    std::vector<uint8_t> out;
    std::istringstream in(text);
    std::string token;
    while (in >> token) {
        char* end = nullptr;
        const long value = std::strtol(token.c_str(), &end, 16);
        if (end == token.c_str() || *end != '\0' || value < 0 || value > 0xff) return {};
        out.push_back(static_cast<uint8_t>(value));
    }
    return out;
}

std::string json_escape(const std::string& value)
{
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u"
                    << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
}

} // namespace

namespace hoot {

Pc98DosDriver::Pc98DosDriver()
{
}

Pc98DosDriver::~Pc98DosDriver()
{
}

HootResult Pc98DosDriver::load(const HootEntry& entry,
                               const std::string& packs_path,
                               int sample_rate,
                               std::string& error)
{
    clear();
    sample_rate_ = sample_rate;
    function_vector_ = 0x7f;
    const auto funcvect = entry.options.find("funcvect");
    if (funcvect != entry.options.end()) {
        function_vector_ = static_cast<uint8_t>(funcvect->second & 0xff);
    }
    clock_multiplier_ = 8;
    const auto clockmul = entry.options.find("clockmul");
    if (clockmul != entry.options.end()) {
        clock_multiplier_ = std::clamp(clockmul->second, 1, 64);
    }

    bare_mode_ = entry.driver_name.rfind("pc98/", 0) == 0
        || entry.driver_name.rfind("pc98vx/", 0) == 0;
    if (bare_mode_) {
        driver_type_ = DriverType::Bare;
        bare_boot_cs_ = static_cast<uint16_t>(entry.options.count("bootcs") ? entry.options.at("bootcs") : 0x0060);
        bare_boot_ip_ = static_cast<uint16_t>(entry.options.count("bootip") ? entry.options.at("bootip") : 0x0000);
        bare_has_data_address_ = entry.options.count("dataaddr") != 0 && entry.options.at("dataaddr") != 0;
        bare_has_data2_address_ = entry.options.count("data2addr") != 0 && entry.options.at("data2addr") != 0;
        bare_data_address_ = static_cast<uint32_t>(entry.options.count("dataaddr") ? entry.options.at("dataaddr") : 0);
        bare_data2_address_ = static_cast<uint32_t>(entry.options.count("data2addr") ? entry.options.at("data2addr") : 0);
        bare_file_size_ = static_cast<uint32_t>(entry.options.count("filesize") ? entry.options.at("filesize") : 0);
        bare_file2_size_ = static_cast<uint32_t>(entry.options.count("file2size") ? entry.options.at("file2size") : 0);
        bare_segmented_addresses_ = (entry.options.count("addressing") && entry.options.at("addressing") != 0)
            || (entry.options.count("adressing") && entry.options.at("adressing") != 0);
    }

    const auto archive_path = std::filesystem::path(packs_path) / (entry.archive + ".zip");
    ZipArchive archive;
    if (!archive.open(archive_path, error)) {
        return HOOT_ERROR_IO;
    }

    files_by_name_.clear();
    std::vector<std::string> shell_names;
    std::vector<HootAssetRef> conin_assets;
    for (const auto& asset : entry.assets) {
        if (bare_mode_) {
            const auto decode_address = [this](uint32_t encoded) {
                if (!bare_segmented_addresses_) return encoded;
                const uint32_t segment = (encoded >> 16) & 0xffffu;
                const uint32_t offset = encoded & 0xffffu;
                return (segment << 4) + offset;
            };
            if (asset.type == "code") {
                auto data = archive.read(asset.path, error);
                if (!error.empty()) return HOOT_ERROR_IO;
                bare_chunks_.push_back({decode_address(asset.offset), std::move(data)});
                continue;
            }
            if (asset.type == "binary") {
                auto data = parse_hex_bytes(asset.path);
                if (data.empty() && !asset.path.empty()) {
                    error = "invalid PC-98 inline binary patch: " + asset.path;
                    return HOOT_ERROR_PARSE;
                }
                bare_chunks_.push_back({decode_address(asset.offset), std::move(data)});
                continue;
            }
            if (asset.type == "bgm" || asset.type == "bgm2") {
                auto data = archive.read(asset.path, error);
                if (!error.empty()) return HOOT_ERROR_IO;
                auto& slots = asset.type == "bgm2" ? files2_by_slot_ : files_by_slot_;
                slots[asset.offset] = LoadedFile{asset.path, std::move(data)};
                continue;
            }
            continue;
        }
        if (asset.type == "device") {
            auto driver_name = asset.path;
            auto data = archive.read(driver_name, error);
            if (!error.empty()) {
                return HOOT_ERROR_IO;
            }
            driver_data_ = std::move(data);
            continue;
        }
        if (asset.type == "shell") {
            shell_command_ = std::vector<uint8_t>(asset.path.begin(), asset.path.end());
            shell_command_.push_back(0);
            shell_names.push_back(asset.path);
            continue;
        }
        if (asset.type == "conin") {
            conin_assets.push_back(asset);
            continue;
        }
        if (asset.type != "file") {
            continue;
        }

        auto data = archive.read(asset.path, error);
        if (!error.empty()) {
            return HOOT_ERROR_IO;
        }

        auto lower_path = to_lower(asset.path);
        files_by_name_[lower_path] = LoadedFile{asset.path, data};
        const auto lower_base = to_lower(std::filesystem::path(asset.path).filename().string());
        if (!lower_base.empty() && files_by_name_.find(lower_base) == files_by_name_.end()) {
            files_by_name_[lower_base] = LoadedFile{asset.path, data};
        }
        if (lower_path == "pmd.com" || lower_path == "pmd.pmd" || lower_path == "pmd") {
            driver_data_ = data;
            driver_type_ = DriverType::PMD;
        } else if (lower_path == "mmd.sys" || lower_path == "mmd2.sys") {
            driver_data_ = data;
            driver_type_ = DriverType::MMD;
        }

        if (!has_negative_offset(asset.offset)) {
            files_by_slot_[asset.offset] = LoadedFile{asset.path, data};
        }
    }

    for (const auto& asset : conin_assets) {
        if (has_negative_offset(asset.offset)) {
            continue;
        }
        const auto found = files_by_name_.find(to_lower(asset.path));
        if (found != files_by_name_.end()) {
            files_by_slot_[asset.offset] = found->second;
        }
    }

    auto find_shell = [&](const std::string& shell) -> const LoadedFile* {
        for (const auto& candidate : shell_executable_candidates(shell)) {
            const auto found = files_by_name_.find(candidate);
            if (found != files_by_name_.end()) {
                return &found->second;
            }
        }
        return nullptr;
    };

    auto shell_resolved_name = [&](const std::string& shell) -> std::string {
        const auto* file = find_shell(shell);
        return file ? to_lower(std::filesystem::path(file->path).filename().string()) : std::string();
    };

    auto load_shell = [&](const std::string& shell) {
        const auto* found = find_shell(shell);
        if (!found) {
            return false;
        }
        driver_data_ = found->data;
        driver_type_ = DriverType::Shell;
        shell_command_ = std::vector<uint8_t>(shell.begin(), shell.end());
        shell_command_.push_back(0);
        return true;
    };

    if (!shell_names.empty()) {
        for (const auto& shell : shell_names) {
            const auto* found = find_shell(shell);
            if (found) {
                shell_programs_.push_back(ShellProgram{shell, found->data});
                const auto executable = shell_resolved_name(shell);
                if (executable == "hhd_98.com") {
                    uses_hhd98_bridge_ = true;
                } else if (executable == "pmd_98.com") {
                    uses_pmd98_bridge_ = true;
                }
                // Some Hoot PC-98 helpers pass stdin straight through to a
                // resident API which then opens the song by DOS filename.
                // Their stdin payload is therefore the selected basename, not
                // the file body.  MMP_HOOT is intentionally different: it
                // asks MMD for a load buffer and streams the song bytes there.
                if (executable == "nc_98.com" || executable == "mddrv_98.com"
                    || executable == "ss_98.com" || executable == "mdrv_98.com") {
                    bridge_stdin_filename_ = true;
                }
            }
        }
        for (const auto& shell : shell_names) {
            if (shell_resolved_name(shell) == "cplay98.com" && shell_names.size() > 1) {
                continue;
            }
            if (load_shell(shell)) {
                break;
            }
        }
    }

    if (driver_data_.empty()) {
        for (const auto& shell : shell_names) {
            if (!driver_data_.empty()) {
                break;
            }
            load_shell(shell);
        }
    }

    if (!bare_mode_ && driver_data_.empty()) {
        error = "pc98dos entry did not provide a driver binary or runnable shell program";
        return HOOT_ERROR_NOT_FOUND;
    }
    if (bare_mode_ && bare_chunks_.empty()) {
        error = "PC-98 bare entry did not provide any code assets";
        return HOOT_ERROR_NOT_FOUND;
    }

    cpu_ = std::make_unique<X86Cpu>();
    cpu_->set_read_memory_callback([this](uint32_t addr) { return read_memory_byte(addr); });
    cpu_->set_write_memory_callback([this](uint32_t addr, uint8_t d) { write_memory_byte(addr, d); });
    cpu_->set_io_read_callback([this](uint16_t p) { return read_io_port(p); });
    cpu_->set_io_write_callback([this](uint16_t p, uint8_t d) { write_io_port(p, d); });
    cpu_->set_interrupt_callback([this](uint8_t i) { handle_interrupt(i); });
    cpu_->set_trace_callback([this](const char* type,
                                    uint8_t opcode,
                                    uint16_t from_cs,
                                    uint16_t from_ip,
                                    uint16_t to_cs,
                                    uint16_t to_ip) {
        trace_cpu_event(type, opcode, from_cs, from_ip, to_cs, to_ip);
    });

    pc9821_mode_ = entry.driver_name.rfind("pc9821dos/", 0) == 0;
    const auto slash = entry.driver_name.find('/');
    const std::string board_type = slash == std::string::npos ? std::string{} : entry.driver_name.substr(slash + 1);
    use_sound_orchestra_ = board_type == "soundorchestra";
    use_sb16_ = board_type == "soundblaster16";
    use_amd98_ = board_type == "amd98";
    use_px_ = board_type == "otomichan" || board_type == "otomix2";
    use_px2_ = board_type == "otomix2";
    // Burai and Rashin are legacy PC-98 wrapper families around OPN-class
    // sound code rather than distinct physical sound boards.
    use_ym2203_ = board_type == "opn" || board_type == "burai" || board_type == "rashin" || use_sound_orchestra_;
    use_beep_ = board_type == "beep";
    use_pcm86_ = board_type == "86" || use_px2_;
    const auto midiout = entry.options.find("midiout");
    midi_enabled_ = midiout != entry.options.end() && midiout->second != 0;
    const auto midiout_type = entry.options.find("midiout_type");
    midiout_type_ = midiout_type != entry.options.end() ? midiout_type->second : -1;
    if (const char* value = std::getenv("HOOT_X68K_MIDI")) {
        if (std::strcmp(value, "0") == 0 || std::strcmp(value, "off") == 0 || std::strcmp(value, "false") == 0) midi_enabled_ = false;
        else if (std::strcmp(value, "1") == 0 || std::strcmp(value, "on") == 0 || std::strcmp(value, "true") == 0) midi_enabled_ = true;
    }
    pcm86_gain_ = 1.0;
    if (const auto pcm_mix = entry.options.find("pcm_mix"); pcm_mix != entry.options.end()) {
        // Hoot board mixer values use 0x100 as unity for the PC-98 PCM path.
        pcm86_gain_ = std::clamp(static_cast<double>(pcm_mix->second) / 256.0, 0.0, 4.0);
    }
    // Hoot mixer options are expressed in half-decibel units.  Match that
    // convention for the separately rendered SSG path: e.g. -13 means
    // -6.5 dB, or an amplitude multiplier of 10^(-13/40).  The 0.90 factor
    // is this wrapper's calibrated 0 dB SSG level.
    int ssg_mix_half_db = -13;
    const auto ssg_mix = entry.options.find("ssg_mix");
    if (ssg_mix != entry.options.end()) {
        ssg_mix_half_db = ssg_mix->second;
    }
    const double catalog_ssg_gain = 0.90 * std::pow(10.0, static_cast<double>(ssg_mix_half_db) / 40.0);
    const bool catalog_controls_ssg = std::getenv("HOOT_PSG_GAIN") == nullptr
        && std::getenv("HOOT_DISABLE_PSG") == nullptr;

    // Board-specific sound cores. These are kept separate from the normal
    // OPN/OPNA path because several PC-98 expansion boards intentionally
    // decode ports that overlap the 86-board extended OPNA window.
    if (use_sound_orchestra_ || use_sb16_) {
        opl_ = std::make_unique<LibvgmOpl>();
        const auto model = use_sb16_ ? LibvgmOpl::Model::YMF262 : LibvgmOpl::Model::YM3812;
        const uint32_t clock = use_sb16_ ? 14'318'180u : 3'579'545u;
        if (!opl_->initialize(model, clock, static_cast<uint32_t>(sample_rate_))) {
            error = use_sb16_ ? "failed to initialize YMF262 OPL3" : "failed to initialize YM3812 OPL2";
            return HOOT_ERROR_UNSUPPORTED;
        }
        const char* mix_name = use_sb16_ ? "opl3_mix" : "opl_mix";
        if (const auto it = entry.options.find(mix_name); it != entry.options.end()) {
            opl_gain_ = std::clamp(static_cast<double>(it->second) / 256.0, 0.0, 4.0);
        }
    }
    if (use_amd98_) {
        for (auto& psg : amd_psg_) {
            psg = std::make_unique<LibvgmYm2203>();
            if (!psg->initialize(3'993'600u, static_cast<uint32_t>(sample_rate_))) {
                error = "failed to initialize AMD-98 PSG";
                return HOOT_ERROR_UNSUPPORTED;
            }
        }
    }
    if (use_px_) {
        const size_t count = use_px2_ ? 5u : 4u;
        for (size_t i = 0; i < count; ++i) {
            px_opna_[i] = std::make_unique<LibvgmYm2608>();
            if (!px_opna_[i]->initialize(7'987'200u, static_cast<uint32_t>(sample_rate_))) {
                error = "failed to initialize Otomi-chan/PX OPNA bank";
                return HOOT_ERROR_UNSUPPORTED;
            }
            px_opna_[i]->allocate_adpcm_memory(0x40000u);
            px_opna_[i]->write(0, 0x29);
            px_opna_[i]->write(1, 0x00);
        }
    }

    if (use_ym2203_) {
        ym2203_ = std::make_unique<LibvgmYm2203>();
        if (!ym2203_->initialize(3'993'632, static_cast<uint32_t>(sample_rate_))) {
            error = "failed to initialize YM2203 sound chip";
            return HOOT_ERROR_UNSUPPORTED;
        }
        if (catalog_controls_ssg) {
            ym2203_->set_ssg_gain(catalog_ssg_gain);
        }
    } else if (!use_beep_ && !use_sb16_ && !use_amd98_ && !use_px_) {
        ym2608_ = std::make_unique<LibvgmYm2608>();
        if (!ym2608_->initialize(7'967'264, static_cast<uint32_t>(sample_rate_))) {
            error = "failed to initialize YM2608 sound chip";
            return HOOT_ERROR_UNSUPPORTED;
        }
        // FMGEN's OPNA core exposes a 256 KiB ADPCM-B/Delta-T RAM window.
        // Hoot PC-98 drivers upload samples through the normal YM2608 register
        // interface, so allocate the equivalent guest-visible RAM even when
        // the catalog does not ship a preloaded PCM image.
        ym2608_->allocate_adpcm_memory(0x40000u);
        if (catalog_controls_ssg) {
            ym2608_->set_ssg_gain(catalog_ssg_gain);
        }
    }
    if (use_pcm86_) {
        pcm86_ = std::make_unique<Pc98Pcm86>();
        if (!pcm86_->initialize(static_cast<uint32_t>(sample_rate_))) {
            error = "failed to initialize PC-9801-86 PCM FIFO/DAC";
            return HOOT_ERROR_UNSUPPORTED;
        }
    }
    if (use_beep_) {
        beep_ = std::make_unique<Pc98Beep>();
        if (!beep_->initialize(static_cast<uint32_t>(sample_rate_), 2457600u)) {
            error = "failed to initialize PC-98 PIT speaker";
            return HOOT_ERROR_UNSUPPORTED;
        }
        beep_gain_ = 1.0;
        if (const char* value = std::getenv("HOOT_PC98_BEEP_GAIN")) {
            char* end = nullptr;
            const double parsed = std::strtod(value, &end);
            if (end != value && std::isfinite(parsed)) {
                beep_gain_ = std::clamp(parsed, 0.0, 4.0);
            }
        }
    }
    setup_midi(entry);
    trace_opna_ = std::getenv("HOOT_TRACE_PC98_OPN") != nullptr;
    trace_dos_ = std::getenv("HOOT_TRACE_PC98_DOS") != nullptr;
    trace_pc98_ = false;
    trace_events_ = 0;
    trace_event_limit_ = 20000;
    if (const char* value = std::getenv("HOOT_PC98_TRACE_LIMIT")) {
        trace_event_limit_ = static_cast<uint32_t>(std::max(0L, std::strtol(value, nullptr, 0)));
    }
    if (const char* trace_path = std::getenv("HOOT_PC98_TRACE")) {
        trace_file_.open(trace_path, std::ios::out | std::ios::trunc);
        trace_pc98_ = trace_file_.is_open();
        if (trace_pc98_) {
            std::ostringstream event;
            event << "{\"type\":\"meta\",\"driver\":\"" << json_escape(entry.driver_name)
                  << "\",\"archive\":\"" << json_escape(entry.archive)
                  << "\",\"sample_rate\":" << sample_rate_ << "}";
            emit_trace_event(event.str());
        }
    }
    trace_opna_limit_ = 512;
    if (const char* value = std::getenv("HOOT_TRACE_PC98_OPN_LIMIT")) {
        trace_opna_limit_ = static_cast<uint32_t>(std::max(0L, std::strtol(value, nullptr, 0)));
    }
    disable_opn_tl_compat_ = std::getenv("HOOT_DISABLE_OPN_TL_COMPAT") != nullptr;
    if (ym2608_) {
        ym2608_->write(0, 0x29);
        ym2608_->write(1, 0x00);
    }

    if (!setup_memory()) {
        error = "failed to setup PC-98 memory";
        return HOOT_ERROR_UNSUPPORTED;
    }

    setup_interrupt_vectors();
    setup_pit();
    if (driver_type_ == DriverType::Bare) {
        for (const auto& chunk : bare_chunks_) {
            if (chunk.address >= 1024u * 1024u
                || chunk.data.size() > 1024u * 1024u - chunk.address) {
                error = "PC-98 code/patch asset exceeds the 1 MiB guest address space";
                return HOOT_ERROR_PARSE;
            }
            for (size_t i = 0; i < chunk.data.size(); ++i) {
                write_memory_byte(chunk.address + static_cast<uint32_t>(i), chunk.data[i]);
            }
        }
        cpu_->clear_halted();
        cpu_->set_cs(bare_boot_cs_);
        cpu_->set_pc(bare_boot_ip_);
        cpu_->set_ds(bare_boot_cs_);
        cpu_->set_es(bare_boot_cs_);
        cpu_->set_ss(bare_boot_cs_);
        cpu_->set_sp(0xfffe);
        // Hoot bare-player stubs initialize the extracted game driver and
        // install their control ISR. Stop naturally if the stub parks on HLT.
        run_cpu_steps(5'000'000);
        cpu_->clear_unsupported_status();
    } else {
        reset_cpu_context();
        if (driver_type_ == DriverType::Shell) install_shell_driver();
    }

    loaded_ = true;
    return HOOT_OK;
}

HootResult Pc98DosDriver::select_track(const HootEntry& entry,
                                       int track_index,
                                       std::string& error)
{
    if (!loaded_) {
        error = "pc98dos driver is not loaded";
        return HOOT_ERROR_NOT_LOADED;
    }
    if (track_index < 0 || static_cast<size_t>(track_index) >= entry.tracks.size()) {
        error = "track index is outside the catalog track list";
        return HOOT_ERROR_INVALID_ARGUMENT;
    }

    // The PC-9801-86 stack is built from several DOS TSRs (EMMDRV/P86DRV/
    // PMD86/PMDPCM86).  Their resident heaps, interrupt vectors and board
    // state are intentionally mutable while a song is active.  Reusing that
    // guest state across catalog track selections caused later FC98 songs to
    // inherit stale PMD/P86 state even though each song worked in a fresh
    // process.  Rebuild the DOS resident environment for every 86 selection.
    // Track changes are infrequent, so deterministic correctness is preferable
    // to preserving a few milliseconds of TSR startup time.
    if (driver_type_ == DriverType::Shell && use_pcm86_) {
        if (!rebuild_shell_runtime()) {
            error = "failed to rebuild PC-9801-86 DOS resident environment";
            return HOOT_ERROR_UNSUPPORTED;
        }
    }

    selected_track_ = track_index;
    selected_code_ = entry.tracks[track_index].code;

    if (driver_type_ == DriverType::Bare) {
        const uint32_t bgm_slot = selected_code_ & 0xffffu;
        bare_load_occurred_ = false;
        bare_hoot_status_ = 0;
        bare_hoot_params_.fill(0);
        // Preserve any CPU problem raised by this playback setup so probes can
        // report it.  Clear the boot-time status before entering cause 0, not
        // after cause 1 has already executed.
        cpu_->clear_unsupported_status();

        // PC98.TXT defines the generic wrapper ABI as two host interrupts.
        // Cause 0 lets the guest prepare playback (and, importantly, choose a
        // runtime BGM destination through 07D0h-07D7h).  Hoot then performs
        // the automatic primary/secondary loads for the low 16-bit song code.
        // Cause 1 is sent only when at least one load actually occurred.
        cpu_->set_ax(static_cast<uint16_t>(selected_code_ & 0xffffu));
        cpu_->set_bx(static_cast<uint16_t>((selected_code_ >> 16) & 0xffffu));
        if (is_interrupt_vector_active(function_vector_)) {
            bare_interrupt_reason_ = 0;
            trigger_interrupt_vector(function_vector_, 5'000'000);
        } else if (function_vector_ != 0) {
            error = "PC-98 bare player did not install control interrupt " + hex_slot(function_vector_);
            return HOOT_ERROR_UNSUPPORTED;
        }

        if (!files_by_slot_.empty() && bare_data_address_ != 0) {
            if (!bare_load_asset(false, bgm_slot, bare_data_address_, bare_file_size_,
                                 0, 0xffffffffu, &error)) {
                return HOOT_ERROR_NOT_FOUND;
            }
        }
        if (!files2_by_slot_.empty() && bare_data2_address_ != 0) {
            if (!bare_load_asset(true, bgm_slot, bare_data2_address_, bare_file2_size_,
                                 0, 0xffffffffu, &error)) {
                return HOOT_ERROR_NOT_FOUND;
            }
        }

        if (bare_load_occurred_ && is_interrupt_vector_active(function_vector_)) {
            bare_interrupt_reason_ = 1;
            trigger_interrupt_vector(function_vector_, 5'000'000);
        }
        bare_interrupt_reason_ = 0xff;
        cpu_->set_cs(0x0000);
        cpu_->set_pc(kHaltOffset);
        cpu_->set_ss(kProgramSegment);
        cpu_->set_sp(0xfffe);
        cpu_->halt();
        playing_ = true;
        rendered_frames_ = 0;
        timer_frames_until_tick_ = sample_rate_ > 0 ? static_cast<double>(sample_rate_) / 60.0 : 735.0;
        error.clear();
        return HOOT_OK;
    }

    const uint32_t voice_slot = (selected_code_ >> 16) & 0xff;
    const uint32_t requested_bgm_slot = selected_code_ & 0xffff;
    selected_voice_path_.clear();
    selected_bgm_path_.clear();

    // Normal PC-98 Hoot track codes directly name the catalog file slot.
    // Extended-effect ranges encode the file slot in bits 8..15 and pass the
    // low byte to the resident driver as its subsong/effect number (for
    // example 0801h means file slot 08h, effect 01h). Prefer an exact slot so
    // ordinary 16-bit slot numbers remain valid, then fall back to that ABI.
    auto bgm = files_by_slot_.find(requested_bgm_slot);
    uint32_t resolved_bgm_slot = requested_bgm_slot;
    if (bgm == files_by_slot_.end() && selected_code_ > 0xffu) {
        const uint32_t extended_slot = (selected_code_ >> 8) & 0xffu;
        bgm = files_by_slot_.find(extended_slot);
        if (bgm != files_by_slot_.end()) resolved_bgm_slot = extended_slot;
    }
    if (bgm == files_by_slot_.end()) {
        error = "pc98dos track references missing BGM slot " + hex_slot(requested_bgm_slot);
        return HOOT_ERROR_NOT_FOUND;
    }
    (void)resolved_bgm_slot;
    selected_bgm_path_ = bgm->second.path;
    selected_bgm_data_ = bgm->second.data;
    selected_file_offset_ = 0;
    selected_file_open_ = false;
    rendered_frames_ = 0;
    trace_opna_events_ = 0;

    const auto voice = files_by_slot_.find(voice_slot);
    if (voice != files_by_slot_.end()) {
        selected_voice_path_ = voice->second.path;
    }

    playing_ = true;
    timer_frames_until_tick_ = sample_rate_ > 0
        ? static_cast<double>(sample_rate_) / 60.0
        : 735.0;
    if (driver_type_ == DriverType::Shell) {
        selected_file_open_ = true;
        selected_file_offset_ = 0;
        bridge_load_pending_ = true;
        // Place transient/resident helper programs after both the traditional
        // track-shell base and every DOS allocation made so far.  A fixed
        // 0x400-paragraph stride is not sufficient for large residents such
        // as PMD86 (~0x900 paragraphs) and caused later helpers to overwrite
        // the installed music driver.  run_shell_program() advances the DOS
        // high-water mark by the actual image/TSR footprint.
        uint16_t track_shell_segment = std::max<uint16_t>(0x3000, dos_alloc_segment_);
        for (size_t i = installed_shell_programs_; i < shell_programs_.size(); ++i) {
            run_shell_program(shell_programs_[i], track_shell_segment);
            track_shell_segment = std::max<uint16_t>(
                static_cast<uint16_t>(track_shell_segment + 1), dos_alloc_segment_);
        }
        // Keep the host filename/bridge buffer outside every resident DOS
        // driver.  The original fixed 1000:0174 location overlaps P86DRV's
        // resident code and corrupts its INT 65h special-entry table.
        if (bridge_buffer_segment_ == 0) {
            bridge_buffer_segment_ = dos_alloc_segment_;
            dos_alloc_segment_ = static_cast<uint16_t>(dos_alloc_segment_ + kBridgeBufferParagraphs);
        }
        auto* mem = cpu_->memory();
        const uint32_t bridge_buffer = (static_cast<uint32_t>(bridge_buffer_segment_) << 4)
            + kBridgeBufferOffset;
        std::fill(mem + bridge_buffer, mem + bridge_buffer + 0x4f, 0);
        const std::string filename = std::filesystem::path(selected_bgm_path_).filename().string();
        const auto copy_size = std::min<size_t>(filename.size(), 0x4e);
        if (copy_size > 0) {
            std::memcpy(mem + bridge_buffer, filename.data(), copy_size);
        }
        if (uses_hhd98_bridge_) {
            load_hhd98_track();
        } else {
            selected_file_open_ = true;
            selected_file_offset_ = 0;
            bridge_load_pending_ = true;
            if (uses_pmd98_bridge_) {
                call_shell_player_api(static_cast<uint16_t>(selected_code_ & 0xff));
            } else {
                call_shell_player_api(0x0900, bridge_buffer_segment_, kBridgeBufferOffset);
                if (voice_slot != 0) {
                    call_shell_player_api(static_cast<uint16_t>(voice_slot & 0xff));
                }
            }
        }
    } else {
        reset_cpu_context();
        run_cpu_steps(20000);
    }

    error.clear();
    return HOOT_OK;
}

void Pc98DosDriver::reset()
{
    // PC98.TXT defines Hoot interrupt cause 2 as the generic bare-wrapper
    // stop notification.  Give a resident legacy wrapper the same chance to
    // silence/release its driver state before the machine state is reset.
    if (loaded_ && driver_type_ == DriverType::Bare && playing_
        && is_interrupt_vector_active(function_vector_)) {
        bare_interrupt_reason_ = 2;
        trigger_interrupt_vector(function_vector_, 1'000'000);
        bare_interrupt_reason_ = 0xff;
    }
    selected_track_ = 0;
    selected_code_ = 0;
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_file_offset_ = 0;
    selected_file_open_ = false;
    playing_ = false;
    pit_counter_ = 0;
    timer_frames_until_tick_ = 0.0;
    fm_timer_a_ = 0;
    fm_timer_b_ = 0;
    fm_mode_ = 0;
    fm_prescaler_sel_ = 2;
    fm_status_ = 0;
    fm_timer_a_interval_frames_ = 0;
    fm_timer_a_frames_until_next_ = 0;
    fm_timer_b_interval_frames_ = 0;
    fm_timer_b_frames_until_next_ = 0;
    reset_opn();
    reset_special_boards();
    if (pcm86_) pcm86_->reset();
    if (beep_) beep_->reset();
    if (mpu401_) {
        mpu401_->reset();
        mpu401_->set_sink([this](const X68kMidiMessage& message) { handle_midi_message(message); });
    }
    reset_midi_synth_mode();
    debug_midi_irq_count_ = 0;
    debug_midi_synth_frames_ = 0;
    debug_midi_sysex_handled_ = 0;
    selected_mpu_irq_line_ = -1;
    pic_master_mask_ = 0xff;
    pic_slave_mask_ = 0xff;
    vrtc_phase_ = false;
    debug_beep_vrtc_irqs_ = 0;
    reset_cpu_context();
}

int Pc98DosDriver::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (!loaded_ || !playing_) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
        return frames;
    }

    if (driver_type_ == DriverType::Shell || driver_type_ == DriverType::Bare) {
        const double frames_per_tick = sample_rate_ > 0
            ? static_cast<double>(sample_rate_) / 60.0
            : 735.0;
        int rendered = 0;
        while (rendered < frames) {
            if (pcm86_ && pcm86_->irq_pending()) {
                service_pcm86_irq();
            }
            if (amd_timer_interval_frames_ > 0 && amd_timer_frames_until_next_ <= 0) {
                service_amd98_timer_irq();
            }
            if (fm_timer_a_interval_frames_ > 0 && fm_timer_a_frames_until_next_ <= 0) {
                service_fm_timer_irq(0x01);
            }
            if (fm_timer_b_interval_frames_ > 0 && fm_timer_b_frames_until_next_ <= 0) {
                service_fm_timer_irq(0x02);
            }
            if (timer_frames_until_tick_ <= 0.0) {
                // PC-98 vertical retrace is IRQ2 / INT 0Ah. Bare pc98/pc98vx
                // wrappers may use it even when the sound target is FM, while
                // DOS PMDB uses it for PIT-speaker sequencing.
                if ((driver_type_ == DriverType::Bare || use_beep_)
                    && is_interrupt_vector_active(0x0a)) {
                    trigger_interrupt_vector(0x0a, 200000);
                    ++debug_beep_vrtc_irqs_;
                }
                if (driver_type_ == DriverType::Shell) {
                    // Several PC-98 MIDI residents (including Nihon Create's NC
                    // and ACID PLAN's MDDRV families) hook the system timer.
                    if (midi_enabled_ && use_beep_ && is_interrupt_vector_active(0x08)) {
                        trigger_interrupt_vector(0x08, 200000);
                    }
                    const uint32_t hook_addr = static_cast<uint32_t>(function_vector_) * 4;
                    const uint16_t hook_offset = static_cast<uint16_t>(read_memory_byte(hook_addr)
                        | (read_memory_byte(hook_addr + 1) << 8));
                    const uint16_t hook_segment = static_cast<uint16_t>(read_memory_byte(hook_addr + 2)
                        | (read_memory_byte(hook_addr + 3) << 8));
                    if (uses_hhd98_bridge_) {
                        const uint32_t int60 = 0x60 * 4;
                        const uint16_t hhd_segment = static_cast<uint16_t>(read_memory_byte(int60 + 2)
                            | (read_memory_byte(int60 + 3) << 8));
                        setup_interrupt_vector(0x7e, hhd_segment, 0x064b);
                        trigger_interrupt_vector(0x7e, 200000);
                    } else {
                        // The Hoot bridge vector (normally INT 7Fh) is a control
                        // entrypoint only: cause 0 loads/starts a song and cause
                        // 2 stops it.  It is not the resident playback clock.
                        // Older OPN drivers install their real periodic service
                        // on INT 14h (for example Nihon Create SS), while
                        // timer-driven drivers receive YM2203/YM2608 overflow
                        // IRQs through INT 0Bh.  Calling INT 7Fh every video tick
                        // accidentally kept the bridge alive but never advanced
                        // those residents.
                        const bool fm_timer_active = fm_timer_a_interval_frames_ > 0
                            || fm_timer_b_interval_frames_ > 0;
                        // INT 14h is a legacy periodic fallback, not a second
                        // clock source. PMD-family residents may leave that
                        // vector installed while driving music from YM Timer
                        // A/B; polling both advances the sequencer multiple
                        // times per real-time interval. Prefer the hardware
                        // timer whenever it is armed.
                        if (!fm_timer_active && is_interrupt_vector_active(0x14)) {
                            trigger_interrupt_vector(0x14, 200000);
                        } else if (!fm_timer_active
                                   && is_interrupt_vector_active(0x0b)) {
                            // Conservative fallback for residents that hook the
                            // sound IRQ but leave the FM timer disabled.
                            trigger_interrupt_vector(0x0b, 200000);
                        } else if (!fm_timer_active && (hook_offset != 0 || hook_segment != 0)) {
                            // Keep compatibility with bridge-only helpers that
                            // intentionally use their function vector as a poll.
                            trigger_interrupt_vector(function_vector_, 200000);
                        }
                    }
                }
                timer_frames_until_tick_ += frames_per_tick;
            }
            int chunk = frames - rendered;
            chunk = std::min(chunk, std::max(1, static_cast<int>(std::ceil(timer_frames_until_tick_))));
            if (fm_timer_a_interval_frames_ > 0) {
                chunk = std::min(chunk, std::max(1, fm_timer_a_frames_until_next_));
            }
            if (fm_timer_b_interval_frames_ > 0) {
                chunk = std::min(chunk, std::max(1, fm_timer_b_frames_until_next_));
            }
            if (amd_timer_interval_frames_ > 0) {
                chunk = std::min(chunk, std::max(1, amd_timer_frames_until_next_));
            }
            if (pcm86_ && pcm86_->interrupt_enabled()) {
                const int until_pcm_irq = pcm86_->frames_until_irq();
                if (until_pcm_irq != std::numeric_limits<int>::max()) {
                    chunk = std::min(chunk, std::max(1, until_pcm_irq));
                }
            }
            if (mpu401_) {
                const int until_midi_irq = mpu401_->frames_until_intelligent_event(sample_rate_);
                if (until_midi_irq != std::numeric_limits<int>::max()) {
                    chunk = std::min(chunk, std::max(1, until_midi_irq));
                }
            }
            run_cpu_steps(chunk * clock_multiplier_);
            if (mpu401_) {
                mpu401_->advance_frames(chunk, sample_rate_);
                service_mpu401_irq();
            }
            render_opn(interleaved_stereo + (rendered * 2), chunk);
            timer_frames_until_tick_ -= static_cast<double>(chunk);
            if (fm_timer_a_interval_frames_ > 0) fm_timer_a_frames_until_next_ -= chunk;
            if (fm_timer_b_interval_frames_ > 0) fm_timer_b_frames_until_next_ -= chunk;
            if (amd_timer_interval_frames_ > 0) amd_timer_frames_until_next_ -= chunk;
            rendered += chunk;
            rendered_frames_ += static_cast<uint64_t>(chunk);
        }
        return frames;
    }

    run_cpu_steps(frames * clock_multiplier_);
    if (mpu401_) {
        mpu401_->advance_frames(frames, sample_rate_);
        service_mpu401_irq();
    }

    render_opn(interleaved_stereo, frames);
    rendered_frames_ += static_cast<uint64_t>(frames);
    return frames;
}

int Pc98DosDriver::render_float(float* interleaved_stereo, int frames)
{
    if (interleaved_stereo == nullptr || frames < 0) {
        return 0;
    }
    if (!loaded_ || !playing_) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), 0.0f);
        return frames;
    }

    mix_buffer_.resize(static_cast<size_t>(frames) * 2);
    render_s16(mix_buffer_.data(), frames);
    for (int i = 0; i < frames * 2; ++i) {
        interleaved_stereo[i] = static_cast<float>(mix_buffer_[i]) / 32768.0f;
    }
    return frames;
}

void Pc98DosDriver::fill_track_info(const HootEntry& entry,
                                     int track_index,
                                     HootTrackInfo& out) const
{
    std::memset(&out, 0, sizeof(out));
    out.track_index = track_index;
    out.sample_rate = sample_rate_;
    out.debug_cpu_cycles = cpu_ && cpu_->unsupported_count() != 0
        ? cpu_->unsupported_count()
        : static_cast<uint32_t>(std::min<uint64_t>(executed_cpu_steps_, UINT32_MAX));
    out.debug_io_reads = static_cast<uint32_t>(files_by_slot_.size());
    out.debug_io_writes = selected_code_;
    out.debug_opn_writes = debug_opna_writes_;
    out.debug_opn_keyons = debug_opna_keyons_;
    out.debug_pc = cpu_ && cpu_->unsupported_count() != 0
        ? ((static_cast<uint32_t>(cpu_->last_unsupported_cs()) << 16) | cpu_->last_unsupported_ip())
        : (cpu_ ? ((static_cast<uint32_t>(cpu_->get_cs()) << 16) | cpu_->get_pc()) : 0);
    out.debug_last_opn_reg = debug_last_opna_reg_;
    out.debug_last_opn_data = debug_last_opna_data_;
    out.debug_port_writes_00 = cpu_ ? cpu_->last_unsupported_opcode() : 0;
    out.debug_port_writes_01 = static_cast<uint64_t>(debug_fm_keyons_by_channel_[0])
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[1]) << 16)
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[2]) << 32)
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[3]) << 48);
    out.debug_port_writes_02 = static_cast<uint64_t>(debug_fm_keyons_by_channel_[4])
        | (static_cast<uint64_t>(debug_fm_keyons_by_channel_[5]) << 32);
    out.debug_port_writes_03 = debug_opna_ssg_writes_;
    out.debug_port_writes_32 = debug_opna_rhythm_writes_;
    uint32_t tl_summary = 0;
    for (uint8_t ch = 0; ch < 3; ++ch) {
        const uint8_t carrier_tl = static_cast<uint8_t>(opna_registers_[0][0x4c + ch] & 0x7f);
        tl_summary |= static_cast<uint32_t>(carrier_tl) << (ch * 8);
    }
    out.debug_port_writes_44 = tl_summary;
    out.debug_port_writes_45 = static_cast<uint64_t>(debug_opna_rhythm_keyons_)
        | (static_cast<uint64_t>(debug_opna_rhythm_keyoffs_) << 24)
        | (static_cast<uint64_t>(debug_last_rhythm_command_) << 48)
        | (static_cast<uint64_t>(opna_registers_[0][0x11] & 0x3f) << 56);
    if (mpu401_) {
        const auto& midi = mpu401_->transport_stats();
        out.debug_midi_bytes_enqueued = midi.bytes_enqueued;
        out.debug_midi_bytes_transmitted = midi.bytes_transmitted;
        out.debug_midi_channel_messages = midi.channel_messages;
        out.debug_midi_system_common_messages = midi.system_common_messages;
        out.debug_midi_sysex_messages = midi.sysex_messages;
        out.debug_midi_sysex_bytes = midi.sysex_bytes;
        out.debug_midi_realtime_bytes = midi.realtime_bytes;
        out.debug_midi_running_status_messages = midi.running_status_messages;
        out.debug_midi_malformed_bytes = midi.malformed_bytes;
        out.debug_midi_note_ons = midi.note_on_messages;
        out.debug_midi_note_offs = midi.note_off_messages;
        out.debug_midi_control_changes = midi.control_changes;
        out.debug_midi_program_changes = midi.program_changes;
        out.debug_midi_pitch_bends = midi.pitch_bends;
        out.debug_midi_fifo_full_transitions = midi.fifo_full_transitions;
        out.debug_midi_irq_count = debug_midi_irq_count_;
        out.debug_midi_synth_frames = debug_midi_synth_frames_;
        out.debug_midi_sysex_handled = debug_midi_sysex_handled_;
        out.debug_midi_fifo_bytes = static_cast<uint32_t>(mpu401_->transport_stats().bytes_enqueued - mpu401_->transport_stats().bytes_transmitted);
        out.debug_midi_peak_fifo_bytes = midi.peak_fifo_bytes;
        out.debug_midi_last_status = midi.last_status;
        out.debug_midi_backend_active = midi_synth_ && midi_synth_->active() ? 1u : 0u;
        out.debug_midi_backend_kind = 0u;
        if (midi_synth_ && midi_synth_->active()) {
            const std::string backend = midi_synth_->backend_name();
            if (backend == "fluidsynth") out.debug_midi_backend_kind = 1u;
            else if (backend == "nuked-sc55-clap") out.debug_midi_backend_kind = 2u;
            else if (backend == "munt-mt32") out.debug_midi_backend_kind = 3u;
            else if (backend == "munt-cm32l") out.debug_midi_backend_kind = 4u;
            else if (backend == "munt-cm64") out.debug_midi_backend_kind = 5u;
            else if (backend == "cm32p") out.debug_midi_backend_kind = 6u;
        }
        out.debug_midiout_type = midiout_type_;
    }
    if (pcm86_) {
        const auto& pcm_stats = pcm86_->stats();
        out.debug_pcm86_port_writes = pcm_stats.port_writes;
        out.debug_pcm86_fifo_writes = pcm_stats.fifo_writes;
        out.debug_pcm86_fifo_reads = pcm_stats.fifo_reads;
        out.debug_pcm86_rendered_frames = pcm_stats.rendered_frames;
        out.debug_pcm86_rendered_source_frames = pcm_stats.rendered_source_frames;
        out.debug_pcm86_irq_requests = pcm_stats.irq_requests;
        out.debug_pcm86_irq_deliveries = pcm_stats.irq_deliveries;
        out.debug_pcm86_fifo_overflows = pcm_stats.fifo_overflows;
        out.debug_pcm86_fifo_bytes = pcm86_->fifo_bytes();
        out.debug_pcm86_peak_fifo_bytes = pcm_stats.peak_fifo_bytes;
        out.debug_pcm86_fifo_threshold = pcm86_->fifo_threshold();
        out.debug_pcm86_fifo_control = pcm86_->fifo_control();
        out.debug_pcm86_dac_control = pcm86_->dac_control();
        out.debug_pcm86_volume_code = pcm86_->volume_code();
        out.debug_pcm86_source_rate_millihz = static_cast<uint32_t>(
            std::lround(pcm86_->source_rate() * 1000.0));
    }
    if (beep_) {
        const auto& beep_stats = beep_->stats();
        out.debug_beep_pit_data_writes = beep_stats.pit_data_writes;
        out.debug_beep_pit_control_writes = beep_stats.pit_control_writes;
        out.debug_beep_ppi_writes = beep_stats.ppi_writes;
        out.debug_beep_gate_changes = beep_stats.gate_changes;
        out.debug_beep_divider_changes = beep_stats.divider_changes;
        out.debug_beep_rendered_frames = beep_stats.rendered_frames;
        out.debug_beep_audible_frames = beep_stats.audible_frames;
        out.debug_beep_vrtc_irqs = debug_beep_vrtc_irqs_;
        out.debug_beep_divider = beep_->divider();
        out.debug_beep_frequency_millihz = static_cast<uint32_t>(
            std::lround(beep_->frequency() * 1000.0));
        out.debug_beep_enabled = beep_->speaker_enabled() ? 1u : 0u;
        out.debug_beep_mode = beep_->mode();
        out.debug_beep_min_divider = beep_stats.min_divider;
        out.debug_beep_max_divider = beep_stats.max_divider;
    }
    if (cpu_) {
        out.debug_unsupported_opcodes = cpu_->unsupported_count();
        out.debug_last_unsupported_opcode = cpu_->last_unsupported_opcode();
        out.debug_last_unsupported_cs = cpu_->last_unsupported_cs();
        out.debug_last_unsupported_ip = cpu_->last_unsupported_ip();
    }
    std::string runtime_warning = driver_warning_;
    if (runtime_warning.empty() && cpu_ && cpu_->unsupported_count() != 0) {
        std::ostringstream message;
        message << "PC-98 guest hit unsupported x86 opcode $"
                << std::hex << std::uppercase << std::setfill('0')
                << std::setw(2) << static_cast<unsigned>(cpu_->last_unsupported_opcode())
                << " at " << std::setw(4) << cpu_->last_unsupported_cs()
                << ':' << std::setw(4) << cpu_->last_unsupported_ip()
                << "; playback may be silent or incomplete.";
        runtime_warning = message.str();
    }
    if (!runtime_warning.empty()) {
        copy_c_string(out.warning, runtime_warning);
    }
    copy_c_string(out.driver, name());
    if (track_index >= 0 && static_cast<size_t>(track_index) < entry.tracks.size()) {
        copy_c_string(out.title, entry.tracks[track_index].title);
    } else {
        copy_c_string(out.title, entry.title);
    }
}


void Pc98DosDriver::fill_visual_state(const HootEntry&, int, HootVisualState& out) const
{
    out.abi_version = HOOT_VISUAL_ABI_VERSION;
    out.struct_size = sizeof(out);
    visual::copy(out.architecture, pc9821_mode_ ? "PC-9821" : "PC-98");
    visual::copy(out.cpu, "V30 / x86");
    std::string devices;
    if (ym2608_) devices += "YM2608";
    else if (ym2203_) devices += "YM2203";
    if (pcm86_) devices += devices.empty() ? "PCM86" : " + PCM86";
    if (beep_) devices += devices.empty() ? "BEEP" : " + BEEP";
    if (opl_) devices += devices.empty() ? (use_sb16_ ? "YMF262" : "YM3812") : (use_sb16_ ? " + YMF262" : " + YM3812");
    if (midi_enabled_) devices += devices.empty() ? "MPU-401 MIDI" : " + MIDI";
    if (use_amd98_) devices += devices.empty() ? "AMD-98" : " + AMD-98";
    if (use_px_) devices += devices.empty() ? "Otomi-chan" : " + Otomi-chan";
    visual::copy(out.device, devices.empty() ? "PC-98 audio" : devices);
    visual::copy(out.driver, name());

    if (cpu_) {
        visual::add_register(out, "AX", cpu_->get_ax());
        visual::add_register(out, "BX", cpu_->get_bx());
        visual::add_register(out, "CX", cpu_->get_cx());
        visual::add_register(out, "DX", cpu_->get_dx());
        visual::add_register(out, "SI", cpu_->get_si());
        visual::add_register(out, "DI", cpu_->get_di());
        visual::add_register(out, "BP", cpu_->get_bp());
        visual::add_register(out, "CS", cpu_->get_cs());
        visual::add_register(out, "DS", cpu_->get_ds());
        visual::add_register(out, "ES", cpu_->get_es());
        visual::add_register(out, "SS", cpu_->get_ss());
        visual::add_register(out, "IP", cpu_->get_pc());
        visual::add_register(out, "FL", cpu_->get_flags());
    }

    const bool opna = ym2608_ != nullptr;
    const int fm_channels = opna ? 6 : (ym2203_ ? 3 : 0);
    for (int ch = 0; ch < fm_channels; ++ch) {
        const int bank = ch >= 3 ? 1 : 0;
        const int local = ch % 3;
        auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_FM, ch,
            std::string(opna ? "YM2608 FM#" : "YM2203 FM#") + std::to_string(ch));
        if (!v) break;
        const uint8_t lo = opna_registers_[bank][0xa0 + local];
        const uint8_t hi = opna_registers_[bank][0xa4 + local];
        const uint16_t fnum = static_cast<uint16_t>(lo | ((hi & 7) << 8));
        const uint8_t block = static_cast<uint8_t>((hi >> 3) & 7);
        v->active = opna_key_on_[ch] ? 1 : 0;
        v->midi_note = visual::opn_fnum_to_midi(fnum, block, opna ? 7987200.0 : 3993600.0);
        if (v->midi_note >= 0) {
            if (v->midi_note < 64) v->key_mask_lo = 1ull << v->midi_note;
            else v->key_mask_hi = 1ull << (v->midi_note - 64);
        }
        v->volume = visual::inverse_tl_volume(opna_registers_[bank][0x4c + local]);
        v->pan = opna ? visual::opn_pan(opna_registers_[bank][0xb4 + local]) : 0;
        v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
    }
    if (ym2203_ || ym2608_) {
        const uint8_t mixer = opna_registers_[0][7];
        for (int ch = 0; ch < 3; ++ch) {
            auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_SSG, ch,
                std::string(opna ? "YM2608 SSG#" : "YM2203 SSG#") + std::to_string(ch));
            if (!v) break;
            const uint16_t period = static_cast<uint16_t>(opna_registers_[0][ch * 2]
                | ((opna_registers_[0][ch * 2 + 1] & 0x0f) << 8));
            const int vol4 = opna_registers_[0][8 + ch] & 0x0f;
            const bool tone = (mixer & (1u << ch)) == 0;
            const bool noise = (mixer & (1u << (ch + 3))) == 0;
            v->active = (vol4 && (tone || noise)) ? 1 : 0;
            v->midi_note = (tone && period) ? visual::frequency_to_midi((opna ? 7987200.0 : 3993600.0) / (64.0 * period)) : -1;
            if (v->midi_note >= 0) {
                if (v->midi_note < 64) v->key_mask_lo = 1ull << v->midi_note;
                else v->key_mask_hi = 1ull << (v->midi_note - 64);
            }
            v->volume = std::clamp(vol4 * 8 + (vol4 ? 7 : 0), 0, 127);
            v->level = v->active ? static_cast<float>(v->volume) / 127.0f : 0.0f;
        }
    }
    if (ym2608_) {
        auto* ad = visual::add_channel(out, HOOT_VISUAL_CHANNEL_ADPCM, 0, "YM2608 ADPCM#0");
        if (ad) {
            ad->active = (opna_registers_[1][0x00] & 0x80) != 0;
            ad->volume = opna_registers_[1][0x0b] & 0x7f;
            ad->pan = visual::opn_pan(opna_registers_[1][0x01]);
            ad->level = ad->active ? static_cast<float>(ad->volume) / 127.0f : 0.0f;
        }
        for (int ch = 0; ch < 6; ++ch) {
            auto* r = visual::add_channel(out, HOOT_VISUAL_CHANNEL_RHYTHM, ch, "YM2608 RHYTHM#" + std::to_string(ch));
            if (!r) break;
            const uint8_t rr = opna_registers_[0][0x18 + ch];
            r->volume = std::clamp((0x3f - static_cast<int>(opna_registers_[0][0x11] & 0x3f)) * 2, 0, 127);
            r->pan = visual::opn_pan(rr);
            r->active = r->volume != 0;
            r->level = r->active ? static_cast<float>(r->volume) / 127.0f : 0.0f;
        }
    }
    if (beep_) {
        auto* b = visual::add_channel(out, HOOT_VISUAL_CHANNEL_BEEP, 0, "PC-98 BEEP");
        if (b) {
            b->active = beep_->speaker_enabled() ? 1 : 0;
            b->midi_note = visual::frequency_to_midi(beep_->frequency());
            if (b->midi_note >= 0) {
                if (b->midi_note < 64) b->key_mask_lo = 1ull << b->midi_note;
                else b->key_mask_hi = 1ull << (b->midi_note - 64);
            }
            b->volume = b->active ? 96 : 0;
            b->level = b->active ? 0.75f : 0.0f;
        }
    }
    if (pcm86_) {
        auto* p = visual::add_channel(out, HOOT_VISUAL_CHANNEL_PCM, 0, "PC-9801-86 PCM");
        if (p) {
            p->active = pcm86_->playback_enabled() && pcm86_->fifo_bytes() != 0;
            p->volume = std::clamp(static_cast<int>(pcm86_->volume_code()) * 8, 0, 127);
            p->level = p->active ? static_cast<float>(p->volume) / 127.0f : 0.0f;
        }
    }
    if (opl_) {
        const int opl_channels = opl_->model() == LibvgmOpl::Model::YMF262 ? 18 : 9;
        for (int ch = 0; ch < opl_channels && out.channel_count < HOOT_VISUAL_CHANNELS_MAX; ++ch) {
            const int bank = ch / 9;
            const int local = ch % 9;
            auto* o = visual::add_channel(out, HOOT_VISUAL_CHANNEL_OPL, ch,
                std::string(opl_->model() == LibvgmOpl::Model::YMF262 ? "YMF262 #" : "YM3812 #") + std::to_string(ch));
            if (!o) break;
            const uint8_t a = opl_->register_value(bank, static_cast<uint8_t>(0xa0 + local));
            const uint8_t b = opl_->register_value(bank, static_cast<uint8_t>(0xb0 + local));
            const uint16_t fnum = static_cast<uint16_t>(a | ((b & 3) << 8));
            const int block = (b >> 2) & 7;
            const double hz = fnum ? (static_cast<double>(fnum) * 3579545.0) / (72.0 * std::ldexp(1.0, 20 - block)) : 0.0;
            o->active = (b & 0x20) != 0;
            o->midi_note = visual::frequency_to_midi(hz);
            if (o->midi_note >= 0) {
                if (o->midi_note < 64) o->key_mask_lo = 1ull << o->midi_note;
                else o->key_mask_hi = 1ull << (o->midi_note - 64);
            }
            o->volume = o->active ? 100 : 0;
            if (opl_->model() == LibvgmOpl::Model::YMF262) {
                const uint8_t c = opl_->register_value(bank, static_cast<uint8_t>(0xc0 + local));
                const bool left = (c & 0x10) != 0, right = (c & 0x20) != 0;
                o->pan = left && !right ? -64 : (!left && right ? 63 : 0);
            }
            o->level = o->active ? 0.75f : 0.0f;
        }
    }
    if (midi_enabled_) {
        for (int ch = 0; ch < 16 && out.channel_count < HOOT_VISUAL_CHANNELS_MAX; ++ch) {
            const auto& m = midi_visualizer_.channel(static_cast<size_t>(ch));
            auto* v = visual::add_channel(out, HOOT_VISUAL_CHANNEL_MIDI, ch, "MIDI CH#" + std::to_string(ch + 1));
            if (!v) break;
            v->key_mask_lo = m.keys_lo;
            v->key_mask_hi = m.keys_hi;
            v->midi_note = (m.keys_lo || m.keys_hi) ? m.last_note : -1;
            v->active = (m.keys_lo || m.keys_hi) ? 1 : 0;
            v->volume = std::clamp((static_cast<int>(m.volume) * static_cast<int>(m.expression)) / 127, 0, 127);
            v->pan = std::clamp(static_cast<int>(m.pan) - 64, -64, 63);
            v->instrument = m.program;
            v->level = v->active ? (static_cast<float>(m.velocity) / 127.0f) * (static_cast<float>(v->volume) / 127.0f) : 0.0f;
        }
    }

    // Do not expose DS as "Driver Work". For generic PC-98 replay wrappers DS
    // is just the current guest data segment and is not a documented live
    // workspace pointer. Showing it as a work area produced convincing-looking
    // pages of 00 bytes that had no diagnostic meaning. A driver-specific
    // implementation can publish a real workspace when its ABI identifies one.
    out.driver_work_base = 0;
    out.driver_work_size = 0;
}

const char* Pc98DosDriver::name() const
{
    if (use_sound_orchestra_) return bare_mode_ ? "pc98-bare-soundorchestra" : "pc98dos-v30-soundorchestra";
    if (use_sb16_) return "pc98dos-v30-sb16";
    if (use_amd98_) return bare_mode_ ? "pc98-bare-amd98" : "pc98dos-v30-amd98";
    if (use_px2_) return "pc98dos-v30-otomix2";
    if (use_px_) return "pc98dos-v30-otomichan";
    if (bare_mode_) {
        if (use_ym2203_) return midi_enabled_ ? "pc98-bare-opn-midi" : "pc98-bare-opn";
        if (use_beep_) return midi_enabled_ ? "pc98-bare-beep-midi" : "pc98-bare-beep";
        if (use_pcm86_) return midi_enabled_ ? "pc98-bare-86-midi" : "pc98-bare-86";
        return midi_enabled_ ? "pc98-bare-opna-midi" : "pc98-bare-opna";
    }
    if (pc9821_mode_) {
        if (use_ym2203_) return midi_enabled_ ? "pc9821dos-386-opn-midi" : "pc9821dos-386-opn";
        if (use_beep_) return midi_enabled_ ? "pc9821dos-386-beep-midi" : "pc9821dos-386-beep";
        if (use_pcm86_) return midi_enabled_ ? "pc9821dos-386-86-midi" : "pc9821dos-386-86";
        return midi_enabled_ ? "pc9821dos-386-opna-midi" : "pc9821dos-386-opna";
    }
    if (use_ym2203_) return midi_enabled_ ? "pc98dos-v30-opn-midi" : "pc98dos-v30-opn";
    if (use_beep_) return midi_enabled_ ? "pc98dos-v30-beep-midi" : "pc98dos-v30-beep";
    if (use_pcm86_) return midi_enabled_ ? "pc98dos-v30-86-midi" : "pc98dos-v30-86";
    return midi_enabled_ ? "pc98dos-v30-opna-midi" : "pc98dos-v30-opna";
}

void Pc98DosDriver::clear()
{
    pc9821_mode_ = false;
    files_by_slot_.clear();
    files2_by_slot_.clear();
    files_by_name_.clear();
    bare_chunks_.clear();
    dos_open_files_.clear();
    next_dos_handle_ = 5;
    driver_data_.clear();
    shell_command_.clear();
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_file_offset_ = 0;
    selected_file_open_ = false;
    bridge_load_pending_ = false;
    bridge_command_active_ = false;
    bridge_command_ = 0xff;
    bridge_argument_ = 0xffff;
    driver_type_ = DriverType::Unknown;
    uses_hhd98_bridge_ = false;
    uses_pmd98_bridge_ = false;
    bridge_stdin_filename_ = false;
    bare_mode_ = false;
    bare_segmented_addresses_ = false;
    bare_boot_cs_ = 0x0060;
    bare_boot_ip_ = 0;
    bare_data_address_ = 0;
    bare_data2_address_ = 0;
    bare_has_data_address_ = false;
    bare_has_data2_address_ = false;
    bare_file_size_ = 0;
    bare_file2_size_ = 0;
    bare_hoot_params_.fill(0);
    bare_hoot_status_ = 0;
    bare_interrupt_reason_ = 0xff;
    bare_load_occurred_ = false;
    bare_hoot_interrupts_enabled_ = true;
    function_vector_ = 0x7f;
    cpu_.reset();
    ym2203_.reset();
    ym2608_.reset();
    opl_.reset();
    for (auto& psg : amd_psg_) psg.reset();
    for (auto& chip : px_opna_) chip.reset();
    pcm86_.reset();
    beep_.reset();
    mpu401_.reset();
    midi_synth_.reset();
    use_ym2203_ = false;
    use_sound_orchestra_ = false;
    use_sb16_ = false;
    use_amd98_ = false;
    use_px_ = false;
    use_px2_ = false;
    use_beep_ = false;
    use_pcm86_ = false;
    opl_gain_ = 1.0;
    amd_psg_address_.fill(0);
    amd_psg_portb_.fill(0);
    amd_psg3_address_ = 0;
    amd_timer_count_ = 0;
    amd_timer_control_ = 0;
    amd_timer_low_pending_ = true;
    amd_timer_interval_frames_ = 0;
    amd_timer_frames_until_next_ = 0;
    amd_rhythm_phase_.fill(0.0);
    amd_rhythm_level_.fill(0.0);
    for (auto& bank : px_address_) bank.fill(0);
    sb_dsp_fifo_.clear();
    sb_dsp_fifo_read_ = 0;
    sb_dsp_command_ = 0;
    sb_dsp_args_needed_ = 0;
    sb_dsp_args_received_ = 0;
    sb_reset_high_ = false;
    sb_speaker_on_ = false;
    sb_test_reg_ = 0;
    midi_enabled_ = false;
    midiout_type_ = -1;
    midi_gain_ = 0.70;
    debug_midi_irq_count_ = 0;
    debug_midi_synth_frames_ = 0;
    debug_midi_sysex_handled_ = 0;
    driver_warning_.clear();
    pcm86_gain_ = 1.0;
    beep_gain_ = 1.0;
    debug_beep_vrtc_irqs_ = 0;
    clock_multiplier_ = 8;
    shell_programs_.clear();
    mix_buffer_.clear();
    board_mix_buffer_.clear();
    int_vector_table_.clear();
    dos_memory_.clear();
    sample_rate_ = 44100;
    selected_track_ = 0;
    selected_code_ = 0;
    loaded_ = false;
    playing_ = false;
    pit_counter_ = 0;
    executed_cpu_steps_ = 0;
    timer_frames_until_tick_ = 0.0;
    fm_timer_a_ = 0;
    fm_timer_b_ = 0;
    fm_mode_ = 0;
    fm_prescaler_sel_ = 2;
    fm_status_ = 0;
    fm_timer_a_interval_frames_ = 0;
    fm_timer_a_frames_until_next_ = 0;
    fm_timer_b_interval_frames_ = 0;
    fm_timer_b_frames_until_next_ = 0;
    debug_fm_timer_irqs_ = 0;
    current_opna_address_[0] = 0;
    current_opna_address_[1] = 0;
    debug_opna_writes_ = 0;
    debug_opna_keyons_ = 0;
    debug_opna_keyoffs_ = 0;
    debug_last_key_command_ = 0;
    debug_opna_bank1_writes_ = 0;
    debug_opna_ssg_writes_ = 0;
    debug_opna_rhythm_writes_ = 0;
    debug_opna_rhythm_keyons_ = 0;
    debug_opna_rhythm_keyoffs_ = 0;
    debug_last_rhythm_command_ = 0;
    debug_ssg_writes_by_reg_.fill(0);
    debug_last_ssg_regs_.fill(0);
    debug_fm_keyons_by_channel_.fill(0);
    debug_keyon_masks_.fill(0);
    debug_last_opna_reg_ = 0;
    debug_last_opna_data_ = 0;
    trace_opna_ = false;
    trace_opna_events_ = 0;
    trace_opna_limit_ = 0;
    rendered_frames_ = 0;
    for (auto& bank : opna_registers_) {
        bank.fill(0);
    }
    opna_key_on_.fill(false);
    midi_visualizer_.reset();
    debug_file_opens_ = 0;
    debug_file_open_matches_ = 0;
    debug_file_reads_ = 0;
    debug_last_open_name_ = 0;
    dos_alloc_segment_ = 0x2000;
    bridge_buffer_segment_ = 0;
    current_shell_tsr_paragraphs_ = 0;
    current_psp_segment_ = kProgramSegment;
    shell_entry_cs_ = kProgramSegment;
    shell_entry_ip_ = kDosEntryPoint;
    shell_stack_ss_ = kProgramSegment;
    shell_stack_sp_ = 0xfffe;
    installed_shell_programs_ = 0;
    trace_dos_ = false;
    if (trace_file_.is_open()) {
        trace_file_.close();
    }
    trace_pc98_ = false;
    trace_events_ = 0;
    trace_event_limit_ = 0;
    shell_async_interrupts_ = false;
    suppress_async_interrupts_ = false;
}

bool Pc98DosDriver::setup_memory()
{
    if (!cpu_) {
        return false;
    }

    auto* mem = cpu_->memory();
    if (!mem) {
        return false;
    }

    std::memset(mem, 0, 1024 * 1024);
    // PC-98 BIOS data area 0501h bit 7: 0 = 5/10 MHz family PIT
    // (2.4576 MHz), 1 = 8 MHz family PIT (1.9968 MHz). The generic host
    // defaults to the more common 5/10 MHz timing used by these Hoot packs.
    mem[0x0501] &= 0x7f;
    mem[kIretOffset] = 0xcf;
    // Park the foreground guest in a real HLT loop.  HLT advances IP before
    // sleeping, so an interrupt taken while halted returns to kHaltOffset+1.
    // A bare HLT byte therefore let the CPU run into the low-memory IVT after
    // IRET.  The short jump sends that resume point back to HLT indefinitely.
    mem[kHaltOffset] = 0xf4;
    mem[kHaltOffset + 1] = 0xeb;
    mem[kHaltOffset + 2] = 0xfd;

    if (driver_type_ == DriverType::Shell) {
        const uint32_t psp_linear = static_cast<uint32_t>(kProgramSegment) << 4;
        mem[psp_linear + 0x0000] = 0xcd;
        mem[psp_linear + 0x0001] = 0x20;
        const std::string command(reinterpret_cast<const char*>(shell_command_.data()),
                                  shell_command_.empty() ? 0 : shell_command_.size() - 1);
        const auto token_end = command.find_first_of(" \t");
        const auto args_start = token_end == std::string::npos
            ? std::string::npos
            : command.find_first_not_of(" \t", token_end);
        std::string args = args_start == std::string::npos ? std::string() : command.substr(args_start);
        if (args.size() > 126) {
            args.resize(126);
        }
        mem[psp_linear + 0x80] = static_cast<uint8_t>(args.size());
        if (!args.empty()) {
            std::memcpy(mem + psp_linear + 0x81, args.data(), args.size());
        }
        mem[psp_linear + 0x81 + args.size()] = '\r';
    }

    if (!driver_data_.empty()) {
        const uint32_t entry_linear = (static_cast<uint32_t>(kProgramSegment) << 4) + kDosEntryPoint;
        auto copy_size = std::min(static_cast<size_t>(driver_data_.size()),
                                 static_cast<size_t>(1024 * 1024 - entry_linear));
        std::memcpy(mem + entry_linear, driver_data_.data(), copy_size);
    }

    if (driver_type_ != DriverType::Shell && driver_type_ != DriverType::Bare) {
        for (const auto& [slot, file] : files_by_slot_) {
            if (file.data.size() > 1024 * 1024) {
                continue;
            }
            std::memcpy(mem + (slot * 16), file.data.data(), file.data.size());
        }
    }

    return true;
}

void Pc98DosDriver::setup_interrupt_vectors()
{
    int_vector_table_.resize(256 * 4, 0);

    if (cpu_) {
        auto* mem = cpu_->memory();
        if (mem) {
            uint16_t ivt_base = 0x0000;

            mem[ivt_base + 0] = 0x00;
            mem[ivt_base + 1] = 0x10;
            mem[ivt_base + 2] = 0x00;
            mem[ivt_base + 3] = 0x00;

            mem[ivt_base + 0x1C * 4 + 0] = 0x00;
            mem[ivt_base + 0x1C * 4 + 1] = 0x10;
            mem[ivt_base + 0x1C * 4 + 2] = 0x00;
            mem[ivt_base + 0x1C * 4 + 3] = 0x00;

            // Provide an inert BIOS timer target before resident drivers hook
            // INT 08h.  Real PC-98 TSRs commonly save and chain the previous
            // timer vector; leaving it as 0000:0000 makes that chain jump into
            // the IVT instead of returning cleanly.
            setup_interrupt_vector(0x08, 0x0000, kIretOffset);
            setup_interrupt_vector(0x18, 0x0000, kIretOffset);
            setup_interrupt_vector(0x21, 0x0000, kIretOffset);
            setup_interrupt_vector(0x04, 0x0000, kIretOffset);
            setup_interrupt_vector(0x0b, 0x0000, kIretOffset);
            setup_interrupt_vector(0x2f, 0x0000, kIretOffset);
        }
    }
}

void Pc98DosDriver::setup_pit()
{
    const uint32_t pit_clock = 14318184 / 12;
    pit_rate_ = pit_clock / 60;
    pit_target_ = 0x10000 - pit_rate_;
    pit_counter_ = 0;
}

uint8_t Pc98DosDriver::read_memory_byte(uint32_t address) const
{
    if (address >= 1024 * 1024) {
        return 0xFF;
    }
    if (!cpu_) {
        return 0xFF;
    }
    return cpu_->memory()[address];
}

void Pc98DosDriver::write_memory_byte(uint32_t address, uint8_t data)
{
    if (address >= 1024 * 1024) {
        return;
    }
    if (!cpu_) {
        return;
    }
    cpu_->memory()[address] = data;
}

uint32_t Pc98DosDriver::bare_linear_address(uint16_t segment, uint16_t offset) const
{
    return (static_cast<uint32_t>(segment) << 4) + offset;
}

bool Pc98DosDriver::bare_load_asset(bool secondary, uint32_t slot, uint32_t address,
                                     uint32_t reserve, uint32_t file_offset,
                                     uint32_t requested_size, std::string* error)
{
    const auto& slots = secondary ? files2_by_slot_ : files_by_slot_;
    const auto it = slots.find(slot);
    const char* label = secondary ? "BGM2" : "BGM";
    if (it == slots.end()) {
        if (error) *error = std::string("PC-98 bare track references missing ") + label + " slot " + hex_slot(slot);
        return false;
    }
    const auto& src = it->second.data;
    if (file_offset > src.size()) {
        if (error) *error = std::string("PC-98 bare ") + label + " load offset exceeds source file";
        return false;
    }
    size_t copy_size = src.size() - static_cast<size_t>(file_offset);
    if (requested_size != 0xffffffffu) {
        copy_size = std::min(copy_size, static_cast<size_t>(requested_size));
    }
    const size_t capacity = reserve != 0 ? static_cast<size_t>(reserve) : copy_size;
    if (copy_size > capacity || address >= 1024u * 1024u || capacity > 1024u * 1024u - address) {
        if (error) *error = std::string("PC-98 bare ") + label + " asset exceeds configured guest buffer";
        return false;
    }
    for (size_t i = 0; i < capacity; ++i) write_memory_byte(address + static_cast<uint32_t>(i), 0);
    for (size_t i = 0; i < copy_size; ++i) {
        write_memory_byte(address + static_cast<uint32_t>(i), src[static_cast<size_t>(file_offset) + i]);
    }
    bare_load_occurred_ = true;
    return true;
}

void Pc98DosDriver::execute_bare_hoot_function(uint8_t function)
{
    // Original Hoot PC98 generic host protocol (PC98.TXT), ports 07D0h-07D7h.
    // Parameters are three little-endian words at 07D2h/4h/6h.
    bare_hoot_status_ = 0xff;
    const bool secondary = (bare_hoot_params_[0] & 0xffu) != 0;
    switch (function) {
    case 0x10: { // set primary/secondary automatic BGM load address
        const uint32_t address = bare_linear_address(bare_hoot_params_[2], bare_hoot_params_[1]);
        if (address < 1024u * 1024u) {
            if (secondary) {
                bare_data2_address_ = address;
                bare_has_data2_address_ = address != 0;
            } else {
                bare_data_address_ = address;
                bare_has_data_address_ = address != 0;
            }
            bare_hoot_status_ = 0x00;
        }
        break;
    }
    case 0x11: { // get configured primary/secondary BGM load address
        const uint32_t address = secondary ? bare_data2_address_ : bare_data_address_;
        if (address < 1024u * 1024u) {
            bare_hoot_params_[1] = static_cast<uint16_t>(address & 0x000fu);
            bare_hoot_params_[2] = static_cast<uint16_t>((address >> 4) & 0xffffu);
            bare_hoot_status_ = 0x00;
        }
        break;
    }
    case 0x18:
        // No current catalogue entry uses strlist. Keep the documented function
        // detectable and fail cleanly until such an asset is encountered.
        bare_hoot_status_ = 0xff;
        break;
    case 0x20:
    case 0x21: { // direct primary/secondary BGM load
        const bool sec = function == 0x21;
        const uint32_t address = bare_linear_address(bare_hoot_params_[2], bare_hoot_params_[1]);
        bare_hoot_status_ = bare_load_asset(sec, bare_hoot_params_[0], address, 0, 0, 0xffffffffu) ? 0x00 : 0xff;
        break;
    }
    case 0x30:
    case 0x31: { // extended load: destination + file offset + byte count in guest parameter block
        const bool sec = function == 0x31;
        const uint32_t parameter = bare_linear_address(bare_hoot_params_[2], bare_hoot_params_[1]);
        if (parameter > 1024u * 1024u - 12u) break;
        const uint16_t load_offset = static_cast<uint16_t>(read_memory_byte(parameter + 0)
            | (static_cast<uint16_t>(read_memory_byte(parameter + 1)) << 8));
        const uint16_t load_segment = static_cast<uint16_t>(read_memory_byte(parameter + 2)
            | (static_cast<uint16_t>(read_memory_byte(parameter + 3)) << 8));
        const uint32_t file_offset = static_cast<uint32_t>(read_memory_byte(parameter + 4))
            | (static_cast<uint32_t>(read_memory_byte(parameter + 5)) << 8)
            | (static_cast<uint32_t>(read_memory_byte(parameter + 6)) << 16)
            | (static_cast<uint32_t>(read_memory_byte(parameter + 7)) << 24);
        const uint32_t load_size = static_cast<uint32_t>(read_memory_byte(parameter + 8))
            | (static_cast<uint32_t>(read_memory_byte(parameter + 9)) << 8)
            | (static_cast<uint32_t>(read_memory_byte(parameter + 10)) << 16)
            | (static_cast<uint32_t>(read_memory_byte(parameter + 11)) << 24);
        const uint32_t address = bare_linear_address(load_segment, load_offset);
        bare_hoot_status_ = bare_load_asset(sec, bare_hoot_params_[0], address, load_size,
                                             file_offset, load_size) ? 0x00 : 0xff;
        break;
    }
    default:
        break;
    }
}

bool Pc98DosDriver::read_special_board_port(uint16_t port, uint8_t& value)
{
    if (use_sound_orchestra_ && opl_) {
        if (port == 0x018c) { value = opl_->read(0); return true; }
        if (port == 0x018e) { value = opl_->read(1); return true; }
    }

    if (use_sb16_ && opl_) {
        auto opl_port = [&](uint16_t p) -> int {
            if (p == 0x20d2 || p == 0xc8d2 || p == 0x28d2) return 0;
            if (p == 0x21d2 || p == 0xc9d2 || p == 0x29d2) return 1;
            if (p == 0x22d2 || p == 0xcad2) return 2;
            if (p == 0x23d2 || p == 0xcbd2) return 3;
            return -1;
        };
        const int op = opl_port(port);
        if (op >= 0) { value = opl_->read(static_cast<uint8_t>(op)); return true; }
        if (port == 0x2ad2) {
            if (sb_dsp_fifo_read_ < sb_dsp_fifo_.size()) value = sb_dsp_fifo_[sb_dsp_fifo_read_++];
            else value = 0xff;
            if (sb_dsp_fifo_read_ >= sb_dsp_fifo_.size()) {
                sb_dsp_fifo_.clear();
                sb_dsp_fifo_read_ = 0;
            }
            return true;
        }
        if (port == 0x2cd2) { value = 0x00; return true; } // DSP ready for write
        if (port == 0x2ed2) { value = sb_dsp_fifo_read_ < sb_dsp_fifo_.size() ? 0x80 : 0x00; return true; }
        if (port == 0x2fd2) { value = 0x00; return true; }
        // CT1745 mixer address/data are sufficient as floating-readable for
        // music-only SB16(98) drivers; they are not part of OPL synthesis.
        if (port == 0x24d2 || port == 0x25d2) { value = 0x00; return true; }
    }

    if (use_amd98_) {
        if (port == 0x00da || port == 0x00db) {
            const size_t chip = port == 0x00da ? 0u : 1u;
            const uint8_t reg = amd_psg_address_[chip];
            if (reg == 0x0e) { value = 0xff; return true; } // no joystick in headless host
            if (reg == 0x0f) { value = amd_psg_portb_[chip]; return true; }
            if (reg < 0x0e && amd_psg_[chip]) {
                amd_psg_[chip]->write(0, reg);
                value = amd_psg_[chip]->read(1);
                return true;
            }
            value = 0xff;
            return true;
        }
        if (port == 0x00d8 || port == 0x00d9 || port == 0x00dc || port == 0x00de) {
            value = 0xff;
            return true;
        }
    }

    if (use_px_) {
        static constexpr uint16_t bases[5] = {0x0088, 0x0188, 0x0488, 0x0588, 0x0288};
        const size_t count = use_px2_ ? 5u : 4u;
        for (size_t i = 0; i < count; ++i) {
            if (port < bases[i] || port > static_cast<uint16_t>(bases[i] + 6) || ((port - bases[i]) & 1u)) continue;
            const uint16_t off = port - bases[i];
            if (!px_opna_[i]) { value = 0xff; return true; }
            if (off == 0) { value = px_opna_[i]->read(0); return true; }
            if (off == 4) { value = px_opna_[i]->read(2); return true; }
            if (off == 2) {
                if (px_address_[i][0] == 0xff) value = 0x01;
                else value = px_opna_[i]->read(1);
                return true;
            }
            value = px_opna_[i]->read(3);
            return true;
        }
    }
    return false;
}

void Pc98DosDriver::arm_amd98_timer()
{
    uint32_t count = amd_timer_count_ == 0 ? 65536u : amd_timer_count_;
    // AMD-98 hangs an extra 8253-compatible channel from the PC-98 PIT
    // clock.  The generic host models the common 2.4576 MHz family.
    amd_timer_interval_frames_ = std::max(1, static_cast<int>(std::lround(
        static_cast<double>(count) * static_cast<double>(sample_rate_) / 2457600.0)));
    amd_timer_frames_until_next_ = amd_timer_interval_frames_;
}

void Pc98DosDriver::trigger_amd98_rhythm(uint8_t map)
{
    for (size_t i = 0; i < 4; ++i) {
        if (map & (1u << i)) {
            amd_rhythm_phase_[i] = 0.0;
            amd_rhythm_level_[i] = 1.0;
        }
    }
}

void Pc98DosDriver::service_amd98_timer_irq()
{
    if (!use_amd98_ || amd_timer_interval_frames_ <= 0) return;
    // NP2kai maps the AMD-98 timer to PIC IRQ 0Dh. In the PC-98 real-mode
    // vector layout used by this host that is INT 15h.
    if (is_interrupt_vector_active(0x15)) trigger_interrupt_vector(0x15, 200000);
    const uint8_t mode = static_cast<uint8_t>((amd_timer_control_ >> 1) & 0x07);
    if (mode == 2 || mode == 3) amd_timer_frames_until_next_ += amd_timer_interval_frames_;
    else amd_timer_interval_frames_ = 0;
}

bool Pc98DosDriver::write_special_board_port(uint16_t port, uint8_t data)
{
    if (use_sound_orchestra_ && opl_) {
        if (port == 0x018c) { opl_->write(0, data); return true; }
        if (port == 0x018e) { opl_->write(1, data); return true; }
    }

    if (use_sb16_ && opl_) {
        auto opl_port = [&](uint16_t p) -> int {
            if (p == 0x20d2 || p == 0xc8d2 || p == 0x28d2) return 0;
            if (p == 0x21d2 || p == 0xc9d2 || p == 0x29d2) return 1;
            if (p == 0x22d2 || p == 0xcad2) return 2;
            if (p == 0x23d2 || p == 0xcbd2) return 3;
            return -1;
        };
        const int op = opl_port(port);
        if (op >= 0) { opl_->write(static_cast<uint8_t>(op), data); return true; }
        if (port == 0x26d2) {
            if (data & 1) {
                sb_reset_high_ = true;
                sb_dsp_fifo_.clear();
                sb_dsp_fifo_read_ = 0;
            } else if (sb_reset_high_) {
                sb_reset_high_ = false;
                sb_dsp_fifo_.assign(1, 0xaa);
                sb_dsp_fifo_read_ = 0;
            }
            return true;
        }
        if (port == 0x2cd2) {
            auto execute = [this]() {
                switch (sb_dsp_command_) {
                case 0xe1: sb_dsp_fifo_.push_back(0x04); sb_dsp_fifo_.push_back(0x0c); break;
                case 0xe0: if (sb_dsp_args_received_ >= 1) sb_dsp_fifo_.push_back(static_cast<uint8_t>(~sb_dsp_args_[0])); break;
                case 0xd1: sb_speaker_on_ = true; break;
                case 0xd3: sb_speaker_on_ = false; break;
                case 0xd8: sb_dsp_fifo_.push_back(sb_speaker_on_ ? 0xff : 0x00); break;
                case 0xe4: if (sb_dsp_args_received_ >= 1) sb_test_reg_ = sb_dsp_args_[0]; break;
                case 0xe8: sb_dsp_fifo_.push_back(sb_test_reg_); break;
                default: break;
                }
                sb_dsp_args_received_ = 0;
                sb_dsp_args_needed_ = 0;
            };
            if (sb_dsp_args_needed_ > 0) {
                if (sb_dsp_args_received_ < static_cast<int>(sb_dsp_args_.size())) sb_dsp_args_[sb_dsp_args_received_] = data;
                ++sb_dsp_args_received_;
                if (sb_dsp_args_received_ >= sb_dsp_args_needed_) execute();
                return true;
            }
            sb_dsp_command_ = data;
            sb_dsp_args_received_ = 0;
            sb_dsp_args_needed_ = (data == 0xe0 || data == 0xe4) ? 1 : 0;
            if (sb_dsp_args_needed_ == 0) execute();
            return true;
        }
        if (port == 0x24d2 || port == 0x25d2) return true;
    }

    if (use_amd98_) {
        if (port == 0x00d8) { amd_psg_address_[0] = data; return true; }
        if (port == 0x00d9) { amd_psg_address_[1] = data; return true; }
        if (port == 0x00da) {
            const uint8_t reg = amd_psg_address_[0];
            if (reg < 0x10 && amd_psg_[0]) {
                amd_psg_[0]->write(0, reg);
                amd_psg_[0]->write(1, data);
                if (reg == 0x0f) amd_psg_portb_[0] = data;
            }
            return true;
        }
        if (port == 0x00db) {
            const uint8_t reg = amd_psg_address_[1];
            if (reg < 0x0e && amd_psg_[1]) {
                amd_psg_[1]->write(0, reg);
                amd_psg_[1]->write(1, data);
            } else if (reg == 0x0f) {
                const uint8_t old = amd_psg_portb_[1];
                if ((old & 1u) > (data & 1u)) {
                    const uint8_t mode = static_cast<uint8_t>(old & 0xc2u);
                    if (mode == 0x42) {
                        amd_psg3_address_ = amd_psg_portb_[0];
                    } else if (mode == 0x40) {
                        if (amd_psg3_address_ < 0x0e && amd_psg_[2]) {
                            amd_psg_[2]->write(0, amd_psg3_address_);
                            amd_psg_[2]->write(1, amd_psg_portb_[0]);
                        } else if (amd_psg3_address_ == 0x0f) {
                            trigger_amd98_rhythm(amd_psg_portb_[0]);
                        }
                    }
                }
                amd_psg_portb_[1] = data;
                if (amd_psg_[1]) {
                    amd_psg_[1]->write(0, 0x0f);
                    amd_psg_[1]->write(1, data);
                }
            }
            return true;
        }
        if (port == 0x00de) {
            amd_timer_control_ = data;
            amd_timer_low_pending_ = true;
            return true;
        }
        if (port == 0x00dc) {
            const uint8_t rw = static_cast<uint8_t>((amd_timer_control_ >> 4) & 3u);
            if (rw == 1) {
                amd_timer_count_ = static_cast<uint16_t>((amd_timer_count_ & 0xff00u) | data);
                arm_amd98_timer();
            } else if (rw == 2) {
                amd_timer_count_ = static_cast<uint16_t>((amd_timer_count_ & 0x00ffu) | (static_cast<uint16_t>(data) << 8));
                arm_amd98_timer();
            } else {
                if (amd_timer_low_pending_) {
                    amd_timer_count_ = static_cast<uint16_t>((amd_timer_count_ & 0xff00u) | data);
                    amd_timer_low_pending_ = false;
                } else {
                    amd_timer_count_ = static_cast<uint16_t>((amd_timer_count_ & 0x00ffu) | (static_cast<uint16_t>(data) << 8));
                    amd_timer_low_pending_ = true;
                    arm_amd98_timer();
                }
            }
            return true;
        }
    }

    if (use_px_) {
        static constexpr uint16_t bases[5] = {0x0088, 0x0188, 0x0488, 0x0588, 0x0288};
        const size_t count = use_px2_ ? 5u : 4u;
        for (size_t i = 0; i < count; ++i) {
            if (port < bases[i] || port > static_cast<uint16_t>(bases[i] + 6) || ((port - bases[i]) & 1u)) continue;
            const uint16_t off = port - bases[i];
            if (!px_opna_[i]) return true;
            if (off == 0) { px_address_[i][0] = data; px_opna_[i]->write(0, data); return true; }
            if (off == 2) {
                px_opna_[i]->write(1, data);
                if (i < 2) update_fm_timer(px_address_[i][0], data);
                return true;
            }
            if (off == 4) { px_address_[i][1] = data; px_opna_[i]->write(2, data); return true; }
            px_opna_[i]->write(3, data);
            return true;
        }
    }
    return false;
}

void Pc98DosDriver::render_special_boards(int16_t* interleaved_stereo, int frames)
{
    if (!interleaved_stereo || frames <= 0) return;
    auto mix_core = [&](auto* core, double gain = 1.0) {
        if (!core) return;
        board_mix_buffer_.assign(static_cast<size_t>(frames) * 2u, int16_t{0});
        core->render_s16(board_mix_buffer_.data(), frames);
        for (int i = 0; i < frames * 2; ++i) {
            const int64_t add = static_cast<int64_t>(std::lround(static_cast<double>(board_mix_buffer_[static_cast<size_t>(i)]) * gain));
            interleaved_stereo[i] = static_cast<int16_t>(std::clamp<int64_t>(static_cast<int64_t>(interleaved_stereo[i]) + add, -32768, 32767));
        }
    };
    if (opl_) mix_core(opl_.get(), opl_gain_);
    if (use_amd98_) {
        for (auto& psg : amd_psg_) mix_core(psg.get(), 0.75);
        static constexpr double hz[4] = {889.0476190476, 172.9411764706, 213.0, 255.44};
        static constexpr double decay[4] = {0.99910, 0.99720, 0.99720, 0.99690};
        for (int f = 0; f < frames; ++f) {
            double sample = 0.0;
            for (size_t r = 0; r < 4; ++r) {
                if (amd_rhythm_level_[r] < 0.0005) continue;
                amd_rhythm_phase_[r] += 2.0 * 3.14159265358979323846 * hz[r] / static_cast<double>(sample_rate_);
                sample += std::sin(amd_rhythm_phase_[r]) * amd_rhythm_level_[r] * (r == 0 ? 9000.0 : 4500.0);
                amd_rhythm_level_[r] *= decay[r];
            }
            const int64_t add = static_cast<int64_t>(std::lround(sample));
            for (int ch = 0; ch < 2; ++ch) {
                const size_t pos = static_cast<size_t>(f * 2 + ch);
                interleaved_stereo[pos] = static_cast<int16_t>(std::clamp<int64_t>(static_cast<int64_t>(interleaved_stereo[pos]) + add, -32768, 32767));
            }
        }
    }
    if (use_px_) {
        const size_t count = use_px2_ ? 5u : 4u;
        const double gain = count ? (0.72 / static_cast<double>(count)) : 1.0;
        for (size_t i = 0; i < count; ++i) mix_core(px_opna_[i].get(), gain);
    }
}

void Pc98DosDriver::reset_special_boards()
{
    if (opl_) opl_->reset();
    for (auto& psg : amd_psg_) if (psg) psg->reset();
    for (auto& chip : px_opna_) if (chip) {
        chip->reset();
        chip->write(0, 0x29);
        chip->write(1, 0x00);
    }
    amd_psg_address_.fill(0);
    amd_psg_portb_.fill(0);
    amd_psg3_address_ = 0;
    amd_timer_interval_frames_ = 0;
    amd_timer_frames_until_next_ = 0;
    amd_rhythm_phase_.fill(0.0);
    amd_rhythm_level_.fill(0.0);
    for (auto& bank : px_address_) bank.fill(0);
    sb_dsp_fifo_.clear();
    sb_dsp_fifo_read_ = 0;
    sb_dsp_args_needed_ = 0;
    sb_dsp_args_received_ = 0;
    sb_reset_high_ = false;
    sb_speaker_on_ = false;
}

uint8_t Pc98DosDriver::read_io_port(uint16_t port)
{
    uint8_t value = 0xff;
    if (driver_type_ == DriverType::Bare && port >= 0x07d0 && port <= 0x07d7) {
        if (port == 0x07d0) {
            value = bare_hoot_status_;
        } else if (port >= 0x07d2) {
            const unsigned param = static_cast<unsigned>((port - 0x07d2) / 2);
            const unsigned byte = static_cast<unsigned>((port - 0x07d2) & 1);
            if (param < bare_hoot_params_.size()) {
                value = static_cast<uint8_t>((bare_hoot_params_[param] >> (byte * 8)) & 0xffu);
            }
        }
        trace_io_event("in-hoot", port, value);
        return value;
    }
    if (port == 0x07e0) {
        value = driver_type_ == DriverType::Bare
            ? bare_interrupt_reason_
            : (bridge_command_active_ ? bridge_command_ : (bridge_load_pending_ ? 0x00 : 0x01));
        trace_io_event("in", port, value);
        return value;
    }
    if (driver_type_ == DriverType::Bare && port >= 0x07e2 && port <= 0x07e5) {
        value = static_cast<uint8_t>((selected_code_ >> ((port - 0x07e2) * 8)) & 0xffu);
        trace_io_event("in-hoot", port, value);
        return value;
    }
    if (port == 0x07e2) {
        value = static_cast<uint8_t>(bridge_argument_ & 0xff);
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x07e3) {
        value = static_cast<uint8_t>((bridge_argument_ >> 8) & 0xff);
        trace_io_event("in", port, value);
        return value;
    }
    // PC-98 8259 interrupt-mask registers.  MIDI residents probe and
    // temporarily unmask their candidate MPU IRQ here even on BEEP-only
    // configurations, so these ports must exist independently from OPN.
    if (port == 0x0002) {
        value = pic_master_mask_;
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x000a) {
        value = pic_slave_mask_;
        trace_io_event("in", port, value);
        return value;
    }
    // PC-98 VRTC status. Hoot bridge helpers wait for a clear->set edge while
    // loading a song; alternating bit 5 supplies that edge without requiring
    // a video subsystem in this audio-only host.
    if (port == 0x00a0) {
        vrtc_phase_ = !vrtc_phase_;
        value = vrtc_phase_ ? 0x20 : 0x00;
        trace_io_event("in", port, value);
        return value;
    }
    if (mpu401_ && mpu401_->handles_port(port)) {
        value = mpu401_->read_port(port);
        trace_io_event("in-midi", port, value);
        return value;
    }
    if (pcm86_ && pcm86_->handles_port(port)) {
        value = pcm86_->read_port(port);
        trace_io_event("in", port, value);
        return value;
    }
    if (beep_ && beep_->handles_port(port)) {
        value = beep_->read_port(port);
        trace_io_event("in", port, value);
        return value;
    }
    if (read_special_board_port(port, value)) {
        trace_io_event("in-board", port, value);
        return value;
    }
    if (!ym2203_ && !ym2608_) {
        trace_io_event("in", port, value);
        return value;
    }

    if (port == 0x88 || port == 0x8B || port == 0x188) {
        if (use_ym2203_ && uses_hhd98_bridge_ && cpu_ && cpu_->get_pc() == 0x067d) {
            if (trace_dos_) {
                std::fprintf(stderr, "pc98dos hhd timer-status pc=%04x\n", cpu_->get_pc());
            }
            value = 0x03;
            trace_io_event("in", port, value);
            return value;
        }
        // Drivers poll bits 0/1 for YM Timer A/B. Hide the transient busy
        // flag for OPN compatibility, but expose timer status so resident
        // PMD/NAX interrupt handlers can acknowledge the actual source.
        value = use_ym2203_ ? static_cast<uint8_t>(fm_status_ & 0x03)
                            : static_cast<uint8_t>(read_opn(0) | (fm_status_ & 0x03));
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x89 || port == 0x8A || port == 0x18A) {
        // YM2608 register FFh is the chip-identification register.  PC-98
        // utilities such as PCMSET probe it through each candidate I/O base
        // and require the documented value 01h before using the extended
        // Delta-T/ADPCM ports.
        if (ym2608_ && current_opna_address_[0] == 0xff) {
            value = 0x01;
        } else {
            value = opna_registers_[0][current_opna_address_[0]];
        }
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x8C || port == 0x8F || port == 0x18C) {
        // On the PC-9801-86, A460h bit 0 controls decode of the extended
        // YM2608 port pair. PMD86 deliberately clears it during board
        // detection and expects the disabled ports to float high (FFh).
        if (pcm86_ && !pcm86_->extended_opna_enabled() && port == 0x18C) {
            value = 0xff;
        } else {
            value = read_opn(2);
        }
        trace_io_event("in", port, value);
        return value;
    }
    if (port == 0x8D || port == 0x8E || port == 0x18E) {
        if (pcm86_ && !pcm86_->extended_opna_enabled() && port == 0x18E) {
            value = 0xff;
        } else {
            value = opna_registers_[1][current_opna_address_[1]];
        }
        trace_io_event("in", port, value);
        return value;
    }

    trace_io_event("in", port, value);
    return value;
}

void Pc98DosDriver::write_io_port(uint16_t port, uint8_t data)
{
    trace_io_event("out", port, data);
    if (driver_type_ == DriverType::Bare && port >= 0x07d0 && port <= 0x07d7) {
        if (port == 0x07d0) {
            execute_bare_hoot_function(data);
        } else if (port >= 0x07d2) {
            const unsigned param = static_cast<unsigned>((port - 0x07d2) / 2);
            const unsigned byte = static_cast<unsigned>((port - 0x07d2) & 1);
            if (param < bare_hoot_params_.size()) {
                const uint16_t mask = static_cast<uint16_t>(0xffu << (byte * 8));
                bare_hoot_params_[param] = static_cast<uint16_t>((bare_hoot_params_[param] & ~mask)
                    | (static_cast<uint16_t>(data) << (byte * 8)));
            }
        }
        return;
    }
    if (driver_type_ == DriverType::Bare && port == 0x07e8) {
        if (data == 0x80) bare_hoot_interrupts_enabled_ = false;
        else if (data == 0x81) bare_hoot_interrupts_enabled_ = true;
        // 82h/83h select normal/burst CPU scheduling in original Hoot.  This
        // audio-only host already advances guest execution against rendered
        // time, so both modes intentionally share the deterministic scheduler.
        return;
    }
    if (port == 0x0002) {
        pic_master_mask_ = data;
        return;
    }
    if (port == 0x000a) {
        pic_slave_mask_ = data;
        return;
    }
    if (mpu401_ && mpu401_->handles_port(port)) {
        mpu401_->write_port(port, data);
        return;
    }
    if (pcm86_ && pcm86_->handles_port(port)) {
        pcm86_->write_port(port, data);
        return;
    }
    if (beep_ && beep_->handles_port(port)) {
        beep_->write_port(port, data);
        return;
    }
    if (write_special_board_port(port, data)) {
        return;
    }
    if (!ym2203_ && !ym2608_) {
        return;
    }

    if (port == 0x88 || port == 0x188) {
        current_opna_address_[0] = data;
        write_opn(0, data);
    } else if (port == 0x89 || port == 0x8A || port == 0x18A) {
        const uint8_t chip_data = (current_opna_address_[0] >= 0xb4 && current_opna_address_[0] <= 0xb6)
            ? static_cast<uint8_t>(data | 0xc0)
            : data;
        ++debug_opna_writes_;
        debug_last_opna_reg_ = current_opna_address_[0];
        debug_last_opna_data_ = chip_data;
        if (current_opna_address_[0] < 0x10) {
            ++debug_opna_ssg_writes_;
            ++debug_ssg_writes_by_reg_[current_opna_address_[0] & 0x0f];
            debug_last_ssg_regs_[current_opna_address_[0] & 0x0f] = chip_data;
            if (trace_opna_ && trace_opna_events_ < trace_opna_limit_) {
                std::fprintf(stderr,
                             "pc98opn frame=%llu ssg r%02x=%02x regs[6,7,8,9,a,b,c,d]=%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n",
                             static_cast<unsigned long long>(rendered_frames_),
                             current_opna_address_[0],
                             chip_data,
                             debug_last_ssg_regs_[0x06],
                             debug_last_ssg_regs_[0x07],
                             debug_last_ssg_regs_[0x08],
                             debug_last_ssg_regs_[0x09],
                             debug_last_ssg_regs_[0x0a],
                             debug_last_ssg_regs_[0x0b],
                             debug_last_ssg_regs_[0x0c],
                             debug_last_ssg_regs_[0x0d]);
                ++trace_opna_events_;
            }
        } else if (current_opna_address_[0] < 0x20) {
            ++debug_opna_rhythm_writes_;
            if (current_opna_address_[0] == 0x10) {
                debug_last_rhythm_command_ = chip_data;
                if ((chip_data & 0x80) != 0) {
                    ++debug_opna_rhythm_keyoffs_;
                } else if ((chip_data & 0x3f) != 0) {
                    ++debug_opna_rhythm_keyons_;
                }
            }
        }
        update_fm_timer(current_opna_address_[0], chip_data);
        if (current_opna_address_[0] == 0x28 && (data & 0xf0) != 0) {
            ++debug_opna_keyons_;
            debug_last_key_command_ = data;
            ++debug_keyon_masks_[(data >> 4) & 0x0f];
            uint8_t channel = data & 0x03;
            if (channel != 3) {
                const uint8_t base_channel = channel;
                if ((data & 0x04) != 0) {
                    channel = static_cast<uint8_t>(channel + 3);
                }
                if (channel < opna_key_on_.size()) opna_key_on_[channel] = true;
                ++debug_fm_keyons_by_channel_[channel];
                if (trace_opna_ && trace_opna_events_ < trace_opna_limit_) {
                    const uint8_t alg = static_cast<uint8_t>(opna_registers_[0][0xb0 + base_channel] & 0x07);
                    const uint8_t feedback = static_cast<uint8_t>((opna_registers_[0][0xb0 + base_channel] >> 3) & 0x07);
                    std::fprintf(stderr,
                                 "pc98opn frame=%llu fm-keyon cmd=%02x ch=%u alg=%u fb=%u tl=%02x,%02x,%02x,%02x ar=%02x,%02x,%02x,%02x rr=%02x,%02x,%02x,%02x\n",
                                 static_cast<unsigned long long>(rendered_frames_),
                                 data,
                                 channel,
                                 alg,
                                 feedback,
                                 opna_registers_[0][0x40 + base_channel],
                                 opna_registers_[0][0x44 + base_channel],
                                 opna_registers_[0][0x48 + base_channel],
                                 opna_registers_[0][0x4c + base_channel],
                                 opna_registers_[0][0x50 + base_channel],
                                 opna_registers_[0][0x54 + base_channel],
                                 opna_registers_[0][0x58 + base_channel],
                                 opna_registers_[0][0x5c + base_channel],
                                 opna_registers_[0][0x80 + base_channel],
                                 opna_registers_[0][0x84 + base_channel],
                                 opna_registers_[0][0x88 + base_channel],
                                 opna_registers_[0][0x8c + base_channel]);
                    ++trace_opna_events_;
                }
            }
        } else if (current_opna_address_[0] == 0x28) {
            ++debug_opna_keyoffs_;
            debug_last_key_command_ = data;
            uint8_t channel = static_cast<uint8_t>(data & 0x03);
            if (channel != 3) {
                if ((data & 0x04) != 0) channel = static_cast<uint8_t>(channel + 3);
                if (channel < opna_key_on_.size()) opna_key_on_[channel] = false;
            }
        }
        opna_registers_[0][current_opna_address_[0]] = chip_data;
        write_opn(1, chip_data);
        if ((current_opna_address_[0] >= 0x40 && current_opna_address_[0] <= 0x4e)
            || (current_opna_address_[0] >= 0xb0 && current_opna_address_[0] <= 0xb2)) {
            apply_opn_fm_tl_compat(static_cast<uint8_t>(current_opna_address_[0] & 0x03));
        }
    } else if (port == 0x8C || port == 0x18C) {
        if (!(pcm86_ && !pcm86_->extended_opna_enabled() && port == 0x18C)) {
            current_opna_address_[1] = data;
            write_opn(2, data);
        }
    } else if (port == 0x8D || port == 0x8E || port == 0x18E) {
        if (pcm86_ && !pcm86_->extended_opna_enabled() && port == 0x18E) {
            return;
        }
        ++debug_opna_writes_;
        ++debug_opna_bank1_writes_;
        debug_last_opna_reg_ = static_cast<uint16_t>(0x100 | current_opna_address_[1]);
        debug_last_opna_data_ = data;
        opna_registers_[1][current_opna_address_[1]] = data;
        write_opn(3, data);
    }

    if (port == kPitIoport) {
    }
}

void Pc98DosDriver::write_opn(uint8_t port, uint8_t data)
{
    if (ym2203_) {
        if ((port & 2) != 0) {
            return;
        }
        ym2203_->write(static_cast<uint8_t>(port & 1), data);
    } else if (ym2608_) {
        ym2608_->write(static_cast<uint8_t>(port & 3), data);
    }
}

uint8_t Pc98DosDriver::read_opn(uint8_t port)
{
    if (ym2203_) {
        if ((port & 2) != 0) {
            return 0xff;
        }
        return ym2203_->read(static_cast<uint8_t>(port & 1));
    }
    if (ym2608_) {
        return ym2608_->read(static_cast<uint8_t>(port & 3));
    }
    return 0xff;
}

void Pc98DosDriver::update_fm_timer(uint8_t reg, uint8_t data)
{
    switch (reg) {
    case 0x24:
        fm_timer_a_ = static_cast<uint16_t>((fm_timer_a_ & 0x0003u) | (static_cast<uint16_t>(data) << 2));
        refresh_fm_irq_interval();
        break;
    case 0x25:
        fm_timer_a_ = static_cast<uint16_t>((fm_timer_a_ & 0x03fcu) | (data & 0x03));
        refresh_fm_irq_interval();
        break;
    case 0x26:
        fm_timer_b_ = data;
        refresh_fm_irq_interval();
        break;
    case 0x27:
        // YM2203/YM2608 mode bits 4/5 clear timer status flags.
        if ((data & 0x10) != 0) fm_status_ &= static_cast<uint8_t>(~0x01u);
        if ((data & 0x20) != 0) fm_status_ &= static_cast<uint8_t>(~0x02u);
        fm_mode_ = data;
        refresh_fm_irq_interval();
        break;
    case 0x2d:
        fm_prescaler_sel_ |= 0x02;
        refresh_fm_irq_interval();
        break;
    case 0x2e:
        fm_prescaler_sel_ |= 0x01;
        refresh_fm_irq_interval();
        break;
    case 0x2f:
        fm_prescaler_sel_ = 0;
        refresh_fm_irq_interval();
        break;
    default:
        break;
    }
}

void Pc98DosDriver::refresh_fm_irq_interval()
{
    static constexpr int kTimerPrescalerBySel[4] = {24, 24, 72, 36};
    // YM2608 runs the OPN timer block behind the chip's extra /2 input stage.
    // This matches both the bundled FM core (OPNPrescaler_w pre_divider=2)
    // and original Hoot's 72/(clock/2) timer interval.  Using selector 0 and
    // omitting the YM2608 pre-divider made default OPN playback 3x too fast
    // and default OPNA playback 6x too fast.
    const int chip_predivider = use_ym2203_ ? 1 : 2;
    const int prescaler = kTimerPrescalerBySel[fm_prescaler_sel_ & 0x03]
        * chip_predivider;
    const double clock = use_ym2203_ ? 3'993'632.0 : 7'967'264.0;

    if ((fm_mode_ & 0x01) != 0 && sample_rate_ > 0) {
        const uint16_t timer_a = static_cast<uint16_t>(fm_timer_a_ & 0x03ffu);
        const double seconds = static_cast<double>(1024 - timer_a) * static_cast<double>(prescaler) / clock;
        fm_timer_a_interval_frames_ = std::max(1, static_cast<int>(std::lround(seconds * sample_rate_)));
        if (fm_timer_a_frames_until_next_ <= 0
            || fm_timer_a_frames_until_next_ > fm_timer_a_interval_frames_ * 4) {
            fm_timer_a_frames_until_next_ = fm_timer_a_interval_frames_;
        }
    } else {
        fm_timer_a_interval_frames_ = 0;
        fm_timer_a_frames_until_next_ = 0;
    }

    if ((fm_mode_ & 0x02) != 0 && fm_timer_b_ != 0xff && sample_rate_ > 0) {
        const double seconds = static_cast<double>((256 - fm_timer_b_) << 4)
            * static_cast<double>(prescaler) / clock;
        fm_timer_b_interval_frames_ = std::max(1, static_cast<int>(std::lround(seconds * sample_rate_)));
        if (fm_timer_b_frames_until_next_ <= 0
            || fm_timer_b_frames_until_next_ > fm_timer_b_interval_frames_ * 4) {
            fm_timer_b_frames_until_next_ = fm_timer_b_interval_frames_;
        }
    } else {
        fm_timer_b_interval_frames_ = 0;
        fm_timer_b_frames_until_next_ = 0;
    }
}

void Pc98DosDriver::service_fm_timer_irq(uint8_t status_bit)
{
    // Advance the expired timer before entering the guest ISR.  The ISR often
    // acknowledges/restarts the timer by writing register 27; if we reloaded
    // after returning, that guest write and the host reload would stack and
    // accidentally double the period.
    if (status_bit == 0x01 && fm_timer_a_interval_frames_ > 0) {
        fm_timer_a_frames_until_next_ += fm_timer_a_interval_frames_;
    } else if (status_bit == 0x02 && fm_timer_b_interval_frames_ > 0) {
        fm_timer_b_frames_until_next_ += fm_timer_b_interval_frames_;
    }

    fm_status_ |= status_bit;
    const bool irq_enabled = (status_bit == 0x01) ? ((fm_mode_ & 0x04) != 0)
                                                   : ((fm_mode_ & 0x08) != 0);
    if (irq_enabled) {
        // PC-98 OPN/OPNA residents commonly install the sound-board IRQ on
        // INT 0Bh.  The former code incorrectly interpreted physical 0000:1000
        // as a far-pointer hook; that address is ordinary low memory, not the
        // IVT entry for this IRQ, so real timer-driven residents (e.g. MDRV)
        // never received a YM timer overflow.
        // PC-98 has two 8259 PICs with vector bases 08h and 10h.  Sound
        // drivers do not all use the same IRQ: PMD-style residents often use
        // INT 0Bh, while older Falcom drivers (e.g. Ys II) select a slave-PIC
        // line such as INT 14h.  Resolve the vector from the guest's PIC masks
        // instead of hard-coding INT 0Bh.
        auto deliver_if_unmasked = [this](uint8_t vector, uint8_t mask, unsigned bit) {
            if ((mask & static_cast<uint8_t>(1u << bit)) != 0) return false;
            if (!is_interrupt_vector_active(vector)) return false;
            ++debug_fm_timer_irqs_;
            trigger_interrupt_vector(vector, 200000);
            return true;
        };
        // Preserve the historically common OPN IRQ first when it is actually
        // enabled, then honor any active slave/master PIC line selected by the
        // guest.  Skip master INT 08h here: that is the system timer, not the
        // FM-board interrupt.
        if (deliver_if_unmasked(0x0b, pic_master_mask_, 3)) return;
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (deliver_if_unmasked(static_cast<uint8_t>(0x10u + bit), pic_slave_mask_, bit)) return;
        }
        for (unsigned bit = 1; bit < 8; ++bit) {
            const uint8_t vector = static_cast<uint8_t>(0x08u + bit);
            if (vector == 0x0b) continue;
            if (deliver_if_unmasked(vector, pic_master_mask_, bit)) return;
        }
        // Some synthetic/legacy wrappers do not model the PIC mask. Keep the
        // old INT 0Bh behavior as a compatibility fallback when no guest PIC
        // line is discoverable.
        if (is_interrupt_vector_active(0x0b)) {
            ++debug_fm_timer_irqs_;
            trigger_interrupt_vector(0x0b, 200000);
            return;
        }
        // Retain the historical fallback for synthetic/legacy wrappers that
        // explicitly plant a far pointer at 0000:1000.
        const uint16_t hook_offset = static_cast<uint16_t>(read_memory_byte(0x1000)
            | (read_memory_byte(0x1001) << 8));
        const uint16_t hook_segment = static_cast<uint16_t>(read_memory_byte(0x1002)
            | (read_memory_byte(0x1003) << 8));
        if (hook_segment != 0 && hook_offset != 0) {
            ++debug_fm_timer_irqs_;
            trigger_far_interrupt(hook_segment, hook_offset, 200000);
        }
    }
}

int Pc98DosDriver::current_mpu_irq_line() const
{
    if (!mpu401_ || !mpu401_->irq_pending()) return -1;

    const auto line_available = [this](int line) {
        if (line < 0 || line >= 15) return false;
        const uint8_t bit = static_cast<uint8_t>(1u << (line & 7));
        const uint8_t mask = line < 8 ? pic_master_mask_ : pic_slave_mask_;
        if ((mask & bit) != 0) return false;
        const uint8_t vector = static_cast<uint8_t>(line + 8);
        return is_interrupt_vector_active(vector);
    };

    if (line_available(selected_mpu_irq_line_)) return selected_mpu_irq_line_;
    for (int line = 0; line < 15; ++line) {
        if (line_available(line)) return line;
    }
    return -1;
}

void Pc98DosDriver::service_mpu401_irq()
{
    if (!cpu_ || !mpu401_ || !mpu401_->irq_pending() || suppress_async_interrupts_) return;
    if (!cpu_->get_interrupt_flag()) return;

    const int line = current_mpu_irq_line();
    if (line < 0) return;
    if (selected_mpu_irq_line_ < 0) selected_mpu_irq_line_ = line;

    ++debug_midi_irq_count_;
    trigger_async_interrupt_vector(static_cast<uint8_t>(line + 8), 200000);
}

void Pc98DosDriver::service_pcm86_irq()
{
    if (!pcm86_ || !pcm86_->irq_pending()) {
        return;
    }

    // The 86-board PCM low-water interrupt reaches the same resident sound
    // service layer used by PC-98 music drivers. Prefer the BIOS sound hook
    // when installed and retain INT 0Bh as the legacy resident fallback.
    const uint16_t hook_offset = static_cast<uint16_t>(read_memory_byte(0x1000)
        | (read_memory_byte(0x1001) << 8));
    const uint16_t hook_segment = static_cast<uint16_t>(read_memory_byte(0x1002)
        | (read_memory_byte(0x1003) << 8));
    if (hook_segment != 0 && hook_offset != 0) {
        trigger_far_interrupt(hook_segment, hook_offset, 200000);
    } else if (is_interrupt_vector_active(0x0b)) {
        trigger_interrupt_vector(0x0b, 200000);
    }
    pcm86_->mark_irq_delivered();
}

void Pc98DosDriver::render_opn(int16_t* interleaved_stereo, int frames)
{
    if (ym2203_) {
        ym2203_->render_s16(interleaved_stereo, frames);
    } else if (ym2608_) {
        ym2608_->render_s16(interleaved_stereo, frames);
    } else if (interleaved_stereo != nullptr && frames > 0) {
        std::fill(interleaved_stereo, interleaved_stereo + (frames * 2), int16_t{0});
    }
    render_special_boards(interleaved_stereo, frames);
    if (pcm86_) {
        pcm86_->mix_s16(interleaved_stereo, frames, pcm86_gain_);
    }
    if (beep_) {
        beep_->mix_s16(interleaved_stereo, frames, beep_gain_);
    }
    render_midi(interleaved_stereo, frames);
}


void Pc98DosDriver::setup_midi(const HootEntry& entry)
{
    if (!midi_enabled_) {
        return;
    }

    mpu401_ = std::make_unique<Pc98Mpu401>();
    mpu401_->reset();
    mpu401_->set_sink([this](const X68kMidiMessage& message) {
        handle_midi_message(message);
    });

    midi_gain_ = 0.70;
    if (const char* value = std::getenv("HOOT_X68K_MIDI_GAIN")) {
        const double parsed = std::strtod(value, nullptr);
        if (std::isfinite(parsed)) midi_gain_ = std::clamp(parsed, 0.0, 4.0);
    }

    const bool munt_compatible = midiout_type_ == 1 || midiout_type_ == 2 || midiout_type_ == 3;
    const bool fluidsynth_compatible = midiout_type_ == 4 || midiout_type_ == 7 || midiout_type_ == 8;
    std::string preference = "auto";
    if (const char* value = std::getenv("HOOT_X68K_MIDI_BACKEND"); value != nullptr && value[0] != '\0') {
        preference = value;
        std::transform(preference.begin(), preference.end(), preference.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    const bool backend_disabled = preference == "none" || preference == "off";
    const bool force_nuked = preference == "nuked" || preference == "nuked-sc55" || preference == "nuked-sc55-clap";
    const bool force_fluid = preference == "fluidsynth" || preference == "fluid";
    const bool force_munt = preference == "munt" || preference == "mt32emu" || preference == "mt-32";
    const bool force_cm64 = preference == "cm64" || preference == "cm-64";
    const bool force_cm32p = preference == "cm32p" || preference == "cm-32p";
    std::string nuked_error;
    std::string fluid_error;
    std::string munt_error;
    std::string cm64_error;
    std::string cm32p_error;
    const std::string cm32p_card = cm32p_card_hint(entry.title);

    if (!backend_disabled && midiout_type_ == 3 && !force_nuked && !force_fluid && !force_munt && !force_cm32p) {
        auto synth = std::make_unique<Cm64MidiSynth>(cm32p_card);
        if (synth->open(sample_rate_, {}, cm64_error)) {
            if (!cm32p_card.empty() && !synth->pcm_card_loaded()) {
                driver_warning_ = cm32p_missing_card_warning(cm32p_card);
            }
            midi_synth_ = std::move(synth);
        }
    }
    if (!backend_disabled && midiout_type_ == 3 && force_cm32p) {
        auto synth = std::make_unique<Cm32pMidiSynth>(cm32p_card);
        if (synth->open(sample_rate_, {}, cm32p_error)) {
            if (!cm32p_card.empty() && !synth->card_loaded()) {
                driver_warning_ = cm32p_missing_card_warning(cm32p_card);
            }
            midi_synth_ = std::move(synth);
        }
    }
    if (!backend_disabled && !midi_synth_ && munt_compatible && !force_nuked && !force_fluid && !force_cm64 && !force_cm32p) {
        const auto model = midiout_type_ == 3 ? Mt32EmuModel::CM32L : Mt32EmuModel::MT32;
        auto synth = std::make_unique<Mt32EmuMidiSynth>(model);
        if (synth->open(sample_rate_, {}, munt_error)) {
            midi_synth_ = std::move(synth);
            if (midiout_type_ == 3 && preference == "auto" && !cm64_error.empty()) {
                driver_warning_ = cm64_la_only_warning(cm32p_card);
            }
        }
    }

    if (!backend_disabled && midiout_type_ == 4 && !force_fluid && !force_munt && !force_cm64 && !force_cm32p) {
        auto synth = std::make_unique<NukedSc55ClapMidiSynth>();
        if (synth->open(sample_rate_, {}, nuked_error)) midi_synth_ = std::move(synth);
    }
    if (!backend_disabled && !midi_synth_ && fluidsynth_compatible && !force_nuked && !force_munt && !force_cm64 && !force_cm32p) {
        auto synth = std::make_unique<FluidSynthMidiSynth>();
        if (synth->open(sample_rate_, {}, fluid_error)) midi_synth_ = std::move(synth);
    }

    if (midi_synth_) {
        reset_midi_synth_mode();
        if (midiout_type_ == 7 && driver_warning_.empty() &&
            std::string(midi_synth_->backend_name()) == "fluidsynth") {
            driver_warning_ = "Roland SC-88 target: FluidSynth compatibility rendering is active, not hardware-exact SC-88. "
                "Set [midi] soundfont in hootplay.ini for the closest available compatible bank; authentic playback requires a real SC-88 or a dedicated SC-88-compatible renderer.";
        }
    } else if (backend_disabled) {
        driver_warning_ = "PC-98 MIDI transport enabled; software synthesizer disabled by configuration";
    } else if (force_cm64 && midiout_type_ != 3) {
        driver_warning_ = "CM-64 backend is only used for Hoot midiout_type 3";
    } else if (force_cm64) {
        driver_warning_ = cm64_error.empty() ? "full CM-64 backend unavailable" : cm64_error;
    } else if (force_cm32p && midiout_type_ != 3) {
        driver_warning_ = "CM-32P backend is only used for Hoot CM-64 midiout_type 3";
    } else if (force_cm32p) {
        driver_warning_ = cm32p_error.empty() ? "CM-32P backend unavailable" : cm32p_error;
    } else if (force_munt && !munt_compatible) {
        driver_warning_ = "Munt/mt32emu is only used for Hoot MT-32/CM-64 midiout_type 1, 2 or 3";
    } else if (force_munt) {
        driver_warning_ = munt_error.empty() ? "Munt/mt32emu backend unavailable" : munt_error;
    } else if (force_nuked && midiout_type_ != 4) {
        driver_warning_ = "Nuked-SC55 is only used for Hoot GS/SC-55 midiout_type 4";
    } else if (force_nuked) {
        driver_warning_ = nuked_error.empty() ? "Nuked-SC55 backend unavailable" : nuked_error;
    } else if (force_fluid) {
        driver_warning_ = fluid_error.empty() ? "FluidSynth backend unavailable" : fluid_error;
    } else if (fluidsynth_compatible) {
        if (!nuked_error.empty() && midiout_type_ == 4) {
            driver_warning_ = nuked_error + "; " +
                (fluid_error.empty() ? "FluidSynth fallback unavailable" : fluid_error);
        } else {
            driver_warning_ = fluid_error.empty() ? "MIDI software synth unavailable" : fluid_error;
        }
    } else if (midiout_type_ == 3) {
        if (!cm64_error.empty() && !munt_error.empty()) driver_warning_ = cm64_error + "; " + munt_error;
        else driver_warning_ = !cm64_error.empty() ? cm64_error : (munt_error.empty() ? "CM-64 software synth unavailable" : munt_error);
    } else if (munt_compatible) {
        driver_warning_ = munt_error.empty() ? "Munt/mt32emu backend unavailable" : munt_error;
    } else {
        driver_warning_ = "PC-98 MPU-401 transport active; this MIDI module class needs a dedicated synth backend";
    }

}

void Pc98DosDriver::reset_midi_synth_mode()
{
    if (!midi_synth_ || !midi_synth_->active()) {
        return;
    }
    midi_synth_->reset();
    if (midiout_type_ == 4 || midiout_type_ == 7) {
        midi_synth_->sysex({0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41});
    } else if (midiout_type_ == 8) {
        midi_synth_->sysex({0x7e, 0x7f, 0x09, 0x01});
    }
}

void Pc98DosDriver::handle_midi_message(const X68kMidiMessage& message)
{
    midi_visualizer_.message(message);
    if (trace_pc98_) {
        std::ostringstream event;
        event << "{\"type\":\"midi-message\",\"status\":" << static_cast<unsigned>(message.status);
        if (message.kind == X68kMidiMessage::Kind::SysEx) {
            event << ",\"kind\":\"sysex\",\"bytes\":" << message.sysex.size();
        } else {
            event << ",\"kind\":\"short\",\"size\":" << static_cast<unsigned>(message.size)
                  << ",\"data1\":" << static_cast<unsigned>(message.data1)
                  << ",\"data2\":" << static_cast<unsigned>(message.data2);
        }
        event << "}";
        emit_trace_event(event.str());
    }
    if (!midi_synth_ || !midi_synth_->active()) {
        return;
    }
    if (message.kind == X68kMidiMessage::Kind::SysEx) {
        midi_synth_->sysex(message.sysex);
        ++debug_midi_sysex_handled_;
    } else {
        midi_synth_->short_message(message.status, message.data1, message.data2, message.size);
    }
}

void Pc98DosDriver::render_midi(int16_t* interleaved_stereo, int frames)
{
    if (!midi_enabled_ || !midi_synth_ || !midi_synth_->active() || !interleaved_stereo || frames <= 0) {
        return;
    }
    mix_buffer_.resize(static_cast<size_t>(frames) * 2u);
    std::fill(mix_buffer_.begin(), mix_buffer_.end(), int16_t{0});
    if (midi_synth_->render_s16(mix_buffer_.data(), frames) != frames) {
        return;
    }
    debug_midi_synth_frames_ += static_cast<uint64_t>(frames);
    for (int i = 0; i < frames * 2; ++i) {
        const int64_t midi = static_cast<int64_t>(std::lround(static_cast<double>(mix_buffer_[static_cast<size_t>(i)]) * midi_gain_));
        const int64_t mixed = static_cast<int64_t>(interleaved_stereo[i]) + midi;
        interleaved_stereo[i] = static_cast<int16_t>(std::clamp<int64_t>(mixed, -32768, 32767));
    }
}

void Pc98DosDriver::reset_opn()
{
    if (ym2203_) {
        ym2203_->reset();
    }
    if (ym2608_) {
        ym2608_->reset();
        ym2608_->write(0, 0x29);
        ym2608_->write(1, 0x00);
    }
}

void Pc98DosDriver::apply_opn_fm_tl_compat(uint8_t channel)
{
    if (disable_opn_tl_compat_ || (!ym2203_ && !ym2608_) || channel >= 3) {
        return;
    }

    const uint8_t algorithm = opna_registers_[0][0xb0 + channel] & 0x07;
    const uint8_t source_tl = opna_registers_[0][0x44 + channel] & 0x7f;
    if (source_tl >= 0x7f) {
        return;
    }
    bool changed = false;

    const auto lower_tl = [&](uint8_t reg) {
        const uint8_t current = opna_registers_[0][reg] & 0x7f;
        if (current <= source_tl) {
            return;
        }
        if (trace_opna_ && trace_opna_events_ < trace_opna_limit_) {
            std::fprintf(stderr,
                         "pc98opn frame=%llu tl-compat ch=%u alg=%u reg=%02x %02x->%02x\n",
                         static_cast<unsigned long long>(rendered_frames_),
                         channel,
                         algorithm,
                         reg,
                         current,
                         source_tl);
            ++trace_opna_events_;
        }
        opna_registers_[0][reg] = source_tl;
        write_opn(0, reg);
        write_opn(1, source_tl);
        changed = true;
    };

    if (algorithm <= 3) {
        lower_tl(static_cast<uint8_t>(0x4c + channel));
    } else if (algorithm == 4) {
        lower_tl(static_cast<uint8_t>(0x48 + channel));
        lower_tl(static_cast<uint8_t>(0x4c + channel));
    } else if (algorithm == 5 || algorithm == 6) {
        lower_tl(static_cast<uint8_t>(0x44 + channel));
        lower_tl(static_cast<uint8_t>(0x48 + channel));
        lower_tl(static_cast<uint8_t>(0x4c + channel));
    }
    if (changed) {
        write_opn(0, current_opna_address_[0]);
    }
}

void Pc98DosDriver::handle_interrupt(uint8_t int_num)
{
    trace_interrupt_event(int_num);
    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos int%02x ax=%04x bx=%04x cx=%04x dx=%04x ds=%04x cs:ip=%04x:%04x\n",
                     int_num,
                     cpu_ ? cpu_->get_ax() : 0,
                     cpu_ ? cpu_->get_bx() : 0,
                     cpu_ ? cpu_->get_cx() : 0,
                     cpu_ ? cpu_->get_dx() : 0,
                     cpu_ ? cpu_->get_ds() : 0,
                     cpu_ ? cpu_->get_cs() : 0,
                     cpu_ ? cpu_->get_pc() : 0);
    }
    if (int_num == 0x21) {
        handle_dos_interrupt();
    } else if (int_num == 0x18 && cpu_ && cpu_->get_ax() == 0x9801) {
        cpu_->halt();
    }
}

void Pc98DosDriver::handle_dos_interrupt()
{
    if (!cpu_) {
        return;
    }

    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos int21 ah=%02x al=%02x ax=%04x bx=%04x cx=%04x dx=%04x si=%04x ds=%04x es=%04x cs:ip=%04x:%04x\n",
                     cpu_->get_ah(),
                     cpu_->get_al(),
                     cpu_->get_ax(),
                     cpu_->get_bx(),
                     cpu_->get_cx(),
                     cpu_->get_dx(),
                     cpu_->get_si(),
                     cpu_->get_ds(),
                     cpu_->get_es(),
                     cpu_->get_cs(),
                     cpu_->get_pc());
    }

    switch (cpu_->get_ah()) {
    case 0x09:
        cpu_->set_carry(false);
        break;
    case 0x1a:
        dos_dta_segment_ = cpu_->get_ds();
        dos_dta_offset_ = cpu_->get_dx();
        cpu_->set_carry(false);
        break;
    case 0x25:
        setup_interrupt_vector(cpu_->get_al(), cpu_->get_ds(), cpu_->get_dx());
        cpu_->set_carry(false);
        break;
    case 0x35: {
        const uint32_t addr = static_cast<uint32_t>(cpu_->get_al()) * 4;
        cpu_->set_bx(static_cast<uint16_t>(read_memory_byte(addr)
            | (read_memory_byte(addr + 1) << 8)));
        cpu_->set_es(static_cast<uint16_t>(read_memory_byte(addr + 2)
            | (read_memory_byte(addr + 3) << 8)));
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos get-vector int=%02x -> %04x:%04x\n",
                         cpu_->get_al(),
                         cpu_->get_es(),
                         cpu_->get_bx());
        }
        cpu_->set_carry(false);
        break;
    }
    case 0x3d:
        dos_open_file();
        break;
    case 0x3e:
        dos_close_file();
        break;
    case 0x3f:
        dos_read_file();
        break;
    case 0x4e:
        dos_find_first();
        break;
    case 0x42:
        dos_seek_file();
        break;
    case 0x31:
        // Terminate-and-stay-resident: DX is the number of paragraphs kept
        // starting at the PSP.  Preserve it so subsequent shell programs are
        // loaded beyond the resident instead of on top of it.
        current_shell_tsr_paragraphs_ = cpu_->get_dx();
        cpu_->set_carry(false);
        cpu_->halt();
        break;
    case 0x4c:
        cpu_->set_carry(false);
        cpu_->halt();
        break;
    case 0x48: {
        const uint16_t paragraphs = cpu_->get_bx();
        if (paragraphs == 0xffff || paragraphs == 0) {
            cpu_->set_ax(0x0008);
            cpu_->set_bx(0x1000);
            cpu_->set_carry(true);
            break;
        }
        const uint16_t segment = dos_alloc_segment_;
        dos_alloc_segment_ = static_cast<uint16_t>(dos_alloc_segment_ + paragraphs + 1);
        cpu_->set_ax(segment);
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos alloc paragraphs=%04x -> %04x\n",
                         paragraphs,
                         segment);
        }
        cpu_->set_carry(false);
        break;
    }
    case 0x49:
        cpu_->set_carry(false);
        break;
    case 0x4a:
        cpu_->set_carry(false);
        break;
    case 0x62:
        cpu_->set_bx(current_psp_segment_);
        cpu_->set_carry(false);
        break;
    default:
        cpu_->set_carry(false);
        break;
    }
}

std::string Pc98DosDriver::read_dos_string(uint16_t segment, uint16_t offset) const
{
    std::string result;
    if (!cpu_ || !cpu_->memory()) {
        return result;
    }
    uint32_t addr = (static_cast<uint32_t>(segment) << 4) + offset;
    for (size_t i = 0; i < 256 && addr < 1024 * 1024; ++i, ++addr) {
        const char ch = static_cast<char>(cpu_->memory()[addr]);
        if (ch == '\0' || ch == '$' || ch == '\r' || ch == '\n') {
            break;
        }
        result.push_back(ch);
    }
    return result;
}

const Pc98DosDriver::LoadedFile* Pc98DosDriver::find_dos_file(const std::string& name) const
{
    if (name.empty()) {
        return nullptr;
    }
    std::string normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    normalized = to_lower(normalized);
    while (normalized.rfind("./", 0) == 0) {
        normalized.erase(0, 2);
    }
    if (const auto it = files_by_name_.find(normalized); it != files_by_name_.end()) {
        return &it->second;
    }
    const auto base = to_lower(std::filesystem::path(normalized).filename().string());
    if (const auto it = files_by_name_.find(base); it != files_by_name_.end()) {
        return &it->second;
    }
    const auto stem = to_lower(std::filesystem::path(base).stem().string());
    if (!stem.empty()) {
        for (const auto& [key, file] : files_by_name_) {
            if (to_lower(std::filesystem::path(key).stem().string()) == stem) {
                return &file;
            }
        }
    }
    return nullptr;
}

void Pc98DosDriver::dos_find_first()
{
    if (!cpu_) {
        return;
    }

    const auto raw_name = read_dos_string(cpu_->get_ds(), cpu_->get_dx());
    const LoadedFile* file = find_dos_file(raw_name);
    if (!file) {
        if (trace_dos_) {
            std::fprintf(stderr, "pc98dos findfirst miss \"%s\"\n", raw_name.c_str());
        }
        cpu_->set_ax(0x0012); // DOS: no more files / no match.
        cpu_->set_carry(true);
        return;
    }

    uint16_t dta_segment = dos_dta_segment_;
    uint16_t dta_offset = dos_dta_offset_;
    if (dta_segment == 0) {
        dta_segment = current_psp_segment_;
        dta_offset = 0x0080;
    }
    const uint32_t dta = (static_cast<uint32_t>(dta_segment) << 4) + dta_offset;
    // DOS 2+ FindFirst DTA layout: 21 reserved bytes, attribute, time, date,
    // 32-bit file size, then an ASCIIZ 8.3 filename at offset 1Eh.  Several
    // PC-98 resident music drivers use the size field to allocate/load their
    // selected song after an existence probe.
    for (uint32_t i = 0; i < 43; ++i) {
        write_memory_byte(dta + i, 0);
    }
    write_memory_byte(dta + 0x15, 0x20); // archive/normal file
    const uint32_t size = static_cast<uint32_t>(std::min<size_t>(file->data.size(), 0xffffffffu));
    for (int i = 0; i < 4; ++i) {
        write_memory_byte(dta + 0x1a + static_cast<uint32_t>(i),
                          static_cast<uint8_t>((size >> (i * 8)) & 0xff));
    }
    const auto basename = std::filesystem::path(file->path).filename().string();
    const auto name_size = std::min<size_t>(basename.size(), 12);
    for (size_t i = 0; i < name_size; ++i) {
        write_memory_byte(dta + 0x1e + static_cast<uint32_t>(i),
                          static_cast<uint8_t>(basename[i]));
    }
    write_memory_byte(dta + 0x1e + static_cast<uint32_t>(name_size), 0);
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos findfirst match \"%s\" -> \"%s\" size=%u dta=%04x:%04x\n",
                     raw_name.c_str(), file->path.c_str(), size, dta_segment, dta_offset);
    }
    cpu_->set_ax(0);
    cpu_->set_carry(false);
}

void Pc98DosDriver::dos_open_file()
{
    if (!cpu_) {
        return;
    }
    ++debug_file_opens_;
    const auto raw_name = read_dos_string(cpu_->get_ds(), cpu_->get_dx());
    debug_last_open_name_ = 0;
    for (size_t i = 0; i < std::min<size_t>(raw_name.size(), 4); ++i) {
        debug_last_open_name_ |= static_cast<uint32_t>(static_cast<uint8_t>(raw_name[i])) << (i * 8);
    }

    const LoadedFile* file = find_dos_file(raw_name);
    if (file) {
        uint16_t handle = next_dos_handle_;
        while (dos_open_files_.count(handle) != 0 || handle < 5) {
            ++handle;
            if (handle == 0) {
                handle = 5;
            }
        }
        next_dos_handle_ = static_cast<uint16_t>(handle + 1);
        dos_open_files_[handle] = DosOpenFile{&file->data, 0, file->path};
        ++debug_file_open_matches_;
        cpu_->set_ax(handle);
        std::ostringstream event;
        event << "{\"type\":\"file_open\",\"result\":\"match\",\"name\":\"" << json_escape(raw_name)
              << "\",\"path\":\"" << json_escape(file->path)
              << "\",\"handle\":" << handle << "}";
        emit_trace_event(event.str());
        if (trace_dos_) {
            std::fprintf(stderr, "pc98dos open match \"%s\" -> \"%s\" handle=%u\n",
                         raw_name.c_str(), file->path.c_str(), handle);
        }
        cpu_->set_carry(false);
        return;
    }

    // Some Hoot bridge helpers deliberately bypass DOS open and read the
    // currently selected song through pseudo handle 0. Keep that path
    // separate from the ordinary DOS file table.
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos open miss \"%s\" selected=\"%s\"\n",
                     raw_name.c_str(), selected_bgm_path_.c_str());
    }
    std::ostringstream event;
    event << "{\"type\":\"file_open\",\"result\":\"miss\",\"name\":\"" << json_escape(raw_name)
          << "\",\"selected\":\"" << json_escape(selected_bgm_path_) << "\"}";
    emit_trace_event(event.str());
    cpu_->set_ax(0x0002);
    cpu_->set_carry(true);
}

void Pc98DosDriver::dos_read_file()
{
    if (!cpu_) {
        return;
    }
    const uint16_t handle = cpu_->get_bx();
    const std::vector<uint8_t>* data = nullptr;
    size_t* file_offset = nullptr;
    std::string path;

    std::vector<uint8_t> filename_input;
    if (handle == 0 && selected_file_open_) {
        if (bridge_stdin_filename_) {
            const auto basename = std::filesystem::path(selected_bgm_path_).filename().string();
            filename_input.assign(basename.begin(), basename.end());
            data = &filename_input;
            path = std::string("<filename:") + basename + ">";
        } else {
            data = &selected_bgm_data_;
            path = selected_bgm_path_;
        }
        file_offset = &selected_file_offset_;
    } else {
        const auto it = dos_open_files_.find(handle);
        if (it != dos_open_files_.end() && it->second.data) {
            data = it->second.data;
            file_offset = &it->second.offset;
            path = it->second.path;
        }
    }

    if (!data || !file_offset) {
        if (trace_dos_) {
            std::fprintf(stderr, "pc98dos read rejected handle=%u\n", handle);
        }
        cpu_->set_ax(0x0006);
        cpu_->set_carry(true);
        return;
    }

    ++debug_file_reads_;
    const auto requested = static_cast<size_t>(cpu_->get_cx());
    const auto remaining = data->size() > *file_offset ? data->size() - *file_offset : 0;
    const auto count = std::min(requested, remaining);
    uint16_t dest_segment = cpu_->get_ds();
    uint16_t dest_offset = cpu_->get_dx();
    if (handle == 0 && dest_segment == 0x0000 && dest_offset == 0x07e0) {
        const uint32_t int60 = 0x60 * 4;
        const uint16_t hhd_segment = static_cast<uint16_t>(read_memory_byte(int60 + 2)
            | (read_memory_byte(int60 + 3) << 8));
        if (hhd_segment != 0) {
            dest_segment = hhd_segment;
            dest_offset = 0x1dbb;
        }
    }
    const uint32_t dest = (static_cast<uint32_t>(dest_segment) << 4) + dest_offset;
    for (size_t i = 0; i < count && dest + i < 1024 * 1024; ++i) {
        write_memory_byte(dest + static_cast<uint32_t>(i), (*data)[*file_offset + i]);
    }
    *file_offset += count;
    cpu_->set_ax(static_cast<uint16_t>(count));
    if (handle == 0) {
        bridge_load_pending_ = false;
    }
    std::ostringstream event;
    event << "{\"type\":\"file_read\",\"handle\":" << handle
          << ",\"path\":\"" << json_escape(path)
          << "\",\"request\":" << requested
          << ",\"count\":" << count
          << ",\"offset\":" << *file_offset
          << ",\"dst_seg\":" << dest_segment
          << ",\"dst_off\":" << dest_offset << "}";
    emit_trace_event(event.str());
    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos read handle=%u path=\"%s\" request=%zu count=%zu offset=%zu dst=%04x:%04x\n",
                     handle, path.c_str(), requested, count, *file_offset, dest_segment, dest_offset);
    }
    cpu_->set_carry(false);
}

void Pc98DosDriver::dos_close_file()
{
    if (!cpu_) {
        return;
    }
    const uint16_t handle = cpu_->get_bx();
    if (handle == 0) {
        selected_file_open_ = false;
    } else {
        dos_open_files_.erase(handle);
    }
    cpu_->set_carry(false);
}

void Pc98DosDriver::dos_seek_file()
{
    if (!cpu_) {
        return;
    }
    const uint16_t handle = cpu_->get_bx();
    const std::vector<uint8_t>* data = nullptr;
    size_t* file_offset = nullptr;
    if (handle == 0 && selected_file_open_) {
        data = &selected_bgm_data_;
        file_offset = &selected_file_offset_;
    } else {
        const auto it = dos_open_files_.find(handle);
        if (it != dos_open_files_.end() && it->second.data) {
            data = it->second.data;
            file_offset = &it->second.offset;
        }
    }
    if (!data || !file_offset) {
        // Resident PMD-family drivers expose internal pseudo-handles that
        // their Hoot helper probes with DOS seek before attempting a read.
        // Legacy Hoot treated those seeks as harmless no-ops; preserve that
        // behaviour while real archive-backed DOS handles use true offsets.
        cpu_->set_ax(0);
        cpu_->set_dx(0);
        cpu_->set_carry(false);
        return;
    }
    const int32_t displacement = static_cast<int32_t>(
        (static_cast<uint32_t>(cpu_->get_cx()) << 16) | cpu_->get_dx());
    int64_t base = 0;
    switch (cpu_->get_al()) {
    case 0: base = 0; break;
    case 1: base = static_cast<int64_t>(*file_offset); break;
    case 2: base = static_cast<int64_t>(data->size()); break;
    default:
        cpu_->set_ax(0x0001);
        cpu_->set_carry(true);
        return;
    }
    int64_t target = base + displacement;
    if (target < 0) {
        target = 0;
    }
    *file_offset = static_cast<size_t>(target);
    const uint32_t pos = static_cast<uint32_t>(std::min<size_t>(*file_offset, 0xffffffffu));
    cpu_->set_ax(static_cast<uint16_t>(pos & 0xffff));
    cpu_->set_dx(static_cast<uint16_t>(pos >> 16));
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos seek handle=%u origin=%u displacement=%d -> %zu\n",
                     handle, cpu_->get_al(), displacement, *file_offset);
    }
    cpu_->set_carry(false);
}

void Pc98DosDriver::pit_timer_tick()
{
    pit_counter_++;
    if (pit_counter_ >= pit_rate_) {
        pit_counter_ = 0;
    }
}

void Pc98DosDriver::reset_cpu_context(uint16_t segment)
{
    if (!cpu_) {
        return;
    }

    cpu_->reset();
    cpu_->set_cs(segment);
    cpu_->set_ds(segment);
    cpu_->set_es(segment);
    cpu_->set_ss(segment);
    cpu_->set_sp(0xFFFE);
    cpu_->set_pc(static_cast<uint16_t>(kDosEntryPoint));
    executed_cpu_steps_ = 0;
}

void Pc98DosDriver::push_cpu_word(uint16_t value)
{
    const uint16_t sp = static_cast<uint16_t>(cpu_->get_sp() - 2);
    cpu_->set_sp(sp);
    const uint32_t addr = (static_cast<uint32_t>(cpu_->get_ss()) << 4) + sp;
    write_memory_byte(addr, static_cast<uint8_t>(value & 0xff));
    write_memory_byte(addr + 1, static_cast<uint8_t>((value >> 8) & 0xff));
}

void Pc98DosDriver::setup_interrupt_vector(uint8_t vector, uint16_t segment, uint16_t offset)
{
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    write_memory_byte(addr, static_cast<uint8_t>(offset & 0xff));
    write_memory_byte(addr + 1, static_cast<uint8_t>((offset >> 8) & 0xff));
    write_memory_byte(addr + 2, static_cast<uint8_t>(segment & 0xff));
    write_memory_byte(addr + 3, static_cast<uint8_t>((segment >> 8) & 0xff));
    {
        std::ostringstream event;
        event << "{\"type\":\"set_vector\",\"int\":" << static_cast<unsigned>(vector)
              << ",\"seg\":" << segment
              << ",\"off\":" << offset << "}";
        emit_trace_event(event.str());
    }
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos set-vector int=%02x -> %04x:%04x\n", vector, segment, offset);
    }
}

bool Pc98DosDriver::is_interrupt_vector_active(uint8_t vector) const
{
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
        | (read_memory_byte(addr + 1) << 8));
    const uint16_t segment = static_cast<uint16_t>(read_memory_byte(addr + 2)
        | (read_memory_byte(addr + 3) << 8));
    if (offset == 0 && segment == 0) {
        return false;
    }
    if (segment == 0 && offset == kIretOffset) {
        return false;
    }
    if (segment == 0 && offset == kHaltOffset) {
        return false;
    }
    return true;
}

void Pc98DosDriver::trigger_interrupt_vector(uint8_t vector, int steps)
{
    if (!cpu_) {
        return;
    }
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
        | (read_memory_byte(addr + 1) << 8));
    const uint16_t segment = static_cast<uint16_t>(read_memory_byte(addr + 2)
        | (read_memory_byte(addr + 3) << 8));
    if (offset == 0 && segment == 0) {
        return;
    }
    const bool was_halted = cpu_->is_halted();
    cpu_->clear_halted();
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    push_cpu_word(0x0200);
    push_cpu_word(0x0000);
    push_cpu_word(kHaltOffset);
    cpu_->set_cs(segment);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    if (was_halted) {
        cpu_->halt();
    } else {
        cpu_->clear_halted();
    }
}

void Pc98DosDriver::trigger_async_interrupt_vector(uint8_t vector, int steps)
{
    if (!cpu_ || suppress_async_interrupts_) {
        return;
    }
    const uint32_t addr = static_cast<uint32_t>(vector) * 4;
    const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
        | (read_memory_byte(addr + 1) << 8));
    const uint16_t segment = static_cast<uint16_t>(read_memory_byte(addr + 2)
        | (read_memory_byte(addr + 3) << 8));
    if (offset == 0 && segment == 0) {
        return;
    }
    if (segment == 0 && (offset == kIretOffset || offset == kHaltOffset)) {
        return;
    }

    suppress_async_interrupts_ = true;
    const bool was_halted = cpu_->is_halted();
    cpu_->clear_halted();
    push_cpu_word(0x0200);
    push_cpu_word(cpu_->get_cs());
    push_cpu_word(cpu_->get_pc());
    cpu_->set_cs(segment);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    if (was_halted) {
        cpu_->halt();
    } else {
        cpu_->clear_halted();
    }
    suppress_async_interrupts_ = false;
}

void Pc98DosDriver::trigger_far_interrupt(uint16_t segment, uint16_t offset, int steps)
{
    if (!cpu_ || segment == 0 || offset == 0 || suppress_async_interrupts_) {
        return;
    }
    suppress_async_interrupts_ = true;
    const bool was_halted = cpu_->is_halted();
    cpu_->clear_halted();
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    push_cpu_word(0x0200);
    push_cpu_word(0x0000);
    push_cpu_word(kHaltOffset);
    cpu_->set_cs(segment);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    if (was_halted) cpu_->halt(); else cpu_->clear_halted();
    suppress_async_interrupts_ = false;
}

void Pc98DosDriver::trigger_near_subroutine(uint16_t segment, uint16_t offset, int steps)
{
    if (!cpu_ || segment == 0) {
        return;
    }
    cpu_->clear_halted();
    cpu_->set_cs(segment);
    cpu_->set_ds(segment);
    cpu_->set_es(segment);
    cpu_->set_ss(segment);
    cpu_->set_sp(0xfffe);
    const uint32_t halt_linear = (static_cast<uint32_t>(segment) << 4) + kHaltOffset;
    write_memory_byte(halt_linear, 0xf4);
    push_cpu_word(kHaltOffset);
    cpu_->set_pc(offset);
    run_cpu_steps(steps);
    cpu_->clear_halted();
}

bool Pc98DosDriver::rebuild_shell_runtime()
{
    if (!cpu_ || driver_type_ != DriverType::Shell) {
        return false;
    }

    playing_ = false;
    selected_bgm_path_.clear();
    selected_voice_path_.clear();
    selected_bgm_data_.clear();
    selected_file_offset_ = 0;
    selected_file_handle_ = 5;
    selected_file_open_ = false;
    bridge_load_pending_ = false;
    bridge_command_active_ = false;
    bridge_command_ = 0xff;
    bridge_argument_ = 0xffff;

    dos_open_files_.clear();
    next_dos_handle_ = 5;
    dos_alloc_segment_ = 0x2000;
    bridge_buffer_segment_ = 0;
    current_shell_tsr_paragraphs_ = 0;
    current_psp_segment_ = kProgramSegment;
    shell_entry_cs_ = kProgramSegment;
    shell_entry_ip_ = kDosEntryPoint;
    shell_stack_ss_ = kProgramSegment;
    shell_stack_sp_ = 0xfffe;
    installed_shell_programs_ = 0;
    shell_async_interrupts_ = false;
    suppress_async_interrupts_ = false;

    pit_counter_ = 0;
    timer_frames_until_tick_ = 0.0;
    fm_timer_a_ = 0;
    fm_timer_b_ = 0;
    fm_mode_ = 0;
    fm_prescaler_sel_ = 2;
    fm_status_ = 0;
    fm_timer_a_interval_frames_ = 0;
    fm_timer_a_frames_until_next_ = 0;
    fm_timer_b_interval_frames_ = 0;
    fm_timer_b_frames_until_next_ = 0;
    current_opna_address_[0] = 0;
    current_opna_address_[1] = 0;
    for (auto& bank : opna_registers_) {
        bank.fill(0);
    }

    reset_opn();
    if (pcm86_) {
        pcm86_->reset();
    }

    if (!setup_memory()) {
        return false;
    }
    setup_interrupt_vectors();
    setup_pit();
    reset_cpu_context();
    install_shell_driver();
    return true;
}

void Pc98DosDriver::install_shell_driver()
{
    if (shell_programs_.empty()) {
        reset_cpu_context();
        run_cpu_steps(2000000);
        cpu_->clear_halted();
        return;
    }

    run_shell_program(shell_programs_.front(), kProgramSegment);
    if (uses_hhd98_bridge_) {
        const uint32_t base = static_cast<uint32_t>(kProgramSegment) << 4;
        write_memory_byte(base + 0x1d6f, 0x01);
        write_memory_byte(base + 0x1d70, 0x00);
    }
    installed_shell_programs_ = 1;
}

void Pc98DosDriver::load_shell_program(const ShellProgram& program, uint16_t segment)
{
    if (!cpu_ || program.data.empty()) {
        return;
    }
    auto* mem = cpu_->memory();
    shell_entry_cs_ = segment;
    shell_entry_ip_ = kDosEntryPoint;
    shell_stack_ss_ = segment;
    shell_stack_sp_ = 0xfffe;

    const bool mz = program.data.size() >= 0x1c
        && program.data[0] == 'M' && program.data[1] == 'Z';
    if (!mz) {
        const uint32_t entry_linear = (static_cast<uint32_t>(segment) << 4) + kDosEntryPoint;
        const auto clear_size = std::min<size_t>(0xff00, 1024 * 1024 - entry_linear);
        std::fill(mem + entry_linear, mem + entry_linear + clear_size, 0);
        const auto copy_size = std::min(program.data.size(), static_cast<size_t>(1024 * 1024 - entry_linear));
        std::memcpy(mem + entry_linear, program.data.data(), copy_size);
        return;
    }

    const uint16_t header_paragraphs = read_le16(program.data, 0x08);
    const uint16_t reloc_count = read_le16(program.data, 0x06);
    const uint16_t reloc_offset = read_le16(program.data, 0x18);
    const uint16_t initial_ss = read_le16(program.data, 0x0e);
    const uint16_t initial_sp = read_le16(program.data, 0x10);
    const uint16_t initial_ip = read_le16(program.data, 0x14);
    const uint16_t initial_cs = read_le16(program.data, 0x16);
    const size_t image_offset = static_cast<size_t>(header_paragraphs) * 16;
    if (image_offset >= program.data.size()) {
        return;
    }
    const uint16_t load_segment = static_cast<uint16_t>(segment + 0x10);
    const uint32_t load_linear = static_cast<uint32_t>(load_segment) << 4;
    const size_t image_size = std::min(program.data.size() - image_offset,
                                       static_cast<size_t>(1024 * 1024 - load_linear));
    std::memcpy(mem + load_linear, program.data.data() + image_offset, image_size);

    for (uint16_t i = 0; i < reloc_count; ++i) {
        const size_t entry = static_cast<size_t>(reloc_offset) + static_cast<size_t>(i) * 4;
        if (entry + 3 >= program.data.size()) {
            break;
        }
        const uint16_t off = read_le16(program.data, entry);
        const uint16_t seg = read_le16(program.data, entry + 2);
        const uint32_t linear = (static_cast<uint32_t>(load_segment + seg) << 4) + off;
        if (linear + 1 >= 1024 * 1024) {
            continue;
        }
        const uint16_t original = static_cast<uint16_t>(mem[linear] | (mem[linear + 1] << 8));
        const uint16_t relocated = static_cast<uint16_t>(original + load_segment);
        mem[linear] = static_cast<uint8_t>(relocated & 0xff);
        mem[linear + 1] = static_cast<uint8_t>(relocated >> 8);
    }

    shell_entry_cs_ = static_cast<uint16_t>(load_segment + initial_cs);
    shell_entry_ip_ = initial_ip;
    shell_stack_ss_ = static_cast<uint16_t>(load_segment + initial_ss);
    shell_stack_sp_ = initial_sp;
    if (trace_dos_) {
        std::fprintf(stderr,
                     "pc98dos mz-load psp=%04x load=%04x entry=%04x:%04x stack=%04x:%04x relocs=%u image=%zu\n",
                     segment, load_segment, shell_entry_cs_, shell_entry_ip_, shell_stack_ss_, shell_stack_sp_,
                     reloc_count, image_size);
    }
}

void Pc98DosDriver::setup_shell_psp(const std::string& command, uint16_t segment)
{
    if (!cpu_) {
        return;
    }
    auto* mem = cpu_->memory();
    if (segment > 0) {
        const uint32_t mcb_linear = static_cast<uint32_t>(segment - 1) << 4;
        mem[mcb_linear + 0x00] = 'M';
        mem[mcb_linear + 0x01] = static_cast<uint8_t>(segment & 0xff);
        mem[mcb_linear + 0x02] = static_cast<uint8_t>((segment >> 8) & 0xff);
        const uint16_t paragraphs = static_cast<uint16_t>(0x9fff - segment);
        mem[mcb_linear + 0x03] = static_cast<uint8_t>(paragraphs & 0xff);
        mem[mcb_linear + 0x04] = static_cast<uint8_t>((paragraphs >> 8) & 0xff);
        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos shell-mcb segment=%04x mcb=%04x size=%04x\n",
                         segment,
                         static_cast<uint16_t>(segment - 1),
                         paragraphs);
        }
    }
    const uint32_t psp_linear = static_cast<uint32_t>(segment) << 4;
    mem[psp_linear + 0x0000] = 0xcd;
    mem[psp_linear + 0x0001] = 0x20;
    std::fill(mem + psp_linear + 0x5c, mem + psp_linear + 0x100, 0);
    const auto token_end = command.find_first_of(" \t");
    const auto args_start = token_end == std::string::npos
        ? std::string::npos
        : command.find_first_not_of(" \t", token_end);
    std::string args = args_start == std::string::npos ? std::string() : command.substr(args_start);
    if (args.size() > 126) {
        args.resize(126);
    }
    mem[psp_linear + 0x80] = static_cast<uint8_t>(args.size());
    if (!args.empty()) {
        std::memcpy(mem + psp_linear + 0x81, args.data(), args.size());
    }
    mem[psp_linear + 0x81 + args.size()] = '\r';
}

void Pc98DosDriver::run_shell_program(const ShellProgram& program, uint16_t segment, int steps)
{
    if (trace_dos_) {
        std::fprintf(stderr, "pc98dos run-shell segment=%04x command=\"%s\"\n", segment, program.command.c_str());
    }

    // Reserve enough conventional-memory space for the PSP plus the loaded
    // image before the guest can issue INT 21h/AH=48 allocations.  The exact
    // resident size, when the program exits through AH=31, is applied below.
    const uint32_t image_bytes = static_cast<uint32_t>(program.data.size()) + 0x200u;
    const uint16_t image_paragraphs = static_cast<uint16_t>(
        std::min<uint32_t>(0x0fffu, std::max<uint32_t>(0x20u, (image_bytes + 15u) / 16u)));
    const uint32_t reserved_end = static_cast<uint32_t>(segment) + image_paragraphs + 1u;
    if (reserved_end < 0xa000u) {
        dos_alloc_segment_ = std::max<uint16_t>(dos_alloc_segment_, static_cast<uint16_t>(reserved_end));
    }
    current_shell_tsr_paragraphs_ = 0;

    load_shell_program(program, segment);
    setup_shell_psp(program.command, segment);
    if (trace_dos_) {
        const uint32_t psp = static_cast<uint32_t>(segment) << 4;
        const uint8_t length = read_memory_byte(psp + 0x80);
        std::fprintf(stderr, "pc98dos shell-tail len=%u text=\"", length);
        for (uint8_t i = 0; i < length && i < 127; ++i) {
            const char ch = static_cast<char>(read_memory_byte(psp + 0x81 + i));
            std::fputc((ch >= 0x20 && ch < 0x7f) ? ch : '.', stderr);
        }
        std::fprintf(stderr, "\"\n");
    }
    reset_cpu_context(segment);
    current_psp_segment_ = segment;
    cpu_->set_cs(shell_entry_cs_);
    cpu_->set_pc(shell_entry_ip_);
    cpu_->set_ss(shell_stack_ss_);
    cpu_->set_sp(shell_stack_sp_);
    // DOS starts EXE programs with DS/ES pointing at the PSP. COM programs use
    // the same segment for PSP and image, so this is correct for both.
    cpu_->set_ds(segment);
    cpu_->set_es(segment);
    const bool old_shell_async = shell_async_interrupts_;
    shell_async_interrupts_ = true;
    run_cpu_steps(steps);
    shell_async_interrupts_ = old_shell_async;
    const uint16_t kept_paragraphs = current_shell_tsr_paragraphs_ != 0
        ? current_shell_tsr_paragraphs_
        : image_paragraphs;
    const uint32_t shell_end = static_cast<uint32_t>(segment) + kept_paragraphs + 1u;
    if (shell_end < 0xa000u) {
        dos_alloc_segment_ = std::max<uint16_t>(dos_alloc_segment_, static_cast<uint16_t>(shell_end));
    }

    if (trace_dos_) {
        const auto print_vector = [this](uint8_t vector) {
            const uint32_t addr = static_cast<uint32_t>(vector) * 4;
            const uint16_t offset = static_cast<uint16_t>(read_memory_byte(addr)
                | (read_memory_byte(addr + 1) << 8));
            const uint16_t seg = static_cast<uint16_t>(read_memory_byte(addr + 2)
                | (read_memory_byte(addr + 3) << 8));
            std::fprintf(stderr, "pc98dos vector int=%02x is %04x:%04x\n", vector, seg, offset);
        };
        std::fprintf(stderr,
                     "pc98dos shell-done cs:ip=%04x:%04x unsupported=%u opcode=%02x reads=%u opn=%u hhd[1d65,1d66]=%02x,%02x\n",
                     cpu_->get_cs(),
                     cpu_->get_pc(),
                     cpu_->unsupported_count(),
                     cpu_->last_unsupported_opcode(),
                     debug_file_reads_,
                     debug_opna_writes_,
                     read_memory_byte((static_cast<uint32_t>(segment) << 4) + 0x1d65),
                     read_memory_byte((static_cast<uint32_t>(segment) << 4) + 0x1d66));
        print_vector(0x14);
        print_vector(0x08);
        print_vector(0x1c);
        print_vector(0x60);
        print_vector(0x62);
        print_vector(0x7f);
    }
    cpu_->clear_halted();
}

void Pc98DosDriver::call_shell_player_api(uint16_t ax, uint16_t ds, uint16_t dx)
{
    if (!cpu_) {
        return;
    }
    cpu_->clear_halted();
    cpu_->set_ax(ax);
    cpu_->set_ds(ds);
    cpu_->set_dx(dx);
    bridge_command_active_ = true;
    bridge_command_ = 0x00;
    bridge_argument_ = ax;
    // Shell bridge calls may synchronously parse sizeable song files before
    // returning to the helper.  In particular NC.COM scans every MTrk chunk
    // of large SMF-derived files here.  run_cpu_steps() already stops as soon
    // as the handler IRETs into the HLT return trampoline, so use a generous
    // safety ceiling rather than truncating a still-active resident API call.
    trigger_interrupt_vector(function_vector_, 5000000);
    bridge_command_active_ = false;
    cpu_->clear_unsupported_status();
    // Resident DOS music drivers are interrupt/API driven after installation.
    // Park the foreground CPU on the real HLT trampoline in segment 0000.
    // Async IRQ delivery saves/restores CS:IP, so parking on the former
    // 1000:xxxx API return address made the post-IRET CPU execute resident
    // song-buffer bytes as instructions.
    cpu_->set_cs(0x0000);
    cpu_->set_pc(kHaltOffset);
    cpu_->set_ds(kProgramSegment);
    cpu_->set_es(kProgramSegment);
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    cpu_->halt();
}

void Pc98DosDriver::load_hhd98_track()
{
    if (!cpu_ || selected_bgm_data_.empty()) {
        return;
    }

    const uint32_t int60 = 0x60 * 4;
    const uint16_t hhd_segment = static_cast<uint16_t>(read_memory_byte(int60 + 2)
        | (read_memory_byte(int60 + 3) << 8));
    if (hhd_segment == 0) {
        return;
    }

    const uint32_t hhd_base = static_cast<uint32_t>(hhd_segment) << 4;
    const auto set_voice_table_ptr = [this, hhd_base]() {
        const uint16_t voice_table = static_cast<uint16_t>(read_memory_byte(hhd_base + 0x1dc0)
            | (read_memory_byte(hhd_base + 0x1dc1) << 8));
        const uint16_t voice_table_ptr = static_cast<uint16_t>(0x1dbb + voice_table);
        write_memory_byte(hhd_base + 0x1d7a, static_cast<uint8_t>(voice_table_ptr & 0xff));
        write_memory_byte(hhd_base + 0x1d7b, static_cast<uint8_t>((voice_table_ptr >> 8) & 0xff));
    };

    const uint32_t int7f = 0x7f * 4;
    const uint16_t bridge_offset = static_cast<uint16_t>(read_memory_byte(int7f)
        | (read_memory_byte(int7f + 1) << 8));
    const uint16_t bridge_segment = static_cast<uint16_t>(read_memory_byte(int7f + 2)
        | (read_memory_byte(int7f + 3) << 8));

    if (bridge_offset != 0 || bridge_segment != 0) {
        selected_file_open_ = true;
        selected_file_offset_ = 0;
        trigger_interrupt_vector(0x7f, 5000000);
        set_voice_table_ptr();
    } else {
        cpu_->set_ax(0x0200);
        trigger_interrupt_vector(0x60, 5000000);

        cpu_->set_ax(0x0000);
        trigger_interrupt_vector(0x60, 5000000);

        uint16_t dest_segment = cpu_->get_ds();
        uint16_t dest_offset = cpu_->get_dx();
        if (dest_segment == 0 || dest_offset == 0x07e0) {
            dest_segment = hhd_segment;
            dest_offset = 0x1dbb;
        }

        const uint32_t dest = (static_cast<uint32_t>(dest_segment) << 4) + dest_offset;
        const size_t count = std::min(selected_bgm_data_.size(), static_cast<size_t>(1024 * 1024 - dest));
        for (size_t i = 0; i < count; ++i) {
            write_memory_byte(dest + static_cast<uint32_t>(i), selected_bgm_data_[i]);
        }
        selected_file_offset_ = count;
        ++debug_file_reads_;

        if (trace_dos_) {
            std::fprintf(stderr,
                         "pc98dos hhd-load bytes=%zu dst=%04x:%04x\n",
                         count,
                         dest_segment,
                         dest_offset);
        }

        write_memory_byte(hhd_base + 0x1684, 0xeb);
        write_memory_byte(hhd_base + 0x1685, 0x17);
        write_memory_byte(hhd_base + 0x1686, 0x03);
        write_memory_byte(hhd_base + 0x1687, 0x01);
        write_memory_byte(hhd_base + 0x1688, 0x76);
        write_memory_byte(hhd_base + 0x1689, 0x03);
        set_voice_table_ptr();
    }

    if (read_memory_byte(hhd_base + 0x1957) == 0 && read_memory_byte(hhd_base + 0x1958) == 0) {
        for (uint16_t offset = 0x1956; offset < 0x1956 + 0x033e; ++offset) {
            write_memory_byte(hhd_base + offset, 0);
        }
        const uint16_t channel_blocks[] = {
            0x1956, 0x19a9, 0x19fc,
            0x1a4f, 0x1aa2, 0x1af5,
            0x1b48, 0x1b9b, 0x1bee,
        };
        for (uint8_t i = 0; i < 9; ++i) {
            const uint16_t block = channel_blocks[i];
            const uint8_t channel_id = i < 6 ? static_cast<uint8_t>(i % 3) : static_cast<uint8_t>(i);
            write_memory_byte(hhd_base + block, channel_id);
            write_memory_byte(hhd_base + block + 0x06, 0x01);
            write_memory_byte(hhd_base + block + 0x08, 0x0a);
            write_memory_byte(hhd_base + block + 0x44, 0x01);
            write_memory_byte(hhd_base + block + 0x45, 0xc0);
            const uint16_t sequence_offset = static_cast<uint16_t>(read_memory_byte(hhd_base + 0x1dc2 + (i * 2))
                | (read_memory_byte(hhd_base + 0x1dc3 + (i * 2)) << 8));
            if (sequence_offset == 0) {
                continue;
            }
            const uint16_t sequence_ptr = static_cast<uint16_t>(0x1dbb + sequence_offset);
            write_memory_byte(hhd_base + block + 0x01, static_cast<uint8_t>(sequence_ptr & 0xff));
            write_memory_byte(hhd_base + block + 0x02, static_cast<uint8_t>((sequence_ptr >> 8) & 0xff));
            cpu_->set_ax(sequence_ptr);
            cpu_->set_di(block);
            trigger_near_subroutine(hhd_segment, 0x02dd, 500000);
        }
        write_memory_byte(hhd_base + 0x1d61, 0x00);
        write_memory_byte(hhd_base + 0x1d96, 0x00);
        write_memory_byte(hhd_base + 0x1d97, 0x00);
    }
    if (trace_dos_) {
        const uint32_t base = static_cast<uint32_t>(hhd_segment) << 4;
        const auto word_at = [this, base](uint16_t offset) {
            return static_cast<uint16_t>(read_memory_byte(base + offset)
                | (read_memory_byte(base + offset + 1) << 8));
        };
        std::fprintf(stderr,
                     "pc98dos hhd-ch pc-after=%04x:%04x ptrs=%04x,%04x,%04x,%04x,%04x,%04x tempo=%02x div=%02x\n",
                     cpu_->get_cs(),
                     cpu_->get_pc(),
                     word_at(0x1957),
                     word_at(0x19aa),
                     word_at(0x19fd),
                     word_at(0x1a50),
                     word_at(0x1aa3),
                     word_at(0x1af6),
                     read_memory_byte(base + 0x1dbd),
                     read_memory_byte(base + 0x1d61));
    }
    cpu_->clear_unsupported_status();
    cpu_->clear_halted();
    cpu_->set_cs(0x0000);
    cpu_->set_pc(kHaltOffset);
    cpu_->set_ds(kProgramSegment);
    cpu_->set_es(kProgramSegment);
    cpu_->set_ss(kProgramSegment);
    cpu_->set_sp(0xfffe);
    cpu_->halt();
}

void Pc98DosDriver::emit_trace_event(const std::string& json)
{
    if (!trace_pc98_ || !trace_file_.is_open()) {
        return;
    }
    if (trace_event_limit_ != 0 && trace_events_ >= trace_event_limit_) {
        return;
    }
    trace_file_ << json << '\n';
    ++trace_events_;
}

void Pc98DosDriver::trace_cpu_event(const char* type,
                                    uint8_t opcode,
                                    uint16_t from_cs,
                                    uint16_t from_ip,
                                    uint16_t to_cs,
                                    uint16_t to_ip)
{
    std::ostringstream event;
    event << "{\"type\":\"" << type
          << "\",\"frame\":" << rendered_frames_
          << ",\"step\":" << executed_cpu_steps_
          << ",\"opcode\":" << static_cast<unsigned>(opcode)
          << ",\"from_cs\":" << from_cs
          << ",\"from_ip\":" << from_ip
          << ",\"to_cs\":" << to_cs
          << ",\"to_ip\":" << to_ip;
    if (cpu_) {
        event << ",\"ax\":" << cpu_->get_ax()
              << ",\"bx\":" << cpu_->get_bx()
              << ",\"cx\":" << cpu_->get_cx()
              << ",\"dx\":" << cpu_->get_dx()
              << ",\"si\":" << cpu_->get_si()
              << ",\"di\":" << cpu_->get_di()
              << ",\"ds\":" << cpu_->get_ds()
              << ",\"es\":" << cpu_->get_es()
              << ",\"ss\":" << cpu_->get_ss()
              << ",\"sp\":" << cpu_->get_sp();
    }
    event << "}";
    emit_trace_event(event.str());
}

void Pc98DosDriver::trace_io_event(const char* type, uint16_t port, uint8_t value)
{
    std::ostringstream event;
    event << "{\"type\":\"" << type
          << "\",\"frame\":" << rendered_frames_
          << ",\"step\":" << executed_cpu_steps_
          << ",\"port\":" << port
          << ",\"value\":" << static_cast<unsigned>(value);
    if (cpu_) {
        event << ",\"cs\":" << cpu_->get_cs()
              << ",\"ip\":" << cpu_->get_pc()
              << ",\"ax\":" << cpu_->get_ax()
              << ",\"dx\":" << cpu_->get_dx();
    }
    if (port == 0x89 || port == 0x8a || port == 0x18a) {
        event << ",\"opna_bank\":0,\"opna_reg\":" << static_cast<unsigned>(current_opna_address_[0]);
    } else if (port == 0x8d || port == 0x8e || port == 0x18e) {
        event << ",\"opna_bank\":1,\"opna_reg\":" << static_cast<unsigned>(current_opna_address_[1]);
    }
    event << "}";
    emit_trace_event(event.str());
}

void Pc98DosDriver::trace_interrupt_event(uint8_t int_num)
{
    std::ostringstream event;
    event << "{\"type\":\"int\",\"frame\":" << rendered_frames_
          << ",\"step\":" << executed_cpu_steps_
          << ",\"int\":" << static_cast<unsigned>(int_num);
    if (cpu_) {
        event << ",\"cs\":" << cpu_->get_cs()
              << ",\"ip\":" << cpu_->get_pc()
              << ",\"ax\":" << cpu_->get_ax()
              << ",\"bx\":" << cpu_->get_bx()
              << ",\"cx\":" << cpu_->get_cx()
              << ",\"dx\":" << cpu_->get_dx()
              << ",\"ds\":" << cpu_->get_ds()
              << ",\"es\":" << cpu_->get_es();
    }
    event << "}";
    emit_trace_event(event.str());
}

void Pc98DosDriver::run_cpu_steps(int steps)
{
    if (!cpu_ || steps <= 0) {
        return;
    }

    int remaining = steps;
    while (remaining > 0 && !cpu_->is_halted()) {
        const bool advance_mpu_guest_time = shell_async_interrupts_
            || bridge_command_active_ || bridge_load_pending_;
        const int quantum = (advance_mpu_guest_time && !suppress_async_interrupts_)
            ? std::min(remaining, 20000)
            : remaining;
        const int executed = cpu_->execute(quantum);
        executed_cpu_steps_ += static_cast<uint64_t>(std::max(0, executed));
        remaining -= std::max(1, executed);
        pit_timer_tick();
        if (executed <= 0) {
            break;
        }
        if (advance_mpu_guest_time && mpu401_ && !suppress_async_interrupts_) {
            const double steps_per_second = static_cast<double>(sample_rate_)
                * static_cast<double>(std::max(1, clock_multiplier_));
            mpu401_->advance_cpu_time(executed, steps_per_second);
            service_mpu401_irq();
        }
        if (shell_async_interrupts_
            && !suppress_async_interrupts_
            && cpu_->get_interrupt_flag()
            && is_interrupt_vector_active(0x08)) {
            trigger_async_interrupt_vector(0x08, 64);
        }
    }
}

} // namespace hoot
