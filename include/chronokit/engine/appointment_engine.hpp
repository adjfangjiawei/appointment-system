#pragma once

#include "chronokit/bricks/brick.hpp"
#include "chronokit/bricks/builtin_bricks.hpp"
#include "chronokit/bricks/v3_bricks.hpp"
#include "chronokit/bricks/v4_bricks.hpp"
#include "chronokit/core/event_bus.hpp"
#include "chronokit/core/types.hpp"
#include "chronokit/lua/lua_vm.hpp"
#include "chronokit/storage/storage.hpp"

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace chronokit {

class AppointmentEngine {
public:
    explicit AppointmentEngine(std::shared_ptr<IStorage> storage = nullptr);
    ~AppointmentEngine();

    // Blueprint & Composable Brick Pipeline Assembly
    void load_blueprint_file(const std::string& path, bool load_seed_data = true);
    void reload_blueprint_code(const std::string& lua_code, bool reset_and_seed = false, const std::string& chunk_name = "live_studio.lua");

    void add_brick(BrickPtr brick);
    bool remove_brick(const std::string& type_or_instance_name);
    void clear_bricks();

    std::vector<BrickDescriptor> describe_pipeline() const;
    static std::vector<BrickDescriptor> available_brick_catalog();
    BlueprintSpec current_blueprint_info() const;

    // Code Generation: Export active pipeline into ready-to-compile C++20 EngineBuilder code!
    std::string export_cpp_builder_code() const;

    // Resource & Actor CRUD
    void upsert_resource(const Resource& r);
    std::optional<Resource> get_resource(const std::string& id) const;
    std::vector<Resource> list_resources() const;

    void upsert_actor(const Actor& a);
    std::optional<Actor> get_actor(const std::string& id) const;
    std::vector<Actor> list_actors() const;

    // Core, v3 & v4 Lifecycle Booking Operations
    BookingOutcome book(const BookingRequest& req);
    BookingOutcome simulate_book(const BookingRequest& req); // Zero-side-effect What-If dry-run!
    RecurringOutcome book_recurring(const RecurringRequest& req);

    // Live External Environment / Weather / IoT Sensor Context
    void set_environment(const MetadataMap& env);
    void set_env_var(const std::string& key, const MetaValue& val);
    MetadataMap get_environment() const;

    // Live In-Session Extension (续时加钟: atomic tail conflict & hierarchy check!)
    BookingOutcome extend_booking(
        const std::string& booking_id,
        const std::string& actor_id,
        int extra_minutes,
        std::optional<EpochMinute> now_opt = std::nullopt);

    // Fair Auditable Public Lottery Draw (公证摇号抽签: weighted Efraimidis-Spirakis sampling!)
    LotteryDrawResult draw_lottery(
        const std::string& resource_id_filter = "",
        std::optional<EpochMinute> now_opt = std::nullopt,
        uint64_t seed_override = 0);

    // RFC 5545 Standard iCalendar (.ics) Exporter
    std::string export_booking_ics(const std::string& booking_id) const;
    std::string export_actor_ics(const std::string& actor_id) const;

    // Two-phase seat hold confirmation (PENDING_PAYMENT -> CONFIRMED)
    BookingOutcome confirm_payment(
        const std::string& booking_id,
        const std::string& actor_id,
        std::optional<EpochMinute> now_opt = std::nullopt);

    // Atomic reschedule (改签: preserves original booking on failure; promotes old slot waitlist on success!)
    BookingOutcome reschedule_booking(const RescheduleRequest& req);

    // Atomic peer-to-peer transfer (转赠/代班移交: validates recipient RBAC, credit, quota & conflicts!)
    BookingOutcome transfer_booking(
        const std::string& booking_id,
        const std::string& from_actor_id,
        const std::string& to_actor_id,
        std::optional<EpochMinute> now_opt = std::nullopt);

    BookingOutcome cancel_booking(
        const std::string& booking_id,
        const std::string& actor_id,
        std::optional<EpochMinute> now_opt = std::nullopt,
        const std::string& reason = "",
        bool by_admin = false);

    BookingOutcome approve_booking(
        const std::string& booking_id,
        const std::string& approver_id,
        bool approve,
        const std::string& note = "",
        std::optional<EpochMinute> now_opt = std::nullopt);

    BookingOutcome check_in(
        const std::string& booking_id,
        const std::string& actor_id,
        std::optional<EpochMinute> now_opt = std::nullopt);

    // Check-out with early-release waitlist promotion & overstay overtime fee settlement
    BookingOutcome check_out_booking(
        const std::string& booking_id,
        const std::string& actor_id,
        std::optional<EpochMinute> now_opt = std::nullopt,
        double overtime_rate_multiplier = 1.5);

    BookingOutcome complete_booking(
        const std::string& booking_id,
        std::optional<EpochMinute> now_opt = std::nullopt);

    // Autonomous Lifecycle Sweeps, Fault Auto-Reallocation & Smart Recommendations
    std::vector<std::string> sweep_no_shows(EpochMinute now);
    std::vector<std::string> sweep_expired_holds(EpochMinute now);

    FaultReportResult report_resource_fault(
        const std::string& resource_id,
        std::optional<EpochMinute> now_opt = std::nullopt,
        const std::string& reason = "设备故障检修");
    bool restore_resource(const std::string& resource_id, std::optional<EpochMinute> now_opt = std::nullopt);

    std::vector<SlotSuggestion> suggest_slots(
        const std::string& actor_id,
        const std::string& date_str,
        int duration_minutes,
        int units = 1,
        const std::set<std::string>& required_tags = {},
        const std::string& kind_filter = "",
        size_t max_results = 8,
        std::optional<EpochMinute> now_opt = std::nullopt);

    // Queries, Timeline Matrix, Observability & Logs
    std::optional<BookingRecord> get_booking(const std::string& id) const;
    std::vector<BookingRecord> list_bookings() const;
    JsonValue timeline_matrix(const std::string& date_str) const;
    std::vector<DomainEvent> recent_events(size_t limit = 50) const { return event_bus_.recent_events(limit); }
    std::vector<std::string> recent_lua_logs(size_t limit = 50) const;
    JsonValue stats_summary() const;
    EventBus& event_bus() { return event_bus_; }

    void set_metadata(const std::string& id, const std::string& name, const std::string& desc = "");
    void set_fault_policy(const FaultPolicy& fp);

private:
    mutable std::recursive_mutex mu_;
    std::shared_ptr<IStorage> storage_;
    std::shared_ptr<LuaVM> lua_vm_;
    std::vector<BrickPtr> bricks_;
    BlueprintSpec active_bp_;
    MetadataMap env_context_;
    EventBus event_bus_;
    uint64_t series_seq_ = 100;

    void sort_pipeline();
    BookingOutcome evaluate_and_book_locked(const BookingRequest& req, bool dry_run);
    std::vector<std::string> try_promote_waitlist_locked(EpochMinute now);
};

// Fluent C++ Builder for programmatic LEGO brick composition without Lua (or hybrid C++ + Lua!)
class EngineBuilder {
public:
    EngineBuilder();

    EngineBuilder& with_storage(std::shared_ptr<IStorage> storage);
    EngineBuilder& with_memory_storage();
    EngineBuilder& with_sqlite_storage(const std::string& db_path);
    EngineBuilder& with_name(const std::string& id, const std::string& name, const std::string& desc = "");
    EngineBuilder& with_fault_policy(const FaultPolicy& fp);
    EngineBuilder& add_brick(BrickPtr brick);
    EngineBuilder& add_brick_by_name(const std::string& type_id, const JsonValue& cfg = JsonValue::object());

    template <typename BrickT>
    EngineBuilder& add_typed_brick(const JsonValue& cfg = JsonValue::object()) {
        return add_brick(std::make_shared<BrickT>(cfg));
    }

    EngineBuilder& with_lua_blueprint(const std::string& blueprint_path, bool load_seed = true);

    std::unique_ptr<AppointmentEngine> build();

private:
    std::shared_ptr<IStorage> storage_;
    std::string sys_id_ = "cpp_composed";
    std::string sys_name_ = "C++ Composed Appointment System";
    std::string sys_desc_;
    FaultPolicy fault_policy_;
    std::vector<BrickPtr> bricks_;
    std::string blueprint_path_;
    bool load_seed_ = true;
};

} // namespace chronokit
