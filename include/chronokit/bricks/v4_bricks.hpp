#pragma once

#include "chronokit/bricks/brick.hpp"
#include "chronokit/bricks/v3_bricks.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace chronokit {

// ============================================================================
// 22. EnvironmentalRuleBrick (Stage 22): Weather / AQI / Wind / IoT Safety Gate
// ============================================================================
class EnvironmentalRuleBrick : public IBrick {
public:
    explicit EnvironmentalRuleBrick(const JsonValue& cfg = JsonValue::object()) { configure(cfg); }

    std::string type_id() const override { return "EnvironmentalRule"; }
    std::string category() const override { return "Temporal"; }
    int order() const override { return 22; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    std::string outdoor_tag_ = "outdoor";
    std::set<std::string> blocked_weather_{"rainstorm", "typhoon", "lightning", "blizzard", "hail"};
    std::set<std::string> advisory_weather_{"light_rain", "cloudy_windy", "heatwave"};
    int max_wind_level_ = 6;
    int max_aqi_ = 200;
    JsonValue raw_cfg_;
};

// ============================================================================
// 23. MembershipPassBrick (Stage 59): Pass / Class-Pack / Quota Card Deduction & Refund
// ============================================================================
class MembershipPassBrick : public IBrick {
public:
    explicit MembershipPassBrick(const JsonValue& cfg = JsonValue::object()) { configure(cfg); }

    std::string type_id() const override { return "MembershipPass"; }
    std::string category() const override { return "Pricing"; }
    int order() const override { return 59; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;
    void on_post_book(BookingContext& ctx, BookingRecord& record) override;
    void on_post_cancel(CancelContext& ctx, BookingRecord& record) override;

private:
    std::string credit_attr_ = "pass_credits";
    std::string cost_mode_ = "per_booking"; // per_booking | per_hour | per_unit
    int credits_per_unit_ = 1;
    bool require_pass_ = false;
    bool refund_on_free_cancel_ = true;
    JsonValue raw_cfg_;
};

// ============================================================================
// 24. PriceGuardBrick (Stage 62): Regulatory Price Floor / Ceiling & Rounding Rail
// ============================================================================
class PriceGuardBrick : public IBrick {
public:
    explicit PriceGuardBrick(const JsonValue& cfg = JsonValue::object()) { configure(cfg); }

    std::string type_id() const override { return "PriceGuard"; }
    std::string category() const override { return "Pricing"; }
    int order() const override { return 62; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    double min_price_ = 0.0;
    double max_price_ = 0.0; // 0 = no cap
    std::string round_mode_ = "two_decimals"; // two_decimals | nearest_int | floor_int
    JsonValue raw_cfg_;
};

// ============================================================================
// 25. CompanionStaffAssignerBrick (Stage 66): Dual-Axis Facility + Staff Shift Matcher
// ============================================================================
class CompanionStaffAssignerBrick : public IBrick {
public:
    explicit CompanionStaffAssignerBrick(const JsonValue& cfg = JsonValue::object()) { configure(cfg); }

    std::string type_id() const override { return "CompanionStaffAssigner"; }
    std::string category() const override { return "Capacity"; }
    int order() const override { return 69; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    std::map<std::string, std::string> resource_to_staff_kind_;
    std::map<std::string, std::string> tag_to_staff_skill_;
    bool charge_staff_rate_ = true;
    JsonValue raw_cfg_;
};

// ============================================================================
// 26. LotteryAndFairQueueBrick (Stage 67): Anti-Bot Public Fair Lottery Pool
// ============================================================================
class LotteryAndFairQueueBrick : public IBrick {
public:
    explicit LotteryAndFairQueueBrick(const JsonValue& cfg = JsonValue::object()) { configure(cfg); }

    std::string type_id() const override { return "LotteryAndFairQueue"; }
    std::string category() const override { return "Workflow"; }
    int order() const override { return 67; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    int min_lead_hours_ = 24;
    bool only_weekends_ = false;
    std::set<std::string> trigger_tags_;
    int max_applicants_per_slot_ = 30;
    std::map<std::string, int> role_weight_bonus_;
    JsonValue raw_cfg_;
};

// ============================================================================
// 27. HierarchicalTreeLockBrick (Stage 72): Parent-Child Divisible Venue Topology Lock
// ============================================================================
class HierarchicalTreeLockBrick : public IBrick {
public:
    explicit HierarchicalTreeLockBrick(const JsonValue& cfg = JsonValue::object()) { configure(cfg); }

    std::string type_id() const override { return "HierarchicalTreeLock"; }
    std::string category() const override { return "Capacity"; }
    int order() const override { return 68; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

    const std::string& mode() const { return mode_; }

    // Reusable static topology conflict detector (used by Engine waitlist & lottery draw too!)
    static bool check_subdivision_conflict(
        const std::string& resource_id,
        EpochMinute eff_start,
        EpochMinute eff_end,
        const std::vector<Resource>& all_resources,
        const std::vector<BookingRecord>& existing_bookings,
        const std::string& exclude_booking_id,
        std::string* out_conflict_res_name = nullptr);

private:
    std::string mode_ = "exclusive_subdivision"; // exclusive_subdivision | parent_capacity_envelope
    bool route_to_waitlist_ = true;
    JsonValue raw_cfg_;
};

// ============================================================================
// 28. UnlessBrick: Higher-Order Inverted Guard Combinator (`Brick.Unless`)
// ============================================================================
class UnlessBrick : public IBrick {
public:
    UnlessBrick(std::string name, int ord, JsonValue cond_cfg, BrickPtr inner_brick);

    std::string type_id() const override { return "Unless"; }
    std::string instance_name() const override { return "Unless:" + name_; }
    std::string category() const override { return "Combinator"; }
    int order() const override { return order_; }

    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    std::string name_;
    int order_ = 45;
    std::shared_ptr<WhenBrick> delegate_;
    BrickPtr inner_;
    JsonValue cond_cfg_;
};

class BrickRegistry;
void register_v4_bricks(BrickRegistry& reg);

} // namespace chronokit
