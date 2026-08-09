#include "sound/x68k_pcm8_mixer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace hoot {
namespace {

constexpr int32_t kPcm8Error = -1;
constexpr uint64_t kPhaseOne = uint64_t{1} << 32;
constexpr int kAdpcmIndexShift[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

int32_t clamp_i32(int64_t value)
{
    return static_cast<int32_t>(std::clamp<int64_t>(
        value, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
}

int adpcm_difference(int step, uint8_t nibble)
{
    static const std::array<int, 49 * 16> table = [] {
        std::array<int, 49 * 16> values{};
        for (int table_step = 0; table_step <= 48; ++table_step) {
            const int step_value = static_cast<int>(std::floor(
                16.0 * std::pow(11.0 / 10.0, static_cast<double>(table_step))));
            for (int table_nibble = 0; table_nibble < 16; ++table_nibble) {
                int magnitude = step_value / 8;
                if ((table_nibble & 0x04) != 0) magnitude += step_value;
                if ((table_nibble & 0x02) != 0) magnitude += step_value / 2;
                if ((table_nibble & 0x01) != 0) magnitude += step_value / 4;
                values[static_cast<size_t>(table_step) * 16u
                    + static_cast<size_t>(table_nibble)] =
                    (table_nibble & 0x08) != 0 ? -magnitude : magnitude;
            }
        }
        return values;
    }();
    return table[static_cast<size_t>(step) * 16u + (nibble & 0x0fu)];
}

bool is_channel_function(X68kPcm8Mixer::CommandKind kind)
{
    using Kind = X68kPcm8Mixer::CommandKind;
    switch (kind) {
    case Kind::Play:
    case Kind::ArrayChain:
    case Kind::LinkedArrayChain:
    case Kind::SetChannelMode:
    case Kind::QueryLength:
    case Kind::QueryMode:
    case Kind::QueryAddress:
    case Kind::PauseChannel:
    case Kind::ResumeChannel:
        return true;
    default:
        return false;
    }
}

} // namespace

void X68kPcm8Mixer::reset()
{
    voices_.fill(Voice{});
    stats_ = Stats{};
    globally_paused_ = false;
    enabled_ = true;
    system_channels_ = kVoiceCount;
    system_work_units_ = 4;
    system_min_volume_ = 0x40;
    system_max_volume_ = 0xa0;
}

void X68kPcm8Mixer::stop_playback()
{
    stop_all();
    globally_paused_ = false;
}

const X68kPcm8Mixer::Voice& X68kPcm8Mixer::voice(int channel) const
{
    static const Voice empty{};
    return channel >= 0 && channel < kVoiceCount
        ? voices_[static_cast<size_t>(channel)]
        : empty;
}

int X68kPcm8Mixer::active_voice_count() const
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(),
        [](const Voice& voice) { return voice.active; }));
}

X68kPcm8Mixer::DecodedFunction X68kPcm8Mixer::decode_function(uint16_t function)
{
    using Kind = CommandKind;
    switch (function) {
    case 0x0100: return {Kind::StopAll, -1, true};
    case 0x0101: return {Kind::PauseAll, -1, true};
    case 0x0102: return {Kind::ResumeAll, -1, true};
    case 0x0103: return {Kind::Disable, -1, true};
    case 0x0104: return {Kind::Enable, -1, true};
    case 0x01f8: return {Kind::SystemInfo, -1, true};
    case 0x01f9: return {Kind::QuerySystemInfo, -1, true};
    case 0x01fa: return {Kind::OperationMode, -1, true};
    case 0x01fb: return {Kind::InterruptMask, -1, true};
    case 0x01fe: return {Kind::ProtectResident, -1, true};
    case 0x01ff: return {Kind::AllowResidentRelease, -1, true};
    default: break;
    }

    const uint16_t legacy_family = function & 0xfff0u;
    const int legacy_channel = function & 0x000fu;
    switch (legacy_family) {
    case 0x0000: return {Kind::Play, legacy_channel, true};
    case 0x0010: return {Kind::ArrayChain, legacy_channel, true};
    case 0x0020: return {Kind::LinkedArrayChain, legacy_channel, true};
    case 0x0070: return {Kind::SetChannelMode, legacy_channel, true};
    case 0x0080: return {Kind::QueryLength, legacy_channel, true};
    case 0x0090: return {Kind::QueryMode, legacy_channel, true};
    case 0x00a0: return {Kind::QueryAddress, legacy_channel, true};
    case 0x00b0: return {Kind::PauseChannel, legacy_channel, true};
    case 0x00c0: return {Kind::ResumeChannel, legacy_channel, true};
    default: break;
    }

    const uint16_t extended_family = function & 0xff00u;
    const int extended_channel = function & 0x00ffu;
    switch (extended_family) {
    case 0x1000: return {Kind::Play, extended_channel, true};
    case 0x1100: return {Kind::ArrayChain, extended_channel, true};
    case 0x1200: return {Kind::LinkedArrayChain, extended_channel, true};
    case 0x1700: return {Kind::SetChannelMode, extended_channel, true};
    case 0x1800: return {Kind::QueryLength, extended_channel, true};
    case 0x1900: return {Kind::QueryMode, extended_channel, true};
    case 0x1a00: return {Kind::QueryAddress, extended_channel, true};
    case 0x1b00: return {Kind::PauseChannel, extended_channel, true};
    case 0x1c00: return {Kind::ResumeChannel, extended_channel, true};
    default: return {};
    }
}

bool X68kPcm8Mixer::decode_frequency(uint8_t code,
                                     Encoding& encoding,
                                     uint32_t& sample_rate)
{
    struct ModeCode {
        uint8_t code;
        Encoding encoding;
        uint32_t rate;
    };
    static constexpr ModeCode modes[] = {
        {0x00, Encoding::Adpcm, 3900}, {0x30, Encoding::Adpcm, 3900},
        {0x01, Encoding::Adpcm, 5200}, {0x31, Encoding::Adpcm, 5200},
        {0x02, Encoding::Adpcm, 7800}, {0x32, Encoding::Adpcm, 7800},
        {0x03, Encoding::Adpcm, 10400}, {0x33, Encoding::Adpcm, 10400},
        {0x04, Encoding::Adpcm, 15600}, {0x34, Encoding::Adpcm, 15600},
        {0x07, Encoding::Adpcm, 20800}, {0x35, Encoding::Adpcm, 20800},
        {0x0a, Encoding::Adpcm, 31200}, {0x36, Encoding::Adpcm, 31200},
        {0x10, Encoding::Pcm16, 3900}, {0x11, Encoding::Pcm16, 5200},
        {0x12, Encoding::Pcm16, 7800}, {0x13, Encoding::Pcm16, 10400},
        {0x05, Encoding::Pcm16, 15600}, {0x14, Encoding::Pcm16, 15600},
        {0x08, Encoding::Pcm16, 20800}, {0x15, Encoding::Pcm16, 20800},
        {0x0b, Encoding::Pcm16, 31200}, {0x16, Encoding::Pcm16, 31200},
        {0x20, Encoding::Pcm8, 3900}, {0x21, Encoding::Pcm8, 5200},
        {0x22, Encoding::Pcm8, 7800}, {0x23, Encoding::Pcm8, 10400},
        {0x06, Encoding::Pcm8, 15600}, {0x24, Encoding::Pcm8, 15600},
        {0x09, Encoding::Pcm8, 20800}, {0x25, Encoding::Pcm8, 20800},
        {0x0c, Encoding::Pcm8, 31200}, {0x26, Encoding::Pcm8, 31200},
    };
    const auto found = std::find_if(std::begin(modes), std::end(modes),
        [code](const ModeCode& mode) { return mode.code == code; });
    if (found == std::end(modes)) {
        return false;
    }
    encoding = found->encoding;
    sample_rate = found->rate;
    return true;
}

bool X68kPcm8Mixer::valid_volume(uint8_t volume)
{
    return volume <= 0x0f || (volume >= 0x40 && volume <= 0xa0);
}

uint32_t X68kPcm8Mixer::compose_mode(const Mode& mode)
{
    return (static_cast<uint32_t>(mode.volume) << 16)
        | (static_cast<uint32_t>(mode.frequency) << 8)
        | mode.pan;
}

bool X68kPcm8Mixer::apply_mode(Voice& voice, uint32_t d1, bool allow_pan_zero_stop)
{
    bool changed = false;
    const uint8_t volume = static_cast<uint8_t>(d1 >> 16);
    const uint8_t frequency = static_cast<uint8_t>(d1 >> 8);
    const uint8_t pan = static_cast<uint8_t>(d1);

    if (volume != 0xff && valid_volume(volume)) {
        voice.mode.volume = volume;
        changed = true;
    }
    if (frequency != 0xff) {
        Encoding encoding = Encoding::Unknown;
        uint32_t sample_rate = 0;
        if (decode_frequency(frequency, encoding, sample_rate)) {
            voice.mode.frequency = frequency;
            voice.mode.encoding = encoding;
            voice.mode.sample_rate = sample_rate;
            changed = true;
        }
    }
    if (pan != 0xff && pan >= 1 && pan <= 3) {
        voice.mode.pan = pan;
        changed = true;
    } else if (allow_pan_zero_stop && pan == 0) {
        stop_voice(voice);
    }
    if (changed) {
        ++stats_.mode_changes;
    }
    return changed;
}

void X68kPcm8Mixer::stop_voice(Voice& voice)
{
    voice.active = false;
    voice.channel_paused = false;
    voice.remaining = 0;
    voice.byte_offset = 0;
    voice.source_index = 0;
    voice.phase_q32 = 0;
    voice.signal = 0;
    voice.previous_signal = 0;
    voice.step = 0;
    voice.adpcm_primed = false;
    voice.chain.clear();
    voice.chain_index = 0;
}

void X68kPcm8Mixer::stop_all()
{
    for (auto& voice : voices_) {
        stop_voice(voice);
    }
}

X68kPcm8Mixer::CommandResult X68kPcm8Mixer::channel_command(
    const DecodedFunction& decoded,
    uint32_t d1,
    uint32_t d2,
    uint32_t a1,
    const MemoryReader& memory_reader)
{
    CommandResult result;
    result.kind = decoded.kind;
    result.channel = decoded.channel;
    result.recognized = true;

    if (decoded.channel < 0 || decoded.channel >= kVoiceCount) {
        ++stats_.unsupported_channels;
        result.return_value = kPcm8Error;
        return result;
    }

    auto& voice = voices_[static_cast<size_t>(decoded.channel)];
    switch (decoded.kind) {
    case CommandKind::Play:
        result.implemented = true;
        if (static_cast<int32_t>(d2) < 0) {
            ++stats_.queries;
            result.return_value = voice.active ? static_cast<int32_t>(voice.remaining) : 0;
        } else if (d2 == 0) {
            const bool was_active = voice.active;
            stop_voice(voice);
            ++stats_.stops;
            result.stopped = was_active;
            result.return_value = 0;
        } else {
            apply_mode(voice, d1, true);
            if ((d1 & 0xffu) == 0) {
                ++stats_.stops;
                result.stopped = true;
                result.return_value = 0;
                break;
            }
            voice.chain.clear();
            voice.chain_index = 0;
            voice.address = a1 & 0x00ffffffu;
            voice.length = d2;
            voice.remaining = d2;
            voice.byte_offset = 0;
            voice.source_index = 0;
            voice.phase_q32 = 0;
            voice.signal = 0;
            voice.previous_signal = 0;
            voice.step = 0;
            voice.adpcm_primed = false;
            voice.active = enabled_;
            voice.channel_paused = false;
            globally_paused_ = false;
            ++stats_.starts;
            result.started = voice.active;
            result.return_value = voice.active ? 0 : kPcm8Error;
        }
        break;
    case CommandKind::SetChannelMode:
        result.implemented = true;
        apply_mode(voice, d1, true);
        result.return_value = 0;
        break;
    case CommandKind::QueryLength:
        result.implemented = true;
        ++stats_.queries;
        result.return_value = voice.active ? static_cast<int32_t>(voice.remaining) : 0;
        break;
    case CommandKind::QueryMode:
        result.implemented = true;
        ++stats_.queries;
        result.return_value = static_cast<int32_t>(compose_mode(voice.mode));
        break;
    case CommandKind::QueryAddress:
        result.implemented = true;
        ++stats_.queries;
        result.return_value = static_cast<int32_t>((voice.address + voice.byte_offset) & 0x00ffffffu);
        break;
    case CommandKind::PauseChannel:
        result.implemented = true;
        voice.channel_paused = true;
        ++stats_.pauses;
        result.return_value = 0;
        break;
    case CommandKind::ResumeChannel:
        result.implemented = true;
        voice.channel_paused = false;
        ++stats_.resumes;
        result.return_value = voice.active ? 0 : kPcm8Error;
        break;
    case CommandKind::ArrayChain:
    case CommandKind::LinkedArrayChain: {
        result.implemented = true;
        if (d2 == 0 && decoded.kind == CommandKind::ArrayChain) {
            const bool was_active = voice.active;
            stop_voice(voice);
            ++stats_.stops;
            result.stopped = was_active;
            result.return_value = 0;
            break;
        }
        apply_mode(voice, d1, true);
        if ((d1 & 0xffu) == 0) {
            ++stats_.stops;
            result.stopped = true;
            result.return_value = 0;
            break;
        }
        const bool ok = decoded.kind == CommandKind::ArrayChain
            ? load_array_chain(voice, a1 & 0x00ffffffu, d2, memory_reader)
            : load_linked_array_chain(voice, a1 & 0x00ffffffu, memory_reader);
        if (ok) {
            voice.active = enabled_;
            voice.channel_paused = false;
            globally_paused_ = false;
            ++stats_.starts;
            if (decoded.kind == CommandKind::ArrayChain) ++stats_.array_chain_starts;
            else ++stats_.linked_chain_starts;
            result.started = voice.active;
            result.return_value = voice.active ? 0 : kPcm8Error;
        } else {
            ++stats_.memory_faults;
            stop_voice(voice);
            result.return_value = kPcm8Error;
        }
        break;
    }
    default:
        break;
    }
    return result;
}

X68kPcm8Mixer::CommandResult X68kPcm8Mixer::command(
    uint32_t d0,
    uint32_t d1,
    uint32_t d2,
    uint32_t a1,
    const MemoryReader& memory_reader)
{
    ++stats_.commands;
    const auto decoded = decode_function(static_cast<uint16_t>(d0));
    CommandResult result;
    if (!decoded.recognized) {
        ++stats_.unknown;
        update_last(result, d0, d1, d2, a1);
        return result;
    }

    if (is_channel_function(decoded.kind)) {
        result = channel_command(decoded, d1, d2, a1, memory_reader);
        update_last(result, d0, d1, d2, a1);
        return result;
    }

    result.kind = decoded.kind;
    result.recognized = true;
    result.implemented = true;
    result.return_value = 0;
    switch (decoded.kind) {
    case CommandKind::StopAll:
        stop_all();
        ++stats_.stops;
        result.stopped = true;
        break;
    case CommandKind::PauseAll:
        globally_paused_ = true;
        ++stats_.pauses;
        break;
    case CommandKind::ResumeAll:
        if (!globally_paused_) {
            result.return_value = kPcm8Error;
        } else {
            globally_paused_ = false;
            ++stats_.resumes;
        }
        break;
    case CommandKind::Disable:
        stop_all();
        enabled_ = false;
        ++stats_.stops;
        result.stopped = true;
        break;
    case CommandKind::Enable:
        stop_all();
        enabled_ = true;
        globally_paused_ = false;
        break;
    case CommandKind::SystemInfo: {
        const uint32_t previous = (static_cast<uint32_t>(system_channels_) << 24)
            | (static_cast<uint32_t>(system_work_units_) << 16)
            | (static_cast<uint32_t>(system_max_volume_) << 8)
            | system_min_volume_;
        result.return_value = static_cast<int32_t>(previous);
        if (d1 != 0xffffffffu) {
            const uint8_t channels = static_cast<uint8_t>(d1 >> 24);
            const uint8_t work_units = static_cast<uint8_t>(d1 >> 16);
            const uint8_t max_volume = static_cast<uint8_t>(d1 >> 8);
            const uint8_t min_volume = static_cast<uint8_t>(d1);
            if (channels != 0xff && channels > 0) system_channels_ = channels;
            if (work_units != 0xff && work_units >= 1 && work_units <= 12) {
                system_work_units_ = work_units;
            }
            if (max_volume != 0xff && valid_volume(max_volume)) system_max_volume_ = max_volume;
            if (min_volume != 0xff && valid_volume(min_volume)) system_min_volume_ = min_volume;
        }
        ++stats_.queries;
        break;
    }
    case CommandKind::QuerySystemInfo:
        result.return_value = static_cast<int32_t>(
            (static_cast<uint32_t>(system_work_units_ * 12) << 16)
            | (static_cast<uint32_t>(system_max_volume_) << 8)
            | system_min_volume_);
        ++stats_.queries;
        break;
    case CommandKind::OperationMode:
        result.return_value = static_cast<int32_t>(
            (static_cast<uint32_t>(system_channels_) << 8)
            | static_cast<uint32_t>(active_voice_count()));
        ++stats_.queries;
        break;
    case CommandKind::InterruptMask:
        // The headless implementation does not alter host interrupt masks.
        // Return a non-negative neutral value so callers can continue.
        result.return_value = 0;
        ++stats_.queries;
        break;
    case CommandKind::ProtectResident:
    case CommandKind::AllowResidentRelease:
        // ZMSC brackets playback with PCM8 resident-protection calls. The
        // embedded resident cannot be unloaded by a headless guest process,
        // so both lifecycle calls are successful no-ops.
        result.return_value = 0;
        break;
    default:
        result.implemented = false;
        result.return_value = kPcm8Error;
        ++stats_.unknown;
        break;
    }
    update_last(result, d0, d1, d2, a1);
    return result;
}

namespace {

bool read_chain_be16(const X68kPcm8Mixer::MemoryReader& reader, uint32_t address, uint16_t& value)
{
    uint8_t hi = 0, lo = 0;
    if (!reader || !reader(address & 0x00ffffffu, hi)
        || !reader((address + 1u) & 0x00ffffffu, lo)) return false;
    value = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
    return true;
}

bool read_chain_be32(const X68kPcm8Mixer::MemoryReader& reader, uint32_t address, uint32_t& value)
{
    uint16_t hi = 0, lo = 0;
    if (!read_chain_be16(reader, address, hi) || !read_chain_be16(reader, address + 2u, lo)) return false;
    value = (static_cast<uint32_t>(hi) << 16) | lo;
    return true;
}

} // namespace

bool X68kPcm8Mixer::start_chain(Voice& voice, const std::vector<Segment>& segments)
{
    if (segments.empty()) return false;
    voice.chain = segments;
    voice.chain_index = 0;
    voice.address = segments[0].address & 0x00ffffffu;
    voice.length = segments[0].length;
    voice.remaining = voice.length;
    voice.byte_offset = 0;
    voice.source_index = 0;
    voice.phase_q32 = 0;
    voice.signal = 0;
    voice.previous_signal = 0;
    voice.step = 0;
    voice.adpcm_primed = false;
    return voice.length != 0;
}

bool X68kPcm8Mixer::load_array_chain(Voice& voice,
                                     uint32_t table_address,
                                     uint32_t count,
                                     const MemoryReader& memory_reader)
{
    if (!memory_reader || count == 0 || count > 65535u) return false;
    std::vector<Segment> segments;
    segments.reserve(std::min<uint32_t>(count, 4096u));
    uint32_t cursor = table_address & 0x00ffffffu;
    for (uint32_t index = 0; index < count; ++index, cursor = (cursor + 6u) & 0x00ffffffu) {
        uint32_t address = 0;
        uint16_t length = 0;
        if (!read_chain_be32(memory_reader, cursor, address)
            || !read_chain_be16(memory_reader, cursor + 4u, length)) return false;
        if (length != 0) segments.push_back({address & 0x00ffffffu, length});
    }
    return start_chain(voice, segments);
}

bool X68kPcm8Mixer::load_linked_array_chain(Voice& voice,
                                            uint32_t table_address,
                                            const MemoryReader& memory_reader)
{
    if (!memory_reader || table_address == 0) return false;
    std::vector<Segment> segments;
    segments.reserve(32);
    uint32_t block = table_address & 0x00ffffffu;
    std::vector<uint32_t> visited;
    visited.reserve(64);
    for (uint32_t guard = 0; guard < 65535u && block != 0; ++guard) {
        if (std::find(visited.begin(), visited.end(), block) != visited.end()) return false;
        visited.push_back(block);
        uint32_t address = 0;
        uint16_t length = 0;
        uint32_t next = 0;
        if (!read_chain_be32(memory_reader, block, address)
            || !read_chain_be16(memory_reader, block + 4u, length)
            || !read_chain_be32(memory_reader, block + 6u, next)) return false;
        if (length != 0) segments.push_back({address & 0x00ffffffu, length});
        if (next == 0) break;
        // PCM8A treats odd link pointers as invalid/end markers.
        if ((next & 1u) != 0) break;
        block = next & 0x00ffffffu;
    }
    return start_chain(voice, segments);
}

bool X68kPcm8Mixer::advance_chain_segment(Voice& voice)
{
    if (voice.chain.empty() || voice.chain_index + 1 >= voice.chain.size()) return false;
    ++voice.chain_index;
    const auto& segment = voice.chain[voice.chain_index];
    voice.address = segment.address & 0x00ffffffu;
    voice.length = segment.length;
    voice.remaining = segment.length;
    voice.byte_offset = 0;
    voice.source_index = 0;
    // Preserve ADPCM predictor/index across chained blocks, as PCM8A does;
    // PCM formats must prefetch the first sample of the new descriptor.
    if (voice.mode.encoding != Encoding::Adpcm) voice.adpcm_primed = false;
    ++stats_.chain_segments_advanced;
    return true;
}

int32_t X68kPcm8Mixer::volume_q16(uint8_t volume)
{
    double db = 0.0;
    if (volume <= 0x0f) {
        db = static_cast<double>(static_cast<int>(volume) - 8) * 2.0;
    } else if (volume >= 0x40 && volume <= 0xa0) {
        db = static_cast<double>(static_cast<int>(volume) - 0x80) * 0.8;
    } else {
        return 0;
    }
    return static_cast<int32_t>(std::lround(std::pow(10.0, db / 20.0) * 65536.0));
}

void X68kPcm8Mixer::account_byte_offset(Voice& voice, uint32_t new_offset)
{
    new_offset = std::min(new_offset, voice.length);
    if (new_offset > voice.byte_offset) {
        stats_.rendered_source_bytes += new_offset - voice.byte_offset;
    }
    voice.byte_offset = new_offset;
    voice.remaining = voice.length - new_offset;
}

void X68kPcm8Mixer::complete_voice(Voice& voice)
{
    if (voice.active && advance_chain_segment(voice)) {
        return;
    }
    if (voice.active) {
        ++stats_.completed_voices;
    }
    voice.active = false;
    voice.channel_paused = false;
    voice.remaining = 0;
    voice.byte_offset = voice.length;
    voice.phase_q32 = 0;
}

bool X68kPcm8Mixer::fetch_adpcm(Voice& voice, const MemoryReader& memory_reader)
{
    const uint64_t total_nibbles = static_cast<uint64_t>(voice.length) * 2u;
    if (voice.source_index >= total_nibbles) {
        complete_voice(voice);
        return false;
    }

    uint8_t packed = 0;
    const uint32_t byte_index = voice.source_index / 2u;
    if (!memory_reader((voice.address + byte_index) & 0x00ffffffu, packed)) {
        ++stats_.memory_faults;
        complete_voice(voice);
        return false;
    }

    // Original Hoot PCM8 explicitly selected low-nibble-first ADPCM.
    const uint8_t nibble = (voice.source_index & 1u) == 0
        ? static_cast<uint8_t>(packed & 0x0fu)
        : static_cast<uint8_t>((packed >> 4) & 0x0fu);
    voice.previous_signal = voice.signal;
    voice.signal = std::clamp(voice.signal + adpcm_difference(voice.step, nibble), -2048, 2047);
    voice.step = std::clamp(voice.step + kAdpcmIndexShift[nibble & 7u], 0, 48);
    ++voice.source_index;
    account_byte_offset(voice, voice.source_index / 2u);
    return true;
}

bool X68kPcm8Mixer::read_pcm_sample(Voice& voice,
                                    const MemoryReader& memory_reader,
                                    int32_t& sample)
{
    const uint32_t bytes_per_sample = voice.mode.encoding == Encoding::Pcm16 ? 2u : 1u;
    if (voice.byte_offset >= voice.length
        || bytes_per_sample > voice.length - voice.byte_offset) {
        complete_voice(voice);
        return false;
    }

    uint8_t high = 0;
    if (!memory_reader((voice.address + voice.byte_offset) & 0x00ffffffu, high)) {
        ++stats_.memory_faults;
        complete_voice(voice);
        return false;
    }
    if (voice.mode.encoding == Encoding::Pcm8) {
        sample = static_cast<int32_t>(static_cast<int8_t>(high)) * 256;
        return true;
    }

    uint8_t low = 0;
    if (!memory_reader((voice.address + voice.byte_offset + 1u) & 0x00ffffffu, low)) {
        ++stats_.memory_faults;
        complete_voice(voice);
        return false;
    }
    const uint16_t packed = static_cast<uint16_t>((static_cast<uint16_t>(high) << 8) | low);
    sample = static_cast<int16_t>(packed);
    return true;
}

void X68kPcm8Mixer::advance_pcm_voice(Voice& voice, uint32_t bytes_per_sample)
{
    account_byte_offset(voice, std::min(voice.length, voice.byte_offset + bytes_per_sample));
    ++voice.source_index;
    if (voice.byte_offset >= voice.length) {
        complete_voice(voice);
    }
}

void X68kPcm8Mixer::mix_s32(int32_t* interleaved_stereo,
                            int frames,
                            uint32_t output_sample_rate,
                            const MemoryReader& memory_reader,
                            double gain)
{
    if (interleaved_stereo == nullptr || frames <= 0 || output_sample_rate == 0
        || !memory_reader || globally_paused_ || !enabled_) {
        return;
    }

    const int64_t gain_q16 = std::llround(std::clamp(gain, 0.0, 16.0) * 65536.0);
    for (auto& voice : voices_) {
        if (!voice.active || voice.channel_paused || voice.mode.encoding == Encoding::Unknown
            || voice.mode.pan == 0 || voice.mode.sample_rate == 0) {
            continue;
        }

        const uint64_t phase_step = (static_cast<uint64_t>(voice.mode.sample_rate) << 32)
            / output_sample_rate;
        const int64_t volume = volume_q16(voice.mode.volume);
        const int64_t scale_q32 = volume * gain_q16;

        for (int frame = 0; frame < frames && voice.active; ++frame) {
            int32_t source_sample = 0;
            if (voice.mode.encoding == Encoding::Adpcm) {
                // Playback starts from silence and advances after the first
                // output frame, matching old Hoot's prefetch/prev-signal path.
                voice.adpcm_primed = true;
                source_sample = voice.signal * 16;
            } else {
                if (!voice.adpcm_primed) {
                    if (!read_pcm_sample(voice, memory_reader, voice.signal)) {
                        break;
                    }
                    voice.adpcm_primed = true;
                }
                source_sample = voice.signal;
            }

            const int32_t scaled = clamp_i32(
                (static_cast<int64_t>(source_sample) * scale_q32 + (int64_t{1} << 31)) >> 32);
            const size_t sample_index = static_cast<size_t>(frame) * 2u;
            if ((voice.mode.pan & 1u) != 0) {
                interleaved_stereo[sample_index] = clamp_i32(
                    static_cast<int64_t>(interleaved_stereo[sample_index]) + scaled);
            }
            if ((voice.mode.pan & 2u) != 0) {
                interleaved_stereo[sample_index + 1u] = clamp_i32(
                    static_cast<int64_t>(interleaved_stereo[sample_index + 1u]) + scaled);
            }
            ++stats_.rendered_voice_frames;

            voice.phase_q32 += phase_step;
            while (voice.active && voice.phase_q32 >= kPhaseOne) {
                voice.phase_q32 -= kPhaseOne;
                if (voice.mode.encoding == Encoding::Adpcm) {
                    if (!fetch_adpcm(voice, memory_reader)) {
                        break;
                    }
                } else {
                    const uint32_t bytes_per_sample = voice.mode.encoding == Encoding::Pcm16 ? 2u : 1u;
                    advance_pcm_voice(voice, bytes_per_sample);
                    if (!voice.active) {
                        break;
                    }
                    if (!read_pcm_sample(voice, memory_reader, voice.signal)) {
                        break;
                    }
                }
            }
        }
    }
}

void X68kPcm8Mixer::update_last(const CommandResult& result,
                                uint32_t d0,
                                uint32_t d1,
                                uint32_t d2,
                                uint32_t a1)
{
    stats_.last_d0 = d0;
    stats_.last_d1 = d1;
    stats_.last_d2 = d2;
    stats_.last_a1 = a1;
    stats_.last_kind = result.kind;
    stats_.last_channel = result.channel;
}

const char* X68kPcm8Mixer::command_name(CommandKind kind)
{
    switch (kind) {
    case CommandKind::Play: return "play";
    case CommandKind::ArrayChain: return "array-chain";
    case CommandKind::LinkedArrayChain: return "linked-array-chain";
    case CommandKind::SetChannelMode: return "set-channel-mode";
    case CommandKind::QueryLength: return "query-length";
    case CommandKind::QueryMode: return "query-mode";
    case CommandKind::QueryAddress: return "query-address";
    case CommandKind::PauseChannel: return "pause-channel";
    case CommandKind::ResumeChannel: return "resume-channel";
    case CommandKind::StopAll: return "stop-all";
    case CommandKind::PauseAll: return "pause-all";
    case CommandKind::ResumeAll: return "resume-all";
    case CommandKind::Disable: return "disable";
    case CommandKind::Enable: return "enable";
    case CommandKind::SystemInfo: return "system-info";
    case CommandKind::QuerySystemInfo: return "query-system-info";
    case CommandKind::OperationMode: return "operation-mode";
    case CommandKind::InterruptMask: return "interrupt-mask";
    case CommandKind::ProtectResident: return "protect-resident";
    case CommandKind::AllowResidentRelease: return "allow-resident-release";
    case CommandKind::Unknown: return "unknown";
    }
    return "unknown";
}

const char* X68kPcm8Mixer::encoding_name(Encoding encoding)
{
    switch (encoding) {
    case Encoding::Adpcm: return "adpcm";
    case Encoding::Pcm16: return "pcm16";
    case Encoding::Pcm8: return "pcm8";
    case Encoding::Unknown: return "unknown";
    }
    return "unknown";
}

std::string X68kPcm8Mixer::describe(const CommandResult& result,
                                    uint32_t d0,
                                    uint32_t d1,
                                    uint32_t d2,
                                    uint32_t a1)
{
    std::ostringstream out;
    out << command_name(result.kind);
    if (result.channel >= 0) {
        out << " ch=" << result.channel;
    }
    out << " d0=0x" << std::hex << std::setw(8) << std::setfill('0') << d0
        << " d1=0x" << std::setw(8) << d1
        << " d2=0x" << std::setw(8) << d2
        << " a1=0x" << std::setw(8) << a1
        << std::dec << " result=" << result.return_value
        << " implemented=" << (result.implemented ? 1 : 0);
    return out.str();
}

} // namespace hoot
