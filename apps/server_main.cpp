#include "chronokit/core/time_utils.hpp"
#include "chronokit/engine/appointment_engine.hpp"
#include "chronokit/server/http_server.hpp"
#include "chronokit/storage/memory_storage.hpp"
#include "chronokit/storage/sqlite_storage.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;
using namespace chronokit;

namespace {

std::string read_text_file(const std::string& p) {
    std::ifstream ifs(p);
    if (!ifs) return "";
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::optional<EpochMinute> opt_parse_dt(const JsonValue& body, const std::string& key) {
    if (!body.contains(key) || !body[key].is_string()) return std::nullopt;
    std::string s = body[key].as_string();
    if (s.empty()) return std::nullopt;
    return time_utils::parse_datetime(s);
}

void seed_demo_bookings_for_blueprint(AppointmentEngine& eng, const std::string& bp_id) {
    if (bp_id == "university_lab") {
        BookingRequest r1;
        r1.actor_id = "stu_alice";
        r1.resource_ids = {"GPU-A100-01"};
        r1.start_time = time_utils::must_parse_datetime("2026-11-10 09:00");
        r1.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
        r1.now_override = time_utils::must_parse_datetime("2026-11-09 10:00");
        eng.book(r1);

        BookingRequest r2;
        r2.actor_id = "prof_zhang";
        r2.resource_ids = {"GPU-A100-01"};
        r2.start_time = time_utils::must_parse_datetime("2026-11-10 09:00");
        r2.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
        r2.allow_waitlist = true;
        r2.now_override = time_utils::must_parse_datetime("2026-11-09 10:05");
        eng.book(r2);
    } else if (bp_id == "hospital_clinic") {
        BookingRequest r1;
        r1.actor_id = "pat_wang";
        r1.resource_ids = {"DOC-CARDIO-01"};
        r1.start_time = time_utils::must_parse_datetime("2026-11-10 09:00");
        r1.end_time = time_utils::must_parse_datetime("2026-11-10 09:30");
        r1.now_override = time_utils::must_parse_datetime("2026-11-09 09:00");
        eng.book(r1);
    } else if (bp_id == "enterprise_meeting") {
        BookingRequest r1;
        r1.actor_id = "emp_chen";
        r1.resource_ids = {"RM-EXEC-20F", "DEV-BOARD-01"};
        r1.start_time = time_utils::must_parse_datetime("2026-11-10 14:00");
        r1.end_time = time_utils::must_parse_datetime("2026-11-10 15:30");
        r1.units = 8;
        r1.now_override = time_utils::must_parse_datetime("2026-11-09 10:00");
        eng.book(r1);
    } else if (bp_id == "ev_supercharger") {
        BookingRequest r1;
        r1.actor_id = "ev_owner_li";
        r1.resource_ids = {"PILE-800V-A1"};
        r1.start_time = time_utils::must_parse_datetime("2026-11-10 10:00");
        r1.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
        r1.coupon_code = "EVFREE15";
        r1.now_override = time_utils::must_parse_datetime("2026-11-10 09:00");
        eng.book(r1);
    } else if (bp_id == "sports_badminton_arena") {
        BookingRequest r1;
        r1.actor_id = "plr_veteran";
        r1.resource_ids = {"CRT-PRO-VIP"};
        r1.start_time = time_utils::must_parse_datetime("2026-11-10 19:00");
        r1.end_time = time_utils::must_parse_datetime("2026-11-10 21:00");
        r1.coupon_code = "SPORT30";
        r1.now_override = time_utils::must_parse_datetime("2026-11-09 12:00");
        eng.book(r1);
    }
}

} // namespace

int main(int argc, char** argv) {
    uint16_t port = 8080;
    std::string db_path = "";
    std::string blueprints_dir = "blueprints";
    std::string ui_path = "apps/web_ui/index.html";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--blueprints" && i + 1 < argc) {
            blueprints_dir = argv[++i];
        } else if (arg == "--ui" && i + 1 < argc) {
            ui_path = argv[++i];
        }
    }

    if (!fs::exists(blueprints_dir) && fs::exists("../blueprints")) {
        blueprints_dir = "../blueprints";
    }
    if (!fs::exists(ui_path) && fs::exists("../apps/web_ui/index.html")) {
        ui_path = "../apps/web_ui/index.html";
    }

    std::shared_ptr<IStorage> storage;
    if (!db_path.empty()) {
        storage = std::make_shared<SqliteStorage>(db_path);
    } else {
        storage = std::make_shared<MemoryStorage>();
    }

    AppointmentEngine engine(storage);

    std::vector<std::string> bp_files;
    if (fs::exists(blueprints_dir)) {
        for (const auto& entry : fs::directory_iterator(blueprints_dir)) {
            if (entry.path().extension() == ".lua") {
                bp_files.push_back(entry.path().string());
            }
        }
        std::sort(bp_files.begin(), bp_files.end());
    }

    if (!bp_files.empty()) {
        engine.load_blueprint_file(bp_files.front(), true);
        seed_demo_bookings_for_blueprint(engine, engine.current_blueprint_info().id);
    }

    HttpServer server;

    server.route("GET", "/", [ui_path](const HttpRequest&) -> HttpResponse {
        std::string html = read_text_file(ui_path);
        if (html.empty()) {
            return HttpResponse::html("ChronoKit Studio UI file not found at " + ui_path, 404);
        }
        return HttpResponse::html(std::move(html));
    });

    server.route("GET", "/api/blueprints", [blueprints_dir, &engine](const HttpRequest&) -> HttpResponse {
        std::vector<std::string> files;
        if (fs::exists(blueprints_dir)) {
            for (const auto& entry : fs::directory_iterator(blueprints_dir)) {
                if (entry.path().extension() == ".lua") files.push_back(entry.path().string());
            }
            std::sort(files.begin(), files.end());
        }

        JsonValue arr = JsonValue::array();
        LuaVM temp_vm;
        for (const auto& f : files) {
            try {
                auto spec = temp_vm.load_blueprint_file(f);
                arr.push_back(JsonValue::object({
                    {"id", spec.id},
                    {"name", spec.name},
                    {"description", spec.description},
                    {"file", fs::path(f).filename().string()},
                    {"path", f},
                    {"brick_count", spec.bricks.size()},
                    {"resource_count", spec.seed_resources.size()}
                }));
            } catch (const std::exception& e) {
                arr.push_back(JsonValue::object({
                    {"file", fs::path(f).filename().string()},
                    {"error", e.what()}
                }));
            }
        }

        auto cur = engine.current_blueprint_info();
        return HttpResponse::json(JsonValue::object({
            {"active_id", cur.id},
            {"active_name", cur.name},
            {"active_lua_source", cur.lua_source},
            {"blueprints", std::move(arr)}
        }));
    });

    server.route("POST", "/api/blueprints/activate", [blueprints_dir, &engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        std::string file = body["file"].as_string();
        bool seed_demo = body["seed_demo"].as_bool(true);
        std::string full_path = (fs::path(blueprints_dir) / fs::path(file).filename()).string();
        if (!fs::exists(full_path)) {
            return HttpResponse::json(JsonValue::object({{"ok", false}, {"message", "Blueprint file not found: " + file}}), 404);
        }
        try {
            engine.load_blueprint_file(full_path, true);
            if (seed_demo) seed_demo_bookings_for_blueprint(engine, engine.current_blueprint_info().id);
            return HttpResponse::json(JsonValue::object({
                {"ok", true},
                {"blueprint_id", engine.current_blueprint_info().id},
                {"name", engine.current_blueprint_info().name},
                {"lua_source", engine.current_blueprint_info().lua_source}
            }));
        } catch (const std::exception& e) {
            return HttpResponse::json(JsonValue::object({{"ok", false}, {"message", e.what()}}), 400);
        }
    });

    server.route("POST", "/api/blueprints/hot-reload", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        std::string code = body["lua_code"].as_string();
        bool reset_data = body["reset_data"].as_bool(false);
        try {
            engine.reload_blueprint_code(code, reset_data);
            return HttpResponse::json(JsonValue::object({
                {"ok", true},
                {"message", "Lua 蓝图热重载已即时生效"},
                {"bricks_count", engine.describe_pipeline().size()}
            }));
        } catch (const std::exception& e) {
            return HttpResponse::json(JsonValue::object({{"ok", false}, {"message", e.what()}}), 400);
        }
    });

    server.route("GET", "/api/pipeline", [&engine](const HttpRequest&) -> HttpResponse {
        JsonValue pipe = JsonValue::array();
        for (const auto& d : engine.describe_pipeline()) pipe.push_back(d.to_json());

        JsonValue cat = JsonValue::array();
        for (const auto& c : AppointmentEngine::available_brick_catalog()) cat.push_back(c.to_json());

        return HttpResponse::json(JsonValue::object({
            {"pipeline", std::move(pipe)},
            {"catalog", std::move(cat)}
        }));
    });

    server.route("POST", "/api/pipeline/add-brick", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        std::string type_id = body["type_id"].as_string();
        JsonValue cfg = body.contains("config") ? body["config"] : JsonValue::object();
        auto brick = BrickRegistry::instance().create(type_id, cfg);
        if (!brick) {
            return HttpResponse::json(JsonValue::object({
                {"ok", false},
                {"message", "不支持通过 JSON 直接实例化的积木类型: " + type_id + " (组合子或 LuaPolicy 请在 Lua 编辑器中声明)"}
            }), 400);
        }
        engine.add_brick(brick);
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"message", "已动态挂载积木 [" + brick->instance_name() + "]"},
            {"bricks_count", engine.describe_pipeline().size()}
        }));
    });

    server.route("POST", "/api/pipeline/remove-brick", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        std::string name = body["name"].as_string();
        bool removed = engine.remove_brick(name);
        return HttpResponse::json(JsonValue::object({
            {"ok", removed},
            {"message", removed ? ("已动态卸载积木 [" + name + "]") : ("未找到积木: " + name)},
            {"bricks_count", engine.describe_pipeline().size()}
        }));
    });

    server.route("GET", "/api/export/cpp", [&engine](const HttpRequest&) -> HttpResponse {
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"cpp_code", engine.export_cpp_builder_code()}
        }));
    });

    server.route("GET", "/api/timeline", [&engine](const HttpRequest& req) -> HttpResponse {
        std::string date = "2026-11-10";
        auto it = req.query.find("date");
        if (it != req.query.end() && !it->second.empty()) date = it->second;
        return HttpResponse::json(engine.timeline_matrix(date));
    });

    server.route("GET", "/api/state", [&engine](const HttpRequest&) -> HttpResponse {
        JsonValue res_arr = JsonValue::array();
        for (const auto& r : engine.list_resources()) res_arr.push_back(r.to_json());

        JsonValue act_arr = JsonValue::array();
        for (const auto& a : engine.list_actors()) act_arr.push_back(a.to_json());

        JsonValue bk_arr = JsonValue::array();
        auto bks = engine.list_bookings();
        std::reverse(bks.begin(), bks.end());
        for (const auto& b : bks) bk_arr.push_back(b.to_json());

        JsonValue ev_arr = JsonValue::array();
        for (const auto& e : engine.recent_events(40)) ev_arr.push_back(e.to_json());

        JsonValue log_arr = JsonValue::array();
        for (const auto& l : engine.recent_lua_logs(40)) log_arr.push_back(l);

        return HttpResponse::json(JsonValue::object({
            {"stats", engine.stats_summary()},
            {"resources", std::move(res_arr)},
            {"actors", std::move(act_arr)},
            {"bookings", std::move(bk_arr)},
            {"events", std::move(ev_arr)},
            {"lua_logs", std::move(log_arr)}
        }));
    });

    server.route("POST", "/api/book", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto st = opt_parse_dt(body, "start_time");
        auto et = opt_parse_dt(body, "end_time");
        if (!st || !et) {
            return HttpResponse::json(JsonValue::object({{"ok", false}, {"message", "start_time / end_time 时间格式应为 YYYY-MM-DD HH:MM"}}), 400);
        }

        BookingRequest breq;
        breq.actor_id = body["actor_id"].as_string();
        if (body.contains("resource_ids") && body["resource_ids"].is_array()) {
            for (const auto& v : body["resource_ids"].as_array()) breq.resource_ids.push_back(v.as_string());
        } else if (body.contains("resource_id")) {
            breq.resource_ids.push_back(body["resource_id"].as_string());
        }
        breq.start_time = *st;
        breq.end_time = *et;
        breq.units = static_cast<int>(body["units"].as_int(1));
        breq.allow_waitlist = body["allow_waitlist"].as_bool(true);
        breq.coupon_code = body["coupon_code"].as_string();
        breq.hold_ttl_minutes = static_cast<int>(body["hold_ttl_minutes"].as_int(0));
        breq.now_override = opt_parse_dt(body, "now");
        if (body.contains("metadata") && body["metadata"].is_object()) {
            breq.metadata = json_to_meta(body["metadata"]);
        }

        int occurrences = static_cast<int>(body["occurrences"].as_int(1));
        if (occurrences > 1) {
            RecurringRequest rreq;
            rreq.base_request = breq;
            rreq.occurrences = occurrences;
            rreq.interval_days = static_cast<int>(body["interval_days"].as_int(7));
            rreq.all_or_nothing = body["all_or_nothing"].as_bool(false);
            auto rout = engine.book_recurring(rreq);
            return HttpResponse::json(rout.to_json(), rout.ok ? 200 : 400);
        }

        auto out = engine.book(breq);
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/pay", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.confirm_payment(
            body["booking_id"].as_string(),
            body["actor_id"].as_string(),
            opt_parse_dt(body, "now"));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/reschedule", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto st = opt_parse_dt(body, "new_start_time");
        auto et = opt_parse_dt(body, "new_end_time");
        if (!st || !et) {
            return HttpResponse::json(JsonValue::object({{"ok", false}, {"message", "new_start_time / new_end_time 格式错误"}}), 400);
        }
        RescheduleRequest rreq;
        rreq.booking_id = body["booking_id"].as_string();
        rreq.actor_id = body["actor_id"].as_string();
        if (body.contains("new_resource_ids") && body["new_resource_ids"].is_array()) {
            for (const auto& v : body["new_resource_ids"].as_array()) {
                if (!v.as_string().empty()) rreq.new_resource_ids.push_back(v.as_string());
            }
        }
        rreq.new_start_time = *st;
        rreq.new_end_time = *et;
        rreq.now_override = opt_parse_dt(body, "now");
        auto out = engine.reschedule_booking(rreq);
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/transfer", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.transfer_booking(
            body["booking_id"].as_string(),
            body["from_actor_id"].as_string(),
            body["to_actor_id"].as_string(),
            opt_parse_dt(body, "now"));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/cancel", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.cancel_booking(
            body["booking_id"].as_string(),
            body["actor_id"].as_string(),
            opt_parse_dt(body, "now"),
            body["reason"].as_string(),
            body["by_admin"].as_bool(false));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/approve", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.approve_booking(
            body["booking_id"].as_string(),
            body["approver_id"].as_string("admin"),
            body["approve"].as_bool(true),
            body["note"].as_string(),
            opt_parse_dt(body, "now"));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/check-in", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.check_in(
            body["booking_id"].as_string(),
            body["actor_id"].as_string(),
            opt_parse_dt(body, "now"));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/check-out", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.check_out_booking(
            body["booking_id"].as_string(),
            body["actor_id"].as_string("admin"),
            opt_parse_dt(body, "now"),
            body["overtime_multiplier"].as_double(1.5));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/bookings/complete", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto out = engine.complete_booking(body["booking_id"].as_string(), opt_parse_dt(body, "now"));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/resources/fault", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        auto rep = engine.report_resource_fault(
            body["resource_id"].as_string(),
            opt_parse_dt(body, "now"),
            body["reason"].as_string("硬件突发故障检修"));
        return HttpResponse::json(rep.to_json());
    });

    server.route("POST", "/api/resources/restore", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        bool ok = engine.restore_resource(body["resource_id"].as_string(), opt_parse_dt(body, "now"));
        return HttpResponse::json(JsonValue::object({{"ok", ok}}));
    });

    server.route("POST", "/api/sweep-no-shows", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        EpochMinute now = opt_parse_dt(body, "now").value_or(time_utils::now_local_minutes());
        auto swept = engine.sweep_no_shows(now);
        auto holds = engine.sweep_expired_holds(now);
        JsonValue arr = JsonValue::array();
        for (const auto& id : swept) arr.push_back(id);
        JsonValue harr = JsonValue::array();
        for (const auto& id : holds) harr.push_back(id);
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"swept_count", swept.size()},
            {"swept_ids", std::move(arr)},
            {"expired_holds_count", holds.size()},
            {"expired_hold_ids", std::move(harr)}
        }));
    });

    server.route("POST", "/api/suggest", [&engine](const HttpRequest& req) -> HttpResponse {
        auto body = req.json();
        std::set<std::string> tags;
        if (body.contains("tags") && body["tags"].is_array()) {
            for (const auto& t : body["tags"].as_array()) {
                if (!t.as_string().empty()) tags.insert(t.as_string());
            }
        }
        auto list = engine.suggest_slots(
            body["actor_id"].as_string(),
            body["date"].as_string("2026-11-10"),
            static_cast<int>(body["duration_minutes"].as_int(60)),
            static_cast<int>(body["units"].as_int(1)),
            tags,
            body["kind"].as_string(),
            static_cast<size_t>(body["max_results"].as_int(6)),
            opt_parse_dt(body, "now"));
        JsonValue arr = JsonValue::array();
        for (const auto& s : list) arr.push_back(s.to_json());
        return HttpResponse::json(JsonValue::object({{"ok", true}, {"suggestions", std::move(arr)}}));
    });

    std::cout << "====================================================================\n"
              << "  ChronoKit Studio v3.0 (C++20 + Lua 5.4 Composable Engine)\n"
              << "  Listening on http://0.0.0.0:" << port << "\n"
              << "  Active Blueprint: " << engine.current_blueprint_info().name << "\n"
              << "  Storage Backend : " << storage->backend_name() << "\n"
              << "====================================================================\n" << std::flush;

    server.listen_and_serve("0.0.0.0", port);
    return 0;
}
