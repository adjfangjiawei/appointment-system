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

## 2. 核心亮点与惊艳特性

1. **12 块正交 C++ 原生积木 + 无限扩展的 `Brick.LuaPolicy` 脚本积木**  
   每块积木自描述参数 Schema（支持反射导出目录）、独立配置、自由插拔，按阶段序号自动编排执行顺序。
2. **全链路微秒级可解释决策轨迹 (`DecisionTrace`)**  
   每一笔预约（无论是通过、加价、转入候补、触发审批还是被拒）都会生成完整的积木级耗时与原因轨迹，彻底告别“黑盒报错”。
3. **扫描线 (`Sweep-Line`) 精确并发容量账本：共享池 & 整间独占双模式**  
   精确计算半开区间 `[effective_start, effective_end)` 内的峰值并发占用量；同时支持**共享号池/团课模式 (`exclusive = false`)** 与**会议室/餐桌整间排他模式 (`exclusive = true`)**。
4. **复合资源原子捆绑预约 (`Atomic Resource Bundles`)**  
   一次请求同时原子锁定 `["5F太平洋会议室", "4K移动投屏车#1", "AI同传套件#1"]`，任一从属设备冲突则整体无副作用拒绝。
5. **前后缓冲时间无感注入 (`BufferTimeBrick`)**  
   自动为长会议追加 15 分钟保洁缓冲、为餐厅餐位追加 20 分钟翻台消杀时间，时间轴自动按扩张后的 `effective_end` 隔离冲突。
6. **智能优先级候补与级联自动递补 (`WaitlistBrick`)**  
   满员自动入列；一旦有人退订、审批驳回或超时爽约释放，引擎立即按 **优先级评分 + FIFO** 自动将最高优候补单转正（如急诊转诊自动优先递补）。
7. **故障自愈与无感自动换座 (`FaultSmartReallocator`)**  
   当管理员将某台电脑/房间标记为 `FAULT` 故障时，引擎自动扫描受影响的未来订单，寻找同类型、同硬件标签且时段空闲的健康资源完成**无感平滑迁移**；无替代资源时自动免责全额退赔并补偿优先级。
8. **签到宽限窗与爽约清扫器 (`NoShowSweeper` + 信用分奖惩闭环)**  
   超时未签到自动判 `NO_SHOW`、扣减用户守约信用分、释放座位并联动触发候补递补；按时履约完结自动奖励信用分。
9. **零外部重依赖内置 C++ HTTP Server 与可视化工作台 (`ChronoKit Studio`)**  
   自带暗黑工业风 Web 控制台：一键切换 5 大行业预设、透视积木流水线、**浏览器在线修改 Lua 代码毫秒级热重载**（内置 500,000 VM 指令配额防死循环沙箱）、测试智能时段推荐与故障换座演练。

---

## 3. 可复用积木全目录 (Brick Catalog)

| 序号 | 积木名称 (`Brick.*`) | 分类 | 核心职责与关键可配置项 |
| :---: | :--- | :---: | :--- |
| `10` | `OpeningHours` | Time | 每日营业起止时间 (`open_time`/`close_time`)、开放星期 (`weekdays`)、节假日闭馆日期 (`blackout_dates`) 及单资源覆盖 |
| `15` | `SlotGranularity` | Time | 时间刻度对齐 (`align_minutes`)、最短/最长时长、步长约束、固定课表节次 (`allowed_fixed_slots`) |
| `20` | `BookingWindow` | Time | 最少提前预约分钟数 (`min_advance_minutes`)、最大提前放号天数及 VIP/角色专属提前期 (`role_max_advance_days`) |
| `25` | `BufferTime` | Time | 前置布场 (`pre_buffer_minutes`)、后置翻台/保洁缓冲 (`post_buffer_minutes`)、触发缓冲的最短时长阈值 |
| `30` | `RoleAndCreditGate` | Access | 黑白名单角色、受限资源标签 RBAC 门禁 (`tag_required_roles`)、最低信用分准入、爽约扣分与完结奖励分 |
| `35` | `PartySizeMatcher` | Capacity | 预约人数上下限、空间最低容量利用率 (`min_capacity_ratio`，防止 2 人霸占 20 人大厅)、支持仅晚高峰生效 |
| `40` | `QuotaLimiter` | Quota | 在途最大单数、单日最大单数、单日/单周累计时长上限、同日同类别限约一次 (`unique_kind_per_day`)、角色配额倍率 |
| `50` | `PricingAndDeposit` | Pricing | 免费/按时/按人时/按人一口价/一口价、高峰溢价、周末溢价、早鸟折扣、标签附加费、角色折扣、比例/固定订金 |
| `60` | `LuaPolicy` | Lua | **图灵完备 Lua 5.4 自定义积木**：可任意读写价格、订金、优先级、缓冲期、审批标记、扩展元数据或自定义拦截 |
| `68` | `Waitlist` | Capacity | 候补开关、单时段最大排队上限、基础分、角色优先级加分 (`role_priority_bonus`)、信用分折算权重、退单自动递补 |
| `70` | `ConflictAndCapacity` | Capacity | 扫描线多资源原子容量校验、共享池/整间独占切换 (`exclusive`)、同用户时段自重叠拦截、满员自动路由至候补 |
| `90` | `ApprovalWorkflow` | Workflow | 超长时长阈值、大容量阈值、高金额阈值、敏感资源标签 (`trigger_tags`) 自动转入 `PENDING_APPROVAL`，支持高管免审 |
| `95` | `CancellationAndCheckIn` | Workflow | 免费退订提前量、临期比例违约金、不可退时限、禁退时限、最早签到时间窗及开场后最晚签到宽限期 (`grace`) |

---

## 4. 开箱即用的 5 套行业预约系统（纯 Lua 积木组装，零 C++ 改动）

仓库 `blueprints/` 目录下内置了 5 套跨度极大的完整行业蓝图：

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
