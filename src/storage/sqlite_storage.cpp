#include "chronokit/storage/sqlite_storage.hpp"

#include <sqlite3.h>
#include <cstdio>
#include <stdexcept>

namespace chronokit {

SqliteStorage::SqliteStorage(const std::string& db_path) : db_path_(db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "unknown";
        if (db_) sqlite3_close(db_);
        throw std::runtime_error("Failed to open SQLite database '" + db_path + "': " + err);
    }

    exec_sql("PRAGMA journal_mode = WAL;");
    exec_sql("PRAGMA synchronous = NORMAL;");

    exec_sql(R"SQL(
        CREATE TABLE IF NOT EXISTS meta_seq (
            key TEXT PRIMARY KEY,
            val INTEGER NOT NULL
        );
        INSERT OR IGNORE INTO meta_seq(key, val) VALUES ('booking_seq', 1000);

        CREATE TABLE IF NOT EXISTS resources (
            id TEXT PRIMARY KEY,
            json_data TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS actors (
            id TEXT PRIMARY KEY,
            json_data TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS bookings (
            id TEXT PRIMARY KEY,
            actor_id TEXT NOT NULL,
            status TEXT NOT NULL,
            start_min INTEGER NOT NULL,
            end_min INTEGER NOT NULL,
            json_data TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_bookings_actor ON bookings(actor_id);
        CREATE INDEX IF NOT EXISTS idx_bookings_time ON bookings(start_min, end_min);
    )SQL");
}

SqliteStorage::~SqliteStorage() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void SqliteStorage::exec_sql(const char* sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "SQLite exec failed";
        if (err_msg) sqlite3_free(err_msg);
        throw std::runtime_error(err);
    }
}

void SqliteStorage::upsert_resource(const Resource& r) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string payload = r.to_json().dump();
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO resources(id, json_data) VALUES (?1, ?2);", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, r.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<Resource> SqliteStorage::get_resource(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT json_data FROM resources WHERE id = ?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Resource> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) res = Resource::from_json(JsonValue::parse(txt));
    }
    sqlite3_finalize(stmt);
    return res;
}

std::vector<Resource> SqliteStorage::list_resources() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Resource> out;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT json_data FROM resources ORDER BY id ASC;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) out.push_back(Resource::from_json(JsonValue::parse(txt)));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool SqliteStorage::delete_resource(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM resources WHERE id = ?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return changes > 0;
}

void SqliteStorage::upsert_actor(const Actor& a) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string payload = a.to_json().dump();
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "INSERT OR REPLACE INTO actors(id, json_data) VALUES (?1, ?2);", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, a.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<Actor> SqliteStorage::get_actor(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT json_data FROM actors WHERE id = ?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Actor> res;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) res = Actor::from_json(JsonValue::parse(txt));
    }
    sqlite3_finalize(stmt);
    return res;
}

std::vector<Actor> SqliteStorage::list_actors() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Actor> out;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT json_data FROM actors ORDER BY id ASC;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) out.push_back(Actor::from_json(JsonValue::parse(txt)));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::string SqliteStorage::next_booking_id() {
    std::lock_guard<std::mutex> lk(mu_);
    exec_sql("UPDATE meta_seq SET val = val + 1 WHERE key = 'booking_seq';");
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT val FROM meta_seq WHERE key = 'booking_seq';", -1, &stmt, nullptr);
    int64_t seq = 1001;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        seq = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "BK-%05lld", static_cast<long long>(seq));
    return buf;
}

void SqliteStorage::save_booking(const BookingRecord& b) {
    std::lock_guard<std::mutex> lk(mu_);
    std::string payload = b.to_json().dump();
    std::string status_str = to_string(b.status);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(
        db_,
        "INSERT OR REPLACE INTO bookings(id, actor_id, status, start_min, end_min, json_data) VALUES (?1, ?2, ?3, ?4, ?5, ?6);",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, b.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, b.actor_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, status_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, b.start_time);
    sqlite3_bind_int64(stmt, 5, b.end_time);
    sqlite3_bind_text(stmt, 6, payload.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::optional<BookingRecord> SqliteStorage::get_booking(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT json_data FROM bookings WHERE id = ?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<BookingRecord> out;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) out = BookingRecord::from_json(JsonValue::parse(txt));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<BookingRecord> SqliteStorage::list_bookings() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<BookingRecord> out;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "SELECT json_data FROM bookings ORDER BY id ASC;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (txt) out.push_back(BookingRecord::from_json(JsonValue::parse(txt)));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool SqliteStorage::delete_booking(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM bookings WHERE id = ?1;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    int changes = sqlite3_changes(db_);
    sqlite3_finalize(stmt);
    return changes > 0;
}

void SqliteStorage::clear_all() {
    std::lock_guard<std::mutex> lk(mu_);
    exec_sql("DELETE FROM bookings; DELETE FROM resources; DELETE FROM actors; UPDATE meta_seq SET val = 1000 WHERE key = 'booking_seq';");
}

} // namespace chronokit
