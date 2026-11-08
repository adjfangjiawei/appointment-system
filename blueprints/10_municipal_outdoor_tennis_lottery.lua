-- ============================================================================
-- 蓝图 10：市民网球中心公证摇号抽签与极端天气室内外联动系统 (v4.0 新增)
-- 组装特性：MixinCatalog.PublicVenueBase + EnvironmentalRule(暴雨/大风/雾霾室外熔断)
--          + Brick.Unless(市队集训配额豁免守卫) + MembershipPass(全民健身次卡抵扣与未中签自动退卡)
--          + LotteryAndFairQueue(周末热门场次防外挂公证摇号池)
-- ============================================================================

return Blueprint {
    id = "municipal_tennis_lottery",
    name = "市民网球中心公证摇号与气象联动预约系统",
    description = "演示暴雨/大风自动熔断室外红土场并放行室内气膜馆、全民健身次卡自动核销与退回、市队豁免守卫及周末热门场防黄牛公证摇号",
    currency = "CNY",

    mixins = {
        MixinCatalog.PublicVenueBase,
    },

    bricks = {
        -- 1. 气象与环境安全感知联动积木：室外球场(#outdoor)遇暴雨/台风/雷电、7级以上大风或 AQI>200 自动熔断！
        Brick.EnvironmentalRule {
            outdoor_tag = "outdoor",
            blocked_weather = { "rainstorm", "typhoon", "lightning", "hail" },
            advisory_weather = { "light_rain", "heatwave" },
            max_wind_level = 6,
            max_aqi = 200,
        },

        -- 2. 高阶豁免守卫组合子 Brick.Unless：除非是市青少年集训队(city_team)或管理员，否则执行单日最多 2 单配额限制！
        Brick.Unless {
            name = "CityTeamQuotaExemption",
            order = 40,
            condition = {
                actor_roles = { "city_team", "admin" },
            },
            brick = Brick.QuotaLimiter {
                max_active_per_actor = 3,
                max_daily_bookings_per_actor = 2,
                max_daily_minutes_per_actor = 120,
            }
        },

        Brick.PricingAndDeposit {
            billing_mode = "per_hour",
            base_rate = 60.0,
            weekend_multiplier = 1.5,
            tag_surcharges = { indoor_dome = 40.0 },
        },

        -- 3. 全民健身次卡抵扣积木：按小时自动核销 tennis_pass_credits 免现金；退订或摇号未中签自动原路退回卡次！
        Brick.MembershipPass {
            credit_attr = "tennis_pass_credits",
            cost_mode = "per_hour",
            credits_per_unit = 1,
            require_pass = false,
            refund_on_free_cancel = true,
        },

        Brick.LuaPolicy {
            name = "RainyDayIndoorSubsidy",
            description = "感知实时气象(ctx.env.weather)：当室外因暴雨停用时，自动补贴室内气膜馆 ¥20 惠民运动金",
            order = 60,
            on_pre_book = function(ctx)
                if ctx.env and ctx.env.weather == "rainstorm" and ctx:resource_has_tag("indoor_dome") and ctx.price > 0 then
                    ctx:apply_discount(20.0, "雨天全民健身关怀补贴：室内气膜馆立减 ¥20")
                end
                return true
            end
        },

        -- 4. 防外挂公证摇号池：周六日场次且距开场 >= 24 小时统一进入 LOTTERY_PENDING 公平抽签！
        Brick.LotteryAndFairQueue {
            min_lead_hours_for_lottery = 24,
            only_weekends = true,
            max_applicants_per_slot = 20,
            role_weight_bonus = { model_citizen = 40 },
        },
    },

    resources = {
        { id = "TNS-CLAY-OUT1", name = "1号 室外法网红土网球场", kind = "tennis_court", capacity = 4, tags = { "outdoor", "clay" }, attributes = { base_rate = 60 } },
        { id = "TNS-HARD-OUT2", name = "2号 室外澳网硬地网球场", kind = "tennis_court", capacity = 4, tags = { "outdoor", "hard" }, attributes = { base_rate = 50 } },
        { id = "TNS-DOME-IN1", name = "V1 全天候恒温室内气膜馆", kind = "tennis_court", capacity = 4, tags = { "indoor_dome" }, attributes = { base_rate = 80 } },
    },

    actors = {
        { id = "cit_pass_holder", name = "周女士 (持10次健身年卡市民)", roles = { "citizen" }, credit_score = 105, attributes = { tennis_pass_credits = 10 } },
        { id = "cit_honor", name = "吴老师 (五星守约市民/持2次卡)", roles = { "citizen", "model_citizen" }, credit_score = 130, attributes = { tennis_pass_credits = 2 } },
        { id = "cit_cash", name = "郑先生 (普通现金散客)", roles = { "citizen" }, credit_score = 100, attributes = { tennis_pass_credits = 0 } },
        { id = "coach_team", name = "市青训队张教练", roles = { "citizen", "city_team" }, credit_score = 115, attributes = { tennis_pass_credits = 0 } },
    }
}
