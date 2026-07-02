#include "config/hoot_catalog.h"

namespace hoot {

void HootCatalog::clear()
{
    entries_.clear();
}

void HootCatalog::add_entry(HootEntry entry)
{
    entries_.push_back(std::move(entry));
}

const std::vector<HootEntry>& HootCatalog::entries() const
{
    return entries_;
}

const HootEntry* HootCatalog::find(std::string_view id) const
{
    for (const auto& entry : entries_) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace hoot
