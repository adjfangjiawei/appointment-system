#pragma once

#include "chronokit/bricks/brick.hpp"
#include "chronokit/lua/lua_vm.hpp"

#include <memory>
#include <string>

namespace chronokit {

// First-class IBrick backed by Lua 5.4 functions (`on_pre_book`, `on_pre_cancel`, `on_pre_check_in`).
// Sits seamlessly alongside native C++ Bricks in the composable Pipeline!
class LuaScriptBrick : public IBrick {
public:
    LuaScriptBrick(
        std::shared_ptr<LuaVM> vm,
        std::string custom_name,
        std::string summary,
        int order_val,
        int pre_book_ref,
        int pre_cancel_ref,
        int pre_checkin_ref,
        JsonValue extra_cfg);

    ~LuaScriptBrick() override;

    std::string type_id() const override { return "LuaPolicy"; }
    std::string instance_name() const override { return custom_name_; }
    std::string category() const override { return "Lua"; }
    int order() const override { return order_; }

    void configure(const JsonValue& cfg) override { extra_cfg_ = cfg; }
    BrickDescriptor describe() const override;

    BrickVerdict on_pre_book(BookingContext& ctx) override;
    BrickVerdict on_pre_cancel(CancelContext& ctx) override;
    BrickVerdict on_pre_check_in(CheckInContext& ctx) override;

private:
    std::shared_ptr<LuaVM> vm_;
    std::string custom_name_;
    std::string summary_;
    int order_ = 60;
    int pre_book_ref_;
    int pre_cancel_ref_;
    int pre_checkin_ref_;
    JsonValue extra_cfg_;
};

} // namespace chronokit
