#include "sound/cm32p_midi_synth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <zlib.h>

namespace hoot {
namespace {

constexpr size_t kRomSize = 0x80000;
constexpr size_t kAddressSpace = 0x280000;
constexpr int kParts = 6;
constexpr int kVoices = 31; // the real sound chip has 32; firmware reserves voice 0
constexpr double kPcmReferenceRate = 32000.0;
constexpr double kPi = 3.14159265358979323846;

uint32_t bitswap19(uint32_t value, const std::array<int, 19>& src_bits)
{
    uint32_t out = 0;
    for (size_t i = 0; i < src_bits.size(); ++i) {
        const int out_bit = 18 - static_cast<int>(i);
        out |= ((value >> src_bits[i]) & 1u) << out_bit;
    }
    return out;
}

uint8_t bitswap8(uint8_t value, const std::array<int, 8>& src_bits)
{
    uint8_t out = 0;
    for (size_t i = 0; i < src_bits.size(); ++i) {
        const int out_bit = 7 - static_cast<int>(i);
        out = static_cast<uint8_t>(out | (((value >> src_bits[i]) & 1u) << out_bit));
    }
    return out;
}

// MAME's BSD-3-Clause CM-32P work (Valley Bell / MAMEdev contributors)
// documents the physical ROM address/data scrambling. Keep these small pure
// helpers local so the Hoot high-level synth can consume ordinary raw
// CM-32P/SN-U110 dumps without importing MAME device machinery. See
// md/LICENSES.md for attribution and redistribution notes.
uint32_t unscramble_addr(uint32_t offset)
{
    static constexpr std::array<int, 19> bits = {
        18,17,15,14,16,12,11,7,9,13,10,8,3,2,1,6,4,5,0
    };
    return bitswap19(offset, bits);
}

uint8_t unscramble_data(uint8_t data)
{
    static constexpr std::array<int, 8> bits = {1,2,7,3,5,0,4,6};
    return bitswap8(data, bits);
}

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    f.seekg(0, std::ios::end);
    const auto size = f.tellg();
    if (size <= 0) return {};
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) return {};
    return data;
}

uint32_t crc32_of(const std::vector<uint8_t>& data)
{
    return static_cast<uint32_t>(::crc32(0, data.data(), static_cast<uInt>(data.size())));
}

std::vector<std::filesystem::path> enumerate_files(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    if (std::filesystem::is_regular_file(root, ec)) {
        files.push_back(root);
        return files;
    }
    ec.clear();
    if (!std::filesystem::is_directory(root, ec)) return files;
    std::filesystem::recursive_directory_iterator it(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (it->is_regular_file(file_ec) && !file_ec) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::string lower_name(const std::filesystem::path& p)
{
    std::string s = p.filename().string();
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool path_exists(const char* env, std::string& out)
{
    if (const char* p = std::getenv(env); p != nullptr && p[0] != '\0') {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec) {
            out = p;
            return true;
        }
    }
    return false;
}

int16_t decode_delta(int8_t data)
{
    int16_t value = data;
    const int sign = value < 0 ? -1 : 1;
    int val = value < 0 ? -value : value;
    const int shift = val >> 4;
    val &= 0x0f;
    const int result = shift == 0 ? val : ((0x10 + val) << (shift - 1));
    return static_cast<int16_t>(result * sign);
}

uint32_t flat_roland_address(uint8_t a0, uint8_t a1, uint8_t a2)
{
    return (static_cast<uint32_t>(a0 & 0x7f) << 14)
         | (static_cast<uint32_t>(a1 & 0x7f) << 7)
         | static_cast<uint32_t>(a2 & 0x7f);
}

struct Patch {
    uint8_t tone_media = 0;
    uint8_t tone_number = 0;
    uint8_t key_shift = 12;  // 0..24 => -12..+12
    uint8_t fine_tune = 50;  // 0..100 => -50..+50 cents
    uint8_t bender_range = 2;
    uint8_t key_lower = 0;
    uint8_t key_upper = 127;
    uint8_t assign_mode = 0;
    uint8_t reverb = 1;
    uint8_t velocity_sens = 8;
    uint8_t env_attack = 96;
    uint8_t env_release = 72;
    uint8_t lfo_rate = 64;
    uint8_t lfo_auto_delay = 0;
    uint8_t lfo_auto_rise = 0;
    uint8_t lfo_auto_depth = 0;
    uint8_t lfo_manual_rise = 0;
    uint8_t lfo_manual_depth = 0;
    uint8_t detune_depth = 0;
    uint8_t pan = 64;        // CM-32P: 127=left, 0=right
    uint8_t output_level = 100;
};

struct Part {
    Patch patch;
    uint8_t channel = 10; // zero based: CM-32P defaults to MIDI 11..16
    uint8_t program = 0;
    uint8_t modulation = 0;
    uint8_t volume = 100;
    uint8_t expression = 100;
    uint8_t sustain = 0;
    uint8_t bend_lsb = 0;
    uint8_t bend_msb = 64;
    uint8_t rpn_lsb = 0xff;
    uint8_t rpn_msb = 0xff;
};

struct Tone {
    uint8_t type = 0;
    std::array<uint8_t, 11> primary_notes{};
    std::array<uint8_t, 12> primary_samples{};
    std::array<uint8_t, 11> secondary_notes{};
    std::array<uint8_t, 12> secondary_samples{};
};

struct Sample {
    uint32_t absolute_start = 0;
    uint32_t length = 0;
    uint32_t loop_length = 0;
    uint8_t loop_mode = 1;
    uint8_t reference_note = 60;
    bool valid = false;
};

struct Voice {
    bool active = false;
    bool releasing = false;
    bool sustained = false;
    int part = 0;
    uint8_t original_note = 0;
    uint8_t effective_note = 0;
    uint8_t sample_id = 0;
    bool card = false;
    double detune_cents = 0.0;
    double position = 0.0;
    double step = 1.0;
    int direction = 1;
    double envelope = 0.0;
    double attack_step = 1.0;
    double release_step = 1.0;
    double base_gain = 1.0;
    double lfo_phase = 0.0;
    uint64_t age = 0;
    std::shared_ptr<std::vector<int16_t>> decoded;
    Sample sample;
};

uint8_t clamp_u8(int v, int lo, int hi)
{
    return static_cast<uint8_t>(std::clamp(v, lo, hi));
}

} // namespace

struct Cm32pMidiSynth::Impl {
    int sample_rate = 44100;
    bool is_active = false;
    std::string rom_path;
    std::string card_model;
    std::vector<uint8_t> rom;
    bool card_loaded = false;
    std::array<Tone, 128> tones{};
    std::array<Tone, 128> card_tones{};
    std::array<Sample, 256> samples{};
    std::array<Sample, 256> card_samples{};
    std::array<Patch, 128> patch_memory{};
    std::array<Part, kParts> parts{};
    std::array<uint8_t, kParts> partial_reserve{{6,5,5,5,5,5}};
    std::array<Voice, kVoices> voices{};
    std::array<std::shared_ptr<std::vector<int16_t>>, 256> decoded_cache{};
    std::array<std::shared_ptr<std::vector<int16_t>>, 256> card_decoded_cache{};
    uint8_t master_tune = 64;
    uint8_t reverb_mode = 1;
    uint8_t reverb_time = 4;
    uint8_t reverb_level = 3;
    uint8_t master_volume = 100;
    uint64_t age_counter = 1;
    uint64_t note_on_count = 0;
    uint64_t sysex_write_count = 0;
    std::vector<double> reverb_l;
    std::vector<double> reverb_r;
    size_t reverb_pos = 0;

    void clear_voices()
    {
        for (auto& v : voices) v = Voice{};
    }

    static Patch default_patch(uint8_t program)
    {
        Patch p;
        p.tone_number = program;
        return p;
    }

    void reset_state()
    {
        clear_voices();
        for (size_t i = 0; i < patch_memory.size(); ++i) {
            patch_memory[i] = default_patch(static_cast<uint8_t>(i));
        }
        for (int i = 0; i < kParts; ++i) {
            parts[i] = Part{};
            parts[i].channel = static_cast<uint8_t>(10 + i);
            parts[i].program = 0;
            parts[i].patch = patch_memory[0];
        }
        partial_reserve = {{6,5,5,5,5,5}};
        master_tune = 64;
        reverb_mode = 1;
        reverb_time = 4;
        reverb_level = 3;
        master_volume = 100;
        reverb_pos = 0;
        std::fill(reverb_l.begin(), reverb_l.end(), 0.0);
        std::fill(reverb_r.begin(), reverb_r.end(), 0.0);
    }

    void configure_reverb()
    {
        if (sample_rate <= 0) return;
        static constexpr std::array<double, 4> base_ms{{72.0, 118.0, 91.0, 176.0}};
        const double scale = 0.75 + (static_cast<double>(reverb_time) / 7.0) * 0.8;
        const size_t frames = std::max<size_t>(32,
            static_cast<size_t>(sample_rate * base_ms[std::min<size_t>(reverb_mode, 3)] * scale / 1000.0));
        if (reverb_l.size() != frames) {
            reverb_l.assign(frames, 0.0);
            reverb_r.assign(frames, 0.0);
            reverb_pos = 0;
        }
    }

    bool read_and_descramble(const std::filesystem::path& p, size_t dst_base, std::string& error)
    {
        auto raw = read_file(p);
        if (raw.size() != kRomSize) {
            error = "CM-32P PCM ROM must be exactly 512 KiB: " + p.string();
            return false;
        }
        for (uint32_t src = 0; src < kRomSize; ++src) {
            rom[dst_base + unscramble_addr(src)] = unscramble_data(raw[src]);
        }
        return true;
    }

    bool locate_internal_roms(const std::filesystem::path& root,
                              std::array<std::filesystem::path, 3>& result,
                              std::string& error)
    {
        struct Candidate { std::filesystem::path path; uint32_t crc = 0; std::string name; };
        std::vector<Candidate> candidates;
        for (const auto& p : enumerate_files(root)) {
            auto raw = read_file(p);
            if (raw.size() != kRomSize) continue;
            candidates.push_back({p, crc32_of(raw), lower_name(p)});
        }
        if (candidates.size() < 3) {
            error = "CM-32P requires three 512 KiB internal PCM ROM dumps (IC18, IC19 and IC20)";
            return false;
        }

        constexpr std::array<uint32_t, 3> known_crc{{0x8e53b2a3u, 0xc8220761u, 0x733c4054u}};
        std::array<bool, 3> found{{false,false,false}};
        for (const auto& c : candidates) {
            for (size_t i = 0; i < known_crc.size(); ++i) {
                if (!found[i] && c.crc == known_crc[i]) {
                    result[i] = c.path;
                    found[i] = true;
                }
            }
        }
        if (found[0] && found[1] && found[2]) return true;

        // Support renamed/test dumps by explicit IC suffix or Roland mask-ROM number.
        const std::array<std::array<const char*, 3>, 3> tokens{{
            {{"ic18", "r15179970", "3b1"}},
            {{"ic19", "r15179971", "3b2"}},
            {{"ic20", "r15179972", "9d1"}},
        }};
        found = {{false,false,false}};
        for (const auto& c : candidates) {
            for (size_t i = 0; i < tokens.size(); ++i) {
                for (const char* token : tokens[i]) {
                    if (!found[i] && c.name.find(token) != std::string::npos) {
                        result[i] = c.path;
                        found[i] = true;
                        break;
                    }
                }
            }
        }
        if (found[0] && found[1] && found[2]) return true;

        error = "unable to identify CM-32P IC18/IC19/IC20 PCM ROMs; use standard filenames or verified dumps";
        return false;
    }

    static Tone parse_tone(const uint8_t* p)
    {
        Tone t;
        t.type = static_cast<uint8_t>(p[0x0a] & 0x87);
        std::copy_n(p + 0x10, 11, t.primary_notes.begin());
        std::copy_n(p + 0x1b, 12, t.primary_samples.begin());
        std::copy_n(p + 0x30, 11, t.secondary_notes.begin());
        std::copy_n(p + 0x3b, 12, t.secondary_samples.begin());
        return t;
    }

    Sample parse_sample(const uint8_t* p, bool table_from_card) const
    {
        Sample s;
        const uint32_t start = static_cast<uint32_t>(p[0])
            | (static_cast<uint32_t>(p[1]) << 8)
            | (static_cast<uint32_t>(p[2] & 0x07) << 16);
        const bool card = table_from_card || ((p[2] & 0x08) != 0);
        const uint8_t bank = static_cast<uint8_t>((p[2] >> 4) & 0x03);
        const uint32_t bank_base = card ? 0x080000u
            : bank == 0 ? 0x000000u
            : bank == 1 ? 0x100000u
            : bank == 2 ? 0x200000u
            : 0xffffffffu;
        s.length = static_cast<uint32_t>(p[3] | (static_cast<uint32_t>(p[4]) << 8)) + 1u;
        s.loop_length = static_cast<uint32_t>(p[5] | (static_cast<uint32_t>(p[6]) << 8));
        s.loop_mode = static_cast<uint8_t>((p[2] >> 6) & 0x03);
        s.reference_note = p[8] <= 127 ? p[8] : 60;
        if (bank_base != 0xffffffffu && start < kRomSize && s.length > 1 && start + s.length <= kRomSize) {
            s.absolute_start = bank_base + start;
            s.loop_length = std::min(s.loop_length, s.length);
            s.valid = s.absolute_start + s.length <= rom.size();
        }
        return s;
    }

    bool parse_tables(std::string& error)
    {
        if (rom.size() < kAddressSpace) {
            error = "CM-32P ROM address space is incomplete";
            return false;
        }
        for (size_t i = 0; i < samples.size(); ++i) {
            samples[i] = parse_sample(&rom[0x000100 + i * 0x0a], false);
        }
        for (size_t i = 0; i < tones.size(); ++i) {
            tones[i] = parse_tone(&rom[0x001000 + i * 0x50]);
        }
        if (card_loaded) {
            for (size_t i = 0; i < card_samples.size(); ++i) {
                card_samples[i] = parse_sample(&rom[0x080100 + i * 0x0a], true);
            }
            for (size_t i = 0; i < card_tones.size(); ++i) {
                card_tones[i] = parse_tone(&rom[0x081000 + i * 0x50]);
            }
        }

        size_t valid_samples = 0;
        for (const auto& s : samples) if (s.valid) ++valid_samples;
        size_t plausible_tones = 0;
        for (const auto& t : tones) {
            const uint8_t type = static_cast<uint8_t>(t.type & 0x07);
            if (type <= 4 && t.primary_samples[0] != 0xff) ++plausible_tones;
        }
        if (valid_samples < 16 || plausible_tones < 32) {
            error = "CM-32P PCM ROM tables are not plausible after descrambling; verify IC18/IC19/IC20 dumps";
            return false;
        }
        return true;
    }

    int select_sample_id(const std::array<uint8_t,11>& breaks,
                         const std::array<uint8_t,12>& ids,
                         uint8_t note) const
    {
        size_t idx = 0;
        while (idx < breaks.size() && breaks[idx] != 0xff && note > breaks[idx]) ++idx;
        if (idx >= ids.size()) idx = ids.size() - 1;
        if (ids[idx] != 0xff) return ids[idx];
        for (size_t distance = 1; distance < ids.size(); ++distance) {
            if (idx >= distance && ids[idx - distance] != 0xff) return ids[idx - distance];
            if (idx + distance < ids.size() && ids[idx + distance] != 0xff) return ids[idx + distance];
        }
        return -1;
    }

    std::shared_ptr<std::vector<int16_t>> decode_sample(uint8_t sample_id, bool card)
    {
        auto& cache = card ? card_decoded_cache[sample_id] : decoded_cache[sample_id];
        if (cache) return cache;
        const Sample& s = card ? card_samples[sample_id] : samples[sample_id];
        if (!s.valid) return {};
        auto pcm = std::make_shared<std::vector<int16_t>>();
        pcm->resize(s.length);
        int16_t accum = 0;
        for (uint32_t i = 0; i < s.length; ++i) {
            const int8_t raw = static_cast<int8_t>(rom[s.absolute_start + i]);
            accum = static_cast<int16_t>(std::clamp<int>(
                static_cast<int>(accum) + decode_delta(raw), -0x7ff, 0x7ff));
            (*pcm)[i] = static_cast<int16_t>(accum * 12);
        }
        cache = pcm;
        return cache;
    }

    int part_for_channel(uint8_t channel, int start_after = -1) const
    {
        for (int i = start_after + 1; i < kParts; ++i) {
            if (parts[i].channel <= 15 && parts[i].channel == channel) return i;
        }
        return -1;
    }

    uint8_t fold_note_to_key_range(const Patch& p, uint8_t note) const
    {
        int n = note;
        while (n < p.key_lower && n + 12 <= 127) n += 12;
        while (n > p.key_upper && n - 12 >= 0) n -= 12;
        return clamp_u8(n, p.key_lower, p.key_upper);
    }

    double bend_cents(const Part& part) const
    {
        const int bend = (static_cast<int>(part.bend_msb) << 7) | part.bend_lsb;
        const double norm = (bend - 8192) / 8192.0;
        return norm * static_cast<double>(part.patch.bender_range) * 100.0;
    }

    void update_voice_step(Voice& v)
    {
        const Part& part = parts[v.part];
        const int key_shift = static_cast<int>(part.patch.key_shift) - 12;
        const int fine = static_cast<int>(part.patch.fine_tune) - 50;
        // CM-32P system master tune maps values 0..127 to 432.1..457.6 Hz.
        // Convert that documented frequency directly to cents around A4=440 Hz.
        const double master_hz = 432.1 + (static_cast<double>(master_tune) * (25.5 / 127.0));
        const double master_cents = 1200.0 * std::log2(master_hz / 440.0);
        const double cents = (static_cast<int>(v.effective_note) + key_shift - static_cast<int>(v.sample.reference_note)) * 100.0
            + fine + master_cents + bend_cents(part) + v.detune_cents;
        v.step = std::pow(2.0, cents / 1200.0) * (kPcmReferenceRate / sample_rate);
        v.step = std::clamp(v.step, 0.002, 64.0);
    }

    double envelope_time(uint8_t rate, bool release) const
    {
        const double x = (127.0 - rate) / 127.0;
        const double max_t = release ? 8.0 : 5.0;
        return (release ? 0.015 : 0.003) + max_t * x * x * x;
    }

    Voice* allocate_voice(int part)
    {
        for (auto& v : voices) if (!v.active) return &v;

        // Respect the documented six-part reserve concept approximately: do
        // not steal a part down below its reserve while another part exceeds
        // its own reserve.  Within the eligible set, steal the oldest voice.
        std::array<int,kParts> counts{};
        for (const auto& v : voices) if (v.active) ++counts[v.part];
        Voice* victim = nullptr;
        for (auto& v : voices) {
            if (!v.active) continue;
            const bool above_reserve = counts[v.part] > partial_reserve[v.part];
            if (above_reserve && (victim == nullptr || v.age < victim->age)) victim = &v;
        }
        if (victim == nullptr) {
            for (auto& v : voices) {
                if (victim == nullptr || v.age < victim->age) victim = &v;
            }
        }
        (void)part;
        return victim;
    }

    bool start_one_voice(int part_index, uint8_t original_note, uint8_t effective_note,
                         uint8_t sample_id, bool card, uint8_t velocity,
                         double layer_gain, double detune_cents)
    {
        const Sample& s = card ? card_samples[sample_id] : samples[sample_id];
        if (!s.valid) return false;
        auto decoded = decode_sample(sample_id, card);
        if (!decoded || decoded->size() < 2) return false;
        Voice* voice = allocate_voice(part_index);
        if (voice == nullptr) return false;
        *voice = Voice{};
        voice->active = true;
        voice->part = part_index;
        voice->original_note = original_note;
        voice->effective_note = effective_note;
        voice->sample_id = sample_id;
        voice->card = card;
        voice->sample = s;
        voice->decoded = std::move(decoded);
        voice->position = 0.0;
        voice->direction = 1;
        voice->detune_cents = detune_cents;
        voice->age = age_counter++;
        const Part& part = parts[part_index];
        const double sens = part.patch.velocity_sens / 15.0;
        const double vel = velocity / 127.0;
        voice->base_gain = layer_gain * ((1.0 - sens) + sens * vel);
        voice->attack_step = 1.0 / std::max(1.0, envelope_time(part.patch.env_attack, false) * sample_rate);
        voice->release_step = 1.0 / std::max(1.0, envelope_time(part.patch.env_release, true) * sample_rate);
        update_voice_step(*voice);
        return true;
    }

    void note_on(int part_index, uint8_t note, uint8_t velocity)
    {
        Part& part = parts[part_index];
        const bool card = part.patch.tone_media != 0;
        if (card && !card_loaded) return;
        const auto& tone = (card ? card_tones : tones)[part.patch.tone_number & 0x7f];
        const uint8_t effective = fold_note_to_key_range(part.patch, note);
        const int primary = select_sample_id(tone.primary_notes, tone.primary_samples, effective);
        const int secondary = select_sample_id(tone.secondary_notes, tone.secondary_samples, effective);
        const uint8_t type = static_cast<uint8_t>(tone.type & 0x07);
        bool started = false;
        const double vel = velocity / 127.0;
        if (type == 0 || type >= 5) {
            if (primary >= 0) started |= start_one_voice(part_index, note, effective,
                static_cast<uint8_t>(primary), card, velocity, 1.0, 0.0);
        } else if (type == 1) { // dual
            if (primary >= 0) started |= start_one_voice(part_index, note, effective,
                static_cast<uint8_t>(primary), card, velocity, 0.72, 0.0);
            if (secondary >= 0) started |= start_one_voice(part_index, note, effective,
                static_cast<uint8_t>(secondary), card, velocity, 0.72, 0.0);
        } else if (type == 2) { // detune: same mapped sample, symmetric detuning
            const double depth = part.patch.detune_depth;
            if (primary >= 0) {
                started |= start_one_voice(part_index, note, effective,
                    static_cast<uint8_t>(primary), card, velocity, 0.72, -depth * 0.5);
                started |= start_one_voice(part_index, note, effective,
                    static_cast<uint8_t>(primary), card, velocity, 0.72, depth * 0.5);
            }
        } else if (type == 3) { // velocity mix
            if (primary >= 0) started |= start_one_voice(part_index, note, effective,
                static_cast<uint8_t>(primary), card, velocity, std::max(0.15, 1.0 - vel), 0.0);
            if (secondary >= 0) started |= start_one_voice(part_index, note, effective,
                static_cast<uint8_t>(secondary), card, velocity, std::max(0.15, vel), 0.0);
        } else if (type == 4) { // velocity switch
            const int chosen = velocity < 64 ? primary : secondary;
            if (chosen >= 0) started |= start_one_voice(part_index, note, effective,
                static_cast<uint8_t>(chosen), card, velocity, 1.0, 0.0);
        }
        if (started) ++note_on_count;
    }

    void release_note(int part_index, uint8_t note)
    {
        Part& part = parts[part_index];
        for (auto& v : voices) {
            if (!v.active || v.part != part_index || v.original_note != note) continue;
            if (part.sustain >= 64) {
                v.sustained = true;
            } else {
                v.releasing = true;
                v.sustained = false;
            }
        }
    }

    void release_part(int part_index, bool immediate)
    {
        for (auto& v : voices) {
            if (!v.active || v.part != part_index) continue;
            if (immediate) v.active = false;
            else {
                v.releasing = true;
                v.sustained = false;
            }
        }
    }

    void release_sustained(int part_index)
    {
        for (auto& v : voices) {
            if (v.active && v.part == part_index && v.sustained) {
                v.sustained = false;
                v.releasing = true;
            }
        }
    }

    void update_part_steps(int part_index)
    {
        for (auto& v : voices) if (v.active && v.part == part_index) update_voice_step(v);
    }

    void program_change(int part_index, uint8_t program)
    {
        Part& part = parts[part_index];
        part.program = program & 0x7f;
        const uint8_t pan = part.patch.pan;
        const uint8_t level = part.patch.output_level;
        part.patch = patch_memory[part.program];
        // Pan/output are temporary-area-only parameters and are not part of
        // the 0x13-byte patch memory record.
        part.patch.pan = pan;
        part.patch.output_level = level;
    }

    void reset_controllers(int part_index)
    {
        Part& p = parts[part_index];
        p.modulation = 0;
        p.expression = 100;
        p.sustain = 0;
        p.bend_lsb = 0;
        p.bend_msb = 64;
        p.rpn_lsb = p.rpn_msb = 0xff;
        release_sustained(part_index);
        update_part_steps(part_index);
    }

    void apply_patch_byte(Patch& p, uint32_t offset, uint8_t value, bool temporary)
    {
        switch (offset) {
        case 0x00: p.tone_media = std::min<uint8_t>(value, 1); break;
        case 0x01: p.tone_number = value & 0x7f; break;
        case 0x02: p.key_shift = std::min<uint8_t>(value, 24); break;
        case 0x03: p.fine_tune = std::min<uint8_t>(value, 100); break;
        case 0x04: p.bender_range = std::min<uint8_t>(value, 12); break;
        case 0x05: p.key_lower = value & 0x7f; break;
        case 0x06: p.key_upper = value & 0x7f; break;
        case 0x07: p.assign_mode = std::min<uint8_t>(value, 3); break;
        case 0x08: p.reverb = value ? 1 : 0; break;
        case 0x09: p.velocity_sens = std::min<uint8_t>(value, 15); break;
        case 0x0a: p.env_attack = value & 0x7f; break;
        case 0x0b: p.env_release = value & 0x7f; break;
        case 0x0c: p.lfo_rate = value & 0x7f; break;
        case 0x0d: p.lfo_auto_delay = std::min<uint8_t>(value, 15); break;
        case 0x0e: p.lfo_auto_rise = std::min<uint8_t>(value, 15); break;
        case 0x0f: p.lfo_auto_depth = std::min<uint8_t>(value, 15); break;
        case 0x10: p.lfo_manual_rise = std::min<uint8_t>(value, 15); break;
        case 0x11: p.lfo_manual_depth = std::min<uint8_t>(value, 15); break;
        case 0x12: p.detune_depth = std::min<uint8_t>(value, 50); break;
        case 0x13: if (temporary) p.pan = value & 0x7f; break;
        case 0x14: if (temporary) p.output_level = std::min<uint8_t>(value, 100); break;
        default: break;
        }
        if (p.key_lower > p.key_upper) std::swap(p.key_lower, p.key_upper);
    }

    void apply_system_byte(uint32_t offset, uint8_t value)
    {
        if (offset == 0x00) {
            master_tune = value & 0x7f;
            for (auto& v : voices) if (v.active) update_voice_step(v);
        } else if (offset == 0x01) {
            reverb_mode = std::min<uint8_t>(value, 3); configure_reverb();
        } else if (offset == 0x02) {
            reverb_time = std::min<uint8_t>(value, 7); configure_reverb();
        } else if (offset == 0x03) {
            reverb_level = std::min<uint8_t>(value, 7);
        } else if (offset >= 0x04 && offset <= 0x09) {
            partial_reserve[offset - 0x04] = std::min<uint8_t>(value, 31);
        } else if (offset >= 0x0a && offset <= 0x0f) {
            const int part = static_cast<int>(offset - 0x0a);
            release_part(part, true);
            parts[part].channel = value == 16 ? 0xff : std::min<uint8_t>(value, 15);
            reset_controllers(part);
        } else if (offset == 0x10) {
            master_volume = std::min<uint8_t>(value, 100);
        }
    }

    void apply_sysex_data(uint32_t address, const uint8_t* bytes, size_t count)
    {
        const uint32_t temp_base = flat_roland_address(0x50,0,0);
        const uint32_t memory_base = flat_roland_address(0x51,0,0);
        const uint32_t system_base = flat_roland_address(0x52,0,0);
        for (size_t i = 0; i < count; ++i, ++address) {
            if (address >= temp_base && address < temp_base + kParts * 0x15u) {
                const uint32_t rel = address - temp_base;
                const int part = static_cast<int>(rel / 0x15u);
                const uint32_t off = rel % 0x15u;
                apply_patch_byte(parts[part].patch, off, bytes[i] & 0x7f, true);
                if (off <= 0x04 || off == 0x12) update_part_steps(part);
                ++sysex_write_count;
            } else if (address >= memory_base && address < memory_base + 128u * 0x13u) {
                const uint32_t rel = address - memory_base;
                const size_t patch = rel / 0x13u;
                const uint32_t off = rel % 0x13u;
                apply_patch_byte(patch_memory[patch], off, bytes[i] & 0x7f, false);
                ++sysex_write_count;
            } else if (address >= system_base && address < system_base + 0x11u) {
                apply_system_byte(address - system_base, bytes[i] & 0x7f);
                ++sysex_write_count;
            }
        }
    }

    double read_voice_sample(Voice& v)
    {
        if (!v.active || !v.decoded || v.decoded->size() < 2) return 0.0;
        const auto& pcm = *v.decoded;
        double pos = v.position;
        if (pos < 0.0) pos = 0.0;
        const size_t i0 = std::min<size_t>(static_cast<size_t>(pos), pcm.size() - 1);
        const size_t i1 = std::min<size_t>(i0 + 1, pcm.size() - 1);
        const double frac = pos - std::floor(pos);
        const double sample = pcm[i0] + (pcm[i1] - pcm[i0]) * frac;

        v.position += v.step * v.direction;
        const double end = static_cast<double>(v.sample.length - 1);
        const double loop_start = static_cast<double>(v.sample.length - v.sample.loop_length);
        if (v.direction > 0 && v.position >= end) {
            if (v.sample.loop_mode == 0 && v.sample.loop_length > 1) {
                while (v.position >= end) v.position -= std::max(1.0, static_cast<double>(v.sample.loop_length));
                if (v.position < loop_start) v.position = loop_start;
            } else if (v.sample.loop_mode == 2 && v.sample.loop_length > 1) {
                v.position = end - (v.position - end);
                v.direction = -1;
            } else {
                v.active = false;
            }
        } else if (v.direction < 0 && v.position <= loop_start) {
            if (v.sample.loop_mode == 2 && v.sample.loop_length > 1) {
                v.position = loop_start + (loop_start - v.position);
                v.direction = 1;
            } else {
                v.active = false;
            }
        }
        return sample;
    }

    void render_frame(double& out_l, double& out_r)
    {
        double send_l = 0.0;
        double send_r = 0.0;
        for (auto& v : voices) {
            if (!v.active) continue;
            Part& part = parts[v.part];
            if (!v.releasing) {
                v.envelope = std::min(1.0, v.envelope + v.attack_step);
            } else {
                v.envelope -= v.release_step;
                if (v.envelope <= 0.0) {
                    v.active = false;
                    continue;
                }
            }

            // CM-32P modulation is pitch LFO. The exact firmware envelope is
            // not yet reverse engineered, but the documented rate/depth and
            // CC1 controls are honored with a bounded vibrato approximation.
            const double lfo_hz = 0.15 + (part.patch.lfo_rate / 127.0) * 7.85;
            v.lfo_phase += 2.0 * kPi * lfo_hz / sample_rate;
            if (v.lfo_phase > 2.0 * kPi) v.lfo_phase -= 2.0 * kPi;
            const double depth_cents = (part.modulation / 127.0) * (part.patch.lfo_manual_depth / 15.0) * 80.0
                                     + (part.patch.lfo_auto_depth / 15.0) * 20.0;
            const double vibrato = std::pow(2.0, std::sin(v.lfo_phase) * depth_cents / 1200.0);
            const double saved_step = v.step;
            v.step *= vibrato;
            const double raw = read_voice_sample(v);
            v.step = saved_step;
            if (!v.active && raw == 0.0) continue;

            const double part_gain = (part.volume / 127.0) * (part.expression / 127.0)
                                   * (part.patch.output_level / 100.0) * (master_volume / 100.0);
            const double level = raw * v.envelope * v.base_gain * part_gain;
            const double pan_norm = (127.0 - part.patch.pan) / 127.0; // 127 left -> angle 0
            const double left_gain = std::cos(pan_norm * (kPi * 0.5));
            const double right_gain = std::sin(pan_norm * (kPi * 0.5));
            const double l = level * left_gain;
            const double r = level * right_gain;
            out_l += l;
            out_r += r;
            if (part.patch.reverb) {
                send_l += l;
                send_r += r;
            }
        }

        if (!reverb_l.empty() && reverb_level != 0) {
            const double wet_l = reverb_l[reverb_pos];
            const double wet_r = reverb_r[reverb_pos];
            const double feedback = 0.20 + (reverb_time / 7.0) * 0.62;
            reverb_l[reverb_pos] = send_l * 0.28 + wet_r * feedback * 0.18;
            reverb_r[reverb_pos] = send_r * 0.28 + wet_l * feedback * 0.18;
            reverb_pos = (reverb_pos + 1) % reverb_l.size();
            const double wet_gain = (reverb_level / 7.0) * 0.38;
            out_l += wet_l * wet_gain;
            out_r += wet_r * wet_gain;
        }
    }
};

Cm32pMidiSynth::Cm32pMidiSynth(std::string card_model) : impl_(std::make_unique<Impl>())
{
    std::transform(card_model.begin(), card_model.end(), card_model.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    impl_->card_model = std::move(card_model);
}
Cm32pMidiSynth::~Cm32pMidiSynth() { close(); }

std::string Cm32pMidiSynth::find_default_rom_path()
{
    std::string configured;
    if (path_exists("HOOT_CM32P_ROM_PATH", configured)) return configured;
    if (path_exists("HOOT_MUNT_ROM_PATH", configured)) {
        const auto cm64 = std::filesystem::path(configured) / "cm32p";
        std::error_code ec;
        if (std::filesystem::exists(cm64, ec) && !ec) return cm64.string();
    }
    const char* candidates[] = {"roms/cm32p", "roms/cm64/cm32p", "roms/cm64", "roms"};
    for (const char* p : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && !ec) return p;
    }
    return {};
}

bool Cm32pMidiSynth::open(int sample_rate, const std::string& rom_path, std::string& error)
{
    close();
    if (sample_rate < 8000 || sample_rate > 384000) {
        error = "invalid CM-32P output sample rate";
        return false;
    }
    impl_->sample_rate = sample_rate;
    impl_->rom_path = rom_path.empty() ? find_default_rom_path() : rom_path;
    if (impl_->rom_path.empty()) {
        error = "no CM-32P PCM ROM path found; set midi.cm32p_rom_path or HOOT_CM32P_ROM_PATH";
        return false;
    }

    std::array<std::filesystem::path,3> internal;
    if (!impl_->locate_internal_roms(impl_->rom_path, internal, error)) return false;
    impl_->rom.assign(kAddressSpace, 0xff);
    if (!impl_->read_and_descramble(internal[0], 0x000000, error)
        || !impl_->read_and_descramble(internal[1], 0x100000, error)
        || !impl_->read_and_descramble(internal[2], 0x200000, error)) {
        close();
        return false;
    }

    impl_->card_loaded = false;
    const char* card_path = nullptr;
    if (impl_->card_model == "07" || impl_->card_model == "sn-u110-07") {
        card_path = std::getenv("HOOT_CM32P_CARD_ROM_07");
    } else if (impl_->card_model == "10" || impl_->card_model == "sn-u110-10") {
        card_path = std::getenv("HOOT_CM32P_CARD_ROM_10");
    }
    if (card_path == nullptr || card_path[0] == '\0') {
        card_path = std::getenv("HOOT_CM32P_CARD_ROM");
    }
    if (card_path != nullptr && card_path[0] != '\0') {
        std::error_code ec;
        if (std::filesystem::is_regular_file(card_path, ec) && !ec) {
            if (!impl_->read_and_descramble(card_path, 0x080000, error)) {
                close();
                return false;
            }
            impl_->card_loaded = true;
        }
    }

    if (!impl_->parse_tables(error)) {
        close();
        return false;
    }
    impl_->decoded_cache = {};
    impl_->card_decoded_cache = {};
    impl_->configure_reverb();
    impl_->reset_state();
    impl_->is_active = true;
    return true;
}

void Cm32pMidiSynth::close()
{
    if (!impl_) return;
    impl_->is_active = false;
    impl_->clear_voices();
    impl_->rom.clear();
    impl_->decoded_cache = {};
    impl_->card_decoded_cache = {};
    impl_->reverb_l.clear();
    impl_->reverb_r.clear();
    impl_->rom_path.clear();
    impl_->card_loaded = false;
}

void Cm32pMidiSynth::reset()
{
    if (!impl_->is_active) return;
    impl_->reset_state();
}

bool Cm32pMidiSynth::active() const { return impl_->is_active; }
const char* Cm32pMidiSynth::backend_name() const { return "cm32p"; }
const std::string& Cm32pMidiSynth::soundfont_path() const { return impl_->rom_path; }

void Cm32pMidiSynth::short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size)
{
    if (!impl_->is_active || status < 0x80 || status >= 0xf0 || size < 2) return;
    const uint8_t type = status & 0xf0;
    const uint8_t channel = status & 0x0f;
    int part = -1;
    while ((part = impl_->part_for_channel(channel, part)) >= 0) {
        Part& p = impl_->parts[part];
        if (type == 0x80 && size >= 3) {
            impl_->release_note(part, data1 & 0x7f);
        } else if (type == 0x90 && size >= 3) {
            if ((data2 & 0x7f) == 0) impl_->release_note(part, data1 & 0x7f);
            else impl_->note_on(part, data1 & 0x7f, data2 & 0x7f);
        } else if (type == 0xb0 && size >= 3) {
            const uint8_t cc = data1 & 0x7f;
            const uint8_t value = data2 & 0x7f;
            switch (cc) {
            case 1: p.modulation = value; break;
            case 6:
                if (p.rpn_msb == 0 && p.rpn_lsb == 0) {
                    p.patch.bender_range = std::min<uint8_t>(value, 12);
                    impl_->update_part_steps(part);
                }
                break;
            case 7: p.volume = value; break;
            case 10: p.patch.pan = value; break;
            case 11: p.expression = value; break;
            case 64:
                if (p.sustain >= 64 && value < 64) impl_->release_sustained(part);
                p.sustain = value;
                break;
            case 100: p.rpn_lsb = value; break;
            case 101: p.rpn_msb = value; break;
            case 120: impl_->release_part(part, true); break;
            case 121: impl_->reset_controllers(part); break;
            case 123: case 124: case 125: case 126: case 127:
                impl_->release_part(part, false); break;
            default: break;
            }
        } else if (type == 0xc0) {
            impl_->program_change(part, data1 & 0x7f);
        } else if (type == 0xe0 && size >= 3) {
            p.bend_lsb = data1 & 0x7f;
            p.bend_msb = data2 & 0x7f;
            impl_->update_part_steps(part);
        }
    }
}

void Cm32pMidiSynth::sysex(const std::vector<uint8_t>& data)
{
    if (!impl_->is_active || data.size() < 9) return;
    // Roland DT1: 41 device 16 12 aa aa aa data... checksum
    if (data[0] != 0x41 || data[2] != 0x16 || data[3] != 0x12) return;
    uint32_t sum = 0;
    for (size_t i = 4; i < data.size(); ++i) sum += data[i] & 0x7f;
    if ((sum & 0x7f) != 0) return;
    const uint8_t a0 = data[4] & 0x7f;
    if (a0 == 0x7f) {
        impl_->reset_state();
        ++impl_->sysex_write_count;
        return;
    }
    const uint32_t address = flat_roland_address(a0, data[5], data[6]);
    impl_->apply_sysex_data(address, data.data() + 7, data.size() - 8);
}

int Cm32pMidiSynth::render_s16(int16_t* interleaved_stereo, int frames)
{
    if (!impl_->is_active || interleaved_stereo == nullptr || frames <= 0) return 0;
    for (int i = 0; i < frames; ++i) {
        double l = 0.0, r = 0.0;
        impl_->render_frame(l, r);
        const long li = std::lround(l);
        const long ri = std::lround(r);
        interleaved_stereo[i * 2] = static_cast<int16_t>(std::clamp<long>(li, -32768, 32767));
        interleaved_stereo[i * 2 + 1] = static_cast<int16_t>(std::clamp<long>(ri, -32768, 32767));
    }
    return frames;
}

uint64_t Cm32pMidiSynth::debug_note_on_count() const { return impl_->note_on_count; }
uint64_t Cm32pMidiSynth::debug_sysex_write_count() const { return impl_->sysex_write_count; }
size_t Cm32pMidiSynth::debug_active_voice_count() const
{
    size_t count = 0;
    for (const auto& v : impl_->voices) if (v.active) ++count;
    return count;
}

bool Cm32pMidiSynth::card_requested() const
{
    return impl_ && !impl_->card_model.empty();
}

bool Cm32pMidiSynth::card_loaded() const
{
    return impl_ && impl_->card_loaded;
}

const std::string& Cm32pMidiSynth::card_model() const
{
    static const std::string empty;
    return impl_ ? impl_->card_model : empty;
}

} // namespace hoot
