-- ============================================================================
-- 蓝图 06：800V 液冷超充站智能派桩与实时供需动态电价系统 (v3.0 新增)
-- 组装特性：ResourceLoadBalancer(磨损均衡自动派桩) + DynamicSurgePricing(实时负载动态电价)
--          + CooldownInterval(防连续霸桩) + 提前拔枪释放候补 / 超时占桩阶梯计费
-- ============================================================================

return Blueprint {
    id = "ev_supercharger",
    name = "800V液冷超充站智能派桩与动态电价系统",
    description = "自动均衡各充电桩日累计磨损时长、根据站内实时负载率自动执行闲时谷电75折与满载需量电价、防连续拆单霸桩及超时占桩结算",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "00:00",
            close_time = "24:00",
            weekdays = { 1, 2, 3, 4, 5, 6, 7 },
        },

        Brick.SlotGranularity {
            align_minutes = 15,
            min_duration_minutes = 30,
            max_duration_minutes = 120,
        },

        -- 核心积木：同一车主两次预约超充桩至少需间隔 45 分钟，杜绝拆单无限霸桩！
        Brick.CooldownInterval {
            min_gap_same_resource_minutes = 45,
            min_gap_same_kind_minutes = 30,
        },

        Brick.PricingAndDeposit {
            billing_mode = "per_hour",
            base_rate = 60.0, -- 基准快充服务费预估 ¥60/小时
            role_discounts = { brand_owner = 0.85, fleet_vip = 0.75 },
        },

        -- 核心积木：实时测算全站超充桩并发负载率 (<20% 享 0.75 错峰补贴；>=60% 触发 1.25 繁忙调价；>=85% 触发 1.50 峰值电价)
        Brick.DynamicSurgePricing {
            low_occupancy_threshold = 0.20,
            low_occupancy_multiplier = 0.75,
            high_occupancy_threshold = 0.60,
            high_occupancy_multiplier = 1.25,
            critical_occupancy_threshold = 0.85,
            critical_occupancy_multiplier = 1.50,
        },

        -- 核心积木：优惠券核销引擎
        Brick.CouponAndVoucher {
            coupons = {
                EVFREE15 = { type = "free_minutes", value = 15, label = "车主首充免15分钟服务费券", single_use_per_actor = true },
                GREEN20  = { type = "amount_off", value = 20, min_spend = 50, label = "绿色出行满50减20券", single_use_per_actor = true },
            }
        },

        Brick.LuaPolicy {
            name = "GridLoadBatteryGuard",
            description = "结合实时站内负载率(ctx.kind_occupancy_ratio)限制高负荷期商用车队大功率包场",
            order = 60,
            on_pre_book = function(ctx)
                if ctx.kind_occupancy_ratio >= 0.75 and ctx:actor_has_role("commercial_fleet") then
                    return ctx:reject("GRID_PEAK_THROTTLE", "当前场站电网负载超 75%，暂停商用车队预约以保障私家车应急补能")
                end
                if ctx.start_hour >= 0 and ctx.start_hour < 6 then
                    ctx:apply_discount(10.0, "凌晨深谷绿电专项激励立减 ¥10")
                end
                return true
            end
        },

        -- 核心积木：自动将车主路由至当前时段空闲且今日累计充电时长最低的液冷桩，延长硬件寿命！
        Brick.ResourceLoadBalancer {
            mode = "always_balance",
            match_tags = true,
            exclusive = true,
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 6,
            role_priority_bonus = { brand_owner = 30 },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
            exclusive = true,
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 15,
            late_cancel_fee_ratio = 0.2,
            check_in_earliest_before_minutes = 15,
            check_in_grace_after_minutes = 10, -- 逾10分钟未插枪自动释放桩位！
        },
    },

    fault_policy = {
        auto_reallocate = true,
        match_same_tags = true,
        compensate_priority = 40,
    },

    resources = {
        { id = "PILE-800V-A1", name = "A1号 800V液冷超充桩 (600kW)", kind = "supercharger", capacity = 1, tags = { "800v", "liquid_cooled" } },
        { id = "PILE-800V-A2", name = "A2号 800V液冷超充桩 (600kW)", kind = "supercharger", capacity = 1, tags = { "800v", "liquid_cooled" } },
        { id = "PILE-800V-A3", name = "A3号 800V液冷超充桩 (600kW)", kind = "supercharger", capacity = 1, tags = { "800v", "liquid_cooled" } },
        { id = "PILE-800V-A4", name = "A4号 800V液冷超充桩 (600kW)", kind = "supercharger", capacity = 1, tags = { "800v", "liquid_cooled" } },
    },

    actors = {
        { id = "ev_owner_li", name = "李车主 (品牌认证私家车)", roles = { "driver", "brand_owner" }, credit_score = 105 },
        { id = "ev_guest_zhou", name = "周先生 (跨品牌社会车辆)", roles = { "driver" }, credit_score = 100 },
        { id = "ev_fleet_01", name = "城配物流车队01号", roles = { "driver", "commercial_fleet" }, credit_score = 95 },
        { id = "station_ops", name = "场站智能运维官", roles = { "admin" }, credit_score = 120 },
    }
}
