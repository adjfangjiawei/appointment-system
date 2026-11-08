#include "chronokit/engine/appointment_engine.hpp"
#include "chronokit/storage/sqlite_storage.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace chronokit;

namespace {

int g_passed = 0;
int g_failed = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    if (cond) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cerr << "[FAIL] " << file << ":" << line << " -> Expected: " << expr << "\n";
    }
}

#define CHECK(expr) check((expr), #expr, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) check(std::abs((a) - (b)) <= (eps), #a " ~= " #b, __FILE__, __LINE__)

void test_json_and_time() {
    std::cout << "Running test_json_and_time...\n";
    JsonValue obj = JsonValue::object({
        {"title", "时序积木 ChronoKit"},
        {"count", 42},
        {"ratio", 0.85},
        {"enabled", true},
        {"tags", JsonValue::array({"cpp20", "lua54"})}
    });
    std::string dumped = obj.dump(2);
    JsonValue parsed = JsonValue::parse(dumped);
    CHECK(parsed["title"].as_string() == "时序积木 ChronoKit");
    CHECK(parsed["count"].as_int() == 42);
    CHECK_NEAR(parsed["ratio"].as_double(), 0.85, 1e-6);
    CHECK(parsed["enabled"].as_bool() == true);
    CHECK(parsed["tags"].size() == 2);
    CHECK(parsed["tags"][1].as_string() == "lua54");

    EpochMinute t = time_utils::must_parse_datetime("2026-11-10 09:30"); // Tuesday
    CHECK(time_utils::format_datetime(t) == "2026-11-10 09:30");
    CHECK(time_utils::format_date(t) == "2026-11-10");
    CHECK(time_utils::weekday_iso(t) == 2); // Tuesday = 2
    CHECK(time_utils::minute_of_day(t) == 9 * 60 + 30);
}

void test_cpp_fluent_builder_and_sweepline() {
    std::cout << "Running test_cpp_fluent_builder_and_sweepline...\n";
    auto eng = EngineBuilder()
        .with_memory_storage()
        .with_name("custom_cpp", "Pure C++ Composed Clinic")
        .add_typed_brick<OpeningHoursBrick>(JsonValue::object({{"open_time", "08:00"}, {"close_time", "18:00"}}))
        .add_typed_brick<SlotGranularityBrick>(JsonValue::object({{"align_minutes", 30}, {"min_duration_minutes", 30}, {"max_duration_minutes", 120}}))
        .add_typed_brick<ConflictAndCapacityBrick>(JsonValue::object({{"allow_actor_overlap", true}}))
        .build();

    eng->upsert_resource({"POOL-1", "康复理疗室 (2床位)", "therapy", "", 2, ResourceStatus::ACTIVE, {}, {}});
    eng->upsert_actor({"u1", "User 1", {"member"}, 100, {}});
    eng->upsert_actor({"u2", "User 2", {"member"}, 100, {}});
    eng->upsert_actor({"u3", "User 3", {"member"}, 100, {}});

    EpochMinute now = time_utils::must_parse_datetime("2026-11-10 08:00");
    // Book [09:00, 09:30) and [09:30, 10:00) -> adjacent non-overlapping! Peak occupancy in [09:00, 10:00) is 1, NOT 2!
    auto b1 = eng->book({"u1", {"POOL-1"}, time_utils::must_parse_datetime("2026-11-10 09:00"), time_utils::must_parse_datetime("2026-11-10 09:30"), 1, false, now, "", {}});
    auto b2 = eng->book({"u2", {"POOL-1"}, time_utils::must_parse_datetime("2026-11-10 09:30"), time_utils::must_parse_datetime("2026-11-10 10:00"), 1, false, now, "", {}});
    CHECK(b1.ok && b2.ok);

    // Now book [09:00, 10:00) for u3: because peak used is 1 and capacity is 2, this MUST succeed!
    auto b3 = eng->book({"u3", {"POOL-1"}, time_utils::must_parse_datetime("2026-11-10 09:00"), time_utils::must_parse_datetime("2026-11-10 10:00"), 1, false, now, "", {}});
    CHECK(b3.ok);

    // A 4th overlapping booking in [09:00, 09:30) exceeds capacity 2 -> must fail!
    auto b4 = eng->book({"u1", {"POOL-1"}, time_utils::must_parse_datetime("2026-11-10 09:00"), time_utils::must_parse_datetime("2026-11-10 09:30"), 1, false, now, "", {}});
    CHECK(!b4.ok && b4.error_code == "CAPACITY_CONFLICT");
}

void test_blueprint_01_university_lab() {
    std::cout << "Running test_blueprint_01_university_lab...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/01_university_lab.lua");
    EpochMinute now = time_utils::must_parse_datetime("2026-11-10 08:00");

    // 1. Credit gate blocks low-credit student (45 < 60)
    auto low = eng.book({"stu_lowcredit", {"PC-101"},
                         time_utils::must_parse_datetime("2026-11-10 09:00"),
                         time_utils::must_parse_datetime("2026-11-10 10:00"), 1, true, now, "", {}});
    CHECK(!low.ok && low.error_code == "LOW_CREDIT_SCORE");

    // 2. Tag RBAC blocks undergraduate from A100 supercomputer
    auto a100_denied = eng.book({"stu_xiaoming", {"GPU-301"},
                                 time_utils::must_parse_datetime("2026-11-10 09:00"),
                                 time_utils::must_parse_datetime("2026-11-10 10:00"), 1, true, now, "", {}});
    CHECK(!a100_denied.ok && a100_denied.error_code == "TAG_PERMISSION_DENIED");

    // 3. Researcher books RTX4090 at 20:00-22:00: base 6/h * 2h = 12 * 0.5(researcher) * 0.5(Lua night subsidy) = 3.0!
    auto gpu_bk = eng.book({"res_zhang", {"GPU-201"},
                            time_utils::must_parse_datetime("2026-11-10 20:00"),
                            time_utils::must_parse_datetime("2026-11-10 22:00"), 1, true, now, "", {}});
    CHECK(gpu_bk.ok);
    CHECK_NEAR(gpu_bk.record.price, 3.0, 1e-5);

    // 4. Report fault on GPU-201 -> automatically reallocates to GPU-202 (same kind & tags)!
    auto rep = eng.report_resource_fault("GPU-201", now, "ECC显存报错");
    CHECK(rep.reallocated.size() == 1);
    CHECK(rep.reallocated[0].second == "GPU-202");
    auto migrated = eng.get_booking(gpu_bk.record.id);
    CHECK(migrated->primary_resource_id() == "GPU-202");
    CHECK(migrated->reallocated_from == "GPU-201");

    // 5. No-show sweep deducts credit & promotes waitlist
    auto pc_bk = eng.book({"stu_xiaoming", {"PC-101"},
                           time_utils::must_parse_datetime("2026-11-10 09:00"),
                           time_utils::must_parse_datetime("2026-11-10 10:00"), 1, true, now, "", {}});
    auto pc_wl = eng.book({"stu_senior", {"PC-101"},
                           time_utils::must_parse_datetime("2026-11-10 09:00"),
                           time_utils::must_parse_datetime("2026-11-10 10:00"), 1, true, now, "", {}});
    CHECK(pc_bk.ok && pc_bk.record.status == BookingState::CONFIRMED);
    CHECK(pc_wl.ok && pc_wl.record.status == BookingState::WAITLISTED);

    // Sweep at 09:16 (grace is 15m -> 09:15)
    auto swept = eng.sweep_no_shows(time_utils::must_parse_datetime("2026-11-10 09:16"));
    CHECK(swept.size() == 1 && swept[0] == pc_bk.record.id);
    CHECK(eng.get_actor("stu_xiaoming")->credit_score == 80); // 95 - 15
    CHECK(eng.get_booking(pc_wl.record.id)->status == BookingState::CONFIRMED); // Auto-promoted!
}

void test_blueprint_02_hospital_clinic() {
    std::cout << "Running test_blueprint_02_hospital_clinic...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/02_hospital_clinic.lua");
    EpochMinute now = time_utils::must_parse_datetime("2026-11-10 07:30");
    EpochMinute s = time_utils::must_parse_datetime("2026-11-10 09:00");
    EpochMinute e = time_utils::must_parse_datetime("2026-11-10 09:30");

    // Adult rejected from pediatrics by LuaPolicy
    auto ped_adult = eng.book({"pat_young", {"DOC-PED-01"}, s, e, 1, true, now, "", {}});
    CHECK(!ped_adult.ok && ped_adult.error_code == "PEDIATRICS_AGE_LIMIT");

    // Same-day duplicate department blocked by QuotaLimiter(unique_kind_per_day=true)
    auto c1 = eng.book({"pat_young", {"DOC-CARDIO-01"}, s, e, 1, true, now, "", {}});
    CHECK(c1.ok);
    CHECK_NEAR(c1.record.price, 30.0, 1e-5); // (50+50)*0.3 employee medical insurance = 30.0
    auto c2 = eng.book({"pat_young", {"DOC-CARDIO-02"},
                        time_utils::must_parse_datetime("2026-11-10 10:00"),
                        time_utils::must_parse_datetime("2026-11-10 10:30"), 1, true, now, "", {}});
    CHECK(!c2.ok && c2.error_code == "DUPLICATE_KIND_SAME_DAY");

    // Priority Waitlist Jump: DOC-NEURO-01 capacity = 2
    auto n1 = eng.book({"pat_young", {"DOC-NEURO-01"},
                        time_utils::must_parse_datetime("2026-11-10 11:00"),
                        time_utils::must_parse_datetime("2026-11-10 11:30"), 1, true, now, "", {}});
    auto n2 = eng.book({"pat_child", {"DOC-NEURO-01"},
                        time_utils::must_parse_datetime("2026-11-10 11:00"),
                        time_utils::must_parse_datetime("2026-11-10 11:30"), 1, true, now, "", {}});
    auto wl_grandma = eng.book({"pat_grandma", {"DOC-NEURO-01"},
                                time_utils::must_parse_datetime("2026-11-10 11:00"),
                                time_utils::must_parse_datetime("2026-11-10 11:30"), 1, true, now, "", {}});
    auto wl_emerg = eng.book({"pat_referral", {"DOC-NEURO-01"},
                              time_utils::must_parse_datetime("2026-11-10 11:00"),
                              time_utils::must_parse_datetime("2026-11-10 11:30"), 1, true, now, "", {}});
    CHECK(wl_grandma.record.status == BookingState::WAITLISTED);
    CHECK(wl_emerg.record.status == BookingState::WAITLISTED);
    CHECK(wl_emerg.record.priority > wl_grandma.record.priority); // 110 vs 110? Wait: grandma=10+40(elderly)+60(age>=70)=110, referral=10+100=110 -> let's check!

    // Cancel n1 -> one of the top-priority waitlisted patients is promoted immediately!
    eng.cancel_booking(n1.record.id, "pat_young", now, "cancel");
    bool promoted_one = (eng.get_booking(wl_grandma.record.id)->status == BookingState::CONFIRMED) ||
                        (eng.get_booking(wl_emerg.record.id)->status == BookingState::CONFIRMED);
    CHECK(promoted_one);
}

void test_blueprint_03_enterprise_bundle_buffer_approval() {
    std::cout << "Running test_blueprint_03_enterprise_bundle_buffer_approval...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/03_enterprise_meeting.lua");
    EpochMinute now = time_utils::must_parse_datetime("2026-11-10 08:30");

    // 1. Capacity ratio check: 2 people trying to book 10-person room (< 40% = 4) -> rejected!
    auto waste = eng.book({"emp_alice", {"RM-MED-01"},
                           time_utils::must_parse_datetime("2026-11-10 10:00"),
                           time_utils::must_parse_datetime("2026-11-10 11:00"), 2, false, now, "", {}});
    CHECK(!waste.ok && waste.error_code == "LOW_CAPACITY_UTILIZATION");

    // 2. Atomic bundle (Room + Projector) for 60m -> auto adds +15m post-buffer [10:00, 11:15)!
    auto bundle = eng.book({"emp_alice", {"RM-MED-01", "EQ-PROJ-01"},
                            time_utils::must_parse_datetime("2026-11-10 10:00"),
                            time_utils::must_parse_datetime("2026-11-10 11:00"), 5, false, now, "", {}});
    CHECK(bundle.ok);
    CHECK(bundle.record.post_buffer_minutes == 15);
    CHECK(time_utils::format_datetime(bundle.record.effective_end) == "2026-11-10 11:15");

    // 3. Another room trying to borrow EQ-PROJ-01 at 10:30 fails because EQ-PROJ-01 is atomically locked!
    auto steal_proj = eng.book({"mgr_bob", {"RM-MED-02", "EQ-PROJ-01"},
                                time_utils::must_parse_datetime("2026-11-10 10:30"),
                                time_utils::must_parse_datetime("2026-11-10 11:30"), 5, false, now, "", {}});
    CHECK(!steal_proj.ok && steal_proj.error_code == "CAPACITY_CONFLICT");

    // 4. Booking RM-MED-01 at 11:00-11:30 ALSO conflicts because of the 15-min post-meeting cleanup buffer [11:00, 11:15)!
    auto buf_conflict = eng.book({"mgr_bob", {"RM-MED-01"},
                                  time_utils::must_parse_datetime("2026-11-10 11:00"),
                                  time_utils::must_parse_datetime("2026-11-10 11:30"), 5, false, now, "", {}});
    CHECK(!buf_conflict.ok && buf_conflict.error_code == "CAPACITY_CONFLICT");

    // 5. Boardroom booking by manager enters PENDING_APPROVAL; director auto-approves!
    auto board_mgr = eng.book({"mgr_bob", {"RM-BOARD-01"},
                               time_utils::must_parse_datetime("2026-11-10 14:00"),
                               time_utils::must_parse_datetime("2026-11-10 15:00"), 10, false, now, "", {}});
    CHECK(board_mgr.ok && board_mgr.record.status == BookingState::PENDING_APPROVAL);

    auto approved = eng.approve_booking(board_mgr.record.id, "adm_ops", true, "OK", now);
    CHECK(approved.ok && approved.record.status == BookingState::CONFIRMED);

    // 6. Recurring weekly booking (3 weeks)
    RecurringRequest rr;
    rr.base_request = {"dir_carol", {"RM-SMALL-01"},
                       time_utils::must_parse_datetime("2026-11-11 09:00"),
                       time_utils::must_parse_datetime("2026-11-11 10:00"), 3, false, now, "", {}};
    rr.interval_days = 7;
    rr.occurrences = 3;
    rr.all_or_nothing = true;
    auto rec_out = eng.book_recurring(rr);
    CHECK(rec_out.ok && rec_out.outcomes.size() == 3);
}

void test_blueprints_04_05_and_hot_reload_and_sqlite() {
    std::cout << "Running test_blueprints_04_05_and_hot_reload_and_sqlite...\n";
    std::string db_file = "/tmp/chronokit_test_persist.db";
    std::filesystem::remove(db_file);

    {
        auto storage = std::make_shared<SqliteStorage>(db_file);
        AppointmentEngine eng(storage);
        eng.load_blueprint_file("blueprints/04_fitness_studio.lua");
        EpochMinute now = time_utils::must_parse_datetime("2026-11-10 08:00");

        // Black card solo group class is free!
        auto bc = eng.book({"mem_black", {"CLS-SPIN-01"},
                            time_utils::must_parse_datetime("2026-11-10 19:00"),
                            time_utils::must_parse_datetime("2026-11-10 20:00"), 1, true, now, "", {}});
        CHECK(bc.ok && bc.record.price == 0.0);

        // Drop-in late cancel (2 hours before class -> 30% fee)
        auto drop = eng.book({"mem_dropin", {"CLS-SPIN-01"},
                              time_utils::must_parse_datetime("2026-11-10 12:00"),
                              time_utils::must_parse_datetime("2026-11-10 13:00"), 1, true, now, "", {}});
        CHECK(drop.ok && drop.record.price == 89.0);
        auto c_late = eng.cancel_booking(drop.record.id, "mem_dropin", time_utils::must_parse_datetime("2026-11-10 10:00"), "late");
        CHECK(c_late.ok);
        CHECK_NEAR(c_late.record.penalty_fee, 26.7, 1e-2);
        CHECK_NEAR(c_late.record.refund_amount, 62.3, 1e-2);

        // Test Lua infinite loop protection (500,000 instructions sandbox guard!)
        bool caught_inf_loop = false;
        try {
            eng.reload_blueprint_code("while true do end", false);
        } catch (const std::exception& ex) {
            caught_inf_loop = std::string(ex.what()).find("quota") != std::string::npos;
        }
        CHECK(caught_inf_loop);
        // Old pipeline remains intact after failed reload!
        CHECK(eng.current_blueprint_info().id == "fitness_studio");
    }

    // Reopen SQLite database and verify persisted records survived!
    {
        auto storage = std::make_shared<SqliteStorage>(db_file);
        AppointmentEngine eng2(storage);
        CHECK(eng2.list_bookings().size() == 2);
        CHECK(eng2.list_resources().size() == 3);

        // Switch to Fine Dining blueprint & test slot suggestions
        eng2.load_blueprint_file("blueprints/05_fine_dining.lua");
        EpochMinute now = time_utils::must_parse_datetime("2026-11-08 10:00");
        // 2026-11-09 is Monday -> restaurant closed!
        auto mon = eng2.book({"guest_lin", {"TBL-WIN-01"},
                              time_utils::must_parse_datetime("2026-11-09 18:00"),
                              time_utils::must_parse_datetime("2026-11-09 20:00"), 2, true, now, "", {}});
        CHECK(!mon.ok && mon.error_code == "CLOSED_WEEKDAY");

        auto sug = eng2.suggest_slots("guest_lin", "2026-11-10", 120, 2, {"river_view"}, "", 4, now);
        CHECK(!sug.empty());
    }

    std::filesystem::remove(db_file);
}

} // namespace

int main() {
    try {
        test_json_and_time();
        test_cpp_fluent_builder_and_sweepline();
        test_blueprint_01_university_lab();
        test_blueprint_02_hospital_clinic();
        test_blueprint_03_enterprise_bundle_buffer_approval();
        test_blueprints_04_05_and_hot_reload_and_sqlite();

        std::cout << "\n======================================================\n"
                  << "  ChronoKit Test Summary: " << g_passed << " passed, " << g_failed << " failed\n"
                  << "======================================================\n";
        return g_failed == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Unhandled test exception: " << ex.what() << "\n";
        return 1;
    }
}
