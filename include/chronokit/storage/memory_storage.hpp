#pragma once

#include "chronokit/storage/storage.hpp"

#include <cstdint>
#include <map>
#include <mutex>

namespace chronokit {

class MemoryStorage : public IStorage {
public:
    std::string backend_name() const override { return "memory"; }

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
    mutable std::mutex mu_;
    uint64_t seq_ = 1000;
    std::map<std::string, Resource> resources_;
    std::map<std::string, Actor> actors_;
    std::map<std::string, BookingRecord> bookings_;
};

} // namespace chronokit
