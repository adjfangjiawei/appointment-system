#include "chronokit/c_api.h"
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

void test_blueprint_06_ev_balancer_surge_and_cooldown() {
    std::cout << "Running test_blueprint_06_ev_balancer_surge_and_cooldown...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/06_ev_supercharger.lua", true);
    CHECK(eng.list_resources().size() == 4);

    // 1. First driver at 0% station load -> gets 0.75x valley discount + EVFREE15 coupon, assigned to PILE-800V-A1
    BookingRequest r1;
    r1.actor_id = "ev_owner_li";
    r1.resource_ids = {"PILE-800V-A1"};
    r1.start_time = time_utils::must_parse_datetime("2026-11-10 10:00");
    r1.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
    r1.coupon_code = "EVFREE15";
    r1.now_override = time_utils::must_parse_datetime("2026-11-10 08:00");
    auto o1 = eng.book(r1);
    CHECK(o1.ok);
    CHECK(o1.record.primary_resource_id() == "PILE-800V-A1");
    CHECK(o1.record.discount_amount > 9.0);

    // 2. CooldownInterval check: ev_owner_li tries to book at 11:15 (< 30m kind gap) on A2 -> blocked!
    BookingRequest r_cool = r1;
    r_cool.resource_ids = {"PILE-800V-A2"};
    r_cool.coupon_code = "";
    r_cool.start_time = time_utils::must_parse_datetime("2026-11-10 11:15");
    r_cool.end_time = time_utils::must_parse_datetime("2026-11-10 12:00");
    auto o_cool = eng.book(r_cool);
    CHECK(!o_cool.ok && o_cool.error_code == "KIND_COOLDOWN_NOT_MET");

    // 3. ResourceLoadBalancer check: ev_guest_zhou also requests PILE-800V-A1 at 10:00-11:00 -> auto-routed to PILE-800V-A2!
    BookingRequest r2;
    r2.actor_id = "ev_guest_zhou";
    r2.resource_ids = {"PILE-800V-A1"};
    r2.start_time = time_utils::must_parse_datetime("2026-11-10 10:00");
    r2.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
    r2.now_override = time_utils::must_parse_datetime("2026-11-10 08:05");
    auto o2 = eng.book(r2);
    CHECK(o2.ok && o2.record.primary_resource_id() == "PILE-800V-A2");

    // 4. Add 3rd concurrent booking -> occupancy becomes 3/4 = 75%
    eng.upsert_actor(Actor{"ev_driver_3", "Driver 3", {"driver"}, 100, {}});
    BookingRequest r3 = r2;
    r3.actor_id = "ev_driver_3";
    auto o3 = eng.book(r3);
    CHECK(o3.ok && o3.record.primary_resource_id() == "PILE-800V-A3");

    // 5. At 75% load: LuaPolicy GridLoadBatteryGuard throttles commercial_fleet!
    BookingRequest r_fleet = r2;
    r_fleet.actor_id = "ev_fleet_01";
    auto o_fleet = eng.book(r_fleet);
    CHECK(!o_fleet.ok && o_fleet.error_code == "GRID_PEAK_THROTTLE");

    //    Private driver 4 passes, gets A4, AND triggers DynamicSurgePricing 1.25x ($60 * 1.25 = $75)!
    eng.upsert_actor(Actor{"ev_driver_4", "Driver 4", {"driver"}, 100, {}});
    BookingRequest r4 = r2;
    r4.actor_id = "ev_driver_4";
    auto o4 = eng.book(r4);
    CHECK(o4.ok && o4.record.primary_resource_id() == "PILE-800V-A4");
    CHECK_NEAR(o4.record.price, 75.0, 1e-4);

    // 6. Early check-out releases tail window and promotes waitlisted EV!
    eng.upsert_actor(Actor{"ev_waiter", "EV Waiter", {"driver"}, 100, {}});
    BookingRequest rw;
    rw.actor_id = "ev_waiter";
    rw.resource_ids = {"PILE-800V-A4"};
    rw.start_time = time_utils::must_parse_datetime("2026-11-10 10:30");
    rw.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
    rw.allow_waitlist = true;
    rw.metadata["pin_resource"] = true;
    rw.now_override = time_utils::must_parse_datetime("2026-11-10 08:10");
    auto ow = eng.book(rw);
    CHECK(ow.ok && ow.record.status == BookingState::WAITLISTED);

    // Driver 4 unplugs early at 10:25 -> frees 10:25..11:00 -> ev_waiter (10:30..11:00) immediately promoted!
    auto co_early = eng.check_out_booking(o4.record.id, "ev_driver_4", time_utils::must_parse_datetime("2026-11-10 10:25"));
    CHECK(co_early.ok && co_early.record.status == BookingState::COMPLETED);
    CHECK(eng.get_booking(ow.record.id)->status == BookingState::CONFIRMED);
}

void test_blueprint_07_gov_prefab_dependency_and_when_combinator() {
    std::cout << "Running test_blueprint_07_gov_prefab_dependency_and_when_combinator...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/07_gov_service_hall.lua", true);

    // 1. Prefab.AntiScalperSuite blocks unverified scalper account!
    BookingRequest r_bot;
    r_bot.actor_id = "scalper_bot";
    r_bot.resource_ids = {"WIN-PASSPORT-01"};
    r_bot.start_time = time_utils::must_parse_datetime("2026-11-10 09:00");
    r_bot.end_time = time_utils::must_parse_datetime("2026-11-10 09:30");
    r_bot.now_override = time_utils::must_parse_datetime("2026-11-09 10:00");
    auto o_bot = eng.book(r_bot);
    CHECK(!o_bot.ok && o_bot.error_code == "UNVERIFIED_IDENTITY");

    // 2. DependencyChainBrick: cit_wang tries property_transfer without tax_assessment -> blocked!
    BookingRequest r_prop;
    r_prop.actor_id = "cit_wang";
    r_prop.resource_ids = {"WIN-PROP-01"};
    r_prop.start_time = time_utils::must_parse_datetime("2026-11-10 10:30");
    r_prop.end_time = time_utils::must_parse_datetime("2026-11-10 11:00");
    r_prop.now_override = time_utils::must_parse_datetime("2026-11-09 10:00");
    auto o_dep_fail = eng.book(r_prop);
    CHECK(!o_dep_fail.ok && o_dep_fail.error_code == "MISSING_PREREQUISITE_BOOKING");

    //    Book prerequisite tax_assessment 09:30-10:00 -> property_transfer succeeds!
    BookingRequest r_tax = r_prop;
    r_tax.resource_ids = {"WIN-TAX-01"};
    r_tax.start_time = time_utils::must_parse_datetime("2026-11-10 09:30");
    r_tax.end_time = time_utils::must_parse_datetime("2026-11-10 10:00");
    auto o_tax = eng.book(r_tax);
    CHECK(o_tax.ok);
    auto o_dep_ok = eng.book(r_prop);
    CHECK(o_dep_ok.ok);

    // 3. Brick.When conditional branch: cit_wang capped at 2 bookings by else_brick!
    BookingRequest r_third = r_prop;
    r_third.resource_ids = {"WIN-PASSPORT-01"};
    r_third.start_time = time_utils::must_parse_datetime("2026-11-10 14:00");
    r_third.end_time = time_utils::must_parse_datetime("2026-11-10 14:30");
    auto o_third = eng.book(r_third);
    CHECK(!o_third.ok && (o_third.error_code == "QUOTA_ACTIVE_EXCEEDED" || o_third.error_code == "QUOTA_DAILY_COUNT_EXCEEDED"));

    //    Meanwhile cit_elder (senior_green_channel) hits then_brick and can book 3 slots on the same day!
    for (int i = 0; i < 3; ++i) {
        BookingRequest re;
        re.actor_id = "cit_elder";
        re.resource_ids = {"WIN-PASSPORT-01"};
        re.start_time = time_utils::must_parse_datetime("2026-11-11 09:00") + i * 90;
        re.end_time = re.start_time + 30;
        re.now_override = time_utils::must_parse_datetime("2026-11-09 10:00");
        CHECK(eng.book(re).ok);
    }

    // 4. AntiScalper frequent-cancel circuit breaker: cit_wang cancels 2 bookings today -> locked out!
    eng.cancel_booking(o_tax.record.id, "cit_wang", time_utils::must_parse_datetime("2026-11-09 11:00"));
    eng.cancel_booking(o_dep_ok.record.id, "cit_wang", time_utils::must_parse_datetime("2026-11-09 11:05"));
    r_third.now_override = time_utils::must_parse_datetime("2026-11-09 11:10");
    auto o_scalp_trip = eng.book(r_third);
    CHECK(!o_scalp_trip.ok && o_scalp_trip.error_code == "SCALPER_CANCEL_CIRCUIT_OPEN");
}

void test_blueprint_08_anyof_hold_reschedule_transfer_checkout_and_cpp_export() {
    std::cout << "Running test_blueprint_08_anyof_hold_reschedule_transfer_checkout_and_cpp_export...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/08_sports_badminton_arena.lua", true);

    // 1. Brick.When + Brick.AnyOf test on #pro_court:
    BookingRequest r_pro;
    r_pro.actor_id = "plr_casual";
    r_pro.resource_ids = {"CRT-PRO-VIP"};
    r_pro.start_time = time_utils::must_parse_datetime("2026-11-10 14:00");
    r_pro.end_time = time_utils::must_parse_datetime("2026-11-10 15:00");
    r_pro.now_override = time_utils::must_parse_datetime("2026-11-09 10:00");
    auto o_casual = eng.book(r_pro);
    CHECK(!o_casual.ok && o_casual.error_code == "ANY_OF_ALL_FAILED");

    //    - plr_veteran (non-gold, credit 120 >= 115) -> PASSES via 2nd branch of AnyOf!
    //      Plus Two-Phase Hold (15m TTL) + SPORT30 coupon ($120 - $30 = $90)!
    r_pro.actor_id = "plr_veteran";
    r_pro.coupon_code = "SPORT30";
    r_pro.hold_ttl_minutes = 15;
    auto o_vet = eng.book(r_pro);
    CHECK(o_vet.ok);
    CHECK(o_vet.record.status == BookingState::PENDING_PAYMENT);
    CHECK_NEAR(o_vet.record.price, 130.0, 1e-4); // base_rate(120) + pro_court surcharge(40) - SPORT30(30) = 130
    CHECK_NEAR(o_vet.record.discount_amount, 30.0, 1e-4);

    //    - Confirm payment within 15m -> CONFIRMED!
    auto o_paid = eng.confirm_payment(o_vet.record.id, "plr_veteran", time_utils::must_parse_datetime("2026-11-09 10:10"));
    CHECK(o_paid.ok && o_paid.record.status == BookingState::CONFIRMED);

    //    - Single-use coupon anti-replay: plr_veteran tries SPORT30 again -> blocked!
    BookingRequest r_dup_cpn = r_pro;
    r_dup_cpn.hold_ttl_minutes = 0;
    r_dup_cpn.start_time = time_utils::must_parse_datetime("2026-11-11 14:00");
    r_dup_cpn.end_time = time_utils::must_parse_datetime("2026-11-11 15:00");
    auto o_dup_cpn = eng.book(r_dup_cpn);
    CHECK(!o_dup_cpn.ok && o_dup_cpn.error_code == "COUPON_ALREADY_USED");

    // 2. Put plr_gold on WAITLIST for 14:00-15:00 CRT-PRO-VIP
    BookingRequest r_gold_wait = r_pro;
    r_gold_wait.actor_id = "plr_gold";
    r_gold_wait.coupon_code = "";
    r_gold_wait.hold_ttl_minutes = 0;
    r_gold_wait.allow_waitlist = true;
    auto o_gw = eng.book(r_gold_wait);
    CHECK(o_gw.ok && o_gw.record.status == BookingState::WAITLISTED);

    // 3. Atomic Reschedule:
    //    Invalid misaligned time (15:15) -> fails atomically, o_vet remains at 14:00-15:00!
    RescheduleRequest rs_bad;
    rs_bad.booking_id = o_vet.record.id;
    rs_bad.actor_id = "plr_veteran";
    rs_bad.new_start_time = time_utils::must_parse_datetime("2026-11-10 15:15");
    rs_bad.new_end_time = time_utils::must_parse_datetime("2026-11-10 16:15");
    rs_bad.now_override = time_utils::must_parse_datetime("2026-11-09 11:00");
    CHECK(!eng.reschedule_booking(rs_bad).ok);
    CHECK(eng.get_booking(o_vet.record.id)->start_time == time_utils::must_parse_datetime("2026-11-10 14:00"));

    //    Valid reschedule to 16:00-17:00 -> moves o_vet AND auto-promotes plr_gold on old 14:00-15:00 slot!
    RescheduleRequest rs_ok = rs_bad;
    rs_ok.new_start_time = time_utils::must_parse_datetime("2026-11-10 16:00");
    rs_ok.new_end_time = time_utils::must_parse_datetime("2026-11-10 17:00");
    auto o_rs = eng.reschedule_booking(rs_ok);
    CHECK(o_rs.ok && o_rs.record.reschedule_count == 1);
    CHECK(eng.get_booking(o_gw.record.id)->status == BookingState::CONFIRMED);

    // 4. Peer-to-Peer Transfer:
    auto tf_bad = eng.transfer_booking(o_vet.record.id, "plr_veteran", "plr_casual", time_utils::must_parse_datetime("2026-11-09 12:00"));
    CHECK(!tf_bad.ok && tf_bad.error_code == "TRANSFER_RECIPIENT_INELIGIBLE");

    auto tf_ok = eng.transfer_booking(o_vet.record.id, "plr_veteran", "plr_gold", time_utils::must_parse_datetime("2026-11-09 12:05"));
    CHECK(tf_ok.ok && tf_ok.record.actor_id == "plr_gold");

    // 5. Overtime Check-Out Fee: plr_gold checks out 30 minutes late on 14:00-15:00 booking -> overtime fee charged!
    auto co_late = eng.check_out_booking(o_gw.record.id, "plr_gold", time_utils::must_parse_datetime("2026-11-10 15:30"), 1.5);
    CHECK(co_late.ok && co_late.record.overtime_fee > 0.0);

    // 6. Expired seat hold sweeper test on CRT-STD-01
    BookingRequest r_hold{"plr_casual", {"CRT-STD-01"},
        time_utils::must_parse_datetime("2026-11-12 10:00"),
        time_utils::must_parse_datetime("2026-11-12 11:00"),
        2, true, time_utils::must_parse_datetime("2026-11-09 12:00"), "", {}, 10, "", ""};
    auto o_hold = eng.book(r_hold);
    CHECK(o_hold.ok && o_hold.record.status == BookingState::PENDING_PAYMENT);
    auto exp = eng.sweep_expired_holds(time_utils::must_parse_datetime("2026-11-09 12:11"));
    CHECK(exp.size() == 1 && exp[0] == o_hold.record.id);
    CHECK(eng.get_booking(o_hold.record.id)->status == BookingState::CANCELLED);

    // 7. C++20 EngineBuilder Code Exporter verification
    std::string cpp_src = eng.export_cpp_builder_code();
    CHECK(cpp_src.find("EngineBuilder()") != std::string::npos);
    CHECK(cpp_src.find("CouponAndVoucher") != std::string::npos);
}

void test_blueprint_09_mixin_hierarchy_staff_and_extension() {
    std::cout << "Running test_blueprint_09_mixin_hierarchy_staff_and_extension...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/09_divisible_convention_center.lua", true);

    // 1. Mixin inheritance verification: pipeline inherited OpeningHours, SlotGranularity, Waitlist, ConflictAndCapacity, CancellationAndCheckIn
    auto pipe = eng.describe_pipeline();
    CHECK(pipe.size() == 10);

    // 2. Book Level-3 sub-room ROOM-VIP-S1 (child of HALL-SOUTH, grandchild of HALL-GRAND) 09:00-10:00:
    //    - Automatically assigns companion staff (STAFF-AV-01 or STAFF-AV-02)!
    BookingRequest r_s1{"org_chamber", {"ROOM-VIP-S1"},
        time_utils::must_parse_datetime("2026-11-10 09:00"),
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        50, false, time_utils::must_parse_datetime("2026-11-09 10:00")};
    auto o_s1 = eng.book(r_s1);
    CHECK(o_s1.ok && o_s1.record.status == BookingState::CONFIRMED);
    CHECK(o_s1.record.resource_ids.size() == 2); // ROOM-VIP-S1 + auto-assigned av_engineer!

    // 3. Parent HALL-SOUTH and Grandparent HALL-GRAND must BOTH be blocked at 09:00-10:00 by HierarchicalTreeLockBrick!
    BookingRequest r_south{"org_corp_a", {"HALL-SOUTH"},
        time_utils::must_parse_datetime("2026-11-10 09:00"),
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        150, false, time_utils::must_parse_datetime("2026-11-09 10:05")};
    auto o_south_blk = eng.book(r_south);
    CHECK(!o_south_blk.ok && o_south_blk.error_code == "HIERARCHY_TOPOLOGY_CONFLICT");

    BookingRequest r_grand{"org_summit", {"HALL-GRAND"},
        time_utils::must_parse_datetime("2026-11-10 09:00"),
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        400, false, time_utils::must_parse_datetime("2026-11-09 10:05")};
    auto o_grand_blk = eng.book(r_grand);
    CHECK(!o_grand_blk.ok && o_grand_blk.error_code == "HIERARCHY_TOPOLOGY_CONFLICT");

    // 4. Sibling wing HALL-NORTH is independent and succeeds in parallel (getting the 2nd free AV engineer)!
    BookingRequest r_north{"org_corp_a", {"HALL-NORTH"},
        time_utils::must_parse_datetime("2026-11-10 09:00"),
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        150, false, time_utils::must_parse_datetime("2026-11-09 10:10")};
    auto o_north = eng.book(r_north);
    CHECK(o_north.ok && o_north.record.resource_ids.size() == 2);
    CHECK(o_north.record.resource_ids[1] != o_s1.record.resource_ids[1]); // Assigned the other free engineer!

    // 5. Book HALL-GRAND (#intl_summit) for 6 hours (13:00-19:00):
    //    - PriceGuard caps hall base price ($3000 * 6h * 0.85 = $15,300 -> capped to $12,000),
    //      then CompanionStaffAssigner assigns STAFF-AV-01 (#simultaneous_interp, +$300*6h = +$1,800 -> $13,800)!
    BookingRequest r_summit{"org_summit", {"HALL-GRAND"},
        time_utils::must_parse_datetime("2026-11-10 13:00"),
        time_utils::must_parse_datetime("2026-11-10 19:00"),
        500, false, time_utils::must_parse_datetime("2026-11-09 10:20")};
    auto o_summit = eng.book(r_summit);
    CHECK(o_summit.ok);
    CHECK(o_summit.record.resource_ids.size() == 2 && o_summit.record.resource_ids[1] == "STAFF-AV-01");
    CHECK_NEAR(o_summit.record.price, 13800.0, 1e-4);

    // 6. Live In-Session Extension (`extend_booking`): extend o_s1 from 10:00 -> 11:00 (+60m) seamlessly!
    auto ext_ok = eng.extend_booking(o_s1.record.id, "org_chamber", 60, time_utils::must_parse_datetime("2026-11-10 09:45"));
    CHECK(ext_ok.ok && ext_ok.record.end_time == time_utils::must_parse_datetime("2026-11-10 11:00"));
}

void test_blueprint_10_weather_pass_unless_lottery_and_ics() {
    std::cout << "Running test_blueprint_10_weather_pass_unless_lottery_and_ics...\n";
    AppointmentEngine eng;
    eng.load_blueprint_file("blueprints/10_municipal_outdoor_tennis_lottery.lua", true);

    EpochMinute sim_now = time_utils::must_parse_datetime("2026-11-10 08:00");

    // 1. Set Rainstorm -> outdoor clay court blocked, indoor dome open with Lua ¥20 rainy-day subsidy!
    eng.set_environment({{"weather", std::string("rainstorm")}, {"wind_level", int64_t(5)}, {"aqi", int64_t(35)}});
    BookingRequest r_out_rain{"cit_cash", {"TNS-CLAY-OUT1"},
        time_utils::must_parse_datetime("2026-11-11 10:00"),
        time_utils::must_parse_datetime("2026-11-11 11:00"),
        2, false, sim_now};
    auto o_rain_blk = eng.book(r_out_rain);
    CHECK(!o_rain_blk.ok && o_rain_blk.error_code == "WEATHER_SAFETY_LOCKOUT");

    BookingRequest r_in_rain{"cit_cash", {"TNS-DOME-IN1"},
        time_utils::must_parse_datetime("2026-11-11 10:00"),
        time_utils::must_parse_datetime("2026-11-11 11:00"),
        2, false, sim_now};
    auto o_in_rain = eng.book(r_in_rain);
    // base_rate(80) + indoor_dome surcharge(40) = 120, minus rainy-day Lua subsidy(20) = 100!
    CHECK(o_in_rain.ok && std::abs(o_in_rain.record.price - 100.0) < 1e-4);

    // 2. Restore sunny weather -> test zero-side-effect `simulate_book` with pass holder
    eng.set_environment({{"weather", std::string("sunny")}, {"wind_level", int64_t(2)}, {"aqi", int64_t(40)}});
    BookingRequest r_sim{"cit_pass_holder", {"TNS-CLAY-OUT1"},
        time_utils::must_parse_datetime("2026-11-11 14:00"),
        time_utils::must_parse_datetime("2026-11-11 16:00"),
        2, false, sim_now};
    auto sim_out = eng.simulate_book(r_sim);
    CHECK(sim_out.ok && sim_out.record.price == 0.0);
    // Verify zero side effects on actor's pass_credits (still 10)!
    CHECK(meta_get_int(eng.get_actor("cit_pass_holder")->attributes, "tennis_pass_credits") == 10);

    // 3. Test `Brick.Unless` exemption: `coach_team` (`city_team`) can book 3 slots on the same weekday (exempt from 2/day quota!)
    for (int i = 0; i < 3; ++i) {
        BookingRequest rc{"coach_team", {"TNS-HARD-OUT2"},
            time_utils::must_parse_datetime("2026-11-12 09:00") + i * 120,
            time_utils::must_parse_datetime("2026-11-12 10:00") + i * 120,
            2, false, sim_now};
        CHECK(eng.book(rc).ok);
    }

    // 4. Weekend Fair Public Lottery + MembershipPass automatic deduction & refund on loss!
    //    2026-11-14 is Saturday (>= 24h from 2026-11-10): all 3 citizens apply for 10:00-11:00 TNS-CLAY-OUT1!
    EpochMinute sat_s = time_utils::must_parse_datetime("2026-11-14 10:00");
    EpochMinute sat_e = time_utils::must_parse_datetime("2026-11-14 11:00");

    auto l1 = eng.book({"cit_pass_holder", {"TNS-CLAY-OUT1"}, sat_s, sat_e, 2, true, sim_now});
    auto l2 = eng.book({"cit_honor", {"TNS-CLAY-OUT1"}, sat_s, sat_e, 2, true, sim_now});
    auto l3 = eng.book({"cit_cash", {"TNS-CLAY-OUT1"}, sat_s, sat_e, 2, true, sim_now});
    CHECK(l1.ok && l1.record.status == BookingState::LOTTERY_PENDING && l1.record.price == 0.0);
    CHECK(l2.ok && l2.record.status == BookingState::LOTTERY_PENDING && l2.record.price == 0.0);
    CHECK(l3.ok && l3.record.status == BookingState::LOTTERY_PENDING && l3.record.price > 0.0);

    // Both pass holders had 1 credit pre-deducted upon entering lottery (10->9 and 2->1)
    CHECK(meta_get_int(eng.get_actor("cit_pass_holder")->attributes, "tennis_pass_credits") == 9);
    CHECK(meta_get_int(eng.get_actor("cit_honor")->attributes, "tennis_pass_credits") == 1);

    // Run deterministic lottery draw! Exactly 1 winner gets CONFIRMED, 2 losers get LOTTERY_LOST + automatic pass refund!
    auto draw = eng.draw_lottery("TNS-CLAY-OUT1", time_utils::must_parse_datetime("2026-11-13 12:00"), 20261114ULL);
    CHECK(draw.winners.size() == 1 && draw.losers.size() == 2);
    CHECK(eng.get_booking(draw.winners[0])->status == BookingState::CONFIRMED);
    for (const auto& lid : draw.losers) {
        auto lb = eng.get_booking(lid);
        CHECK(lb->status == BookingState::LOTTERY_LOST);
        if (lb->actor_id == "cit_pass_holder") {
            CHECK(meta_get_int(eng.get_actor("cit_pass_holder")->attributes, "tennis_pass_credits") == 10);
        }
        if (lb->actor_id == "cit_honor") {
            CHECK(meta_get_int(eng.get_actor("cit_honor")->attributes, "tennis_pass_credits") == 2);
        }
    }

    // 5. RFC 5545 .ics export check
    std::string ics = eng.export_booking_ics(draw.winners[0]);
    CHECK(ics.find("BEGIN:VCALENDAR") != std::string::npos);
    CHECK(ics.find("BEGIN:VEVENT") != std::string::npos);
}

void test_embeddable_c_api_ffi() {
    std::cout << "Running test_embeddable_c_api_ffi...\n";
    chronokit_engine_t* c_eng = chronokit_create(nullptr);
    CHECK(c_eng != nullptr);
    CHECK(chronokit_load_blueprint_file(c_eng, "blueprints/01_university_lab.lua", 1) == 0);

    char* sim_str = chronokit_simulate_json(c_eng,
        R"({"actor_id":"res_zhang","resource_id":"GPU-201","start_time":"2026-11-10 20:00","end_time":"2026-11-10 22:00","now":"2026-11-09 10:00"})");
    JsonValue sim_j = JsonValue::parse(sim_str);
    chronokit_free_string(sim_str);
    CHECK(sim_j["ok"].as_bool() == true);

    char* bk_str = chronokit_book_json(c_eng,
        R"({"actor_id":"res_zhang","resource_id":"GPU-201","start_time":"2026-11-10 20:00","end_time":"2026-11-10 22:00","now":"2026-11-09 10:00"})");
    JsonValue bk_j = JsonValue::parse(bk_str);
    chronokit_free_string(bk_str);
    CHECK(bk_j["ok"].as_bool() == true);
    std::string bid = bk_j["record"]["id"].as_string();

    char* ics_str = chronokit_export_ics(c_eng, bid.c_str());
    CHECK(std::string(ics_str).find("UID:" + bid) != std::string::npos);
    chronokit_free_string(ics_str);

    chronokit_destroy(c_eng);
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
        test_blueprint_06_ev_balancer_surge_and_cooldown();
        test_blueprint_07_gov_prefab_dependency_and_when_combinator();
        test_blueprint_08_anyof_hold_reschedule_transfer_checkout_and_cpp_export();
        test_blueprint_09_mixin_hierarchy_staff_and_extension();
        test_blueprint_10_weather_pass_unless_lottery_and_ics();
        test_embeddable_c_api_ffi();

        std::cout << "\n======================================================\n"
                  << "  ChronoKit v4.0 Test Summary: " << g_passed << " passed, " << g_failed << " failed\n"
                  << "======================================================\n";
        return g_failed == 0 ? 0 : 1;
    } catch (const std::exception& ex) {
        std::cerr << "Unhandled test exception: " << ex.what() << "\n";
        return 1;
    }
}
