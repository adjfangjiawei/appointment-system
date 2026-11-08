#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chronokit {

// EpochMinute = minutes since 1970-01-01 00:00 in civil local calendar.
using EpochMinute = int64_t;

namespace time_utils {

// Parse "YYYY-MM-DD HH:MM" or "YYYY-MM-DDTHH:MM" into EpochMinute.
std::optional<EpochMinute> parse_datetime(std::string_view str);
EpochMinute must_parse_datetime(std::string_view str);

// Format EpochMinute as "YYYY-MM-DD HH:MM"
std::string format_datetime(EpochMinute t);

// Format EpochMinute date part as "YYYY-MM-DD"
std::string format_date(EpochMinute t);

// Parse "HH:MM" into minutes from midnight [0, 1440]
std::optional<int> parse_hhmm(std::string_view str);
int must_parse_hhmm(std::string_view str);

// Format [0, 1440] into "HH:MM"
std::string format_hhmm(int minutes_of_day);

// Minute of day in [0, 1439]
int minute_of_day(EpochMinute t);

// Start of civil day (00:00) for t
EpochMinute start_of_day(EpochMinute t);

// ISO weekday: Monday = 1 ... Sunday = 7
int weekday_iso(EpochMinute t);

// Start of ISO week (Monday 00:00)
EpochMinute start_of_iso_week(EpochMinute t);

// Current system local time in EpochMinute
EpochMinute now_local_minutes();

} // namespace time_utils
} // namespace chronokit
