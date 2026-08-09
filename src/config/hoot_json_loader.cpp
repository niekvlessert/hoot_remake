#include "config/hoot_json_loader.h"
#include "config/hoot_user_overrides.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <utility>
#include <variant>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace hoot {
namespace {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, int64_t, double, std::string, Array, Object>;

    JsonValue() : value_(nullptr) {}
    explicit JsonValue(Storage value) : value_(std::move(value)) {}

    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_integer() const { return std::holds_alternative<int64_t>(value_); }
    bool is_double() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<Array>(value_); }
    bool is_object() const { return std::holds_alternative<Object>(value_); }

    const std::string& string() const { return std::get<std::string>(value_); }
    const Array& array() const { return std::get<Array>(value_); }
    const Object& object() const { return std::get<Object>(value_); }
    int64_t integer() const { return std::get<int64_t>(value_); }
    double number() const {
        if (is_integer()) {
            return static_cast<double>(integer());
        }
        return std::get<double>(value_);
    }
    bool boolean() const { return std::get<bool>(value_); }

    const JsonValue* get(std::string_view key) const {
        if (!is_object()) {
            return nullptr;
        }
        const auto it = object().find(std::string(key));
        return it == object().end() ? nullptr : &it->second;
    }

private:
    Storage value_;
};

void append_utf8(std::string& out, uint32_t codepoint)
{
    if (codepoint <= 0x7f) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {}

    bool parse(JsonValue& value, std::string& error)
    {
        skip_space();
        if (!parse_value(value)) {
            error = error_message();
            return false;
        }
        skip_space();
        if (position_ != input_.size()) {
            fail("unexpected trailing data");
            error = error_message();
            return false;
        }
        return true;
    }

private:
    bool parse_value(JsonValue& value)
    {
        skip_space();
        if (position_ >= input_.size()) {
            return fail("unexpected end of input");
        }
        const char ch = input_[position_];
        if (ch == '{') return parse_object(value);
        if (ch == '[') return parse_array(value);
        if (ch == '"') {
            std::string result;
            if (!parse_string(result)) return false;
            value = JsonValue(std::move(result));
            return true;
        }
        if (ch == 't') return parse_literal("true", JsonValue(true), value);
        if (ch == 'f') return parse_literal("false", JsonValue(false), value);
        if (ch == 'n') return parse_literal("null", JsonValue(nullptr), value);
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return parse_number(value);
        }
        return fail("unexpected character");
    }

    bool parse_object(JsonValue& value)
    {
        ++position_;
        JsonValue::Object object;
        skip_space();
        if (consume('}')) {
            value = JsonValue(std::move(object));
            return true;
        }
        while (position_ < input_.size()) {
            std::string key;
            if (!parse_string(key)) return false;
            skip_space();
            if (!consume(':')) return fail("expected ':' after object key");
            JsonValue item;
            if (!parse_value(item)) return false;
            object[std::move(key)] = std::move(item);
            skip_space();
            if (consume('}')) {
                value = JsonValue(std::move(object));
                return true;
            }
            if (!consume(',')) return fail("expected ',' or '}' in object");
            skip_space();
        }
        return fail("unterminated object");
    }

    bool parse_array(JsonValue& value)
    {
        ++position_;
        JsonValue::Array array;
        skip_space();
        if (consume(']')) {
            value = JsonValue(std::move(array));
            return true;
        }
        while (position_ < input_.size()) {
            JsonValue item;
            if (!parse_value(item)) return false;
            array.push_back(std::move(item));
            skip_space();
            if (consume(']')) {
                value = JsonValue(std::move(array));
                return true;
            }
            if (!consume(',')) return fail("expected ',' or ']' in array");
            skip_space();
        }
        return fail("unterminated array");
    }

    bool parse_string(std::string& out)
    {
        skip_space();
        if (!consume('"')) return fail("expected string");
        while (position_ < input_.size()) {
            const unsigned char ch = static_cast<unsigned char>(input_[position_++]);
            if (ch == '"') return true;
            if (ch < 0x20) return fail("control character in string");
            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }
            if (position_ >= input_.size()) return fail("unterminated string escape");
            const char escape = input_[position_++];
            switch (escape) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                uint32_t codepoint = 0;
                if (!parse_hex4(codepoint)) return false;
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (position_ + 2 > input_.size() || input_[position_] != '\\' || input_[position_ + 1] != 'u') {
                        return fail("high surrogate without low surrogate");
                    }
                    position_ += 2;
                    uint32_t low = 0;
                    if (!parse_hex4(low) || low < 0xdc00 || low > 0xdfff) {
                        return fail("invalid low surrogate");
                    }
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                }
                append_utf8(out, codepoint);
                break;
            }
            default: return fail("invalid string escape");
            }
        }
        return fail("unterminated string");
    }

    bool parse_hex4(uint32_t& value)
    {
        if (position_ + 4 > input_.size()) return fail("short unicode escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = input_[position_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value |= static_cast<uint32_t>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value |= static_cast<uint32_t>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value |= static_cast<uint32_t>(ch - 'A' + 10);
            else return fail("invalid unicode escape");
        }
        return true;
    }

    bool parse_number(JsonValue& value)
    {
        const size_t start = position_;
        if (input_[position_] == '-') ++position_;
        if (position_ >= input_.size()) return fail("invalid number");
        if (input_[position_] == '0') {
            ++position_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(input_[position_]))) return fail("invalid number");
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        bool integer = true;
        if (position_ < input_.size() && input_[position_] == '.') {
            integer = false;
            ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) return fail("invalid fraction");
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            integer = false;
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            if (position_ >= input_.size() || !std::isdigit(static_cast<unsigned char>(input_[position_]))) return fail("invalid exponent");
            while (position_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[position_]))) ++position_;
        }
        const std::string token(input_.substr(start, position_ - start));
        try {
            if (integer) {
                value = JsonValue(static_cast<int64_t>(std::stoll(token)));
            } else {
                value = JsonValue(std::stod(token));
            }
        } catch (...) {
            return fail("number out of range");
        }
        return true;
    }

    bool parse_literal(std::string_view literal, JsonValue literal_value, JsonValue& value)
    {
        if (input_.substr(position_, literal.size()) != literal) return fail("invalid literal");
        position_ += literal.size();
        value = std::move(literal_value);
        return true;
    }

    void skip_space()
    {
        while (position_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[position_]))) ++position_;
    }

    bool consume(char expected)
    {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    bool fail(std::string message)
    {
        if (error_.empty()) error_ = std::move(message);
        return false;
    }

    std::string error_message() const
    {
        std::ostringstream stream;
        stream << (error_.empty() ? "JSON parse error" : error_)
               << " at byte " << position_;
        return stream.str();
    }

    std::string_view input_;
    size_t position_ = 0;
    std::string error_;
};

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string string_value(const JsonValue* value, std::string fallback = {})
{
    if (value == nullptr) return fallback;
    if (value->is_string()) return value->string();
    if (value->is_integer()) return std::to_string(value->integer());
    return fallback;
}

int64_t parse_integer_text(const std::string& input, int64_t fallback = 0)
{
    if (input.empty()) return fallback;
    char* end = nullptr;
    const auto value = std::strtoll(input.c_str(), &end, 0);
    if (end == input.c_str() || *end != '\0') return fallback;
    return value;
}

int64_t integer_value(const JsonValue* value, int64_t fallback = 0)
{
    if (value == nullptr) return fallback;
    if (value->is_integer()) return value->integer();
    if (value->is_double()) return static_cast<int64_t>(value->number());
    if (value->is_string()) return parse_integer_text(value->string(), fallback);
    return fallback;
}

std::string format_code(const std::string& format, uint64_t code)
{
    std::string output;
    for (size_t i = 0; i < format.size();) {
        if (format[i] != '%') {
            output.push_back(format[i++]);
            continue;
        }
        if (i + 1 < format.size() && format[i + 1] == '%') {
            output.push_back('%');
            i += 2;
            continue;
        }
        size_t j = i + 1;
        bool zero_pad = false;
        size_t width = 0;
        if (j < format.size() && format[j] == '0') {
            zero_pad = true;
            ++j;
        }
        while (j < format.size() && std::isdigit(static_cast<unsigned char>(format[j]))) {
            width = width * 10 + static_cast<size_t>(format[j] - '0');
            ++j;
        }
        if (j >= format.size() || std::string_view("xXdu").find(format[j]) == std::string_view::npos) {
            output.push_back('%');
            ++i;
            continue;
        }
        std::ostringstream rendered;
        if (zero_pad) rendered << std::setfill('0');
        if (width != 0) rendered << std::setw(static_cast<int>(width));
        if (format[j] == 'x') rendered << std::hex << std::nouppercase << code;
        else if (format[j] == 'X') rendered << std::hex << std::uppercase << code;
        else rendered << std::dec << code;
        output += rendered.str();
        i = j + 1;
    }
    return output;
}

bool parse_game(const JsonValue& game, HootCatalog& catalog, std::string& error)
{
    if (!game.is_object()) {
        error = "game entry is not an object";
        return false;
    }
    HootEntry entry;
    entry.id = string_value(game.get("id"));
    entry.title = string_value(game.get("title"));
    entry.archive = string_value(game.get("archive"));
    entry.default_sample_rate = static_cast<int>(integer_value(game.get("default_sample_rate"), 44100));
    entry.refresh_hz = static_cast<int>(integer_value(game.get("refresh_hz"), 60));

    const auto* driver = game.get("driver");
    if (driver != nullptr && driver->is_object()) {
        entry.driver_name = string_value(driver->get("name"));
        entry.driver_type = string_value(driver->get("type"));
        entry.driver_alias = string_value(driver->get("alias"));
        const auto platform = string_value(driver->get("platform"));
        if (!entry.driver_alias.empty() && !platform.empty()) {
            entry.driver_alias += "/" + platform;
        }
    }
    if (!entry.driver_type.empty()) {
        entry.driver_name += "/" + entry.driver_type;
    }

    const auto* options = game.get("options");
    if (options != nullptr && options->is_array()) {
        for (const auto& option : options->array()) {
            if (!option.is_object()) continue;
            const auto name = string_value(option.get("name"));
            if (!name.empty()) {
                entry.options[name] = static_cast<int>(integer_value(option.get("value")));
            }
        }
    }

    const auto* assets = game.get("assets");
    if (assets != nullptr && assets->is_array()) {
        for (const auto& asset_value : assets->array()) {
            if (!asset_value.is_object()) continue;
            HootAssetRef asset;
            asset.type = string_value(asset_value.get("type"));
            asset.path = string_value(asset_value.get("path"));
            asset.transform = string_value(asset_value.get("transform"));
            asset.offset = static_cast<uint32_t>(integer_value(asset_value.get("offset")));
            if (asset_value.get("crc32") != nullptr) {
                asset.crc32 = static_cast<uint32_t>(integer_value(asset_value.get("crc32")));
                asset.has_crc32 = true;
            }
            entry.assets.push_back(std::move(asset));
        }
    }

    const auto* title_entries = game.get("title_entries");
    if (title_entries != nullptr && title_entries->is_array()) {
        for (const auto& title_entry : title_entries->array()) {
            if (!title_entry.is_object()) continue;
            const auto kind = string_value(title_entry.get("kind"));
            if (kind == "title") {
                CatalogTrack track;
                track.code = static_cast<uint32_t>(integer_value(title_entry.get("code")));
                track.title = string_value(title_entry.get("title"));
                track.voice_bank = string_value(title_entry.get("voice_bank"));
                entry.tracks.push_back(std::move(track));
            } else if (kind == "range") {
                const auto minimum = integer_value(title_entry.get("min"));
                const auto maximum = integer_value(title_entry.get("max"), minimum);
                const auto format = string_value(title_entry.get("title_format"));
                const auto* start_value = title_entry.get("start");
                const bool has_start = start_value != nullptr && !start_value->is_null();
                const auto start = integer_value(start_value);
                if (maximum < minimum || static_cast<uint64_t>(maximum - minimum) > 1000000ULL) {
                    error = "invalid title range in " + entry.id;
                    return false;
                }
                for (int64_t code = minimum; code <= maximum; ++code) {
                    const auto display_code = has_start ? start + (code - minimum) : code;
                    CatalogTrack track;
                    track.code = static_cast<uint32_t>(code);
                    track.title = format_code(format, static_cast<uint64_t>(display_code));
                    entry.tracks.push_back(std::move(track));
                }
            }
        }
    }

    if (entry.id.empty() || entry.title.empty() || entry.driver_name.empty()) {
        error = "JSON game requires id, title, and driver.name";
        return false;
    }
    catalog.add_entry(std::move(entry));
    return true;
}

bool parse_games_document(const JsonValue& root, HootCatalog& catalog, std::string& error)
{
    const auto format = string_value(root.get("format"));
    if (format != "hoot-catalog-shard") {
        error = "unsupported Hoot JSON document format: " + format;
        return false;
    }
    if (integer_value(root.get("version"), 0) != 1) {
        error = "unsupported Hoot JSON version";
        return false;
    }
    const auto* games = root.get("games");
    if (games == nullptr || !games->is_array()) {
        error = "Hoot JSON shard has no games array";
        return false;
    }
    for (const auto& game : games->array()) {
        if (!parse_game(game, catalog, error)) return false;
    }
    return true;
}

bool parse_json(const std::string& json, JsonValue& root, std::string& error)
{
    JsonParser parser(json);
    if (!parser.parse(root, error)) return false;
    if (!root.is_object()) {
        error = "Hoot JSON root must be an object";
        return false;
    }
    return true;
}

bool load_recursive(const std::filesystem::path& path,
                    HootCatalog& catalog,
                    std::map<std::filesystem::path, bool>& visited,
                    std::string& error)
{
    const auto canonical = std::filesystem::weakly_canonical(path);
    if (visited[canonical]) return true;
    visited[canonical] = true;

    const auto json = read_file(canonical);
    if (json.empty()) {
        error = "unable to read JSON catalogue: " + canonical.string();
        return false;
    }
    JsonValue root;
    if (!parse_json(json, root, error)) {
        error += " in " + canonical.string();
        return false;
    }
    const auto format = string_value(root.get("format"));
    if (format == "hoot-catalog-shard") {
        return parse_games_document(root, catalog, error);
    }
    if (format != "hoot-catalog-manifest") {
        error = "unsupported Hoot JSON format in " + canonical.string() + ": " + format;
        return false;
    }
    if (integer_value(root.get("version"), 0) != 1) {
        error = "unsupported Hoot JSON manifest version";
        return false;
    }
    const auto* includes = root.get("includes");
    if (includes == nullptr || !includes->is_array()) {
        error = "Hoot JSON manifest has no includes array";
        return false;
    }
    for (const auto& include : includes->array()) {
        if (!include.is_string()) {
            error = "Hoot JSON include is not a string";
            return false;
        }
        const std::filesystem::path include_path(include.string());
        const auto resolved = include_path.is_absolute() ? include_path : canonical.parent_path() / include_path;
        if (!load_recursive(resolved, catalog, visited, error)) return false;
    }
    return true;
}

} // namespace

bool HootJsonLoader::load_file(const std::string& path, HootCatalog& catalog, std::string& error) const
{
    catalog.clear();
    std::map<std::filesystem::path, bool> visited;
    if (!load_recursive(path, catalog, visited, error)) return false;
    if (catalog.entries().empty()) {
        error = "JSON catalogue contains no games";
        return false;
    }
    return true;
}

bool HootJsonLoader::load_string(const std::string& json, HootCatalog& catalog, std::string& error) const
{
    catalog.clear();
    JsonValue root;
    if (!parse_json(json, root, error)) return false;
    if (!parse_games_document(root, catalog, error)) return false;
    if (catalog.entries().empty()) {
        error = "JSON catalogue contains no games";
        return false;
    }
    return true;
}

namespace {

std::string json_escape(const std::string& value)
{
    std::ostringstream out;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                    << std::dec << std::setfill(' ');
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

bool parse_override_entry(const std::string& id, const JsonValue& value,
                          HootEntryOverride& out, std::string& error)
{
    if (!value.is_object()) {
        error = "override for " + id + " is not an object";
        return false;
    }
    out = {};
    out.id = id;
    if (const auto* v = value.get("hidden")) {
        if (v->is_bool()) out.hidden = v->boolean();
        else out.hidden = integer_value(v, 0) != 0;
    }
    if (const auto* v = value.get("create")) {
        if (v->is_bool()) out.create = v->boolean();
        else out.create = integer_value(v, 0) != 0;
    }
    if (const auto* v = value.get("title")) { out.has_title = true; out.title = string_value(v); }
    if (const auto* v = value.get("archive")) { out.has_archive = true; out.archive = string_value(v); }
    if (const auto* v = value.get("default_sample_rate")) {
        out.has_default_sample_rate = true; out.default_sample_rate = static_cast<int>(integer_value(v, 44100));
    }
    if (const auto* v = value.get("refresh_hz")) {
        out.has_refresh_hz = true; out.refresh_hz = static_cast<int>(integer_value(v, 60));
    }
    if (const auto* driver = value.get("driver")) {
        if (!driver->is_object()) { error = "driver override for " + id + " is not an object"; return false; }
        if (const auto* v = driver->get("name")) { out.has_driver_name = true; out.driver_name = string_value(v); }
        if (const auto* v = driver->get("type")) { out.has_driver_type = true; out.driver_type = string_value(v); }
        if (const auto* v = driver->get("alias")) { out.has_driver_alias = true; out.driver_alias = string_value(v); }
    }
    if (const auto* options = value.get("options")) {
        if (!options->is_object()) { error = "options override for " + id + " is not an object"; return false; }
        out.replace_options = true;
        for (const auto& pair : options->object())
            out.options[pair.first] = static_cast<int>(integer_value(&pair.second, 0));
    }
    if (const auto* assets = value.get("assets")) {
        if (!assets->is_array()) { error = "assets override for " + id + " is not an array"; return false; }
        out.replace_assets = true;
        for (const auto& item : assets->array()) {
            if (!item.is_object()) { error = "asset override for " + id + " is not an object"; return false; }
            HootAssetRef asset;
            asset.type = string_value(item.get("type"));
            asset.path = string_value(item.get("path"));
            asset.transform = string_value(item.get("transform"));
            asset.offset = static_cast<uint32_t>(integer_value(item.get("offset"), 0));
            if (item.get("crc32")) {
                asset.has_crc32 = true;
                asset.crc32 = static_cast<uint32_t>(integer_value(item.get("crc32"), 0));
            }
            out.assets.push_back(std::move(asset));
        }
    }
    if (const auto* tracks = value.get("tracks")) {
        if (!tracks->is_array()) { error = "tracks override for " + id + " is not an array"; return false; }
        out.replace_tracks = true;
        for (const auto& item : tracks->array()) {
            if (!item.is_object()) { error = "track override for " + id + " is not an object"; return false; }
            CatalogTrack track;
            track.code = static_cast<uint32_t>(integer_value(item.get("code"), 0));
            track.title = string_value(item.get("title"));
            track.voice_bank = string_value(item.get("voice_bank"));
            out.tracks.push_back(std::move(track));
        }
    }
    return true;
}

void write_override(std::ostream& out, const HootEntryOverride& e, int indent)
{
    const std::string i(static_cast<size_t>(indent), ' ');
    const std::string j(static_cast<size_t>(indent + 2), ' ');
    bool first = true;
    auto field = [&](const std::string& name, const std::string& raw) {
        out << (first ? "\n" : ",\n") << j << '"' << name << "\": " << raw;
        first = false;
    };
    out << "{";
    if (e.hidden) field("hidden", "true");
    if (e.create) field("create", "true");
    if (e.has_title) field("title", "\"" + json_escape(e.title) + "\"");
    if (e.has_archive) field("archive", "\"" + json_escape(e.archive) + "\"");
    if (e.has_driver_name || e.has_driver_type || e.has_driver_alias) {
        std::ostringstream d;
        d << "{";
        bool df = true;
        auto dfield = [&](const char* name, const std::string& value) {
            if (!df) d << ", ";
            d << '"' << name << "\": \"" << json_escape(value) << '"';
            df = false;
        };
        if (e.has_driver_name) dfield("name", e.driver_name);
        if (e.has_driver_type) dfield("type", e.driver_type);
        if (e.has_driver_alias) dfield("alias", e.driver_alias);
        d << "}";
        field("driver", d.str());
    }
    if (e.has_default_sample_rate) field("default_sample_rate", std::to_string(e.default_sample_rate));
    if (e.has_refresh_hz) field("refresh_hz", std::to_string(e.refresh_hz));
    if (e.replace_options) {
        std::ostringstream o;
        o << "{";
        bool of = true;
        for (const auto& pair : e.options) {
            if (!of) o << ", ";
            o << '"' << json_escape(pair.first) << "\": " << pair.second;
            of = false;
        }
        o << "}";
        field("options", o.str());
    }
    if (e.replace_assets) {
        std::ostringstream a;
        a << "[";
        for (size_t n = 0; n < e.assets.size(); ++n) {
            const auto& asset = e.assets[n];
            if (n) a << ", ";
            a << "{\"type\": \"" << json_escape(asset.type) << "\", \"path\": \"" << json_escape(asset.path)
              << "\", \"transform\": \"" << json_escape(asset.transform) << "\", \"offset\": " << asset.offset;
            if (asset.has_crc32) a << ", \"crc32\": " << asset.crc32;
            a << "}";
        }
        a << "]";
        field("assets", a.str());
    }
    if (e.replace_tracks) {
        std::ostringstream t;
        t << "[";
        for (size_t n = 0; n < e.tracks.size(); ++n) {
            const auto& track = e.tracks[n];
            if (n) t << ", ";
            t << "{\"code\": " << track.code << ", \"title\": \"" << json_escape(track.title) << '"';
            if (!track.voice_bank.empty()) t << ", \"voice_bank\": \"" << json_escape(track.voice_bank) << '"';
            t << "}";
        }
        t << "]";
        field("tracks", t.str());
    }
    if (!first) out << "\n" << i;
    out << "}";
}

} // namespace

bool load_hoot_user_overrides(const std::filesystem::path& path,
                              HootUserOverrides& document,
                              std::string& error)
{
    document.entries.clear();
    error.clear();
    if (path.empty() || !std::filesystem::exists(path)) return true;
    const auto json = read_file(path);
    if (json.empty()) { error = "unable to read user override file: " + path.string(); return false; }
    JsonValue root;
    if (!parse_json(json, root, error)) { error += " in " + path.string(); return false; }
    if (string_value(root.get("format")) != "hoot-user-overrides") {
        error = "unsupported user override format in " + path.string();
        return false;
    }
    if (integer_value(root.get("version"), 0) != 1) {
        error = "unsupported user override version in " + path.string();
        return false;
    }
    const auto* entries = root.get("entries");
    if (!entries || !entries->is_object()) {
        error = "user override file has no entries object: " + path.string();
        return false;
    }
    for (const auto& pair : entries->object()) {
        HootEntryOverride entry;
        if (!parse_override_entry(pair.first, pair.second, entry, error)) return false;
        document.entries[pair.first] = std::move(entry);
    }
    return true;
}

bool save_hoot_user_overrides(const std::filesystem::path& path,
                              const HootUserOverrides& document,
                              std::string& error)
{
    error.clear();
    if (path.empty()) { error = "empty user override path"; return false; }
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) { error = "unable to create override directory: " + ec.message(); return false; }
    }
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "unable to write temporary override file: " + temp.string(); return false; }
        out << "{\n  \"format\": \"hoot-user-overrides\",\n  \"version\": 1,\n  \"entries\": {";
        bool first = true;
        for (const auto& pair : document.entries) {
            out << (first ? "\n" : ",\n") << "    \"" << json_escape(pair.first) << "\": ";
            write_override(out, pair.second, 4);
            first = false;
        }
        if (!first) out << "\n";
        out << "  }\n}\n";
        if (!out) { error = "failed while writing override file: " + temp.string(); return false; }
    }
#if defined(_WIN32)
    if (!MoveFileExW(temp.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "unable to atomically replace override file (Win32 error " + std::to_string(GetLastError()) + ")";
        std::filesystem::remove(temp, ec);
        return false;
    }
#else
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        error = "unable to atomically replace override file: " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
#endif
    return true;
}

HootEntryOverride complete_override_from_entry(const HootEntry& entry)
{
    HootEntryOverride out;
    out.id = entry.id;
    out.has_title = true; out.title = entry.title;
    out.has_archive = true; out.archive = entry.archive;
    std::string driver = entry.driver_name;
    const auto slash = driver.find('/');
    out.has_driver_name = true;
    out.has_driver_type = true;
    if (slash == std::string::npos) { out.driver_name = driver; out.driver_type.clear(); }
    else { out.driver_name = driver.substr(0, slash); out.driver_type = driver.substr(slash + 1); }
    out.has_driver_alias = true; out.driver_alias = entry.driver_alias;
    out.has_default_sample_rate = true; out.default_sample_rate = entry.default_sample_rate;
    out.has_refresh_hz = true; out.refresh_hz = entry.refresh_hz;
    out.replace_options = true; out.options = entry.options;
    out.replace_assets = true; out.assets = entry.assets;
    out.replace_tracks = true; out.tracks = entry.tracks;
    return out;
}

bool apply_hoot_user_overrides(const HootUserOverrides& document,
                               HootCatalog& catalog,
                               std::string& error)
{
    error.clear();
    auto& entries = catalog.mutable_entries();
    for (const auto& pair : document.entries) {
        const auto& patch = pair.second;
        auto it = std::find_if(entries.begin(), entries.end(), [&](const HootEntry& e) { return e.id == pair.first; });
        if (patch.hidden) {
            if (it != entries.end()) entries.erase(it);
            continue;
        }
        if (it == entries.end()) {
            if (!patch.create) { error = "user override references unknown entry: " + pair.first; return false; }
            HootEntry created; created.id = pair.first; entries.push_back(std::move(created)); it = std::prev(entries.end());
        }
        auto& entry = *it;
        if (patch.has_title) entry.title = patch.title;
        if (patch.has_archive) entry.archive = patch.archive;
        if (patch.has_driver_name || patch.has_driver_type) {
            std::string name = patch.has_driver_name ? patch.driver_name : entry.driver_name;
            std::string type = patch.has_driver_type ? patch.driver_type : std::string{};
            if (!patch.has_driver_type) {
                const auto slash = name.find('/');
                if (slash != std::string::npos) { type = name.substr(slash + 1); name.resize(slash); }
            }
            entry.driver_name = name + (type.empty() ? std::string{} : "/" + type);
        }
        if (patch.has_driver_alias) entry.driver_alias = patch.driver_alias;
        if (patch.has_default_sample_rate) entry.default_sample_rate = patch.default_sample_rate;
        if (patch.has_refresh_hz) entry.refresh_hz = patch.refresh_hz;
        if (patch.replace_options) entry.options = patch.options;
        if (patch.replace_assets) entry.assets = patch.assets;
        if (patch.replace_tracks) entry.tracks = patch.tracks;
        if (entry.id.empty() || entry.title.empty() || entry.driver_name.empty()) {
            error = "user override leaves entry incomplete: " + pair.first;
            return false;
        }
        if (entry.default_sample_rate <= 0 || entry.refresh_hz <= 0) {
            error = "invalid sample rate/refresh in user override: " + pair.first;
            return false;
        }
    }
    return true;
}

bool apply_hoot_user_overrides_file(const std::filesystem::path& path,
                                    HootCatalog& catalog,
                                    std::string& error)
{
    HootUserOverrides document;
    if (!load_hoot_user_overrides(path, document, error)) return false;
    return apply_hoot_user_overrides(document, catalog, error);
}

} // namespace hoot
