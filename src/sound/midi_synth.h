#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hoot {

class MidiSynth {
public:
    virtual ~MidiSynth() = default;
    virtual bool open(int sample_rate, const std::string& soundfont, std::string& error) = 0;
    virtual void close() = 0;
    virtual void reset() = 0;
    virtual bool active() const = 0;
    virtual const char* backend_name() const = 0;
    virtual const std::string& soundfont_path() const = 0;

    virtual void short_message(uint8_t status, uint8_t data1, uint8_t data2, uint8_t size) = 0;
    virtual void sysex(const std::vector<uint8_t>& data) = 0;
    virtual int render_s16(int16_t* interleaved_stereo, int frames) = 0;
};

} // namespace hoot
