#include "chronokit/engine/appointment_engine.hpp"
#include "chronokit/core/timeline_index.hpp"
#include "chronokit/storage/memory_storage.hpp"
#include "chronokit/storage/sqlite_storage.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace chronokit {

AppointmentEngine::AppointmentEngine(std::shared_ptr<IStorage> storage)
    : storage_(storage ? std::move(storage) : std::make_shared<MemoryStorage>())
    , lua_vm_(std::make_shared<LuaVM>())
{
    event_bus_.subscribe([this](const DomainEvent& ev) {
        if (lua_vm_) lua_vm_->dispatch_event(ev);
    });
}

AppointmentEngine::~AppointmentEngine() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    bricks_.clear(); // Release LuaScriptBrick registry refs while lua_vm_ is alive
}

void AppointmentEngine::sort_pipeline() {
    std::stable_sort(bricks_.begin(), bricks_.end(), [](const BrickPtr& a, const BrickPtr& b) {
        return a->order() < b->order();
    });
    active_bp_.bricks = bricks_;
}

void AppointmentEngine::load_blueprint_file(const std::string& path, bool load_seed_data) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    bricks_.clear();
    BlueprintSpec spec = lua_vm_->load_blueprint_file(path);
    active_bp_ = spec;
    bricks_ = spec.bricks;
    sort_pipeline();

    if (load_seed_data) {
        storage_->clear_all();
        event_bus_.clear_history();
        for (const auto& r : spec.seed_resources) storage_->upsert_resource(r);
        for (const auto& a : spec.seed_actors) storage_->upsert_actor(a);
    }

    event_bus_.publish({
        "blueprint_loaded",
        time_utils::now_local_minutes(),
        "", "", "",
        "已加载预约系统蓝图 [" + active_bp_.name + "]，组装积木 " + std::to_string(bricks_.size()) + " 块",
        JsonValue::object({{"blueprint_id", active_bp_.id}, {"brick_count", bricks_.size()}})
    });
}

void AppointmentEngine::reload_blueprint_code(const std::string& lua_code, bool reset_and_seed, const std::string& chunk_name) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    // Parse new blueprint first; if Lua has a syntax/runtime error, old pipeline stays intact!
    BlueprintSpec spec = lua_vm_->load_blueprint_string(lua_code, chunk_name);
    bricks_.clear();
    active_bp_ = spec;
    bricks_ = spec.bricks;
    sort_pipeline();

    if (reset_and_seed) {
        storage_->clear_all();
        for (const auto& r : spec.seed_resources) storage_->upsert_resource(r);
        for (const auto& a : spec.seed_actors) storage_->upsert_actor(a);
    } else {
        // Upsert any newly added seed resources/actors without wiping existing bookings!
        for (const auto& r : spec.seed_resources) {
            if (!storage_->get_resource(r.id)) storage_->upsert_resource(r);
        }
        for (const auto& a : spec.seed_actors) {
            if (!storage_->get_actor(a.id)) storage_->upsert_actor(a);
        }
    }

    event_bus_.publish({
        "blueprint_hot_reloaded",
        time_utils::now_local_minutes(),
        "", "", "",
        "Lua 蓝图热重载成功：[" + active_bp_.name + "] (流水线含 " + std::to_string(bricks_.size()) + " 块积木)",
        JsonValue::object({{"blueprint_id", active_bp_.id}, {"reset_data", reset_and_seed}})
    });
}

void AppointmentEngine::add_brick(BrickPtr brick) {
    if (!brick) return;
    std::lock_guard<std::recursive_mutex> lk(mu_);
    bricks_.push_back(std::move(brick));
    sort_pipeline();
}

bool AppointmentEngine::remove_brick(const std::string& type_or_instance_name) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto it = std::remove_if(bricks_.begin(), bricks_.end(), [&](const BrickPtr& b) {
        return b->type_id() == type_or_instance_name || b->instance_name() == type_or_instance_name;
    });
    if (it == bricks_.end()) return false;
    bricks_.erase(it, bricks_.end());
    active_bp_.bricks = bricks_;
    return true;
}

void AppointmentEngine::clear_bricks() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    bricks_.clear();
    active_bp_.bricks.clear();
}

std::vector<BrickDescriptor> AppointmentEngine::describe_pipeline() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::vector<BrickDescriptor> out;
    out.reserve(bricks_.size());
    for (const auto& b : bricks_) out.push_back(b->describe());
    return out;
}

std::vector<BrickDescriptor> AppointmentEngine::available_brick_catalog() {
    auto cat = BrickRegistry::instance().catalog();
    cat.push_back({
        "LuaPolicy", "Lua",
        "支持用 Lua 5.4 编写任意自定义业务规则、动态计价公式、优先级与准入逻辑的可热更积木",
        60,
        {
            {"name", "string", "CustomLuaRule", "策略积木实例名"},
            {"order", "int", 60, "流水线执行阶段序号"},
            {"on_pre_book", "function", "function(ctx) return true end", "预约前钩子函数"},
            {"on_pre_cancel", "function", nullptr, "取消前钩子函数"},
            {"on_pre_check_in", "function", nullptr, "签到前钩子函数"}
        },
        JsonValue::object()
    });
    return cat;
}

BlueprintSpec AppointmentEngine::current_blueprint_info() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return active_bp_;
}

void AppointmentEngine::upsert_resource(const Resource& r) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    storage_->upsert_resource(r);
}

std::optional<Resource> AppointmentEngine::get_resource(const std::string& id) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return storage_->get_resource(id);
}

std::vector<Resource> AppointmentEngine::list_resources() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return storage_->list_resources();
}

void AppointmentEngine::upsert_actor(const Actor& a) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    storage_->upsert_actor(a);
}

std::optional<Actor> AppointmentEngine::get_actor(const std::string& id) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return storage_->get_actor(id);
}

std::vector<Actor> AppointmentEngine::list_actors() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return storage_->list_actors();
}

BookingOutcome AppointmentEngine::book(const BookingRequest& req) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return evaluate_and_book_locked(req, /*dry_run=*/false);
}

BookingOutcome AppointmentEngine::evaluate_and_book_locked(const BookingRequest& req, bool dry_run) {
    BookingOutcome out;
    if (req.resource_ids.empty()) {
        out.error_code = "EMPTY_RESOURCES";
        out.message = "必须至少指定一个预约资源";
        return out;
    }
    if (req.units <= 0) {
        out.error_code = "INVALID_UNITS";
        out.message = "预约数量必须大于 0";
        return out;
    }

    auto actor_opt = storage_->get_actor(req.actor_id);
    if (!actor_opt) {
        out.error_code = "ACTOR_NOT_FOUND";
        out.message = "预约用户不存在: " + req.actor_id;
        return out;
    }

    std::vector<Resource> resolved_resources;
    std::set<std::string> seen_res;
    for (const auto& rid : req.resource_ids) {
        if (seen_res.count(rid)) continue;
        seen_res.insert(rid);
        auto r_opt = storage_->get_resource(rid);
        if (!r_opt) {
            out.error_code = "RESOURCE_NOT_FOUND";
            out.message = "预约资源不存在: " + rid;
            return out;
        }
        resolved_resources.push_back(*r_opt);
    }

    EpochMinute now = req.now_override.value_or(time_utils::now_local_minutes());
    auto all_bookings = storage_->list_bookings();

    BookingContext ctx{
        req,
        *actor_opt,
        std::move(resolved_resources),
        all_bookings,
        now,
        dry_run
    };
    ctx.effective_start = req.start_time;
    ctx.effective_end = req.end_time;
    ctx.currency = active_bp_.currency;
    ctx.working_metadata = req.metadata;

    for (const auto& brick : bricks_) {
        auto t0 = std::chrono::steady_clock::now();
        BrickVerdict verdict = brick->on_pre_book(ctx);
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();

        BrickDecision dec{
            brick->instance_name(),
            brick->category(),
            verdict.ok,
            verdict.outcome,
            verdict.note,
            us
        };
        out.trace.push_back(dec);

        if (!verdict.ok) {
            out.ok = false;
            out.error_code = verdict.error_code;
            out.message = verdict.note;
            return out;
        }
    }

    BookingRecord rec;
    rec.id = dry_run ? "DRY-RUN" : storage_->next_booking_id();
    rec.series_id = req.series_id;
    rec.actor_id = ctx.actor.id;
    for (const auto& r : ctx.resources) rec.resource_ids.push_back(r.id);
    rec.start_time = req.start_time;
    rec.end_time = req.end_time;
    rec.effective_start = ctx.effective_start;
    rec.effective_end = ctx.effective_end;
    rec.pre_buffer_minutes = ctx.pre_buffer_minutes;
    rec.post_buffer_minutes = ctx.post_buffer_minutes;
    rec.units = req.units;
    rec.status = ctx.target_state;
    rec.priority = ctx.priority;
    rec.price = ctx.price;
    rec.deposit = ctx.deposit;
    rec.currency = ctx.currency;
    rec.created_at = now;
    rec.updated_at = now;
    rec.metadata = ctx.working_metadata;
    rec.decision_trace = out.trace;

    if (!dry_run) {
        for (const auto& brick : bricks_) {
            brick->on_post_book(ctx, rec);
        }
        storage_->save_booking(rec);

        std::string ev_type = "booking_confirmed";
        std::string msg = "预约成功确认 [" + rec.id + "]：" + ctx.actor.name + " -> " + ctx.primary_resource().name;
        if (rec.status == BookingState::WAITLISTED) {
            ev_type = "booking_waitlisted";
            msg = "预约进入候补队列 [" + rec.id + "] (优先级=" + std::to_string(rec.priority) + ")：" + ctx.actor.name;
        } else if (rec.status == BookingState::PENDING_APPROVAL) {
            ev_type = "booking_pending_approval";
            msg = "预约已提交待审批 [" + rec.id + "]：" + ctx.actor.name + " -> " + ctx.primary_resource().name;
        }

        event_bus_.publish({
            ev_type, now, rec.id, rec.primary_resource_id(), rec.actor_id, msg,
            JsonValue::object({
                {"status", to_string(rec.status)},
                {"price", rec.price},
                {"units", rec.units}
            })
        });
    }

    out.ok = true;
    out.message = "状态: " + to_string(rec.status);
    out.record = std::move(rec);
    return out;
}

RecurringOutcome AppointmentEngine::book_recurring(const RecurringRequest& req) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    RecurringOutcome res;
    if (req.occurrences <= 0 || req.interval_days <= 0) {
        res.summary = "周期参数无效";
        return res;
    }

    char sbuf[32];
    std::snprintf(sbuf, sizeof(sbuf), "SER-%04llu", static_cast<unsigned long long>(++series_seq_));
    res.series_id = sbuf;

    std::vector<std::string> created_ids;
    int success_cnt = 0;

    for (int i = 0; i < req.occurrences; ++i) {
        BookingRequest sub = req.base_request;
        int64_t shift = static_cast<int64_t>(i) * req.interval_days * 1440;
        sub.start_time += shift;
        sub.end_time += shift;
        sub.series_id = res.series_id;
        // For recurring bookings, disable auto-waitlist when all_or_nothing=true so conflicts fail deterministically
        if (req.all_or_nothing) sub.allow_waitlist = false;

        BookingOutcome bo = evaluate_and_book_locked(sub, /*dry_run=*/false);
        if (bo.ok) {
            created_ids.push_back(bo.record.id);
            ++success_cnt;
        } else if (req.all_or_nothing) {
            // Rollback previously created occurrences in this series
            for (const auto& cid : created_ids) {
                storage_->delete_booking(cid);
            }
            res.outcomes.push_back(std::move(bo));
            res.ok = false;
            res.summary = "周期预约原子回滚：第 " + std::to_string(i + 1) + " 期冲突或未通过校验";
            return res;
        }
        res.outcomes.push_back(std::move(bo));
    }

    res.ok = success_cnt > 0;
    res.summary = "周期预约完成 (" + std::to_string(success_cnt) + "/" + std::to_string(req.occurrences) + " 期成功)";
    return res;
}

BookingOutcome AppointmentEngine::cancel_booking(
    const std::string& booking_id,
    const std::string& actor_id,
    std::optional<EpochMinute> now_opt,
    const std::string& reason,
    bool by_admin)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    BookingOutcome out;
    auto b_opt = storage_->get_booking(booking_id);
    if (!b_opt) {
        out.error_code = "BOOKING_NOT_FOUND";
        out.message = "未找到预约单: " + booking_id;
        return out;
    }

    BookingRecord rec = *b_opt;
    if (rec.status == BookingState::CANCELLED || rec.status == BookingState::COMPLETED || rec.status == BookingState::NO_SHOW) {
        out.error_code = "INVALID_STATE_FOR_CANCEL";
        out.message = "当前订单状态(" + to_string(rec.status) + ")不可取消";
        return out;
    }

    Actor actor = storage_->get_actor(actor_id).value_or(Actor{actor_id, actor_id, {}, 100, {}});
    bool is_admin = by_admin || actor.has_role("admin");
    if (!is_admin && rec.actor_id != actor_id) {
        out.error_code = "PERMISSION_DENIED";
        out.message = "仅预约本人或管理员可取消该预约单";
        return out;
    }

    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    CancelContext ctx{rec, actor, now, reason, is_admin, 0.0, rec.price, {}};

    for (const auto& brick : bricks_) {
        auto t0 = std::chrono::steady_clock::now();
        BrickVerdict v = brick->on_pre_cancel(ctx);
        auto t1 = std::chrono::steady_clock::now();
        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        out.trace.push_back({brick->instance_name(), brick->category(), v.ok, v.outcome, v.note, us});
        if (!v.ok) {
            out.ok = false;
            out.error_code = v.error_code;
            out.message = v.note;
            return out;
        }
    }

    bool freed_capacity = occupies_capacity(rec.status);
    rec.status = BookingState::CANCELLED;
    rec.penalty_fee = ctx.penalty_fee;
    rec.refund_amount = ctx.refund_amount;
    rec.cancel_reason = reason.empty() ? (is_admin ? "管理员取消" : "用户主动取消") : reason;
    rec.updated_at = now;
    for (const auto& d : out.trace) {
        if (!d.note.empty()) rec.decision_trace.push_back(d);
    }

    for (const auto& brick : bricks_) brick->on_post_cancel(ctx, rec);
    storage_->save_booking(rec);

    event_bus_.publish({
        "booking_cancelled", now, rec.id, rec.primary_resource_id(), rec.actor_id,
        "预约已取消 [" + rec.id + "]，违约金 ¥" + std::to_string(rec.penalty_fee).substr(0, 5) +
        "，退款 ¥" + std::to_string(rec.refund_amount).substr(0, 5),
        JsonValue::object({{"penalty_fee", rec.penalty_fee}, {"refund_amount", rec.refund_amount}})
    });

    if (freed_capacity) {
        try_promote_waitlist_locked(now);
    }

    out.ok = true;
    out.message = "取消成功";
    out.record = rec;
    return out;
}

BookingOutcome AppointmentEngine::approve_booking(
    const std::string& booking_id,
    const std::string& approver_id,
    bool approve,
    const std::string& note,
    std::optional<EpochMinute> now_opt)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    BookingOutcome out;
    auto b_opt = storage_->get_booking(booking_id);
    if (!b_opt) {
        out.error_code = "BOOKING_NOT_FOUND";
        out.message = "未找到预约单: " + booking_id;
        return out;
    }

    BookingRecord rec = *b_opt;
    if (rec.status != BookingState::PENDING_APPROVAL) {
        out.error_code = "NOT_PENDING_APPROVAL";
        out.message = "该预约单不处于待审批状态 (当前: " + to_string(rec.status) + ")";
        return out;
    }

    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    rec.status = approve ? BookingState::CONFIRMED : BookingState::REJECTED;
    rec.updated_at = now;
    if (!approve) {
        rec.refund_amount = rec.price;
        rec.cancel_reason = note.empty() ? "审批驳回" : note;
    }
    rec.decision_trace.push_back({
        "ApprovalDecision", "Workflow", true, approve ? "APPROVED" : "REJECTED",
        "审批人 [" + approver_id + "] " + (approve ? "通过" : "驳回") + (note.empty() ? "" : ": " + note),
        0.0
    });
    storage_->save_booking(rec);

    event_bus_.publish({
        approve ? "booking_approved" : "booking_rejected",
        now, rec.id, rec.primary_resource_id(), rec.actor_id,
        "预约单 [" + rec.id + "] 审批" + (approve ? "通过" : "驳回") + " (操作人: " + approver_id + ")",
        JsonValue::object({{"approver_id", approver_id}, {"approved", approve}})
    });

    if (!approve) {
        try_promote_waitlist_locked(now);
    }

    out.ok = true;
    out.message = approve ? "审批已通过" : "审批已驳回";
    out.record = rec;
    return out;
}

BookingOutcome AppointmentEngine::check_in(
    const std::string& booking_id,
    const std::string& actor_id,
    std::optional<EpochMinute> now_opt)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    BookingOutcome out;
    auto b_opt = storage_->get_booking(booking_id);
    if (!b_opt) {
        out.error_code = "BOOKING_NOT_FOUND";
        out.message = "预约单不存在";
        return out;
    }

    BookingRecord rec = *b_opt;
    if (rec.status != BookingState::CONFIRMED) {
        out.error_code = "INVALID_STATE_FOR_CHECKIN";
        out.message = "仅已确认(CONFIRMED)的预约可签到 (当前: " + to_string(rec.status) + ")";
        return out;
    }

    Actor actor = storage_->get_actor(actor_id).value_or(Actor{actor_id, actor_id, {}, 100, {}});
    if (!actor.has_role("admin") && rec.actor_id != actor_id) {
        out.error_code = "PERMISSION_DENIED";
        out.message = "仅本人或管理员可执行签到";
        return out;
    }

    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    CheckInContext ctx{rec, actor, now, {}};

    for (const auto& brick : bricks_) {
        BrickVerdict v = brick->on_pre_check_in(ctx);
        if (!v.ok) {
            out.ok = false;
            out.error_code = v.error_code;
            out.message = v.note;
            return out;
        }
    }

    rec.status = BookingState::CHECKED_IN;
    rec.checked_in_at = now;
    rec.updated_at = now;
    rec.decision_trace.push_back({"CheckIn", "Workflow", true, "PASS", "于 " + time_utils::format_datetime(now) + " 成功签到", 0.0});
    storage_->save_booking(rec);

    event_bus_.publish({
        "booking_checked_in", now, rec.id, rec.primary_resource_id(), rec.actor_id,
        "用户签到成功 [" + rec.id + "]", JsonValue::object()
    });

    out.ok = true;
    out.message = "签到成功";
    out.record = rec;
    return out;
}

BookingOutcome AppointmentEngine::complete_booking(
    const std::string& booking_id,
    std::optional<EpochMinute> now_opt)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    BookingOutcome out;
    auto b_opt = storage_->get_booking(booking_id);
    if (!b_opt) {
        out.error_code = "BOOKING_NOT_FOUND";
        out.message = "预约单不存在";
        return out;
    }

    BookingRecord rec = *b_opt;
    if (rec.status != BookingState::CHECKED_IN && rec.status != BookingState::CONFIRMED) {
        out.error_code = "INVALID_STATE_FOR_COMPLETE";
        out.message = "当前状态不可标记完结";
        return out;
    }

    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    rec.status = BookingState::COMPLETED;
    rec.updated_at = now;

    int reward = 0;
    for (const auto& b : bricks_) reward += b->complete_credit_reward();
    if (reward > 0) {
        if (auto act_opt = storage_->get_actor(rec.actor_id)) {
            Actor act = *act_opt;
            act.credit_score = std::min(150, act.credit_score + reward);
            storage_->upsert_actor(act);
        }
    }

    storage_->save_booking(rec);
    event_bus_.publish({
        "booking_completed", now, rec.id, rec.primary_resource_id(), rec.actor_id,
        "预约履约完结 [" + rec.id + "]" + (reward > 0 ? "，奖励信用分 +" + std::to_string(reward) : ""),
        JsonValue::object({{"credit_reward", reward}})
    });

    out.ok = true;
    out.message = "履约完结";
    out.record = rec;
    return out;
}

std::vector<std::string> AppointmentEngine::try_promote_waitlist_locked(EpochMinute now) {
    bool should_promote = false;
    bool global_excl = false;
    for (const auto& b : bricks_) {
        if (b->auto_promote_waitlist()) should_promote = true;
        if (b->is_exclusive_mode()) global_excl = true;
    }
    if (!should_promote) return {};

    auto all = storage_->list_bookings();
    std::vector<BookingRecord> candidates;
    for (const auto& b : all) {
        if (b.status == BookingState::WAITLISTED && b.end_time > now) {
            candidates.push_back(b);
        }
    }
    if (candidates.empty()) return {};

    // Sort by highest priority first, then earliest created_at (FIFO tie-breaker)
    std::sort(candidates.begin(), candidates.end(), [](const BookingRecord& a, const BookingRecord& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.created_at != b.created_at) return a.created_at < b.created_at;
        return a.id < b.id;
    });

    std::vector<std::string> promoted_ids;
    for (auto& cand : candidates) {
        // Re-fetch all bookings so multi-promotions respect newly claimed capacity
        auto latest_all = storage_->list_bookings();
        bool fits = true;

        // Check actor self-overlap
        if (TimelineIndex::has_actor_overlap(latest_all, cand.actor_id, cand.start_time, cand.end_time, cand.id)) {
            fits = false;
        }

        // Check every bundled resource status & peak capacity
        if (fits) {
            for (size_t i = 0; i < cand.resource_ids.size(); ++i) {
                auto r_opt = storage_->get_resource(cand.resource_ids[i]);
                if (!r_opt || r_opt->status != ResourceStatus::ACTIVE) {
                    fits = false;
                    break;
                }
                int req_u = (i == 0) ? cand.units : 1;
                bool is_excl = r_opt->attributes.count("exclusive")
                    ? meta_get_bool(r_opt->attributes, "exclusive", global_excl)
                    : global_excl;
                int used = TimelineIndex::peak_occupied_units(
                    latest_all, r_opt->id, cand.effective_start, cand.effective_end, cand.id);
                if (is_excl ? (used > 0) : (used + req_u > r_opt->capacity)) {
                    fits = false;
                    break;
                }
            }
        }

        if (fits) {
            cand.status = BookingState::CONFIRMED;
            cand.updated_at = now;
            cand.decision_trace.push_back({
                "WaitlistAutoPromoter", "Capacity", true, "PROMOTED",
                "席位释放，按优先级(" + std::to_string(cand.priority) + ")自动从候补递补转正", 0.0
            });
            storage_->save_booking(cand);
            promoted_ids.push_back(cand.id);

            event_bus_.publish({
                "waitlist_promoted", now, cand.id, cand.primary_resource_id(), cand.actor_id,
                "候补自动递补成功！订单 [" + cand.id + "] (用户 " + cand.actor_id + ", 优先级 " + std::to_string(cand.priority) + ") 已转正为 CONFIRMED",
                JsonValue::object({{"priority", cand.priority}})
            });
        }
    }

    return promoted_ids;
}

std::vector<std::string> AppointmentEngine::sweep_no_shows(EpochMinute now) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::optional<int> grace;
    int credit_deduct = 0;
    for (const auto& b : bricks_) {
        if (auto g = b->no_show_grace_minutes()) grace = g;
        credit_deduct += b->no_show_credit_deduction();
    }
    if (!grace) return {};

    auto all = storage_->list_bookings();
    std::vector<std::string> no_show_ids;

    for (auto& rec : all) {
        if (rec.status == BookingState::CONFIRMED && now > rec.start_time + *grace) {
            rec.status = BookingState::NO_SHOW;
            rec.updated_at = now;
            rec.decision_trace.push_back({
                "NoShowSweeper", "Workflow", true, "NO_SHOW",
                "开场后超过 " + std::to_string(*grace) + " 分钟未签到，自动判定爽约并释放资源" +
                (credit_deduct > 0 ? " (扣减信用分 -" + std::to_string(credit_deduct) + ")" : ""),
                0.0
            });
            storage_->save_booking(rec);
            no_show_ids.push_back(rec.id);

            if (credit_deduct > 0) {
                if (auto act_opt = storage_->get_actor(rec.actor_id)) {
                    Actor act = *act_opt;
                    act.credit_score = std::max(0, act.credit_score - credit_deduct);
                    storage_->upsert_actor(act);
                }
            }

            event_bus_.publish({
                "booking_no_show", now, rec.id, rec.primary_resource_id(), rec.actor_id,
                "超时未签到自动释放 [" + rec.id + "]，用户 " + rec.actor_id + " 扣除信用分 " + std::to_string(credit_deduct),
                JsonValue::object({{"credit_deducted", credit_deduct}})
            });
        }
    }

    if (!no_show_ids.empty()) {
        try_promote_waitlist_locked(now);
    }
    return no_show_ids;
}

FaultReportResult AppointmentEngine::report_resource_fault(
    const std::string& resource_id,
    std::optional<EpochMinute> now_opt,
    const std::string& reason)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    FaultReportResult report;
    report.faulted_resource_id = resource_id;

    auto res_opt = storage_->get_resource(resource_id);
    if (!res_opt) return report;

    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    Resource faulted = *res_opt;
    faulted.status = ResourceStatus::FAULT;
    faulted.attributes["fault_reason"] = reason;
    storage_->upsert_resource(faulted);

    auto all_resources = storage_->list_resources();
    auto all_bookings = storage_->list_bookings();

    for (auto& rec : all_bookings) {
        if (!occupies_capacity(rec.status)) continue;
        if (rec.end_time <= now) continue;

        auto it_rid = std::find(rec.resource_ids.begin(), rec.resource_ids.end(), resource_id);
        if (it_rid == rec.resource_ids.end()) continue;

        bool is_primary = (it_rid == rec.resource_ids.begin());
        int needed_units = is_primary ? rec.units : 1;

        std::string replacement_id;
        if (active_bp_.fault_policy.auto_reallocate) {
            auto latest_bookings = storage_->list_bookings();
            for (const auto& cand : all_resources) {
                if (cand.id == resource_id || cand.status != ResourceStatus::ACTIVE) continue;
                if (cand.kind != faulted.kind || cand.capacity < needed_units) continue;
                if (std::find(rec.resource_ids.begin(), rec.resource_ids.end(), cand.id) != rec.resource_ids.end()) continue;

                if (active_bp_.fault_policy.match_same_tags) {
                    bool tags_ok = true;
                    for (const auto& t : faulted.tags) {
                        if (!cand.has_tag(t)) { tags_ok = false; break; }
                    }
                    if (!tags_ok) continue;
                }

                bool global_excl = false;
                for (const auto& b : bricks_) if (b->is_exclusive_mode()) global_excl = true;
                bool is_excl = cand.attributes.count("exclusive")
                    ? meta_get_bool(cand.attributes, "exclusive", global_excl)
                    : global_excl;
                int used = TimelineIndex::peak_occupied_units(
                    latest_bookings, cand.id, rec.effective_start, rec.effective_end, rec.id);
                if (is_excl ? (used == 0) : (used + needed_units <= cand.capacity)) {
                    replacement_id = cand.id;
                    break;
                }
            }
        }

        if (!replacement_id.empty()) {
            *it_rid = replacement_id;
            rec.reallocated_from = resource_id;
            rec.updated_at = now;
            rec.decision_trace.push_back({
                "FaultSmartReallocator", "Capacity", true, "REALLOCATED",
                "原资源 [" + resource_id + "] 故障(" + reason + ")，系统已自动无感迁移至同规格资源 [" + replacement_id + "]",
                0.0
            });
            storage_->save_booking(rec);
            report.reallocated.emplace_back(rec.id, replacement_id);

            event_bus_.publish({
                "resource_fault_reallocated", now, rec.id, replacement_id, rec.actor_id,
                "故障自动换座成功：订单 [" + rec.id + "] 已从故障资源 [" + resource_id + "] 自动迁移至 [" + replacement_id + "]",
                JsonValue::object({{"old_resource", resource_id}, {"new_resource", replacement_id}})
            });
        } else {
            rec.status = BookingState::FAULT_DISPLACED;
            rec.refund_amount = rec.price;
            rec.penalty_fee = 0.0;
            rec.priority += active_bp_.fault_policy.compensate_priority;
            rec.cancel_reason = "资源故障且无同规格空闲替代资源: " + reason;
            rec.updated_at = now;
            rec.decision_trace.push_back({
                "FaultSmartReallocator", "Capacity", false, "FAULT_DISPLACED",
                "原资源 [" + resource_id + "] 故障，暂无可用替代席位，已全额退款并补偿优先级 +" + std::to_string(active_bp_.fault_policy.compensate_priority),
                0.0
            });
            storage_->save_booking(rec);
            report.displaced.push_back(rec.id);

            event_bus_.publish({
                "resource_fault_displaced", now, rec.id, resource_id, rec.actor_id,
                "资源 [" + resource_id + "] 故障停用，订单 [" + rec.id + "] 已全额免责退回",
                JsonValue::object({{"reason", reason}})
            });
        }
    }

    event_bus_.publish({
        "resource_fault", now, "", resource_id, "",
        "资源 [" + faulted.name + "] 标记故障 (" + reason + ")：自动迁移 " +
        std::to_string(report.reallocated.size()) + " 单，全额退赔 " + std::to_string(report.displaced.size()) + " 单",
        report.to_json()
    });

    return report;
}

bool AppointmentEngine::restore_resource(const std::string& resource_id, std::optional<EpochMinute> now_opt) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto res_opt = storage_->get_resource(resource_id);
    if (!res_opt) return false;

    Resource r = *res_opt;
    r.status = ResourceStatus::ACTIVE;
    r.attributes.erase("fault_reason");
    storage_->upsert_resource(r);

    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    auto promoted = try_promote_waitlist_locked(now);

    event_bus_.publish({
        "resource_restored", now, "", resource_id, "",
        "资源 [" + r.name + "] 已恢复正常服务，自动递补候补单 " + std::to_string(promoted.size()) + " 笔",
        JsonValue::object({{"promoted_count", promoted.size()}})
    });
    return true;
}

std::vector<SlotSuggestion> AppointmentEngine::suggest_slots(
    const std::string& actor_id,
    const std::string& date_str,
    int duration_minutes,
    int units,
    const std::set<std::string>& required_tags,
    const std::string& kind_filter,
    size_t max_results,
    std::optional<EpochMinute> now_opt)
{
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::vector<SlotSuggestion> results;
    auto day_opt = time_utils::parse_datetime(date_str + " 00:00");
    if (!day_opt || duration_minutes <= 0) return results;

    EpochMinute day_start = *day_opt;
    EpochMinute now = now_opt.value_or(time_utils::now_local_minutes());
    auto resources = storage_->list_resources();
    auto bookings = storage_->list_bookings();

    // Determine step granularity from SlotGranularityBrick if present (default 30 mins)
    int step = 30;
    for (const auto& b : bricks_) {
        if (b->type_id() == "SlotGranularity") {
            auto d = b->describe();
            step = std::max(15, static_cast<int>(d.current_config["align_minutes"].as_int(30)));
            break;
        }
    }

    for (const auto& res : resources) {
        if (res.status != ResourceStatus::ACTIVE) continue;
        if (!kind_filter.empty() && res.kind != kind_filter) continue;
        if (res.capacity < units) continue;

        bool tags_ok = true;
        for (const auto& t : required_tags) {
            if (!res.has_tag(t)) { tags_ok = false; break; }
        }
        if (!tags_ok) continue;

        for (int m = 0; m + duration_minutes <= 1440; m += step) {
            EpochMinute st = day_start + m;
            EpochMinute et = st + duration_minutes;
            if (st < now) continue;

            BookingRequest probe;
            probe.actor_id = actor_id;
            probe.resource_ids = {res.id};
            probe.start_time = st;
            probe.end_time = et;
            probe.units = units;
            probe.allow_waitlist = false;
            probe.now_override = now;

            BookingOutcome dry = evaluate_and_book_locked(probe, /*dry_run=*/true);
            if (!dry.ok) continue;

            int used = TimelineIndex::peak_occupied_units(
                bookings, res.id, dry.record.effective_start, dry.record.effective_end);
            int avail = std::max(0, res.capacity - used);
            if (avail < units) continue;

            // Score prefers tighter capacity match (less waste), lower price, and prime daytime hours
            double util_score = static_cast<double>(units) / static_cast<double>(std::max(1, res.capacity)) * 40.0;
            double price_score = std::max(0.0, 30.0 - dry.record.price * 0.1);
            double hour_score = (m >= 9 * 60 && m <= 17 * 60) ? 20.0 : 10.0;
            double total_score = std::round((util_score + price_score + hour_score) * 10.0) / 10.0;

            results.push_back({
                res.id,
                res.name,
                st,
                et,
                avail,
                dry.record.price,
                total_score,
                "余量 " + std::to_string(avail) + "/" + std::to_string(res.capacity) +
                (dry.record.post_buffer_minutes > 0 ? " (含后置缓冲 " + std::to_string(dry.record.post_buffer_minutes) + "m)" : "")
            });
        }
    }

    std::sort(results.begin(), results.end(), [](const SlotSuggestion& a, const SlotSuggestion& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.start_time < b.start_time;
    });

    if (results.size() > max_results) results.resize(max_results);
    return results;
}

std::optional<BookingRecord> AppointmentEngine::get_booking(const std::string& id) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return storage_->get_booking(id);
}

std::vector<BookingRecord> AppointmentEngine::list_bookings() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    return storage_->list_bookings();
}

std::vector<std::string> AppointmentEngine::recent_lua_logs(size_t limit) const {
    if (!lua_vm_) return {};
    return lua_vm_->recent_logs(limit);
}

JsonValue AppointmentEngine::stats_summary() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    auto resources = storage_->list_resources();
    auto actors = storage_->list_actors();
    auto bookings = storage_->list_bookings();

    int active_res = 0, fault_res = 0;
    for (const auto& r : resources) {
        if (r.status == ResourceStatus::ACTIVE) ++active_res;
        else if (r.status == ResourceStatus::FAULT) ++fault_res;
    }

    std::map<std::string, int> by_status;
    double revenue = 0.0;
    for (const auto& b : bookings) {
        by_status[to_string(b.status)]++;
        if (b.status == BookingState::CONFIRMED || b.status == BookingState::CHECKED_IN || b.status == BookingState::COMPLETED) {
            revenue += b.price;
        } else if (b.status == BookingState::CANCELLED) {
            revenue += b.penalty_fee;
        }
    }

    JsonValue st_obj = JsonValue::object();
    for (const auto& [k, v] : by_status) st_obj[k] = v;

    return JsonValue::object({
        {"blueprint_id", active_bp_.id},
        {"blueprint_name", active_bp_.name},
        {"blueprint_description", active_bp_.description},
        {"storage_backend", storage_->backend_name()},
        {"bricks_count", bricks_.size()},
        {"resources_total", resources.size()},
        {"resources_active", active_res},
        {"resources_fault", fault_res},
        {"actors_total", actors.size()},
        {"bookings_total", bookings.size()},
        {"bookings_by_status", std::move(st_obj)},
        {"net_revenue", revenue},
        {"currency", active_bp_.currency}
    });
}

void AppointmentEngine::set_metadata(const std::string& id, const std::string& name, const std::string& desc) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    active_bp_.id = id;
    active_bp_.name = name;
    active_bp_.description = desc;
}

void AppointmentEngine::set_fault_policy(const FaultPolicy& fp) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    active_bp_.fault_policy = fp;
}

// ============================================================================
// EngineBuilder
// ============================================================================
EngineBuilder::EngineBuilder() = default;

EngineBuilder& EngineBuilder::with_storage(std::shared_ptr<IStorage> storage) {
    storage_ = std::move(storage);
    return *this;
}

EngineBuilder& EngineBuilder::with_memory_storage() {
    storage_ = std::make_shared<MemoryStorage>();
    return *this;
}

EngineBuilder& EngineBuilder::with_sqlite_storage(const std::string& db_path) {
    storage_ = std::make_shared<SqliteStorage>(db_path);
    return *this;
}

EngineBuilder& EngineBuilder::with_name(const std::string& id, const std::string& name, const std::string& desc) {
    sys_id_ = id;
    sys_name_ = name;
    sys_desc_ = desc;
    return *this;
}

EngineBuilder& EngineBuilder::with_fault_policy(const FaultPolicy& fp) {
    fault_policy_ = fp;
    return *this;
}

EngineBuilder& EngineBuilder::add_brick(BrickPtr brick) {
    if (brick) bricks_.push_back(std::move(brick));
    return *this;
}

EngineBuilder& EngineBuilder::add_brick_by_name(const std::string& type_id, const JsonValue& cfg) {
    auto b = BrickRegistry::instance().create(type_id, cfg);
    if (b) bricks_.push_back(std::move(b));
    return *this;
}

EngineBuilder& EngineBuilder::with_lua_blueprint(const std::string& blueprint_path, bool load_seed) {
    blueprint_path_ = blueprint_path;
    load_seed_ = load_seed;
    return *this;
}

std::unique_ptr<AppointmentEngine> EngineBuilder::build() {
    auto eng = std::make_unique<AppointmentEngine>(storage_);
    eng->set_metadata(sys_id_, sys_name_, sys_desc_);
    eng->set_fault_policy(fault_policy_);
    if (!blueprint_path_.empty()) {
        eng->load_blueprint_file(blueprint_path_, load_seed_);
    }
    for (const auto& b : bricks_) {
        eng->add_brick(b);
    }
    return eng;
}

} // namespace chronokit
