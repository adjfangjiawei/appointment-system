#include "chronokit/core/time_utils.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>
#include <tuple>

namespace chronokit::time_utils {

namespace {

// Howard Hinnant's civil calendar algorithms (public domain)
constexpr int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

constexpr std::tuple<int, unsigned, unsigned> civil_from_days(int64_t z) noexcept {
    z += 719468;
    const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp + (mp < 10 ? 3 : -9);
    return {y + (m <= 2), m, d};
}

int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r > 0) != (b > 0))) --q;
    return q;
}

} // namespace

std::optional<EpochMinute> parse_datetime(std::string_view str) {
    // Trim spaces
    while (!str.empty() && str.front() == ' ') str.remove_prefix(1);
    while (!str.empty() && str.back() == ' ') str.remove_suffix(1);
    if (str.size() < 16) return std::nullopt;

    int y = 0, m = 0, d = 0, hh = 0, mm = 0;
    char sep = ' ';
    std::string s(str.substr(0, 16));
    if (std::sscanf(s.c_str(), "%d-%d-%d%c%d:%d", &y, &m, &d, &sep, &hh, &mm) != 6) {
        return std::nullopt;
    }
    if (sep != ' ' && sep != 'T' && sep != 't') return std::nullopt;
    if (m < 1 || m > 12 || d < 1 || d > 31 || hh < 0 || hh > 23 || mm < 0 || mm > 59) {
        return std::nullopt;
    }
    int64_t days = days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
    return days * 1440 + hh * 60 + mm;
}

EpochMinute must_parse_datetime(std::string_view str) {
    auto res = parse_datetime(str);
    if (!res) {
        throw std::invalid_argument("Invalid datetime string (expected YYYY-MM-DD HH:MM): " + std::string(str));
    }
    return *res;
}

std::string format_datetime(EpochMinute t) {
    int64_t days = floor_div(t, 1440);
    int mod = static_cast<int>(t - days * 1440);
    auto [y, m, d] = civil_from_days(days);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02d:%02d", y, m, d, mod / 60, mod % 60);
    return buf;
}

std::string format_date(EpochMinute t) {
    int64_t days = floor_div(t, 1440);
    auto [y, m, d] = civil_from_days(days);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02u-%02u", y, m, d);
    return buf;
}

std::optional<int> parse_hhmm(std::string_view str) {
    int hh = 0, mm = 0;
    std::string s(str);
    if (std::sscanf(s.c_str(), "%d:%d", &hh, &mm) != 2) return std::nullopt;
    if (hh == 24 && mm == 0) return 1440;
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return std::nullopt;
    return hh * 60 + mm;
}

int must_parse_hhmm(std::string_view str) {
    auto v = parse_hhmm(str);
    if (!v) throw std::invalid_argument("Invalid HH:MM string: " + std::string(str));
    return *v;
}

std::string format_hhmm(int minutes_of_day) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes_of_day / 60, minutes_of_day % 60);
    return buf;
}

int minute_of_day(EpochMinute t) {
    int64_t days = floor_div(t, 1440);
    return static_cast<int>(t - days * 1440);
}

EpochMinute start_of_day(EpochMinute t) {
    return floor_div(t, 1440) * 1440;
}

int weekday_iso(EpochMinute t) {
    // 1970-01-01 was Thursday (ISO 4)
    int64_t days = floor_div(t, 1440);
    int64_t wd = (days + 3) % 7;
    if (wd < 0) wd += 7;
    return static_cast<int>(wd) + 1; // 1 = Mon ... 7 = Sun
}

EpochMinute start_of_iso_week(EpochMinute t) {
    int wd = weekday_iso(t); // 1..7
    return start_of_day(t) - static_cast<int64_t>(wd - 1) * 1440;
}

EpochMinute now_local_minutes() {
    std::time_t now = std::time(nullptr);
    std::tm tm_val{};
#if defined(_WIN32)
    localtime_s(&tm_val, &now);
#else
    localtime_r(&now, &tm_val);
#endif
    int64_t days = days_from_civil(tm_val.tm_year + 1900,
                                   static_cast<unsigned>(tm_val.tm_mon + 1),
                                   static_cast<unsigned>(tm_val.tm_mday));
    return days * 1440 + tm_val.tm_hour * 60 + tm_val.tm_min;
}

} // namespace chronokit::time_utils
