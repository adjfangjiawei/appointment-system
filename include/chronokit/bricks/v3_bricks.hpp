#pragma once

#include "chronokit/bricks/brick.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace chronokit {

// 13. AntiScalperBrick: Anti-scalping burst creation limiter, daily cancel circuit-breaker & real-name check
class AntiScalperBrick : public IBrick {
public:
    AntiScalperBrick() { configure(JsonValue::object()); }
    explicit AntiScalperBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "AntiScalper"; }
    std::string category() const override { return "Access"; }
    int order() const override { return 28; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int max_cancellations_per_day_ = 3;
    int max_creates_in_window_ = 5;
    int window_minutes_ = 60;
    std::string require_verified_attr_;
    std::set<std::string> exempt_roles_{"admin"};
};

// 14. DependencyChainBrick: Enforces prerequisite booking workflows (e.g. Lab Safety Training -> Laser Cutter; Triage -> MRI)
class DependencyChainBrick : public IBrick {
public:
    struct PrereqRule {
        std::string required_kind;
        bool require_completed = false;
        int min_gap_minutes = 0;
        int within_days = 30;
    };

    DependencyChainBrick() { configure(JsonValue::object()); }
    explicit DependencyChainBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "DependencyChain"; }
    std::string category() const override { return "Workflow"; }
    int order() const override { return 32; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    std::map<std::string, PrereqRule> rules_;
};

// 15. CooldownIntervalBrick: Minimum cooldown gap between consecutive bookings to prevent continuous seat hogging
class CooldownIntervalBrick : public IBrick {
public:
    CooldownIntervalBrick() { configure(JsonValue::object()); }
    explicit CooldownIntervalBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "CooldownInterval"; }
    std::string category() const override { return "Quota"; }
    int order() const override { return 42; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    int min_gap_same_resource_ = 60;
    int min_gap_same_kind_ = 0;
    std::set<std::string> exempt_roles_{"admin"};
};

// 16. DynamicSurgePricingBrick: Real-time supply/demand occupancy-driven surge & off-peak subsidy pricing
class DynamicSurgePricingBrick : public IBrick {
public:
    DynamicSurgePricingBrick() { configure(JsonValue::object()); }
    explicit DynamicSurgePricingBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "DynamicSurgePricing"; }
    std::string category() const override { return "Pricing"; }
    int order() const override { return 54; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    double low_occ_threshold_ = 0.20;
    double low_occ_multiplier_ = 0.75;
    double high_occ_threshold_ = 0.60;
    double high_occ_multiplier_ = 1.25;
    double crit_occ_threshold_ = 0.85;
    double crit_occ_multiplier_ = 1.50;
};

// 17. CouponAndVoucherBrick: Configurable promo codes, cash-off vouchers, discount coupons & free-hour passes
class CouponAndVoucherBrick : public IBrick {
public:
    struct CouponRule {
        std::string code;
        std::string type = "amount_off"; // amount_off | discount_rate | free_minutes
        double value = 0.0;
        double min_spend = 0.0;
        std::string required_role;
        bool single_use_per_actor = true;
        std::string label;
    };

    CouponAndVoucherBrick() { configure(JsonValue::object()); }
    explicit CouponAndVoucherBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "CouponAndVoucher"; }
    std::string category() const override { return "Pricing"; }
    int order() const override { return 58; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    std::map<std::string, CouponRule> coupons_;
};

// 18. ResourceLoadBalancerBrick: Automatic least-utilized / idle sibling routing & hardware wear-leveling
class ResourceLoadBalancerBrick : public IBrick {
public:
    ResourceLoadBalancerBrick() { configure(JsonValue::object()); }
    explicit ResourceLoadBalancerBrick(const JsonValue& cfg) { configure(cfg); }

    std::string type_id() const override { return "ResourceLoadBalancer"; }
    std::string category() const override { return "Capacity"; }
    int order() const override { return 65; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    JsonValue raw_cfg_;
    std::string mode_ = "always_balance"; // always_balance | on_conflict_or_auto
    bool match_tags_ = true;
    bool exclusive_ = false;
};

// ============================================================================
// Higher-Order Functional Combinator Bricks (高阶组合子积木)
// ============================================================================

// 19. WhenBrick: Conditional branching combinator (`if condition -> then_brick else -> else_brick`)
class WhenBrick : public IBrick {
public:
    using CustomPredicate = std::function<bool(const BookingContext&)>;

    WhenBrick(
        std::string name,
        int ord,
        JsonValue cond_cfg,
        BrickPtr then_brick,
        BrickPtr else_brick = nullptr,
        CustomPredicate pred = nullptr);

    std::string type_id() const override { return "When"; }
    std::string instance_name() const override { return "When:" + name_; }
    std::string category() const override { return "Combinator"; }
    int order() const override { return order_; }
    void configure(const JsonValue& cfg) override;
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    std::string name_;
    int order_ = 55;
    JsonValue cond_cfg_;
    BrickPtr then_brick_;
    BrickPtr else_brick_;
    CustomPredicate custom_pred_;

    std::set<int> cond_weekdays_;
    std::set<std::string> cond_roles_;
    std::set<std::string> cond_tags_;
    int cond_min_units_ = 0;
    int cond_min_duration_ = 0;
    int cond_start_from_ = -1;
    int cond_start_to_ = -1;

    bool eval_condition(const BookingContext& ctx) const;
};

// 20. AnyOfBrick: Logical OR combinator — passes on the first child brick that succeeds!
class AnyOfBrick : public IBrick {
public:
    AnyOfBrick(std::string name, int ord, std::vector<BrickPtr> children);

    std::string type_id() const override { return "AnyOf"; }
    std::string instance_name() const override { return "AnyOf:" + name_; }
    std::string category() const override { return "Combinator"; }
    int order() const override { return order_; }
    void configure(const JsonValue&) override {}
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;

private:
    std::string name_;
    int order_ = 30;
    std::vector<BrickPtr> children_;
};

// 21. AllOfBrick: Named composite sub-pipeline / Prefab macro-brick
class AllOfBrick : public IBrick {
public:
    AllOfBrick(std::string name, int ord, std::vector<BrickPtr> children);

    std::string type_id() const override { return "AllOf"; }
    std::string instance_name() const override { return "Prefab:" + name_; }
    std::string category() const override { return "Combinator"; }
    int order() const override { return order_; }
    void configure(const JsonValue&) override {}
    BrickDescriptor describe() const override;
    BrickVerdict on_pre_book(BookingContext& ctx) override;
    BrickVerdict on_pre_cancel(CancelContext& ctx) override;
    BrickVerdict on_pre_check_in(CheckInContext& ctx) override;

    std::optional<int> no_show_grace_minutes() const override;
    int no_show_credit_deduction() const override;
    int complete_credit_reward() const override;
    bool auto_promote_waitlist() const override;
    bool is_exclusive_mode() const override;

private:
    std::string name_;
    int order_ = 50;
    std::vector<BrickPtr> children_;
};

class BrickRegistry;
void register_v3_bricks(BrickRegistry& reg);

} // namespace chronokit
