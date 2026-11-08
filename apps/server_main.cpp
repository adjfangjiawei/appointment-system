#include "chronokit/engine/appointment_engine.hpp"
#include "chronokit/server/http_server.hpp"
#include "chronokit/storage/sqlite_storage.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace chronokit;
namespace fs = std::filesystem;

namespace {

std::string read_text_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) return "";
    std::stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

// Seed 2-3 realistic initial bookings for each blueprint so the live Studio visualization pops immediately!
void seed_demo_bookings(AppointmentEngine& eng, const std::string& bp_id) {
    EpochMinute sim_now = time_utils::must_parse_datetime("2026-11-10 08:00");
    if (bp_id == "university_lab") {
        eng.book({"stu_xiaoming", {"PC-101"},
                  time_utils::must_parse_datetime("2026-11-10 09:00"),
                  time_utils::must_parse_datetime("2026-11-10 11:00"), 1, true, sim_now, "", {}});
        eng.book({"res_zhang", {"GPU-201"},
                  time_utils::must_parse_datetime("2026-11-10 20:00"),
                  time_utils::must_parse_datetime("2026-11-10 22:00"), 1, true, sim_now, "", {}});
        eng.book({"stu_senior", {"GPU-201"},
                  time_utils::must_parse_datetime("2026-11-10 20:00"),
                  time_utils::must_parse_datetime("2026-11-10 22:00"), 1, true, sim_now, "", {}});
    } else if (bp_id == "hospital_clinic") {
        EpochMinute s = time_utils::must_parse_datetime("2026-11-10 09:00");
        EpochMinute e = time_utils::must_parse_datetime("2026-11-10 09:30");
        eng.book({"pat_young", {"DOC-NEURO-01"}, s, e, 1, true, sim_now, "", {}});
        eng.book({"pat_grandma", {"DOC-NEURO-01"}, s, e, 1, true, sim_now, "", {}});
        eng.book({"pat_referral", {"DOC-NEURO-01"}, s, e, 1, true, sim_now, "", {}});
    } else if (bp_id == "enterprise_meeting") {
        eng.book({"mgr_bob", {"RM-MED-01", "EQ-PROJ-01"},
                  time_utils::must_parse_datetime("2026-11-10 14:00"),
                  time_utils::must_parse_datetime("2026-11-10 16:30"), 6, true, sim_now, "", {}});
        eng.book({"emp_alice", {"RM-SMALL-01"},
                  time_utils::must_parse_datetime("2026-11-10 10:00"),
                  time_utils::must_parse_datetime("2026-11-10 11:00"), 3, true, sim_now, "", {}});
    } else if (bp_id == "fitness_studio") {
        eng.book({"mem_black", {"CLS-REFORMER-01"},
                  time_utils::must_parse_datetime("2026-11-10 19:00"),
                  time_utils::must_parse_datetime("2026-11-10 20:00"), 1, true, sim_now, "", {}});
        eng.book({"mem_gold", {"CLS-SPIN-01"},
                  time_utils::must_parse_datetime("2026-11-13 19:00"),
                  time_utils::must_parse_datetime("2026-11-13 20:00"), 2, true, sim_now, "", {}});
    } else if (bp_id == "fine_dining") {
        MetadataMap m;
        m["occasion"] = std::string("anniversary");
        eng.book({"guest_lin", {"TBL-WIN-01"},
                  time_utils::must_parse_datetime("2026-11-10 19:00"),
                  time_utils::must_parse_datetime("2026-11-10 21:00"), 2, true, sim_now, "", m});
        eng.book({"guest_vip", {"SUITE-VIP-88"},
                  time_utils::must_parse_datetime("2026-11-10 18:30"),
                  time_utils::must_parse_datetime("2026-11-10 21:00"), 8, true, sim_now, "", {}});
    }
}

} // namespace

int main(int argc, char** argv) {
    uint16_t port = 8080;
    std::string db_path;
    std::string blueprints_dir = "blueprints";
    std::string ui_path = "apps/web_ui/index.html";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = static_cast<uint16_t>(std::stoi(argv[++i]));
        else if (arg == "--db" && i + 1 < argc) db_path = argv[++i];
        else if (arg == "--blueprints" && i + 1 < argc) blueprints_dir = argv[++i];
        else if (arg == "--ui" && i + 1 < argc) ui_path = argv[++i];
    }

    std::shared_ptr<IStorage> storage;
    if (!db_path.empty()) {
        storage = std::make_shared<SqliteStorage>(db_path);
    }
    AppointmentEngine engine(storage);

    // Load default blueprint
    std::string first_bp = blueprints_dir + "/01_university_lab.lua";
    if (fs::exists(first_bp)) {
        engine.load_blueprint_file(first_bp, true);
        seed_demo_bookings(engine, engine.current_blueprint_info().id);
    }

    HttpServer server;

    server.route("GET", "/", [&](const HttpRequest&) {
        std::string html = read_text_file(ui_path);
        if (html.empty()) {
            return HttpResponse::html("<h1>ChronoKit Server Running</h1><p>UI file not found at " + ui_path + "</p>");
        }
        return HttpResponse::html(std::move(html));
    });

    server.route("GET", "/api/blueprints", [&](const HttpRequest&) {
        JsonValue files = JsonValue::array();
        if (fs::exists(blueprints_dir)) {
            std::vector<std::string> paths;
            for (const auto& entry : fs::directory_iterator(blueprints_dir)) {
                if (entry.path().extension() == ".lua") {
                    paths.push_back(entry.path().string());
                }
            }
            std::sort(paths.begin(), paths.end());
            for (const auto& p : paths) {
                files.push_back(JsonValue::object({
                    {"filename", fs::path(p).filename().string()},
                    {"path", p}
                }));
            }
        }
        JsonValue cat = JsonValue::array();
        for (const auto& b : AppointmentEngine::available_brick_catalog()) cat.push_back(b.to_json());
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"presets", std::move(files)},
            {"brick_catalog", std::move(cat)}
        }));
    });

    server.route("GET", "/api/state", [&](const HttpRequest&) {
        auto bp = engine.current_blueprint_info();
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
            {"ok", true},
            {"blueprint", bp.summary_json()},
            {"lua_source", bp.lua_source},
            {"stats", engine.stats_summary()},
            {"resources", std::move(res_arr)},
            {"actors", std::move(act_arr)},
            {"bookings", std::move(bk_arr)},
            {"events", std::move(ev_arr)},
            {"lua_logs", std::move(log_arr)}
        }));
    });

    server.route("POST", "/api/blueprint/switch", [&](const HttpRequest& req) {
        auto j = req.json();
        std::string path = j["path"].as_string();
        bool seed_demo = j["seed_demo"].as_bool(true);
        engine.load_blueprint_file(path, true);
        if (seed_demo) {
            seed_demo_bookings(engine, engine.current_blueprint_info().id);
        }
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"blueprint", engine.current_blueprint_info().summary_json()}
        }));
    });

    server.route("POST", "/api/blueprint/reload", [&](const HttpRequest& req) {
        auto j = req.json();
        std::string code = j["lua_code"].as_string();
        bool reset = j["reset_data"].as_bool(false);
        engine.reload_blueprint_code(code, reset, "live_studio_hot_reload.lua");
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"message", "Lua 蓝图热重载生效！"},
            {"blueprint", engine.current_blueprint_info().summary_json()}
        }));
    });

    server.route("POST", "/api/book", [&](const HttpRequest& req) {
        auto j = req.json();
        BookingRequest br;
        br.actor_id = j["actor_id"].as_string();
        if (j["resource_ids"].is_array()) {
            for (const auto& r : j["resource_ids"].as_array()) br.resource_ids.push_back(r.as_string());
        } else if (j.contains("resource_id")) {
            br.resource_ids.push_back(j["resource_id"].as_string());
        }
        br.start_time = time_utils::must_parse_datetime(j["start_time"].as_string());
        br.end_time = time_utils::must_parse_datetime(j["end_time"].as_string());
        br.units = static_cast<int>(j["units"].as_int(1));
        br.allow_waitlist = j["allow_waitlist"].as_bool(true);
        if (j.contains("now") && !j["now"].as_string().empty()) {
            br.now_override = time_utils::parse_datetime(j["now"].as_string());
        } else {
            br.now_override = time_utils::must_parse_datetime("2026-11-10 08:00");
        }
        br.metadata = json_to_meta(j["metadata"]);

        auto out = engine.book(br);
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/book_recurring", [&](const HttpRequest& req) {
        auto j = req.json();
        RecurringRequest rr;
        rr.base_request.actor_id = j["actor_id"].as_string();
        for (const auto& r : j["resource_ids"].as_array()) rr.base_request.resource_ids.push_back(r.as_string());
        rr.base_request.start_time = time_utils::must_parse_datetime(j["start_time"].as_string());
        rr.base_request.end_time = time_utils::must_parse_datetime(j["end_time"].as_string());
        rr.base_request.units = static_cast<int>(j["units"].as_int(1));
        rr.base_request.now_override = time_utils::parse_datetime(j["now"].as_string("2026-11-10 08:00"));
        rr.interval_days = static_cast<int>(j["interval_days"].as_int(7));
        rr.occurrences = static_cast<int>(j["occurrences"].as_int(3));
        rr.all_or_nothing = j["all_or_nothing"].as_bool(false);
        return HttpResponse::json(engine.book_recurring(rr).to_json());
    });

    server.route("POST", "/api/cancel", [&](const HttpRequest& req) {
        auto j = req.json();
        auto now_opt = time_utils::parse_datetime(j["now"].as_string("2026-11-10 08:30"));
        auto out = engine.cancel_booking(
            j["booking_id"].as_string(),
            j["actor_id"].as_string(),
            now_opt,
            j["reason"].as_string("控制台操作取消"),
            j["by_admin"].as_bool(false));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/approve", [&](const HttpRequest& req) {
        auto j = req.json();
        auto now_opt = time_utils::parse_datetime(j["now"].as_string("2026-11-10 08:30"));
        auto out = engine.approve_booking(
            j["booking_id"].as_string(),
            j["approver_id"].as_string("admin"),
            j["approve"].as_bool(true),
            j["note"].as_string(),
            now_opt);
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/checkin", [&](const HttpRequest& req) {
        auto j = req.json();
        auto b_opt = engine.get_booking(j["booking_id"].as_string());
        std::optional<EpochMinute> now_opt;
        if (j.contains("now") && !j["now"].as_string().empty()) {
            now_opt = time_utils::parse_datetime(j["now"].as_string());
        } else if (b_opt) {
            now_opt = b_opt->start_time; // Simulate on-time check-in by default in Studio!
        }
        auto out = engine.check_in(j["booking_id"].as_string(), j["actor_id"].as_string("admin"), now_opt);
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/complete", [&](const HttpRequest& req) {
        auto j = req.json();
        auto out = engine.complete_booking(j["booking_id"].as_string(), time_utils::must_parse_datetime("2026-11-10 22:00"));
        return HttpResponse::json(out.to_json());
    });

    server.route("POST", "/api/sweep_noshows", [&](const HttpRequest& req) {
        auto j = req.json();
        EpochMinute sweep_at = time_utils::must_parse_datetime(j["now"].as_string("2026-11-10 12:00"));
        auto swept = engine.sweep_no_shows(sweep_at);
        JsonValue arr = JsonValue::array();
        for (const auto& id : swept) arr.push_back(id);
        return HttpResponse::json(JsonValue::object({
            {"ok", true},
            {"swept_count", swept.size()},
            {"swept_ids", std::move(arr)}
        }));
    });

    server.route("POST", "/api/resource/fault", [&](const HttpRequest& req) {
        auto j = req.json();
        std::string rid = j["resource_id"].as_string();
        std::string action = j["action"].as_string("fault");
        EpochMinute now = time_utils::must_parse_datetime(j["now"].as_string("2026-11-10 08:15"));
        if (action == "restore") {
            bool ok = engine.restore_resource(rid, now);
            return HttpResponse::json(JsonValue::object({{"ok", ok}}));
        }
        auto rep = engine.report_resource_fault(rid, now, j["reason"].as_string("硬件突发故障检修"));
        return HttpResponse::json(JsonValue::object({{"ok", true}, {"report", rep.to_json()}}));
    });

    server.route("POST", "/api/suggest", [&](const HttpRequest& req) {
        auto j = req.json();
        std::set<std::string> tags;
        for (const auto& t : j["required_tags"].as_array()) tags.insert(t.as_string());
        auto list = engine.suggest_slots(
            j["actor_id"].as_string(),
            j["date"].as_string("2026-11-10"),
            static_cast<int>(j["duration_minutes"].as_int(60)),
            static_cast<int>(j["units"].as_int(1)),
            tags,
            j["kind"].as_string(""),
            static_cast<size_t>(j["max_results"].as_int(6)),
            time_utils::must_parse_datetime("2026-11-10 08:00"));
        JsonValue arr = JsonValue::array();
        for (const auto& s : list) arr.push_back(s.to_json());
        return HttpResponse::json(JsonValue::object({{"ok", true}, {"suggestions", std::move(arr)}}));
    });

    std::cout << "====================================================================\n"
              << "  ChronoKit Studio & REST Engine listening on http://0.0.0.0:" << port << "\n"
              << "  Storage Backend : " << engine.stats_summary()["storage_backend"].as_string() << "\n"
              << "  Active Blueprint: " << engine.current_blueprint_info().name << "\n"
              << "====================================================================\n"
              << std::flush;

    server.listen_and_serve("0.0.0.0", port);
    return 0;
}
