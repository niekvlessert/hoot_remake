#pragma once

#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cpu/x86_cpu.h"
#include "drivers/hoot_driver.h"
#include "sound/libvgm_ym2608.h"

namespace hoot {

class MicrocabinPc98DosDriver final : public HootDriver {
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

private:
    struct LoadedFile {
        std::string path;
        std::vector<uint8_t> data;
    };

    void clear();
    bool setup_runtime(std::string& error);
    void setup_interrupt_vector(uint8_t vector, uint16_t segment, uint16_t offset);
    void initialize_mmd_device();
    void trigger_interrupt_vector(uint8_t vector);
    void push_cpu_word(uint16_t value);
    void reset_runtime();
    void run_cpu_steps(int steps);
    uint8_t read_memory_byte(uint32_t address);
    void write_memory_byte(uint32_t address, uint8_t data);
    uint8_t read_io_port(uint16_t port);
    void write_io_port(uint16_t port, uint8_t data);
    void handle_interrupt(uint8_t int_num);
    void handle_dos_interrupt();
    void handle_mmd_interrupt();
    void dos_read_selected_file();
    void dos_seek_selected_file();
    void call_mmd_api(uint16_t ax, uint16_t bx = 0, uint16_t cx = 0, uint16_t dx = 0);
    void load_mmd_stream(uint8_t command, const std::vector<uint8_t>& data);
    void copy_to_transfer_buffer(const uint8_t* data, size_t size);

    std::map<uint32_t, LoadedFile> files_by_slot_;
    std::vector<uint8_t> mmd_sys_;
    std::vector<uint8_t> mmd_helper_;
    std::string shell_command_;
    std::string mmd_device_command_;
    std::string selected_bgm_path_;
    std::string selected_voice_path_;
    std::vector<uint8_t> selected_bgm_data_;
    std::vector<uint8_t> selected_voice_data_;
    std::unique_ptr<X86Cpu> cpu_;
    std::unique_ptr<LibvgmYm2608> ym2608_;
    std::vector<int16_t> mix_buffer_;
    int sample_rate_ = 44100;
    int selected_track_ = 0;
    uint32_t selected_code_ = 0;
    bool loaded_ = false;
    bool playing_ = false;
    bool command_pending_ = false;
    uint8_t command_latch_ = 0;
    uint8_t command_low_ = 0xff;
    uint8_t command_high_ = 0xff;
    uint8_t selected_file_handle_ = 1;
    size_t selected_file_offset_ = 0;
    uint64_t executed_cpu_steps_ = 0;
    uint32_t debug_io_reads_ = 0;
    uint32_t debug_io_writes_ = 0;
    uint32_t debug_int21_ = 0;
    uint32_t debug_intd2_ = 0;
    uint32_t debug_opna_writes_ = 0;
    uint32_t debug_mailbox_reads_ = 0;
    uint32_t debug_file_reads_ = 0;
    uint32_t debug_file_seeks_ = 0;
    uint8_t debug_last_int_ = 0;
    uint8_t debug_last_ah_ = 0;
    uint8_t debug_last_mailbox_port_ = 0;
    uint8_t debug_last_mailbox_value_ = 0;
    uint8_t current_opna_address_[2] = {0, 0};
    std::array<std::array<uint8_t, 256>, 2> opna_registers_{};

    static constexpr uint32_t kMemorySize = 1024 * 1024;
    static constexpr uint16_t kHelperSegment = 0x0000;
    static constexpr uint16_t kHelperOffset = 0x0100;
    static constexpr uint16_t kStackSegment = 0x2000;
    static constexpr uint16_t kStackPointer = 0xfffe;
    static constexpr uint16_t kMmdSegment = 0x3000;
    static constexpr uint16_t kMmdOffset = 0x0000;
    static constexpr uint16_t kIretSegment = 0x0000;
    static constexpr uint16_t kIretOffset = 0x00f0;
    static constexpr uint16_t kApiReturnOffset = 0x00f1;
    static constexpr uint16_t kDeviceRequestOffset = 0x0600;
    static constexpr uint16_t kDeviceCommandOffset = 0x0700;
    static constexpr uint16_t kTransferSegment = 0x0000;
    static constexpr uint16_t kTransferOffset = 0x0800;
    static constexpr size_t kTransferSize = 0x0c00;
};

} // namespace hoot
