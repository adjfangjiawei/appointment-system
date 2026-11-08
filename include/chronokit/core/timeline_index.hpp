#pragma once

#include "chronokit/core/types.hpp"

#include <string>
#include <vector>

namespace chronokit {

// Sweep-line timeline occupancy calculator for exact peak concurrent capacity checks.
class TimelineIndex {
public:
    // Compute peak concurrent occupied units on `resource_id` within [eff_start, eff_end).
    static int peak_occupied_units(
        const std::vector<BookingRecord>& bookings,
        const std::string& resource_id,
        EpochMinute eff_start,
        EpochMinute eff_end,
        const std::string& exclude_booking_id = "");

    // Check if an actor already has an overlapping active booking within [start, end).
    static bool has_actor_overlap(
        const std::vector<BookingRecord>& bookings,
        const std::string& actor_id,
        EpochMinute start,
        EpochMinute end,
        const std::string& exclude_booking_id = "");

    // Count waiting list items overlapping [start, end) on resource_id.
    static int waitlist_count(
        const std::vector<BookingRecord>& bookings,
        const std::string& resource_id,
        EpochMinute start,
        EpochMinute end);
};

} // namespace chronokit
