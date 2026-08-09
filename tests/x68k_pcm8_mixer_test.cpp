#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "sound/x68k_pcm8_mixer.h"

namespace {

using Mixer = hoot::X68kPcm8Mixer;

Mixer::MemoryReader vector_reader(const std::vector<uint8_t>& memory, uint32_t base)
{
    return [&memory, base](uint32_t address, uint8_t& value) {
        if (address < base || address - base >= memory.size()) {
            return false;
        }
        value = memory[address - base];
        return true;
    };
}

void test_commands()
{
    Mixer mixer;
    mixer.reset();

    // Legacy PCM8 normal output: channel 3, original volume, 15.6 kHz ADPCM,
    // centered, 0x1234 bytes from guest address 0x070000.
    auto result = mixer.command(0x0003, 0x00080403, 0x1234, 0x070000);
    assert(result.recognized && result.implemented && result.started);
    assert(result.return_value == 0 && result.channel == 3);
    assert(mixer.active_voice_count() == 1);
    const auto& voice3 = mixer.voice(3);
    assert(voice3.active && voice3.address == 0x070000 && voice3.length == 0x1234);
    assert(voice3.mode.encoding == Mixer::Encoding::Adpcm);
    assert(voice3.mode.sample_rate == 15600 && voice3.mode.pan == 3);

    // The extended PCM8A form uses an eight-bit channel field.
    result = mixer.command(0x1004, 0x00082001, 0x200, 0x080000);
    assert(result.started && result.channel == 4);
    assert(mixer.voice(4).mode.encoding == Mixer::Encoding::Pcm8);
    assert(mixer.voice(4).mode.sample_rate == 3900);
    assert(mixer.voice(4).mode.pan == 1);

    // Mode changes preserve bytes marked 0xff and accept documented 16-bit
    // PCM codes. Pan zero stops the channel without replacing the old pan.
    result = mixer.command(0x0073, 0x00ff1502, 0, 0);
    assert(result.recognized && result.implemented);
    assert(mixer.voice(3).mode.encoding == Mixer::Encoding::Pcm16);
    assert(mixer.voice(3).mode.sample_rate == 20800);
    assert(mixer.voice(3).mode.pan == 2);
    result = mixer.command(0x0073, 0x00ffff00, 0, 0);
    assert(!mixer.voice(3).active);
    assert(mixer.voice(3).mode.pan == 2);

    result = mixer.command(0x1804, 0, 0, 0);
    assert(result.return_value == 0x200);
    result = mixer.command(0x1904, 0, 0, 0);
    assert(static_cast<uint32_t>(result.return_value) == 0x00082001u);
    result = mixer.command(0x1a04, 0, 0, 0);
    assert(static_cast<uint32_t>(result.return_value) == 0x080000u);

    // D2=0 is a stop and must not alter the mode bytes.
    result = mixer.command(0x1004, 0x00000a03, 0, 0);
    assert(result.recognized && result.implemented);
    assert(!mixer.voice(4).active);
    assert(mixer.voice(4).mode.frequency == 0x20);

    // Chain calls need guest memory and reject a missing descriptor reader.
    result = mixer.command(0x0010, 0x00080603, 1, 0x1000);
    assert(result.recognized && result.implemented && result.return_value == -1);

    // The direct renderer deliberately exposes eight voices.
    result = mixer.command(0x1008, 0x00080403, 1, 0x1000);
    assert(result.recognized && !result.implemented && result.return_value == -1);

    result = mixer.command(0xdead, 0, 0, 0);
    assert(!result.recognized && result.return_value == -1);

    result = mixer.command(0x0101, 0, 0, 0);
    assert(result.implemented && mixer.globally_paused());
    result = mixer.command(0x0102, 0, 0, 0);
    assert(result.return_value == 0 && !mixer.globally_paused());
    result = mixer.command(0x0103, 0, 0, 0);
    assert(!mixer.enabled() && mixer.active_voice_count() == 0);
    result = mixer.command(0x0104, 0, 0, 0);
    assert(mixer.enabled());
    result = mixer.command(0x01fe, 0, 0, 0);
    assert(result.recognized && result.implemented && result.return_value == 0);
    assert(result.kind == Mixer::CommandKind::ProtectResident);
    result = mixer.command(0x01ff, 0, 0, 0);
    assert(result.recognized && result.implemented && result.return_value == 0);
    assert(result.kind == Mixer::CommandKind::AllowResidentRelease);
    assert(mixer.command(0x0000, 0x00080403, 4, 0x1000).started);
    const uint64_t command_count_before_host_stop = mixer.stats().commands;
    mixer.stop_playback();
    assert(mixer.active_voice_count() == 0 && !mixer.globally_paused());
    assert(mixer.stats().commands == command_count_before_host_stop);

    const auto& stats = mixer.stats();
    assert(stats.commands == 18);
    assert(stats.starts == 3);
    assert(stats.unimplemented == 0);
    assert(stats.unsupported_channels == 1);
    assert(stats.unknown == 1);
    assert(stats.last_d0 == 0x0000);
}

void test_pcm8_direct()
{
    Mixer mixer;
    mixer.reset();
    const uint32_t base = 0x1000;
    const std::vector<uint8_t> memory{0x80, 0x00, 0x7f};
    auto result = mixer.command(0x0000, 0x00080603, memory.size(), base);
    assert(result.started);

    std::array<int32_t, 6> output{};
    mixer.mix_s32(output.data(), 3, 15600, vector_reader(memory, base));
    assert(output[0] == -32768 && output[1] == -32768);
    assert(output[2] == 0 && output[3] == 0);
    assert(output[4] == 32512 && output[5] == 32512);
    assert(!mixer.voice(0).active && mixer.voice(0).remaining == 0);
    assert(mixer.stats().rendered_voice_frames == 3);
    assert(mixer.stats().rendered_source_bytes == 3);
    assert(mixer.stats().completed_voices == 1);

    result = mixer.command(0x00a0, 0, 0, 0);
    assert(static_cast<uint32_t>(result.return_value) == base + memory.size());
}

void test_pcm16_direct()
{
    Mixer mixer;
    mixer.reset();
    const uint32_t base = 0x2000;
    const std::vector<uint8_t> memory{0x40, 0x00, 0xc0, 0x00};
    auto result = mixer.command(0x0001, 0x00080501, memory.size(), base);
    assert(result.started);

    std::array<int32_t, 4> output{};
    mixer.mix_s32(output.data(), 2, 15600, vector_reader(memory, base));
    assert(output[0] == 16384 && output[1] == 0);
    assert(output[2] == -16384 && output[3] == 0);
    assert(mixer.stats().rendered_source_bytes == 4);
    assert(mixer.stats().completed_voices == 1);
}

void test_adpcm_low_nibble_first()
{
    Mixer mixer;
    mixer.reset();
    const uint32_t base = 0x3000;
    const std::vector<uint8_t> memory{0x11};
    auto result = mixer.command(0x0002, 0x00080403, memory.size(), base);
    assert(result.started);

    std::array<int32_t, 6> output{};
    mixer.mix_s32(output.data(), 3, 15600, vector_reader(memory, base));
    assert(output[0] == 0 && output[1] == 0);
    assert(output[2] == 96 && output[3] == 96);
    assert(output[4] == 192 && output[5] == 192);
    assert(!mixer.voice(2).active);
    assert(mixer.stats().rendered_source_bytes == 1);
    assert(mixer.stats().completed_voices == 1);
}


void test_fixed_point_resampling_is_chunk_independent()
{
    const uint32_t base = 0x6000;
    const std::vector<uint8_t> memory{0x10, 0x20, 0x30, 0x40};

    Mixer whole;
    whole.reset();
    assert(whole.command(0x0000, 0x00802003, memory.size(), base).started);
    std::array<int32_t, 92> whole_output{};
    whole.mix_s32(whole_output.data(), 46, 44100, vector_reader(memory, base));

    Mixer chunked;
    chunked.reset();
    assert(chunked.command(0x0000, 0x00802003, memory.size(), base).started);
    std::array<int32_t, 92> chunked_output{};
    chunked.mix_s32(chunked_output.data(), 10, 44100, vector_reader(memory, base));
    chunked.mix_s32(chunked_output.data() + 20, 36, 44100, vector_reader(memory, base));

    assert(whole_output == chunked_output);
    assert(whole.stats().rendered_source_bytes == chunked.stats().rendered_source_bytes);
    assert(whole.stats().rendered_voice_frames == chunked.stats().rendered_voice_frames);
    assert(whole.stats().completed_voices == 1);
    assert(chunked.stats().completed_voices == 1);
    // Modern PCM8A volume 0x80 is the same unity point as legacy volume 8.
    assert(whole_output[0] == 4096 && whole_output[1] == 4096);
}


void test_array_chain()
{
    Mixer mixer;
    mixer.reset();
    const uint32_t base = 0x7000;
    // Two 6-byte PCM8A descriptors: BE32 source address + BE16 byte length.
    std::vector<uint8_t> memory(0x40, 0);
    auto put16 = [&](size_t off, uint16_t v) { memory[off] = v >> 8; memory[off + 1] = v; };
    auto put32 = [&](size_t off, uint32_t v) { put16(off, v >> 16); put16(off + 2, v); };
    put32(0x00, base + 0x20); put16(0x04, 2);
    put32(0x06, base + 0x22); put16(0x0a, 2);
    memory[0x20] = 0x10; memory[0x21] = 0x20;
    memory[0x22] = 0x30; memory[0x23] = 0x40;

    auto result = mixer.command(0x0010, 0x00802003, 2, base, vector_reader(memory, base));
    assert(result.recognized && result.implemented && result.started);
    std::array<int32_t, 8> output{};
    mixer.mix_s32(output.data(), 4, 3900, vector_reader(memory, base));
    assert(output[0] == 4096 && output[1] == 4096);
    assert(output[2] == 8192 && output[3] == 8192);
    assert(output[4] == 12288 && output[5] == 12288);
    assert(output[6] == 16384 && output[7] == 16384);
    assert(!mixer.voice(0).active);
    assert(mixer.stats().array_chain_starts == 1);
    assert(mixer.stats().chain_segments_advanced == 1);
    assert(mixer.stats().rendered_source_bytes == 4);
}

void test_linked_array_chain()
{
    Mixer mixer;
    mixer.reset();
    const uint32_t base = 0x8000;
    std::vector<uint8_t> memory(0x80, 0);
    auto put16 = [&](size_t off, uint16_t v) { memory[off] = v >> 8; memory[off + 1] = v; };
    auto put32 = [&](size_t off, uint32_t v) { put16(off, v >> 16); put16(off + 2, v); };
    // Root descriptor, then pointer to a second descriptor block. Each block
    // ends in a BE32 link pointer at +6; zero terminates the linked chain.
    put32(0x00, base + 0x40); put16(0x04, 1); put32(0x06, base + 0x20);
    put32(0x20, base + 0x41); put16(0x24, 2); put32(0x26, 0);
    memory[0x40] = 0x10; memory[0x41] = 0x20; memory[0x42] = 0x30;

    auto result = mixer.command(0x0021, 0x00802001, 0, base, vector_reader(memory, base));
    assert(result.recognized && result.implemented && result.started && result.channel == 1);
    std::array<int32_t, 6> output{};
    mixer.mix_s32(output.data(), 3, 3900, vector_reader(memory, base));
    assert(output[0] == 4096 && output[1] == 0);
    assert(output[2] == 8192 && output[3] == 0);
    assert(output[4] == 12288 && output[5] == 0);
    assert(!mixer.voice(1).active);
    assert(mixer.stats().linked_chain_starts == 1);
    assert(mixer.stats().chain_segments_advanced == 1);
}

void test_pause_and_memory_fault()
{
    Mixer mixer;
    mixer.reset();
    const uint32_t base = 0x4000;
    const std::vector<uint8_t> memory{0x10, 0x20};
    assert(mixer.command(0x0000, 0x00080603, memory.size(), base).started);
    assert(mixer.command(0x00b0, 0, 0, 0).implemented);

    std::array<int32_t, 4> paused_output{};
    mixer.mix_s32(paused_output.data(), 2, 15600, vector_reader(memory, base));
    assert(paused_output[0] == 0 && paused_output[1] == 0);
    assert(paused_output[2] == 0 && paused_output[3] == 0);
    assert(mixer.voice(0).remaining == memory.size());

    assert(mixer.command(0x00c0, 0, 0, 0).implemented);
    std::array<int32_t, 2> resumed_output{};
    mixer.mix_s32(resumed_output.data(), 1, 15600, vector_reader(memory, base));
    assert(resumed_output[0] == 4096 && resumed_output[1] == 4096);

    Mixer faulting;
    faulting.reset();
    assert(faulting.command(0x0000, 0x00080603, 1, 0x5000).started);
    std::array<int32_t, 2> fault_output{};
    faulting.mix_s32(fault_output.data(), 1, 15600,
        [](uint32_t, uint8_t&) { return false; });
    assert(faulting.stats().memory_faults == 1);
    assert(faulting.stats().completed_voices == 1);
    assert(!faulting.voice(0).active);

    Mixer muted;
    muted.reset();
    assert(muted.command(0x0000, 0x00080603, memory.size(), base).started);
    std::array<int32_t, 4> muted_output{};
    muted.mix_s32(muted_output.data(), 2, 15600, vector_reader(memory, base), 0.0);
    assert(muted_output[0] == 0 && muted_output[1] == 0);
    assert(muted_output[2] == 0 && muted_output[3] == 0);
    assert(!muted.voice(0).active);
    assert(muted.stats().rendered_source_bytes == memory.size());
}

} // namespace

int main()
{
    test_commands();
    test_pcm8_direct();
    test_pcm16_direct();
    test_adpcm_low_nibble_first();
    test_fixed_point_resampling_is_chunk_independent();
    test_array_chain();
    test_linked_array_chain();
    test_pause_and_memory_fault();
    return 0;
}
