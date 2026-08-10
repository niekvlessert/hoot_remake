#include "apps/hootgui/wav_recorder.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main()
{
    const auto path = std::filesystem::temp_directory_path() / "hoot_wav_recorder_test.wav";
    std::string error;
    hootgui::WavRecorder recorder;
    assert(recorder.start(path.string(), 44100, error));
    const int16_t samples[] = {100, -100, 200, -200, 300, -300, 400, -400};
    assert(recorder.append(samples, 4, error));
    assert(recorder.frames() == 4);
    assert(recorder.stop(error));
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
        assert(bytes.size() == 44 + 16);
        assert(std::string(reinterpret_cast<const char*>(bytes.data()), 4) == "RIFF");
        assert(std::string(reinterpret_cast<const char*>(bytes.data() + 8), 4) == "WAVE");
        assert(bytes[40] == 16 && bytes[41] == 0 && bytes[42] == 0 && bytes[43] == 0);
    }
    std::filesystem::remove(path);
    return 0;
}
