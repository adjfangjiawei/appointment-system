-- ============================================================================
-- 蓝图 01：高校机房与 GPU 算力工作站预约系统
-- 组装特性：单人独享机位 + GPU标签权限门禁 + 信用分准入 + 夜间算力补贴(Lua) + 故障自动换座
-- ============================================================================

return Blueprint {
    id = "university_lab",
    name = "高校机房与GPU算力座位预约系统",
    description = "面向计算机学院公共机房与AI实验室：支持标签权限门禁、签到超时释放扣信用分、候补递补及故障电脑无感自动换座",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "08:00",
            close_time = "22:00",
            weekdays = { 1, 2, 3, 4, 5, 6, 7 },
        },

        Brick.SlotGranularity {
            align_minutes = 60,
            min_duration_minutes = 60,
            max_duration_minutes = 240,
        },

        Brick.BookingWindow {
            min_advance_minutes = 0,
            max_advance_days = 7,
            role_max_advance_days = {
                researcher = 14,
                teacher = 30,
            },
        },

        Brick.RoleAndCreditGate {
            min_credit_score = 60,
            deduct_on_no_show = 15,
            reward_on_complete = 2,
            tag_required_roles = {
                gpu_a100 = { "researcher", "teacher" },
            },
        },

        Brick.QuotaLimiter {
            max_active_per_actor = 2,
            max_daily_bookings_per_actor = 2,
            max_daily_minutes_per_actor = 240,
            role_multiplier = {
                researcher = 2.0,
                teacher = 3.0,
            },
        },

        Brick.PricingAndDeposit {
            billing_mode = "per_hour",
            base_rate = 0.0, -- 普通教学机位免费开放
            tag_surcharges = {
                gpu_rtx4090 = 6.0,
                gpu_a100 = 20.0,
            },
            role_discounts = {
                teacher = 0.0,      -- 教师科研免机时费
                researcher = 0.5,   -- 研究生课题组 5 折
                student = 0.8,      -- 本科生 8 折
            },
        },

        -- 自定义 Lua 策略积木：夜间低谷电价算力补贴 & 毕业设计冲刺优先权
        Brick.LuaPolicy {
            name = "NightComputeSubsidy",
            description = "20:00后GPU算力额外5折补贴，应届毕业生自动获得候补优先权加成",
            order = 60,
            on_pre_book = function(ctx)
                if ctx.start_hour >= 20 and ctx.price > 0 then
                    ctx.price = ctx.price * 0.5
                    ctx:add_note("夜间低谷算力补贴：20:00后机时费折上5折")
                end
                if ctx:actor_has_role("graduating_senior") then
                    ctx.priority = ctx.priority + 25
                    ctx:add_note("毕业设计冲刺绿色通道：候补优先级 +25")
                end
                return true
            end
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 5,
            base_priority = 10,
            role_priority_bonus = {
                teacher = 50,
                researcher = 20,
            },
            credit_priority_weight = 0.1,
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 30,
            late_cancel_fee_ratio = 0.5,
            check_in_earliest_before_minutes = 15,
            check_in_grace_after_minutes = 15,
        },
    },

    fault_policy = {
        auto_reallocate = true,     -- 电脑标记故障时，自动迁移至同规格空闲电脑！
        match_same_tags = true,
        compensate_priority = 30,
    },

    resources = {
        { id = "PC-101", name = "基础机房A-01号机", kind = "lab_pc", capacity = 1, tags = { "win11", "c_cpp" } },
        { id = "PC-102", name = "基础机房A-02号机", kind = "lab_pc", capacity = 1, tags = { "win11", "c_cpp" } },
        { id = "PC-103", name = "基础机房A-03号机", kind = "lab_pc", capacity = 1, tags = { "win11", "c_cpp" } },
        { id = "GPU-201", name = "AI工作站B-01 (RTX4090)", kind = "gpu_node", capacity = 1, tags = { "linux", "gpu_rtx4090" } },
        { id = "GPU-202", name = "AI工作站B-02 (RTX4090)", kind = "gpu_node", capacity = 1, tags = { "linux", "gpu_rtx4090" } },
        { id = "GPU-301", name = "超算节点C-01 (A100-80G)", kind = "gpu_cluster", capacity = 1, tags = { "linux", "gpu_a100" } },
    },

    actors = {
        { id = "stu_xiaoming", name = "小明 (大二本科生)", roles = { "student" }, credit_score = 95 },
        { id = "stu_senior", name = "林学姐 (大四毕设生)", roles = { "student", "graduating_senior" }, credit_score = 100 },
        { id = "res_zhang", name = "张博士 (AI课题组)", roles = { "researcher" }, credit_score = 100 },
        { id = "prof_wang", name = "王教授 (实验室主任)", roles = { "teacher", "admin" }, credit_score = 120 },
        { id = "stu_lowcredit", name = "李同学 (多次爽约)", roles = { "student" }, credit_score = 45 },
    },

    hooks = {
        on_waitlist_promoted = function(ev)
            log("[机房通知] 候补转正提醒：订单 " .. ev.booking_id .. " 已自动锁定机位 " .. ev.resource_id)
        end,
        on_resource_fault_reallocated = function(ev)
            log("[智能运维] 故障无感换座：订单 " .. ev.booking_id .. " 已自动移至新机位 " .. ev.resource_id)
        end
    }
}
