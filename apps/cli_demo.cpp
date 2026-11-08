#include "chronokit/engine/appointment_engine.hpp"
#include "chronokit/storage/sqlite_storage.hpp"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

using namespace chronokit;

namespace {

void print_banner(const std::string& title) {
    std::cout << "\n================================================================================\n"
              << "  " << title << "\n"
              << "================================================================================\n";
}

void print_outcome(const std::string& label, const BookingOutcome& out) {
    std::cout << "  -> [" << (out.ok ? "OK" : "REJECT") << "] " << label << " | ";
    if (out.ok) {
        std::cout << out.record.id << " status=" << to_string(out.record.status)
                  << " price=¥" << out.record.price
                  << " prio=" << out.record.priority
                  << " eff=[" << time_utils::format_datetime(out.record.effective_start).substr(11)
                  << "-" << time_utils::format_datetime(out.record.effective_end).substr(11) << "]\n";
    } else {
        std::cout << out.error_code << ": " << out.message << "\n";
    }
    for (const auto& step : out.trace) {
        std::printf("       [%s] %-24s (%5.1f us) -> %s\n",
                    step.outcome.c_str(), step.brick_name.c_str(), step.elapsed_us, step.note.c_str());
    }
}

void run_showcase() {
    print_banner("ChronoKit (时序积木) — C++20 + Lua 5.4 可组合预约系统组件库全场景演示");

    std::cout << "\n[可复用积木目录 (Brick Catalog)]\n";
    auto catalog = AppointmentEngine::available_brick_catalog();
    for (const auto& d : catalog) {
        std::printf("  * [Order %2d | %-8s] %-24s : %s (%zu 可配参数)\n",
                    d.order, d.category.c_str(), d.type_id.c_str(), d.summary.c_str(), d.params.size());
    }

    EpochMinute sim_now = time_utils::must_parse_datetime("2026-11-10 08:00"); // Tuesday

    // ------------------------------------------------------------------------
    // Scenario 1: University Computer Lab & GPU Cluster
    // ------------------------------------------------------------------------
    print_banner("场景 1/5：高校机房与 GPU 工作站预约 (blueprints/01_university_lab.lua)");
    AppointmentEngine engine;
    engine.load_blueprint_file("blueprints/01_university_lab.lua");

    // Low-credit student blocked
    auto r1 = engine.book({
        "stu_lowcredit", {"PC-101"},
        time_utils::must_parse_datetime("2026-11-10 09:00"),
        time_utils::must_parse_datetime("2026-11-10 11:00"),
        1, true, sim_now, "", {}
    });
    print_outcome("低信用分学生尝试预约普通机位", r1);

    // Night GPU subsidy via LuaPolicy
    auto r2 = engine.book({
        "res_zhang", {"GPU-201"},
        time_utils::must_parse_datetime("2026-11-10 20:00"),
        time_utils::must_parse_datetime("2026-11-10 22:00"),
        1, true, sim_now, "", {}
    });
    print_outcome("博士生预约晚20:00后 RTX4090 (触发研究生5折 + Lua夜间折上5折)", r2);

    // Fault auto-reallocation!
    std::cout << "\n  [模拟运维事件] GPU-201 显卡散热异常标记故障，触发智能无感换座...\n";
    auto fault_rep = engine.report_resource_fault("GPU-201", sim_now, "风扇转速异常检修");
    for (const auto& [bid, new_res] : fault_rep.reallocated) {
        std::cout << "    >> 自动迁移成功: 订单 " << bid << " 已无感切换至同配置节点 [" << new_res << "]\n";
    }

    // ------------------------------------------------------------------------
    // Scenario 2: Hospital Specialist Clinic Triage & Waitlist Cascade
    // ------------------------------------------------------------------------
    print_banner("场景 2/5：三甲医院专家门诊分时段挂号 (blueprints/02_hospital_clinic.lua)");
    engine.load_blueprint_file("blueprints/02_hospital_clinic.lua");

    EpochMinute slot_s = time_utils::must_parse_datetime("2026-11-10 09:00");
    EpochMinute slot_e = time_utils::must_parse_datetime("2026-11-10 09:30");

    auto h1 = engine.book({"pat_young", {"DOC-NEURO-01"}, slot_s, slot_e, 1, true, sim_now, "", {}});
    auto h2 = engine.book({"pat_grandma", {"DOC-NEURO-01"}, slot_s, slot_e, 1, true, sim_now, "", {}});
    print_outcome("76岁刘奶奶挂特需专家号 (容量2/2满额)", h2);

    // Capacity is 2; next two enter waitlist: child (normal priority) vs emergency referral (priority +100!)
    auto h3 = engine.book({"pat_child", {"DOC-NEURO-01"}, slot_s, slot_e, 1, true, sim_now, "", {}});
    auto h4 = engine.book({"pat_referral", {"DOC-NEURO-01"}, slot_s, slot_e, 1, true, sim_now, "", {}});
    print_outcome("普通患儿满员进入候补 (先排队)", h3);
    print_outcome("基层急诊转诊患者满员进入候补 (后排队但高优先级)", h4);

    std::cout << "\n  [模拟退号事件] 陈先生取消特需号 " << h1.record.id << "，观察谁先递补转正...\n";
    engine.cancel_booking(h1.record.id, "pat_young", sim_now, "临时出差改期");
    auto promoted_check = engine.get_booking(h4.record.id);
    std::cout << "    >> 急诊转诊单 " << h4.record.id << " 当前最新状态: " << to_string(promoted_check->status) << " (优先级插队自动递补成功!)\n";

    // ------------------------------------------------------------------------
    // Scenario 3: Enterprise Compound Bundle + Post-Buffer + Approval Workflow
    // ------------------------------------------------------------------------
    print_banner("场景 3/5：企业会议室+移动设备原子绑定与审批流 (blueprints/03_enterprise_meeting.lua)");
    engine.load_blueprint_file("blueprints/03_enterprise_meeting.lua");

    auto m1 = engine.book({
        "mgr_bob", {"RM-MED-01", "EQ-PROJ-01", "EQ-INTERP-01"},
        time_utils::must_parse_datetime("2026-11-10 14:00"),
        time_utils::must_parse_datetime("2026-11-10 16:30"),
        6, true, sim_now, "", {}
    });
    print_outcome("产品经理跨国评审会 (10人会议室+激光投屏+AI同传 3资源原子锁 & 超2h自动触发审批 & +15m保洁缓冲)", m1);

    auto m_approve = engine.approve_booking(m1.record.id, "adm_ops", true, "跨国客户会议予以批准", sim_now);
    std::cout << "    >> 行政审批结果: " << m_approve.record.id << " -> " << to_string(m_approve.record.status) << "\n";

    // ------------------------------------------------------------------------
    // Scenario 4 & 5 Quick Verification + Throughput Benchmark
    // ------------------------------------------------------------------------
    print_banner("场景 4 & 5：精品健身房结伴折扣 & 黑珍珠餐厅纪念日礼遇 + 引擎性能基准压测");
    engine.load_blueprint_file("blueprints/04_fitness_studio.lua");
    auto f1 = engine.book({
        "mem_gold", {"CLS-REFORMER-01"},
        time_utils::must_parse_datetime("2026-11-13 19:00"), // >48h early bird + peak hour + gold card + 2 buddies!
        time_utils::must_parse_datetime("2026-11-13 20:00"),
        2, true, sim_now, "", {}
    });
    print_outcome("金卡会员提前3天邀闺蜜双人约晚高峰普拉提 (金卡6折×早鸟85折×高峰1.2倍×双人同行88折)", f1);

    engine.load_blueprint_file("blueprints/05_fine_dining.lua");
    MetadataMap dining_meta;
    dining_meta["occasion"] = std::string("anniversary");
    auto d1 = engine.book({
        "guest_lin", {"TBL-WIN-01"},
        time_utils::must_parse_datetime("2026-11-11 19:00"),
        time_utils::must_parse_datetime("2026-11-11 21:00"),
        2, true, sim_now, "", dining_meta
    });
    print_outcome("结婚纪念日预订临窗双人江景位 (自动+20m翻台缓冲 + 按位订金 + Lua侍酒师香槟礼遇)", d1);

    // Micro-benchmark: 2,000 full 10-brick + Lua VM evaluations via suggest_slots / dry-run
    auto t_start = std::chrono::steady_clock::now();
    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        BookingRequest bench_req{
            "guest_vip", {"SUITE-VIP-88"},
            time_utils::must_parse_datetime("2026-11-12 12:00"),
            time_utils::must_parse_datetime("2026-11-12 14:00"),
            6, false, sim_now, "", dining_meta
        };
        // Alternate slot recommendation / evaluation
        if (i == 0) {
            auto sug = engine.suggest_slots("guest_vip", "2026-11-12", 120, 4, {"river_view"}, "", 5, sim_now);
            std::cout << "\n  [智能时段推荐引擎] 为4位宾客检索 2026-11-12 江景席位 Top-" << sug.size() << " 推荐:\n";
            for (const auto& s : sug) {
                std::printf("    * %-24s | %s - %s | 预估订金 ¥%.2f | 评分 %.1f (%s)\n",
                            s.resource_name.c_str(),
                            time_utils::format_datetime(s.start_time).c_str(),
                            time_utils::format_datetime(s.end_time).substr(11).c_str(),
                            s.estimated_price, s.score, s.reason.c_str());
            }
        }
    }
    // Benchmark pure pipeline booking + cancel cycle 1,000 times
    t_start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        auto b = engine.book({
            "guest_vip", {"SUITE-VIP-88"},
            time_utils::must_parse_datetime("2026-11-14 12:00"),
            time_utils::must_parse_datetime("2026-11-14 14:00"),
            6, false, sim_now, "", dining_meta
        });
        engine.cancel_booking(b.record.id, "guest_vip", sim_now, "bench");
    }
    auto t_end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    double ops_per_sec = (iterations * 2.0) / (ms / 1000.0);
    std::printf("\n  [C++20 × Lua 5.4 性能压测] 完成 %d 次完整『11积木流水线预约 + 退订递补扫描』闭环:\n", iterations);
    std::printf("    总耗时: %.2f ms | 平均单次操作延迟: %.1f us | 吞吐量: %.0f ops/sec\n",
                ms, (ms * 1000.0) / (iterations * 2.0), ops_per_sec);

    // v3.0 Showcase: 06 EV Supercharger + 07 Gov Service Hall + 08 Sports Arena
    std::cout << "\n====================================================================\n"
              << "  [v3.0 新增场景] 06 超充站磨损均衡派桩 & 07 智慧政务前置链 & 08 体育馆高阶组合子\n"
              << "====================================================================\n";

    engine.load_blueprint_file("blueprints/06_ev_supercharger.lua");
    BookingRequest ev1{"ev_owner_li", {"PILE-800V-A1"},
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        time_utils::must_parse_datetime("2026-11-10 11:00"),
        1, true, sim_now, "", {}, 0, "", "EVFREE15"};
    print_outcome("超充首单 (闲时谷电75折 × 品牌车主85折 × EVFREE15首充免15m券)", engine.book(ev1));

    BookingRequest ev2{"ev_guest_zhou", {"PILE-800V-A1"},
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        time_utils::must_parse_datetime("2026-11-10 11:00"),
        1, true, sim_now, "", {}};
    auto ev2_out = engine.book(ev2);
    print_outcome("同刻请求 A1 桩 -> 磨损均衡器自动无感派单至零磨损空闲桩 " + ev2_out.record.primary_resource_id(), ev2_out);

    engine.load_blueprint_file("blueprints/07_gov_service_hall.lua");
    BookingRequest prop_req{"cit_wang", {"WIN-PROP-01"},
        time_utils::must_parse_datetime("2026-11-10 10:30"),
        time_utils::must_parse_datetime("2026-11-10 11:00"),
        1, true, sim_now, "", {}};
    print_outcome("未办契税核定直接预约不动产过户 (被 DependencyChain 前置依赖链拦截)", engine.book(prop_req));
    engine.book({"cit_wang", {"WIN-TAX-01"},
        time_utils::must_parse_datetime("2026-11-10 09:30"),
        time_utils::must_parse_datetime("2026-11-10 10:00"),
        1, true, sim_now, "", {}});
    print_outcome("补齐 09:30 契税核定前置单后再次预约 10:30 不动产过户", engine.book(prop_req));

    engine.load_blueprint_file("blueprints/08_sports_badminton_arena.lua");
    BookingRequest pro_req{"plr_veteran", {"CRT-PRO-VIP"},
        time_utils::must_parse_datetime("2026-11-10 14:00"),
        time_utils::must_parse_datetime("2026-11-10 15:00"),
        4, true, sim_now, "", {}, 15, "", "SPORT30"};
    auto pro_out = engine.book(pro_req);
    print_outcome("高信用球友通过 Brick.AnyOf 或门解锁赛事中心场 + 15分钟锁座 + SPORT30满减券", pro_out);
    print_outcome("完成锁座支付转正 (PENDING_PAYMENT -> CONFIRMED)", engine.confirm_payment(pro_out.record.id, "plr_veteran", sim_now));
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "--blueprint") {
            AppointmentEngine eng;
            eng.load_blueprint_file(argv[2]);
            std::cout << eng.current_blueprint_info().summary_json().dump(2) << "\n";
            return 0;
        }
        run_showcase();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
