#pragma once

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

#include "drivers/hoot_driver.h"
#include "sound/libvgm_okim6258.h"
#include "sound/libvgm_ym2151.h"

namespace hoot {

class X68kGenericDriver final : public HootDriver {
public:
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

    uint8_t read_memory_8(uint32_t address);
    void write_memory_8(uint32_t address, uint8_t data);

private:
    static constexpr size_t kRomSize = 0x100000;
    static constexpr size_t kRamSize = 0x10000;
    static constexpr size_t kScratchSize = 0x100000;

    void clear();
    bool load_ad68snd_legacy_pack(const std::string& packs_path, std::string& error);
    void execute_seconds(double seconds);
    void open_trace_from_environment();
    void trace_ym2151(uint8_t reg, uint8_t data);
    void trace_io(const char* operation, uint32_t address, uint8_t data);
    uint8_t read_midi(uint32_t address);
    void write_midi(uint32_t address, uint8_t data);
    static void ym2151_irq_handler(void* param, uint8_t irq);
    uint32_t read_memory_32(uint32_t address);
    uint32_t read_be32(size_t offset) const;

    std::array<uint8_t, kRomSize> rom_{};
    std::array<uint8_t, kRamSize> ram_{};
    std::array<uint8_t, kScratchSize> scratch_{};
    int sample_rate_ = 44100;
    double cpu_clock_hz_ = 10000000.0;
    uint32_t ym2151_clock_hz_ = 4000000;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    uint32_t reset_sp_ = 0;
    uint32_t reset_pc_ = 0;
    uint32_t memdump_address_ = 0;
    uint64_t loaded_code_bytes_ = 0;
    uint64_t debug_cpu_cycles_ = 0;
    uint64_t debug_io_reads_ = 0;
    uint64_t debug_io_writes_ = 0;
    uint64_t debug_ym2151_writes_ = 0;
    uint64_t debug_ym2151_keyons_ = 0;
    uint64_t debug_ym2151_irqs_ = 0;
    uint32_t pending_ym2151_irq_pulses_ = 0;
    uint64_t debug_adpcm_writes_ = 0;
    uint64_t debug_adpcm_starts_ = 0;
    uint32_t adpcm_address_ = 0;
    uint32_t adpcm_size_ = 0;
    double adpcm_gain_ = 0.40;
    uint8_t current_ym2151_reg_ = 0;
    uint8_t debug_last_ym2151_reg_ = 0;
    uint8_t debug_last_ym2151_data_ = 0;
    uint8_t mailbox_flag_ = 0;
    uint16_t mailbox_code_ = 0;
    bool midi_enabled_ = false;
    uint8_t midi_reg_high_ = 0;
    uint8_t midi_vector_ = 0;
    uint8_t midi_int_enable_ = 0;
    uint8_t midi_int_vect_ = 0x10;
    uint32_t midi_buffered_ = 0;
    bool loaded_ = false;
    bool ad68snd_legacy_layout_ = false;
    std::ofstream trace_;
    uint64_t trace_events_ = 0;
    uint64_t trace_limit_ = 0;
    LibvgmYm2151 ym2151_;
    LibvgmOkim6258 adpcm_;
};

} // namespace hoot
