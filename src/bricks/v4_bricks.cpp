#include "chronokit/bricks/v4_bricks.hpp"
#include "chronokit/bricks/builtin_bricks.hpp"
#include "chronokit/core/timeline_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace chronokit {

namespace {
std::string fmt_money(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}
} // namespace

// ============================================================================
// 22. EnvironmentalRuleBrick
// ============================================================================
void EnvironmentalRuleBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("outdoor_tag")) outdoor_tag_ = cfg["outdoor_tag"].as_string("outdoor");
    if (cfg.contains("blocked_weather") && cfg["blocked_weather"].is_array()) {
        blocked_weather_.clear();
        for (const auto& w : cfg["blocked_weather"].as_array()) blocked_weather_.insert(w.as_string());
    }
    if (cfg.contains("advisory_weather") && cfg["advisory_weather"].is_array()) {
        advisory_weather_.clear();
        for (const auto& w : cfg["advisory_weather"].as_array()) advisory_weather_.insert(w.as_string());
    }
    if (cfg.contains("max_wind_level")) max_wind_level_ = static_cast<int>(cfg["max_wind_level"].as_int(6));
    if (cfg.contains("max_aqi")) max_aqi_ = static_cast<int>(cfg["max_aqi"].as_int(200));
}

BrickDescriptor EnvironmentalRuleBrick::describe() const {
    return {
        type_id(), category(),
        "环境与气象传感器联动积木：感知实时天气/风力/空气质量(AQI)，自动熔断高危室外场地预约并放行全天候室内场馆",
        order(),
        {
            {"outdoor_tag", "string", "outdoor", "受气象条件约束的室外资源标签"},
            {"blocked_weather", "array", JsonValue::array({"rainstorm", "typhoon", "lightning"}), "触发强制安全停用的恶劣天气"},
            {"max_wind_level", "int", 6, "室外资源最大允许风力等级"},
            {"max_aqi", "int", 200, "室外运动场地最大允许空气质量指数(AQI)"}
        },
        raw_cfg_
    };
}

BrickVerdict EnvironmentalRuleBrick::on_pre_book(BookingContext& ctx) {
    bool has_outdoor = false;
    std::string outdoor_name;
    for (const auto& r : ctx.resources) {
        if (r.has_tag(outdoor_tag_)) {
            has_outdoor = true;
            outdoor_name = r.name;
            break;
        }
    }
    if (!has_outdoor) {
        return BrickVerdict::pass("全天候室内资源，不受外部气象限制");
    }

    std::string weather = meta_get_string(ctx.env_context, "weather", "sunny");
    int wind = static_cast<int>(meta_get_int(ctx.env_context, "wind_level", 2));
    int aqi = static_cast<int>(meta_get_int(ctx.env_context, "aqi", 45));

    if (blocked_weather_.count(weather)) {
        return BrickVerdict::reject(
            "WEATHER_SAFETY_LOCKOUT",
            "气象安全熔断：当前天气为 [" + weather + "]，室外场地 [" + outdoor_name + "] 已暂停预约，建议改约室内场馆");
    }
    if (max_wind_level_ > 0 && wind > max_wind_level_) {
        return BrickVerdict::reject(
            "HIGH_WIND_LOCKOUT",
            "大风安全预警：当前风力 " + std::to_string(wind) + " 级 (超安全阈值 " +
            std::to_string(max_wind_level_) + " 级)，室外场地暂停开放");
    }
    if (max_aqi_ > 0 && aqi > max_aqi_) {
        return BrickVerdict::reject(
            "HIGH_AQI_LOCKOUT",
            "空气重度污染预警：当前 AQI=" + std::to_string(aqi) + " (超阈值 " +
            std::to_string(max_aqi_) + ")，室外运动场地暂停开放");
    }

    if (advisory_weather_.count(weather)) {
        ctx.working_metadata["weather_advisory"] = weather;
        return BrickVerdict::mutated("气象温馨提示：当前天气 [" + weather + "]，请注意室外活动防护");
    }

    return BrickVerdict::pass(
        "气象条件适宜 (" + weather + ", 风力" + std::to_string(wind) + "级, AQI " + std::to_string(aqi) + ")");
}

// ============================================================================
// 23. MembershipPassBrick
// ============================================================================
void MembershipPassBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("credit_attr")) credit_attr_ = cfg["credit_attr"].as_string("pass_credits");
    if (cfg.contains("cost_mode")) cost_mode_ = cfg["cost_mode"].as_string("per_booking");
    if (cfg.contains("credits_per_unit")) credits_per_unit_ = std::max(1, static_cast<int>(cfg["credits_per_unit"].as_int(1)));
    if (cfg.contains("require_pass")) require_pass_ = cfg["require_pass"].as_bool(false);
    if (cfg.contains("refund_on_free_cancel")) refund_on_free_cancel_ = cfg["refund_on_free_cancel"].as_bool(true);
}

BrickDescriptor MembershipPassBrick::describe() const {
    return {
        type_id(), category(),
        "次卡/课时包/算力卡额度抵扣积木：支持按次/按时/按人数自动核销预付卡余额免除现金，并在合规退订或未中签时自动原路退回卡次",
        order(),
        {
            {"credit_attr", "string", "pass_credits", "用户 attributes 中存储次卡/课时额度的字段名"},
            {"cost_mode", "string", "per_booking", "扣次规则: per_booking | per_hour | per_unit"},
            {"credits_per_unit", "int", 1, "每单位消耗的卡次/点数"},
            {"require_pass", "bool", false, "是否强制仅限持卡会员预约 (false 则无卡用户自动走普通现金支付)"}
        },
        raw_cfg_
    };
}

BrickVerdict MembershipPassBrick::on_pre_book(BookingContext& ctx) {
    int needed = credits_per_unit_;
    if (cost_mode_ == "per_hour") {
        int hours = std::max(1, static_cast<int>(std::ceil(ctx.duration_minutes() / 60.0)));
        needed = hours * credits_per_unit_;
    } else if (cost_mode_ == "per_unit") {
        needed = ctx.request.units * credits_per_unit_;
    }

    int64_t balance = meta_get_int(ctx.actor.attributes, credit_attr_, 0);
    if (balance < needed) {
        if (require_pass_) {
            return BrickVerdict::reject(
                "INSUFFICIENT_PASS_CREDITS",
                "会员次卡/课时额度不足：需 " + std::to_string(needed) + " 点 [" + credit_attr_ +
                "]，当前剩余 " + std::to_string(balance) + " 点");
        }
        return BrickVerdict::pass("未持有足够次卡额度 (" + std::to_string(balance) + ")，按常规现金计费");
    }

    double waived = ctx.price;
    ctx.discount_amount = std::round((ctx.discount_amount + waived) * 100.0) / 100.0;
    ctx.price = 0.0;
    ctx.working_metadata["_pass_attr"] = credit_attr_;
    ctx.working_metadata["_pass_pending_deduct"] = static_cast<int64_t>(needed);

    return BrickVerdict::mutated(
        "次卡额度抵扣成功：核销 " + std::to_string(needed) + " 点 [" + credit_attr_ +
        "] (剩余 " + std::to_string(balance - needed) + " 点)，免除现金 ¥" + fmt_money(waived));
}

void MembershipPassBrick::on_post_book(BookingContext& ctx, BookingRecord& record) {
    int64_t deduct = meta_get_int(ctx.working_metadata, "_pass_pending_deduct", 0);
    // Avoid double deduction during reschedule if already deducted on original booking
    if (deduct > 0 && !ctx.dry_run) {
        int64_t cur = meta_get_int(ctx.actor.attributes, credit_attr_, 0);
        ctx.actor.attributes[credit_attr_] = std::max<int64_t>(0, cur - deduct);
        ctx.actor_mutated = true;
        record.metadata["_pass_attr"] = credit_attr_;
        record.metadata["_pass_deducted"] = deduct;
        record.metadata.erase("_pass_pending_deduct");
    }
}

void MembershipPassBrick::on_post_cancel(CancelContext& ctx, BookingRecord& record) {
    if (!refund_on_free_cancel_) return;
    int64_t deducted = meta_get_int(record.metadata, "_pass_deducted", 0);
    std::string attr = meta_get_string(record.metadata, "_pass_attr", credit_attr_);
    if (deducted > 0 && ctx.penalty_fee <= 0.0 && !ctx.booker_actor.id.empty()) {
        int64_t cur = meta_get_int(ctx.booker_actor.attributes, attr, 0);
        ctx.booker_actor.attributes[attr] = cur + deducted;
        ctx.booker_actor_mutated = true;
        record.metadata["_pass_refunded"] = deducted;
        record.metadata["_pass_deducted"] = int64_t(0);
    }
}

// ============================================================================
// 24. PriceGuardBrick
// ============================================================================
void PriceGuardBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("min_price")) min_price_ = cfg["min_price"].as_double(0.0);
    if (cfg.contains("max_price")) max_price_ = cfg["max_price"].as_double(0.0);
    if (cfg.contains("round_mode")) round_mode_ = cfg["round_mode"].as_string("two_decimals");
}

BrickDescriptor PriceGuardBrick::describe() const {
    return {
        type_id(), category(),
        "价格封顶保底与规整护栏积木：在多重溢价/补贴/优惠叠加后执行监管限价封顶、最低消费兜底与金额抹零规整",
        order(),
        {
            {"min_price", "double", 0.0, "最低收费兜底金额"},
            {"max_price", "double", 0.0, "最高收费封顶金额 (0 表示不封顶)"},
            {"round_mode", "string", "two_decimals", "规整方式: two_decimals | nearest_int | floor_int"}
        },
        raw_cfg_
    };
}

BrickVerdict PriceGuardBrick::on_pre_book(BookingContext& ctx) {
    double orig = ctx.price;
    double p = orig;

    if (min_price_ > 0.0 && p < min_price_) p = min_price_;
    if (max_price_ > 0.0 && p > max_price_) {
        ctx.discount_amount += (p - max_price_);
        p = max_price_;
    }

    if (round_mode_ == "nearest_int") p = std::round(p);
    else if (round_mode_ == "floor_int") p = std::floor(p);
    else p = std::round(p * 100.0) / 100.0;

    if (std::abs(p - orig) > 1e-6) {
        ctx.price = p;
        return BrickVerdict::mutated(
            "价格护栏规整：由 ¥" + fmt_money(orig) + " 调整为 ¥" + fmt_money(ctx.price));
    }
    return BrickVerdict::pass("价格处于合规区间 (¥" + fmt_money(ctx.price) + ")");
}

// ============================================================================
// 25. CompanionStaffAssignerBrick
// ============================================================================
void CompanionStaffAssignerBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    resource_to_staff_kind_.clear();
    tag_to_staff_skill_.clear();
    if (cfg.contains("resource_to_staff_kind") && cfg["resource_to_staff_kind"].is_object()) {
        for (const auto& [rk, sk] : cfg["resource_to_staff_kind"].as_object()) {
            resource_to_staff_kind_[rk] = sk.as_string();
        }
    }
    if (cfg.contains("tag_to_staff_skill") && cfg["tag_to_staff_skill"].is_object()) {
        for (const auto& [rt, st] : cfg["tag_to_staff_skill"].as_object()) {
            tag_to_staff_skill_[rt] = st.as_string();
        }
    }
    if (cfg.contains("charge_staff_rate")) charge_staff_rate_ = cfg["charge_staff_rate"].as_bool(true);
}

BrickDescriptor CompanionStaffAssignerBrick::describe() const {
    return {
        type_id(), category(),
        "双轴服务人员自动排班联动积木：预订主场地/设备时，自动匹配空闲且具备对应资质标签的教练/音控师/技师并原子捆绑锁班",
        order(),
        {
            {"resource_to_staff_kind", "object", JsonValue::object(), "主资源 kind -> 必需配套人员 kind 映射表"},
            {"tag_to_staff_skill", "object", JsonValue::object(), "主资源标签 -> 人员必需技能标签映射表"},
            {"charge_staff_rate", "bool", true, "是否自动叠加配套人员工时服务费"}
        },
        raw_cfg_
    };
}

BrickVerdict CompanionStaffAssignerBrick::on_pre_book(BookingContext& ctx) {
    const Resource primary = ctx.primary_resource();
    auto it = resource_to_staff_kind_.find(primary.kind);
    if (it == resource_to_staff_kind_.end() || it->second.empty()) {
        return BrickVerdict::pass("该资源无需强制指派专职配套人员");
    }

    const std::string& staff_kind = it->second;

    // Collect required staff skills based on primary resource tags or request metadata
    std::set<std::string> req_skills;
    for (const auto& [r_tag, s_skill] : tag_to_staff_skill_) {
        if (primary.has_tag(r_tag)) req_skills.insert(s_skill);
    }
    std::string extra_skill = meta_get_string(ctx.working_metadata, "required_skill");
    if (!extra_skill.empty()) req_skills.insert(extra_skill);

    // Check if caller already explicitly bundled a staff resource of `staff_kind`
    for (size_t i = 1; i < ctx.resources.size(); ++i) {
        if (ctx.resources[i].kind == staff_kind) {
            for (const auto& sk : req_skills) {
                if (!ctx.resources[i].has_tag(sk)) {
                    return BrickVerdict::reject(
                        "STAFF_SKILL_MISMATCH",
                        "所选配套人员 [" + ctx.resources[i].name + "] 不具备必需资质标签 #" + sk);
                }
            }
            return BrickVerdict::pass("已包含用户指定配套人员 [" + ctx.resources[i].name + "]");
        }
    }

    EpochMinute day_s = time_utils::start_of_day(ctx.request.start_time);
    int req_start_mod = time_utils::minute_of_day(ctx.request.start_time);
    int req_end_mod = req_start_mod + ctx.duration_minutes();

    const Resource* best_staff = nullptr;
    int64_t best_workload = std::numeric_limits<int64_t>::max();

    for (const auto& cand : ctx.all_resources) {
        if (cand.kind != staff_kind || cand.status != ResourceStatus::ACTIVE) continue;

        bool skills_ok = true;
        for (const auto& sk : req_skills) {
            if (!cand.has_tag(sk)) { skills_ok = false; break; }
        }
        if (!skills_ok) continue;

        // Respect per-staff shift hours if specified in attributes
        std::string s_open = meta_get_string(cand.attributes, "open_time");
        std::string s_close = meta_get_string(cand.attributes, "close_time");
        if (!s_open.empty() && !s_close.empty()) {
            int om = time_utils::parse_hhmm(s_open).value_or(0);
            int cm = time_utils::parse_hhmm(s_close).value_or(1440);
            if (req_start_mod < om || req_end_mod > cm) continue;
        }

        int used = TimelineIndex::peak_occupied_units(
            ctx.existing_bookings, cand.id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
        if (used > 0) continue;

        int64_t workload = 0;
        for (const auto& b : ctx.existing_bookings) {
            if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
            if (!occupies_capacity(b.status)) continue;
            if (time_utils::start_of_day(b.start_time) == day_s &&
                std::find(b.resource_ids.begin(), b.resource_ids.end(), cand.id) != b.resource_ids.end())
            {
                workload += (b.end_time - b.start_time);
            }
        }

        if (workload < best_workload) {
            best_workload = workload;
            best_staff = &cand;
        }
    }

    if (!best_staff) {
        return BrickVerdict::reject(
            "NO_AVAILABLE_COMPANION_STAFF",
            "排班不足：该时段暂无空闲且具备对应资质的配套人员 [" + staff_kind + "]");
    }

    ctx.resources.push_back(*best_staff);
    double staff_fee = 0.0;
    if (charge_staff_rate_) {
        double rate = meta_get_double(best_staff->attributes, "base_rate", 0.0);
        if (rate > 0.0) {
            staff_fee = std::round(rate * (ctx.duration_minutes() / 60.0) * 100.0) / 100.0;
            ctx.price = std::round((ctx.price + staff_fee) * 100.0) / 100.0;
        }
    }

    return BrickVerdict::mutated(
        "已自动排班指派配套人员 [" + best_staff->name + "]" +
        (staff_fee > 0.0 ? " (工时费 +¥" + fmt_money(staff_fee) + ")" : ""));
}

// ============================================================================
// 26. LotteryAndFairQueueBrick
// ============================================================================
void LotteryAndFairQueueBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("min_lead_hours_for_lottery")) min_lead_hours_ = static_cast<int>(cfg["min_lead_hours_for_lottery"].as_int(24));
    if (cfg.contains("only_weekends")) only_weekends_ = cfg["only_weekends"].as_bool(false);
    trigger_tags_.clear();
    if (cfg.contains("trigger_tags") && cfg["trigger_tags"].is_array()) {
        for (const auto& t : cfg["trigger_tags"].as_array()) trigger_tags_.insert(t.as_string());
    }
    if (cfg.contains("max_applicants_per_slot")) max_applicants_per_slot_ = static_cast<int>(cfg["max_applicants_per_slot"].as_int(30));
    role_weight_bonus_.clear();
    if (cfg.contains("role_weight_bonus") && cfg["role_weight_bonus"].is_object()) {
        for (const auto& [r, w] : cfg["role_weight_bonus"].as_object()) {
            role_weight_bonus_[r] = static_cast<int>(w.as_int(0));
        }
    }
}

BrickDescriptor LotteryAndFairQueueBrick::describe() const {
    return {
        type_id(), category(),
        "防外挂公证摇号抽签池积木：稀缺场次在提前申购期内不拼手速，统一进入 LOTTERY_PENDING 摇号池按信用与权重公平抽签",
        order(),
        {
            {"min_lead_hours_for_lottery", "int", 24, "距开场大于等于多少小时进入摇号申购池 (临期空余场次自动转直接捡漏)"},
            {"only_weekends", "bool", false, "是否仅针对周六日热门场次开启公证摇号"},
            {"trigger_tags", "array", JsonValue::array(), "触发摇号的资源标签列表 (为空则全部资源适用)"},
            {"max_applicants_per_slot", "int", 30, "单时段最大摇号申购登记人数上限"}
        },
        raw_cfg_
    };
}

BrickVerdict LotteryAndFairQueueBrick::on_pre_book(BookingContext& ctx) {
    if (meta_get_bool(ctx.working_metadata, "skip_lottery", false)) {
        return BrickVerdict::pass("跳过摇号直通确认");
    }

    int64_t lead_mins = ctx.request.start_time - ctx.now;
    if (lead_mins < static_cast<int64_t>(min_lead_hours_) * 60) {
        return BrickVerdict::pass("已过摇号申购截止期，进入常规即时捡漏锁座模式");
    }

    if (only_weekends_) {
        int wd = time_utils::weekday_iso(ctx.request.start_time);
        if (wd < 6) return BrickVerdict::pass("工作日场次免摇号即时确认");
    }

    if (!trigger_tags_.empty()) {
        bool hit = false;
        for (const auto& t : trigger_tags_) {
            if (ctx.primary_resource().has_tag(t)) { hit = true; break; }
        }
        if (!hit) return BrickVerdict::pass("非热门摇号标签资源，即时确认");
    }

    // If this slot already has a CONFIRMED winner (lottery already drawn!), let normal capacity/waitlist handle it
    int confirmed_used = TimelineIndex::peak_occupied_units(
        ctx.existing_bookings, ctx.primary_resource().id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
    if (confirmed_used >= ctx.primary_resource().capacity) {
        return BrickVerdict::pass("该场次摇号已开奖满额，转交容量与候补队列处理");
    }

    int pool_count = 0;
    for (const auto& b : ctx.existing_bookings) {
        if (!ctx.exclude_booking_id.empty() && b.id == ctx.exclude_booking_id) continue;
        if (b.status != BookingState::LOTTERY_PENDING) continue;
        if (b.primary_resource_id() != ctx.primary_resource().id) continue;
        if (std::max(b.start_time, ctx.request.start_time) < std::min(b.end_time, ctx.request.end_time)) {
            if (b.actor_id == ctx.actor.id) {
                return BrickVerdict::reject("DUPLICATE_LOTTERY_ENTRY", "您已参与该场次的公证摇号登记，请勿重复申购");
            }
            ++pool_count;
        }
    }

    if (max_applicants_per_slot_ > 0 && pool_count >= max_applicants_per_slot_) {
        return BrickVerdict::reject("LOTTERY_POOL_FULL", "该场次摇号申购人数已达上限 (" + std::to_string(max_applicants_per_slot_) + " 人)");
    }

    int weight = std::max(10, ctx.actor.credit_score);
    for (const auto& r : ctx.actor.roles) {
        auto it = role_weight_bonus_.find(r);
        if (it != role_weight_bonus_.end()) weight += it->second;
    }

    ctx.target_state = BookingState::LOTTERY_PENDING;
    ctx.bypass_capacity_lock = true;
    ctx.priority = std::max(ctx.priority, weight);
    ctx.working_metadata["_lottery_weight"] = static_cast<int64_t>(weight);

    return BrickVerdict::mutated(
        "已登记进入公证摇号池 (当前申购 #" + std::to_string(pool_count + 1) +
        "，中签权重=" + std::to_string(weight) + ")，待开奖确认",
        "LOTTERY_QUEUED");
}

// ============================================================================
// 27. HierarchicalTreeLockBrick
// ============================================================================
void HierarchicalTreeLockBrick::configure(const JsonValue& cfg) {
    raw_cfg_ = cfg.is_object() ? cfg : JsonValue::object();
    if (cfg.contains("mode")) mode_ = cfg["mode"].as_string("exclusive_subdivision");
    if (cfg.contains("route_to_waitlist")) route_to_waitlist_ = cfg["route_to_waitlist"].as_bool(true);
}

BrickDescriptor HierarchicalTreeLockBrick::describe() const {
    return {
        type_id(), category(),
        "空间父子拓扑联动互斥锁积木：基于 parent_id 自动维护『可拆分大宴会厅/全场球场 <-> 子分厅/半场』双向互斥或父空间安全容量包络",
        order(),
        {
            {"mode", "string", "exclusive_subdivision", "联动模式: exclusive_subdivision (全场与子厅双向互斥) | parent_capacity_envelope (父空间总容量包络)"},
            {"route_to_waitlist", "bool", true, "发生父子空间冲突时是否允许自动转入候补队列"}
        },
        raw_cfg_
    };
}

bool HierarchicalTreeLockBrick::check_subdivision_conflict(
    const std::string& resource_id,
    EpochMinute eff_start,
    EpochMinute eff_end,
    const std::vector<Resource>& all_resources,
    const std::vector<BookingRecord>& existing_bookings,
    const std::string& exclude_booking_id,
    std::string* out_conflict_res_name)
{
    std::map<std::string, const Resource*> by_id;
    std::map<std::string, std::vector<std::string>> children_of;
    for (const auto& r : all_resources) {
        by_id[r.id] = &r;
        if (!r.parent_id.empty()) {
            children_of[r.parent_id].push_back(r.id);
        }
    }

    auto it_self = by_id.find(resource_id);
    if (it_self == by_id.end()) return false;

    // 1. Walk ancestors up to root
    std::string cur_parent = it_self->second->parent_id;
    while (!cur_parent.empty()) {
        if (TimelineIndex::peak_occupied_units(existing_bookings, cur_parent, eff_start, eff_end, exclude_booking_id) > 0) {
            if (out_conflict_res_name && by_id.count(cur_parent)) {
                *out_conflict_res_name = "父级整体空间 [" + by_id[cur_parent]->name + "]";
            }
            return true;
        }
        auto it_p = by_id.find(cur_parent);
        cur_parent = (it_p != by_id.end()) ? it_p->second->parent_id : "";
    }

    // 2. BFS/DFS all descendants
    std::vector<std::string> stack = children_of[resource_id];
    while (!stack.empty()) {
        std::string cid = stack.back();
        stack.pop_back();
        if (TimelineIndex::peak_occupied_units(existing_bookings, cid, eff_start, eff_end, exclude_booking_id) > 0) {
            if (out_conflict_res_name && by_id.count(cid)) {
                *out_conflict_res_name = "下属子分区 [" + by_id[cid]->name + "]";
            }
            return true;
        }
        for (const auto& grand : children_of[cid]) stack.push_back(grand);
    }

    return false;
}

BrickVerdict HierarchicalTreeLockBrick::on_pre_book(BookingContext& ctx) {
    if (ctx.target_state == BookingState::LOTTERY_PENDING || ctx.target_state == BookingState::WAITLISTED) {
        return BrickVerdict::pass("候补/摇号态暂缓父子拓扑物理锁占");
    }

    std::map<std::string, const Resource*> by_id;
    std::map<std::string, std::vector<std::string>> children_of;
    for (const auto& r : ctx.all_resources) {
        by_id[r.id] = &r;
        if (!r.parent_id.empty()) children_of[r.parent_id].push_back(r.id);
    }

    // Prevent bundling both parent and its own descendant in a single request
    std::set<std::string> req_set;
    for (const auto& r : ctx.resources) req_set.insert(r.id);
    for (const auto& r : ctx.resources) {
        std::string p = r.parent_id;
        while (!p.empty()) {
            if (req_set.count(p)) {
                return BrickVerdict::reject(
                    "HIERARCHY_SELF_BUNDLE_CONFLICT",
                    "不可在同一订单中同时捆绑父级空间 [" + p + "] 与其子分区 [" + r.id + "]");
            }
            p = by_id.count(p) ? by_id[p]->parent_id : "";
        }
    }

    if (mode_ == "exclusive_subdivision") {
        for (const auto& r : ctx.resources) {
            std::string conflict_desc;
            if (check_subdivision_conflict(
                    r.id, ctx.effective_start, ctx.effective_end,
                    ctx.all_resources, ctx.existing_bookings, ctx.exclude_booking_id, &conflict_desc))
            {
                if (route_to_waitlist_ && ctx.waitlist_enabled && ctx.request.allow_waitlist && !ctx.dry_run) {
                    ctx.target_state = BookingState::WAITLISTED;
                    return BrickVerdict::mutated(
                        "空间拓扑互斥：" + conflict_desc + " 在该时段已占用，已自动转入候补队列",
                        "WAITLIST");
                }
                return BrickVerdict::reject(
                    "HIERARCHY_TOPOLOGY_CONFLICT",
                    "空间父子拓扑互斥：因 " + conflict_desc + " 在该时段已有预约，[" + r.name + "] 当前不可同时锁占");
            }
        }
        return BrickVerdict::pass("空间父子拓扑互斥校验通过");
    }

    // Mode: parent_capacity_envelope
    for (const auto& r : ctx.resources) {
        if (r.parent_id.empty() || !by_id.count(r.parent_id)) continue;
        const Resource* parent = by_id[r.parent_id];
        int sum_used = TimelineIndex::peak_occupied_units(
            ctx.existing_bookings, parent->id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
        for (const auto& sib_id : children_of[parent->id]) {
            sum_used += TimelineIndex::peak_occupied_units(
                ctx.existing_bookings, sib_id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id);
        }
        if (sum_used + ctx.request.units > parent->capacity) {
            return BrickVerdict::reject(
                "PARENT_ENVELOPE_EXCEEDED",
                "超出父级空间 [" + parent->name + "] 并发安全总容量包络 (" +
                std::to_string(sum_used + ctx.request.units) + "/" + std::to_string(parent->capacity) + ")");
        }
    }

    return BrickVerdict::pass("父级空间安全容量包络校验通过");
}

// ============================================================================
// 28. UnlessBrick (`Brick.Unless`)
// ============================================================================
UnlessBrick::UnlessBrick(std::string name, int ord, JsonValue cond_cfg, BrickPtr inner_brick)
    : name_(std::move(name))
    , order_(ord)
    , inner_(std::move(inner_brick))
    , cond_cfg_(std::move(cond_cfg))
{
    // Delegate condition evaluation to WhenBrick(cond_cfg, /*then=*/nullptr, /*else=*/inner_)!
    delegate_ = std::make_shared<WhenBrick>(name_, order_, cond_cfg_, nullptr, inner_);
}

void UnlessBrick::configure(const JsonValue& cfg) {
    cond_cfg_ = cfg;
    delegate_ = std::make_shared<WhenBrick>(name_, order_, cond_cfg_, nullptr, inner_);
}

BrickDescriptor UnlessBrick::describe() const {
    std::vector<BrickDescriptor> kids;
    if (inner_) kids.push_back(inner_->describe());
    return {
        instance_name(), category(),
        "高阶豁免守卫组合子(Unless)：除非命中豁免条件，否则强制执行内部子积木",
        order_,
        {{"condition", "object", cond_cfg_, "豁免条件"}},
        cond_cfg_,
        std::move(kids)
    };
}

BrickVerdict UnlessBrick::on_pre_book(BookingContext& ctx) {
    BrickVerdict v = delegate_->on_pre_book(ctx);
    return v;
}

void register_v4_bricks(BrickRegistry& reg) {
    reg.register_brick("EnvironmentalRule", [](const JsonValue& c) { return std::make_shared<EnvironmentalRuleBrick>(c); });
    reg.register_brick("MembershipPass", [](const JsonValue& c) { return std::make_shared<MembershipPassBrick>(c); });
    reg.register_brick("PriceGuard", [](const JsonValue& c) { return std::make_shared<PriceGuardBrick>(c); });
    reg.register_brick("CompanionStaffAssigner", [](const JsonValue& c) { return std::make_shared<CompanionStaffAssignerBrick>(c); });
    reg.register_brick("LotteryAndFairQueue", [](const JsonValue& c) { return std::make_shared<LotteryAndFairQueueBrick>(c); });
    reg.register_brick("HierarchicalTreeLock", [](const JsonValue& c) { return std::make_shared<HierarchicalTreeLockBrick>(c); });
}

} // namespace chronokit
