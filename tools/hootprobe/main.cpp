#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "config/hoot_catalog.h"
#include "config/hoot_catalog_loader.h"
#include "core/entry_validation.h"
#include "core/console_utf8.h"
#include "core/hoot_track_info.h"
#include "drivers/driver_registry.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string catalog = "catalog/hoot.sqlite.zst";
    std::string packs = ".";
    std::string output = "compatibility.json";
    std::string entry;
    std::string archive;
    std::string catalog_driver;
    int seconds = 5;
    int startup_grace_seconds = 3;
    int rate = 44100;
    int max_tracks = 1;
    int track_index = -1;
    int timeout_seconds = 20;
    int silence_peak = 8;
    bool include_missing = false;
    bool isolate = true;
    bool progress = true;
};

struct AudioStats {
    int peak = 0;
    long double sum_squares = 0.0;
    uint64_t sample_count = 0;
    uint64_t nonzero_samples = 0;
    uint64_t clipped_samples = 0;
    int64_t first_audible_sample = -1;

    void add(const int16_t* samples, size_t count, int silence_peak)
    {
        for (size_t i = 0; i < count; ++i) {
            const auto sample = samples[i];
            const int magnitude = std::abs(static_cast<int>(sample));
            if (first_audible_sample < 0 && magnitude > silence_peak) {
                first_audible_sample = static_cast<int64_t>(sample_count);
            }
            peak = std::max(peak, magnitude);
            sum_squares += static_cast<long double>(sample) * static_cast<long double>(sample);
            ++sample_count;
            if (sample != 0) {
                ++nonzero_samples;
            }
            if (magnitude >= 32767) {
                ++clipped_samples;
            }
        }
    }

    double rms() const
    {
        return sample_count == 0
            ? 0.0
            : std::sqrt(static_cast<double>(sum_squares / static_cast<long double>(sample_count)));
    }
};

struct TrackScan {
    int index = 0;
    uint32_t code = 0;
    std::string title;
    std::string result = "not-run";
    std::string error;
    int rendered_frames = 0;
    int64_t elapsed_ms = 0;
    AudioStats audio;
    HootTrackInfo baseline{};
    HootTrackInfo info{};
    uint64_t cpu_cycles_or_steps = 0;
    uint64_t io_reads = 0;
    uint64_t io_writes = 0;
    uint64_t chip_writes = 0;
    uint64_t keyons = 0;
    uint64_t unsupported_opcodes = 0;
    uint64_t pcm8_commands = 0;
    uint64_t pcm8_starts = 0;
    uint64_t pcm8_stops = 0;
    uint64_t pcm8_unimplemented = 0;
    uint64_t pcm8_unknown = 0;
    uint64_t pcm8_unsupported_channels = 0;
    uint64_t pcm8_rendered_voice_frames = 0;
    uint64_t pcm8_rendered_source_bytes = 0;
    uint64_t pcm8_completed_voices = 0;
    uint64_t pcm8_memory_faults = 0;
    uint64_t midi_bytes_enqueued = 0;
    uint64_t midi_bytes_transmitted = 0;
    uint64_t midi_channel_messages = 0;
    uint64_t midi_system_common_messages = 0;
    uint64_t midi_sysex_messages = 0;
    uint64_t midi_sysex_bytes = 0;
    uint64_t midi_running_status_messages = 0;
    uint64_t midi_malformed_bytes = 0;
    uint64_t midi_note_ons = 0;
    uint64_t midi_note_offs = 0;
    uint64_t midi_control_changes = 0;
    uint64_t midi_program_changes = 0;
    uint64_t midi_pitch_bends = 0;
    uint64_t midi_irq_count = 0;
    uint64_t midi_synth_frames = 0;
};

struct EntryScan {
    std::string outcome = "not-run";
    std::string load_error;
    int64_t load_ms = 0;
    std::vector<TrackScan> tracks;
};

enum ExitCode {
    ExitPass = 0,
    ExitSilent = 10,
    ExitLoadError = 11,
    ExitTrackError = 12,
    ExitRenderError = 13,
    ExitCpuError = 14,
    ExitWarning = 15,
    ExitInternalError = 16,
    ExitNoTracks = 17,
    ExitTimeout = 18,
    ExitPartialSilent = 19,
    ExitControlOnly = 20
};

void usage(const char* argv0)
{
    std::cerr
        << "usage: " << argv0 << " [--catalog file] [--packs dir] [--output report.json]\n"
        << "       [--seconds n] [--startup-grace n] [--rate hz]\n"
        << "       [--max-tracks n|--all-tracks|--track n]\n"
        << "       [--timeout n] [--entry id|--archive name] [--catalog-driver name]\n"
        << "       [--include-missing]\n"
        << "       [--no-isolate] [--silence-peak n] [--quiet]\n\n"
        << "By default only catalogue entries whose archive ZIP is locally present are tested.\n"
        << "On POSIX systems each entry is isolated in a child process so crashes and hard timeouts\n"
        << "do not abort the complete scan. Track numbers in the report are zero-based.\n";
}

bool need_value(int argc, char** argv, int index)
{
    if (index + 1 < argc) {
        return true;
    }
    std::cerr << "missing value for " << argv[index] << "\n";
    return false;
}

bool parse_options(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--catalog" && need_value(argc, argv, i)) {
            options.catalog = argv[++i];
        } else if (arg == "--packs" && need_value(argc, argv, i)) {
            options.packs = argv[++i];
        } else if (arg == "--output" && need_value(argc, argv, i)) {
            options.output = argv[++i];
        } else if (arg == "--seconds" && need_value(argc, argv, i)) {
            options.seconds = std::atoi(argv[++i]);
        } else if (arg == "--startup-grace" && need_value(argc, argv, i)) {
            options.startup_grace_seconds = std::atoi(argv[++i]);
        } else if (arg == "--rate" && need_value(argc, argv, i)) {
            options.rate = std::atoi(argv[++i]);
        } else if (arg == "--max-tracks" && need_value(argc, argv, i)) {
            options.max_tracks = std::atoi(argv[++i]);
        } else if (arg == "--all-tracks") {
            options.max_tracks = 0;
        } else if (arg == "--track" && need_value(argc, argv, i)) {
            options.track_index = std::atoi(argv[++i]);
        } else if (arg == "--timeout" && need_value(argc, argv, i)) {
            options.timeout_seconds = std::atoi(argv[++i]);
        } else if (arg == "--entry" && need_value(argc, argv, i)) {
            options.entry = argv[++i];
        } else if (arg == "--archive" && need_value(argc, argv, i)) {
            options.archive = argv[++i];
        } else if (arg == "--catalog-driver" && need_value(argc, argv, i)) {
            options.catalog_driver = argv[++i];
        } else if (arg == "--include-missing") {
            options.include_missing = true;
        } else if (arg == "--no-isolate") {
            options.isolate = false;
        } else if (arg == "--silence-peak" && need_value(argc, argv, i)) {
            options.silence_peak = std::atoi(argv[++i]);
        } else if (arg == "--quiet") {
            options.progress = false;
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        }
    }
    if (options.seconds <= 0 || options.startup_grace_seconds < 0
        || options.rate <= 0 || options.timeout_seconds <= 0
        || options.max_tracks < 0 || options.track_index < -1 || options.silence_peak < 0) {
        std::cerr << "numeric options must be positive (startup-grace may be zero; "
                     "max-tracks may be zero for all)\n";
        return false;
    }
    if (!options.entry.empty() && !options.archive.empty()) {
        std::cerr << "--entry and --archive are mutually exclusive\n";
        return false;
    }
#if defined(_WIN32)
    options.isolate = false;
#endif
    return true;
}

std::string json_escape(const std::string& value)
{
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                    << static_cast<int>(ch) << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
            break;
        }
    }
    return out.str();
}

const char* midiout_class_name(int type)
{
    switch (type) {
    case 0: return "mt32";
    case 1: return "mt32-emulation";
    case 2: return "mt32";
    case 3: return "cm64";
    case 4: return "gs-sc55";
    case 5: return "korg-m1";
    case 6: return "vermouth";
    case 7: return "gs-sc88";
    case 8: return "gm";
    default: return "none-or-unknown";
    }
}

const char* midi_backend_name(uint32_t kind)
{
    switch (kind) {
    case 1: return "fluidsynth";
    case 2: return "nuked-sc55-clap";
    case 3: return "munt-mt32";
    case 4: return "munt-cm32l";
    case 5: return "munt-cm64";
    case 6: return "cm32p";
    case 7: return "vermouth";
    default: return "none";
    }
}


std::string quoted(const std::string& value)
{
    return "\"" + json_escape(value) + "\"";
}

std::string utc_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

uint64_t counter_delta(uint64_t after, uint64_t before)
{
    return after >= before ? after - before : after;
}

uint32_t last_unsupported_opcode(const HootTrackInfo& info)
{
    return info.debug_last_unsupported_opcode;
}

bool is_control_track_title(const std::string& title)
{
    std::string ascii;
    ascii.reserve(title.size());
    for (const unsigned char ch : title) {
        ascii.push_back(ch < 0x80
            ? static_cast<char>(std::tolower(ch))
            : static_cast<char>(ch));
    }

    const auto first = ascii.find_first_not_of(" \t\r\n");
    const auto last = ascii.find_last_not_of(" \t\r\n");
    const std::string trimmed = first == std::string::npos
        ? std::string{}
        : ascii.substr(first, last - first + 1);
    return trimmed == "stop"
        || trimmed == "music stop"
        || trimmed == "sound stop"
        || trimmed.find("fade out") != std::string::npos
        || title.find("演奏停止") != std::string::npos
        || title.find("再生停止") != std::string::npos;
}


EntryScan scan_entry(const hoot::HootEntry& entry, const Options& options)
{
    EntryScan result;
    auto driver = hoot::DriverRegistry::instance().create(entry);
    if (!driver) {
        result.outcome = "unsupported";
        result.load_error = "driver registry did not create a replay host";
        return result;
    }

    std::string error;
    const auto load_start = Clock::now();
    const auto load_result = driver->load(entry, options.packs, options.rate, error);
    result.load_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - load_start).count();
    if (load_result != HOOT_OK) {
        result.outcome = "load-error";
        result.load_error = error;
        return result;
    }

    std::vector<size_t> track_indices;
    if (options.track_index >= 0) {
        if (static_cast<size_t>(options.track_index) >= entry.tracks.size()) {
            result.outcome = "track-error";
            result.load_error = "requested track index is outside the catalog track list";
            return result;
        }
        track_indices.push_back(static_cast<size_t>(options.track_index));
    } else {
        const size_t track_limit = options.max_tracks == 0
            ? entry.tracks.size()
            : std::min(entry.tracks.size(), static_cast<size_t>(options.max_tracks));
        track_indices.reserve(track_limit);
        for (size_t index = 0; index < track_limit; ++index) {
            track_indices.push_back(index);
        }
    }
    if (track_indices.empty()) {
        result.outcome = "no-tracks";
        return result;
    }

    int worst_severity = 0;
    bool any_audio = false;
    bool any_silent = false;
    bool any_control = false;
    bool any_warning = false;
    constexpr int kChunkFrames = 1024;
    std::vector<int16_t> pcm(static_cast<size_t>(kChunkFrames) * 2);
    for (const size_t track_index : track_indices) {
        TrackScan track;
        track.index = static_cast<int>(track_index);
        track.code = entry.tracks[track_index].code;
        track.title = entry.tracks[track_index].title;
        const bool control_track = is_control_track_title(track.title);

        driver->fill_track_info(entry, track.index, track.baseline);
        const auto start = Clock::now();
        error.clear();
        const auto select_result = driver->select_track(entry, track.index, error);
        if (select_result != HOOT_OK) {
            track.result = "select-error";
            track.error = error;
            worst_severity = std::max(worst_severity, 4);
            result.tracks.push_back(std::move(track));
            continue;
        }

        const int requested_frames = options.seconds * options.rate;
        const int grace_frames = options.startup_grace_seconds * options.rate;
        while (true) {
            int target_frames = requested_frames;
            if (!control_track) {
                if (track.audio.first_audible_sample >= 0) {
                    const int first_audible_frame = static_cast<int>(track.audio.first_audible_sample / 2);
                    target_frames = first_audible_frame + requested_frames;
                } else {
                    target_frames = requested_frames + grace_frames;
                }
            }
            if (track.rendered_frames >= target_frames) {
                break;
            }
            if (Clock::now() - start >= std::chrono::seconds(options.timeout_seconds)) {
                track.result = "timeout";
                track.error = "soft render timeout reached between audio chunks";
                worst_severity = std::max(worst_severity, 6);
                break;
            }
            const int request = std::min(kChunkFrames, target_frames - track.rendered_frames);
            const int rendered = driver->render_s16(pcm.data(), request);
            if (rendered <= 0 || rendered > request) {
                track.result = "render-error";
                track.error = "driver returned an invalid rendered frame count";
                worst_severity = std::max(worst_severity, 5);
                break;
            }
            track.audio.add(pcm.data(), static_cast<size_t>(rendered) * 2, options.silence_peak);
            track.rendered_frames += rendered;
        }
        track.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        driver->fill_track_info(entry, track.index, track.info);
        track.cpu_cycles_or_steps = counter_delta(track.info.debug_cpu_cycles, track.baseline.debug_cpu_cycles);
        track.io_reads = counter_delta(track.info.debug_io_reads, track.baseline.debug_io_reads);
        track.io_writes = counter_delta(track.info.debug_io_writes, track.baseline.debug_io_writes);
        track.chip_writes = counter_delta(track.info.debug_opn_writes, track.baseline.debug_opn_writes);
        track.keyons = counter_delta(track.info.debug_opn_keyons, track.baseline.debug_opn_keyons);
        track.unsupported_opcodes = counter_delta(track.info.debug_unsupported_opcodes,
                                                   track.baseline.debug_unsupported_opcodes);
        track.pcm8_commands = counter_delta(track.info.debug_pcm8_commands,
                                            track.baseline.debug_pcm8_commands);
        track.pcm8_starts = counter_delta(track.info.debug_pcm8_starts,
                                          track.baseline.debug_pcm8_starts);
        track.pcm8_stops = counter_delta(track.info.debug_pcm8_stops,
                                         track.baseline.debug_pcm8_stops);
        track.pcm8_unimplemented = counter_delta(track.info.debug_pcm8_unimplemented,
                                                 track.baseline.debug_pcm8_unimplemented);
        track.pcm8_unknown = counter_delta(track.info.debug_pcm8_unknown,
                                           track.baseline.debug_pcm8_unknown);
        track.pcm8_unsupported_channels = counter_delta(
            track.info.debug_pcm8_unsupported_channels,
            track.baseline.debug_pcm8_unsupported_channels);
        track.pcm8_rendered_voice_frames = counter_delta(
            track.info.debug_pcm8_rendered_voice_frames,
            track.baseline.debug_pcm8_rendered_voice_frames);
        track.pcm8_rendered_source_bytes = counter_delta(
            track.info.debug_pcm8_rendered_source_bytes,
            track.baseline.debug_pcm8_rendered_source_bytes);
        track.pcm8_completed_voices = counter_delta(
            track.info.debug_pcm8_completed_voices,
            track.baseline.debug_pcm8_completed_voices);
        track.pcm8_memory_faults = counter_delta(
            track.info.debug_pcm8_memory_faults,
            track.baseline.debug_pcm8_memory_faults);
        track.midi_bytes_enqueued = counter_delta(track.info.debug_midi_bytes_enqueued,
                                                  track.baseline.debug_midi_bytes_enqueued);
        track.midi_bytes_transmitted = counter_delta(track.info.debug_midi_bytes_transmitted,
                                                     track.baseline.debug_midi_bytes_transmitted);
        track.midi_channel_messages = counter_delta(track.info.debug_midi_channel_messages,
                                                    track.baseline.debug_midi_channel_messages);
        track.midi_system_common_messages = counter_delta(
            track.info.debug_midi_system_common_messages,
            track.baseline.debug_midi_system_common_messages);
        track.midi_sysex_messages = counter_delta(track.info.debug_midi_sysex_messages,
                                                  track.baseline.debug_midi_sysex_messages);
        track.midi_sysex_bytes = counter_delta(track.info.debug_midi_sysex_bytes,
                                               track.baseline.debug_midi_sysex_bytes);
        track.midi_running_status_messages = counter_delta(
            track.info.debug_midi_running_status_messages,
            track.baseline.debug_midi_running_status_messages);
        track.midi_malformed_bytes = counter_delta(track.info.debug_midi_malformed_bytes,
                                                   track.baseline.debug_midi_malformed_bytes);
        track.midi_note_ons = counter_delta(track.info.debug_midi_note_ons,
                                            track.baseline.debug_midi_note_ons);
        track.midi_note_offs = counter_delta(track.info.debug_midi_note_offs,
                                             track.baseline.debug_midi_note_offs);
        track.midi_control_changes = counter_delta(track.info.debug_midi_control_changes,
                                                   track.baseline.debug_midi_control_changes);
        track.midi_program_changes = counter_delta(track.info.debug_midi_program_changes,
                                                   track.baseline.debug_midi_program_changes);
        track.midi_pitch_bends = counter_delta(track.info.debug_midi_pitch_bends,
                                               track.baseline.debug_midi_pitch_bends);
        track.midi_irq_count = counter_delta(track.info.debug_midi_irq_count,
                                             track.baseline.debug_midi_irq_count);
        track.midi_synth_frames = counter_delta(track.info.debug_midi_synth_frames,
                                                track.baseline.debug_midi_synth_frames);

        if (track.result == "not-run") {
            if (track.unsupported_opcodes != 0) {
                track.result = "cpu-error";
                worst_severity = std::max(worst_severity, 3);
            } else if (control_track) {
                track.result = track.audio.peak <= options.silence_peak
                    ? "control-silent"
                    : "control-tail";
                any_control = true;
            } else if (track.audio.peak <= options.silence_peak) {
                track.result = "silent";
                any_silent = true;
            } else if (track.info.warning[0] != '\0') {
                track.result = "warning";
                any_warning = true;
                any_audio = true;
            } else {
                track.result = "audio-active";
                any_audio = true;
            }
        }
        result.tracks.push_back(std::move(track));
    }

    switch (worst_severity) {
    case 3: result.outcome = "cpu-error"; break;
    case 4: result.outcome = "track-error"; break;
    case 5: result.outcome = "render-error"; break;
    case 6: result.outcome = "timeout"; break;
    default:
        if (any_warning) {
            result.outcome = "warning";
        } else if (any_audio && any_silent) {
            result.outcome = "partial-silent";
        } else if (any_audio) {
            result.outcome = "audio-active";
        } else if (any_silent) {
            result.outcome = "silent";
        } else if (any_control) {
            result.outcome = "control-only";
        } else {
            result.outcome = "error";
        }
        break;
    }
    return result;
}

int outcome_exit_code(const EntryScan& scan)
{
    if (scan.outcome == "audio-active") return ExitPass;
    if (scan.outcome == "partial-silent") return ExitPartialSilent;
    if (scan.outcome == "control-only") return ExitControlOnly;
    if (scan.outcome == "silent") return ExitSilent;
    if (scan.outcome == "load-error") return ExitLoadError;
    if (scan.outcome == "track-error") return ExitTrackError;
    if (scan.outcome == "render-error") return ExitRenderError;
    if (scan.outcome == "cpu-error") return ExitCpuError;
    if (scan.outcome == "warning") return ExitWarning;
    if (scan.outcome == "no-tracks") return ExitNoTracks;
    if (scan.outcome == "timeout") return ExitTimeout;
    return ExitInternalError;
}

void append_track_json(std::ostringstream& out, const TrackScan& track)
{
    const int64_t first_audible_frame = track.audio.first_audible_sample < 0
        ? -1
        : track.audio.first_audible_sample / 2;
    out << "{\"index\":" << track.index
        << ",\"code\":" << track.code
        << ",\"title\":" << quoted(track.title)
        << ",\"result\":" << quoted(track.result)
        << ",\"error\":" << quoted(track.error)
        << ",\"rendered_frames\":" << track.rendered_frames
        << ",\"first_audible_frame\":" << first_audible_frame
        << ",\"elapsed_ms\":" << track.elapsed_ms
        << ",\"audio\":{\"peak\":" << track.audio.peak
        << ",\"rms\":" << std::fixed << std::setprecision(3) << track.audio.rms()
        << ",\"nonzero_samples\":" << track.audio.nonzero_samples
        << ",\"clipped_samples\":" << track.audio.clipped_samples << "}"
        << ",\"diagnostics\":{\"cpu_cycles_or_steps\":" << track.cpu_cycles_or_steps
        << ",\"unsupported_opcodes\":" << track.unsupported_opcodes
        << ",\"last_unsupported_opcode\":" << last_unsupported_opcode(track.info)
        << ",\"last_unsupported_cs\":" << track.info.debug_last_unsupported_cs
        << ",\"last_unsupported_ip\":" << track.info.debug_last_unsupported_ip
        << ",\"pc\":" << track.info.debug_pc
        << ",\"io_reads\":" << track.io_reads
        << ",\"io_writes\":" << track.io_writes
        << ",\"chip_writes\":" << track.chip_writes
        << ",\"keyons\":" << track.keyons
        << ",\"last_chip_register\":" << track.info.debug_last_opn_reg
        << ",\"last_chip_data\":" << track.info.debug_last_opn_data
        << ",\"warning\":" << quoted(track.info.warning)
        << ",\"driver_debug\":{\"slot_00\":" << track.info.debug_port_writes_00
        << ",\"slot_01\":" << track.info.debug_port_writes_01
        << ",\"slot_02\":" << track.info.debug_port_writes_02
        << ",\"slot_03\":" << track.info.debug_port_writes_03
        << ",\"slot_32\":" << track.info.debug_port_writes_32
        << ",\"slot_44\":" << track.info.debug_port_writes_44
        << ",\"slot_45\":" << track.info.debug_port_writes_45
        << "},\"x68k_startup\":{\"policy\":"
        << quoted(track.info.debug_x68k_startup_policy == 0 ? "auto"
                  : track.info.debug_x68k_startup_policy == 1 ? "native" : "hoot")
        << ",\"resolved\":"
        << quoted(track.info.debug_x68k_startup_mode == 0 ? "native" : "hoot")
        << ",\"fallbacks\":"
        << counter_delta(track.info.debug_x68k_startup_fallbacks,
                         track.baseline.debug_x68k_startup_fallbacks)
        << ",\"mailbox_pending\":" << track.info.debug_x68k_mailbox_pending
        << "},\"pcm8\":{\"commands\":" << track.pcm8_commands
        << ",\"starts\":" << track.pcm8_starts
        << ",\"stops\":" << track.pcm8_stops
        << ",\"mode_changes\":" << counter_delta(track.info.debug_pcm8_mode_changes, track.baseline.debug_pcm8_mode_changes)
        << ",\"queries\":" << counter_delta(track.info.debug_pcm8_queries, track.baseline.debug_pcm8_queries)
        << ",\"unimplemented\":" << track.pcm8_unimplemented
        << ",\"unknown\":" << track.pcm8_unknown
        << ",\"unsupported_channels\":" << track.pcm8_unsupported_channels
        << ",\"rendered_voice_frames\":" << track.pcm8_rendered_voice_frames
        << ",\"rendered_source_bytes\":" << track.pcm8_rendered_source_bytes
        << ",\"completed_voices\":" << track.pcm8_completed_voices
        << ",\"memory_faults\":" << track.pcm8_memory_faults
        << ",\"active_voices\":" << track.info.debug_pcm8_active_voices
        << ",\"last_d0\":" << track.info.debug_pcm8_last_d0
        << ",\"last_d1\":" << track.info.debug_pcm8_last_d1
        << ",\"last_d2\":" << track.info.debug_pcm8_last_d2
        << ",\"last_a1\":" << track.info.debug_pcm8_last_a1
        << ",\"last_kind\":" << track.info.debug_pcm8_last_kind
        << ",\"last_channel\":" << track.info.debug_pcm8_last_channel
        << "},\"pcm86\":{\"port_writes\":" << counter_delta(track.info.debug_pcm86_port_writes, track.baseline.debug_pcm86_port_writes)
        << ",\"fifo_writes\":" << counter_delta(track.info.debug_pcm86_fifo_writes, track.baseline.debug_pcm86_fifo_writes)
        << ",\"fifo_reads\":" << counter_delta(track.info.debug_pcm86_fifo_reads, track.baseline.debug_pcm86_fifo_reads)
        << ",\"rendered_frames\":" << counter_delta(track.info.debug_pcm86_rendered_frames, track.baseline.debug_pcm86_rendered_frames)
        << ",\"rendered_source_frames\":" << counter_delta(track.info.debug_pcm86_rendered_source_frames, track.baseline.debug_pcm86_rendered_source_frames)
        << ",\"irq_requests\":" << counter_delta(track.info.debug_pcm86_irq_requests, track.baseline.debug_pcm86_irq_requests)
        << ",\"irq_deliveries\":" << counter_delta(track.info.debug_pcm86_irq_deliveries, track.baseline.debug_pcm86_irq_deliveries)
        << ",\"fifo_overflows\":" << counter_delta(track.info.debug_pcm86_fifo_overflows, track.baseline.debug_pcm86_fifo_overflows)
        << ",\"fifo_bytes\":" << track.info.debug_pcm86_fifo_bytes
        << ",\"peak_fifo_bytes\":" << track.info.debug_pcm86_peak_fifo_bytes
        << ",\"fifo_threshold\":" << track.info.debug_pcm86_fifo_threshold
        << ",\"fifo_control\":" << track.info.debug_pcm86_fifo_control
        << ",\"dac_control\":" << track.info.debug_pcm86_dac_control
        << ",\"volume_code\":" << track.info.debug_pcm86_volume_code
        << ",\"source_rate_millihz\":" << track.info.debug_pcm86_source_rate_millihz
        << "},\"beep\":{\"pit_data_writes\":" << counter_delta(track.info.debug_beep_pit_data_writes, track.baseline.debug_beep_pit_data_writes)
        << ",\"pit_control_writes\":" << counter_delta(track.info.debug_beep_pit_control_writes, track.baseline.debug_beep_pit_control_writes)
        << ",\"ppi_writes\":" << counter_delta(track.info.debug_beep_ppi_writes, track.baseline.debug_beep_ppi_writes)
        << ",\"gate_changes\":" << counter_delta(track.info.debug_beep_gate_changes, track.baseline.debug_beep_gate_changes)
        << ",\"divider_changes\":" << counter_delta(track.info.debug_beep_divider_changes, track.baseline.debug_beep_divider_changes)
        << ",\"rendered_frames\":" << counter_delta(track.info.debug_beep_rendered_frames, track.baseline.debug_beep_rendered_frames)
        << ",\"audible_frames\":" << counter_delta(track.info.debug_beep_audible_frames, track.baseline.debug_beep_audible_frames)
        << ",\"vrtc_irqs\":" << counter_delta(track.info.debug_beep_vrtc_irqs, track.baseline.debug_beep_vrtc_irqs)
        << ",\"divider\":" << track.info.debug_beep_divider
        << ",\"frequency_millihz\":" << track.info.debug_beep_frequency_millihz
        << ",\"enabled\":" << track.info.debug_beep_enabled
        << ",\"mode\":" << track.info.debug_beep_mode
        << ",\"min_divider\":" << track.info.debug_beep_min_divider
        << ",\"max_divider\":" << track.info.debug_beep_max_divider
        << "},\"midi\":{\"midiout_type\":" << track.info.debug_midiout_type
        << ",\"class\":" << quoted(midiout_class_name(track.info.debug_midiout_type))
        << ",\"backend_active\":" << track.info.debug_midi_backend_active
        << ",\"backend\":" << quoted(midi_backend_name(track.info.debug_midi_backend_kind))
        << ",\"bytes_enqueued\":" << track.midi_bytes_enqueued
        << ",\"bytes_transmitted\":" << track.midi_bytes_transmitted
        << ",\"fifo_bytes\":" << track.info.debug_midi_fifo_bytes
        << ",\"peak_fifo_bytes\":" << track.info.debug_midi_peak_fifo_bytes
        << ",\"channel_messages\":" << track.midi_channel_messages
        << ",\"system_common_messages\":" << track.midi_system_common_messages
        << ",\"sysex_messages\":" << track.midi_sysex_messages
        << ",\"sysex_bytes\":" << track.midi_sysex_bytes
        << ",\"running_status_messages\":" << track.midi_running_status_messages
        << ",\"malformed_bytes\":" << track.midi_malformed_bytes
        << ",\"note_ons\":" << track.midi_note_ons
        << ",\"note_offs\":" << track.midi_note_offs
        << ",\"control_changes\":" << track.midi_control_changes
        << ",\"program_changes\":" << track.midi_program_changes
        << ",\"pitch_bends\":" << track.midi_pitch_bends
        << ",\"irq_count\":" << track.midi_irq_count
        << ",\"synth_frames\":" << track.midi_synth_frames
        << ",\"sysex_handled\":" << counter_delta(track.info.debug_midi_sysex_handled, track.baseline.debug_midi_sysex_handled)
        << ",\"last_status\":" << track.info.debug_midi_last_status
        << "}}}";
}

std::string scan_json(const EntryScan& scan)
{
    std::ostringstream out;
    out << "{\"outcome\":" << quoted(scan.outcome)
        << ",\"load_error\":" << quoted(scan.load_error)
        << ",\"load_ms\":" << scan.load_ms
        << ",\"tracks\":[";
    for (size_t i = 0; i < scan.tracks.size(); ++i) {
        if (i != 0) out << ',';
        append_track_json(out, scan.tracks[i]);
    }
    out << "]}";
    return out.str();
}

std::string static_scan_json(const std::string& outcome, const std::string& error)
{
    return "{\"outcome\":" + quoted(outcome)
        + ",\"load_error\":" + quoted(error)
        + ",\"load_ms\":0,\"tracks\":[]}";
}

struct IsolatedResult {
    std::string json;
    std::string outcome;
    bool crashed = false;
    bool timed_out = false;
    int signal = 0;
};

std::string temporary_result_path(size_t ordinal)
{
    std::filesystem::path base = std::filesystem::temp_directory_path();
    std::ostringstream name;
#if defined(_WIN32)
    name << "hootprobe-" << ordinal << ".json";
#else
    name << "hootprobe-" << static_cast<long long>(::getpid()) << '-' << ordinal << ".json";
#endif
    return (base / name.str()).string();
}

IsolatedResult run_isolated(const hoot::HootEntry& entry,
                            const Options& options,
                            size_t ordinal)
{
#if defined(_WIN32)
    const auto scan = scan_entry(entry, options);
    return {scan_json(scan), scan.outcome, false, false, 0};
#else
    if (!options.isolate) {
        const auto scan = scan_entry(entry, options);
        return {scan_json(scan), scan.outcome, false, false, 0};
    }

    const auto path = temporary_result_path(ordinal);
    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    const pid_t pid = fork();
    if (pid < 0) {
        return {static_scan_json("internal-error", "fork failed"), "internal-error", false, false, 0};
    }
    if (pid == 0) {
        const auto scan = scan_entry(entry, options);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << scan_json(scan);
        file.close();
        _exit(outcome_exit_code(scan));
    }

    const size_t planned_tracks = options.track_index >= 0
        ? 1
        : (options.max_tracks == 0
            ? std::max<size_t>(1, entry.tracks.size())
            : std::max<size_t>(1, std::min(entry.tracks.size(), static_cast<size_t>(options.max_tracks))));
    const auto hard_timeout = std::chrono::seconds(5)
        + std::chrono::seconds(options.timeout_seconds * static_cast<int>(planned_tracks));
    const auto deadline = Clock::now() + hard_timeout;
    int status = 0;
    while (true) {
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            break;
        }
        if (done < 0) {
            return {static_scan_json("internal-error", "waitpid failed"), "internal-error", false, false, 0};
        }
        if (Clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            std::filesystem::remove(path, remove_error);
            return {static_scan_json("timeout", "entry exceeded hard process timeout"),
                    "timeout", false, true, SIGKILL};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (WIFSIGNALED(status)) {
        const int signal = WTERMSIG(status);
        std::filesystem::remove(path, remove_error);
        return {static_scan_json("crash", "child process terminated by signal " + std::to_string(signal)),
                "crash", true, false, signal};
    }

    std::ifstream file(path, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    std::filesystem::remove(path, remove_error);
    if (content.str().empty()) {
        return {static_scan_json("internal-error", "child produced no result"),
                "internal-error", false, false, 0};
    }

    std::string outcome = "error";
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : ExitInternalError;
    switch (code) {
    case ExitPass: outcome = "audio-active"; break;
    case ExitPartialSilent: outcome = "partial-silent"; break;
    case ExitControlOnly: outcome = "control-only"; break;
    case ExitSilent: outcome = "silent"; break;
    case ExitLoadError: outcome = "load-error"; break;
    case ExitTrackError: outcome = "track-error"; break;
    case ExitRenderError: outcome = "render-error"; break;
    case ExitCpuError: outcome = "cpu-error"; break;
    case ExitWarning: outcome = "warning"; break;
    case ExitNoTracks: outcome = "no-tracks"; break;
    case ExitTimeout: outcome = "timeout"; break;
    default: outcome = "internal-error"; break;
    }
    return {content.str(), outcome, false, false, 0};
#endif
}

std::string entry_json_prefix(const hoot::HootEntry& entry,
                              const hoot::DriverProbeResult& probe,
                              const hoot::EntryAssetValidation& validation)
{
    std::ostringstream out;
    out << "{\"id\":" << quoted(entry.id)
        << ",\"title\":" << quoted(entry.title)
        << ",\"archive\":" << quoted(entry.archive)
        << ",\"catalog_driver\":" << quoted(entry.driver_name)
        << ",\"driver_alias\":" << quoted(entry.driver_alias)
        << ",\"track_count\":" << entry.tracks.size()
        << ",\"capability\":{\"status\":"
        << quoted(hoot::driver_support_status_name(probe.status))
        << ",\"replay_host\":" << quoted(probe.driver_id)
        << ",\"reason\":" << quoted(probe.reason) << "}"
        << ",\"assets\":{\"archive_required\":" << (validation.archive_required ? "true" : "false")
        << ",\"archive_present\":" << (validation.archive_present ? "true" : "false")
        << ",\"archive_path\":" << quoted(validation.archive_path.string())
        << ",\"error\":" << quoted(validation.error)
        << ",\"missing\":[";
    for (size_t i = 0; i < validation.missing_assets.size(); ++i) {
        if (i != 0) out << ',';
        out << quoted(validation.missing_assets[i]);
    }
    out << "]},\"scan\":";
    return out.str();
}

} // namespace

int main(int argc, char** argv)
{
    hoot::enable_utf8_console();
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage(argv[0]);
        return 2;
    }

    hoot::HootCatalog catalog;
    hoot::HootCatalogLoader loader;
    std::string error;
    if (!loader.load_file(options.catalog, catalog, error)) {
        std::cerr << "hootprobe: " << error << "\n";
        return 1;
    }

    std::ofstream report(options.output, std::ios::binary | std::ios::trunc);
    if (!report) {
        std::cerr << "hootprobe: unable to open output: " << options.output << "\n";
        return 1;
    }

    std::map<std::string, uint64_t> outcomes;
    std::map<std::string, uint64_t> capabilities;
    uint64_t selected = 0;
    uint64_t tested = 0;
    uint64_t skipped_missing = 0;
    uint64_t missing_assets = 0;

    report << "{\n  \"schema_version\": 1,\n"
           << "  \"generated_utc\": " << quoted(utc_timestamp()) << ",\n"
           << "  \"catalog\": " << quoted(options.catalog) << ",\n"
           << "  \"packs\": " << quoted(options.packs) << ",\n"
           << "  \"settings\": {\"seconds\":" << options.seconds
           << ",\"startup_grace_seconds\":" << options.startup_grace_seconds
           << ",\"sample_rate\":" << options.rate
           << ",\"max_tracks\":" << options.max_tracks
           << ",\"track_index\":" << options.track_index
           << ",\"per_track_timeout_seconds\":" << options.timeout_seconds
           << ",\"silence_peak\":" << options.silence_peak
           << ",\"process_isolation\":" << (options.isolate ? "true" : "false") << "},\n"
           << "  \"entries\": [\n";

    bool first = true;
    size_t ordinal = 0;
    for (const auto& entry : catalog.entries()) {
        if (!options.entry.empty() && entry.id != options.entry) {
            continue;
        }
        if (!options.archive.empty() && entry.archive != options.archive) {
            continue;
        }
        if (!options.catalog_driver.empty() && entry.driver_name != options.catalog_driver) {
            continue;
        }
        ++selected;

        const auto probe = hoot::DriverRegistry::instance().probe(entry);
        const auto capability_name = std::string(hoot::driver_support_status_name(probe.status));
        ++capabilities[capability_name];
        const auto validation = hoot::validate_entry_assets(entry, options.packs);
        if (!validation.missing_assets.empty()) {
            ++missing_assets;
        }
        const bool targeted = !options.entry.empty() || !options.archive.empty();
        if (!options.include_missing && !targeted
            && validation.archive_required && !validation.archive_present) {
            ++skipped_missing;
            continue;
        }

        std::string scan;
        std::string outcome;
        if (!probe.supported()) {
            outcome = "unsupported";
            scan = static_scan_json(outcome, probe.reason);
        } else if (!validation.ok()) {
            if (!validation.archive_present) {
                outcome = "missing-archive";
            } else if (!validation.error.empty()) {
                outcome = "archive-error";
            } else {
                outcome = "missing-assets";
            }
            scan = static_scan_json(outcome,
                validation.error.empty() ? "one or more catalog assets are missing" : validation.error);
        } else {
            const auto isolated = run_isolated(entry, options, ordinal);
            scan = isolated.json;
            outcome = isolated.outcome;
            ++tested;
        }
        ++outcomes[outcome];

        if (options.progress) {
            std::cerr << '[' << (ordinal + 1) << "] " << entry.id
                      << " capability=" << capability_name
                      << " result=" << outcome << "\n";
        }

        if (!first) report << ",\n";
        first = false;
        report << "    " << entry_json_prefix(entry, probe, validation) << scan << '}';
        ++ordinal;
    }

    report << "\n  ],\n  \"summary\": {\n"
           << "    \"catalog_entries\": " << catalog.entries().size() << ",\n"
           << "    \"selected_entries\": " << selected << ",\n"
           << "    \"reported_entries\": " << ordinal << ",\n"
           << "    \"executed_entries\": " << tested << ",\n"
           << "    \"skipped_missing_archives\": " << skipped_missing << ",\n"
           << "    \"entries_with_missing_assets\": " << missing_assets << ",\n"
           << "    \"capabilities\": {";
    bool first_count = true;
    for (const auto& pair : capabilities) {
        if (!first_count) report << ',';
        first_count = false;
        report << quoted(pair.first) << ':' << pair.second;
    }
    report << "},\n    \"outcomes\": {";
    first_count = true;
    for (const auto& pair : outcomes) {
        if (!first_count) report << ',';
        first_count = false;
        report << quoted(pair.first) << ':' << pair.second;
    }
    report << "}\n  }\n}\n";
    report.close();

    if (options.progress) {
        std::cerr << "hootprobe: wrote " << options.output
                  << " (reported=" << ordinal << ", executed=" << tested
                  << ", missing archives skipped=" << skipped_missing << ")\n";
    }
    if (selected == 0) {
        std::cerr << "hootprobe: no catalogue entries matched the requested filter\n";
        return 1;
    }
    return 0;
}
