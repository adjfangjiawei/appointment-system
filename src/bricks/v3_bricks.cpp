#include "chronokit/bricks/v3_bricks.hpp"
#include "chronokit/bricks/builtin_bricks.hpp"
#include "chronokit/core/timeline_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace chronokit {

namespace {
std::string fmt_money2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

struct WorkingStateSnapshot {
    std::vector<Resource> resources;
    EpochMinute effective_start;
    EpochMinute effective_end;
    int pre_buffer_minutes;
    int post_buffer_minutes;
    BookingState target_state;
    int priority;
    double price;
    double discount_amount;
    std::string coupon_code;
    double deposit;
    bool waitlist_enabled;
    int max_waitlist_per_slot;
    MetadataMap working_metadata;
    std::vector<std::string> step_notes;

    static WorkingStateSnapshot capture(const BookingContext& ctx) {
        return {
            ctx.resources, ctx.effective_start, ctx.effective_end,
            ctx.pre_buffer_minutes, ctx.post_buffer_minutes,
            ctx.target_state, ctx.priority, ctx.price, ctx.discount_amount,
            ctx.coupon_code, ctx.deposit, ctx.waitlist_enabled,
            ctx.max_waitlist_per_slot, ctx.working_metadata, ctx.step_notes
        };
    }

    void restore(BookingContext& ctx) const {
        ctx.resources = resources;
        ctx.effective_start = effective_start;
        ctx.effective_end = effective_end;
        ctx.pre_buffer_minutes = pre_buffer_minutes;
        ctx.post_buffer_minutes = post_buffer_minutes;
        ctx.target_state = target_state;
        ctx.priority = priority;
        ctx.price = price;
        ctx.discount_amount = discount_amount;
        ctx.coupon_code = coupon_code;
        ctx.deposit = deposit;
        ctx.waitlist_enabled = waitlist_enabled;
        ctx.max_waitlist_per_slot = max_waitlist_per_slot;
        ctx.working_metadata = working_metadata;
        ctx.step_notes = step_notes;
    }
};
} // namespace

// ============================================================================
// 13. AntiScalperBrick
// ============================================================================
void AntiScalperBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("max_cancellations_per_day")) max_cancellations_per_day_ = static_cast<int>(cfg["max_cancellations_per_day"].as_int(3));
    if (cfg.contains("max_creates_in_window")) max_creates_in_window_ = static_cast<int>(cfg["max_creates_in_window"].as_int(5));
    if (cfg.contains("window_minutes")) window_minutes_ = static_cast<int>(cfg["window_minutes"].as_int(60));
    if (cfg.contains("require_verified_attribute")) require_verified_attr_ = cfg["require_verified_attribute"].as_string();
    if (cfg.contains("exempt_roles") && cfg["exempt_roles"].is_array()) {
        exempt_roles_.clear();
        for (const auto& r : cfg["exempt_roles"].as_array()) exempt_roles_.insert(r.as_string());
    }
}

BrickDescriptor AntiScalperBrick::describe() const {
    return {
        type_id(), category(),
        "防黄牛与高频抢退风控积木：实名核验、滑动时间窗高频建单限流与单日频繁退号熔断",
        order(),
        {
            {"max_cancellations_per_day", "int", 3, "单日累计主动退号达到多少次后熔断当日抢号资格"},
            {"max_creates_in_window", "int", 5, "滑动窗口内最大允许发起预约次数"},
            {"window_minutes", "int", 60, "滑动限流时间窗口大小(分钟)"},
            {"require_verified_attribute", "string", "", "必需为true的用户实名/认证属性名(如 real_name_verified)"}
        },
        raw_cfg_, {}
    };
}

BrickVerdict AntiScalperBrick::on_pre_book(BookingContext& ctx) {
    for (const auto& r : ctx.actor.roles) {
        if (exempt_roles_.count(r)) return BrickVerdict::pass("风控豁免角色");
    }

    if (!require_verified_attr_.empty()) {
        if (!meta_get_bool(ctx.actor.attributes, require_verified_attr_, false)) {
            return BrickVerdict::reject(
                "UNVERIFIED_IDENTITY",
                "未完成实名/资质核验 (" + require_verified_attr_ + ")，禁止参与抢号预约");
        }
    }

    EpochMinute today_start = time_utils::start_of_day(ctx.now);
    int cancel_today = 0;
    int recent_creates = 0;

    for (const auto& b : ctx.existing_bookings) {
        if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
        if (b.actor_id != ctx.actor.id) continue;

        if (b.status == BookingState::CANCELLED && time_utils::start_of_day(b.updated_at) == today_start) {
            ++cancel_today;
        }
        if (window_minutes_ > 0 && b.created_at >= ctx.now - window_minutes_ && b.created_at <= ctx.now) {
            ++recent_creates;
        }
    }

    if (max_cancellations_per_day_ > 0 && cancel_today >= max_cancellations_per_day_) {
        return BrickVerdict::reject(
            "SCALPER_CANCEL_CIRCUIT_OPEN",
            "防黄牛风控熔断：您今日已累计退号 " + std::to_string(cancel_today) +
            " 次(达上限 " + std::to_string(max_cancellations_per_day_) + " 次)，今日暂停新约");
    }

    if (max_creates_in_window_ > 0 && recent_creates >= max_creates_in_window_) {
        return BrickVerdict::reject(
            "BURST_RATE_LIMITED",
            "操作过于频繁：过去 " + std::to_string(window_minutes_) + " 分钟内已发起 " +
            std::to_string(recent_creates) + " 次预约，请稍后再试");
    }

    return BrickVerdict::pass("风控核验通过 (今日退单 " + std::to_string(cancel_today) + " 次)");
}

// ============================================================================
// 14. DependencyChainBrick
// ============================================================================
void DependencyChainBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    rules_.clear();
    if (cfg.contains("dependencies") && cfg["dependencies"].is_object()) {
        for (const auto& [target_kind, r_obj] : cfg["dependencies"].as_object()) {
            PrereqRule rule;
            rule.required_kind = r_obj["required_kind"].as_string();
            rule.require_completed = r_obj["require_completed"].as_bool(false);
            rule.min_gap_minutes = static_cast<int>(r_obj["min_gap_minutes"].as_int(0));
            rule.within_days = static_cast<int>(r_obj["within_days"].as_int(30));
            if (!rule.required_kind.empty()) rules_[target_kind] = rule;
        }
    }
}

BrickDescriptor DependencyChainBrick::describe() const {
    return {
        type_id(), category(),
        "前置流程依赖链积木：强制校验先导业务履约或串联顺序（如先初诊后MRI、先安全培训后高危设备、先核税后过户）",
        order(),
        {
            {"dependencies", "object", JsonValue::object(), "目标资源kind -> {required_kind, require_completed, min_gap_minutes, within_days}"}
        },
        raw_cfg_, {}
    };
}

BrickVerdict DependencyChainBrick::on_pre_book(BookingContext& ctx) {
    if (ctx.actor.has_role("admin")) return BrickVerdict::pass("管理员跳过前置依赖校验");

    const std::string& target_kind = ctx.primary_resource().kind;
    auto it = rules_.find(target_kind);
    if (it == rules_.end()) {
        return BrickVerdict::pass("无前置流程约束");
    }

    const PrereqRule& rule = it->second;
    // Map resource_id -> kind from all_resources
    std::map<std::string, std::string> id_to_kind;
    for (const auto& r : ctx.all_resources) id_to_kind[r.id] = r.kind;

    for (const auto& b : ctx.existing_bookings) {
        if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
        if (b.actor_id != ctx.actor.id) continue;

        bool valid_status = rule.require_completed
            ? (b.status == BookingState::COMPLETED)
            : (b.status == BookingState::COMPLETED || b.status == BookingState::CHECKED_IN || b.status == BookingState::CONFIRMED);
        if (!valid_status) continue;

        std::string b_kind = meta_get_string(b.metadata, "_primary_kind");
        if (b_kind.empty() && !b.primary_resource_id().empty()) {
            b_kind = id_to_kind[b.primary_resource_id()];
        }
        if (b_kind != rule.required_kind) continue;

        // Preceding booking must finish at least min_gap_minutes before this booking starts
        if (b.end_time + rule.min_gap_minutes > ctx.request.start_time) continue;
        if (rule.within_days > 0 && ctx.request.start_time - b.end_time > static_cast<int64_t>(rule.within_days) * 1440) continue;

        return BrickVerdict::pass("前置环节 [" + rule.required_kind + "] 已就绪 (关联单 " + b.id + ")");
    }

    return BrickVerdict::reject(
        "MISSING_PREREQUISITE_BOOKING",
        "流程前置条件未满足：预约 [" + target_kind + "] 前，必须先" +
        (rule.require_completed ? "完成" : "预约并早于当前时段结束") +
        " [" + rule.required_kind + "] 环节" +
        (rule.min_gap_minutes > 0 ? " (需间隔至少 " + std::to_string(rule.min_gap_minutes) + " 分钟)" : ""));
}

// ============================================================================
// 15. CooldownIntervalBrick
// ============================================================================
void CooldownIntervalBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("min_gap_same_resource_minutes")) min_gap_same_resource_ = static_cast<int>(cfg["min_gap_same_resource_minutes"].as_int(60));
    if (cfg.contains("min_gap_same_kind_minutes")) min_gap_same_kind_ = static_cast<int>(cfg["min_gap_same_kind_minutes"].as_int(0));
    if (cfg.contains("exempt_roles") && cfg["exempt_roles"].is_array()) {
        exempt_roles_.clear();
        for (const auto& r : cfg["exempt_roles"].as_array()) exempt_roles_.insert(r.as_string());
    }
}

BrickDescriptor CooldownIntervalBrick::describe() const {
    return {
        type_id(), category(),
        "防连占冷却间隔积木：强制相邻两次预约之间保留冷却间隔，防止拆分订单变相无限霸座/霸桩",
        order(),
        {
            {"min_gap_same_resource_minutes", "int", 60, "同一资源两次预约之间的最小冷却间隔(分钟)"},
            {"min_gap_same_kind_minutes", "int", 0, "同类别资源两次预约之间的最小冷却间隔(分钟)"},
            {"exempt_roles", "array", JsonValue::array({"admin"}), "免除冷却间隔的角色"}
        },
        raw_cfg_, {}
    };
}

BrickVerdict CooldownIntervalBrick::on_pre_book(BookingContext& ctx) {
    for (const auto& r : ctx.actor.roles) {
        if (exempt_roles_.count(r)) return BrickVerdict::pass("角色豁免冷却期");
    }

    std::map<std::string, std::string> id_to_kind;
    for (const auto& r : ctx.all_resources) id_to_kind[r.id] = r.kind;

    const std::string& req_res = ctx.primary_resource().id;
    const std::string& req_kind = ctx.primary_resource().kind;

    for (const auto& b : ctx.existing_bookings) {
        if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
        if (b.actor_id != ctx.actor.id || !occupies_capacity(b.status)) continue;

        int64_t gap = 0;
        if (ctx.request.start_time >= b.end_time) gap = ctx.request.start_time - b.end_time;
        else if (b.start_time >= ctx.request.end_time) gap = b.start_time - ctx.request.end_time;
        else continue; // Direct overlap handled by ConflictAndCapacityBrick

        if (min_gap_same_resource_ > 0 && b.primary_resource_id() == req_res && gap < min_gap_same_resource_) {
            return BrickVerdict::reject(
                "COOLDOWN_NOT_MET",
                "防连续霸占限制：同一资源两次预约间需至少冷却间隔 " + std::to_string(min_gap_same_resource_) +
                " 分钟 (距订单 " + b.id + " 仅间隔 " + std::to_string(gap) + " 分钟)");
        }

        std::string b_kind = id_to_kind[b.primary_resource_id()];
        if (min_gap_same_kind_ > 0 && b_kind == req_kind && gap < min_gap_same_kind_) {
            return BrickVerdict::reject(
                "KIND_COOLDOWN_NOT_MET",
                "同类资源预约冷却期未满：需间隔至少 " + std::to_string(min_gap_same_kind_) + " 分钟");
        }
    }

    return BrickVerdict::pass("满足防连占冷却间隔");
}

// ============================================================================
// 16. DynamicSurgePricingBrick
// ============================================================================
void DynamicSurgePricingBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("low_occupancy_threshold")) low_occ_threshold_ = cfg["low_occupancy_threshold"].as_double(0.20);
    if (cfg.contains("low_occupancy_multiplier")) low_occ_multiplier_ = cfg["low_occupancy_multiplier"].as_double(0.75);
    if (cfg.contains("high_occupancy_threshold")) high_occ_threshold_ = cfg["high_occupancy_threshold"].as_double(0.60);
    if (cfg.contains("high_occupancy_multiplier")) high_occ_multiplier_ = cfg["high_occupancy_multiplier"].as_double(1.25);
    if (cfg.contains("critical_occupancy_threshold")) crit_occ_threshold_ = cfg["critical_occupancy_threshold"].as_double(0.85);
    if (cfg.contains("critical_occupancy_multiplier")) crit_occ_multiplier_ = cfg["critical_occupancy_multiplier"].as_double(1.50);
}

BrickDescriptor DynamicSurgePricingBrick::describe() const {
    return {
        type_id(), category(),
        "实时供需动态调价积木：自动测算同类资源池在目标时段的实时负载率，闲时谷电补贴、忙时需量溢价",
        order(),
        {
            {"low_occupancy_threshold", "double", 0.20, "闲时补贴负载率上限阈值"},
            {"low_occupancy_multiplier", "double", 0.75, "闲时低负载折扣系数"},
            {"high_occupancy_threshold", "double", 0.60, "高负载溢价触发阈值"},
            {"high_occupancy_multiplier", "double", 1.25, "高负载溢价系数"},
            {"critical_occupancy_threshold", "double", 0.85, "极限满载触发阈值"},
            {"critical_occupancy_multiplier", "double", 1.50, "极限满载溢价系数"}
        },
        raw_cfg_, {}
    };
}

BrickVerdict DynamicSurgePricingBrick::on_pre_book(BookingContext& ctx) {
    if (ctx.price <= 0.0) return BrickVerdict::pass("零基础费率无需动态调价");

    const std::string& kind = ctx.primary_resource().kind;
    int total_cap = 0;
    int used_cap = 0;

    for (const auto& r : ctx.all_resources) {
        if (r.status != ResourceStatus::ACTIVE || r.kind != kind) continue;
        total_cap += r.capacity;
        int u = TimelineIndex::peak_occupied_units(
            ctx.existing_bookings, r.id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
        used_cap += std::min(r.capacity, u);
    }

    double occ = total_cap > 0 ? static_cast<double>(used_cap) / static_cast<double>(total_cap) : 0.0;
    int occ_pct = static_cast<int>(std::round(occ * 100.0));

    double mult = 1.0;
    std::string label = "平峰标准价";
    if (occ >= crit_occ_threshold_) {
        mult = crit_occ_multiplier_;
        label = "极高负载需量调价";
    } else if (occ >= high_occ_threshold_) {
        mult = high_occ_multiplier_;
        label = "繁忙时段动态溢价";
    } else if (occ <= low_occ_threshold_) {
        mult = low_occ_multiplier_;
        label = "闲时低负载错峰补贴";
    }

    if (std::abs(mult - 1.0) < 1e-6) {
        return BrickVerdict::pass("同类池实时负载 " + std::to_string(occ_pct) + "% (" + label + ")");
    }

    ctx.price = std::round(ctx.price * mult * 100.0) / 100.0;
    return BrickVerdict::mutated(
        "同类池实时负载 " + std::to_string(occ_pct) + "% -> " + label +
        " (×" + fmt_money2(mult) + " => ¥" + fmt_money2(ctx.price) + ")");
}

// ============================================================================
// 17. CouponAndVoucherBrick
// ============================================================================
void CouponAndVoucherBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    coupons_.clear();
    if (cfg.contains("coupons") && cfg["coupons"].is_object()) {
        for (const auto& [code, c_obj] : cfg["coupons"].as_object()) {
            CouponRule rule;
            rule.code = code;
            rule.type = c_obj["type"].as_string("amount_off");
            rule.value = c_obj["value"].as_double(0.0);
            rule.min_spend = c_obj["min_spend"].as_double(0.0);
            rule.required_role = c_obj["required_role"].as_string();
            rule.single_use_per_actor = c_obj["single_use_per_actor"].as_bool(true);
            rule.label = c_obj["label"].as_string(code);
            coupons_[code] = rule;
        }
    }
}

BrickDescriptor CouponAndVoucherBrick::describe() const {
    return {
        type_id(), category(),
        "优惠券与次卡核销积木：支持满减现金券(amount_off)、折扣券(discount_rate)、免时长体验券(free_minutes)与单人防重核销",
        order(),
        {
            {"coupons", "object", JsonValue::object(), "券码字典: code -> {type, value, min_spend, required_role, single_use_per_actor, label}"}
        },
        raw_cfg_, {}
    };
}

BrickVerdict CouponAndVoucherBrick::on_pre_book(BookingContext& ctx) {
    std::string code = !ctx.request.coupon_code.empty()
        ? ctx.request.coupon_code
        : meta_get_string(ctx.working_metadata, "coupon_code");
    if (code.empty()) return BrickVerdict::pass("未使用优惠券");

    auto it = coupons_.find(code);
    if (it == coupons_.end()) {
        return BrickVerdict::reject("INVALID_COUPON_CODE", "无效或已下架的优惠券码: " + code);
    }

    const CouponRule& rule = it->second;
    if (!rule.required_role.empty() && !ctx.actor.has_role(rule.required_role)) {
        return BrickVerdict::reject(
            "COUPON_ROLE_MISMATCH",
            "优惠券 [" + rule.label + "] 仅限 '" + rule.required_role + "' 身份使用");
    }

    if (ctx.price < rule.min_spend) {
        return BrickVerdict::reject(
            "COUPON_MIN_SPEND_NOT_MET",
            "优惠券 [" + rule.label + "] 需订单满 ¥" + fmt_money2(rule.min_spend) + " 可用 (当前 ¥" + fmt_money2(ctx.price) + ")");
    }

    if (rule.single_use_per_actor) {
        for (const auto& b : ctx.existing_bookings) {
            if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
            if (b.actor_id == ctx.actor.id && b.coupon_code == code &&
                b.status != BookingState::CANCELLED && b.status != BookingState::REJECTED)
            {
                return BrickVerdict::reject("COUPON_ALREADY_USED", "您已核销过单次优惠券 [" + code + "]，不可重复使用");
            }
        }
    }

    double orig_price = ctx.price;
    double new_price = orig_price;
    if (rule.type == "amount_off") {
        new_price = std::max(0.0, orig_price - rule.value);
    } else if (rule.type == "discount_rate") {
        new_price = orig_price * rule.value;
    } else if (rule.type == "free_minutes" && ctx.duration_minutes() > 0) {
        double ratio = std::min(1.0, rule.value / static_cast<double>(ctx.duration_minutes()));
        new_price = orig_price * (1.0 - ratio);
    }

    new_price = std::round(new_price * 100.0) / 100.0;
    ctx.discount_amount = std::round((orig_price - new_price) * 100.0) / 100.0;
    ctx.price = new_price;
    ctx.coupon_code = code;

    return BrickVerdict::mutated(
        "已核销 [" + rule.label + "]，立减 ¥" + fmt_money2(ctx.discount_amount) + " => 实付 ¥" + fmt_money2(ctx.price));
}

// ============================================================================
// 18. ResourceLoadBalancerBrick
// ============================================================================
void ResourceLoadBalancerBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("mode")) mode_ = cfg["mode"].as_string("always_balance");
    if (cfg.contains("match_tags")) match_tags_ = cfg["match_tags"].as_bool(true);
    if (cfg.contains("exclusive")) exclusive_ = cfg["exclusive"].as_bool(false);
}

BrickDescriptor ResourceLoadBalancerBrick::describe() const {
    return {
        type_id(), category(),
        "智能派单与磨损均衡路由积木：自动在同规格空闲资源池中挑选当日累计负载最低的节点（如超充桩派桩、窗口自动分流）",
        order(),
        {
            {"mode", "string", "always_balance", "派单策略: always_balance (默认全局均衡) | on_conflict_or_auto (仅原选繁忙或指定自动分配时介入)"},
            {"match_tags", "bool", true, "自动路由候选节点是否必须满足原选资源的全部标签"},
            {"exclusive", "bool", false, "是否按整间独占模式测算空闲"}
        },
        raw_cfg_, {}
    };
}

BrickVerdict ResourceLoadBalancerBrick::on_pre_book(BookingContext& ctx) {
    if (meta_get_bool(ctx.working_metadata, "pin_resource", false)) {
        return BrickVerdict::pass("用户指定锁定资源 (pin_resource=true)，跳过均衡派单");
    }

    const Resource orig = ctx.primary_resource();
    auto is_free = [&](const Resource& cand) -> bool {
        if (cand.status != ResourceStatus::ACTIVE || cand.capacity < ctx.request.units) return false;
        bool excl = cand.attributes.count("exclusive")
            ? meta_get_bool(cand.attributes, "exclusive", exclusive_)
            : exclusive_;
        int used = TimelineIndex::peak_occupied_units(
            ctx.existing_bookings, cand.id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
        return excl ? (used == 0) : (used + ctx.request.units <= cand.capacity);
    };

    bool auto_requested = meta_get_bool(ctx.working_metadata, "auto_assign", false);
    if (mode_ == "on_conflict_or_auto" && !auto_requested && is_free(orig)) {
        return BrickVerdict::pass("首选资源 [" + orig.id + "] 当前空闲，保持原选");
    }

    EpochMinute day_start = time_utils::start_of_day(ctx.request.start_time);
    const Resource* best = nullptr;
    int64_t best_daily_mins = std::numeric_limits<int64_t>::max();

    for (const auto& cand : ctx.all_resources) {
        if (cand.kind != orig.kind) continue;
        if (match_tags_) {
            bool ok = true;
            for (const auto& t : orig.tags) {
                if (!cand.has_tag(t)) { ok = false; break; }
            }
            if (!ok) continue;
        }
        // Make sure cand isn't already one of the secondary bundled resources
        bool in_secondary = false;
        for (size_t i = 1; i < ctx.resources.size(); ++i) {
            if (ctx.resources[i].id == cand.id) { in_secondary = true; break; }
        }
        if (in_secondary || !is_free(cand)) continue;

        int64_t daily_mins = 0;
        for (const auto& b : ctx.existing_bookings) {
            if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
            if (!occupies_capacity(b.status)) continue;
            if (b.primary_resource_id() == cand.id && time_utils::start_of_day(b.start_time) == day_start) {
                daily_mins += (b.end_time - b.start_time);
            }
        }

        if (daily_mins < best_daily_mins || (daily_mins == best_daily_mins && cand.id == orig.id)) {
            best_daily_mins = daily_mins;
            best = &cand;
        }
    }

    if (!best) {
        return BrickVerdict::pass("池中暂无空闲可替代节点，交由后续容量/候补积木处理");
    }

    if (best->id != orig.id) {
        ctx.resources[0] = *best;
        return BrickVerdict::mutated(
            "磨损均衡智能派单：已自动由 [" + orig.id + "] 路由至低负载空闲节点 [" + best->id +
            "] (今日累计负载 " + std::to_string(best_daily_mins) + "m)");
    }

    return BrickVerdict::pass("当前节点 [" + orig.id + "] 即为最低负载最优节点 (" + std::to_string(best_daily_mins) + "m)");
}

// ============================================================================
// 19. WhenBrick (Higher-Order Conditional Combinator)
// ============================================================================
WhenBrick::WhenBrick(
    std::string name,
    int ord,
    JsonValue cond_cfg,
    BrickPtr then_brick,
    BrickPtr else_brick,
    CustomPredicate pred)
    : name_(std::move(name))
    , order_(ord)
    , then_brick_(std::move(then_brick))
    , else_brick_(std::move(else_brick))
    , custom_pred_(std::move(pred))
{
    configure(cond_cfg);
}

void WhenBrick::configure(const JsonValue& cfg) {
    cond_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    cond_weekdays_.clear();
    cond_roles_.clear();
    cond_tags_.clear();
    if (cfg.contains("weekdays") && cfg["weekdays"].is_array()) {
        for (const auto& w : cfg["weekdays"].as_array()) cond_weekdays_.insert(static_cast<int>(w.as_int()));
    }
    if (cfg.contains("actor_roles") && cfg["actor_roles"].is_array()) {
        for (const auto& r : cfg["actor_roles"].as_array()) cond_roles_.insert(r.as_string());
    }
    if (cfg.contains("resource_tags") && cfg["resource_tags"].is_array()) {
        for (const auto& t : cfg["resource_tags"].as_array()) cond_tags_.insert(t.as_string());
    }
    cond_min_units_ = static_cast<int>(cfg["min_units"].as_int(0));
    cond_min_duration_ = static_cast<int>(cfg["min_duration_minutes"].as_int(0));
    if (cfg.contains("start_from")) cond_start_from_ = time_utils::parse_hhmm(cfg["start_from"].as_string()).value_or(-1);
    if (cfg.contains("start_to")) cond_start_to_ = time_utils::parse_hhmm(cfg["start_to"].as_string()).value_or(-1);
}

bool WhenBrick::eval_condition(const BookingContext& ctx) const {
    if (custom_pred_) return custom_pred_(ctx);

    if (!cond_weekdays_.empty() && !cond_weekdays_.count(time_utils::weekday_iso(ctx.request.start_time))) return false;
    if (!cond_roles_.empty()) {
        bool any = false;
        for (const auto& r : cond_roles_) if (ctx.actor.has_role(r)) { any = true; break; }
        if (!any) return false;
    }
    if (!cond_tags_.empty()) {
        bool any = false;
        for (const auto& t : cond_tags_) if (ctx.primary_resource().has_tag(t)) { any = true; break; }
        if (!any) return false;
    }
    if (cond_min_units_ > 0 && ctx.request.units < cond_min_units_) return false;
    if (cond_min_duration_ > 0 && ctx.duration_minutes() < cond_min_duration_) return false;

    int s_mod = time_utils::minute_of_day(ctx.request.start_time);
    if (cond_start_from_ >= 0 && s_mod < cond_start_from_) return false;
    if (cond_start_to_ >= 0 && s_mod >= cond_start_to_) return false;
    return true;
}

BrickDescriptor WhenBrick::describe() const {
    std::vector<BrickDescriptor> kids;
    if (then_brick_) kids.push_back(then_brick_->describe());
    if (else_brick_) kids.push_back(else_brick_->describe());
    return {
        instance_name(), category(),
        "高阶条件分支组合子：根据星期/角色/标签/时段动态路由执行 then_brick 或 else_brick 子积木",
        order_,
        {{"condition", "object", cond_cfg_, "分支触发条件"}},
        cond_cfg_,
        std::move(kids)
    };
}

BrickVerdict WhenBrick::on_pre_book(BookingContext& ctx) {
    bool matched = eval_condition(ctx);
    BrickPtr branch = matched ? then_brick_ : else_brick_;
    if (!branch) {
        return BrickVerdict::pass(matched ? "条件命中 (无 then 子积木)" : "条件未命中，跳过分支");
    }
    BrickVerdict v = branch->on_pre_book(ctx);
    std::string prefix = std::string(matched ? "[命中Then->" : "[命中Else->") + branch->instance_name() + "] ";
    v.note = prefix + v.note;
    return v;
}

// ============================================================================
// 20. AnyOfBrick (Higher-Order Logical OR Combinator)
// ============================================================================
AnyOfBrick::AnyOfBrick(std::string name, int ord, std::vector<BrickPtr> children)
    : name_(std::move(name)), order_(ord), children_(std::move(children)) {}

BrickDescriptor AnyOfBrick::describe() const {
    std::vector<BrickDescriptor> kids;
    for (const auto& c : children_) if (c) kids.push_back(c->describe());
    return {
        instance_name(), category(),
        "高阶逻辑『或(OR)』门组合子：任意一块子积木通过即放行（如：满足VIP身份 或 信用分>=115 或 持特批码）",
        order_, {}, JsonValue::object({{"branches_count", children_.size()}}), std::move(kids)
    };
}

BrickVerdict AnyOfBrick::on_pre_book(BookingContext& ctx) {
    if (children_.empty()) return BrickVerdict::pass();

    WorkingStateSnapshot base_snap = WorkingStateSnapshot::capture(ctx);
    std::string fail_reasons;

    for (const auto& child : children_) {
        if (!child) continue;
        base_snap.restore(ctx);
        BrickVerdict v = child->on_pre_book(ctx);
        if (v.ok) {
            v.note = "[满足其一: " + child->instance_name() + "] " + v.note;
            return v;
        }
        if (!fail_reasons.empty()) fail_reasons += "；或 ";
        fail_reasons += child->instance_name() + "(" + v.note + ")";
    }

    base_snap.restore(ctx);
    return BrickVerdict::reject("ANY_OF_ALL_FAILED", "未满足任一准入条件：" + fail_reasons);
}

// ============================================================================
// 21. AllOfBrick (Higher-Order Composite Prefab Sub-Pipeline)
// ============================================================================
AllOfBrick::AllOfBrick(std::string name, int ord, std::vector<BrickPtr> children)
    : name_(std::move(name)), order_(ord), children_(std::move(children)) {}

BrickDescriptor AllOfBrick::describe() const {
    std::vector<BrickDescriptor> kids;
    for (const auto& c : children_) if (c) kids.push_back(c->describe());
    return {
        instance_name(), category(),
        "高阶串联预制件模组(Prefab)：将多块子积木封装为可整体复用的复合能力包 (" + std::to_string(children_.size()) + " 块子积木)",
        order_, {}, JsonValue::object({{"children_count", children_.size()}}), std::move(kids)
    };
}

BrickVerdict AllOfBrick::on_pre_book(BookingContext& ctx) {
    bool any_mutated = false;
    std::string notes;
    for (const auto& c : children_) {
        if (!c) continue;
        BrickVerdict v = c->on_pre_book(ctx);
        if (!v.ok) {
            v.note = "[" + name_ + "::" + c->instance_name() + "] " + v.note;
            return v;
        }
        if (v.outcome != "PASS") any_mutated = true;
        if (!v.note.empty()) {
            if (!notes.empty()) notes += " | ";
            notes += c->instance_name() + ": " + v.note;
        }
    }
    return any_mutated ? BrickVerdict::mutated(notes) : BrickVerdict::pass(notes);
}

BrickVerdict AllOfBrick::on_pre_cancel(CancelContext& ctx) {
    for (const auto& c : children_) {
        if (!c) continue;
        BrickVerdict v = c->on_pre_cancel(ctx);
        if (!v.ok) return v;
    }
    return BrickVerdict::pass();
}

BrickVerdict AllOfBrick::on_pre_check_in(CheckInContext& ctx) {
    for (const auto& c : children_) {
        if (!c) continue;
        BrickVerdict v = c->on_pre_check_in(ctx);
        if (!v.ok) return v;
    }
    return BrickVerdict::pass();
}

std::optional<int> AllOfBrick::no_show_grace_minutes() const {
    for (const auto& c : children_) {
        if (auto g = c->no_show_grace_minutes()) return g;
    }
    return std::nullopt;
}

int AllOfBrick::no_show_credit_deduction() const {
    int sum = 0;
    for (const auto& c : children_) sum += c->no_show_credit_deduction();
    return sum;
}

int AllOfBrick::complete_credit_reward() const {
    int sum = 0;
    for (const auto& c : children_) sum += c->complete_credit_reward();
    return sum;
}

bool AllOfBrick::auto_promote_waitlist() const {
    for (const auto& c : children_) if (c->auto_promote_waitlist()) return true;
    return false;
}

bool AllOfBrick::is_exclusive_mode() const {
    for (const auto& c : children_) if (c->is_exclusive_mode()) return true;
    return false;
}

void register_v3_bricks(BrickRegistry& reg) {
    reg.register_brick("AntiScalper", [](const JsonValue& c) { return std::make_shared<AntiScalperBrick>(c); });
    reg.register_brick("DependencyChain", [](const JsonValue& c) { return std::make_shared<DependencyChainBrick>(c); });
    reg.register_brick("CooldownInterval", [](const JsonValue& c) { return std::make_shared<CooldownIntervalBrick>(c); });
    reg.register_brick("DynamicSurgePricing", [](const JsonValue& c) { return std::make_shared<DynamicSurgePricingBrick>(c); });
    reg.register_brick("CouponAndVoucher", [](const JsonValue& c) { return std::make_shared<CouponAndVoucherBrick>(c); });
    reg.register_brick("ResourceLoadBalancer", [](const JsonValue& c) { return std::make_shared<ResourceLoadBalancerBrick>(c); });
}

} // namespace chronokit
