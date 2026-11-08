#pragma once

#include "chronokit/bricks/brick.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace chronokit {

// 1. OpeningHoursBrick: Business hours, weekday schedule, blackout dates & per-resource overrides
class OpeningHoursBrick : public IBrick {
public:
    OpeningHoursBrick() { configure(JsonValue::object()); }
    explicit OpeningHoursBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "OpeningHours"; }
    std::string category() const override { return "Time"; }
    int order() const override { return 10; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int open_min_ = 8 * 60;
    int close_min_ = 22 * 60;
    std::set<int> weekdays_{1, 2, 3, 4, 5, 6, 7};
    std::set<std::string> blackout_dates_;
};

// 2. SlotGranularityBrick: Grid alignment, min/max duration, step & fixed discrete slot enforcement
class SlotGranularityBrick : public IBrick {
public:
    SlotGranularityBrick() { configure(JsonValue::object()); }
    explicit SlotGranularityBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "SlotGranularity"; }
    std::string category() const override { return "Time"; }
    int order() const override { return 15; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int align_minutes_ = 30;
    int min_duration_ = 30;
    int max_duration_ = 240;
    int step_minutes_ = 0;
    std::vector<std::pair<int, int>> fixed_slots_;
};

// 3. BookingWindowBrick: Minimum lead time & maximum advance booking horizon (with role overrides)
class BookingWindowBrick : public IBrick {
public:
    BookingWindowBrick() { configure(JsonValue::object()); }
    explicit BookingWindowBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "BookingWindow"; }
    std::string category() const override { return "Time"; }
    int order() const override { return 20; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int min_advance_minutes_ = 0;
    int max_advance_days_ = 14;
    std::map<std::string, int> role_max_advance_days_;
};

// 4. BufferTimeBrick: Automatic setup/cleanup/turnover buffer expansion around bookings
class BufferTimeBrick : public IBrick {
public:
    BufferTimeBrick() { configure(JsonValue::object()); }
    explicit BufferTimeBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "BufferTime"; }
    std::string category() const override { return "Time"; }
    int order() const override { return 25; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int pre_buffer_ = 0;
    int post_buffer_ = 15;
    int min_duration_to_apply_ = 0;
};

// 5. RoleAndCreditGateBrick: RBAC permissions, resource tag access gates & credit score thresholds
class RoleAndCreditGateBrick : public IBrick {
public:
    RoleAndCreditGateBrick() { configure(JsonValue::object()); }
    explicit RoleAndCreditGateBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "RoleAndCreditGate"; }
    std::string category() const override { return "Access"; }
    int order() const override { return 30; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;
    int no_show_credit_deduction() const override { return deduct_on_no_show_; }
    int complete_credit_reward() const override { return reward_on_complete_; }

private:
    JsonValue raw_cfg_;
    int min_credit_score_ = 60;
    int deduct_on_no_show_ = 10;
    int reward_on_complete_ = 2;
    std::set<std::string> blocked_roles_;
    std::set<std::string> allowed_roles_;
    std::map<std::string, std::set<std::string>> tag_required_roles_;
};

// 6. PartySizeMatcherBrick: Party size bounds & capacity utilization anti-waste enforcement
class PartySizeMatcherBrick : public IBrick {
public:
    PartySizeMatcherBrick() { configure(JsonValue::object()); }
    explicit PartySizeMatcherBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "PartySizeMatcher"; }
    std::string category() const override { return "Capacity"; }
    int order() const override { return 35; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int min_units_ = 1;
    int max_units_ = 100;
    double min_capacity_ratio_ = 0.0;
    bool enforce_ratio_peak_only_ = false;
    int peak_start_min_ = 18 * 60;
    int peak_end_min_ = 21 * 60;
};

// 7. QuotaLimiterBrick: Active/daily/weekly booking & duration caps + same-kind deduplication
class QuotaLimiterBrick : public IBrick {
public:
    QuotaLimiterBrick() { configure(JsonValue::object()); }
    explicit QuotaLimiterBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "QuotaLimiter"; }
    std::string category() const override { return "Quota"; }
    int order() const override { return 40; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int max_active_per_actor_ = 5;
    int max_daily_bookings_ = 3;
    int max_daily_minutes_ = 360;
    int max_weekly_minutes_ = 1440;
    bool unique_kind_per_day_ = false;
    std::set<std::string> exempt_roles_{"admin"};
    std::map<std::string, double> role_multiplier_;
};

// 8. PricingAndDepositBrick: Multi-mode billing, peak/weekend surge, tag surcharges, discounts & deposits
class PricingAndDepositBrick : public IBrick {
public:
    PricingAndDepositBrick() { configure(JsonValue::object()); }
    explicit PricingAndDepositBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "PricingAndDeposit"; }
    std::string category() const override { return "Pricing"; }
    int order() const override { return 50; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    std::string billing_mode_ = "per_hour"; // free, per_hour, per_unit_hour, per_unit_flat, flat
    double base_rate_ = 0.0;
    int peak_start_min_ = 18 * 60;
    int peak_end_min_ = 21 * 60;
    double peak_multiplier_ = 1.0;
    double weekend_multiplier_ = 1.0;
    int early_bird_hours_ = 0;
    double early_bird_discount_ = 1.0;
    double deposit_ratio_ = 0.0;
    double flat_deposit_ = 0.0;
    std::map<std::string, double> tag_surcharges_;
    std::map<std::string, double> role_discounts_;
};

// 9. WaitlistBrick: Priority scoring & automatic waitlist queue configuration
class WaitlistBrick : public IBrick {
public:
    WaitlistBrick() { configure(JsonValue::object()); }
    explicit WaitlistBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "Waitlist"; }
    std::string category() const override { return "Capacity"; }
    int order() const override { return 68; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;
    bool auto_promote_waitlist() const override { return enabled_ && auto_promote_on_cancel_; }

private:
    JsonValue raw_cfg_;
    bool enabled_ = true;
    int max_waitlist_per_slot_ = 5;
    int base_priority_ = 10;
    double credit_priority_weight_ = 0.0;
    bool auto_promote_on_cancel_ = true;
    std::map<std::string, int> role_priority_bonus_;
};

// 10. ConflictAndCapacityBrick: Sweep-line multi-resource atomic capacity check & waitlist routing
class ConflictAndCapacityBrick : public IBrick {
public:
    ConflictAndCapacityBrick() { configure(JsonValue::object()); }
    explicit ConflictAndCapacityBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "ConflictAndCapacity"; }
    std::string category() const override { return "Capacity"; }
    int order() const override { return 70; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;
    bool is_exclusive_mode() const override { return exclusive_; }

private:
    JsonValue raw_cfg_;
    bool allow_actor_overlap_ = false;
    bool route_full_to_waitlist_ = true;
    bool exclusive_ = false;
};

// 11. ApprovalWorkflowBrick: Conditional routing into PENDING_APPROVAL state
class ApprovalWorkflowBrick : public IBrick {
public:
    ApprovalWorkflowBrick() { configure(JsonValue::object()); }
    explicit ApprovalWorkflowBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "ApprovalWorkflow"; }
    std::string category() const override { return "Workflow"; }
    int order() const override { return 90; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    bool require_always_ = false;
    int duration_threshold_ = 0;
    int capacity_threshold_ = 0;
    double price_threshold_ = 0.0;
    std::set<std::string> trigger_tags_;
    std::set<std::string> auto_approve_roles_{"admin"};
};

// 12. CancellationAndCheckInBrick: Tiered refund/penalty fee, check-in window & no-show grace rule
class CancellationAndCheckInBrick : public IBrick {
public:
    CancellationAndCheckInBrick() { configure(JsonValue::object()); }
    explicit CancellationAndCheckInBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "CancellationAndCheckIn"; }
    std::string category() const override { return "Workflow"; }
    int order() const override { return 95; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_cancel(CancelContext& ctx) override;
    BrickVerdict on_pre_check_in(CheckInContext& ctx) override;
    std::optional<int> no_show_grace_minutes() const override {
        return check_in_grace_after_ > 0 ? std::optional<int>(check_in_grace_after_) : std::nullopt;
    }

private:
    JsonValue raw_cfg_;
    int free_cancel_before_ = 120;
    double late_cancel_fee_ratio_ = 0.3;
    int non_refundable_before_ = 0;
    int forbid_cancel_before_ = 0;
    int check_in_earliest_before_ = 30;
    int check_in_grace_after_ = 15;
};

// Factory Registry for dynamic assembly from C++, JSON, or Lua Blueprints!
class BrickRegistry {
public:
    using Creator = std::function<BrickPtr(const JsonValue&)>;

    static BrickRegistry& instance();
    void register_brick(const std::string& type_id, Creator creator);
    BrickPtr create(const std::string& type_id, const JsonValue& cfg = JsonValue::object()) const;
    std::vector<BrickDescriptor> catalog() const;

private:
    BrickRegistry();
    std::map<std::string, Creator> creators_;
};

} // namespace chronokit
