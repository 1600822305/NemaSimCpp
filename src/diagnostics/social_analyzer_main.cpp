// ================================================================
// Step 122: Social Feeding Analyzer — Multi-Worm Aggregation Diagnostic
//
// Simulates N worms sharing an environment to test social aggregation.
// Social interactions through O₂ consumption by nearby worms →
// URX/RMG hub-and-spoke circuit responds differentially in
// N2 (npr-1 215V, solitary) vs Hawaiian (npr-1 lf, social).
//
// Usage:
//   social_analyzer --worms 5 --duration 120 --strain N2
//   social_analyzer --worms 5 --duration 120 --strain Hawaiian
//   social_analyzer --worms 8 --duration 180 --compare
//
// REF: Ding 2019 eLife, Macosko 2009 Nature, Laurent 2015 eLife
// ================================================================

#include "../simulation/multi_worm_simulation.h"
#include "../core/logger.h"
#include <iostream>
#include <iomanip>
#include <string>
#include <cstdlib>
#include <cmath>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#undef ERROR  // Windows macro conflicts with LogLevel::ERROR
#endif

using namespace celegans;

// Generate starting positions in a ring around food center
static std::vector<MultiWormSimulation::WormConfig> make_configs(
    int num_worms, double npr1_rmg, unsigned int base_seed,
    Vector2d center = {40.0, 35.0}, double spread = 3.0)
    // center at food EDGE (5mm from food source at 35,35) where O₂ ~15%
    // spread 1.5mm → worms start within proximity radius
{
    std::vector<MultiWormSimulation::WormConfig> configs(num_worms);
    for (int i = 0; i < num_worms; ++i) {
        double angle = 2.0 * 3.14159265 * i / num_worms;
        configs[i].start_position = {
            center.x + spread * std::cos(angle),
            center.y + spread * std::sin(angle)
        };
        configs[i].start_heading = angle + 3.14159265;  // face toward center
        configs[i].seed = base_seed + i * 17;  // distinct seeds
        configs[i].npr1_rmg = npr1_rmg;
    }
    return configs;
}

static void print_metrics_header() {
    std::cout << std::setw(8) << "Time(s)"
              << std::setw(10) << "MeanNND"
              << std::setw(10) << "MeanSpd"
              << std::setw(10) << "ClustFrac"
              << std::setw(8) << "Clust#"
              << std::setw(10) << "ClustSz"
              << std::setw(10) << "PairCorr"
              << "\n";
    std::cout << std::string(66, '-') << "\n";
}

static void print_metrics_row(const MultiWormSimulation::SocialMetrics& m) {
    std::cout << std::fixed
              << std::setw(8) << std::setprecision(1) << m.time_s
              << std::setw(10) << std::setprecision(2) << m.mean_nnd
              << std::setw(10) << std::setprecision(3) << m.mean_speed
              << std::setw(10) << std::setprecision(1) << (m.cluster_fraction * 100) << "%"
              << std::setw(8) << m.num_clusters
              << std::setw(10) << std::setprecision(1) << m.mean_cluster_size
              << std::setw(10) << std::setprecision(2) << m.pair_correlation_short
              << "\n";
}

static void run_strain(const std::string& name, int num_worms, double duration,
                       double npr1_val, unsigned int seed,
                       std::vector<MultiWormSimulation::Snapshot>& out_snapshots)
{
    std::cout << "\n========================================\n";
    std::cout << "  " << name << " strain  (NPR-1 RMG = "
              << npr1_val << " pA, " << num_worms << " worms)\n";
    std::cout << "========================================\n\n";

    auto configs = make_configs(num_worms, npr1_val, seed);
    MultiWormSimulation sim;
    sim.initialize(configs);

    std::cout << "  起始位置:\n";
    for (int i = 0; i < num_worms; ++i) {
        std::cout << "    Worm " << i << ": ("
                  << std::fixed << std::setprecision(1)
                  << configs[i].start_position.x << ", "
                  << configs[i].start_position.y << ")\n";
    }
    std::cout << "\n  运行仿真 " << duration << "s ...\n\n";

    // Snapshot every 5 seconds
    double snapshot_dt = 5.0;
    print_metrics_header();

    sim.run(duration, snapshot_dt);

    // Print snapshots
    for (const auto& snap : sim.snapshots()) {
        print_metrics_row(snap.metrics);
    }

    out_snapshots = sim.snapshots();

    // Final summary
    if (!sim.snapshots().empty()) {
        const auto& final_m = sim.snapshots().back().metrics;
        std::cout << "\n--- 最终状态 (t=" << duration << "s) ---\n";
        std::cout << "  平均最近邻距离:  " << std::setprecision(2) << final_m.mean_nnd << " mm\n";
        std::cout << "  聚集比例:        " << std::setprecision(1) << (final_m.cluster_fraction * 100) << "%\n";
        std::cout << "  聚群数:          " << final_m.num_clusters << "\n";
        std::cout << "  平均聚群大小:    " << std::setprecision(1) << final_m.mean_cluster_size << "\n";
        std::cout << "  短程对关联:      " << std::setprecision(2) << final_m.pair_correlation_short << "\n";
        std::cout << "  平均速度:        " << std::setprecision(3) << final_m.mean_speed << " mm/s\n";

        // Worm positions
        std::cout << "\n  最终位置:\n";
        for (int i = 0; i < sim.num_worms(); ++i) {
            auto p = sim.worm(i).body().get_head_position();
            auto spd = sim.worm(i).body().get_speed();
            std::cout << "    Worm " << i << ": ("
                      << std::setprecision(1) << p.x << ", " << p.y
                      << ")  speed=" << std::setprecision(3) << spd << " mm/s\n";
        }
    }
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Logger::instance().set_level(LogLevel::ERROR);

    int num_worms = 5;
    double duration = 60.0;
    unsigned int seed = 42;
    std::string strain = "compare";  // default: compare both
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--worms" && i + 1 < argc) {
            num_worms = std::atoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--strain" && i + 1 < argc) {
            strain = argv[++i];
        } else if (arg == "--compare") {
            strain = "compare";
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: social_analyzer [options]\n\n"
                      << "社会觅食/聚集行为分析工具\n\n"
                      << "Options:\n"
                      << "  --worms <N>       虫数量 (默认: 5)\n"
                      << "  --duration <sec>  仿真时长 (默认: 60)\n"
                      << "  --seed <n>        基础随机种子 (默认: 42)\n"
                      << "  --strain <name>   N2 / Hawaiian / compare (默认: compare)\n"
                      << "  --compare         对比 N2 vs Hawaiian (默认)\n"
                      << "  --verbose / -v    详细输出\n"
                      << "  --help / -h       显示帮助\n\n"
                      << "输出指标:\n"
                      << "  MeanNND   — 平均最近邻距离 (mm)\n"
                      << "  ClustFrac — 聚集比例 (≥1邻居在2mm内)\n"
                      << "  Clust#    — 聚群数 (union-find, r=2mm)\n"
                      << "  ClustSz   — 平均聚群大小\n"
                      << "  PairCorr  — 短程(2mm)对关联函数\n"
                      << "\n社交机制:\n"
                      << "  N2 (npr-1 215V):  RMG 被抑制 → 独居分散\n"
                      << "  Hawaiian (npr-1 lf): RMG 活跃 → O₂驱动聚集\n";
            return 0;
        }
    }

    std::cout << "========================================\n";
    std::cout << "  社会觅食分析器 (Step 122)\n";
    std::cout << "========================================\n";
    std::cout << "  虫数量:    " << num_worms << "\n";
    std::cout << "  仿真时长:  " << duration << " s\n";
    std::cout << "  随机种子:  " << seed << "\n";
    std::cout << "  菌株:      " << strain << "\n";

    std::vector<MultiWormSimulation::Snapshot> n2_snaps, hw_snaps;

    if (strain == "N2" || strain == "n2") {
        run_strain("N2 (Solitary)", num_worms, duration, -20.0, seed, n2_snaps);
    } else if (strain == "Hawaiian" || strain == "hawaiian") {
        run_strain("Hawaiian (Social)", num_worms, duration, 0.0, seed, hw_snaps);
    } else {
        // Compare mode
        run_strain("N2 (Solitary)", num_worms, duration, -20.0, seed, n2_snaps);
        run_strain("Hawaiian (Social)", num_worms, duration, 0.0, seed, hw_snaps);

        // Comparison summary
        std::cout << "\n========================================\n";
        std::cout << "  N2 vs Hawaiian 对比\n";
        std::cout << "========================================\n\n";

        if (!n2_snaps.empty() && !hw_snaps.empty()) {
            const auto& n2f = n2_snaps.back().metrics;
            const auto& hwf = hw_snaps.back().metrics;

            std::cout << std::fixed;
            std::cout << "  指标              N2          Hawaiian\n";
            std::cout << "  " << std::string(46, '-') << "\n";
            std::cout << "  MeanNND (mm)      "
                      << std::setw(8) << std::setprecision(2) << n2f.mean_nnd
                      << "      " << std::setw(8) << hwf.mean_nnd << "\n";
            std::cout << "  ClustFrac (%)     "
                      << std::setw(8) << std::setprecision(1) << (n2f.cluster_fraction * 100)
                      << "      " << std::setw(8) << (hwf.cluster_fraction * 100) << "\n";
            std::cout << "  Clusters          "
                      << std::setw(8) << n2f.num_clusters
                      << "      " << std::setw(8) << hwf.num_clusters << "\n";
            std::cout << "  ClustSize         "
                      << std::setw(8) << std::setprecision(1) << n2f.mean_cluster_size
                      << "      " << std::setw(8) << hwf.mean_cluster_size << "\n";
            std::cout << "  PairCorr          "
                      << std::setw(8) << std::setprecision(2) << n2f.pair_correlation_short
                      << "      " << std::setw(8) << hwf.pair_correlation_short << "\n";
            std::cout << "  MeanSpeed (mm/s)  "
                      << std::setw(8) << std::setprecision(3) << n2f.mean_speed
                      << "      " << std::setw(8) << hwf.mean_speed << "\n";

            // Verdict
            std::cout << "\n  判定: ";
            if (hwf.mean_nnd < n2f.mean_nnd * 0.7 ||
                hwf.cluster_fraction > n2f.cluster_fraction + 0.2) {
                std::cout << "✅ Hawaiian 聚集 > N2 (社会行为涌现)\n";
            } else if (std::abs(hwf.mean_nnd - n2f.mean_nnd) < n2f.mean_nnd * 0.1) {
                std::cout << "⚠️ 差异不显著 (可能需要更长仿真或更多虫)\n";
            } else {
                std::cout << "❌ 未检测到聚集差异\n";
            }
        }
    }

    std::cout << "\n";
    return 0;
}
