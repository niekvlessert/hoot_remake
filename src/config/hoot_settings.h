#pragma once

#include <map>
#include <string>
#include <vector>

namespace hoot {

enum class HootSettingKind {
    Text,
    Boolean,
    Integer,
    Number,
    Path,
    Directory,
    Choice
};

struct HootSettingSpec {
    const char* section;
    const char* key;
    const char* label;
    HootSettingKind kind;
    const char* default_value;
    const char* choices;      // pipe-separated for Choice, otherwise empty
    const char* description;
};

struct HootSettingValue {
    const HootSettingSpec* spec = nullptr;
    bool enabled = false;
    std::string value;
};

struct HootSettingsDocument {
    std::vector<HootSettingValue> values;
    std::map<std::string, std::string> environment;
};

const std::vector<HootSettingSpec>& hoot_setting_specs();
void reset_hoot_settings(HootSettingsDocument& document);
bool load_hoot_settings_document(const std::string& path,
                                 HootSettingsDocument& document,
                                 std::string& error);
bool save_hoot_settings_document(const std::string& path,
                                 const HootSettingsDocument& document,
                                 std::string& error);

} // namespace hoot
