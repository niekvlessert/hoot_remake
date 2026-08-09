#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {

struct t_mdxmini {
    int samples;
    int channels;
    void* mdx;
    void* pdx;
    void* self;
    void* songdata;
    int nlg_tempo;
};

static int g_rate = 44100;

void mdx_set_rate(int freq) { g_rate = freq; }

int mdx_open(t_mdxmini* data, char* filename, char*) {
    if (!data || !filename) return -1;
    FILE* f = std::fopen(filename, "rb");
    if (!f) return -1;
    unsigned char magic[4]{};
    const size_t n = std::fread(magic, 1, sizeof(magic), f);
    std::fclose(f);
    if (n != 4 || std::memcmp(magic, "MDX!", 4) != 0) return -1;
    data->samples = 0;
    data->channels = 2;
    data->self = reinterpret_cast<void*>(static_cast<uintptr_t>(0x4d445821u));
    data->nlg_tempo = g_rate;
    return 0;
}

void mdx_close(t_mdxmini* data) {
    if (data) data->self = nullptr;
}

int mdx_calc_sample(t_mdxmini* data, short* buf, int buffer_size) {
    if (!data || !data->self || !buf || buffer_size <= 0) return 0;
    const int samples = buffer_size / static_cast<int>(sizeof(short));
    for (int i = 0; i + 1 < samples; i += 2) {
        // deterministic stereo signal so channel order and byte sizing are observable.
        buf[i] = static_cast<short>(1200 + ((data->samples / 2) & 31));
        buf[i + 1] = static_cast<short>(-900 - ((data->samples / 2) & 31));
        data->samples += 2;
    }
    return 1;
}

void mdx_set_max_loop(t_mdxmini*, int) {}

}
