#include "wav_recorder.h"

#include <algorithm>
#include <filesystem>
#include <limits>

namespace hootgui {
namespace {

void put_u16(std::ostream& out, uint16_t value)
{
    out.put(static_cast<char>(value & 0xffu));
    out.put(static_cast<char>((value >> 8) & 0xffu));
}

void put_u32(std::ostream& out, uint32_t value)
{
    put_u16(out, static_cast<uint16_t>(value & 0xffffu));
    put_u16(out, static_cast<uint16_t>((value >> 16) & 0xffffu));
}

} // namespace

WavRecorder::~WavRecorder()
{
    if (out_.is_open()) {
        std::string ignored;
        stop(ignored);
    }
}

bool WavRecorder::start(const std::string& path, int sample_rate, std::string& error)
{
    error.clear();
    if (out_.is_open()) {
        if (!stop(error)) return false;
    }
    if (path.empty() || sample_rate <= 0) {
        error = "invalid WAV recording path or sample rate";
        return false;
    }
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "unable to create recording directory: " + ec.message();
            return false;
        }
    }
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) {
        error = "unable to open WAV recording: " + path;
        return false;
    }
    path_ = path;
    sample_rate_ = sample_rate;
    frames_ = 0;

    out_.write("RIFF", 4); put_u32(out_, 0);
    out_.write("WAVE", 4);
    out_.write("fmt ", 4); put_u32(out_, 16);
    put_u16(out_, 1);       // PCM
    put_u16(out_, 2);       // stereo
    put_u32(out_, static_cast<uint32_t>(sample_rate_));
    put_u32(out_, static_cast<uint32_t>(sample_rate_) * 4u);
    put_u16(out_, 4);       // block align
    put_u16(out_, 16);      // bits/sample
    out_.write("data", 4); put_u32(out_, 0);
    if (!out_) {
        error = "failed while writing WAV header: " + path;
        out_.close();
        return false;
    }
    return true;
}

bool WavRecorder::append(const int16_t* samples, int frames, std::string& error)
{
    error.clear();
    if (!out_.is_open() || !samples || frames <= 0) return true;
    constexpr uint64_t max_frames = (static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) - 36u) / 4u;
    const uint64_t accepted = std::min<uint64_t>(static_cast<uint64_t>(frames), max_frames - std::min(frames_, max_frames));
    for (uint64_t i = 0; i < accepted * 2u; ++i)
        put_u16(out_, static_cast<uint16_t>(samples[i]));
    frames_ += accepted;
    if (!out_) {
        error = "failed while writing WAV recording: " + path_;
        return false;
    }
    if (accepted != static_cast<uint64_t>(frames)) {
        error = "WAV recording reached the 4 GiB RIFF limit";
        return false;
    }
    return true;
}

bool WavRecorder::finish_header(std::string& error)
{
    if (!out_.is_open()) return true;
    const uint64_t data64 = frames_ * 4u;
    if (data64 > std::numeric_limits<uint32_t>::max() - 36u) {
        error = "WAV recording is too large for RIFF/WAVE";
        return false;
    }
    const uint32_t data = static_cast<uint32_t>(data64);
    out_.seekp(4, std::ios::beg); put_u32(out_, 36u + data);
    out_.seekp(40, std::ios::beg); put_u32(out_, data);
    out_.seekp(0, std::ios::end);
    if (!out_) {
        error = "unable to finalize WAV header: " + path_;
        return false;
    }
    return true;
}

bool WavRecorder::stop(std::string& error)
{
    error.clear();
    if (!out_.is_open()) return true;
    const bool ok = finish_header(error);
    out_.close();
    if (ok && !out_) {
        error = "unable to close WAV recording: " + path_;
        return false;
    }
    return ok;
}

} // namespace hootgui
