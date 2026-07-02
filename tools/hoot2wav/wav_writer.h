#pragma once

#include <cstdint>
#include <string>

bool write_wav_s16(const std::string& path,
                   const int16_t* interleaved_stereo,
                   int frames,
                   int sample_rate,
                   std::string& error);
