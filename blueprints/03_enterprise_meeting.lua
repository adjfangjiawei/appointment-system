-- ============================================================================
-- 蓝图 03：企业智能会议室与设备捆绑预约系统
-- 组装特性：多资源原子绑定(会议室+投屏/同传车) + 会后自动保洁缓冲 + 容量利用率防浪费 + 超长/高管室审批流
-- ============================================================================

return Blueprint {
    id = "enterprise_meeting",
    name = "企业智能会议室与移动设备联动预约系统",
    description = "支持『会议室+设备』复合资源原子锁、会后15分钟保洁复位缓冲、防少人占大房、超长会议及董事厅自动转入审批流",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "08:30",
            close_time = "21:30",
            weekdays = { 1, 2, 3, 4, 5 },
        },

        Brick.SlotGranularity {
            align_minutes = 15,
            min_duration_minutes = 30,
            max_duration_minutes = 300,
        },

        -- 核心积木：时长 >= 60 分钟的会议自动追加 15 分钟会后保洁/设备复位缓冲！
        Brick.BufferTime {
            pre_buffer_minutes = 0,
            post_buffer_minutes = 15,
            min_duration_to_apply = 60,
        },

        -- 核心积木：参会人数至少达到会议室容量的 40%，杜绝 2 人霸占 20 人大厅
        Brick.PartySizeMatcher {
            min_units = 2,
            max_units = 40,
            min_capacity_ratio = 0.4,
            enforce_ratio_peak_only = false,
        },

        Brick.QuotaLimiter {
            max_active_per_actor = 6,
            max_daily_bookings_per_actor = 4,
            max_daily_minutes_per_actor = 360,
            exempt_roles = { "admin", "director", "ea" },
        },

        -- 内部成本核算定价（用于跨部门预算结算单）
        Brick.PricingAndDeposit {
            billing_mode = "per_hour",
            base_rate = 0.0,
            role_discounts = { director = 1.0 },
        },

        Brick.LuaPolicy {
            name = "CostCenterAndEquipmentGuard",
            description = "校验跨楼层移动设备借用合规性，并自动打上部门成本中心结算标签",
            order = 60,
            on_pre_book = function(ctx)
                local dept = ctx.actor.attributes.department or "General"
                ctx.metadata.cost_center = "CC-" .. dept
                if #ctx.resources > 1 then
                    ctx:add_note("复合资源联动锁：主空间 + " .. tostring(#ctx.resources - 1) .. " 台配套设备同步占用 (归属 " .. ctx.metadata.cost_center .. ")")
                else
                    ctx:add_note("部门成本中心记账: " .. ctx.metadata.cost_center)
                end
                return true
            end
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 3,
            role_priority_bonus = { director = 50, manager = 20 },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
            exclusive = true, -- 整间排他独占模式：一场会议即锁定整间会议室
        },

        -- 核心积木：超过 120 分钟的长会或带 #boardroom 标签的高管厅自动进入待审批状态（总监免审直通）
        Brick.ApprovalWorkflow {
            duration_threshold_minutes = 120,
            trigger_tags = { "boardroom", "roadshow_hall" },
            auto_approve_roles = { "admin", "director" },
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 15,
            late_cancel_fee_ratio = 0.0,
            check_in_earliest_before_minutes = 15,
            check_in_grace_after_minutes = 15, -- 开会15分钟内未扫码签到自动释放会议室！
        },
    },

    fault_policy = {
        auto_reallocate = true,
        match_same_tags = false,
        compensate_priority = 20,
    },

    resources = {
        { id = "RM-SMALL-01", name = "3F 敏捷讨论室 (4人)", kind = "meeting_room", capacity = 4, tags = { "whiteboard" }, attributes = { base_rate = 40 } },
        { id = "RM-MED-01", name = "5F 太平洋会议室 (10人)", kind = "meeting_room", capacity = 10, tags = { "tv_screen", "video_conf" }, attributes = { base_rate = 100 } },
        { id = "RM-MED-02", name = "5F 大西洋会议室 (10人)", kind = "meeting_room", capacity = 10, tags = { "tv_screen", "video_conf" }, attributes = { base_rate = 100 } },
        { id = "RM-BOARD-01", name = "18F 环球董事会议厅 (20人)", kind = "meeting_room", capacity = 20, tags = { "boardroom", "video_conf", "led_wall" }, attributes = { base_rate = 300 } },
        { id = "EQ-PROJ-01", name = "4K 激光移动投屏车 #1", kind = "equipment", capacity = 1, tags = { "portable_av" }, attributes = { base_rate = 30 } },
        { id = "EQ-INTERP-01", name = "AI 同声传译主机套件 #1", kind = "equipment", capacity = 1, tags = { "translation" }, attributes = { base_rate = 80 } },
    },

    actors = {
        { id = "emp_alice", name = "Alice (研发工程师)", roles = { "employee" }, credit_score = 100, attributes = { department = "R&D" } },
        { id = "mgr_bob", name = "Bob (产品经理)", roles = { "employee", "manager" }, credit_score = 100, attributes = { department = "Product" } },
        { id = "dir_carol", name = "Carol (研发副总裁)", roles = { "employee", "director" }, credit_score = 120, attributes = { department = "Executive" } },
        { id = "adm_ops", name = "行政空间管理员", roles = { "admin" }, credit_score = 120, attributes = { department = "AdminOps" } },
    }
}
