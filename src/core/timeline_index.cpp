#include "chronokit/core/timeline_index.hpp"

#include <algorithm>
#include <utility>

namespace chronokit {

int TimelineIndex::peak_occupied_units(
    const std::vector<BookingRecord>& bookings,
    const std::string& resource_id,
    EpochMinute eff_start,
    EpochMinute eff_end,
    const std::string& exclude_booking_id)
{
    if (eff_end <= eff_start) return 0;

    // Sweep-line events clamped to [eff_start, eff_end):
    // At same timestamp, process end (-units) BEFORE start (+units) because intervals are half-open [s, e).
    std::vector<std::pair<EpochMinute, int>> deltas;
    deltas.reserve(bookings.size() * 2);

    for (const auto& b : bookings) {
        if (!occupies_capacity(b.status)) continue;
        if (!exclude_booking_id.empty() && b.id == exclude_booking_id) continue;

        bool uses_res = false;
        for (const auto& rid : b.resource_ids) {
            if (rid == resource_id) {
                uses_res = true;
                break;
            }
        }
        if (!uses_res) continue;

        EpochMinute s = std::max(eff_start, b.effective_start);
        EpochMinute e = std::min(eff_end, b.effective_end);
        if (s < e) {
            // For bundling: primary resource consumes b.units, secondary bundled equipment consumes 1 unit (or min(cap, units))
            int consumed = (!b.resource_ids.empty() && b.resource_ids.front() == resource_id) ? b.units : 1;
            deltas.emplace_back(s, +consumed);
            deltas.emplace_back(e, -consumed);
        }
    }

    if (deltas.empty()) return 0;

    std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second; // Negative (release) comes before Positive (acquire)
    });

    int current = 0;
    int peak = 0;
    for (const auto& [t, d] : deltas) {
        (void)t;
        current += d;
        if (current > peak) peak = current;
    }
    return peak;
}

bool TimelineIndex::has_actor_overlap(
    const std::vector<BookingRecord>& bookings,
    const std::string& actor_id,
    EpochMinute start,
    EpochMinute end,
    const std::string& exclude_booking_id)
{
    for (const auto& b : bookings) {
        if (!occupies_capacity(b.status)) continue;
        if (b.actor_id != actor_id) continue;
        if (!exclude_booking_id.empty() && b.id == exclude_booking_id) continue;
        if (std::max(start, b.start_time) < std::min(end, b.end_time)) {
            return true;
        }
    }
    return false;
}

int TimelineIndex::waitlist_count(
    const std::vector<BookingRecord>& bookings,
    const std::string& resource_id,
    EpochMinute start,
    EpochMinute end)
{
    int count = 0;
    for (const auto& b : bookings) {
        if (b.status != BookingState::WAITLISTED) continue;
        for (const auto& rid : b.resource_ids) {
            if (rid == resource_id && std::max(start, b.start_time) < std::min(end, b.end_time)) {
                ++count;
                break;
            }
        }
    }
    return count;
}

} // namespace chronokit
