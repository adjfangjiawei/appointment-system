-- ============================================================================
-- 蓝图 05：黑珍珠高端餐厅餐位与景观包厢预订系统
-- 组装特性：周一店休 + 20分钟翻台布草缓冲 + 晚市黄金档餐位人数匹配 + 按位订金 + 纪念日侍酒师礼遇(Lua)
-- ============================================================================

return Blueprint {
    id = "fine_dining",
    name = "黑珍珠高端餐厅餐位与江景包厢预订系统",
    description = "面向精致餐饮场景：内置周一店休、每桌20分钟翻台消杀缓冲、晚高峰餐桌人数利用率防空置、按位订金及纪念日专属礼遇",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "11:30",
            close_time = "22:30",
            weekdays = { 2, 3, 4, 5, 6, 7 }, -- 周一闭店备料休息
            blackout_dates = { "2026-12-31" }, -- 跨年夜仅接受线下包场
        },

        Brick.SlotGranularity {
            align_minutes = 30,
            min_duration_minutes = 90,
            max_duration_minutes = 180,
        },

        Brick.BookingWindow {
            min_advance_minutes = 60,
            max_advance_days = 30,
            role_max_advance_days = { black_pearl_vip = 60 },
        },

        -- 每桌用餐结束后自动留出 20 分钟翻台与桌面布草重置时间
        Brick.BufferTime {
            pre_buffer_minutes = 0,
            post_buffer_minutes = 20,
            min_duration_to_apply = 60,
        },

        -- 晚市 18:00-21:00 黄金档至少坐满餐桌容量的 60%（如 8 人包厢至少 5 人起订，VIP 豁免）
        Brick.PartySizeMatcher {
            min_units = 1,
            max_units = 12,
            min_capacity_ratio = 0.6,
            enforce_ratio_peak_only = true,
            peak_start = "18:00",
            peak_end = "21:00",
        },

        Brick.RoleAndCreditGate {
            min_credit_score = 70,
            deduct_on_no_show = 25,
            tag_required_roles = {
                chef_table = { "black_pearl_vip", "vip" },
            },
        },

        -- 按就餐人数收取订金 (¥100/位，江景包厢附加服务订金)
        Brick.PricingAndDeposit {
            billing_mode = "per_unit_flat",
            base_rate = 100.0,
            tag_surcharges = {
                river_view = 50.0,
                private_suite = 100.0,
            },
            deposit_ratio = 1.0,
        },

        Brick.LuaPolicy {
            name = "SommelierAndOccasionConcierge",
            description = "识别纪念日/生日备注自动安排侍酒师香槟礼遇，6人以上大桌自动触发主厨菜单确认流",
            order = 60,
            on_pre_book = function(ctx)
                local occasion = ctx.metadata.occasion or ""
                if occasion == "anniversary" or occasion == "birthday" then
                    ctx.metadata.complimentary_perk = "赠送双人年份香槟与手写主厨贺卡"
                    ctx:add_note("纪念日礼遇已激活：赠送侍酒师精选香槟与定制布置")
                end
                if ctx.units >= 8 then
                    ctx.require_approval = true
                    ctx:add_note("8人以上大型宴请：已自动转交餐厅经理确认定制配菜方案")
                end
                return true
            end
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 6,
            role_priority_bonus = { black_pearl_vip = 50, vip = 25 },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
            exclusive = true, -- 整桌排他独占模式：不拼桌
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 240,   -- 用餐前 4 小时免费退订金
            late_cancel_fee_ratio = 0.5,        -- 临期退订扣除 50% 备餐损耗
            non_refundable_before_minutes = 60,
            check_in_earliest_before_minutes = 30,
            check_in_grace_after_minutes = 20,
        },
    },

    resources = {
        { id = "TBL-WIN-01", name = "临窗双人浪漫雅座 W1", kind = "dining_table", capacity = 2, tags = { "river_view" } },
        { id = "TBL-WIN-02", name = "临窗四人景观桌 W2", kind = "dining_table", capacity = 4, tags = { "river_view" } },
        { id = "TBL-HALL-01", name = "主厅花园四人桌 H1", kind = "dining_table", capacity = 4, tags = {} },
        { id = "SUITE-VIP-88", name = "外滩全景私密包厢 888 (8人)", kind = "private_room", capacity = 8, tags = { "river_view", "private_suite" } },
        { id = "CHEF-TABLE-01", name = "主厨板前品鉴席 (仅限VIP)", kind = "chef_counter", capacity = 4, tags = { "chef_table" } },
    },

    actors = {
        { id = "guest_lin", name = "林先生", roles = { "guest" }, credit_score = 100 },
        { id = "guest_vip", name = "苏女士 (黑珍珠黑卡会员)", roles = { "guest", "vip", "black_pearl_vip" }, credit_score = 120 },
        { id = "maitre_d", name = "餐厅主理人", roles = { "admin" }, credit_score = 120 },
    }
}
