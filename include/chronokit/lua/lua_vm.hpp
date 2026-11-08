#pragma once

#include "chronokit/bricks/brick.hpp"
#include "chronokit/core/event_bus.hpp"
#include "chronokit/core/json.hpp"
#include "chronokit/core/types.hpp"

#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct lua_State;

namespace chronokit {

struct BlueprintSpec {
    std::string id = "custom";
    std::string name = "Custom Blueprint";
    std::string description;
    std::string currency = "CNY";
    std::string source_path;
    std::string lua_source;
    FaultPolicy fault_policy;
    std::vector<BrickPtr> bricks;
    std::vector<Resource> seed_resources;
    std::vector<Actor> seed_actors;

    JsonValue summary_json() const {
        JsonValue b_arr = JsonValue::array();
        for (const auto& b : bricks) b_arr.push_back(b->describe().to_json());
        return JsonValue::object({
            {"id", id},
            {"name", name},
            {"description", description},
            {"currency", currency},
            {"source_path", source_path},
            {"fault_policy", JsonValue::object({
                {"auto_reallocate", fault_policy.auto_reallocate},
                {"match_same_tags", fault_policy.match_same_tags},
                {"compensate_priority", fault_policy.compensate_priority}
            })},
            {"bricks", std::move(b_arr)},
            {"seed_resources_count", seed_resources.size()},
            {"seed_actors_count", seed_actors.size()}
        });
    }
};

class LuaVM {
public:
    LuaVM();
    ~LuaVM();

    LuaVM(const LuaVM&) = delete;
    LuaVM& operator=(const LuaVM&) = delete;

    lua_State* state() const { return L_; }
    std::recursive_mutex& mutex() const { return mu_; }

    // Load a Blueprint from a .lua file or raw Lua code string
    BlueprintSpec load_blueprint_file(const std::string& path);
    BlueprintSpec load_blueprint_string(const std::string& code, const std::string& chunk_name = "inline_blueprint.lua");

    // Dispatch domain event to registered Lua blueprint hooks
    void dispatch_event(const DomainEvent& ev);

    // Lua `log(...)` output ring buffer for Studio / debugging
    void append_log(const std::string& msg);
    std::vector<std::string> recent_logs(size_t limit = 50) const;

    // Conversion helpers (caller must hold mutex())
    static JsonValue lua_to_json(lua_State* L, int index);
    static void json_to_lua(lua_State* L, const JsonValue& val);

private:
    lua_State* L_ = nullptr;
    mutable std::recursive_mutex mu_;
    std::map<std::string, int> hook_refs_; // event_name -> LUA_REGISTRYINDEX ref
    std::deque<std::string> logs_;

    void init_sandbox_and_dsl();
    void clear_hook_refs();
    BrickPtr parse_single_brick(int brick_tbl_idx);
    BlueprintSpec parse_blueprint_table(int table_idx, const std::string& src_path, const std::string& raw_code);
};

} // namespace chronokit
