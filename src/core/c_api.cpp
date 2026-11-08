#include "chronokit/c_api.h"
#include "chronokit/core/time_utils.hpp"
#include "chronokit/engine/appointment_engine.hpp"
#include "chronokit/storage/memory_storage.hpp"
#include "chronokit/storage/sqlite_storage.hpp"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

struct chronokit_engine {
    std::unique_ptr<chronokit::AppointmentEngine> impl;
    std::string last_error;
};

namespace {

char* dup_cstr(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) {
        std::memcpy(p, s.data(), s.size());
        p[s.size()] = '\0';
    }
    return p;
}

chronokit::BookingRequest parse_req_json(const chronokit::JsonValue& body) {
    using namespace chronokit;
    BookingRequest breq;
    breq.actor_id = body["actor_id"].as_string();
    if (body.contains("resource_ids") && body["resource_ids"].is_array()) {
        for (const auto& v : body["resource_ids"].as_array()) breq.resource_ids.push_back(v.as_string());
    } else if (body.contains("resource_id")) {
        breq.resource_ids.push_back(body["resource_id"].as_string());
    }
    breq.start_time = time_utils::must_parse_datetime(body["start_time"].as_string());
    breq.end_time = time_utils::must_parse_datetime(body["end_time"].as_string());
    breq.units = static_cast<int>(body["units"].as_int(1));
    breq.allow_waitlist = body["allow_waitlist"].as_bool(true);
    breq.coupon_code = body["coupon_code"].as_string();
    breq.hold_ttl_minutes = static_cast<int>(body["hold_ttl_minutes"].as_int(0));
    if (body.contains("now") && body["now"].is_string() && !body["now"].as_string().empty()) {
        breq.now_override = time_utils::parse_datetime(body["now"].as_string());
    }
    if (body.contains("metadata") && body["metadata"].is_object()) {
        breq.metadata = json_to_meta(body["metadata"]);
    }
    return breq;
}

} // namespace

extern "C" {

chronokit_engine_t* chronokit_create(const char* sqlite_db_path) {
    try {
        auto* h = new chronokit_engine();
        std::shared_ptr<chronokit::IStorage> st;
        if (sqlite_db_path && sqlite_db_path[0] != '\0') {
            st = std::make_shared<chronokit::SqliteStorage>(sqlite_db_path);
        } else {
            st = std::make_shared<chronokit::MemoryStorage>();
        }
        h->impl = std::make_unique<chronokit::AppointmentEngine>(st);
        return h;
    } catch (...) {
        return nullptr;
    }
}

void chronokit_destroy(chronokit_engine_t* eng) {
    delete eng;
}

int chronokit_load_blueprint_file(chronokit_engine_t* eng, const char* path, int load_seed_data) {
    if (!eng || !path) return -1;
    try {
        eng->impl->load_blueprint_file(path, load_seed_data != 0);
        eng->last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        eng->last_error = e.what();
        return 1;
    }
}

int chronokit_hot_reload_lua(chronokit_engine_t* eng, const char* lua_code, int reset_and_seed) {
    if (!eng || !lua_code) return -1;
    try {
        eng->impl->reload_blueprint_code(lua_code, reset_and_seed != 0);
        eng->last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        eng->last_error = e.what();
        return 1;
    }
}

char* chronokit_book_json(chronokit_engine_t* eng, const char* request_json) {
    if (!eng || !request_json) return dup_cstr("{\"ok\":false,\"error_code\":\"NULL_ARG\"}");
    try {
        auto body = chronokit::JsonValue::parse(request_json);
        auto out = eng->impl->book(parse_req_json(body));
        return dup_cstr(out.to_json().dump());
    } catch (const std::exception& e) {
        eng->last_error = e.what();
        return dup_cstr(chronokit::JsonValue::object({{"ok", false}, {"message", e.what()}}).dump());
    }
}

char* chronokit_simulate_json(chronokit_engine_t* eng, const char* request_json) {
    if (!eng || !request_json) return dup_cstr("{\"ok\":false,\"error_code\":\"NULL_ARG\"}");
    try {
        auto body = chronokit::JsonValue::parse(request_json);
        auto out = eng->impl->simulate_book(parse_req_json(body));
        return dup_cstr(out.to_json().dump());
    } catch (const std::exception& e) {
        eng->last_error = e.what();
        return dup_cstr(chronokit::JsonValue::object({{"ok", false}, {"message", e.what()}}).dump());
    }
}

char* chronokit_cancel_json(chronokit_engine_t* eng, const char* booking_id, const char* actor_id, const char* reason) {
    if (!eng || !booking_id || !actor_id) return dup_cstr("{\"ok\":false}");
    auto out = eng->impl->cancel_booking(booking_id, actor_id, std::nullopt, reason ? reason : "");
    return dup_cstr(out.to_json().dump());
}

char* chronokit_draw_lottery_json(chronokit_engine_t* eng, const char* resource_id_filter, uint64_t seed) {
    if (!eng) return dup_cstr("{\"ok\":false}");
    auto out = eng->impl->draw_lottery(resource_id_filter ? resource_id_filter : "", std::nullopt, seed);
    return dup_cstr(out.to_json().dump());
}

char* chronokit_set_env_json(chronokit_engine_t* eng, const char* env_json) {
    if (!eng || !env_json) return dup_cstr("{\"ok\":false}");
    try {
        auto j = chronokit::JsonValue::parse(env_json);
        eng->impl->set_environment(chronokit::json_to_meta(j));
        return dup_cstr(chronokit::JsonValue::object({
            {"ok", true},
            {"environment", chronokit::meta_to_json(eng->impl->get_environment())}
        }).dump());
    } catch (const std::exception& e) {
        return dup_cstr(chronokit::JsonValue::object({{"ok", false}, {"message", e.what()}}).dump());
    }
}

char* chronokit_stats_json(chronokit_engine_t* eng) {
    if (!eng) return dup_cstr("{}");
    return dup_cstr(eng->impl->stats_summary().dump());
}

char* chronokit_export_ics(chronokit_engine_t* eng, const char* booking_id) {
    if (!eng || !booking_id) return dup_cstr("");
    return dup_cstr(eng->impl->export_booking_ics(booking_id));
}

const char* chronokit_last_error(chronokit_engine_t* eng) {
    if (!eng) return "null engine";
    return eng->last_error.c_str();
}

void chronokit_free_string(char* str) {
    std::free(str);
}

} // extern "C"
