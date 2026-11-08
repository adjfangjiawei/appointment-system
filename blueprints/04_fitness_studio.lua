-- ============================================================================
-- 蓝图 04：精品健身房团课与私教预约系统
-- 组装特性：多人团课容量池 + 早鸟折扣与晚高峰溢价 + 双人同行立减(Lua) + 严格阶梯退课手续费
-- ============================================================================

return Blueprint {
    id = "fitness_studio",
    name = "精品健身房团课与私教预约系统",
    description = "融合多人燃脂团课与1v1私教预约：支持黑金会员免单、早鸟85折、双人结伴优惠、满员自动候补及阶梯退改扣费",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "07:00",
            close_time = "22:00",
            weekdays = { 1, 2, 3, 4, 5, 6, 7 },
        },

        Brick.SlotGranularity {
            align_minutes = 30,
            min_duration_minutes = 60,
            max_duration_minutes = 120,
        },

        Brick.BookingWindow {
            min_advance_minutes = 30,
            max_advance_days = 7,
            role_max_advance_days = { black_card = 14 },
        },

        Brick.PartySizeMatcher {
            min_units = 1,
            max_units = 4, -- 允许最多邀约 3 位好友一起拼团上课
        },

        Brick.PricingAndDeposit {
            billing_mode = "per_unit_hour",
            base_rate = 99.0,
            peak_start = "18:30",
            peak_end = "21:00",
            peak_multiplier = 1.2,      -- 晚间黄金档 1.2 倍
            early_bird_hours = 48,      -- 提前48小时享早鸟 85 折
            early_bird_discount = 0.85,
            role_discounts = {
                silver_card = 0.8,
                gold_card = 0.6,
            },
        },

        Brick.LuaPolicy {
            name = "BlackCardAndBuddyPerk",
            description = "黑金卡会员团课单人全免，2人及以上结伴同行享折上 88 折激励",
            order = 60,
            on_pre_book = function(ctx)
                if ctx:actor_has_role("black_card") and ctx.resource.kind == "group_class" and ctx.units == 1 then
                    ctx.price = 0.0
                    ctx.priority = ctx.priority + 40
                    ctx:add_note("黑金无限卡权益：精品团课本人免单 & 候补优先 +40")
                    return true
                end

                if ctx.units >= 2 then
                    ctx.price = math.floor(ctx.price * 0.88 * 100) / 100
                    ctx:add_note("好友结伴燃脂激励：" .. tostring(ctx.units) .. "人同行享折上 88 折")
                end
                return true
            end
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 8,
            role_priority_bonus = { black_card = 40, gold_card = 20 },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 360,    -- 开课6小时前免费取消
            late_cancel_fee_ratio = 0.30,        -- 1~6小时内取消收 30% 手续费
            non_refundable_before_minutes = 60,  -- 开课前1小时内取消不予退款
            check_in_earliest_before_minutes = 30,
            check_in_grace_after_minutes = 10,
        },
    },

    resources = {
        { id = "CLS-SPIN-01", name = "沉浸式动感单车燃脂课 (12人舱)", kind = "group_class", capacity = 12, tags = { "cardio", "hiit" }, attributes = { base_rate = 89 } },
        { id = "CLS-REFORMER-01", name = "普拉提核心床小班课 (6人精品)", kind = "group_class", capacity = 6, tags = { "pilates", "reformer" }, attributes = { base_rate = 168 } },
        { id = "PT-COACH-LEO", name = "明星私教 Leo 1v1 增肌塑形", kind = "personal_trainer", capacity = 1, tags = { "strength", "1v1" }, attributes = { base_rate = 360, max_party = 1 } },
    },

    actors = {
        { id = "mem_dropin", name = "周小姐 (散客体验)", roles = { "member" }, credit_score = 100 },
        { id = "mem_gold", name = "吴先生 (金卡会员)", roles = { "member", "gold_card" }, credit_score = 100 },
        { id = "mem_black", name = "郑女士 (黑金无限卡)", roles = { "member", "black_card" }, credit_score = 110 },
        { id = "coach_admin", name = "店长兼主教练", roles = { "admin" }, credit_score = 120 },
    }
}
