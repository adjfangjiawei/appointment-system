#pragma once

#include "chronokit/storage/storage.hpp"

#include <mutex>
#include <string>

struct sqlite3;

namespace chronokit {

class SqliteStorage : public IStorage {
public:
    explicit SqliteStorage(const std::string& db_path);
    ~SqliteStorage() override;

    SqliteStorage(const SqliteStorage&) = delete;
    SqliteStorage& operator=(const SqliteStorage&) = delete;

    std::string backend_name() const override { return "sqlite:" + db_path_; }

    void upsert_resource(const Resource& r) override;
    std::optional<Resource> get_resource(const std::string& id) const override;
    std::vector<Resource> list_resources() const override;
    bool delete_resource(const std::string& id) override;

    void upsert_actor(const Actor& a) override;
    std::optional<Actor> get_actor(const std::string& id) const override;
    std::vector<Actor> list_actors() const override;

    std::string next_booking_id() override;
    void save_booking(const BookingRecord& b) override;
    std::optional<BookingRecord> get_booking(const std::string& id) const override;
    std::vector<BookingRecord> list_bookings() const override;
    bool delete_booking(const std::string& id) override;

    void clear_all() override;

private:
    std::string db_path_;
    sqlite3* db_ = nullptr;
    mutable std::mutex mu_;

    void exec_sql(const char* sql);
};

} // namespace chronokit
