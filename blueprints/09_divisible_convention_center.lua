-- ============================================================================
-- 蓝图 09：国际会展中心可拆分大宴会厅与专职音控师自动排班系统 (v4.0 新增)
-- 组装特性：Mixin 多重继承复用 + HierarchicalTreeLock(三级空间父子拓扑双向互斥)
--          + CompanionStaffAssigner(双轴场地+认证工程师自动排班捆绑) + PriceGuard(封顶规整)
-- ============================================================================

return Blueprint {
    id = "divisible_convention_center",
    name = "国际会展中心可拆分宴会厅与音控排班系统",
    description = "演示 Mixin 蓝图继承、全场大宴会厅 <-> 南北分厅 <-> 贵宾子厅三级空间拓扑互斥锁，以及主场地+同传音控师自动排班绑定",
    currency = "CNY",

    -- 一行复用标准营业时间基座与企业高可靠容灾候补闭环 Mixin！
    mixins = {
        MixinCatalog.StandardBusinessHours,
        MixinCatalog.EnterpriseReliability,
    },

    bricks = {
        Brick.BufferTime {
            pre_buffer_minutes = 0,
            post_buffer_minutes = 30, -- 会展撤场复位自动追加 30 分钟后置缓冲
            apply_if_duration_at_least = 120,
        },

        Brick.PricingAndDeposit {
            billing_mode = "per_hour",
            base_rate = 2000.0,
            weekend_multiplier = 1.25,
            role_discounts = { strategic_partner = 0.85 },
        },

        -- 核心积木 1：价格护栏 (单场封顶 ¥12,000，且自动四舍五入至整元)
        Brick.PriceGuard {
            min_price = 500.0,
            max_price = 12000.0,
            round_mode = "nearest_int",
        },

        -- 核心积木 2：双轴人员自动排班！预订宴会厅自动指派空闲且今日工时最低的音控工程师；
        -- 若预订带 #intl_summit 标签的国际峰会主厅，强制匹配具备 #simultaneous_interp 同传资质的工程师！
        Brick.CompanionStaffAssigner {
            resource_to_staff_kind = {
                banquet_hall = "av_engineer",
            },
            tag_to_staff_skill = {
                intl_summit = "simultaneous_interp",
            },
            charge_staff_rate = true,
        },

        -- 核心积木 3：三级空间父子拓扑互斥锁！
        -- 锁占 HALL-GRAND(全场) 会自动互斥阻塞 HALL-NORTH / HALL-SOUTH / ROOM-VIP-S1；
        -- 反之锁占任意子厅也会自动阻止同一时段整场包厅 (支持自动转入候补队列)！
        Brick.HierarchicalTreeLock {
            mode = "exclusive_subdivision",
            route_to_waitlist = true,
        },
    },

    resources = {
        -- Level 1 Root: 1200㎡ 无柱大宴会厅全场
        {
            id = "HALL-GRAND",
            name = "1F 紫荆无柱大宴会厅 (全场)",
            kind = "banquet_hall",
            parent_id = "",
            capacity = 600,
            tags = { "led_wall", "intl_summit" },
            attributes = { base_rate = 3000 }
        },
        -- Level 2 Partition A: 北翼分厅 (parent = HALL-GRAND)
        {
            id = "HALL-NORTH",
            name = "1F 紫荆宴会厅 · 北翼分厅",
            kind = "banquet_hall",
            parent_id = "HALL-GRAND",
            capacity = 300,
            tags = { "led_wall" },
            attributes = { base_rate = 1600 }
        },
        -- Level 2 Partition B: 南翼分厅 (parent = HALL-GRAND)
        {
            id = "HALL-SOUTH",
            name = "1F 紫荆宴会厅 · 南翼分厅",
            kind = "banquet_hall",
            parent_id = "HALL-GRAND",
            capacity = 300,
            tags = { "projector" },
            attributes = { base_rate = 1600 }
        },
        -- Level 3 Sub-partition: 南翼要客沙龙室 (parent = HALL-SOUTH)
        {
            id = "ROOM-VIP-S1",
            name = "1F 南翼要客闭门沙龙室 S1",
            kind = "banquet_hall",
            parent_id = "HALL-SOUTH",
            capacity = 80,
            tags = { "boardroom" },
            attributes = { base_rate = 800 }
        },

        -- 配套专职音控/同传工程师排班池
        {
            id = "STAFF-AV-01",
            name = "首席同传音控师 · 陆工",
            kind = "av_engineer",
            capacity = 1,
            tags = { "dante_audio", "simultaneous_interp" },
            attributes = { base_rate = 300, open_time = "08:30", close_time = "21:30" }
        },
        {
            id = "STAFF-AV-02",
            name = "高级舞美音控师 · 许工",
            kind = "av_engineer",
            capacity = 1,
            tags = { "dante_audio" },
            attributes = { base_rate = 200, open_time = "08:30", close_time = "21:30" }
        },
    },

    actors = {
        { id = "org_summit", name = "亚太金融科技峰会组委会", roles = { "organizer", "strategic_partner" }, credit_score = 115 },
        { id = "org_corp_a", name = "华星科技年度发布会", roles = { "organizer" }, credit_score = 100 },
        { id = "org_chamber", name = "青年企业家商会", roles = { "organizer" }, credit_score = 105 },
        { id = "venue_director", name = "会展中心排期总监", roles = { "admin" }, credit_score = 120 },
    }
}
