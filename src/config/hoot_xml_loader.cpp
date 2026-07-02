#include "config/hoot_xml_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>

namespace hoot {
namespace {

std::string read_file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

std::string trim(std::string value)
{
    auto not_space = [](unsigned char ch) {
        return std::isspace(ch) == 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string xml_unescape(std::string value)
{
    struct Replacement {
        const char* from;
        const char* to;
    };
    constexpr Replacement replacements[] = {
        {"&quot;", "\""},
        {"&apos;", "'"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&amp;", "&"},
    };

    for (const auto& replacement : replacements) {
        size_t pos = 0;
        while ((pos = value.find(replacement.from, pos)) != std::string::npos) {
            value.replace(pos, std::strlen(replacement.from), replacement.to);
            pos += std::strlen(replacement.to);
        }
    }
    return value;
}

std::string strip_xml_comments(std::string value)
{
    std::string stripped;
    size_t pos = 0;
    while (pos < value.size()) {
        const auto begin = value.find("<!--", pos);
        if (begin == std::string::npos) {
            stripped.append(value, pos, std::string::npos);
            break;
        }
        stripped.append(value, pos, begin - pos);
        const auto end = value.find("-->", begin + 4);
        if (end == std::string::npos) {
            break;
        }
        pos = end + 3;
    }
    return stripped;
}

std::string tag_text(const std::string& block, const std::string& tag)
{
    const std::regex pattern("<" + tag + "(?:\\s+[^>]*)?>([\\s\\S]*?)</" + tag + ">",
                             std::regex::icase);
    std::smatch match;
    if (!std::regex_search(block, match, pattern)) {
        return {};
    }
    return xml_unescape(trim(match[1].str()));
}

std::string tag_open(const std::string& block, const std::string& tag)
{
    const std::regex pattern("<" + tag + "(?:\\s+[^>]*)?>",
                             std::regex::icase);
    std::smatch match;
    if (!std::regex_search(block, match, pattern)) {
        return {};
    }
    return match[0].str();
}

std::string attr_value(const std::string& open_tag, const std::string& attr)
{
    const std::regex pattern("\\s" + attr + "\\s*=\\s*\"([^\"]*)\"",
                             std::regex::icase);
    std::smatch match;
    if (!std::regex_search(open_tag, match, pattern)) {
        return {};
    }
    return xml_unescape(match[1].str());
}

uint32_t parse_u32(const std::string& text)
{
    if (text.empty()) {
        return 0;
    }
    char* end = nullptr;
    return static_cast<uint32_t>(std::strtoul(text.c_str(), &end, 0));
}

std::string slugify(const std::string& text)
{
    std::string slug;
    bool last_dash = false;
    for (unsigned char ch : text) {
        if (std::isalnum(ch)) {
            slug.push_back(static_cast<char>(std::tolower(ch)));
            last_dash = false;
        } else if (!last_dash && !slug.empty()) {
            slug.push_back('-');
            last_dash = true;
        }
    }
    if (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    return slug.empty() ? "entry" : slug;
}

std::string unique_id(const HootEntry& entry, std::map<std::string, int>& seen)
{
    std::string base = !entry.archive.empty() ? entry.archive : slugify(entry.title);
    if (!entry.driver_type.empty()) {
        base = slugify(base + "-" + entry.driver_type);
    } else {
        base = slugify(base);
    }

    const int count = seen[base]++;
    if (count == 0) {
        return base;
    }
    return base + "-" + std::to_string(count + 1);
}

std::vector<std::string> matching_blocks(const std::string& xml, const std::string& tag)
{
    const std::regex pattern("<" + tag + "(?:\\s+[^>]*)?>[\\s\\S]*?</" + tag + ">",
                             std::regex::icase);
    std::vector<std::string> blocks;
    for (std::sregex_iterator it(xml.begin(), xml.end(), pattern), end; it != end; ++it) {
        blocks.push_back(it->str());
    }
    return blocks;
}

std::vector<std::string> matching_open_tags(const std::string& xml, const std::string& tag)
{
    const std::regex pattern("<" + tag + "(?:\\s+[^>]*)?/?>",
                             std::regex::icase);
    std::vector<std::string> tags;
    for (std::sregex_iterator it(xml.begin(), xml.end(), pattern), end; it != end; ++it) {
        tags.push_back(it->str());
    }
    return tags;
}

bool parse_games(const std::string& xml, HootCatalog& catalog, std::map<std::string, int>& seen_ids)
{
    const auto uncommented_xml = strip_xml_comments(xml);

    for (const auto& game : matching_blocks(uncommented_xml, "game")) {
        HootEntry entry;
        entry.title = tag_text(game, "name");

        const auto driver_open = tag_open(game, "driver");
        entry.driver_name = tag_text(game, "driver");
        entry.driver_type = attr_value(driver_open, "type");
        if (!entry.driver_type.empty()) {
            entry.driver_name += "/" + entry.driver_type;
        }

        const auto driver_alias_open = tag_open(game, "driveralias");
        const auto driver_alias_text = tag_text(game, "driveralias");
        const auto driver_alias_type = attr_value(driver_alias_open, "type");
        if (!driver_alias_text.empty()) {
            entry.driver_alias = driver_alias_text;
            if (!driver_alias_type.empty()) {
                entry.driver_alias += "/" + driver_alias_type;
            }
        }

        const auto optionlists = matching_blocks(game, "options");
        if (!optionlists.empty()) {
            for (const auto& option_open : matching_open_tags(optionlists.front(), "option")) {
                const auto name = attr_value(option_open, "name");
                if (!name.empty()) {
                    entry.options[name] = static_cast<int>(parse_u32(attr_value(option_open, "value")));
                }
            }
        }

        const auto romlist = tag_text(game, "romlist").empty()
            ? std::string()
            : matching_blocks(game, "romlist").front();
        if (!romlist.empty()) {
            entry.archive = attr_value(tag_open(romlist, "romlist"), "archive");
            for (const auto& rom : matching_blocks(romlist, "rom")) {
                HootAssetRef asset;
                asset.type = attr_value(tag_open(rom, "rom"), "type");
                asset.offset = parse_u32(attr_value(tag_open(rom, "rom"), "offset"));
                asset.path = tag_text(rom, "rom");
                entry.assets.push_back(std::move(asset));
            }
        }

        const auto titlelists = matching_blocks(game, "titlelist");
        if (!titlelists.empty()) {
            for (const auto& title : matching_blocks(titlelists.front(), "title")) {
                CatalogTrack track;
                track.code = parse_u32(attr_value(tag_open(title, "title"), "code"));
                track.title = tag_text(title, "title");
                entry.tracks.push_back(std::move(track));
            }
        }

        if (entry.title.empty() || entry.driver_name.empty()) {
            continue;
        }

        entry.id = unique_id(entry, seen_ids);
        catalog.add_entry(std::move(entry));
    }
    return true;
}

std::vector<std::string> child_list_paths(const std::string& xml)
{
    std::vector<std::string> paths;
    const auto uncommented_xml = strip_xml_comments(xml);
    for (const auto& childlists : matching_blocks(uncommented_xml, "childlists")) {
        for (const auto& list : matching_blocks(childlists, "list")) {
            const auto path = tag_text(list, "list");
            if (!path.empty()) {
                paths.push_back(path);
            }
        }
    }
    return paths;
}

bool load_file_recursive(const std::filesystem::path& path,
                         HootCatalog& catalog,
                         std::map<std::string, int>& seen_ids,
                         std::map<std::filesystem::path, bool>& visited,
                         std::string& error)
{
    const auto canonical_path = std::filesystem::weakly_canonical(path);
    if (visited[canonical_path]) {
        return true;
    }
    visited[canonical_path] = true;

    const auto xml = read_file(canonical_path.string());
    if (xml.empty()) {
        error = "unable to read catalog: " + canonical_path.string();
        return false;
    }

    parse_games(xml, catalog, seen_ids);

    const auto base_dir = canonical_path.parent_path();
    for (const auto& child : child_list_paths(xml)) {
        const std::filesystem::path child_path(child);
        const auto resolved = child_path.is_absolute() ? child_path : base_dir / child_path;
        if (!load_file_recursive(resolved, catalog, seen_ids, visited, error)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool HootXmlLoader::load_file(const std::string& path, HootCatalog& catalog, std::string& error) const
{
    catalog.clear();
    std::map<std::string, int> seen_ids;
    std::map<std::filesystem::path, bool> visited;
    if (!load_file_recursive(path, catalog, seen_ids, visited, error)) {
        return false;
    }

    if (catalog.entries().empty()) {
        error = "catalog contains no supported <game> entries";
        return false;
    }
    return true;
}

bool HootXmlLoader::load_string(const std::string& xml, HootCatalog& catalog, std::string& error) const
{
    catalog.clear();
    std::map<std::string, int> seen_ids;
    parse_games(xml, catalog, seen_ids);

    if (catalog.entries().empty()) {
        error = "catalog contains no supported <game> entries";
        return false;
    }
    return true;
}

} // namespace hoot
