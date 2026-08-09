#include "config/hoot_sqlite_loader.h"

#include <sqlite3.h>
#if !defined(HOOT_NO_ZSTD)
#include <zstd.h>
#endif

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace hoot {
namespace {

struct DatabaseCloser {
    void operator()(sqlite3* db) const { if (db != nullptr) sqlite3_close(db); }
};
using Database = std::unique_ptr<sqlite3, DatabaseCloser>;

struct StatementCloser {
    void operator()(sqlite3_stmt* stmt) const { if (stmt != nullptr) sqlite3_finalize(stmt); }
};
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

#if !defined(HOOT_NO_ZSTD)
std::vector<unsigned char> read_binary(const std::string& path, std::string& error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "unable to read catalogue: " + path;
        return {};
    }
    return std::vector<unsigned char>(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool is_zstd(const std::vector<unsigned char>& data)
{
    return data.size() >= 4 && data[0] == 0x28 && data[1] == 0xb5
        && data[2] == 0x2f && data[3] == 0xfd;
}

bool is_sqlite(const std::vector<unsigned char>& data)
{
    static constexpr char signature[] = "SQLite format 3\0";
    return data.size() >= sizeof(signature) - 1
        && std::memcmp(data.data(), signature, sizeof(signature) - 1) == 0;
}

bool decompress_zstd(const std::vector<unsigned char>& compressed,
                     std::vector<unsigned char>& output,
                     std::string& error)
{
    const unsigned long long frame_size = ZSTD_getFrameContentSize(compressed.data(), compressed.size());
    if (frame_size != ZSTD_CONTENTSIZE_ERROR && frame_size != ZSTD_CONTENTSIZE_UNKNOWN) {
        if (frame_size > static_cast<unsigned long long>(std::numeric_limits<size_t>::max())) {
            error = "zstd catalogue is too large";
            return false;
        }
        output.resize(static_cast<size_t>(frame_size));
        const size_t result = ZSTD_decompress(output.data(), output.size(), compressed.data(), compressed.size());
        if (ZSTD_isError(result)) {
            error = std::string("zstd decompression failed: ") + ZSTD_getErrorName(result);
            return false;
        }
        output.resize(result);
        return true;
    }
    if (frame_size == ZSTD_CONTENTSIZE_ERROR) {
        error = "invalid zstd catalogue";
        return false;
    }

    std::unique_ptr<ZSTD_DStream, decltype(&ZSTD_freeDStream)> stream(ZSTD_createDStream(), &ZSTD_freeDStream);
    if (!stream) {
        error = "unable to create zstd decompressor";
        return false;
    }
    size_t result = ZSTD_initDStream(stream.get());
    if (ZSTD_isError(result)) {
        error = std::string("zstd initialization failed: ") + ZSTD_getErrorName(result);
        return false;
    }
    std::vector<unsigned char> chunk(ZSTD_DStreamOutSize());
    ZSTD_inBuffer input{compressed.data(), compressed.size(), 0};
    while (input.pos < input.size) {
        ZSTD_outBuffer out{chunk.data(), chunk.size(), 0};
        result = ZSTD_decompressStream(stream.get(), &out, &input);
        if (ZSTD_isError(result)) {
            error = std::string("zstd decompression failed: ") + ZSTD_getErrorName(result);
            return false;
        }
        output.insert(output.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(out.pos));
    }
    if (result != 0) {
        error = "truncated zstd catalogue";
        return false;
    }
    return true;
}
#endif

bool prepare(sqlite3* db, const char* sql, Statement& statement, std::string& error)
{
    sqlite3_stmt* raw = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql, -1, &raw, nullptr);
    if (rc != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    statement.reset(raw);
    return true;
}

std::string column_text(sqlite3_stmt* statement, int column)
{
    const auto* value = sqlite3_column_text(statement, column);
    return value == nullptr ? std::string() : reinterpret_cast<const char*>(value);
}

bool bind_game_id(sqlite3* db, sqlite3_stmt* statement, const std::string& id, std::string& error)
{
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    const int rc = sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        error = sqlite3_errmsg(db);
        return false;
    }
    return true;
}

bool verify_metadata(sqlite3* db, std::string& error)
{
    Statement statement;
    if (!prepare(db, "SELECT key,value FROM meta WHERE key IN ('format','schema_version')", statement, error)) {
        return false;
    }
    std::string format;
    std::string version;
    int rc = SQLITE_ROW;
    while ((rc = sqlite3_step(statement.get())) == SQLITE_ROW) {
        const auto key = column_text(statement.get(), 0);
        const auto value = column_text(statement.get(), 1);
        if (key == "format") format = value;
        if (key == "schema_version") version = value;
    }
    if (rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        return false;
    }
    if (format != "hoot-sqlite") {
        error = "not a Hoot SQLite catalogue";
        return false;
    }
    if (version != "1") {
        error = "unsupported Hoot SQLite schema version: " + version;
        return false;
    }
    return true;
}

bool load_database(sqlite3* db, HootCatalog& catalog, std::string& error)
{
    if (!verify_metadata(db, error)) return false;

    Statement games;
    Statement options;
    Statement assets;
    Statement tracks;
    if (!prepare(db,
        "SELECT id,title,driver_name,driver_type,driver_alias,platform,archive,default_sample_rate,refresh_hz "
        "FROM games ORDER BY source_order", games, error)
        || !prepare(db,
        "SELECT name,value FROM game_options WHERE game_id=? ORDER BY ordinal", options, error)
        || !prepare(db,
        "SELECT type,path,transform,offset,crc32 FROM assets WHERE game_id=? ORDER BY ordinal", assets, error)
        || !prepare(db,
        "SELECT code,title,voice_bank FROM tracks WHERE game_id=? ORDER BY ordinal", tracks, error)) {
        return false;
    }

    int game_rc = SQLITE_ROW;
    while ((game_rc = sqlite3_step(games.get())) == SQLITE_ROW) {
        HootEntry entry;
        entry.id = column_text(games.get(), 0);
        entry.title = column_text(games.get(), 1);
        entry.driver_name = column_text(games.get(), 2);
        entry.driver_type = column_text(games.get(), 3);
        if (!entry.driver_type.empty()) entry.driver_name += "/" + entry.driver_type;
        entry.driver_alias = column_text(games.get(), 4);
        const auto platform = column_text(games.get(), 5);
        if (!entry.driver_alias.empty() && !platform.empty()) entry.driver_alias += "/" + platform;
        entry.archive = column_text(games.get(), 6);
        entry.default_sample_rate = sqlite3_column_int(games.get(), 7);
        entry.refresh_hz = sqlite3_column_int(games.get(), 8);

        if (!bind_game_id(db, options.get(), entry.id, error)) return false;
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(options.get())) == SQLITE_ROW) {
            entry.options[column_text(options.get(), 0)] = sqlite3_column_int(options.get(), 1);
        }
        if (rc != SQLITE_DONE) { error = sqlite3_errmsg(db); return false; }

        if (!bind_game_id(db, assets.get(), entry.id, error)) return false;
        while ((rc = sqlite3_step(assets.get())) == SQLITE_ROW) {
            HootAssetRef asset;
            asset.type = column_text(assets.get(), 0);
            asset.path = column_text(assets.get(), 1);
            asset.transform = column_text(assets.get(), 2);
            asset.offset = static_cast<uint32_t>(sqlite3_column_int64(assets.get(), 3));
            asset.has_crc32 = sqlite3_column_type(assets.get(), 4) != SQLITE_NULL;
            if (asset.has_crc32) asset.crc32 = static_cast<uint32_t>(sqlite3_column_int64(assets.get(), 4));
            entry.assets.push_back(std::move(asset));
        }
        if (rc != SQLITE_DONE) { error = sqlite3_errmsg(db); return false; }

        if (!bind_game_id(db, tracks.get(), entry.id, error)) return false;
        while ((rc = sqlite3_step(tracks.get())) == SQLITE_ROW) {
            CatalogTrack track;
            track.code = static_cast<uint32_t>(sqlite3_column_int64(tracks.get(), 0));
            track.title = column_text(tracks.get(), 1);
            track.voice_bank = column_text(tracks.get(), 2);
            entry.tracks.push_back(std::move(track));
        }
        if (rc != SQLITE_DONE) { error = sqlite3_errmsg(db); return false; }

        catalog.add_entry(std::move(entry));
    }
    if (game_rc != SQLITE_DONE) {
        error = sqlite3_errmsg(db);
        return false;
    }
    if (catalog.entries().empty()) {
        error = "SQLite catalogue contains no games";
        return false;
    }
    return true;
}

} // namespace

bool HootSqliteLoader::load_file(const std::string& path, HootCatalog& catalog, std::string& error) const
{
    catalog.clear();
#if defined(HOOT_NO_ZSTD)
    // The browser bundle preloads an ordinary SQLite file. Open it directly
    // from Emscripten's virtual filesystem instead of reading and copying the
    // whole catalogue into a second in-memory SQLite image. This keeps startup
    // memory lower and avoids depending on sqlite3_deserialize in the web port.
    sqlite3* raw_db = nullptr;
    const int open_rc = sqlite3_open_v2(path.c_str(), &raw_db, SQLITE_OPEN_READONLY, nullptr);
    if (open_rc != SQLITE_OK) {
        error = raw_db == nullptr ? "unable to open SQLite catalogue: " + path : sqlite3_errmsg(raw_db);
        if (raw_db != nullptr) sqlite3_close(raw_db);
        return false;
    }
    Database db(raw_db);
    return load_database(db.get(), catalog, error);
#else
    auto bytes = read_binary(path, error);
    if (bytes.empty()) return false;

    std::vector<unsigned char> decompressed;
    if (is_zstd(bytes)) {
        if (!decompress_zstd(bytes, decompressed, error)) return false;
        bytes.swap(decompressed);
    }
    if (!is_sqlite(bytes)) {
        error = "catalogue is neither SQLite nor zstd-compressed SQLite: " + path;
        return false;
    }
    if (bytes.size() > static_cast<size_t>(std::numeric_limits<sqlite3_int64>::max())) {
        error = "SQLite catalogue is too large";
        return false;
    }

    sqlite3* raw_db = nullptr;
    if (sqlite3_open(":memory:", &raw_db) != SQLITE_OK) {
        error = raw_db == nullptr ? "unable to open SQLite" : sqlite3_errmsg(raw_db);
        if (raw_db != nullptr) sqlite3_close(raw_db);
        return false;
    }
    Database db(raw_db);
    auto* image = static_cast<unsigned char*>(sqlite3_malloc64(bytes.size()));
    if (image == nullptr) {
        error = "unable to allocate SQLite catalogue image";
        return false;
    }
    std::memcpy(image, bytes.data(), bytes.size());
    const int rc = sqlite3_deserialize(db.get(), "main", image,
        static_cast<sqlite3_int64>(bytes.size()), static_cast<sqlite3_int64>(bytes.size()),
        SQLITE_DESERIALIZE_READONLY | SQLITE_DESERIALIZE_FREEONCLOSE);
    if (rc != SQLITE_OK) {
        sqlite3_free(image);
        error = std::string("unable to mount SQLite catalogue: ") + sqlite3_errstr(rc);
        return false;
    }
    return load_database(db.get(), catalog, error);
#endif
}

} // namespace hoot
