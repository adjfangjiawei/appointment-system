# ChronoKit Lua 5.4 蓝图与自定义积木参考手册

ChronoKit 内置了声明式 Lua 5.4 DSL，使得组装一套新行业的预约系统只需编写一个返回 `Blueprint { ... }` 的 `.lua` 文件，且支持在不停机的情况下通过 `reload_blueprint_code` 毫秒级热更新流水线。

---

## 1. 蓝图顶层结构 (`Blueprint`)

```lua
return Blueprint {
    id = "unique_system_id",
    name = "系统中文名称",
    description = "业务场景说明",
    currency = "CNY",

    -- 1. 组装积木流水线（自动按各积木的 Stage Order 稳定排序执行）
    bricks = {
        Brick.OpeningHours { ... },
        Brick.SlotGranularity { ... },
        Brick.LuaPolicy { ... },
        Brick.ConflictAndCapacity { ... },
    },

    -- 2. 设备/空间突发故障策略
    fault_policy = {
        auto_reallocate = true,     -- 故障时是否自动搜索同类型健康资源无感迁移
        match_same_tags = true,     -- 自动换座时是否要求新资源具备原资源全部标签
        compensate_priority = 30,   -- 无法换座而被迫退单时补偿的优先级分值
    },

    -- 3. 初始资源种子（可选）
    resources = {
        {
            id = "RES-01",
            name = "资源名称",
            kind = "seat",
            capacity = 1,
            tags = { "gpu", "vip_only" },
            attributes = { base_rate = 20.0, open_time = "09:00", close_time = "21:00" }
        }
    },

    -- 4. 初始用户/角色种子（可选）
    actors = {
        {
            id = "user_01",
            name = "张三",
            roles = { "student", "vip" },
            credit_score = 100,
            attributes = { department = "CS", age = 22 }
        }
    },

    -- 5. 领域事件异步钩子（可选）
    hooks = {
        on_booking_confirmed = function(ev) log("新确认订单: " .. ev.booking_id) end,
        on_waitlist_promoted = function(ev) log("候补自动转正: " .. ev.booking_id) end,
        on_resource_fault_reallocated = function(ev) log("故障换座完成: " .. ev.booking_id) end,
        on_any_event = function(ev) end,
    }
}
```

---

## 2. 编写自定义 Lua 策略积木 (`Brick.LuaPolicy`)

当 12 块内置 C++ 积木的参数组合仍不足以表达极其特殊的行业定制逻辑（如医保统筹精算、阶梯拼团折扣、纪念日礼遇赠送、特殊年龄/资质校验）时，可直接在流水线中插入一块或多块 `Brick.LuaPolicy`：

```lua
Brick.LuaPolicy {
    name = "CustomRuleName",
    description = "规则说明（会展示在 Studio 流水线透视图中）",
    order = 60, -- 默认 60（位于基础计价 50 之后、容量与候补判定 70 之前）

    on_pre_book = function(ctx)
        -- 1. 读取上下文只读属性
        -- ctx.actor.id, ctx.actor.name, ctx.actor.credit_score, ctx.actor.attributes
        -- ctx.resource.id, ctx.resource.name, ctx.resource.kind, ctx.resource.capacity
        -- ctx.resources (数组：当复合捆绑预约时包含全部资源)
        -- ctx.units, ctx.duration_minutes, ctx.start_hour, ctx.weekday (1..7), ctx.lead_minutes
        -- ctx.start_time ("YYYY-MM-DD HH:MM"), ctx.end_time, ctx.date

        -- 2. 便捷辅助判断方法
        if ctx:resource_has_tag("restricted") and not ctx:actor_has_role("certified") then
            return ctx:reject("CERT_REQUIRED", "预约受限设备需先通过安全操作认证")
        end

        -- 3. 动态修改订单工作区字段（C++ 引擎在函数返回后自动回写生效！）
        if ctx.units >= 3 then
            ctx.price = ctx.price * 0.85
            ctx:add_note("三人及以上团购立享 85 折")
        end

        ctx.priority = ctx.priority + 10
        ctx.post_buffer_minutes = ctx.post_buffer_minutes + 5
        ctx.metadata.custom_tag = "verified_by_lua"

        -- 若需动态触发人工审批流：
        if ctx.price >= 1000 then
            ctx.require_approval = true
            ctx:add_note("千元以上大额预约，动态转交主管审批")
        end

        return true
    end,

    on_pre_cancel = function(ctx)
        -- 可读取 ctx.price, ctx.mins_before_start，并按需调整 ctx.penalty_fee / ctx.refund_amount
        return true
    end,

    on_pre_check_in = function(ctx)
        -- 可读取 ctx.mins_since_start 校验自定义签到逻辑
        return true
    end
}
```

---

## 3. 高阶组合子与可复用预制件 (`Brick.When` / `Brick.AnyOf` / `Brick.AllOf` / `Prefab.*`)

v3.0 引入了函数式高阶积木组合子，支持任意层级嵌套编排：

```lua
-- 1. 条件分支组合子 Brick.When：不同人群/时段/资源标签走完全不同的积木策略
Brick.When {
    name = "ProCourtAccessCheck",
    order = 30,
    condition = {
        resource_tags = { "pro_court" }, -- 支持 weekdays / actor_roles / resource_tags / min_units / start_from / start_to
    },
    -- 2. 逻辑『或』门组合子 Brick.AnyOf：满足任一子积木即放行（失败分支自动回滚上下文修改）
    then_brick = Brick.AnyOf {
        name = "GoldOrHighCreditGate",
        bricks = {
            Brick.RoleAndCreditGate { allowed_roles = { "arena_gold", "admin" } },
            Brick.RoleAndCreditGate { min_credit_score = 115 },
        }
    },
    else_brick = Brick.RoleAndCreditGate { min_credit_score = 60 }
}

-- 3. 一行调用内置预制件模组 Prefab.* (底层展开为 Brick.AllOf 复合包)
Prefab.AntiScalperSuite {
    name = "GovAntiScalperShield",
    max_cancels = 2,
    verified_attr = "real_name_verified",
    cooldown_minutes = 30,
}
```

此外，`Brick.LuaPolicy` 的 `ctx` 上下文在 v3.0 中新增了实时宿主状态查询方法：
- `ctx.kind_occupancy_ratio`：当前请求资源所属类别在目标时段的实时并发负载率 (`0.0 ~ 1.0`)
- `ctx.actor_completed_count` / `ctx.actor_daily_cancels`：当前用户历史完结单数与今日退单次数
- `ctx:actor_completed_kind("safety_training")`：判断用户是否已完成指定类别的前置预约
- `ctx:apply_discount(20.0, "专项补贴立减 ¥20")`：安全扣减订单价格并累加记录至 `discount_amount`

---

## 4. 沙箱安全保证

- **安全标准库白名单**：仅开放 `base`, `table`, `string`, `math`, `utf8`；默认屏蔽 `io`, `os`, `loadfile`, `dofile` 等宿主机文件系统与进程调用。
- **VM 指令配额防死循环保护**：每次调用 Lua 蓝图或策略函数均受 `500,000` 条虚拟机指令上限保护，一旦脚本存在死循环会在数毫秒内自动中断并返回安全错误码。
- **原子热更新**：在线热重载新 Lua 代码时，若存在语法或初始化错误，引擎保证原有正在运行的积木流水线不受任何破坏。
