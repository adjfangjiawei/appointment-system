#pragma once

#include "chronokit/core/json.hpp"
#include "chronokit/core/time_utils.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace chronokit {

using MetaValue = std::variant<std::monostate, bool, int64_t, double, std::string>;
using MetadataMap = std::map<std::string, MetaValue>;

inline JsonValue meta_to_json(const MetadataMap& m) {
    JsonValue obj = JsonValue::object();
    for (const auto& [k, v] : m) {
        if (std::holds_alternative<bool>(v)) obj[k] = std::get<bool>(v);
        else if (std::holds_alternative<int64_t>(v)) obj[k] = std::get<int64_t>(v);
        else if (std::holds_alternative<double>(v)) obj[k] = std::get<double>(v);
        else if (std::holds_alternative<std::string>(v)) obj[k] = std::get<std::string>(v);
        else obj[k] = nullptr;
    }
    return obj;
}

inline MetadataMap json_to_meta(const JsonValue& j) {
    MetadataMap m;
    if (!j.is_object()) return m;
    for (const auto& [k, v] : j.as_object()) {
        if (v.is_bool()) m[k] = v.as_bool();
        else if (v.is_int()) m[k] = v.as_int();
        else if (v.is_double()) m[k] = v.as_double();
        else if (v.is_string()) m[k] = v.as_string();
    }
    return m;
}

inline std::string meta_get_string(const MetadataMap& m, const std::string& key, const std::string& def = "") {
    auto it = m.find(key);
    if (it == m.end()) return def;
    if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
    if (std::holds_alternative<int64_t>(it->second)) return std::to_string(std::get<int64_t>(it->second));
    if (std::holds_alternative<double>(it->second)) return std::to_string(std::get<double>(it->second));
    if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? "true" : "false";
    return def;
}

inline int64_t meta_get_int(const MetadataMap& m, const std::string& key, int64_t def = 0) {
    auto it = m.find(key);
    if (it == m.end()) return def;
    if (std::holds_alternative<int64_t>(it->second)) return std::get<int64_t>(it->second);
    if (std::holds_alternative<double>(it->second)) return static_cast<int64_t>(std::get<double>(it->second));
    return def;
}

inline double meta_get_double(const MetadataMap& m, const std::string& key, double def = 0.0) {
    auto it = m.find(key);
    if (it == m.end()) return def;
    if (std::holds_alternative<double>(it->second)) return std::get<double>(it->second);
    if (std::holds_alternative<int64_t>(it->second)) return static_cast<double>(std::get<int64_t>(it->second));
    return def;
}

inline bool meta_get_bool(const MetadataMap& m, const std::string& key, bool def = false) {
    auto it = m.find(key);
    if (it == m.end()) return def;
    if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
    if (std::holds_alternative<int64_t>(it->second)) return std::get<int64_t>(it->second) != 0;
    return def;
}

enum class ResourceStatus {
    ACTIVE = 0,
    MAINTENANCE = 1,
    FAULT = 2,
    DISABLED = 3
};

inline std::string to_string(ResourceStatus s) {
    switch (s) {
        case ResourceStatus::ACTIVE: return "ACTIVE";
        case ResourceStatus::MAINTENANCE: return "MAINTENANCE";
        case ResourceStatus::FAULT: return "FAULT";
        case ResourceStatus::DISABLED: return "DISABLED";
    }
    return "ACTIVE";
}

inline ResourceStatus resource_status_from_string(const std::string& s) {
    if (s == "MAINTENANCE" || s == "maintenance") return ResourceStatus::MAINTENANCE;
    if (s == "FAULT" || s == "fault" || s == "BROKEN") return ResourceStatus::FAULT;
    if (s == "DISABLED" || s == "disabled") return ResourceStatus::DISABLED;
    return ResourceStatus::ACTIVE;
}

struct Resource {
    std::string id;
    std::string name;
    std::string kind = "generic";
    std::string parent_id;
    int capacity = 1;
    ResourceStatus status = ResourceStatus::ACTIVE;
    std::set<std::string> tags;
    MetadataMap attributes;

    bool has_tag(const std::string& t) const { return tags.find(t) != tags.end(); }

    JsonValue to_json() const {
        JsonValue j = JsonValue::object();
        j["id"] = id;
        j["name"] = name;
        j["kind"] = kind;
        j["parent_id"] = parent_id;
        j["capacity"] = capacity;
        j["status"] = to_string(status);
        JsonValue t_arr = JsonValue::array();
        for (const auto& t : tags) t_arr.push_back(t);
        j["tags"] = std::move(t_arr);
        j["attributes"] = meta_to_json(attributes);
        return j;
    }

    static Resource from_json(const JsonValue& j) {
        Resource r;
        r.id = j["id"].as_string();
        r.name = j["name"].as_string(r.id);
        r.kind = j["kind"].as_string("generic");
        r.parent_id = j["parent_id"].as_string();
        r.capacity = static_cast<int>(j["capacity"].as_int(1));
        r.status = resource_status_from_string(j["status"].as_string("ACTIVE"));
        for (const auto& t : j["tags"].as_array()) r.tags.insert(t.as_string());
        r.attributes = json_to_meta(j["attributes"]);
        return r;
    }
};

struct Actor {
    std::string id;
    std::string name;
    std::set<std::string> roles;
    int credit_score = 100;
    MetadataMap attributes;

    bool has_role(const std::string& r) const { return roles.find(r) != roles.end(); }

    JsonValue to_json() const {
        JsonValue j = JsonValue::object();
        j["id"] = id;
        j["name"] = name;
        JsonValue r_arr = JsonValue::array();
        for (const auto& r : roles) r_arr.push_back(r);
        j["roles"] = std::move(r_arr);
        j["credit_score"] = credit_score;
        j["attributes"] = meta_to_json(attributes);
        return j;
    }

    static Actor from_json(const JsonValue& j) {
        Actor a;
        a.id = j["id"].as_string();
        a.name = j["name"].as_string(a.id);
        for (const auto& r : j["roles"].as_array()) a.roles.insert(r.as_string());
        a.credit_score = static_cast<int>(j["credit_score"].as_int(100));
        a.attributes = json_to_meta(j["attributes"]);
        return a;
    }
};

enum class BookingState {
    PENDING_APPROVAL = 0,
    CONFIRMED = 1,
    WAITLISTED = 2,
    CHECKED_IN = 3,
    COMPLETED = 4,
    CANCELLED = 5,
    REJECTED = 6,
    NO_SHOW = 7,
    FAULT_DISPLACED = 8
};

inline bool occupies_capacity(BookingState s) {
    return s == BookingState::PENDING_APPROVAL ||
           s == BookingState::CONFIRMED ||
           s == BookingState::CHECKED_IN;
}

inline std::string to_string(BookingState s) {
    switch (s) {
        case BookingState::PENDING_APPROVAL: return "PENDING_APPROVAL";
        case BookingState::CONFIRMED: return "CONFIRMED";
        case BookingState::WAITLISTED: return "WAITLISTED";
        case BookingState::CHECKED_IN: return "CHECKED_IN";
        case BookingState::COMPLETED: return "COMPLETED";
        case BookingState::CANCELLED: return "CANCELLED";
        case BookingState::REJECTED: return "REJECTED";
        case BookingState::NO_SHOW: return "NO_SHOW";
        case BookingState::FAULT_DISPLACED: return "FAULT_DISPLACED";
    }
    return "CONFIRMED";
}

inline BookingState booking_state_from_string(const std::string& s) {
    if (s == "PENDING_APPROVAL") return BookingState::PENDING_APPROVAL;
    if (s == "CONFIRMED") return BookingState::CONFIRMED;
    if (s == "WAITLISTED") return BookingState::WAITLISTED;
    if (s == "CHECKED_IN") return BookingState::CHECKED_IN;
    if (s == "COMPLETED") return BookingState::COMPLETED;
    if (s == "CANCELLED") return BookingState::CANCELLED;
    if (s == "REJECTED") return BookingState::REJECTED;
    if (s == "NO_SHOW") return BookingState::NO_SHOW;
    if (s == "FAULT_DISPLACED") return BookingState::FAULT_DISPLACED;
    return BookingState::CONFIRMED;
}

struct BrickDecision {
    std::string brick_name;
    std::string category;
    bool passed = true;
    std::string outcome = "PASS"; // PASS, MUTATED, WAITLIST, APPROVAL_REQUIRED, REJECT
    std::string note;
    double elapsed_us = 0.0;

    JsonValue to_json() const {
        return JsonValue::object({
            {"brick_name", brick_name},
            {"category", category},
            {"passed", passed},
            {"outcome", outcome},
            {"note", note},
            {"elapsed_us", elapsed_us}
        });
    }

    static BrickDecision from_json(const JsonValue& j) {
        BrickDecision d;
        d.brick_name = j["brick_name"].as_string();
        d.category = j["category"].as_string();
        d.passed = j["passed"].as_bool(true);
        d.outcome = j["outcome"].as_string("PASS");
        d.note = j["note"].as_string();
        d.elapsed_us = j["elapsed_us"].as_double();
        return d;
    }
};

struct BookingRequest {
    std::string actor_id;
    std::vector<std::string> resource_ids; // 1 for single resource, >1 for atomic bundle!
    EpochMinute start_time = 0;
    EpochMinute end_time = 0;
    int units = 1;                         // party size / seats requested
    bool allow_waitlist = true;
    std::optional<EpochMinute> now_override;
    std::string series_id;
    MetadataMap metadata;
};

struct BookingRecord {
    std::string id;
    std::string series_id;
    std::string actor_id;
    std::vector<std::string> resource_ids;
    EpochMinute start_time = 0;
    EpochMinute end_time = 0;
    EpochMinute effective_start = 0;
    EpochMinute effective_end = 0;
    int pre_buffer_minutes = 0;
    int post_buffer_minutes = 0;
    int units = 1;
    BookingState status = BookingState::CONFIRMED;
    int priority = 0;
    double price = 0.0;
    double deposit = 0.0;
    double refund_amount = 0.0;
    double penalty_fee = 0.0;
    std::string currency = "CNY";
    EpochMinute created_at = 0;
    EpochMinute updated_at = 0;
    EpochMinute checked_in_at = 0;
    std::string cancel_reason;
    std::string reallocated_from;
    MetadataMap metadata;
    std::vector<BrickDecision> decision_trace;

    std::string primary_resource_id() const {
        return resource_ids.empty() ? "" : resource_ids.front();
    }

    JsonValue to_json() const {
        JsonValue j = JsonValue::object();
        j["id"] = id;
        j["series_id"] = series_id;
        j["actor_id"] = actor_id;
        JsonValue r_arr = JsonValue::array();
        for (const auto& rid : resource_ids) r_arr.push_back(rid);
        j["resource_ids"] = std::move(r_arr);
        j["start_time"] = time_utils::format_datetime(start_time);
        j["end_time"] = time_utils::format_datetime(end_time);
        j["effective_start"] = time_utils::format_datetime(effective_start);
        j["effective_end"] = time_utils::format_datetime(effective_end);
        j["start_epoch_min"] = start_time;
        j["end_epoch_min"] = end_time;
        j["effective_start_min"] = effective_start;
        j["effective_end_min"] = effective_end;
        j["pre_buffer_minutes"] = pre_buffer_minutes;
        j["post_buffer_minutes"] = post_buffer_minutes;
        j["units"] = units;
        j["status"] = to_string(status);
        j["priority"] = priority;
        j["price"] = price;
        j["deposit"] = deposit;
        j["refund_amount"] = refund_amount;
        j["penalty_fee"] = penalty_fee;
        j["currency"] = currency;
        j["created_at"] = time_utils::format_datetime(created_at);
        j["updated_at"] = time_utils::format_datetime(updated_at);
        j["created_epoch_min"] = created_at;
        j["updated_epoch_min"] = updated_at;
        j["checked_in_epoch_min"] = checked_in_at;
        j["checked_in_at"] = checked_in_at > 0 ? time_utils::format_datetime(checked_in_at) : "";
        j["cancel_reason"] = cancel_reason;
        j["reallocated_from"] = reallocated_from;
        j["metadata"] = meta_to_json(metadata);
        JsonValue trace_arr = JsonValue::array();
        for (const auto& d : decision_trace) trace_arr.push_back(d.to_json());
        j["decision_trace"] = std::move(trace_arr);
        return j;
    }

    static BookingRecord from_json(const JsonValue& j) {
        BookingRecord b;
        b.id = j["id"].as_string();
        b.series_id = j["series_id"].as_string();
        b.actor_id = j["actor_id"].as_string();
        for (const auto& r : j["resource_ids"].as_array()) b.resource_ids.push_back(r.as_string());
        b.start_time = j["start_epoch_min"].as_int();
        b.end_time = j["end_epoch_min"].as_int();
        b.effective_start = j["effective_start_min"].as_int(b.start_time);
        b.effective_end = j["effective_end_min"].as_int(b.end_time);
        b.pre_buffer_minutes = static_cast<int>(j["pre_buffer_minutes"].as_int(0));
        b.post_buffer_minutes = static_cast<int>(j["post_buffer_minutes"].as_int(0));
        b.units = static_cast<int>(j["units"].as_int(1));
        b.status = booking_state_from_string(j["status"].as_string("CONFIRMED"));
        b.priority = static_cast<int>(j["priority"].as_int(0));
        b.price = j["price"].as_double(0.0);
        b.deposit = j["deposit"].as_double(0.0);
        b.refund_amount = j["refund_amount"].as_double(0.0);
        b.penalty_fee = j["penalty_fee"].as_double(0.0);
        b.currency = j["currency"].as_string("CNY");
        b.created_at = j["created_epoch_min"].as_int();
        b.updated_at = j["updated_epoch_min"].as_int();
        b.checked_in_at = j["checked_in_epoch_min"].as_int(0);
        b.cancel_reason = j["cancel_reason"].as_string();
        b.reallocated_from = j["reallocated_from"].as_string();
        b.metadata = json_to_meta(j["metadata"]);
        for (const auto& d : j["decision_trace"].as_array()) {
            b.decision_trace.push_back(BrickDecision::from_json(d));
        }
        return b;
    }
};

struct BookingOutcome {
    bool ok = false;
    std::string error_code;
    std::string message;
    BookingRecord record;
    std::vector<BrickDecision> trace;

    JsonValue to_json() const {
        JsonValue t_arr = JsonValue::array();
        for (const auto& d : trace) t_arr.push_back(d.to_json());
        return JsonValue::object({
            {"ok", ok},
            {"error_code", error_code},
            {"message", message},
            {"record", ok ? record.to_json() : JsonValue(nullptr)},
            {"trace", std::move(t_arr)}
        });
    }
};

struct FaultPolicy {
    bool auto_reallocate = true;
    bool match_same_tags = true;
    int compensate_priority = 30;
};

struct FaultReportResult {
    std::string faulted_resource_id;
    std::vector<std::pair<std::string, std::string>> reallocated; // booking_id -> new_resource_id
    std::vector<std::string> displaced;                           // booking_ids marked FAULT_DISPLACED

    JsonValue to_json() const {
        JsonValue r_arr = JsonValue::array();
        for (const auto& [bid, nid] : reallocated) {
            r_arr.push_back(JsonValue::object({{"booking_id", bid}, {"new_resource_id", nid}}));
        }
        JsonValue d_arr = JsonValue::array();
        for (const auto& bid : displaced) d_arr.push_back(bid);
        return JsonValue::object({
            {"faulted_resource_id", faulted_resource_id},
            {"reallocated", std::move(r_arr)},
            {"displaced", std::move(d_arr)}
        });
    }
};

struct SlotSuggestion {
    std::string resource_id;
    std::string resource_name;
    EpochMinute start_time = 0;
    EpochMinute end_time = 0;
    int available_units = 0;
    double estimated_price = 0.0;
    double score = 0.0;
    std::string reason;

    JsonValue to_json() const {
        return JsonValue::object({
            {"resource_id", resource_id},
            {"resource_name", resource_name},
            {"start_time", time_utils::format_datetime(start_time)},
            {"end_time", time_utils::format_datetime(end_time)},
            {"available_units", available_units},
            {"estimated_price", estimated_price},
            {"score", score},
            {"reason", reason}
        });
    }
};

struct RecurringRequest {
    BookingRequest base_request;
    int interval_days = 7;        // e.g. 1 = daily, 7 = weekly
    int occurrences = 4;
    bool all_or_nothing = false;  // if true, rollback all if any single occurrence fails
};

struct RecurringOutcome {
    bool ok = false;
    std::string series_id;
    std::string summary;
    std::vector<BookingOutcome> outcomes;

    JsonValue to_json() const {
        JsonValue arr = JsonValue::array();
        for (const auto& o : outcomes) arr.push_back(o.to_json());
        return JsonValue::object({
            {"ok", ok},
            {"series_id", series_id},
            {"summary", summary},
            {"outcomes", std::move(arr)}
        });
    }
};

} // namespace chronokit
