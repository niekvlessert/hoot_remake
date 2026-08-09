#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/hoot_catalog.h"
#include "drivers/hoot_driver.h"

namespace hoot {

enum class DriverSupportStatus {
    Unsupported = 0,
    Recognized = 1,
    Experimental = 2,
    Playable = 3,
    Verified = 4
};

struct DriverProbeResult {
    DriverSupportStatus status = DriverSupportStatus::Unsupported;
    std::string driver_id;
    std::string reason;

    bool supported() const { return status != DriverSupportStatus::Unsupported; }
};

const char* driver_support_status_name(DriverSupportStatus status);

class DriverRegistry {
public:
    using Factory = std::function<std::unique_ptr<HootDriver>()>;
    using Probe = std::function<DriverProbeResult(const HootEntry&)>;

    struct Registration {
        std::string id;
        Probe probe;
        Factory factory;
    };

    static const DriverRegistry& instance();

    DriverProbeResult probe(const HootEntry& entry) const;
    std::unique_ptr<HootDriver> create(const HootEntry& entry) const;
    const std::vector<Registration>& registrations() const;

private:
    DriverRegistry();

    std::vector<Registration> registrations_;
};

} // namespace hoot
