#pragma once

#include <cstdint>
#include <fstream>
#include <string>

namespace hootgui {

class WavRecorder {
public:
    WavRecorder() = default;
    ~WavRecorder();

    WavRecorder(const WavRecorder&) = delete;
    WavRecorder& operator=(const WavRecorder&) = delete;

    bool start(const std::string& path, int sample_rate, std::string& error);
    bool append(const int16_t* interleaved_stereo, int frames, std::string& error);
    bool stop(std::string& error);

    bool active() const { return out_.is_open(); }
    const std::string& path() const { return path_; }
    uint64_t frames() const { return frames_; }

private:
    bool finish_header(std::string& error);

    std::ofstream out_;
    std::string path_;
    int sample_rate_ = 0;
    uint64_t frames_ = 0;
};

} // namespace hootgui
