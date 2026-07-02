#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cpu/x86_cpu.h"
#include "drivers/hoot_driver.h"
#include "sound/libvgm_ym2608.h"

namespace hoot {

class Pc98DosDriver final : public HootDriver {
public:
    Pc98DosDriver();
    ~Pc98DosDriver() override;

    HootResult load(const HootEntry& entry,
                    const std::string& packs_path,
                    int sample_rate,
                    std::string& error) override;
    HootResult select_track(const HootEntry& entry,
                            int track_index,
                            std::string& error) override;
    void reset() override;
    int render_s16(int16_t* interleaved_stereo, int frames) override;
    int render_float(float* interleaved_stereo, int frames) override;
    void fill_track_info(const HootEntry& entry,
                         int track_index,
                         HootTrackInfo& out) const override;
    const char* name() const override;

private:
    struct LoadedFile {
        std::string path;
        std::vector<uint8_t> data;
    };

    enum class DriverType {
        Unknown,
        PMD,
        MMD,
    };

    void clear();
    bool setup_memory();
    void setup_interrupt_vectors();
    void setup_pit();
    uint8_t read_memory_byte(uint32_t address);
    void write_memory_byte(uint32_t address, uint8_t data);
    uint8_t read_io_port(uint16_t port);
    void write_io_port(uint16_t port, uint8_t data);
    void handle_interrupt(uint8_t int_num);
    void pit_timer_tick();
    void reset_cpu_context();
    void run_cpu_steps(int steps);
    bool is_playing() const { return playing_; }

    std::map<uint32_t, LoadedFile> files_by_slot_;
    std::vector<uint8_t> driver_data_;
    std::vector<uint8_t> shell_command_;
    std::string selected_bgm_path_;
    std::string selected_voice_path_;
    DriverType driver_type_ = DriverType::Unknown;

    std::unique_ptr<X86Cpu> cpu_;
    std::unique_ptr<LibvgmYm2608> ym2608_;

    std::vector<int16_t> mix_buffer_;

    std::vector<uint8_t> int_vector_table_;
    std::vector<uint8_t> dos_memory_;

    int sample_rate_ = 44100;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    bool loaded_ = false;
    bool playing_ = false;

    uint32_t pit_counter_ = 0;
    uint32_t pit_rate_ = 0;
    uint32_t pit_target_ = 0;
    uint64_t executed_cpu_steps_ = 0;

    static constexpr uint32_t kDosMemorySize = 64 * 1024;
    static constexpr uint32_t kDosEntryPoint = 0x0100;
    static constexpr uint16_t kPitIoport = 0x0080;
    static constexpr uint8_t kYm2608Clock = 8;
};

} // namespace hoot
