#pragma once

#include <cstddef>
#include <cstdint>

namespace hoot {

class LibvgmOkim6258 {
public:
    LibvgmOkim6258();
    ~LibvgmOkim6258();

    LibvgmOkim6258(const LibvgmOkim6258&) = delete;
    LibvgmOkim6258& operator=(const LibvgmOkim6258&) = delete;

    bool initialize(uint32_t output_sample_rate);
    void reset();
    void stop();
    void set_pan_and_rate(uint8_t value);
    uint8_t pan_and_rate() const { return pan_and_rate_; }
    bool play_memory(const uint8_t* data, size_t size);
    bool is_playing() const { return playing_; }
    void mix_s16(int16_t* interleaved_stereo, int frames, double gain);

private:
    void fetch();
    int current_native_rate() const;
    int pan_mask() const;

    uint32_t output_sample_rate_ = 44100;
    uint8_t pan_and_rate_ = 0x08;
    const uint8_t* playback_data_ = nullptr;
    size_t playback_size_ = 0;
    int sample_index_ = 0;
    int signal_ = 0;
    int prev_signal_ = 0;
    int step_ = 0;
    int count_ = 0;
    bool playing_ = false;
    bool release_ = false;
};

} // namespace hoot
