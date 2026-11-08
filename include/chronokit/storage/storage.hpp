#pragma once

#include "chronokit/core/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace chronokit {

class IStorage {
public:
    virtual ~IStorage() = default;
    virtual std::string backend_name() const = 0;

    // Resources
    virtual void upsert_resource(const Resource& r) = 0;
    virtual std::optional<Resource> get_resource(const std::string& id) const = 0;
    virtual std::vector<Resource> list_resources() const = 0;
    virtual bool delete_resource(const std::string& id) = 0;

    // Actors
    virtual void upsert_actor(const Actor& a) = 0;
    virtual std::optional<Actor> get_actor(const std::string& id) const = 0;
    virtual std::vector<Actor> list_actors() const = 0;

    // Bookings
    virtual std::string next_booking_id() = 0;
    virtual void save_booking(const BookingRecord& b) = 0;
    virtual std::optional<BookingRecord> get_booking(const std::string& id) const = 0;
    virtual std::vector<BookingRecord> list_bookings() const = 0;
    virtual bool delete_booking(const std::string& id) = 0;

    virtual void clear_all() = 0;
};

} // namespace chronokit
