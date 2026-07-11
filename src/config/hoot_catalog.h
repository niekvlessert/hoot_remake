#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hoot {

struct HootAssetRef {
    std::string type;
    std::string path;
    std::string transform;
    uint32_t offset = 0;
};

struct CatalogTrack {
    uint32_t code = 0;
    std::string title;
    std::string voice_bank;
};

struct HootEntry {
    std::string id;
    std::string title;
    std::string driver_name;
    std::string driver_type;
    std::string driver_alias;
    std::map<std::string, int> options;
    std::string archive;
    std::vector<HootAssetRef> assets;
    std::vector<CatalogTrack> tracks;
    int default_sample_rate = 44100;
    int refresh_hz = 60;
};

class HootCatalog {
public:
    void clear();
    void add_entry(HootEntry entry);

    const std::vector<HootEntry>& entries() const;
    std::vector<HootEntry>& mutable_entries();
    const HootEntry* find(std::string_view id) const;

private:
    std::vector<HootEntry> entries_;
};

} // namespace hoot
