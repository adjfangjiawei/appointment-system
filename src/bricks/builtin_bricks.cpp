#include "chronokit/bricks/builtin_bricks.hpp"
#include "chronokit/bricks/v3_bricks.hpp"
#include "chronokit/core/timeline_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace chronokit {

namespace {
std::string fmt_money(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}
} // namespace

// ============================================================================
// 1. OpeningHoursBrick
// ============================================================================
void OpeningHoursBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("open_time")) {
        open_min_ = time_utils::parse_hhmm(cfg["open_time"].as_string("08:00")).value_or(8 * 60);
    }
    if (cfg.contains("close_time")) {
        close_min_ = time_utils::parse_hhmm(cfg["close_time"].as_string("22:00")).value_or(22 * 60);
    }
    if (cfg.contains("weekdays") && cfg["weekdays"].is_array()) {
        weekdays_.clear();
        for (const auto& w : cfg["weekdays"].as_array()) {
            weekdays_.insert(static_cast<int>(w.as_int()));
        }
    }
    if (cfg.contains("blackout_dates") && cfg["blackout_dates"].is_array()) {
        blackout_dates_.clear();
        for (const auto& d : cfg["blackout_dates"].as_array()) {
            blackout_dates_.insert(d.as_string());
        }
    }
}

BrickDescriptor OpeningHoursBrick::describe() const {
    return {
        type_id(), category(),
        "控制全局或单资源的每日开放时段、开放星期与节假日闭馆日期",
        order(),
        {
            {"open_time", "string", "08:00", "每日开始营业时间 (HH:MM)"},
            {"close_time", "string", "22:00", "每日结束营业时间 (HH:MM)"},
            {"weekdays", "array", JsonValue::array({1,2,3,4,5,6,7}), "允许预约的星期 (1=周一..7=周日)"},
            {"blackout_dates", "array", JsonValue::array(), "不可预约闭馆日期列表 (YYYY-MM-DD)"}
        },
        raw_cfg_
    };
}

BrickVerdict OpeningHoursBrick::on_pre_book(BookingContext& ctx) {
    EpochMinute s = ctx.request.start_time;
    EpochMinute e = ctx.request.end_time;
    if (e <= s) {
        return BrickVerdict::reject("INVALID_TIME_RANGE", "结束时间必须晚于开始时间");
    }
    if (time_utils::start_of_day(s) != time_utils::start_of_day(e - 1)) {
        return BrickVerdict::reject("CROSS_DAY_FORBIDDEN", "预约时段不可跨自然日");
    }

    std::string date_str = time_utils::format_date(s);
    if (blackout_dates_.count(date_str)) {
        return BrickVerdict::reject("BLACKOUT_DATE", "所选日期 " + date_str + " 处于闭馆/停诊维护期");
    }

    int wd = time_utils::weekday_iso(s);
    if (!weekdays_.count(wd)) {
        return BrickVerdict::reject("CLOSED_WEEKDAY", "该星期(" + std::to_string(wd) + ")未开放预约");
    }

    int eff_open = open_min_;
    int eff_close = close_min_;
    const auto& res = ctx.primary_resource();
    std::string r_open = meta_get_string(res.attributes, "open_time");
    std::string r_close = meta_get_string(res.attributes, "close_time");
    if (!r_open.empty()) eff_open = time_utils::parse_hhmm(r_open).value_or(eff_open);
    if (!r_close.empty()) eff_close = time_utils::parse_hhmm(r_close).value_or(eff_close);

    int s_mod = time_utils::minute_of_day(s);
    int e_mod = (time_utils::start_of_day(e) > time_utils::start_of_day(s)) ? 1440 : time_utils::minute_of_day(e);

    if (s_mod < eff_open || e_mod > eff_close) {
        return BrickVerdict::reject(
            "OUT_OF_OPENING_HOURS",
            "所选时段超出开放时间 (" + time_utils::format_hhmm(eff_open) + "-" + time_utils::format_hhmm(eff_close) + ")");
    }

    return BrickVerdict::pass("符合开放时间 (" + time_utils::format_hhmm(eff_open) + "-" + time_utils::format_hhmm(eff_close) + ")");
}

// ============================================================================
// 2. SlotGranularityBrick
// ============================================================================
void SlotGranularityBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("align_minutes")) align_minutes_ = static_cast<int>(cfg["align_minutes"].as_int(30));
    if (cfg.contains("min_duration_minutes")) min_duration_ = static_cast<int>(cfg["min_duration_minutes"].as_int(30));
    if (cfg.contains("max_duration_minutes")) max_duration_ = static_cast<int>(cfg["max_duration_minutes"].as_int(240));
    if (cfg.contains("step_duration_minutes")) step_minutes_ = static_cast<int>(cfg["step_duration_minutes"].as_int(0));
    if (cfg.contains("allowed_fixed_slots") && cfg["allowed_fixed_slots"].is_array()) {
        fixed_slots_.clear();
        for (const auto& item : cfg["allowed_fixed_slots"].as_array()) {
            std::string slot_s = item.as_string();
            auto dash = slot_s.find('-');
            if (dash != std::string::npos) {
                auto a = time_utils::parse_hhmm(slot_s.substr(0, dash));
                auto b = time_utils::parse_hhmm(slot_s.substr(dash + 1));
                if (a && b) fixed_slots_.emplace_back(*a, *b);
            }
        }
    }
}

BrickDescriptor SlotGranularityBrick::describe() const {
    return {
        type_id(), category(),
        "约束时间刻度对齐粒度、最短/最长预约时长以及固定节次时段",
        order(),
        {
            {"align_minutes", "int", 30, "起止时间刻度对齐分钟数 (如 15/30/60)"},
            {"min_duration_minutes", "int", 30, "单次最短预约时长(分钟)"},
            {"max_duration_minutes", "int", 240, "单次最长预约时长(分钟)"},
            {"step_duration_minutes", "int", 0, "时长递增步长(0表示不额外限制)"},
            {"allowed_fixed_slots", "array", JsonValue::array(), "可选固定班次/节次列表 (如 '08:00-10:00')"}
        },
        raw_cfg_
    };
}

BrickVerdict SlotGranularityBrick::on_pre_book(BookingContext& ctx) {
    int dur = ctx.duration_minutes();
    if (dur < min_duration_ || dur > max_duration_) {
        return BrickVerdict::reject(
            "INVALID_DURATION",
            "预约时长(" + std::to_string(dur) + "分钟)不在允许范围[" +
            std::to_string(min_duration_) + ", " + std::to_string(max_duration_) + "]内");
    }

    int s_mod = time_utils::minute_of_day(ctx.request.start_time);
    int e_mod = s_mod + dur;

    if (!fixed_slots_.empty()) {
        bool matched = false;
        for (const auto& [fs, fe] : fixed_slots_) {
            if (s_mod == fs && e_mod == fe) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return BrickVerdict::reject("FIXED_SLOT_MISMATCH", "必须选择系统规定的标准固定时段节次");
        }
        return BrickVerdict::pass("匹配固定节次 (" + std::to_string(dur) + "m)");
    }

    if (align_minutes_ > 1) {
        if (s_mod % align_minutes_ != 0 || e_mod % align_minutes_ != 0) {
            return BrickVerdict::reject(
                "UNALIGNED_SLOT",
                "起止时间需对齐 " + std::to_string(align_minutes_) + " 分钟刻度");
        }
    }
    if (step_minutes_ > 1 && (dur - min_duration_) % step_minutes_ != 0) {
        return BrickVerdict::reject(
            "INVALID_DURATION_STEP",
            "时长需按 " + std::to_string(step_minutes_) + " 分钟步长递增");
    }

    return BrickVerdict::pass("粒度校验通过 (" + std::to_string(dur) + "分钟)");
}

// ============================================================================
// 3. BookingWindowBrick
// ============================================================================
void BookingWindowBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("min_advance_minutes")) min_advance_minutes_ = static_cast<int>(cfg["min_advance_minutes"].as_int(0));
    if (cfg.contains("max_advance_days")) max_advance_days_ = static_cast<int>(cfg["max_advance_days"].as_int(14));
    if (cfg.contains("role_max_advance_days") && cfg["role_max_advance_days"].is_object()) {
        role_max_advance_days_.clear();
        for (const auto& [r, v] : cfg["role_max_advance_days"].as_object()) {
            role_max_advance_days_[r] = static_cast<int>(v.as_int());
        }
    }
}

BrickDescriptor BookingWindowBrick::describe() const {
    return {
        type_id(), category(),
        "控制最早可提前预约天数与最晚截止提前量（支持VIP/角色差异化放号窗口）",
        order(),
        {
            {"min_advance_minutes", "int", 0, "距开始时间最少提前多少分钟预约"},
            {"max_advance_days", "int", 14, "默认最多可提前多少天预约"},
            {"role_max_advance_days", "object", JsonValue::object(), "不同角色的最大提前天数覆盖表"}
        },
        raw_cfg_
    };
}

BrickVerdict BookingWindowBrick::on_pre_book(BookingContext& ctx) {
    int64_t lead = ctx.request.start_time - ctx.now;
    if (lead < min_advance_minutes_) {
        return BrickVerdict::reject(
            "TOO_LATE_TO_BOOK",
            "需至少提前 " + std::to_string(min_advance_minutes_) + " 分钟发起预约");
    }

    int allowed_days = max_advance_days_;
    for (const auto& r : ctx.actor.roles) {
        auto it = role_max_advance_days_.find(r);
        if (it != role_max_advance_days_.end()) {
            allowed_days = std::max(allowed_days, it->second);
        }
    }

    if (allowed_days > 0 && lead > static_cast<int64_t>(allowed_days) * 1440) {
        return BrickVerdict::reject(
            "BEYOND_ADVANCE_HORIZON",
            "当前身份最多可提前 " + std::to_string(allowed_days) + " 天预约");
    }

    return BrickVerdict::pass("提前期合规 (上限 " + std::to_string(allowed_days) + " 天)");
}

// ============================================================================
// 4. BufferTimeBrick
// ============================================================================
void BufferTimeBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("pre_buffer_minutes")) pre_buffer_ = static_cast<int>(cfg["pre_buffer_minutes"].as_int(0));
    if (cfg.contains("post_buffer_minutes")) post_buffer_ = static_cast<int>(cfg["post_buffer_minutes"].as_int(15));
    if (cfg.contains("min_duration_to_apply")) min_duration_to_apply_ = static_cast<int>(cfg["min_duration_to_apply"].as_int(0));
}

BrickDescriptor BufferTimeBrick::describe() const {
    return {
        type_id(), category(),
        "自动在预约前后注入翻台/消杀/布场缓冲时间，无感隔离相邻预约冲突",
        order(),
        {
            {"pre_buffer_minutes", "int", 0, "前置准备缓冲分钟数"},
            {"post_buffer_minutes", "int", 15, "后置清洁/翻台缓冲分钟数"},
            {"min_duration_to_apply", "int", 0, "仅当时长达到该阈值(分钟)时追加缓冲"}
        },
        raw_cfg_
    };
}

BrickVerdict BufferTimeBrick::on_pre_book(BookingContext& ctx) {
    if (ctx.duration_minutes() < min_duration_to_apply_) {
        return BrickVerdict::pass("时长未达缓冲阈值，无需追加缓冲");
    }

    int pre = pre_buffer_;
    int post = post_buffer_;
    const auto& res = ctx.primary_resource();
    if (res.attributes.count("pre_buffer_minutes")) {
        pre = static_cast<int>(meta_get_int(res.attributes, "pre_buffer_minutes", pre));
    }
    if (res.attributes.count("post_buffer_minutes")) {
        post = static_cast<int>(meta_get_int(res.attributes, "post_buffer_minutes", post));
    }

    if (pre == 0 && post == 0) {
        return BrickVerdict::pass("零缓冲时间");
    }

    ctx.pre_buffer_minutes += pre;
    ctx.post_buffer_minutes += post;
    ctx.effective_start = ctx.request.start_time - ctx.pre_buffer_minutes;
    ctx.effective_end = ctx.request.end_time + ctx.post_buffer_minutes;

    return BrickVerdict::mutated(
        "已自动锁定缓冲时段 (前+" + std::to_string(pre) + "m, 后+" + std::to_string(post) +
        "m => 实际占用 " + time_utils::format_datetime(ctx.effective_start).substr(11) + "-" +
        time_utils::format_datetime(ctx.effective_end).substr(11) + ")");
}

// ============================================================================
// 5. RoleAndCreditGateBrick
// ============================================================================
void RoleAndCreditGateBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("min_credit_score")) min_credit_score_ = static_cast<int>(cfg["min_credit_score"].as_int(60));
    if (cfg.contains("deduct_on_no_show")) deduct_on_no_show_ = static_cast<int>(cfg["deduct_on_no_show"].as_int(10));
    if (cfg.contains("reward_on_complete")) reward_on_complete_ = static_cast<int>(cfg["reward_on_complete"].as_int(2));

    if (cfg.contains("blocked_roles") && cfg["blocked_roles"].is_array()) {
        blocked_roles_.clear();
        for (const auto& r : cfg["blocked_roles"].as_array()) blocked_roles_.insert(r.as_string());
    }
    if (cfg.contains("allowed_roles") && cfg["allowed_roles"].is_array()) {
        allowed_roles_.clear();
        for (const auto& r : cfg["allowed_roles"].as_array()) allowed_roles_.insert(r.as_string());
    }
    if (cfg.contains("tag_required_roles") && cfg["tag_required_roles"].is_object()) {
        tag_required_roles_.clear();
        for (const auto& [tag, arr] : cfg["tag_required_roles"].as_object()) {
            std::set<std::string> roles;
            for (const auto& item : arr.as_array()) roles.insert(item.as_string());
            tag_required_roles_[tag] = std::move(roles);
        }
    }
}

BrickDescriptor RoleAndCreditGateBrick::describe() const {
    return {
        type_id(), category(),
        "基于角色权限(RBAC)、资源标签准入门槛及守约信用分的准入控制积木",
        order(),
        {
            {"min_credit_score", "int", 60, "最低准入信用分"},
            {"deduct_on_no_show", "int", 10, "爽约自动扣减信用分"},
            {"reward_on_complete", "int", 2, "履约完成奖励信用分"},
            {"blocked_roles", "array", JsonValue::array(), "黑名单禁止角色列表"},
            {"allowed_roles", "array", JsonValue::array(), "白名单允许角色列表(空表示不限)"},
            {"tag_required_roles", "object", JsonValue::object(), "特定资源标签对应的准入角色集合"}
        },
        raw_cfg_
    };
}

BrickVerdict RoleAndCreditGateBrick::on_pre_book(BookingContext& ctx) {
    for (const auto& r : ctx.actor.roles) {
        if (blocked_roles_.count(r)) {
            return BrickVerdict::reject("ROLE_BLOCKED", "当前用户角色 '" + r + "' 已被限制预约权限");
        }
    }

    if (!allowed_roles_.empty()) {
        bool ok = false;
        for (const auto& r : ctx.actor.roles) {
            if (allowed_roles_.count(r)) { ok = true; break; }
        }
        if (!ok) return BrickVerdict::reject("ROLE_NOT_ALLOWED", "当前用户不具备系统允许的预约角色");
    }

    if (ctx.actor.credit_score < min_credit_score_ && !ctx.actor.has_role("admin")) {
        return BrickVerdict::reject(
            "LOW_CREDIT_SCORE",
            "信用分不足 (当前 " + std::to_string(ctx.actor.credit_score) + " < 准入线 " + std::to_string(min_credit_score_) + ")");
    }

    for (const auto& res : ctx.resources) {
        for (const auto& tag : res.tags) {
            auto it = tag_required_roles_.find(tag);
            if (it != tag_required_roles_.end() && !it->second.empty()) {
                bool matched = ctx.actor.has_role("admin");
                for (const auto& req_r : it->second) {
                    if (ctx.actor.has_role(req_r)) { matched = true; break; }
                }
                if (!matched) {
                    return BrickVerdict::reject(
                        "TAG_PERMISSION_DENIED",
                        "资源 [" + res.name + "] 含受限标签 #" + tag + "，当前角色无权预约");
                }
            }
        }
    }

    return BrickVerdict::pass("权限与信用校验通过 (信用分 " + std::to_string(ctx.actor.credit_score) + ")");
}

// ============================================================================
// 6. PartySizeMatcherBrick
// ============================================================================
void PartySizeMatcherBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("min_units")) min_units_ = static_cast<int>(cfg["min_units"].as_int(1));
    if (cfg.contains("max_units")) max_units_ = static_cast<int>(cfg["max_units"].as_int(100));
    if (cfg.contains("min_capacity_ratio")) min_capacity_ratio_ = cfg["min_capacity_ratio"].as_double(0.0);
    if (cfg.contains("enforce_ratio_peak_only")) enforce_ratio_peak_only_ = cfg["enforce_ratio_peak_only"].as_bool(false);
    if (cfg.contains("peak_start")) peak_start_min_ = time_utils::parse_hhmm(cfg["peak_start"].as_string("18:00")).value_or(18 * 60);
    if (cfg.contains("peak_end")) peak_end_min_ = time_utils::parse_hhmm(cfg["peak_end"].as_string("21:00")).value_or(21 * 60);
}

BrickDescriptor PartySizeMatcherBrick::describe() const {
    return {
        type_id(), category(),
        "校验预约人数上下限与大空间/大包厢容量利用率，防止少人占用大资源",
        order(),
        {
            {"min_units", "int", 1, "单笔最少预约人数/份数"},
            {"max_units", "int", 100, "单笔最多预约人数/份数"},
            {"min_capacity_ratio", "double", 0.0, "最低容量利用率要求 (如 0.5 代表至少坐满一半)"},
            {"enforce_ratio_peak_only", "bool", false, "是否仅在高峰时段强制检查容量利用率"}
        },
        raw_cfg_
    };
}

BrickVerdict PartySizeMatcherBrick::on_pre_book(BookingContext& ctx) {
    int u = ctx.request.units;
    if (u < min_units_ || u > max_units_) {
        return BrickVerdict::reject(
            "INVALID_PARTY_SIZE",
            "预约人数/单位(" + std::to_string(u) + ")超出范围[" + std::to_string(min_units_) + ", " + std::to_string(max_units_) + "]");
    }

    const auto& res = ctx.primary_resource();
    int r_min = static_cast<int>(meta_get_int(res.attributes, "min_party", 0));
    int r_max = static_cast<int>(meta_get_int(res.attributes, "max_party", 0));
    if (r_min > 0 && u < r_min) {
        return BrickVerdict::reject("BELOW_RESOURCE_MIN_PARTY", "资源 [" + res.name + "] 至少需 " + std::to_string(r_min) + " 人起订");
    }
    if (r_max > 0 && u > r_max) {
        return BrickVerdict::reject("ABOVE_RESOURCE_MAX_PARTY", "资源 [" + res.name + "] 最多容纳 " + std::to_string(r_max) + " 人");
    }

    if (min_capacity_ratio_ > 0.0 && res.capacity > 1) {
        bool check_ratio = true;
        if (enforce_ratio_peak_only_) {
            int s_mod = time_utils::minute_of_day(ctx.request.start_time);
            int e_mod = s_mod + ctx.duration_minutes();
            check_ratio = std::max(s_mod, peak_start_min_) < std::min(e_mod, peak_end_min_);
        }
        int effective_cap = r_max > 0 ? r_max : res.capacity;
        int min_req = static_cast<int>(std::ceil(effective_cap * min_capacity_ratio_));
        if (check_ratio && u < min_req && !ctx.actor.has_role("vip") && !ctx.actor.has_role("admin")) {
            return BrickVerdict::reject(
                "LOW_CAPACITY_UTILIZATION",
                "资源容量利用率不足：[" + res.name + "] 至少需 " + std::to_string(min_req) + " 人可使用 (当前 " + std::to_string(u) + " 人)");
        }
    }

    return BrickVerdict::pass("人数匹配通过 (" + std::to_string(u) + " 单位)");
}

// ============================================================================
// 7. QuotaLimiterBrick
// ============================================================================
void QuotaLimiterBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("max_active_per_actor")) max_active_per_actor_ = static_cast<int>(cfg["max_active_per_actor"].as_int(5));
    if (cfg.contains("max_daily_bookings_per_actor")) max_daily_bookings_ = static_cast<int>(cfg["max_daily_bookings_per_actor"].as_int(3));
    if (cfg.contains("max_daily_minutes_per_actor")) max_daily_minutes_ = static_cast<int>(cfg["max_daily_minutes_per_actor"].as_int(360));
    if (cfg.contains("max_weekly_minutes_per_actor")) max_weekly_minutes_ = static_cast<int>(cfg["max_weekly_minutes_per_actor"].as_int(1440));
    if (cfg.contains("unique_kind_per_day")) unique_kind_per_day_ = cfg["unique_kind_per_day"].as_bool(false);

    if (cfg.contains("exempt_roles") && cfg["exempt_roles"].is_array()) {
        exempt_roles_.clear();
        for (const auto& r : cfg["exempt_roles"].as_array()) exempt_roles_.insert(r.as_string());
    }
    if (cfg.contains("role_multiplier") && cfg["role_multiplier"].is_object()) {
        role_multiplier_.clear();
        for (const auto& [r, m] : cfg["role_multiplier"].as_object()) {
            role_multiplier_[r] = m.as_double(1.0);
        }
    }
}

BrickDescriptor QuotaLimiterBrick::describe() const {
    return {
        type_id(), category(),
        "多维配额防刷积木：限制同时生效单数、单日单数、单日/单周总时长及同类别互斥",
        order(),
        {
            {"max_active_per_actor", "int", 5, "单用户最大在途未结束预约数"},
            {"max_daily_bookings_per_actor", "int", 3, "单用户单日最大预约次数"},
            {"max_daily_minutes_per_actor", "int", 360, "单用户单日累计最大预约分钟数"},
            {"max_weekly_minutes_per_actor", "int", 1440, "单用户单周累计最大预约分钟数"},
            {"unique_kind_per_day", "bool", false, "同一天同类型(kind)资源是否仅限预约一次(如医院同科室限号)"},
            {"role_multiplier", "object", JsonValue::object(), "角色配额倍率加成表"}
        },
        raw_cfg_
    };
}

BrickVerdict QuotaLimiterBrick::on_pre_book(BookingContext& ctx) {
    for (const auto& r : ctx.actor.roles) {
        if (exempt_roles_.count(r)) {
            return BrickVerdict::pass("豁免角色 (" + r + ") 不受配额限制");
        }
    }

    double mult = 1.0;
    for (const auto& r : ctx.actor.roles) {
        auto it = role_multiplier_.find(r);
        if (it != role_multiplier_.end()) mult = std::max(mult, it->second);
    }

    int cap_active = max_active_per_actor_ > 0 ? static_cast<int>(std::round(max_active_per_actor_ * mult)) : 0;
    int cap_daily_cnt = max_daily_bookings_ > 0 ? static_cast<int>(std::round(max_daily_bookings_ * mult)) : 0;
    int cap_daily_min = max_daily_minutes_ > 0 ? static_cast<int>(std::round(max_daily_minutes_ * mult)) : 0;
    int cap_weekly_min = max_weekly_minutes_ > 0 ? static_cast<int>(std::round(max_weekly_minutes_ * mult)) : 0;

    EpochMinute day_start = time_utils::start_of_day(ctx.request.start_time);
    EpochMinute week_start = time_utils::start_of_iso_week(ctx.request.start_time);

    int active_cnt = 0;
    int daily_cnt = 0;
    int daily_min = 0;
    int weekly_min = 0;

    for (const auto& b : ctx.existing_bookings) {
        if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
        if (b.actor_id != ctx.actor.id) continue;
        bool active_or_waiting = occupies_capacity(b.status) || b.status == BookingState::WAITLISTED;
        if (!active_or_waiting) continue;

        if (b.end_time > ctx.now) ++active_cnt;

        if (time_utils::start_of_day(b.start_time) == day_start) {
            ++daily_cnt;
            daily_min += static_cast<int>(b.end_time - b.start_time);

            if (unique_kind_per_day_) {
                std::string booked_kind = meta_get_string(b.metadata, "_primary_kind");
                if (!booked_kind.empty() && booked_kind == ctx.primary_resource().kind) {
                    return BrickVerdict::reject(
                        "DUPLICATE_KIND_SAME_DAY",
                        "同一天内在 [" + booked_kind + "] 类别下仅限预约一次");
                }
            }
        }

        if (time_utils::start_of_iso_week(b.start_time) == week_start) {
            weekly_min += static_cast<int>(b.end_time - b.start_time);
        }
    }

    ctx.working_metadata["_primary_kind"] = ctx.primary_resource().kind;

    int req_dur = ctx.duration_minutes();
    if (cap_active > 0 && active_cnt >= cap_active) {
        return BrickVerdict::reject("QUOTA_ACTIVE_EXCEEDED", "在途有效预约数已达上限 (" + std::to_string(cap_active) + " 单)");
    }
    if (cap_daily_cnt > 0 && daily_cnt >= cap_daily_cnt) {
        return BrickVerdict::reject("QUOTA_DAILY_COUNT_EXCEEDED", "当日预约次数已达上限 (" + std::to_string(cap_daily_cnt) + " 次)");
    }
    if (cap_daily_min > 0 && daily_min + req_dur > cap_daily_min) {
        return BrickVerdict::reject(
            "QUOTA_DAILY_MINUTES_EXCEEDED",
            "当日累计预约时长将超限 (已用 " + std::to_string(daily_min) + "m + 本次 " + std::to_string(req_dur) + "m > 上限 " + std::to_string(cap_daily_min) + "m)");
    }
    if (cap_weekly_min > 0 && weekly_min + req_dur > cap_weekly_min) {
        return BrickVerdict::reject("QUOTA_WEEKLY_MINUTES_EXCEEDED", "本周累计预约时长已超配额上限");
    }

    return BrickVerdict::pass(
        "配额充裕 (今日已用 " + std::to_string(daily_cnt) + "/" +
        (cap_daily_cnt > 0 ? std::to_string(cap_daily_cnt) : "∞") + " 次)");
}

// ============================================================================
// 8. PricingAndDepositBrick
// ============================================================================
void PricingAndDepositBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("billing_mode")) billing_mode_ = cfg["billing_mode"].as_string("per_hour");
    if (cfg.contains("base_rate")) base_rate_ = cfg["base_rate"].as_double(0.0);
    if (cfg.contains("peak_start")) peak_start_min_ = time_utils::parse_hhmm(cfg["peak_start"].as_string("18:00")).value_or(18 * 60);
    if (cfg.contains("peak_end")) peak_end_min_ = time_utils::parse_hhmm(cfg["peak_end"].as_string("21:00")).value_or(21 * 60);
    if (cfg.contains("peak_multiplier")) peak_multiplier_ = cfg["peak_multiplier"].as_double(1.0);
    if (cfg.contains("weekend_multiplier")) weekend_multiplier_ = cfg["weekend_multiplier"].as_double(1.0);
    if (cfg.contains("early_bird_hours")) early_bird_hours_ = static_cast<int>(cfg["early_bird_hours"].as_int(0));
    if (cfg.contains("early_bird_discount")) early_bird_discount_ = cfg["early_bird_discount"].as_double(1.0);
    if (cfg.contains("deposit_ratio")) deposit_ratio_ = cfg["deposit_ratio"].as_double(0.0);
    if (cfg.contains("flat_deposit")) flat_deposit_ = cfg["flat_deposit"].as_double(0.0);

    if (cfg.contains("tag_surcharges") && cfg["tag_surcharges"].is_object()) {
        tag_surcharges_.clear();
        for (const auto& [k, v] : cfg["tag_surcharges"].as_object()) tag_surcharges_[k] = v.as_double(0.0);
    }
    if (cfg.contains("role_discounts") && cfg["role_discounts"].is_object()) {
        role_discounts_.clear();
        for (const auto& [k, v] : cfg["role_discounts"].as_object()) role_discounts_[k] = v.as_double(1.0);
    }
}

BrickDescriptor PricingAndDepositBrick::describe() const {
    return {
        type_id(), category(),
        "多模式计费引擎：支持按小时/按人次/一口价、高峰周末溢价、早鸟折扣、标签附加费与订金",
        order(),
        {
            {"billing_mode", "string", "per_hour", "计费模式: free | per_hour | per_unit_hour | per_unit_flat | flat"},
            {"base_rate", "double", 0.0, "基准费率(支持被资源属性 base_rate 覆盖)"},
            {"peak_multiplier", "double", 1.0, "高峰时段溢价系数"},
            {"weekend_multiplier", "double", 1.0, "周末溢价系数"},
            {"early_bird_hours", "int", 0, "早鸟优惠提前小时数阈值"},
            {"early_bird_discount", "double", 1.0, "早鸟优惠折扣率(如 0.85)"},
            {"tag_surcharges", "object", JsonValue::object(), "资源标签附加费表"},
            {"role_discounts", "object", JsonValue::object(), "角色专属折扣率表"},
            {"deposit_ratio", "double", 0.0, "订金占总价比例"},
            {"flat_deposit", "double", 0.0, "固定订金金额"}
        },
        raw_cfg_
    };
}

BrickVerdict PricingAndDepositBrick::on_pre_book(BookingContext& ctx) {
    if (billing_mode_ == "free") {
        ctx.price = 0.0;
        ctx.deposit = flat_deposit_;
        return BrickVerdict::pass("免费公益模式");
    }

    const auto& primary = ctx.primary_resource();
    double rate = meta_get_double(primary.attributes, "base_rate", base_rate_);

    double surcharge = 0.0;
    for (const auto& res : ctx.resources) {
        if (&res != &primary) {
            surcharge += meta_get_double(res.attributes, "base_rate", 0.0);
        }
        for (const auto& t : res.tags) {
            auto it = tag_surcharges_.find(t);
            if (it != tag_surcharges_.end()) surcharge += it->second;
        }
    }

    double hours = ctx.duration_minutes() / 60.0;
    double total = 0.0;
    if (billing_mode_ == "per_hour") {
        total = (rate + surcharge) * hours;
    } else if (billing_mode_ == "per_unit_hour") {
        total = (rate + surcharge) * hours * ctx.request.units;
    } else if (billing_mode_ == "per_unit_flat") {
        total = (rate + surcharge) * ctx.request.units;
    } else { // flat / per_slot
        total = rate + surcharge;
    }

    int s_mod = time_utils::minute_of_day(ctx.request.start_time);
    int e_mod = s_mod + ctx.duration_minutes();
    if (peak_multiplier_ != 1.0 && std::max(s_mod, peak_start_min_) < std::min(e_mod, peak_end_min_)) {
        total *= peak_multiplier_;
    }

    int wd = time_utils::weekday_iso(ctx.request.start_time);
    if (weekend_multiplier_ != 1.0 && (wd == 6 || wd == 7)) {
        total *= weekend_multiplier_;
    }

    if (early_bird_hours_ > 0 && early_bird_discount_ < 1.0) {
        if (ctx.request.start_time - ctx.now >= static_cast<int64_t>(early_bird_hours_) * 60) {
            total *= early_bird_discount_;
        }
    }

    double best_discount = 1.0;
    for (const auto& r : ctx.actor.roles) {
        auto it = role_discounts_.find(r);
        if (it != role_discounts_.end()) {
            best_discount = std::min(best_discount, it->second);
        }
    }
    total *= best_discount;
    total = std::round(total * 100.0) / 100.0;

    ctx.price = total;
    ctx.deposit = std::round((flat_deposit_ + total * deposit_ratio_) * 100.0) / 100.0;

    return BrickVerdict::mutated(
        "核定费用 ¥" + fmt_money(ctx.price) +
        (ctx.deposit > 0 ? " (含订金 ¥" + fmt_money(ctx.deposit) + ")" : ""));
}

// ============================================================================
// 9. WaitlistBrick
// ============================================================================
void WaitlistBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("enabled")) enabled_ = cfg["enabled"].as_bool(true);
    if (cfg.contains("max_waitlist_per_slot")) max_waitlist_per_slot_ = static_cast<int>(cfg["max_waitlist_per_slot"].as_int(5));
    if (cfg.contains("base_priority")) base_priority_ = static_cast<int>(cfg["base_priority"].as_int(10));
    if (cfg.contains("credit_priority_weight")) credit_priority_weight_ = cfg["credit_priority_weight"].as_double(0.0);
    if (cfg.contains("auto_promote_on_cancel")) auto_promote_on_cancel_ = cfg["auto_promote_on_cancel"].as_bool(true);
    if (cfg.contains("role_priority_bonus") && cfg["role_priority_bonus"].is_object()) {
        role_priority_bonus_.clear();
        for (const auto& [r, p] : cfg["role_priority_bonus"].as_object()) {
            role_priority_bonus_[r] = static_cast<int>(p.as_int());
        }
    }
}

BrickDescriptor WaitlistBrick::describe() const {
    return {
        type_id(), category(),
        "智能候补队列与优先级评估积木：支持满员入列及退单/爽约时按优先级自动递补转正",
        order(),
        {
            {"enabled", "bool", true, "是否启用候补队列机制"},
            {"max_waitlist_per_slot", "int", 5, "单时段最大候补排队人数"},
            {"base_priority", "int", 10, "基础优先级分值"},
            {"role_priority_bonus", "object", JsonValue::object(), "角色优先级加分表 (如急诊/老人/VIP)"},
            {"credit_priority_weight", "double", 0.0, "信用分折算优先级权重"},
            {"auto_promote_on_cancel", "bool", true, "席位释放时是否自动递补最高优先级候补单"}
        },
        raw_cfg_
    };
}

BrickVerdict WaitlistBrick::on_pre_book(BookingContext& ctx) {
    ctx.waitlist_enabled = enabled_ && ctx.request.allow_waitlist;
    ctx.max_waitlist_per_slot = max_waitlist_per_slot_;

    int prio = base_priority_ + ctx.priority;
    int best_bonus = 0;
    for (const auto& r : ctx.actor.roles) {
        auto it = role_priority_bonus_.find(r);
        if (it != role_priority_bonus_.end()) best_bonus = std::max(best_bonus, it->second);
    }
    prio += best_bonus;
    prio += static_cast<int>(std::round(ctx.actor.credit_score * credit_priority_weight_));
    ctx.priority = prio;

    return BrickVerdict::pass("优先级评分=" + std::to_string(ctx.priority) + (ctx.waitlist_enabled ? " (候补就绪)" : ""));
}

// ============================================================================
// 10. ConflictAndCapacityBrick
// ============================================================================
void ConflictAndCapacityBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("allow_actor_overlap")) allow_actor_overlap_ = cfg["allow_actor_overlap"].as_bool(false);
    if (cfg.contains("route_full_to_waitlist")) route_full_to_waitlist_ = cfg["route_full_to_waitlist"].as_bool(true);
    if (cfg.contains("exclusive")) exclusive_ = cfg["exclusive"].as_bool(false);
}

BrickDescriptor ConflictAndCapacityBrick::describe() const {
    return {
        type_id(), category(),
        "基于扫描线(Sweep-Line)的多资源原子容量账本与时间轴防冲突引擎(支持共享容量池与整间独占双模式)",
        order(),
        {
            {"allow_actor_overlap", "bool", false, "是否允许同一用户在同一时段并行拥有多笔预约"},
            {"route_full_to_waitlist", "bool", true, "容量满载时若开启候补则自动转入WAITLISTED"},
            {"exclusive", "bool", false, "整间独占模式(如会议室/餐桌：capacity代表最大容纳人数，单笔预订即整间排他锁定)"}
        },
        raw_cfg_
    };
}

BrickVerdict ConflictAndCapacityBrick::on_pre_book(BookingContext& ctx) {
    for (const auto& res : ctx.resources) {
        if (res.status != ResourceStatus::ACTIVE) {
            return BrickVerdict::reject(
                "RESOURCE_UNAVAILABLE",
                "资源 [" + res.name + "] 当前不可用 (状态: " + to_string(res.status) + ")");
        }
    }

    if (!allow_actor_overlap_) {
        if (TimelineIndex::has_actor_overlap(
                ctx.existing_bookings, ctx.actor.id, ctx.request.start_time, ctx.request.end_time, ctx.exclude_booking_id)) {
            return BrickVerdict::reject("ACTOR_TIME_CONFLICT", "您在该时间段内已有其他生效中的预约，不可重复占用");
        }
    }

    for (size_t i = 0; i < ctx.resources.size(); ++i) {
        const auto& res = ctx.resources[i];
        int req_units = (i == 0) ? ctx.request.units : 1;
        if (req_units > res.capacity) {
            return BrickVerdict::reject(
                "EXCEEDS_TOTAL_CAPACITY",
                "请求数量(" + std::to_string(req_units) + ")超过资源 [" + res.name + "] 最大物理容量(" + std::to_string(res.capacity) + ")");
        }

        bool is_excl = res.attributes.count("exclusive")
            ? meta_get_bool(res.attributes, "exclusive", exclusive_)
            : exclusive_;

        int peak_used = TimelineIndex::peak_occupied_units(
            ctx.existing_bookings, res.id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);

        bool capacity_exceeded = is_excl ? (peak_used > 0) : (peak_used + req_units > res.capacity);

        if (capacity_exceeded) {
            // Check if waitlist routing is available
            if (route_full_to_waitlist_ && ctx.waitlist_enabled && !ctx.dry_run) {
                int wl_cnt = TimelineIndex::waitlist_count(
                    ctx.existing_bookings, res.id, ctx.request.start_time, ctx.request.end_time);
                if (wl_cnt >= ctx.max_waitlist_per_slot) {
                    return BrickVerdict::reject(
                        "WAITLIST_FULL",
                        "资源 [" + res.name + "] 已约满且候补队列已满 (" + std::to_string(wl_cnt) + "/" + std::to_string(ctx.max_waitlist_per_slot) + ")");
                }
                ctx.target_state = BookingState::WAITLISTED;
                return BrickVerdict::mutated(
                    "资源 [" + res.name + "] 时段已被占用，已自动进入优先级候补队列 (当前排队 #" + std::to_string(wl_cnt + 1) + ")",
                    "WAITLIST");
            }
            return BrickVerdict::reject(
                "CAPACITY_CONFLICT",
                is_excl
                    ? ("独占空间 [" + res.name + "] 在所选时段(含缓冲期)已被占用")
                    : ("资源 [" + res.name + "] 在所选时段容量不足 (已占用 " + std::to_string(peak_used) + "/" + std::to_string(res.capacity) + ")"));
        }
    }

    const auto& prim = ctx.primary_resource();
    int used_prim = TimelineIndex::peak_occupied_units(
        ctx.existing_bookings, prim.id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
    return BrickVerdict::pass(
        "原子容量锁定就绪 (占用后 " + std::to_string(used_prim + ctx.request.units) + "/" + std::to_string(prim.capacity) +
        (ctx.resources.size() > 1 ? "，含绑定从属资源 " + std::to_string(ctx.resources.size() - 1) + " 项" : "") + ")");
}

// ============================================================================
// 11. ApprovalWorkflowBrick
// ============================================================================
void ApprovalWorkflowBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("require_approval_always")) require_always_ = cfg["require_approval_always"].as_bool(false);
    if (cfg.contains("duration_threshold_minutes")) duration_threshold_ = static_cast<int>(cfg["duration_threshold_minutes"].as_int(0));
    if (cfg.contains("capacity_threshold")) capacity_threshold_ = static_cast<int>(cfg["capacity_threshold"].as_int(0));
    if (cfg.contains("price_threshold")) price_threshold_ = cfg["price_threshold"].as_double(0.0);
    if (cfg.contains("trigger_tags") && cfg["trigger_tags"].is_array()) {
        trigger_tags_.clear();
        for (const auto& t : cfg["trigger_tags"].as_array()) trigger_tags_.insert(t.as_string());
    }
    if (cfg.contains("auto_approve_roles") && cfg["auto_approve_roles"].is_array()) {
        auto_approve_roles_.clear();
        for (const auto& r : cfg["auto_approve_roles"].as_array()) auto_approve_roles_.insert(r.as_string());
    }
}

BrickDescriptor ApprovalWorkflowBrick::describe() const {
    return {
        type_id(), category(),
        "条件工作流路由积木：当预约触发超长时长、大容量空间、高金额或敏感标签时自动转入待审批流",
        order(),
        {
            {"require_approval_always", "bool", false, "是否所有预约均需人工审批"},
            {"duration_threshold_minutes", "int", 0, "触发审批的时长阈值(分钟, 0不启用)"},
            {"capacity_threshold", "int", 0, "触发审批的资源容量阈值(0不启用)"},
            {"price_threshold", "double", 0.0, "触发审批的订单金额阈值(0不启用)"},
            {"trigger_tags", "array", JsonValue::array(), "触发审批的敏感资源标签列表"},
            {"auto_approve_roles", "array", JsonValue::array({"admin"}), "免审自动通过的白名单角色"}
        },
        raw_cfg_
    };
}

BrickVerdict ApprovalWorkflowBrick::on_pre_book(BookingContext& ctx) {
    if (ctx.target_state != BookingState::CONFIRMED) {
        return BrickVerdict::pass("当前目标状态非直通确认，跳过审批路由");
    }

    for (const auto& r : ctx.actor.roles) {
        if (auto_approve_roles_.count(r)) {
            return BrickVerdict::pass("角色 '" + r + "' 享有免审直通权限");
        }
    }

    std::string reason;
    if (require_always_) {
        reason = "全局强制审批策略";
    } else if (duration_threshold_ > 0 && ctx.duration_minutes() >= duration_threshold_) {
        reason = "预约时长(" + std::to_string(ctx.duration_minutes()) + "m)达到审批阈值(" + std::to_string(duration_threshold_) + "m)";
    } else if (capacity_threshold_ > 0 && ctx.primary_resource().capacity >= capacity_threshold_) {
        reason = "大型资源容量(" + std::to_string(ctx.primary_resource().capacity) + ")达到审批阈值";
    } else if (price_threshold_ > 0.0 && ctx.price >= price_threshold_) {
        reason = "订单金额(¥" + fmt_money(ctx.price) + ")达到大额审批线";
    } else {
        for (const auto& res : ctx.resources) {
            for (const auto& t : res.tags) {
                if (trigger_tags_.count(t)) {
                    reason = "包含受控审批标签 #" + t;
                    break;
                }
            }
            if (!reason.empty()) break;
        }
    }

    if (!reason.empty()) {
        ctx.target_state = BookingState::PENDING_APPROVAL;
        return BrickVerdict::mutated("已转入待审批状态 (" + reason + ")", "APPROVAL_REQUIRED");
    }

    return BrickVerdict::pass("满足免审直通条件");
}

// ============================================================================
// 12. CancellationAndCheckInBrick
// ============================================================================
void CancellationAndCheckInBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("free_cancel_before_minutes")) free_cancel_before_ = static_cast<int>(cfg["free_cancel_before_minutes"].as_int(120));
    if (cfg.contains("late_cancel_fee_ratio")) late_cancel_fee_ratio_ = cfg["late_cancel_fee_ratio"].as_double(0.3);
    if (cfg.contains("non_refundable_before_minutes")) non_refundable_before_ = static_cast<int>(cfg["non_refundable_before_minutes"].as_int(0));
    if (cfg.contains("forbid_cancel_before_minutes")) forbid_cancel_before_ = static_cast<int>(cfg["forbid_cancel_before_minutes"].as_int(0));
    if (cfg.contains("check_in_earliest_before_minutes")) check_in_earliest_before_ = static_cast<int>(cfg["check_in_earliest_before_minutes"].as_int(30));
    if (cfg.contains("check_in_grace_after_minutes")) check_in_grace_after_ = static_cast<int>(cfg["check_in_grace_after_minutes"].as_int(15));
}

BrickDescriptor CancellationAndCheckInBrick::describe() const {
    return {
        type_id(), category(),
        "退改签阶梯违约金核算、签到时间窗校验及超时未签到(No-Show)宽限期控制积木",
        order(),
        {
            {"free_cancel_before_minutes", "int", 120, "距离开始时间多久以上可免费无损取消(分钟)"},
            {"late_cancel_fee_ratio", "double", 0.3, "临期取消违约金扣费比例 (0.0~1.0)"},
            {"non_refundable_before_minutes", "int", 0, "距离开始不足多少分钟全额扣款不退"},
            {"forbid_cancel_before_minutes", "int", 0, "距离开始不足多少分钟禁止取消"},
            {"check_in_earliest_before_minutes", "int", 30, "开场前最早允许签到分钟数"},
            {"check_in_grace_after_minutes", "int", 15, "开场后最晚签到宽限分钟数(超时自动判爽约释放席位)"}
        },
        raw_cfg_
    };
}

BrickVerdict CancellationAndCheckInBrick::on_pre_cancel(CancelContext& ctx) {
    if (ctx.by_admin || ctx.record.status == BookingState::WAITLISTED || ctx.record.status == BookingState::PENDING_APPROVAL) {
        ctx.penalty_fee = 0.0;
        ctx.refund_amount = ctx.record.price;
        return BrickVerdict::pass("免责全额退回");
    }

    int64_t mins_before = ctx.record.start_time - ctx.now;
    if (forbid_cancel_before_ > 0 && mins_before < forbid_cancel_before_) {
        return BrickVerdict::reject(
            "CANCEL_WINDOW_CLOSED",
            "距开始不足 " + std::to_string(forbid_cancel_before_) + " 分钟，已关闭自主退订通道");
    }

    if (mins_before >= free_cancel_before_) {
        ctx.penalty_fee = 0.0;
        ctx.refund_amount = ctx.record.price;
        return BrickVerdict::pass("满足免费退改时限，全额退款 ¥" + fmt_money(ctx.refund_amount));
    }

    if (non_refundable_before_ > 0 && mins_before < non_refundable_before_) {
        ctx.penalty_fee = ctx.record.price;
        ctx.refund_amount = 0.0;
        return BrickVerdict::mutated("临近开场不足 " + std::to_string(non_refundable_before_) + " 分钟，违约金 100%");
    }

    ctx.penalty_fee = std::round(ctx.record.price * late_cancel_fee_ratio_ * 100.0) / 100.0;
    ctx.refund_amount = std::max(0.0, std::round((ctx.record.price - ctx.penalty_fee) * 100.0) / 100.0);
    return BrickVerdict::mutated(
        "临期退订扣除违约金 ¥" + fmt_money(ctx.penalty_fee) + "，应退金额 ¥" + fmt_money(ctx.refund_amount));
}

BrickVerdict CancellationAndCheckInBrick::on_pre_check_in(CheckInContext& ctx) {
    EpochMinute earliest = ctx.record.start_time - check_in_earliest_before_;
    EpochMinute latest = check_in_grace_after_ > 0
        ? (ctx.record.start_time + check_in_grace_after_)
        : ctx.record.end_time;

    if (ctx.now < earliest) {
        return BrickVerdict::reject(
            "CHECK_IN_TOO_EARLY",
            "尚未到达签到开放时间 (最早可于开场前 " + std::to_string(check_in_earliest_before_) + " 分钟签到)");
    }
    if (ctx.now > latest) {
        return BrickVerdict::reject(
            "CHECK_IN_EXPIRED",
            "已超过最晚签到宽限期 (" + time_utils::format_datetime(latest) + ")");
    }
    return BrickVerdict::pass("签到窗口校验通过");
}

// ============================================================================
// BrickRegistry
// ============================================================================
BrickRegistry::BrickRegistry() {
    register_brick("OpeningHours", [](const JsonValue& c) { return std::make_shared<OpeningHoursBrick>(c); });
    register_brick("SlotGranularity", [](const JsonValue& c) { return std::make_shared<SlotGranularityBrick>(c); });
    register_brick("BookingWindow", [](const JsonValue& c) { return std::make_shared<BookingWindowBrick>(c); });
    register_brick("BufferTime", [](const JsonValue& c) { return std::make_shared<BufferTimeBrick>(c); });
    register_brick("RoleAndCreditGate", [](const JsonValue& c) { return std::make_shared<RoleAndCreditGateBrick>(c); });
    register_brick("PartySizeMatcher", [](const JsonValue& c) { return std::make_shared<PartySizeMatcherBrick>(c); });
    register_brick("QuotaLimiter", [](const JsonValue& c) { return std::make_shared<QuotaLimiterBrick>(c); });
    register_brick("PricingAndDeposit", [](const JsonValue& c) { return std::make_shared<PricingAndDepositBrick>(c); });
    register_brick("Waitlist", [](const JsonValue& c) { return std::make_shared<WaitlistBrick>(c); });
    register_brick("ConflictAndCapacity", [](const JsonValue& c) { return std::make_shared<ConflictAndCapacityBrick>(c); });
    register_brick("ApprovalWorkflow", [](const JsonValue& c) { return std::make_shared<ApprovalWorkflowBrick>(c); });
    register_brick("CancellationAndCheckIn", [](const JsonValue& c) { return std::make_shared<CancellationAndCheckInBrick>(c); });
    register_v3_bricks(*this);
}

BrickRegistry& BrickRegistry::instance() {
    static BrickRegistry reg;
    return reg;
}

void BrickRegistry::register_brick(const std::string& type_id, Creator creator) {
    creators_[type_id] = std::move(creator);
}

BrickPtr BrickRegistry::create(const std::string& type_id, const JsonValue& cfg) const {
    auto it = creators_.find(type_id);
    if (it == creators_.end()) return nullptr;
    return it->second(cfg);
}

std::vector<BrickDescriptor> BrickRegistry::catalog() const {
    std::vector<BrickDescriptor> list;
    for (const auto& [_, fn] : creators_) {
        auto b = fn(JsonValue::object());
        list.push_back(b->describe());
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) {
        return a.order < b.order;
    });
    return list;
}

} // namespace chronokit
