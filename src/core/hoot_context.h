#pragma once

#include <memory>
#include <string>

#include "config/hoot_catalog.h"
#include "core/hoot_api.h"
#include "drivers/hoot_driver.h"
#include "io/asset_provider.h"

struct HootContext {
    explicit HootContext(const HootConfig* config);

    void set_error(std::string message);

    int sample_rate = 44100;
    int selected_track = 0;
    std::string packs_path = ".";
    std::string last_error;
    hoot::HootCatalog catalog;
    const hoot::HootEntry* current_entry = nullptr;
    std::unique_ptr<hoot::HootDriver> current_driver;
    std::unique_ptr<hoot::AssetProvider> asset_provider;
};
