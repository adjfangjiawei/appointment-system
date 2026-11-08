-- ============================================================================
-- 蓝图 02：三甲医院专家门诊分时段挂号系统
-- 组装特性：分时段号池(多容量) + 同科室单日限挂一号 + 急诊/高龄分诊优先候补 + 医保动态结算(Lua)
-- ============================================================================

return Blueprint {
    id = "hospital_clinic",
    name = "三甲医院专家门诊分时段挂号系统",
    description = "多号源分时段容量池，支持同日同专科防重挂号、医保统筹实时抵扣、70岁长者优待及退号智能优先级递补",
    currency = "CNY",

    bricks = {
        Brick.OpeningHours {
            open_time = "08:00",
            close_time = "17:30",
            weekdays = { 1, 2, 3, 4, 5, 6 }, -- 周一至周六门诊
        },

        Brick.SlotGranularity {
            align_minutes = 30,
            min_duration_minutes = 30,
            max_duration_minutes = 30, -- 标准 30 分钟分时段号源池
        },

        Brick.BookingWindow {
            min_advance_minutes = 15,
            max_advance_days = 14,
        },

        Brick.QuotaLimiter {
            max_active_per_actor = 3,
            max_daily_bookings_per_actor = 2,
            max_daily_minutes_per_actor = 120,
            unique_kind_per_day = true, -- 核心规则：同一患者同一天同一专科仅限挂一个号！
        },

        Brick.PricingAndDeposit {
            billing_mode = "flat",
            base_rate = 50.0,
            tag_surcharges = {
                chief_physician = 50.0,      -- 主任医师专家号 ¥100
                academician_clinic = 250.0,  -- 特需名医门诊 ¥300
            },
        },

        -- 自定义 Lua 策略积木：医保统筹报销计算与高龄长者关怀策略
        Brick.LuaPolicy {
            name = "MedicalInsuranceAndTriage",
            description = "自动识别患者年龄与医保类型，计算统筹减免并赋予高龄/急诊转诊高优先级",
            order = 60,
            on_pre_book = function(ctx)
                local age = tonumber(ctx.actor.attributes.age) or 30
                local ins = ctx.actor.attributes.insurance or "self_pay"

                if ctx:resource_has_tag("pediatrics") and age >= 18 then
                    return ctx:reject("PEDIATRICS_AGE_LIMIT", "儿科专家门诊仅面向 18 周岁以下患儿开放")
                end

                if age >= 70 then
                    ctx.priority = ctx.priority + 60
                    ctx.price = math.max(0, ctx.price - 20)
                    ctx:add_note("70岁以上高龄长者优待：减免诊查费 ¥20，候补优先级 +60")
                end

                if ins == "employee_medical" and not ctx:resource_has_tag("academician_clinic") then
                    ctx.price = math.floor(ctx.price * 0.3 * 100) / 100
                    ctx:add_note("职工医保实时统筹报销 70%，个人自付 ¥" .. tostring(ctx.price))
                elseif ins == "resident_medical" and not ctx:resource_has_tag("academician_clinic") then
                    ctx.price = math.floor(ctx.price * 0.5 * 100) / 100
                    ctx:add_note("居民医保实时统筹报销 50%，个人自付 ¥" .. tostring(ctx.price))
                end

                return true
            end
        },

        Brick.Waitlist {
            enabled = true,
            max_waitlist_per_slot = 10,
            base_priority = 10,
            role_priority_bonus = {
                emergency_referral = 120,
                elderly = 40,
            },
            auto_promote_on_cancel = true,
        },

        Brick.ConflictAndCapacity {
            allow_actor_overlap = false,
            route_full_to_waitlist = true,
        },

        Brick.CancellationAndCheckIn {
            free_cancel_before_minutes = 60,
            late_cancel_fee_ratio = 0.2,
            check_in_earliest_before_minutes = 60,
            check_in_grace_after_minutes = 20,
        },
    },

    resources = {
        { id = "DOC-CARDIO-01", name = "心内科-林主任医师 (每半小时4号)", kind = "cardiology", capacity = 4, tags = { "chief_physician" } },
        { id = "DOC-CARDIO-02", name = "心内科-陈副主任医师 (每半小时6号)", kind = "cardiology", capacity = 6, tags = {} },
        { id = "DOC-NEURO-01", name = "神经内科-周知名专家特需门诊", kind = "neurology", capacity = 2, tags = { "academician_clinic" } },
        { id = "DOC-PED-01", name = "儿科呼吸专病-赵主任", kind = "pediatrics", capacity = 5, tags = { "chief_physician", "pediatrics" } },
    },

    actors = {
        { id = "pat_young", name = "陈先生 (32岁/职工医保)", roles = { "patient" }, credit_score = 100, attributes = { age = 32, insurance = "employee_medical" } },
        { id = "pat_grandma", name = "刘奶奶 (76岁/居民医保)", roles = { "patient", "elderly" }, credit_score = 100, attributes = { age = 76, insurance = "resident_medical" } },
        { id = "pat_referral", name = "赵先生 (基层急诊转诊)", roles = { "patient", "emergency_referral" }, credit_score = 100, attributes = { age = 55, insurance = "employee_medical" } },
        { id = "pat_child", name = "朵朵 (6岁儿童)", roles = { "patient" }, credit_score = 100, attributes = { age = 6, insurance = "resident_medical" } },
        { id = "nurse_admin", name = "门诊分诊台护士长", roles = { "admin" }, credit_score = 100, attributes = { age = 38 } },
    },

    hooks = {
        on_waitlist_promoted = function(ev)
            log("[门诊导诊] 退号递补成功：候补患者 " .. ev.actor_id .. " 已自动获配专家号 " .. ev.booking_id)
        end
    }
}
