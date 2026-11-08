-- ============================================================================
-- 蓝图 08：综合体育馆羽毛球场与赛事级场馆预订系统 (v3.0 新增)
-- 组装特性：Brick.AnyOf(逻辑『或』门准入：金卡会员 或 高信用常客均可解锁赛事中心场)
--          + CouponAndVoucher(满减券/学生券) + 球场+发球机捆绑 + 支持锁座待支付/改签/转赠
-- ============================================================================

return Blueprint {
    id = "sports_badminton_arena",
    name = "综合体育馆羽毛球场与器材租赁预约系统",
    description = "演示 Brick.AnyOf 逻辑或门组合子、优惠券防重核销、两阶段锁座支付、一键无损改签及球友间场地合规转赠",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "08:00",
            close_time = "22:30",
            weekdays = { 1, 2, 3, 4, 5, 6, 7 },
        },

        Brick.SlotGranularity {
            align_minutes = 60,
            min_duration_minutes = 60,
            max_duration_minutes = 180,
        },

        -- 核心演示：Brick.When + Brick.AnyOf 组合！
        -- 当预订带有 #pro_court 标签的赛事级中心球场时，只需满足『场馆金卡会员』【或】『守约信用分 >= 115 的资深球友』任一条件即可放行！
        Brick.When {
            name = "ProCourtAccessCheck",
            order = 30,
            condition = {
                resource_tags = { "pro_court" },
            },
            then_brick = Brick.AnyOf {
                name = "GoldOrHighCreditGate",
                order = 30,
                bricks = {
                    Brick.RoleAndCreditGate {
                        allowed_roles = { "arena_gold", "admin" },
                        min_credit_score = 60,
                    },
                    Brick.RoleAndCreditGate {
                        min_credit_score = 115,
                    },
                }
            },
            else_brick = Brick.RoleAndCreditGate {
                min_credit_score = 60,
                deduct_on_no_show = 15,
                reward_on_complete = 3,
            }
        },

        Brick.QuotaLimiter {
            max_active_per_actor = 3,
            max_daily_bookings_per_actor = 2,
            max_daily_minutes_per_actor = 180,
        },

        Brick.PricingAndDeposit {
            billing_mode = "per_hour",
            base_rate = 80.0,
            peak_start = "18:00",
            peak_end = "22:00",
            peak_multiplier = 1.5,     -- 黄金晚场 ¥120/h
            weekend_multiplier = 1.25,
            tag_surcharges = { pro_court = 40.0 },
            role_discounts = { arena_gold = 0.8 },
        },

        Brick.CouponAndVoucher {
            coupons = {
                SPORT30  = { type = "amount_off", value = 30.0, min_spend = 100.0, label = "全民健身满100减30消费券", single_use_per_actor = true },
                CAMPUS75 = { type = "discount_rate", value = 0.75, required_role = "student", label = "大学生阳光运动75折券", single_use_per_actor = false },
            }
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 5,
            role_priority_bonus = { arena_gold = 35 },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
            exclusive = true,
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 180,
            late_cancel_fee_ratio = 0.3,
            check_in_earliest_before_minutes = 20,
            check_in_grace_after_minutes = 20,
        },
    },

    fault_policy = {
        auto_reallocate = true,
        match_same_tags = false,
        compensate_priority = 30,
    },

    resources = {
        { id = "CRT-STD-01", name = "1号 标准塑胶羽毛球场", kind = "badminton_court", capacity = 6, tags = { "standard" }, attributes = { base_rate = 80 } },
        { id = "CRT-STD-02", name = "2号 标准塑胶羽毛球场", kind = "badminton_court", capacity = 6, tags = { "standard" }, attributes = { base_rate = 80 } },
        { id = "CRT-PRO-VIP", name = "V1 赛事级龙骨木地板中心场", kind = "badminton_court", capacity = 8, tags = { "pro_court", "tv_replay" }, attributes = { base_rate = 120 } },
        { id = "EQ-FEEDER-01", name = "智能羽毛球自动发球机 #1", kind = "sport_gear", capacity = 1, tags = { "training" }, attributes = { base_rate = 35 } },
    },

    actors = {
        { id = "plr_casual", name = "小陈 (普通球友/信用95)", roles = { "player", "student" }, credit_score = 95 },
        { id = "plr_veteran", name = "老赵 (非金卡但信用120高分常客)", roles = { "player" }, credit_score = 120 },
        { id = "plr_gold", name = "林总 (球馆金卡会员/信用90)", roles = { "player", "arena_gold" }, credit_score = 90 },
        { id = "arena_mgr", name = "球馆运营主管", roles = { "admin" }, credit_score = 120 },
    }
}
