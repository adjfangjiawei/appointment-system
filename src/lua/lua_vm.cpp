#include "chronokit/lua/lua_vm.hpp"
#include "chronokit/bricks/builtin_bricks.hpp"
#include "chronokit/bricks/v3_bricks.hpp"
#include "chronokit/lua/lua_brick.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace chronokit {

namespace {

struct StackGuard {
    lua_State* L;
    int top;
    explicit StackGuard(lua_State* s) : L(s), top(lua_gettop(s)) {}
    ~StackGuard() { lua_settop(L, top); }
};

void instruction_quota_hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "Lua execution exceeded 500,000 instructions quota (infinite loop guard)");
}

int lua_host_log(lua_State* L) {
    auto* vm = static_cast<LuaVM*>(lua_touserdata(L, lua_upvalueindex(1)));
    int n = lua_gettop(L);
    std::string out;
    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += " ";
        if (s) out.append(s, len);
        lua_pop(L, 1);
    }
    if (vm) vm->append_log(out);
    return 0;
}

bool is_lua_array(lua_State* L, int abs_idx, size_t& max_idx) {
    max_idx = 0;
    size_t count = 0;
    lua_pushnil(L);
    while (lua_next(L, abs_idx) != 0) {
        if (lua_type(L, -2) != LUA_TNUMBER || !lua_isinteger(L, -2)) {
            lua_pop(L, 2);
            return false;
        }
        lua_Integer k = lua_tointeger(L, -2);
        if (k <= 0) {
            lua_pop(L, 2);
            return false;
        }
        if (static_cast<size_t>(k) > max_idx) max_idx = static_cast<size_t>(k);
        ++count;
        lua_pop(L, 1);
    }
    return count == max_idx;
}

} // namespace

LuaVM::LuaVM() {
    L_ = luaL_newstate();
    if (!L_) throw std::runtime_error("Failed to allocate Lua 5.4 state");
    init_sandbox_and_dsl();
}

LuaVM::~LuaVM() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    clear_hook_refs();
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

void LuaVM::clear_hook_refs() {
    if (!L_) return;
    for (const auto& [_, ref] : hook_refs_) {
        if (ref != LUA_NOREF) luaL_unref(L_, LUA_REGISTRYINDEX, ref);
    }
    hook_refs_.clear();
}

void LuaVM::init_sandbox_and_dsl() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    StackGuard guard(L_);

    luaL_requiref(L_, "_G", luaopen_base, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L_, 1);
    luaL_requiref(L_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L_, 1);

    lua_pushnil(L_);
    lua_setglobal(L_, "dofile");
    lua_pushnil(L_);
    lua_setglobal(L_, "loadfile");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, lua_host_log, 1);
    lua_setglobal(L_, "log");

    lua_pushlightuserdata(L_, this);
    lua_pushcclosure(L_, lua_host_log, 1);
    lua_setglobal(L_, "print");

    // Inject ChronoKit v3.0 Declarative Composable DSL (`Blueprint`, `Brick.*`, and `Prefab.*` Reusable Suites)
    const char* prelude = R"LUA(
        Brick = setmetatable({}, {
            __index = function(_, brick_type)
                return function(cfg)
                    cfg = cfg or {}
                    cfg._brick_type = brick_type
                    return cfg
                end
            end
        })

        Prefab = {
            -- 可复用预制件模组 1：防黄牛风控与防连占冷却组合包
            AntiScalperSuite = function(opts)
                opts = opts or {}
                return Brick.AllOf {
                    name = opts.name or "AntiScalperSuite",
                    order = opts.order or 28,
                    bricks = {
                        Brick.AntiScalper {
                            max_cancellations_per_day = opts.max_cancels or 2,
                            max_creates_in_window = opts.max_burst or 4,
                            window_minutes = opts.window_minutes or 60,
                            require_verified_attribute = opts.verified_attr or "",
                        },
                        Brick.CooldownInterval {
                            min_gap_same_resource_minutes = opts.cooldown_minutes or 60,
                            min_gap_same_kind_minutes = opts.kind_cooldown_minutes or 0,
                        }
                    }
                }
            end,

            -- 可复用预制件模组 2：信用准入与履约奖惩套件
            CreditGuardSuite = function(opts)
                opts = opts or {}
                return Brick.AllOf {
                    name = opts.name or "CreditGuardSuite",
                    order = opts.order or 30,
                    bricks = {
                        Brick.RoleAndCreditGate {
                            min_credit_score = opts.min_credit or 65,
                            deduct_on_no_show = opts.deduct or 15,
                            reward_on_complete = opts.reward or 3,
                        },
                        Brick.CancellationAndCheckIn {
                            free_cancel_before_minutes = opts.free_cancel_mins or 60,
                            late_cancel_fee_ratio = opts.cancel_fee or 0.3,
                            check_in_earliest_before_minutes = opts.checkin_before or 30,
                            check_in_grace_after_minutes = opts.grace_mins or 15,
                        }
                    }
                }
            end
        }

        function Blueprint(spec)
            spec = spec or {}
            spec._is_blueprint = true
            return spec
        end
    )LUA";

    if (luaL_dostring(L_, prelude) != LUA_OK) {
        std::string err = lua_tostring(L_, -1);
        throw std::runtime_error("Failed to initialize Lua DSL prelude: " + err);
    }
}

void LuaVM::append_log(const std::string& msg) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    logs_.push_back(msg);
    if (logs_.size() > 200) logs_.pop_front();
}

std::vector<std::string> LuaVM::recent_logs(size_t limit) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    std::vector<std::string> out;
    size_t start = logs_.size() > limit ? logs_.size() - limit : 0;
    for (size_t i = logs_.size(); i > start; --i) {
        out.push_back(logs_[i - 1]);
    }
    return out;
}

JsonValue LuaVM::lua_to_json(lua_State* L, int index) {
    int abs_idx = lua_absindex(L, index);
    int t = lua_type(L, abs_idx);
    switch (t) {
        case LUA_TNIL:
        case LUA_TNONE:
            return JsonValue(nullptr);
        case LUA_TBOOLEAN:
            return JsonValue(lua_toboolean(L, abs_idx) != 0);
        case LUA_TNUMBER:
            if (lua_isinteger(L, abs_idx)) {
                return JsonValue(static_cast<int64_t>(lua_tointeger(L, abs_idx)));
            }
            return JsonValue(static_cast<double>(lua_tonumber(L, abs_idx)));
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, abs_idx, &len);
            return JsonValue(std::string(s ? s : "", len));
        }
        case LUA_TTABLE: {
            lua_pushnil(L);
            if (lua_next(L, abs_idx) == 0) {
                return JsonValue::object();
            }
            lua_pop(L, 2);

            size_t max_idx = 0;
            if (is_lua_array(L, abs_idx, max_idx) && max_idx > 0) {
                JsonValue::Array arr;
                arr.reserve(max_idx);
                for (size_t i = 1; i <= max_idx; ++i) {
                    lua_rawgeti(L, abs_idx, static_cast<lua_Integer>(i));
                    arr.push_back(lua_to_json(L, -1));
                    lua_pop(L, 1);
                }
                return JsonValue(std::move(arr));
            } else {
                JsonValue::Object obj;
                lua_pushnil(L);
                while (lua_next(L, abs_idx) != 0) {
                    if (lua_type(L, -2) == LUA_TSTRING) {
                        std::string key = lua_tostring(L, -2);
                        int vt = lua_type(L, -1);
                        if (vt != LUA_TFUNCTION && vt != LUA_TUSERDATA && vt != LUA_TTHREAD) {
                            obj[std::move(key)] = lua_to_json(L, -1);
                        }
                    }
                    lua_pop(L, 1);
                }
                return JsonValue(std::move(obj));
            }
        }
        default:
            return JsonValue(nullptr);
    }
}

void LuaVM::json_to_lua(lua_State* L, const JsonValue& val) {
    if (val.is_null()) {
        lua_pushnil(L);
    } else if (val.is_bool()) {
        lua_pushboolean(L, val.as_bool() ? 1 : 0);
    } else if (val.is_int()) {
        lua_pushinteger(L, static_cast<lua_Integer>(val.as_int()));
    } else if (val.is_double()) {
        lua_pushnumber(L, static_cast<lua_Number>(val.as_double()));
    } else if (val.is_string()) {
        const std::string s = val.as_string();
        lua_pushlstring(L, s.data(), s.size());
    } else if (val.is_array()) {
        const auto& arr = val.as_array();
        lua_createtable(L, static_cast<int>(arr.size()), 0);
        for (size_t i = 0; i < arr.size(); ++i) {
            json_to_lua(L, arr[i]);
            lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
        }
    } else if (val.is_object()) {
        const auto& obj = val.as_object();
        lua_createtable(L, 0, static_cast<int>(obj.size()));
        for (const auto& [k, v] : obj) {
            json_to_lua(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    } else {
        lua_pushnil(L);
    }
}

BrickPtr LuaVM::parse_single_brick(int brick_tbl_idx) {
    StackGuard guard(L_);
    int b_idx = lua_absindex(L_, brick_tbl_idx);
    if (!lua_istable(L_, b_idx)) {
        throw std::runtime_error("Expected table when parsing Brick definition");
    }

    JsonValue b_cfg = lua_to_json(L_, b_idx);
    std::string b_type = b_cfg["_brick_type"].as_string(b_cfg["type"].as_string());

    // 1. Recursive Higher-Order Combinator: Brick.When
    if (b_type == "When") {
        std::string name = b_cfg["name"].as_string("Branch");
        int ord = static_cast<int>(b_cfg["order"].as_int(55));
        JsonValue cond = b_cfg.contains("condition") ? b_cfg["condition"] : b_cfg;

        BrickPtr then_b;
        lua_getfield(L_, b_idx, "then_brick");
        if (lua_istable(L_, -1)) then_b = parse_single_brick(lua_gettop(L_));
        lua_pop(L_, 1);

        BrickPtr else_b;
        lua_getfield(L_, b_idx, "else_brick");
        if (lua_istable(L_, -1)) else_b = parse_single_brick(lua_gettop(L_));
        lua_pop(L_, 1);

        return std::make_shared<WhenBrick>(name, ord, cond, std::move(then_b), std::move(else_b));
    }

    // 2. Recursive Higher-Order Combinators: Brick.AnyOf / Brick.AllOf
    if (b_type == "AnyOf" || b_type == "AllOf") {
        std::string name = b_cfg["name"].as_string(b_type == "AnyOf" ? "OrGate" : "PrefabGroup");
        int ord = static_cast<int>(b_cfg["order"].as_int(b_type == "AnyOf" ? 30 : 50));
        std::vector<BrickPtr> children;

        lua_getfield(L_, b_idx, "bricks");
        if (lua_istable(L_, -1)) {
            int arr_idx = lua_gettop(L_);
            lua_Integer n = luaL_len(L_, arr_idx);
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L_, arr_idx, i);
                if (lua_istable(L_, -1)) {
                    children.push_back(parse_single_brick(lua_gettop(L_)));
                }
                lua_pop(L_, 1);
            }
        }
        lua_pop(L_, 1);

        if (b_type == "AnyOf") return std::make_shared<AnyOfBrick>(name, ord, std::move(children));
        return std::make_shared<AllOfBrick>(name, ord, std::move(children));
    }

    // 3. LuaPolicy Script Brick
    auto grab_fn_ref = [&](const char* fn_name) -> int {
        lua_getfield(L_, b_idx, fn_name);
        if (lua_isfunction(L_, -1)) {
            return luaL_ref(L_, LUA_REGISTRYINDEX);
        }
        lua_pop(L_, 1);
        return LUA_NOREF;
    };

    int pre_book_ref = grab_fn_ref("on_pre_book");
    int pre_cancel_ref = grab_fn_ref("on_pre_cancel");
    int pre_checkin_ref = grab_fn_ref("on_pre_check_in");

    if (b_type == "LuaPolicy" || pre_book_ref != LUA_NOREF ||
        pre_cancel_ref != LUA_NOREF || pre_checkin_ref != LUA_NOREF)
    {
        std::string cname = b_cfg["name"].as_string("LuaCustomPolicy");
        std::string summary = b_cfg["description"].as_string("Lua 动态业务规则与自定义计价/准入扩展积木");
        int ord = static_cast<int>(b_cfg["order"].as_int(60));
        std::shared_ptr<LuaVM> non_owning_vm(this, [](LuaVM*) {});
        return std::make_shared<LuaScriptBrick>(
            non_owning_vm, cname, summary, ord,
            pre_book_ref, pre_cancel_ref, pre_checkin_ref, b_cfg);
    }

    // 4. Native C++ Brick from BrickRegistry
    auto native_brick = BrickRegistry::instance().create(b_type, b_cfg);
    if (!native_brick) {
        throw std::runtime_error("Unknown Brick type in Lua blueprint: '" + b_type + "'");
    }
    return native_brick;
}

BlueprintSpec LuaVM::load_blueprint_file(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs) {
        throw std::runtime_error("Cannot open Lua blueprint file: " + path);
    }
    std::stringstream ss;
    ss << ifs.rdbuf();
    std::string code = ss.str();

    std::lock_guard<std::recursive_mutex> lk(mu_);
    StackGuard guard(L_);
    lua_sethook(L_, instruction_quota_hook, LUA_MASKCOUNT, 500000);

    if (luaL_loadbuffer(L_, code.data(), code.size(), ("@" + path).c_str()) != LUA_OK) {
        std::string err = lua_tostring(L_, -1);
        lua_sethook(L_, nullptr, 0, 0);
        throw std::runtime_error("Lua syntax error in " + path + ": " + err);
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        std::string err = lua_tostring(L_, -1);
        lua_sethook(L_, nullptr, 0, 0);
        throw std::runtime_error("Lua runtime error in " + path + ": " + err);
    }
    lua_sethook(L_, nullptr, 0, 0);

    if (!lua_istable(L_, -1)) {
        throw std::runtime_error("Lua blueprint '" + path + "' must return a Blueprint { ... } table");
    }

    return parse_blueprint_table(lua_gettop(L_), path, code);
}

BlueprintSpec LuaVM::load_blueprint_string(const std::string& code, const std::string& chunk_name) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    StackGuard guard(L_);
    lua_sethook(L_, instruction_quota_hook, LUA_MASKCOUNT, 500000);

    if (luaL_loadbuffer(L_, code.data(), code.size(), chunk_name.c_str()) != LUA_OK) {
        std::string err = lua_tostring(L_, -1);
        lua_sethook(L_, nullptr, 0, 0);
        throw std::runtime_error("Lua syntax error: " + err);
    }

    if (lua_pcall(L_, 0, 1, 0) != LUA_OK) {
        std::string err = lua_tostring(L_, -1);
        lua_sethook(L_, nullptr, 0, 0);
        throw std::runtime_error("Lua runtime error: " + err);
    }
    lua_sethook(L_, nullptr, 0, 0);

    if (!lua_istable(L_, -1)) {
        throw std::runtime_error("Lua blueprint script must return a Blueprint { ... } table");
    }

    return parse_blueprint_table(lua_gettop(L_), chunk_name, code);
}

BlueprintSpec LuaVM::parse_blueprint_table(int table_idx, const std::string& src_path, const std::string& raw_code) {
    int abs_bp = lua_absindex(L_, table_idx);
    JsonValue bp_json = lua_to_json(L_, abs_bp);

    BlueprintSpec spec;
    spec.id = bp_json["id"].as_string("custom");
    spec.name = bp_json["name"].as_string(spec.id);
    spec.description = bp_json["description"].as_string();
    spec.currency = bp_json["currency"].as_string("CNY");
    spec.source_path = src_path;
    spec.lua_source = raw_code;

    if (bp_json.contains("fault_policy") && bp_json["fault_policy"].is_object()) {
        const auto& fp = bp_json["fault_policy"];
        spec.fault_policy.auto_reallocate = fp["auto_reallocate"].as_bool(true);
        spec.fault_policy.match_same_tags = fp["match_same_tags"].as_bool(true);
        spec.fault_policy.compensate_priority = static_cast<int>(fp["compensate_priority"].as_int(30));
    }

    if (bp_json.contains("resources") && bp_json["resources"].is_array()) {
        for (const auto& r_j : bp_json["resources"].as_array()) {
            spec.seed_resources.push_back(Resource::from_json(r_j));
        }
    }

    if (bp_json.contains("actors") && bp_json["actors"].is_array()) {
        for (const auto& a_j : bp_json["actors"].as_array()) {
            spec.seed_actors.push_back(Actor::from_json(a_j));
        }
    }

    lua_getfield(L_, abs_bp, "bricks");
    if (lua_istable(L_, -1)) {
        int bricks_tbl = lua_gettop(L_);
        lua_Integer len = luaL_len(L_, bricks_tbl);
        for (lua_Integer i = 1; i <= len; ++i) {
            lua_rawgeti(L_, bricks_tbl, i);
            if (lua_istable(L_, -1)) {
                spec.bricks.push_back(parse_single_brick(lua_gettop(L_)));
            }
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);

    clear_hook_refs();
    lua_getfield(L_, abs_bp, "hooks");
    if (lua_istable(L_, -1)) {
        int hooks_tbl = lua_gettop(L_);
        lua_pushnil(L_);
        while (lua_next(L_, hooks_tbl) != 0) {
            if (lua_type(L_, -2) == LUA_TSTRING && lua_isfunction(L_, -1)) {
                std::string h_name = lua_tostring(L_, -2);
                int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
                hook_refs_[h_name] = ref;
            } else {
                lua_pop(L_, 1);
            }
        }
    }
    lua_pop(L_, 1);

    return spec;
}

void LuaVM::dispatch_event(const DomainEvent& ev) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (!L_ || hook_refs_.empty()) return;

    auto invoke_ref = [&](int ref) {
        StackGuard guard(L_);
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
        if (!lua_isfunction(L_, -1)) return;
        json_to_lua(L_, ev.to_json());
        lua_sethook(L_, instruction_quota_hook, LUA_MASKCOUNT, 200000);
        if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
            std::string err = lua_tostring(L_, -1);
            append_log("[LuaHook Error] " + err);
        }
        lua_sethook(L_, nullptr, 0, 0);
    };

    auto it_specific = hook_refs_.find("on_" + ev.type);
    if (it_specific != hook_refs_.end()) invoke_ref(it_specific->second);

    auto it_any = hook_refs_.find("on_any_event");
    if (it_any != hook_refs_.end()) invoke_ref(it_any->second);
}

} // namespace chronokit
