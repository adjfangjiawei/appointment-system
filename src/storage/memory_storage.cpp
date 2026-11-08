#include "chronokit/storage/memory_storage.hpp"

#include <cstdio>

namespace chronokit {

void MemoryStorage::upsert_resource(const Resource& r) {
    std::lock_guard<std::mutex> lk(mu_);
    resources_[r.id] = r;
}

std::optional<Resource> MemoryStorage::get_resource(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = resources_.find(id);
    if (it == resources_.end()) return std::nullopt;
    return it->second;
}

std::vector<Resource> MemoryStorage::list_resources() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Resource> out;
    out.reserve(resources_.size());
    for (const auto& [_, r] : resources_) out.push_back(r);
    return out;
}

bool MemoryStorage::delete_resource(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    return resources_.erase(id) > 0;
}

void MemoryStorage::upsert_actor(const Actor& a) {
    std::lock_guard<std::mutex> lk(mu_);
    actors_[a.id] = a;
}

std::optional<Actor> MemoryStorage::get_actor(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = actors_.find(id);
    if (it == actors_.end()) return std::nullopt;
    return it->second;
}

std::vector<Actor> MemoryStorage::list_actors() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<Actor> out;
    out.reserve(actors_.size());
    for (const auto& [_, a] : actors_) out.push_back(a);
    return out;
}

std::string MemoryStorage::next_booking_id() {
    std::lock_guard<std::mutex> lk(mu_);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "BK-%05llu", static_cast<unsigned long long>(++seq_));
    return buf;
}

void MemoryStorage::save_booking(const BookingRecord& b) {
    std::lock_guard<std::mutex> lk(mu_);
    bookings_[b.id] = b;
}

std::optional<BookingRecord> MemoryStorage::get_booking(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = bookings_.find(id);
    if (it == bookings_.end()) return std::nullopt;
    return it->second;
}

std::vector<BookingRecord> MemoryStorage::list_bookings() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<BookingRecord> out;
    out.reserve(bookings_.size());
    for (const auto& [_, b] : bookings_) out.push_back(b);
    return out;
}

bool MemoryStorage::delete_booking(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    return bookings_.erase(id) > 0;
}

void MemoryStorage::clear_all() {
    std::lock_guard<std::mutex> lk(mu_);
    seq_ = 1000;
    resources_.clear();
    actors_.clear();
    bookings_.clear();
}

} // namespace chronokit
