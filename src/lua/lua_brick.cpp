#include "chronokit/lua/lua_brick.hpp"
#include "chronokit/core/timeline_index.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cmath>

namespace chronokit {

namespace {

struct StackGuard {
    lua_State* L;
    int top;
    explicit StackGuard(lua_State* s) : L(s), top(lua_gettop(s)) {}
    ~StackGuard() { lua_settop(L, top); }
};

void lua_brick_quota_hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "LuaPolicy brick exceeded 500,000 instructions quota");
}

// Helper methods attached to `ctx` table in Lua:
// ctx:add_note("...")
int l_ctx_add_note(lua_State* L) {
    // arg 1 = ctx table, arg 2 = string note
    if (!lua_istable(L, 1)) return 0;
    const char* s = luaL_optstring(L, 2, "");
    lua_getfield(L, 1, "_notes");
    if (lua_istable(L, -1)) {
        lua_Integer n = luaL_len(L, -1);
        lua_pushstring(L, s);
        lua_rawseti(L, -2, n + 1);
    }
    lua_pop(L, 1);
    return 0;
}

// ctx:reject("ERR_CODE", "reason")
int l_ctx_reject(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    const char* code = luaL_optstring(L, 2, "LUA_POLICY_REJECTED");
    const char* msg = luaL_optstring(L, 3, "Rejected by Lua policy");
    lua_pushboolean(L, 1);
    lua_setfield(L, 1, "_rejected");
    lua_pushstring(L, code);
    lua_setfield(L, 1, "_reject_code");
    lua_pushstring(L, msg);
    lua_setfield(L, 1, "_reject_msg");
    lua_pushboolean(L, 0);
    return 1;
}

// ctx:actor_has_role("vip")
int l_ctx_actor_has_role(lua_State* L) {
    if (!lua_istable(L, 1)) { lua_pushboolean(L, 0); return 1; }
    const char* role = luaL_checkstring(L, 2);
    lua_getfield(L, 1, "actor");
    bool has = false;
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "role_map");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, role);
            has = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

// ctx:resource_has_tag("gpu")
int l_ctx_resource_has_tag(lua_State* L) {
    if (!lua_istable(L, 1)) { lua_pushboolean(L, 0); return 1; }
    const char* tag = luaL_checkstring(L, 2);
    lua_getfield(L, 1, "resource");
    bool has = false;
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "tag_map");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, tag);
            has = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

// ctx:actor_completed_kind("safety_training") -> boolean
int l_ctx_actor_completed_kind(lua_State* L) {
    if (!lua_istable(L, 1)) { lua_pushboolean(L, 0); return 1; }
    const char* kind = luaL_checkstring(L, 2);
    lua_getfield(L, 1, "_completed_kinds");
    bool has = false;
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, kind);
        has = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    lua_pushboolean(L, has ? 1 : 0);
    return 1;
}

// ctx:apply_discount(amount, note)
int l_ctx_apply_discount(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    double amt = luaL_checknumber(L, 2);
    const char* note = luaL_optstring(L, 3, "");
    lua_getfield(L, 1, "price");
    double cur = lua_tonumber(L, -1);
    lua_pop(L, 1);
    double next_p = std::max(0.0, cur - amt);
    lua_pushnumber(L, next_p);
    lua_setfield(L, 1, "price");

    lua_getfield(L, 1, "discount_amount");
    double cur_d = lua_tonumber(L, -1);
    lua_pop(L, 1);
    lua_pushnumber(L, cur_d + (cur - next_p));
    lua_setfield(L, 1, "discount_amount");

    if (note && note[0] != '\0') {
        lua_getfield(L, 1, "_notes");
        if (lua_istable(L, -1)) {
            lua_Integer n = luaL_len(L, -1);
            lua_pushstring(L, note);
            lua_rawseti(L, -2, n + 1);
        }
        lua_pop(L, 1);
    }
    return 0;
}

void push_actor_lua(lua_State* L, const Actor& actor) {
    LuaVM::json_to_lua(L, actor.to_json());
    // Also attach `role_map = { student = true, ... }` for O(1) Lua lookup
    lua_createtable(L, 0, static_cast<int>(actor.roles.size()));
    for (const auto& r : actor.roles) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, r.c_str());
    }
    lua_setfield(L, -2, "role_map");
}

void push_resource_lua(lua_State* L, const Resource& res) {
    LuaVM::json_to_lua(L, res.to_json());
    lua_createtable(L, 0, static_cast<int>(res.tags.size()));
    for (const auto& t : res.tags) {
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, t.c_str());
    }
    lua_setfield(L, -2, "tag_map");
}

} // namespace

LuaScriptBrick::LuaScriptBrick(
    std::shared_ptr<LuaVM> vm,
    std::string custom_name,
    std::string summary,
    int order_val,
    int pre_book_ref,
    int pre_cancel_ref,
    int pre_checkin_ref,
    JsonValue extra_cfg)
    : vm_(std::move(vm))
    , custom_name_(std::move(custom_name))
    , summary_(std::move(summary))
    , order_(order_val)
    , pre_book_ref_(pre_book_ref)
    , pre_cancel_ref_(pre_cancel_ref)
    , pre_checkin_ref_(pre_checkin_ref)
    , extra_cfg_(std::move(extra_cfg))
{}

LuaScriptBrick::~LuaScriptBrick() {
    if (vm_ && vm_->state()) {
        std::lock_guard<std::recursive_mutex> lk(vm_->mutex());
        lua_State* L = vm_->state();
        if (pre_book_ref_ != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, pre_book_ref_);
        if (pre_cancel_ref_ != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, pre_cancel_ref_);
        if (pre_checkin_ref_ != LUA_NOREF) luaL_unref(L, LUA_REGISTRYINDEX, pre_checkin_ref_);
    }
}

BrickDescriptor LuaScriptBrick::describe() const {
    JsonValue hooks = JsonValue::array();
    if (pre_book_ref_ != LUA_NOREF) hooks.push_back("on_pre_book");
    if (pre_cancel_ref_ != LUA_NOREF) hooks.push_back("on_pre_cancel");
    if (pre_checkin_ref_ != LUA_NOREF) hooks.push_back("on_pre_check_in");

    JsonValue cfg = extra_cfg_;
    cfg["active_hooks"] = hooks;
    return {
        "LuaPolicy:" + custom_name_,
        category(),
        summary_,
        order_,
        {
            {"name", "string", custom_name_, "Lua 自定义策略积木名称"},
            {"order", "int", order_, "流水线执行阶段序号"}
        },
        cfg
    };
}

BrickVerdict LuaScriptBrick::on_pre_book(BookingContext& ctx) {
    if (pre_book_ref_ == LUA_NOREF || !vm_ || !vm_->state()) {
        return BrickVerdict::pass();
    }

    std::lock_guard<std::recursive_mutex> lk(vm_->mutex());
    lua_State* L = vm_->state();
    StackGuard guard(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pre_book_ref_);
    if (!lua_isfunction(L, -1)) return BrickVerdict::pass();

    // Build `ctx` table
    lua_createtable(L, 0, 24);
    int ctx_idx = lua_gettop(L);

    push_actor_lua(L, ctx.actor);
    lua_setfield(L, ctx_idx, "actor");

    push_resource_lua(L, ctx.primary_resource());
    lua_setfield(L, ctx_idx, "resource");

    lua_createtable(L, static_cast<int>(ctx.resources.size()), 0);
    for (size_t i = 0; i < ctx.resources.size(); ++i) {
        push_resource_lua(L, ctx.resources[i]);
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    lua_setfield(L, ctx_idx, "resources");

    int s_mod = time_utils::minute_of_day(ctx.request.start_time);
    lua_pushinteger(L, ctx.request.units);
    lua_setfield(L, ctx_idx, "units");
    lua_pushinteger(L, ctx.duration_minutes());
    lua_setfield(L, ctx_idx, "duration_minutes");
    lua_pushinteger(L, s_mod);
    lua_setfield(L, ctx_idx, "start_minute_of_day");
    lua_pushinteger(L, s_mod + ctx.duration_minutes());
    lua_setfield(L, ctx_idx, "end_minute_of_day");
    lua_pushinteger(L, s_mod / 60);
    lua_setfield(L, ctx_idx, "start_hour");
    lua_pushinteger(L, time_utils::weekday_iso(ctx.request.start_time));
    lua_setfield(L, ctx_idx, "weekday");
    lua_pushinteger(L, ctx.request.start_time - ctx.now);
    lua_setfield(L, ctx_idx, "lead_minutes");

    std::string st_s = time_utils::format_datetime(ctx.request.start_time);
    std::string et_s = time_utils::format_datetime(ctx.request.end_time);
    std::string dt_s = time_utils::format_date(ctx.request.start_time);
    lua_pushstring(L, st_s.c_str());
    lua_setfield(L, ctx_idx, "start_time");
    lua_pushstring(L, et_s.c_str());
    lua_setfield(L, ctx_idx, "end_time");
    lua_pushstring(L, dt_s.c_str());
    lua_setfield(L, ctx_idx, "date");

    // Mutable fields
    lua_pushnumber(L, ctx.price);
    lua_setfield(L, ctx_idx, "price");
    lua_pushnumber(L, ctx.discount_amount);
    lua_setfield(L, ctx_idx, "discount_amount");
    lua_pushnumber(L, ctx.deposit);
    lua_setfield(L, ctx_idx, "deposit");
    lua_pushinteger(L, ctx.priority);
    lua_setfield(L, ctx_idx, "priority");
    lua_pushinteger(L, ctx.pre_buffer_minutes);
    lua_setfield(L, ctx_idx, "pre_buffer_minutes");
    lua_pushinteger(L, ctx.post_buffer_minutes);
    lua_setfield(L, ctx_idx, "post_buffer_minutes");
    lua_pushboolean(L, ctx.target_state == BookingState::PENDING_APPROVAL ? 1 : 0);
    lua_setfield(L, ctx_idx, "require_approval");

    // Precompute rich live host state queries for Lua policies!
    std::map<std::string, std::string> id_to_kind;
    int total_kind_cap = 0, used_kind_cap = 0;
    for (const auto& r : ctx.all_resources) {
        id_to_kind[r.id] = r.kind;
        if (r.status == ResourceStatus::ACTIVE && r.kind == ctx.primary_resource().kind) {
            total_kind_cap += r.capacity;
            used_kind_cap += std::min(r.capacity, TimelineIndex::peak_occupied_units(
                ctx.existing_bookings, r.id, ctx.effective_start, ctx.effective_end, ctx.exclude_booking_id));
        }
    }
    double occ_ratio = total_kind_cap > 0 ? static_cast<double>(used_kind_cap) / static_cast<double>(total_kind_cap) : 0.0;
    lua_pushnumber(L, occ_ratio);
    lua_setfield(L, ctx_idx, "kind_occupancy_ratio");

    int completed_cnt = 0;
    int daily_cancels = 0;
    EpochMinute today_s = time_utils::start_of_day(ctx.now);
    lua_createtable(L, 0, 4);
    for (const auto& b : ctx.existing_bookings) {
        if (b.actor_id != ctx.actor.id) continue;
        if (b.status == BookingState::COMPLETED) {
            ++completed_cnt;
            std::string k = meta_get_string(b.metadata, "_primary_kind");
            if (k.empty() && !b.primary_resource_id().empty()) k = id_to_kind[b.primary_resource_id()];
            if (!k.empty()) {
                lua_pushboolean(L, 1);
                lua_setfield(L, -2, k.c_str());
            }
        } else if (b.status == BookingState::CANCELLED && time_utils::start_of_day(b.updated_at) == today_s) {
            ++daily_cancels;
        }
    }
    lua_setfield(L, ctx_idx, "_completed_kinds");
    lua_pushinteger(L, completed_cnt);
    lua_setfield(L, ctx_idx, "actor_completed_count");
    lua_pushinteger(L, daily_cancels);
    lua_setfield(L, ctx_idx, "actor_daily_cancels");

    LuaVM::json_to_lua(L, meta_to_json(ctx.working_metadata));
    lua_setfield(L, ctx_idx, "metadata");

    LuaVM::json_to_lua(L, meta_to_json(ctx.env_context));
    lua_setfield(L, ctx_idx, "env");

    lua_createtable(L, 0, 0);
    lua_setfield(L, ctx_idx, "_notes");

    // Helper methods directly on ctx
    lua_pushcfunction(L, l_ctx_add_note);
    lua_setfield(L, ctx_idx, "add_note");
    lua_pushcfunction(L, l_ctx_reject);
    lua_setfield(L, ctx_idx, "reject");
    lua_pushcfunction(L, l_ctx_actor_has_role);
    lua_setfield(L, ctx_idx, "actor_has_role");
    lua_pushcfunction(L, l_ctx_resource_has_tag);
    lua_setfield(L, ctx_idx, "resource_has_tag");
    lua_pushcfunction(L, l_ctx_actor_completed_kind);
    lua_setfield(L, ctx_idx, "actor_completed_kind");
    lua_pushcfunction(L, l_ctx_apply_discount);
    lua_setfield(L, ctx_idx, "apply_discount");

    // Keep copy of ctx reference on stack so we can inspect mutations after pcall!
    lua_pushvalue(L, ctx_idx); // Stack: [fn, ctx, ctx] -> wait: pcall pops fn + args!
    // Reorder: we want [ctx_saved, fn, ctx_arg]
    lua_insert(L, ctx_idx - 1); // Stack: [ctx_saved, fn, ctx]
    int saved_ctx = ctx_idx - 1;

    lua_sethook(L, lua_brick_quota_hook, LUA_MASKCOUNT, 500000);
    if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_sethook(L, nullptr, 0, 0);
        return BrickVerdict::reject("LUA_RUNTIME_ERROR", "Lua策略积木 [" + custom_name_ + "] 执行异常: " + err);
    }
    lua_sethook(L, nullptr, 0, 0);

    int ret1 = lua_gettop(L) - 1;
    int ret2 = lua_gettop(L);

    // Check explicit `ctx:reject(...)`
    lua_getfield(L, saved_ctx, "_rejected");
    bool ctx_rejected = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);

    if (ctx_rejected) {
        lua_getfield(L, saved_ctx, "_reject_code");
        std::string r_code = luaL_optstring(L, -1, "LUA_POLICY_REJECTED");
        lua_pop(L, 1);
        lua_getfield(L, saved_ctx, "_reject_msg");
        std::string r_msg = luaL_optstring(L, -1, "被 Lua 策略拦截");
        lua_pop(L, 1);
        return BrickVerdict::reject(r_code, r_msg);
    }

    if (lua_isboolean(L, ret1) && lua_toboolean(L, ret1) == 0) {
        std::string msg = lua_isstring(L, ret2) ? lua_tostring(L, ret2) : ("未通过策略积木 [" + custom_name_ + "]");
        return BrickVerdict::reject("LUA_POLICY_REJECTED", msg);
    }

    // Read back mutations from `saved_ctx`
    bool mutated = false;
    double orig_price = ctx.price;
    double orig_dep = ctx.deposit;
    int orig_prio = ctx.priority;

    lua_getfield(L, saved_ctx, "price");
    if (lua_isnumber(L, -1)) ctx.price = std::round(lua_tonumber(L, -1) * 100.0) / 100.0;
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "discount_amount");
    if (lua_isnumber(L, -1)) ctx.discount_amount = std::round(lua_tonumber(L, -1) * 100.0) / 100.0;
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "deposit");
    if (lua_isnumber(L, -1)) ctx.deposit = std::round(lua_tonumber(L, -1) * 100.0) / 100.0;
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "priority");
    if (lua_isinteger(L, -1)) ctx.priority = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "pre_buffer_minutes");
    if (lua_isinteger(L, -1)) {
        int nb = static_cast<int>(lua_tointeger(L, -1));
        if (nb != ctx.pre_buffer_minutes) {
            ctx.pre_buffer_minutes = nb;
            ctx.effective_start = ctx.request.start_time - nb;
            mutated = true;
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "post_buffer_minutes");
    if (lua_isinteger(L, -1)) {
        int nb = static_cast<int>(lua_tointeger(L, -1));
        if (nb != ctx.post_buffer_minutes) {
            ctx.post_buffer_minutes = nb;
            ctx.effective_end = ctx.request.end_time + nb;
            mutated = true;
        }
    }
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "require_approval");
    if (lua_toboolean(L, -1) != 0 && ctx.target_state == BookingState::CONFIRMED) {
        ctx.target_state = BookingState::PENDING_APPROVAL;
        mutated = true;
    }
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "metadata");
    if (lua_istable(L, -1)) {
        auto m_json = LuaVM::lua_to_json(L, -1);
        for (const auto& [k, v] : json_to_meta(m_json)) {
            ctx.working_metadata[k] = v;
        }
    }
    lua_pop(L, 1);

    if (std::abs(ctx.price - orig_price) > 1e-6 || std::abs(ctx.deposit - orig_dep) > 1e-6 || ctx.priority != orig_prio) {
        mutated = true;
    }

    // Collect notes
    std::string combined_note;
    lua_getfield(L, saved_ctx, "_notes");
    if (lua_istable(L, -1)) {
        lua_Integer n = luaL_len(L, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_isstring(L, -1)) {
                if (!combined_note.empty()) combined_note += "；";
                combined_note += lua_tostring(L, -1);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);

    if (combined_note.empty()) {
        combined_note = mutated ? "Lua 策略动态调整完成" : "Lua 策略校验通过";
    }
    return mutated ? BrickVerdict::mutated(combined_note) : BrickVerdict::pass(combined_note);
}

BrickVerdict LuaScriptBrick::on_pre_cancel(CancelContext& ctx) {
    if (pre_cancel_ref_ == LUA_NOREF || !vm_ || !vm_->state()) {
        return BrickVerdict::pass();
    }

    std::lock_guard<std::recursive_mutex> lk(vm_->mutex());
    lua_State* L = vm_->state();
    StackGuard guard(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pre_cancel_ref_);
    if (!lua_isfunction(L, -1)) return BrickVerdict::pass();

    lua_createtable(L, 0, 12);
    int ctx_idx = lua_gettop(L);
    push_actor_lua(L, ctx.actor);
    lua_setfield(L, ctx_idx, "actor");
    lua_pushnumber(L, ctx.record.price);
    lua_setfield(L, ctx_idx, "price");
    lua_pushnumber(L, ctx.penalty_fee);
    lua_setfield(L, ctx_idx, "penalty_fee");
    lua_pushnumber(L, ctx.refund_amount);
    lua_setfield(L, ctx_idx, "refund_amount");
    lua_pushinteger(L, ctx.record.start_time - ctx.now);
    lua_setfield(L, ctx_idx, "mins_before_start");
    lua_createtable(L, 0, 0);
    lua_setfield(L, ctx_idx, "_notes");
    lua_pushcfunction(L, l_ctx_add_note);
    lua_setfield(L, ctx_idx, "add_note");
    lua_pushcfunction(L, l_ctx_reject);
    lua_setfield(L, ctx_idx, "reject");

    lua_pushvalue(L, ctx_idx);
    lua_insert(L, ctx_idx - 1);
    int saved_ctx = ctx_idx - 1;

    lua_sethook(L, lua_brick_quota_hook, LUA_MASKCOUNT, 500000);
    if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_sethook(L, nullptr, 0, 0);
        return BrickVerdict::reject("LUA_RUNTIME_ERROR", err);
    }
    lua_sethook(L, nullptr, 0, 0);

    int ret1 = lua_gettop(L) - 1;
    int ret2 = lua_gettop(L);
    if (lua_isboolean(L, ret1) && lua_toboolean(L, ret1) == 0) {
        return BrickVerdict::reject("LUA_CANCEL_REJECTED", lua_isstring(L, ret2) ? lua_tostring(L, ret2) : "退订被 Lua 策略拦截");
    }

    lua_getfield(L, saved_ctx, "penalty_fee");
    if (lua_isnumber(L, -1)) ctx.penalty_fee = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, saved_ctx, "refund_amount");
    if (lua_isnumber(L, -1)) ctx.refund_amount = lua_tonumber(L, -1);
    lua_pop(L, 1);

    return BrickVerdict::pass("Lua 退订策略执行完毕");
}

BrickVerdict LuaScriptBrick::on_pre_check_in(CheckInContext& ctx) {
    if (pre_checkin_ref_ == LUA_NOREF || !vm_ || !vm_->state()) {
        return BrickVerdict::pass();
    }

    std::lock_guard<std::recursive_mutex> lk(vm_->mutex());
    lua_State* L = vm_->state();
    StackGuard guard(L);

    lua_rawgeti(L, LUA_REGISTRYINDEX, pre_checkin_ref_);
    if (!lua_isfunction(L, -1)) return BrickVerdict::pass();

    lua_createtable(L, 0, 8);
    int ctx_idx = lua_gettop(L);
    push_actor_lua(L, ctx.actor);
    lua_setfield(L, ctx_idx, "actor");
    lua_pushinteger(L, ctx.now - ctx.record.start_time);
    lua_setfield(L, ctx_idx, "mins_since_start");

    lua_sethook(L, lua_brick_quota_hook, LUA_MASKCOUNT, 500000);
    if (lua_pcall(L, 1, 2, 0) != LUA_OK) {
        std::string err = lua_tostring(L, -1);
        lua_sethook(L, nullptr, 0, 0);
        return BrickVerdict::reject("LUA_RUNTIME_ERROR", err);
    }
    lua_sethook(L, nullptr, 0, 0);

    int ret1 = lua_gettop(L) - 1;
    int ret2 = lua_gettop(L);
    if (lua_isboolean(L, ret1) && lua_toboolean(L, ret1) == 0) {
        return BrickVerdict::reject("LUA_CHECKIN_REJECTED", lua_isstring(L, ret2) ? lua_tostring(L, ret2) : "签到被 Lua 策略拦截");
    }
    return BrickVerdict::pass("Lua 签到策略通过");
}

} // namespace chronokit
