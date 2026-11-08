# ChronoKit (时序积木) 🧱

> **C++20 确定性高性能内核 × Lua 5.4 声明式积木组装与实时热更脚本引擎**  
> 像搭乐高积木一样，**零 C++ 代码修改**即可在几分钟内组装出一套面向任意行业的生产级预约系统。

---

## 1. 设计哲学：为什么是「C++20 + Lua 5.4 + 积木流水线」？

传统预约系统往往把「开放时段、人数校验、配额限制、计价折扣、候补排队、超时爽约、审批流、设备联动」硬编码在业务代码里——一旦从**高校机房**换成**三甲医院门诊**、**企业会议室**或**黑珍珠餐厅**，几乎需要推倒重写。

**ChronoKit** 采用与 **Redis + Lua / OpenResty / 现代游戏引擎** 同源的双层可组合架构：

```text
+-----------------------------------------------------------------------------------+
|                        Lua 5.4 声明式装配蓝图与动态策略层                          |
|   Blueprint {                                                                     |
|     bricks = { Brick.OpeningHours{..}, Brick.BufferTime{..}, Brick.LuaPolicy{..} }|
|     fault_policy = { auto_reallocate = true }, hooks = { on_waitlist_promoted }   |
|   }                                                                               |
+-----------------------------------------+-----------------------------------------+
                                          | 零停机热重载 (Hot-Reload) & 50万指令沙箱配额保护
                                          v
+-----------------------------------------------------------------------------------+
|                         C++20 可组合积木流水线 (BookingPipeline)                   |
|  [10 OpeningHours] -> [15 SlotGranularity] -> [20 BookingWindow] -> [25 Buffer]   |
|  -> [30 RoleCreditGate] -> [35 PartySizeMatcher] -> [40 QuotaLimiter]             |
|  -> [50 PricingDeposit] -> [60 LuaPolicy(自定义脚本)] -> [68 Waitlist]            |
|  -> [70 ConflictAndCapacity(扫描线原子锁)] -> [90 Approval] -> [95 CancelCheckIn] |
+-----------------------------------------+-----------------------------------------+
                                          | 全链路微秒级 DecisionTrace 决策轨迹追踪
                                          v
+-----------------------------------------------------------------------------------+
|                        C++20 确定性原子底座与可视化工作台                          |
|  * Sweep-Line 扫描线峰值容量索引  * 复合资源原子捆绑锁   * 故障智能无感换座器     |
|  * Memory / SQLite3(WAL) 双存储   * 领域事件总线 EventBus * 内置 HTTP & Studio UI  |
+-----------------------------------------------------------------------------------+
```

---

## 2. 核心亮点与惊艳特性 (v3.0 全面升级)

1. **18 块正交 C++20 原生积木 + 3 大高阶组合子 (`When` / `AnyOf` / `AllOf`) + `Prefab.*` 预制件模组 + `Brick.LuaPolicy`**  
   不仅能线性拼装原子积木，还支持**条件分支路由 (`Brick.When`)**、**逻辑或门 (`Brick.AnyOf` 失败自动回滚上下文快照)** 与 **可复用预制件套件 (`Prefab.AntiScalperSuite`)**，支持任意深度嵌套组合！
2. **全生命周期闭环：两阶段锁座支付 · 无损原子改签 · 合规权益转赠 · 提前签退释放与超时结算**  
   - **两阶段锁座 (`PENDING_PAYMENT`)**：锁定席位指定分钟数，`confirm_payment` 支付转正，超时未付自动释放并递补候补。
   - **无损原子改签 (`reschedule_booking`)**：新时段经全量积木预演失败时原单 100% 无损保留；成功迁出后原席位立即触发候补自动递补。
   - **点对点合规转赠 (`transfer_booking`)**：移交前对接收人执行全链路 RBAC、信用分、防黄牛与配额复核。
   - **签退结算 (`check_out_booking`)**：提前离场立即截断释放尾段容量并唤醒候补；超时霸占自动按倍率核算超时占用费。
3. **磨损均衡自动派单 (`ResourceLoadBalancer`) & 实时供需动态调价 (`DynamicSurgePricing`)**  
   自动将预约路由至同规格空闲且当日累计运行时长最低的节点（如超充桩/政务窗口），并根据实时并发负载率自动执行闲时谷电补贴或繁忙需量溢价。
4. **防黄牛熔断 (`AntiScalper`) + 前置流程依赖链 (`DependencyChain`) + 优惠券防重核销 (`CouponAndVoucher`)**  
   内置实名属性核验、单日高频退单熔断、先核税后过户/先培训后上机串联校验，以及满减券/折扣券/免时长券单人防重核销。
5. **全链路微秒级可解释决策轨迹 (`DecisionTrace`) + 一键导出纯 C++20 `EngineBuilder` 代码**  
   在 **ChronoKit Studio v3.0** 中在线可视化编排积木、查看资源排期甘特图，并支持一键将当前蓝图导出为可直接编译部署的纯 C++20 源码。

---

## 3. 可复用积木全目录 (Brick Catalog)

| 序号 | 积木名称 (`Brick.*`) | 分类 | 核心职责与关键可配置项 |
| :---: | :--- | :---: | :--- |
| `10` | `OpeningHours` | Time | 每日营业起止时间 (`open_time`/`close_time`)、开放星期 (`weekdays`)、节假日闭馆日期 (`blackout_dates`) 及单资源覆盖 |
| `15` | `SlotGranularity` | Time | 时间刻度对齐 (`align_minutes`)、最短/最长时长、步长约束、固定课表节次 (`allowed_fixed_slots`) |
| `20` | `BookingWindow` | Time | 最少提前预约分钟数 (`min_advance_minutes`)、最大提前放号天数及 VIP/角色专属提前期 (`role_max_advance_days`) |
| `25` | `BufferTime` | Time | 前置布场 (`pre_buffer_minutes`)、后置翻台/保洁缓冲 (`post_buffer_minutes`)、触发缓冲的最短时长阈值 |
| `28` | `AntiScalper` | Access | **防黄牛风控**：实名属性核验 (`require_verified_attribute`)、滑动窗口建单限流、单日频繁退号自动熔断 |
| `30` | `RoleAndCreditGate` | Access | 黑白名单角色、受限资源标签 RBAC 门禁 (`tag_required_roles`)、最低信用分准入、爽约扣分与完结奖励分 |
| `30` | `AnyOf` | Combinator | **高阶逻辑『或』门组合子**：任意子积木通过即放行（未通过分支自动回滚工作区修改） |
| `32` | `DependencyChain` | Workflow | **前置流程依赖链**：强制先完成/先预约前置资源类别（如先核税后过户、先安全培训后操作重型设备） |
| `35` | `PartySizeMatcher` | Capacity | 预约人数上下限、空间最低容量利用率 (`min_capacity_ratio`，防止 2 人霸占 20 人大厅)、支持仅晚高峰生效 |
| `40` | `QuotaLimiter` | Quota | 在途最大单数、单日最大单数、单日/单周累计时长上限、同日同类别限约一次 (`unique_kind_per_day`)、角色配额倍率 |
| `42` | `CooldownInterval` | Quota | **防连占冷却间隔**：强制同一资源或同类别资源相邻两次预约保留最小冷却间隔，防止拆单霸座/霸桩 |
| `50` | `PricingAndDeposit` | Pricing | 免费/按时/按人时/按人一口价/一口价、高峰溢价、周末溢价、早鸟折扣、标签附加费、角色折扣、比例/固定订金 |
| `50` | `AllOf` | Combinator | **高阶串联预制件模组 (`Prefab`)**：将多块子积木封装为可整体复用的复合能力包 |
| `54` | `DynamicSurgePricing` | Pricing | **实时供需动态调价**：实时测算同类池并发负载率，低负载错峰补贴折扣、高负载/满载自动需量溢价 |
| `55` | `When` | Combinator | **高阶条件分支组合子**：根据星期/角色/资源标签/人数/时段动态选择执行 `then_brick` 或 `else_brick` |
| `58` | `CouponAndVoucher` | Pricing | **优惠券与次卡核销**：支持满减券 (`amount_off`)、折扣券 (`discount_rate`)、免时长券 (`free_minutes`) 及单人防重核销 |
| `60` | `LuaPolicy` | Lua | **图灵完备 Lua 5.4 自定义积木**：可读取实时负载率/履约历史并动态改写价格、优先级、缓冲期、审批流或拦截 |
| `65` | `ResourceLoadBalancer` | Capacity | **磨损均衡智能派单**：自动在同规格空闲资源池中挑选当日累计负载最低的节点（支持 `always_balance` / `on_conflict_or_auto`） |
| `68` | `Waitlist` | Capacity | 候补开关、单时段最大排队上限、基础分、角色优先级加分 (`role_priority_bonus`)、信用分折算权重、退单自动递补 |
| `70` | `ConflictAndCapacity` | Capacity | 扫描线多资源原子容量校验、共享池/整间独占切换 (`exclusive`)、同用户时段自重叠拦截、满员自动路由至候补 |
| `90` | `ApprovalWorkflow` | Workflow | 超长时长阈值、大容量阈值、高金额阈值、敏感资源标签 (`trigger_tags`) 自动转入 `PENDING_APPROVAL`，支持高管免审 |
| `95` | `CancellationAndCheckIn` | Workflow | 免费退订提前量、临期比例违约金、不可退时限、禁退时限、最早签到时间窗及开场后最晚签到宽限期 (`grace`) |

---

## 4. 开箱即用的 8 套行业预约系统（纯 Lua 积木组装，零 C++ 改动）

仓库 `blueprints/` 目录下内置了 8 套跨度极大的完整行业蓝图：

1. **`blueprints/01_university_lab.lua` — 高校机房与 GPU 算力工作站预约系统**  
   `OpeningHours` + `RoleAndCreditGate(#gpu_a100 仅限科研人员, 信用分>=60)` + `PricingAndDeposit` + `LuaPolicy(20:00后低谷电价算力折上5折 & 毕设生优先级+25)` + `故障电脑无感自动换座`。
2. **`blueprints/02_hospital_clinic.lua` — 三甲医院专家门诊分时段挂号系统**  
   30 分钟多号源分时段容量池 + `QuotaLimiter(unique_kind_per_day=true 同科室限挂一号)` + `LuaPolicy(儿科年龄拦截、职工/居民医保实时统筹报销、70岁长者减免优待)` + `急诊转诊高优插队自动递补`。
3. **`blueprints/03_enterprise_meeting.lua` — 企业智能会议室与移动设备联动预约系统**  
   `exclusive=true` 整间排他锁 + **会议室+激光投屏+AI同传套件多资源原子锁** + `BufferTime(会后自动锁15m保洁复位)` + `PartySizeMatcher(>=40%利用率防浪费)` + `ApprovalWorkflow(超2小时或董事厅自动转待审批)`。
4. **`blueprints/04_fitness_studio.lua` — 精品健身房团课与私教预约系统**  
   多人燃脂团课池 + `早鸟48h享85折` + `晚高峰溢价` + `LuaPolicy(黑金无限卡团课免单 & 好友双人同行折上88折)` + `三级阶梯退课手续费`。
5. **`blueprints/05_fine_dining.lua` — 黑珍珠餐厅餐位与江景包厢预订系统**  
   周一店休 + `BufferTime(+20m翻台布草)` + `晚市黄金档>=60%餐桌人数匹配` + `按位订金` + `LuaPolicy(纪念日自动赠送侍酒师香槟礼遇 & 8人以上晚宴转主厨定制确认流)`。
6. **`blueprints/06_ev_supercharger.lua` — 800V 液冷超充站智能派桩与实时动态电价系统**  
   `ResourceLoadBalancer(磨损均衡自动派桩)` + `DynamicSurgePricing(闲时谷电75折 / 满载需量电价)` + `CooldownInterval(防拆单霸桩)` + `CouponAndVoucher(EVFREE15首充免15分钟券)` + `提前拔枪释放尾段递补`。
7. **`blueprints/07_gov_service_hall.lua` — 智慧政务大厅一窗联办与防黄牛风控系统**  
   `Prefab.AntiScalperSuite(实名校验 + 单日退号2次熔断预制件)` + `DependencyChain(不动产『先契税核定 -> 后产权过户』串联依赖)` + `Brick.When(高龄长者绿色通道条件分支配额)`。
8. **`blueprints/08_sports_badminton_arena.lua` — 综合体育馆羽毛球场与赛事级场馆预订系统**  
   `Brick.When + Brick.AnyOf(金卡会员 或 信用>=115高分球友均可解锁中心赛场)` + `两阶段15分钟锁座支付` + `无损原子改签` + `球友间场地合规转赠`。

---

## 5. 快速上手：两种积木组装方式

### 方式 A：使用 Lua 声明式蓝图组装（推荐，支持运行时热重载）

```lua
return Blueprint {
    id = "maker_space",
    name = "3D打印创客空间预约系统",
    bricks = {
        Brick.OpeningHours { open_time = "09:00", close_time = "21:00" },
        Brick.SlotGranularity { align_minutes = 30, min_duration_minutes = 60, max_duration_minutes = 240 },
        Brick.BufferTime { post_buffer_minutes = 10 }, -- 打印后10分钟清盘冷却缓冲
        Brick.PricingAndDeposit { billing_mode = "per_hour", base_rate = 15.0 },
        Brick.LuaPolicy {
            name = "StudentMakerSubsidy",
            on_pre_book = function(ctx)
                if ctx:actor_has_role("student") then
                    ctx.price = ctx.price * 0.5
                    ctx:add_note("创客学子耗材补贴 5 折")
                end
                return true
            end
        },
        Brick.Waitlist { enabled = true, auto_promote_on_cancel = true },
        Brick.ConflictAndCapacity { exclusive = true },
    },
    fault_policy = { auto_reallocate = true, match_same_tags = true },
}
```

### 方式 B：在 C++20 代码中使用 Fluent `EngineBuilder` 编程式组装

```cpp
#include "chronokit/engine/appointment_engine.hpp"
using namespace chronokit;

auto engine = EngineBuilder()
    .with_sqlite_storage("appointments.db") // 或 .with_memory_storage()
    .with_name("study_room", "24h 自习室预约")
    .add_typed_brick<SlotGranularityBrick>(JsonValue::object({{"align_minutes", 30}}))
    .add_typed_brick<RoleAndCreditGateBrick>(JsonValue::object({{"min_credit_score", 70}}))
    .add_typed_brick<ConflictAndCapacityBrick>(JsonValue::object({{"exclusive", true}}))
    .add_typed_brick<CancellationAndCheckInBrick>(JsonValue::object({{"check_in_grace_after_minutes", 15}}))
    .build();
```

---

## 6. 编译、测试与运行

### 环境依赖
- C++20 编译器（GCC 10+ / Clang 12+ / MSVC 2019+）
- CMake 3.16+
- Lua 5.4 与 SQLite3（若系统未安装，CMake 会通过 `FetchContent` 自动拉取源码编译，零手工配置）

### 一键构建与运行测试

```bash
# 1. 配置并编译
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 2. 运行完整自动化单元测试套件 (覆盖 5 大蓝图、扫描线算法、SQLite 持久化与沙箱保护)
ctest --test-dir build --output-on-failure -V

# 3. 运行命令行全场景演示与 C++ × Lua 吞吐量压测
./build/chronokit_cli

# 4. 启动内置 HTTP REST 引擎与 ChronoKit Studio 可视化工作台
./build/chronokit_server --port 8080 --db chronokit.db
# 浏览器访问 http://localhost:8080 即可交互体验！
```

---

## 7. 项目结构

```text
appointment-system/
├── include/chronokit/
│   ├── core/          # 时间日历工具、自研轻量 JSON、扫描线容量索引 TimelineIndex、事件总线
│   ├── storage/       # IStorage 抽象、MemoryStorage 与 ACID SqliteStorage (WAL 模式)
│   ├── bricks/        # IBrick 接口、自描述 Schema 与 12 块核心可复用 C++ 积木
│   ├── lua/           # Lua 5.4 沙箱虚拟机、指令配额防死循环保护、LuaScriptBrick 桥接
│   ├── engine/        # AppointmentEngine 核心编排器、候补递补、故障换座、时段推荐与 EngineBuilder
│   └── server/        # 零第三方依赖跨平台 C++20 HTTP REST 服务器
├── src/               # 核心库实现源码
├── blueprints/        # 5 套纯 Lua 组装的行业级预约系统蓝图
├── apps/
│   ├── cli_demo.cpp   # 命令行全场景演示与性能基准压测工具
│   ├── server_main.cpp# REST API 与 Studio 服务端入口
│   └── web_ui/        # ChronoKit Studio 可视化积木透视与在线 Lua 热更新工作台
├── tests/             # 54 项全链路单元与集成测试套件
└── docs/              # Lua DSL 完整语法与自定义积木开发手册
```

更多关于编写自定义 Lua 积木与事件钩子的细节请参阅 [`docs/LUA_DSL_REFERENCE.md`](docs/LUA_DSL_REFERENCE.md)。
