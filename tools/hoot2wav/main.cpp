#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "config/hoot_xml_loader.h"
#include "core/hoot_api.h"
#include "wav_writer.h"

namespace {

struct Options {
    std::string catalog;
    std::string packs = ".";
    std::string entry;
    std::string out;
    int track = 0;
    int seconds = 30;
    int rate = 44100;
    bool list = false;
    bool verbose = false;
    std::string trace_xak2;
    std::string trace_pc98;
    int trace_limit = 0;
};

void usage()
{
    std::cerr
        << "usage: hoot2wav --catalog <path> [--packs <path>] [--entry <id>]\n"
        << "                [--track <n>] [--seconds <n>] [--rate <hz>]\n"
        << "                [--out <path>] [--list] [--verbose]\n"
        << "                [--trace-xak2 <path>] [--trace-pc98 <path>]\n"
        << "                [--trace-limit <n>]\n";
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
        } else if (arg == "--entry" && need_value(argc, argv, i)) {
            options.entry = argv[++i];
        } else if (arg == "--track" && need_value(argc, argv, i)) {
            options.track = std::atoi(argv[++i]);
        } else if (arg == "--seconds" && need_value(argc, argv, i)) {
            options.seconds = std::atoi(argv[++i]);
        } else if (arg == "--rate" && need_value(argc, argv, i)) {
            options.rate = std::atoi(argv[++i]);
        } else if (arg == "--out" && need_value(argc, argv, i)) {
            options.out = argv[++i];
        } else if (arg == "--list") {
            options.list = true;
        } else if (arg == "--verbose") {
            options.verbose = true;
        } else if (arg == "--trace-xak2" && need_value(argc, argv, i)) {
            options.trace_xak2 = argv[++i];
        } else if (arg == "--trace-pc98" && need_value(argc, argv, i)) {
            options.trace_pc98 = argv[++i];
        } else if (arg == "--trace-limit" && need_value(argc, argv, i)) {
            options.trace_limit = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        } else {
            std::cerr << "unknown option: " << arg << "\n";
            return false;
        }
    }
    return true;
}

int list_entries(const std::string& catalog_path)
{
    hoot::HootCatalog catalog;
    hoot::HootXmlLoader loader;
    std::string error;
    if (!loader.load_file(catalog_path, catalog, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    for (const auto& entry : catalog.entries()) {
        std::cout << entry.id << "\t" << entry.title << "\t" << entry.driver_name << "\n";
        for (size_t i = 0; i < entry.tracks.size(); ++i) {
            std::cout << "  [" << i << "] " << entry.tracks[i].title << "\n";
        }
    }
    return 0;
}

void print_track_info(const Options& options, const HootTrackInfo& info)
{
    std::cerr << "entry=" << options.entry
              << " track=" << info.track_index
              << " title=\"" << info.title << "\""
              << " driver=\"" << info.driver << "\""
              << " packs=\"" << options.packs << "\""
              << " rate=" << info.sample_rate
              << " pc=0x" << std::hex << info.debug_pc << std::dec
              << " cycles=" << info.debug_cpu_cycles
              << " io-read=" << info.debug_io_reads
              << " io-write=" << info.debug_io_writes
              << " opn-write=" << info.debug_opn_writes
              << " opn-keyon=" << info.debug_opn_keyons
              << " last-opn=0x" << std::hex << info.debug_last_opn_reg
              << ":0x" << info.debug_last_opn_data << std::dec
              << " writes[00,01,02,03,32,44,45]="
              << info.debug_port_writes_00 << ","
              << info.debug_port_writes_01 << ","
              << info.debug_port_writes_02 << ","
              << info.debug_port_writes_03 << ","
              << info.debug_port_writes_32 << ","
              << info.debug_port_writes_44 << ","
              << info.debug_port_writes_45 << "\n";
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        usage();
        return 2;
    }

    if (options.catalog.empty()) {
        std::cerr << "--catalog is required\n";
        usage();
        return 2;
    }

    if (options.list) {
        return list_entries(options.catalog);
    }

    if (options.entry.empty() || options.out.empty()) {
        std::cerr << "--entry and --out are required unless --list is used\n";
        usage();
        return 2;
    }
    if (options.seconds <= 0 || options.rate <= 0) {
        std::cerr << "--seconds and --rate must be positive\n";
        return 2;
    }

    HootConfig config{};
    config.sample_rate = options.rate;
    config.packs_path = options.packs.c_str();

    if (!options.trace_xak2.empty()) {
        setenv("HOOT_XAK2_TRACE", options.trace_xak2.c_str(), 1);
        if (options.trace_limit > 0) {
            setenv("HOOT_XAK2_TRACE_LIMIT", std::to_string(options.trace_limit).c_str(), 1);
        }
    }
    if (!options.trace_pc98.empty()) {
        setenv("HOOT_PC98_TRACE", options.trace_pc98.c_str(), 1);
        if (options.trace_limit > 0) {
            setenv("HOOT_PC98_TRACE_LIMIT", std::to_string(options.trace_limit).c_str(), 1);
        }
    }

    std::unique_ptr<HootContext, decltype(&hoot_destroy)> ctx(
        hoot_create(&config), hoot_destroy);
    if (!ctx) {
        std::cerr << "unable to create Hoot context\n";
        return 1;
    }

    if (hoot_load_xml(ctx.get(), options.catalog.c_str()) != HOOT_OK) {
        std::cerr << hoot_last_error(ctx.get()) << "\n";
        return 1;
    }
    if (hoot_load_entry(ctx.get(), options.entry.c_str()) != HOOT_OK) {
        std::cerr << hoot_last_error(ctx.get()) << "\n";
        return 1;
    }
    if (hoot_select_track(ctx.get(), options.track) != HOOT_OK) {
        std::cerr << hoot_last_error(ctx.get()) << "\n";
        return 1;
    }

    const int frames = options.seconds * options.rate;
    std::vector<int16_t> pcm(static_cast<size_t>(frames) * 2);
    const int rendered = hoot_render_s16(ctx.get(), pcm.data(), frames);
    if (rendered != frames) {
        std::cerr << "render failed\n";
        return 1;
    }

    std::string error;
    if (!write_wav_s16(options.out, pcm.data(), frames, options.rate, error)) {
        std::cerr << error << "\n";
        return 1;
    }

    if (options.verbose) {
        HootTrackInfo info{};
        if (hoot_get_track_info(ctx.get(), &info) == HOOT_OK) {
            print_track_info(options, info);
        }
        int peak = 0;
        long double sum_squares = 0.0;
        for (const auto sample : pcm) {
            const int value = std::abs(static_cast<int>(sample));
            peak = std::max(peak, value);
            sum_squares += static_cast<long double>(sample) * static_cast<long double>(sample);
        }
        const auto rms = pcm.empty()
            ? 0.0L
            : std::sqrt(sum_squares / static_cast<long double>(pcm.size()));
        std::cerr << "audio peak=" << peak << " rms=" << static_cast<double>(rms) << "\n";
        std::cerr << "wrote " << options.out << " (" << frames << " frames)\n";
    }
    return 0;
}
