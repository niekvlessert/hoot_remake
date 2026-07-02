#include "wav_writer.h"

#include <fstream>

namespace {

void write_u16_le(std::ofstream& out, uint16_t value)
{
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void write_u32_le(std::ofstream& out, uint32_t value)
{
    write_u16_le(out, static_cast<uint16_t>(value & 0xffff));
    write_u16_le(out, static_cast<uint16_t>((value >> 16) & 0xffff));
}

} // namespace

bool write_wav_s16(const std::string& path,
                   const int16_t* interleaved_stereo,
                   int frames,
                   int sample_rate,
                   std::string& error)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "unable to open output WAV: " + path;
        return false;
    }

    constexpr uint16_t channels = 2;
    constexpr uint16_t bits_per_sample = 16;
    constexpr uint16_t block_align = channels * (bits_per_sample / 8);
    const uint32_t data_bytes = static_cast<uint32_t>(frames * block_align);
    const uint32_t byte_rate = static_cast<uint32_t>(sample_rate * block_align);

    out.write("RIFF", 4);
    write_u32_le(out, 36 + data_bytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    write_u32_le(out, 16);
    write_u16_le(out, 1);
    write_u16_le(out, channels);
    write_u32_le(out, static_cast<uint32_t>(sample_rate));
    write_u32_le(out, byte_rate);
    write_u16_le(out, block_align);
    write_u16_le(out, bits_per_sample);
    out.write("data", 4);
    write_u32_le(out, data_bytes);

    for (int i = 0; i < frames * channels; ++i) {
        write_u16_le(out, static_cast<uint16_t>(interleaved_stereo[i]));
    }

    if (!out) {
        error = "failed while writing output WAV: " + path;
        return false;
    }
    return true;
}
