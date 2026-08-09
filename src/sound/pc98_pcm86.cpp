#include "sound/pc98_pcm86.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hoot {
namespace {

// The PC-9801-86 rate selector is commonly documented as these clocks / 8.
// Keep them as exact rational doubles to preserve the 33075/2 and similar
// half-Hz modes instead of rounding them in the transport state.
constexpr double kRates[8] = {
    44100.0,
    33075.0,
    22050.0,
    16537.5,
    11025.0,
    8268.75,
    5501.25,
    4134.375,
};

int16_t clamp_s16(int32_t value)
{
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

} // namespace

bool Pc98Pcm86::initialize(uint32_t output_rate)
{
    if (output_rate == 0) {
        return false;
    }
    output_rate_ = output_rate;
    reset();
    return true;
}

void Pc98Pcm86::reset()
{
    fifo_read_ = 0;
    fifo_write_ = 0;
    fifo_count_ = 0;
    fifo_threshold_ = 0x80;
    ext_function_ = 0;
    fifo_control_ = 0;
    dac_control_ = 0x32;
    volume_code_ = 15;
    phase_q32_ = 0;
    step_q32_ = 0;
    current_left_ = 0;
    current_right_ = 0;
    have_current_ = false;
    irq_armed_ = false;
    irq_pending_ = false;
    irq_delivered_ = false;
    stats_ = {};
}

bool Pc98Pcm86::handles_port(uint16_t port) const
{
    switch (port) {
    case 0xa460:
    case 0xa462:
    case 0xa464:
    case 0xa466:
    case 0xa468:
    case 0xa46a:
    case 0xa46c:
    case 0xa46e:
        return true;
    default:
        return false;
    }
}

uint8_t Pc98Pcm86::read_port(uint16_t port)
{
    switch (port) {
    case 0xa460:
        // Sound ID 4 (PC-9801-86) in the upper nibble. Bit 0 mirrors the
        // extended OPNA-channel enable latch.
        return static_cast<uint8_t>(0x40 | (ext_function_ & 0x01));
    case 0xa466: {
        uint8_t result = phase_q32_ >= (uint64_t{1} << 31) ? 0x01 : 0x00;
        if (fifo_count_ >= kFifoCapacity) result |= 0x80;
        else if (fifo_count_ == 0) result |= 0x40;
        return result;
    }
    case 0xa468:
        refresh_irq();
        return static_cast<uint8_t>((fifo_control_ & ~0x10u) | (irq_pending_ ? 0x10u : 0u));
    case 0xa46a:
        return dac_control_;
    case 0xa462:
    case 0xa464:
    case 0xa46c:
    case 0xa46e:
    default:
        return 0;
    }
}

void Pc98Pcm86::write_port(uint16_t port, uint8_t value)
{
    ++stats_.port_writes;
    switch (port) {
    case 0xa460:
        ext_function_ = value;
        break;
    case 0xa466:
        if ((value & 0xe0) == 0xa0) {
            volume_code_ = static_cast<uint8_t>((~value) & 0x0f);
        }
        break;
    case 0xa468: {
        const uint8_t changed = static_cast<uint8_t>(fifo_control_ ^ value);
        if ((changed & 0x08) != 0 && (value & 0x08) != 0) {
            clear_fifo();
        }
        // Falling edge of bit 4 acknowledges the low-water interrupt.
        if ((changed & 0x10) != 0 && (value & 0x10) == 0) {
            irq_pending_ = false;
            irq_delivered_ = false;
        }
        if ((changed & 0x07) != 0) {
            phase_q32_ = 0;
            have_current_ = false;
        }
        fifo_control_ = value;
        if ((changed & 0x80) != 0 && (value & 0x80) != 0) {
            phase_q32_ = 0;
            have_current_ = false;
        }
        refresh_irq();
        break;
    }
    case 0xa46a:
        if ((fifo_control_ & 0x20) != 0) {
            fifo_threshold_ = value == 0xff
                ? 0x7ffcu
                : std::min<uint32_t>((static_cast<uint32_t>(value) + 1u) << 7, 0x7ffcu);
        } else {
            dac_control_ = value;
            have_current_ = false;
            phase_q32_ = 0;
        }
        refresh_irq();
        break;
    case 0xa46c:
        push_byte(value);
        break;
    default:
        break;
    }
}

double Pc98Pcm86::source_rate() const
{
    return kRates[fifo_control_ & 0x07];
}

int Pc98Pcm86::bytes_per_source_frame() const
{
    switch (dac_control_ & 0x70) {
    case 0x10:
    case 0x20:
        return 2;
    case 0x30:
        return 4;
    case 0x50:
    case 0x60:
        return 1;
    case 0x70:
        return 2;
    default:
        return 0;
    }
}

void Pc98Pcm86::clear_fifo()
{
    fifo_read_ = 0;
    fifo_write_ = 0;
    fifo_count_ = 0;
    phase_q32_ = 0;
    current_left_ = 0;
    current_right_ = 0;
    have_current_ = false;
    irq_armed_ = false;
    irq_pending_ = false;
    irq_delivered_ = false;
}

void Pc98Pcm86::push_byte(uint8_t value)
{
    if (fifo_count_ >= kFifoCapacity) {
        // Real software should observe the full bit and wait.  If a broken or
        // timing-insensitive writer overruns it, preserve forward progress by
        // discarding the oldest byte rather than deadlocking the guest.
        fifo_read_ = (fifo_read_ + 1) % kFifoCapacity;
        --fifo_count_;
        ++stats_.fifo_overflows;
    }
    fifo_[fifo_write_] = value;
    fifo_write_ = (fifo_write_ + 1) % kFifoCapacity;
    ++fifo_count_;
    ++stats_.fifo_writes;
    stats_.peak_fifo_bytes = std::max(stats_.peak_fifo_bytes, fifo_count_);
    irq_armed_ = true;
    if (fifo_count_ > fifo_threshold_) {
        irq_pending_ = false;
        irq_delivered_ = false;
    }
}

uint8_t Pc98Pcm86::pop_byte()
{
    if (fifo_count_ == 0) {
        return 0;
    }
    const uint8_t value = fifo_[fifo_read_];
    fifo_read_ = (fifo_read_ + 1) % kFifoCapacity;
    --fifo_count_;
    ++stats_.fifo_reads;
    return value;
}

bool Pc98Pcm86::fetch_source_frame(int32_t& left, int32_t& right)
{
    const int bpf = bytes_per_source_frame();
    if (bpf <= 0 || fifo_count_ < static_cast<uint32_t>(bpf)) {
        left = 0;
        right = 0;
        return false;
    }

    const auto read8 = [this]() -> int32_t {
        // Multiplication preserves the signed 8-bit PCM value without the
        // undefined behaviour of left-shifting a negative signed integer.
        return static_cast<int32_t>(static_cast<int8_t>(pop_byte())) * 256;
    };
    const auto read16 = [this]() -> int32_t {
        // PC-9801-86 16-bit PCM is big-endian. Assemble through an unsigned
        // word so negative high bytes never participate in a signed shift.
        const uint16_t word = static_cast<uint16_t>(static_cast<uint16_t>(pop_byte()) << 8)
            | static_cast<uint16_t>(pop_byte());
        return static_cast<int16_t>(word);
    };

    switch (dac_control_ & 0x70) {
    case 0x10:
        left = 0;
        right = read16();
        break;
    case 0x20:
        left = read16();
        right = 0;
        break;
    case 0x30:
        left = read16();
        right = read16();
        break;
    case 0x50:
        left = 0;
        right = read8();
        break;
    case 0x60:
        left = read8();
        right = 0;
        break;
    case 0x70:
        left = read8();
        right = read8();
        break;
    default:
        left = right = 0;
        return false;
    }
    ++stats_.rendered_source_frames;
    refresh_irq();
    return true;
}

void Pc98Pcm86::refresh_irq()
{
    if (!interrupt_enabled() || !irq_armed_) {
        return;
    }
    if (fifo_count_ <= fifo_threshold_ && !irq_pending_) {
        irq_pending_ = true;
        irq_delivered_ = false;
        ++stats_.irq_requests;
    }
}

void Pc98Pcm86::mark_irq_delivered()
{
    if (irq_pending_ && !irq_delivered_) {
        irq_delivered_ = true;
        ++stats_.irq_deliveries;
    }
}

int Pc98Pcm86::frames_until_irq() const
{
    if (!playback_enabled() || !interrupt_enabled() || !irq_armed_ || irq_pending_) {
        return irq_pending_ ? 0 : std::numeric_limits<int>::max();
    }
    const int bpf = bytes_per_source_frame();
    if (bpf <= 0 || fifo_count_ <= fifo_threshold_) {
        return 0;
    }
    const uint32_t bytes = fifo_count_ - fifo_threshold_;
    const uint32_t source_frames = (bytes + static_cast<uint32_t>(bpf) - 1u) / static_cast<uint32_t>(bpf);
    const double rate = source_rate();
    if (rate <= 0.0 || output_rate_ == 0) {
        return std::numeric_limits<int>::max();
    }
    return std::max(1, static_cast<int>(std::ceil(static_cast<double>(source_frames) * output_rate_ / rate)));
}

void Pc98Pcm86::mix_s16(int16_t* interleaved_stereo, int frames, double gain)
{
    if (interleaved_stereo == nullptr || frames <= 0) {
        return;
    }
    if (!playback_enabled() || bytes_per_source_frame() == 0 || volume_code_ == 0) {
        stats_.rendered_frames += static_cast<uint64_t>(frames);
        refresh_irq();
        return;
    }

    const double rate = source_rate();
    step_q32_ = static_cast<uint64_t>(std::llround((rate / static_cast<double>(output_rate_)) * 4294967296.0));
    const double volume = (static_cast<double>(volume_code_) / 15.0) * std::clamp(gain, 0.0, 8.0);

    if (!have_current_) {
        have_current_ = fetch_source_frame(current_left_, current_right_);
        phase_q32_ = 0;
    }

    for (int i = 0; i < frames; ++i) {
        const int32_t pcm_l = have_current_ ? static_cast<int32_t>(std::lround(current_left_ * volume)) : 0;
        const int32_t pcm_r = have_current_ ? static_cast<int32_t>(std::lround(current_right_ * volume)) : 0;
        interleaved_stereo[i * 2] = clamp_s16(static_cast<int32_t>(interleaved_stereo[i * 2]) + pcm_l);
        interleaved_stereo[i * 2 + 1] = clamp_s16(static_cast<int32_t>(interleaved_stereo[i * 2 + 1]) + pcm_r);

        phase_q32_ += step_q32_;
        while (phase_q32_ >= (uint64_t{1} << 32)) {
            phase_q32_ -= (uint64_t{1} << 32);
            have_current_ = fetch_source_frame(current_left_, current_right_);
            if (!have_current_) {
                current_left_ = current_right_ = 0;
                phase_q32_ = 0;
                break;
            }
        }
    }
    stats_.rendered_frames += static_cast<uint64_t>(frames);
    refresh_irq();
}

} // namespace hoot
