-- ============================================================================
-- 蓝图 07：智慧政务大厅一窗联办与防黄牛风控系统 (v3.0 新增)
-- 组装特性：Prefab.AntiScalperSuite(实名核验+退单熔断预制件) + DependencyChain(先核税后过户串联)
--          + Brick.When(高阶条件分支：长者/绿色通道免限额 vs 普通办事窗口配额)
-- ============================================================================

return Blueprint {
    id = "gov_service_hall",
    name = "智慧政务大厅一窗联办与防黄牛预约系统",
    description = "展示高阶组合子与预制件模组：内置实名防黄牛频繁退号熔断、不动产『先税务核定->后产权过户』强制前置依赖链及长者绿色通道条件分支",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "09:00",
            close_time = "17:00",
            weekdays = { 1, 2, 3, 4, 5 },
        },

        Brick.SlotGranularity {
            align_minutes = 30,
            min_duration_minutes = 30,
            max_duration_minutes = 60,
        },

        -- 1. 一行代码引入可复用预制件模组 Prefab.AntiScalperSuite (串联 AntiScalper + CooldownInterval)！
        Prefab.AntiScalperSuite {
            name = "GovAntiScalperShield",
            max_cancels = 2,                       -- 单日退号满 2 次即触发黄牛熔断！
            max_burst = 4,
            verified_attr = "real_name_verified",  -- 强制校验政务实名认证属性
            cooldown_minutes = 30,
        },

        -- 2. 前置业务依赖链积木：预约「不动产产权过户(property_transfer)」前，必须先约妥早于该时段至少15分钟的「契税核定(tax_assessment)」！
        Brick.DependencyChain {
            dependencies = {
                property_transfer = {
                    required_kind = "tax_assessment",
                    require_completed = false,
                    min_gap_minutes = 15,
                    within_days = 7,
                }
            }
        },

        -- 3. 高阶条件组合子 Brick.When：若为高龄长者/残障优抚对象走宽松绿色配额，普通市民执行严格单日限号！
        Brick.When {
            name = "GreenChannelBranch",
            order = 40,
            condition = {
                actor_roles = { "senior_green_channel", " veteran" },
            },
            then_brick = Brick.QuotaLimiter {
                max_active_per_actor = 4,
                max_daily_bookings_per_actor = 4,
                max_daily_minutes_per_actor = 240,
            },
            else_brick = Brick.QuotaLimiter {
                max_active_per_actor = 2,
                max_daily_bookings_per_actor = 2,
                max_daily_minutes_per_actor = 90,
            },
        },

        Brick.PricingAndDeposit {
            billing_mode = "free",
        },

        Brick.ResourceLoadBalancer {
            mode = "on_conflict_or_auto",
            match_tags = false,
            exclusive = false,
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 10,
            role_priority_bonus = { senior_green_channel = 80 },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
            exclusive = false,
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 30,
            late_cancel_fee_ratio = 0.0,
            check_in_earliest_before_minutes = 30,
            check_in_grace_after_minutes = 15,
        },
    },

    resources = {
        { id = "WIN-TAX-01", name = "税务联办窗口 01-03号池", kind = "tax_assessment", capacity = 3, tags = { "real_estate" } },
        { id = "WIN-PROP-01", name = "不动产过户登记专窗 A区", kind = "property_transfer", capacity = 2, tags = { "real_estate" } },
        { id = "WIN-PASSPORT-01", name = "出入境证照综合窗口", kind = "passport_visa", capacity = 4, tags = { "public_security" } },
    },

    actors = {
        { id = "cit_wang", name = "王市民 (已实名认证)", roles = { "citizen" }, credit_score = 100, attributes = { real_name_verified = true } },
        { id = "cit_elder", name = "孙大爷 (绿色通道优抚长者)", roles = { "citizen", "senior_green_channel" }, credit_score = 110, attributes = { real_name_verified = true } },
        { id = "scalper_bot", name = "疑似抢号黄牛账号 (未实名)", roles = { "citizen" }, credit_score = 80, attributes = { real_name_verified = false } },
        { id = "hall_director", name = "政务大厅值班长", roles = { "admin" }, credit_score = 120, attributes = { real_name_verified = true } },
    }
}
