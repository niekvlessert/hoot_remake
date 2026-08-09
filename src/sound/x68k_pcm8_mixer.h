#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hoot {

// Stateful trap-#2 PCM8/PCM8A endpoint and eight-voice direct-block renderer.
// The mixer follows the documented PCM8A data formats while retaining the
// original Hoot PCM8 low-nibble-first ADPCM behavior. Array-chain commands are
// still diagnosed separately and are intentionally not accepted as playback.
class X68kPcm8Mixer {
public:
    static constexpr int kVoiceCount = 8;
    using MemoryReader = std::function<bool(uint32_t address, uint8_t& value)>;

    enum class Encoding : uint8_t {
        Adpcm,
        Pcm16,
        Pcm8,
        Unknown
    };

    enum class CommandKind : uint8_t {
        Unknown,
        Play,
        ArrayChain,
        LinkedArrayChain,
        SetChannelMode,
        QueryLength,
        QueryMode,
        QueryAddress,
        PauseChannel,
        ResumeChannel,
        StopAll,
        PauseAll,
        ResumeAll,
        Disable,
        Enable,
        SystemInfo,
        QuerySystemInfo,
        OperationMode,
        InterruptMask,
        ProtectResident,
        AllowResidentRelease
    };

    struct Mode {
        uint8_t volume = 0x08;
        uint8_t frequency = 0x04;
        uint8_t pan = 0x03;
        Encoding encoding = Encoding::Adpcm;
        uint32_t sample_rate = 15600;
    };

    struct Segment {
        uint32_t address = 0;
        uint32_t length = 0;
    };

    struct Voice {
        Mode mode;
        uint32_t address = 0;
        uint32_t length = 0;
        uint32_t remaining = 0;
        uint32_t byte_offset = 0;
        uint32_t source_index = 0;
        uint64_t phase_q32 = 0;
        int32_t signal = 0;
        int32_t previous_signal = 0;
        int32_t step = 0;
        bool active = false;
        bool channel_paused = false;
        bool adpcm_primed = false;
        std::vector<Segment> chain;
        size_t chain_index = 0;
    };

    struct CommandResult {
        CommandKind kind = CommandKind::Unknown;
        int channel = -1;
        int32_t return_value = -1;
        bool recognized = false;
        bool implemented = false;
        bool started = false;
        bool stopped = false;
    };

    struct Stats {
        uint64_t commands = 0;
        uint64_t starts = 0;
        uint64_t stops = 0;
        uint64_t mode_changes = 0;
        uint64_t queries = 0;
        uint64_t pauses = 0;
        uint64_t resumes = 0;
        uint64_t unimplemented = 0;
        uint64_t unknown = 0;
        uint64_t unsupported_channels = 0;
        uint64_t rendered_voice_frames = 0;
        uint64_t rendered_source_bytes = 0;
        uint64_t completed_voices = 0;
        uint64_t array_chain_starts = 0;
        uint64_t linked_chain_starts = 0;
        uint64_t chain_segments_advanced = 0;
        uint64_t memory_faults = 0;
        uint32_t last_d0 = 0;
        uint32_t last_d1 = 0;
        uint32_t last_d2 = 0;
        uint32_t last_a1 = 0;
        CommandKind last_kind = CommandKind::Unknown;
        int last_channel = -1;
    };

    void reset();
    // Stop active voices for a host-side track switch without fabricating a
    // guest PCM8 command or discarding accumulated diagnostics.
    void stop_playback();
    CommandResult command(uint32_t d0, uint32_t d1, uint32_t d2, uint32_t a1, const MemoryReader& memory_reader = {});

    // Adds PCM8 output to a signed 32-bit interleaved stereo accumulation
    // buffer. No clipping is performed here. The memory reader must reject
    // unmapped or device addresses rather than returning fabricated sample
    // bytes. Resampling is deterministic Q32 nearest/hold, matching old Hoot's
    // non-interpolating PCM8 path.
    void mix_s32(int32_t* interleaved_stereo,
                 int frames,
                 uint32_t output_sample_rate,
                 const MemoryReader& memory_reader,
                 double gain = 1.0);

    const Voice& voice(int channel) const;
    const Stats& stats() const { return stats_; }
    int active_voice_count() const;
    bool globally_paused() const { return globally_paused_; }
    bool enabled() const { return enabled_; }

    static const char* command_name(CommandKind kind);
    static const char* encoding_name(Encoding encoding);
    static std::string describe(const CommandResult& result,
                                uint32_t d0,
                                uint32_t d1,
                                uint32_t d2,
                                uint32_t a1);

private:
    struct DecodedFunction {
        CommandKind kind = CommandKind::Unknown;
        int channel = -1;
        bool recognized = false;
    };

    static DecodedFunction decode_function(uint16_t function);
    static bool decode_frequency(uint8_t code,
                                 Encoding& encoding,
                                 uint32_t& sample_rate);
    static bool valid_volume(uint8_t volume);
    static uint32_t compose_mode(const Mode& mode);
    static int32_t volume_q16(uint8_t volume);

    bool apply_mode(Voice& voice, uint32_t d1, bool allow_pan_zero_stop);
    CommandResult channel_command(const DecodedFunction& decoded,
                                  uint32_t d1,
                                  uint32_t d2,
                                  uint32_t a1,
                                  const MemoryReader& memory_reader);
    bool load_array_chain(Voice& voice, uint32_t table_address, uint32_t count,
                          const MemoryReader& memory_reader);
    bool load_linked_array_chain(Voice& voice, uint32_t table_address,
                                 const MemoryReader& memory_reader);
    bool start_chain(Voice& voice, const std::vector<Segment>& segments);
    bool advance_chain_segment(Voice& voice);
    bool fetch_adpcm(Voice& voice, const MemoryReader& memory_reader);
    bool read_pcm_sample(Voice& voice,
                         const MemoryReader& memory_reader,
                         int32_t& sample);
    void advance_pcm_voice(Voice& voice, uint32_t bytes_per_sample);
    void account_byte_offset(Voice& voice, uint32_t new_offset);
    void complete_voice(Voice& voice);
    void stop_voice(Voice& voice);
    void stop_all();
    void update_last(const CommandResult& result,
                     uint32_t d0,
                     uint32_t d1,
                     uint32_t d2,
                     uint32_t a1);

    std::array<Voice, kVoiceCount> voices_{};
    Stats stats_{};
    bool globally_paused_ = false;
    bool enabled_ = true;
    uint8_t system_channels_ = kVoiceCount;
    uint8_t system_work_units_ = 4;
    uint8_t system_min_volume_ = 0x40;
    uint8_t system_max_volume_ = 0xa0;
};

} // namespace hoot
