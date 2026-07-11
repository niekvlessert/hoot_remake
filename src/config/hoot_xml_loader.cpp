#include "config/hoot_xml_loader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#include <utility>

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

struct XmlNode {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::string text;
    std::vector<XmlNode> children;

    std::string attribute(std::string_view attr_name) const {
        for (const auto& attr : attributes) {
            if (attr.first == attr_name) {
                return attr.second;
            }
        }
        return {};
    }
};

void skip_whitespace(std::string_view& sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv[0]))) {
        sv.remove_prefix(1);
    }
}

bool parse_tag(std::string_view tag_content, std::string& name, std::vector<std::pair<std::string, std::string>>& attributes, bool& self_closing) {
    skip_whitespace(tag_content);
    if (tag_content.empty()) return false;

    // Read name
    size_t name_end = 0;
    while (name_end < tag_content.size() && !std::isspace(static_cast<unsigned char>(tag_content[name_end])) && tag_content[name_end] != '/' && tag_content[name_end] != '>') {
        name_end++;
    }
    if (name_end == 0) return false;
    name = std::string(tag_content.substr(0, name_end));
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    tag_content.remove_prefix(name_end);

    // Read attributes
    while (true) {
        skip_whitespace(tag_content);
        if (tag_content.empty()) break;
        if (tag_content[0] == '/') {
            self_closing = true;
            tag_content.remove_prefix(1);
            skip_whitespace(tag_content);
            break;
        }
        if (tag_content[0] == '>') {
            break;
        }

        // Read attribute key
        size_t key_end = 0;
        while (key_end < tag_content.size() && tag_content[key_end] != '=' && !std::isspace(static_cast<unsigned char>(tag_content[key_end])) && tag_content[key_end] != '/' && tag_content[key_end] != '>') {
            key_end++;
        }
        if (key_end == 0) {
            tag_content.remove_prefix(1);
            continue;
        }
        std::string key(tag_content.substr(0, key_end));
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        tag_content.remove_prefix(key_end);

        skip_whitespace(tag_content);
        if (tag_content.empty() || tag_content[0] != '=') {
            attributes.push_back({key, ""});
            continue;
        }
        tag_content.remove_prefix(1); // skip '='
        skip_whitespace(tag_content);

        if (tag_content.empty()) return false;
        char quote = tag_content[0];
        if (quote != '"' && quote != '\'') {
            size_t val_end = 0;
            while (val_end < tag_content.size() && !std::isspace(static_cast<unsigned char>(tag_content[val_end])) && tag_content[val_end] != '/' && tag_content[val_end] != '>') {
                val_end++;
            }
            std::string val(tag_content.substr(0, val_end));
            attributes.push_back({key, xml_unescape(val)});
            tag_content.remove_prefix(val_end);
            continue;
        }
        tag_content.remove_prefix(1); // skip quote

        size_t val_end = tag_content.find(quote);
        if (val_end == std::string_view::npos) return false;
        std::string val(tag_content.substr(0, val_end));
        attributes.push_back({key, xml_unescape(val)});
        tag_content.remove_prefix(val_end + 1); // skip val and quote
    }
    return true;
}

bool parse_xml(std::string_view xml, XmlNode& root, std::string& error) {
    std::vector<XmlNode*> stack;
    XmlNode dummy;
    stack.push_back(&dummy);

    size_t pos = 0;
    while (pos < xml.size()) {
        size_t next_lt = xml.find('<', pos);
        if (next_lt == std::string_view::npos) {
            if (!stack.empty() && pos < xml.size()) {
                stack.back()->text.append(xml.substr(pos));
            }
            break;
        }

        if (next_lt > pos) {
            if (!stack.empty()) {
                stack.back()->text.append(xml.substr(pos, next_lt - pos));
            }
        }

        pos = next_lt;

        if (xml.compare(pos, 4, "<!--") == 0) {
            size_t end_comment = xml.find("-->", pos + 4);
            if (end_comment == std::string_view::npos) {
                error = "Unclosed comment";
                return false;
            }
            pos = end_comment + 3;
        } else if (xml.compare(pos, 9, "<![CDATA[") == 0) {
            size_t end_cdata = xml.find("]]>", pos + 9);
            if (end_cdata == std::string_view::npos) {
                error = "Unclosed CDATA";
                return false;
            }
            if (!stack.empty()) {
                stack.back()->text.append(xml.substr(pos + 9, end_cdata - (pos + 9)));
            }
            pos = end_cdata + 3;
        } else if (xml.compare(pos, 2, "<!") == 0) {
            size_t bracket_count = 1;
            size_t scan = pos + 2;
            while (scan < xml.size() && bracket_count > 0) {
                if (xml[scan] == '<') bracket_count++;
                else if (xml[scan] == '>') bracket_count--;
                scan++;
            }
            pos = scan;
        } else if (xml.compare(pos, 2, "<?") == 0) {
            size_t end_pi = xml.find("?>", pos + 2);
            if (end_pi == std::string_view::npos) {
                error = "Unclosed processing instruction";
                return false;
            }
            pos = end_pi + 2;
        } else if (xml.compare(pos, 2, "</") == 0) {
            size_t end_tag = xml.find('>', pos + 2);
            if (end_tag == std::string_view::npos) {
                error = "Unclosed closing tag";
                return false;
            }
            std::string_view tag_name = xml.substr(pos + 2, end_tag - (pos + 2));
            while (!tag_name.empty() && std::isspace(static_cast<unsigned char>(tag_name.front()))) tag_name.remove_prefix(1);
            while (!tag_name.empty() && std::isspace(static_cast<unsigned char>(tag_name.back()))) tag_name.remove_suffix(1);

            if (stack.size() <= 1) {
                error = "Unexpected closing tag: " + std::string(tag_name);
                return false;
            }

            auto nocase_compare = [](std::string_view s1, std::string_view s2) {
                if (s1.size() != s2.size()) return false;
                for (size_t i = 0; i < s1.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(s1[i])) != std::tolower(static_cast<unsigned char>(s2[i]))) {
                        return false;
                    }
                }
                return true;
            };

            if (!nocase_compare(stack.back()->name, tag_name)) {
                error = "Mismatched closing tag: expected " + stack.back()->name + ", found " + std::string(tag_name);
                return false;
            }
            stack.pop_back();
            pos = end_tag + 1;
        } else {
            size_t end_tag = xml.find('>', pos + 1);
            if (end_tag == std::string_view::npos) {
                error = "Unclosed opening tag";
                return false;
            }
            std::string_view tag_content = xml.substr(pos + 1, end_tag - (pos + 1));
            
            std::string name;
            std::vector<std::pair<std::string, std::string>> attributes;
            bool self_closing = false;
            if (!parse_tag(tag_content, name, attributes, self_closing)) {
                error = "Failed to parse tag: <" + std::string(tag_content) + ">";
                return false;
            }

            if (!tag_content.empty() && tag_content.back() == '/') {
                self_closing = true;
            }

            stack.back()->children.emplace_back();
            XmlNode* new_node = &stack.back()->children.back();
            new_node->name = std::move(name);
            new_node->attributes = std::move(attributes);

            if (!self_closing) {
                stack.push_back(new_node);
            }
            pos = end_tag + 1;
        }
    }

    if (stack.size() != 1) {
        error = "Unclosed tags remaining: " + stack.back()->name;
        return false;
    }

    if (dummy.children.empty()) {
        error = "No elements found in XML";
        return false;
    }

    root = std::move(dummy.children.front());
    return true;
}

void find_nodes_by_name(const XmlNode& node, std::string_view name, std::vector<const XmlNode*>& result) {
    if (node.name == name) {
        result.push_back(&node);
    }
    for (const auto& child : node.children) {
        find_nodes_by_name(child, name, result);
    }
}

bool parse_games(const XmlNode& root, HootCatalog& catalog, std::map<std::string, int>& seen_ids)
{
    std::vector<const XmlNode*> game_nodes;
    find_nodes_by_name(root, "game", game_nodes);

    for (const auto* game_node : game_nodes) {
        HootEntry entry;
        for (const auto& game_child : game_node->children) {
            if (game_child.name == "name") {
                entry.title = xml_unescape(trim(game_child.text));
            } else if (game_child.name == "driver") {
                entry.driver_name = xml_unescape(trim(game_child.text));
                entry.driver_type = game_child.attribute("type");
            } else if (game_child.name == "driveralias") {
                entry.driver_alias = xml_unescape(trim(game_child.text));
                const auto driver_alias_type = game_child.attribute("type");
                if (!entry.driver_alias.empty() && !driver_alias_type.empty()) {
                    entry.driver_alias += "/" + driver_alias_type;
                }
            } else if (game_child.name == "options") {
                for (const auto& opt_child : game_child.children) {
                    if (opt_child.name == "option") {
                        const auto opt_name = opt_child.attribute("name");
                        if (!opt_name.empty()) {
                            entry.options[opt_name] = static_cast<int>(parse_u32(opt_child.attribute("value")));
                        }
                    }
                }
            } else if (game_child.name == "romlist") {
                entry.archive = game_child.attribute("archive");
                for (const auto& rom_child : game_child.children) {
                    if (rom_child.name == "rom") {
                        HootAssetRef asset;
                        asset.type = rom_child.attribute("type");
                        asset.offset = parse_u32(rom_child.attribute("offset"));
                        asset.path = xml_unescape(trim(rom_child.text));
                        entry.assets.push_back(std::move(asset));
                    }
                }
            } else if (game_child.name == "titlelist") {
                for (const auto& title_child : game_child.children) {
                    if (title_child.name == "title") {
                        CatalogTrack track;
                        track.code = parse_u32(title_child.attribute("code"));
                        track.title = xml_unescape(trim(title_child.text));
                        entry.tracks.push_back(std::move(track));
                    }
                }
            }
        }

        if (entry.title.empty() || entry.driver_name.empty()) {
            continue;
        }

        if (!entry.driver_type.empty()) {
            entry.driver_name += "/" + entry.driver_type;
        }

        entry.id = unique_id(entry, seen_ids);
        catalog.add_entry(std::move(entry));
    }
    return true;
}

std::vector<std::string> child_list_paths(const XmlNode& root)
{
    std::vector<std::string> paths;
    std::vector<const XmlNode*> childlists_nodes;
    find_nodes_by_name(root, "childlists", childlists_nodes);
    for (const auto* childlists : childlists_nodes) {
        for (const auto& list_node : childlists->children) {
            if (list_node.name == "list") {
                auto path = xml_unescape(trim(list_node.text));
                if (!path.empty()) {
                    paths.push_back(std::move(path));
                }
            }
        }
    }
    return paths;
}

bool apply_overrides(const std::filesystem::path& path,
                     HootCatalog& catalog,
                     std::string& error)
{
    const auto xml = read_file(path.string());
    if (xml.empty()) {
        error = "unable to read overrides: " + path.string();
        return false;
    }

    XmlNode root;
    if (!parse_xml(xml, root, error)) {
        error = "unable to parse overrides: " + error + " in " + path.string();
        return false;
    }
    if (root.name != "hoot-overrides") {
        error = "override root must be <hoot-overrides>: " + path.string();
        return false;
    }

    for (const auto& game : root.children) {
        if (game.name != "game") {
            continue;
        }
        const auto match_id = game.attribute("id");
        const auto match_archive = game.attribute("archive");
        bool matched = false;
        for (auto& entry : catalog.mutable_entries()) {
            if ((!match_id.empty() && entry.id != match_id)
                || (!match_archive.empty() && entry.archive != match_archive)) {
                continue;
            }
            if (match_id.empty() && match_archive.empty()) {
                continue;
            }
            matched = true;
            for (const auto& item : game.children) {
                if (item.name == "voicebank") {
                    const auto id = item.attribute("id");
                    const auto file = item.attribute("file");
                    if (id.empty() || file.empty()) {
                        error = "<voicebank> requires id and file in " + path.string();
                        return false;
                    }
                    HootAssetRef asset;
                    asset.type = "voicebank:" + id;
                    asset.path = file;
                    asset.offset = parse_u32(item.attribute("offset"));
                    entry.assets.erase(
                        std::remove_if(entry.assets.begin(), entry.assets.end(), [&](const auto& existing) {
                            return existing.type == asset.type;
                        }),
                        entry.assets.end());
                    entry.assets.push_back(std::move(asset));
                } else if (item.name == "track") {
                    const auto code_text = item.attribute("code");
                    const auto voice_bank = item.attribute("voicebank");
                    if (code_text.empty() || voice_bank.empty()) {
                        error = "<track> requires code and voicebank in " + path.string();
                        return false;
                    }
                    const auto code = parse_u32(code_text);
                    bool track_matched = false;
                    for (auto& track : entry.tracks) {
                        if (track.code == code) {
                            track.voice_bank = voice_bank;
                            track_matched = true;
                        }
                    }
                    if (!track_matched) {
                        error = "override track code " + code_text + " was not found in " + entry.id;
                        return false;
                    }
                }
            }
        }
        (void)matched;
    }
    return true;
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

    XmlNode root;
    if (!parse_xml(xml, root, error)) {
        error = "unable to parse XML: " + error + " in " + canonical_path.string();
        return false;
    }

    parse_games(root, catalog, seen_ids);

    const auto base_dir = canonical_path.parent_path();
    for (const auto& child : child_list_paths(root)) {
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

    std::filesystem::path override_path;
    if (const char* configured = std::getenv("HOOT_OVERRIDE_XML")) {
        override_path = configured;
        if (!std::filesystem::exists(override_path)) {
            error = "configured override file does not exist: " + override_path.string();
            return false;
        }
    } else {
        const auto cwd_override = std::filesystem::current_path() / "hoot-overrides.xml";
        const auto catalog_override = std::filesystem::path(path).parent_path() / "hoot-overrides.xml";
        if (std::filesystem::exists(cwd_override)) {
            override_path = cwd_override;
        } else if (std::filesystem::exists(catalog_override)) {
            override_path = catalog_override;
        }
    }
    if (!override_path.empty() && !apply_overrides(override_path, catalog, error)) {
        return false;
    }
    return true;
}

bool HootXmlLoader::load_string(const std::string& xml, HootCatalog& catalog, std::string& error) const
{
    catalog.clear();
    std::map<std::string, int> seen_ids;
    XmlNode root;
    if (!parse_xml(xml, root, error)) {
        return false;
    }
    parse_games(root, catalog, seen_ids);

    if (catalog.entries().empty()) {
        error = "catalog contains no supported <game> entries";
        return false;
    }
    return true;
}

} // namespace hoot
