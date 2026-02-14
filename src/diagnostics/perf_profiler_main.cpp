#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <chrono>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#undef ERROR
#endif

using namespace celegans;
using Clock = std::chrono::high_resolution_clock;

// ================================================================
// 性能计时器
// ================================================================
struct TimingResult {
    std::string name;
    double total_us;      // 总时间 (微秒)
    double mean_us;       // 平均每步 (微秒)
    double max_us;        // 最大单步 (微秒)
    double min_us;        // 最小单步 (微秒)
    double pct;           // 占总时间百分比
};

// ================================================================
// 内存估算
// ================================================================
struct MemoryEstimate {
    std::string component;
    size_t bytes;
};

static std::string format_bytes(size_t bytes) {
    if (bytes >= 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    if (bytes >= 1024) return std::to_string(bytes / 1024) + " KB";
    return std::to_string(bytes) + " B";
}

#ifdef _WIN32
static size_t get_process_memory_bytes() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
}
#else
static size_t get_process_memory_bytes() { return 0; }
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Logger::instance().set_level(LogLevel::ERROR);

    double duration = 10.0;
    unsigned int seed = 123;
    int warmup_steps = 200;
    bool detailed = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i+1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--warmup" && i+1 < argc) {
            warmup_steps = std::atoi(argv[++i]);
        } else if (arg == "-v" || arg == "--verbose") {
            detailed = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: perf_profiler [options]\n\n"
                      << "Profile simulation performance bottlenecks\n\n"
                      << "Options:\n"
                      << "  --duration <sec>     Duration to profile (default: 10)\n"
                      << "  --seed <n>           RNG seed (default: 123)\n"
                      << "  --warmup <steps>     Warmup steps before timing (default: 200)\n"
                      << "  -v, --verbose        Show per-step histogram\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Reports:\n"
                      << "  Step timing:         Per-step wall-clock time\n"
                      << "  Realtime ratio:      Simulation speed vs real-time\n"
                      << "  Memory:              Estimated memory usage by component\n"
                      << "  Scalability:         Projected cost for longer runs\n";
            return 0;
        }
    }

    std::cout << "========================================\n";
    std::cout << "  Performance Profiler\n";
    std::cout << "========================================\n\n";

    // === Phase 1: 初始化计时 ===
    std::cout << "  [1/4] Initialization... " << std::flush;
    auto t_init_start = Clock::now();

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    sim.reset_transducers();

    auto t_init_end = Clock::now();
    double init_ms = std::chrono::duration<double, std::milli>(t_init_end - t_init_start).count();

    size_t mem_after_init = get_process_memory_bytes();

    const auto& conn = sim.connectome();
    int n_neurons = (int)sim.neurons().size();
    int n_synapses = (int)conn.synapses().size();
    int n_gap = (int)conn.gap_junctions().size();

    std::cout << "Done! (" << std::fixed << std::setprecision(1) << init_ms << " ms)\n";
    std::cout << "    Neurons: " << n_neurons
              << "  Synapses: " << n_synapses
              << "  Gap junctions: " << n_gap << "\n\n";

    // === Phase 2: Warmup ===
    std::cout << "  [2/4] Warmup (" << warmup_steps << " steps)... " << std::flush;
    for (int s = 0; s < warmup_steps; ++s) sim.step();
    std::cout << "Done!\n";

    // === Phase 3: 逐步计时 ===
    double duration_ms = duration * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());

    std::cout << "  [3/4] Profiling " << total_steps << " steps (" << duration << "s sim)... " << std::flush;

    std::vector<double> step_times_us;
    step_times_us.reserve(total_steps);

    // 分段计时: 每1000步记录一次吞吐量
    int segment_size = 1000;
    std::vector<double> segment_throughput;  // steps/sec

    auto t_profile_start = Clock::now();
    auto t_seg_start = t_profile_start;

    for (int s = 0; s < total_steps; ++s) {
        auto t0 = Clock::now();
        sim.step();
        auto t1 = Clock::now();

        double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
        step_times_us.push_back(us);

        if ((s + 1) % segment_size == 0) {
            auto t_seg_end = Clock::now();
            double seg_sec = std::chrono::duration<double>(t_seg_end - t_seg_start).count();
            segment_throughput.push_back(segment_size / seg_sec);
            t_seg_start = t_seg_end;
        }
    }

    auto t_profile_end = Clock::now();
    double profile_sec = std::chrono::duration<double>(t_profile_end - t_profile_start).count();

    size_t mem_after_run = get_process_memory_bytes();

    std::cout << "Done!\n\n";

    // === Phase 4: 分析 ===
    std::cout << "  [4/4] Analyzing... " << std::flush;

    // 基本统计
    double total_us = std::accumulate(step_times_us.begin(), step_times_us.end(), 0.0);
    double mean_us = total_us / step_times_us.size();
    double max_us = *std::max_element(step_times_us.begin(), step_times_us.end());
    double min_us = *std::min_element(step_times_us.begin(), step_times_us.end());

    // 标准差
    double sq = 0;
    for (double t : step_times_us) sq += (t - mean_us) * (t - mean_us);
    double std_us = std::sqrt(sq / step_times_us.size());

    // 百分位数
    std::vector<double> sorted_times = step_times_us;
    std::sort(sorted_times.begin(), sorted_times.end());
    double p50 = sorted_times[sorted_times.size() / 2];
    double p95 = sorted_times[(int)(sorted_times.size() * 0.95)];
    double p99 = sorted_times[(int)(sorted_times.size() * 0.99)];

    // 实时比率
    double sim_time_ms = total_steps * sim.dt();
    double realtime_ratio = (sim_time_ms / 1000.0) / profile_sec;
    double steps_per_sec = total_steps / profile_sec;

    // 内存估算
    std::vector<MemoryEstimate> mem_estimates;
    // 每个神经元: ~500 bytes (V, ions, channels, STP)
    mem_estimates.push_back({"Neurons (" + std::to_string(n_neurons) + ")",
                            (size_t)(n_neurons * 500)});
    // 每个突触: ~200 bytes
    mem_estimates.push_back({"Synapses (" + std::to_string(n_synapses) + ")",
                            (size_t)(n_synapses * 200)});
    // Gap junctions: ~80 bytes
    mem_estimates.push_back({"Gap Junctions (" + std::to_string(n_gap) + ")",
                            (size_t)(n_gap * 80)});
    // Body model: 12 segments × ~200 bytes + rods
    mem_estimates.push_back({"Body Model", 12 * 200 + 13 * 100});
    // Environment: fields ~50KB
    mem_estimates.push_back({"Environment", 50 * 1024});
    // Neuromodulation: ~5KB
    mem_estimates.push_back({"Neuromodulation", 5 * 1024});
    // Connectome metadata
    mem_estimates.push_back({"Connectome Meta", (size_t)(n_neurons * 150)});

    size_t total_est = 0;
    for (const auto& m : mem_estimates) total_est += m.bytes;

    std::cout << "Done!\n";

    // === 输出 ===
    std::cout << "\n========================================\n";
    std::cout << "  TIMING RESULTS\n";
    std::cout << "========================================\n\n";

    std::cout << "--- Initialization ---\n";
    std::cout << "  Init time:          " << std::fixed << std::setprecision(1) << init_ms << " ms\n\n";

    std::cout << "--- Per-Step Timing ---\n";
    std::cout << "  Mean:               " << std::setprecision(1) << mean_us << " us\n";
    std::cout << "  Std:                " << std_us << " us\n";
    std::cout << "  Min:                " << min_us << " us\n";
    std::cout << "  P50 (median):       " << p50 << " us\n";
    std::cout << "  P95:                " << p95 << " us\n";
    std::cout << "  P99:                " << p99 << " us\n";
    std::cout << "  Max:                " << max_us << " us\n\n";

    std::cout << "--- Throughput ---\n";
    std::cout << "  Steps/sec:          " << std::setprecision(0) << steps_per_sec << "\n";
    std::cout << "  Sim dt:             " << sim.dt() << " ms\n";
    std::cout << "  Realtime ratio:     " << std::setprecision(1) << realtime_ratio << "x";
    if (realtime_ratio >= 1.0) std::cout << " (FASTER than realtime)";
    else std::cout << " (SLOWER than realtime)";
    std::cout << "\n\n";

    // 吞吐量趋势
    if (!segment_throughput.empty()) {
        std::cout << "--- Throughput Trend (per " << segment_size << " steps) ---\n";
        double first_tp = segment_throughput.front();
        double last_tp = segment_throughput.back();
        double mean_tp = std::accumulate(segment_throughput.begin(), segment_throughput.end(), 0.0) / segment_throughput.size();
        std::cout << "  First seg:          " << std::setprecision(0) << first_tp << " steps/s\n";
        std::cout << "  Last seg:           " << last_tp << " steps/s\n";
        std::cout << "  Mean:               " << mean_tp << " steps/s\n";
        double drift_pct = 100.0 * (last_tp - first_tp) / first_tp;
        std::cout << "  Drift:              " << std::setprecision(1) << std::showpos << drift_pct << "%" << std::noshowpos;
        if (drift_pct < -10) std::cout << " (DEGRADING - possible memory/cache issue)";
        else if (drift_pct > 10) std::cout << " (IMPROVING - cache warming)";
        else std::cout << " (STABLE)";
        std::cout << "\n\n";
    }

    // === 瓶颈分析 ===
    std::cout << "--- Bottleneck Analysis (estimated) ---\n";
    // 估算各子系统占比: 基于神经元/突触数量
    double neuron_cost = n_neurons * 0.15;   // 每神经元 ~0.15us (离子通道方程)
    double synapse_cost = n_synapses * 0.08; // 每突触 ~0.08us (权重*释放*门控)
    double gap_cost = n_gap * 0.05;          // 每gap ~0.05us
    double body_cost = 15.0;                 // 体物理 ~15us
    double sensory_cost = 8.0;               // 感觉输入 ~8us
    double motor_cost = 5.0;                 // 运动控制 ~5us
    double internal_cost = 4.0;              // 内部状态 ~4us
    double neuromod_cost = 3.0;              // 神经调质 ~3us
    double env_cost = 2.0;                   // 环境 ~2us

    double est_total = neuron_cost + synapse_cost + gap_cost + body_cost +
                       sensory_cost + motor_cost + internal_cost + neuromod_cost + env_cost;

    // 按实际测量校准
    double scale = mean_us / est_total;
    neuron_cost *= scale;
    synapse_cost *= scale;
    gap_cost *= scale;
    body_cost *= scale;
    sensory_cost *= scale;
    motor_cost *= scale;
    internal_cost *= scale;
    neuromod_cost *= scale;
    env_cost *= scale;

    struct SubSystem {
        std::string name;
        double cost_us;
    };
    std::vector<SubSystem> subsystems = {
        {"Neuron step (" + std::to_string(n_neurons) + ")", neuron_cost},
        {"Synapse compute (" + std::to_string(n_synapses) + ")", synapse_cost},
        {"Gap junctions (" + std::to_string(n_gap) + ")", gap_cost},
        {"Body physics (12 seg)", body_cost},
        {"Sensory input", sensory_cost},
        {"Motor control", motor_cost},
        {"Internal states", internal_cost},
        {"Neuromodulation", neuromod_cost},
        {"Environment", env_cost}
    };

    std::sort(subsystems.begin(), subsystems.end(),
              [](const SubSystem& a, const SubSystem& b) { return a.cost_us > b.cost_us; });

    for (const auto& ss : subsystems) {
        double pct = 100.0 * ss.cost_us / mean_us;
        std::cout << "  " << std::setw(30) << std::left << ss.name
                  << std::right << std::setw(8) << std::setprecision(1) << ss.cost_us << " us"
                  << "  (" << std::setw(5) << std::setprecision(1) << pct << "%)\n";
    }
    std::cout << "  " << std::string(50, '-') << "\n";
    std::cout << "  " << std::setw(30) << std::left << "TOTAL"
              << std::right << std::setw(8) << std::setprecision(1) << mean_us << " us\n\n";

    // === 内存 ===
    std::cout << "========================================\n";
    std::cout << "  MEMORY USAGE\n";
    std::cout << "========================================\n\n";

    for (const auto& m : mem_estimates) {
        double pct = 100.0 * m.bytes / total_est;
        std::cout << "  " << std::setw(30) << std::left << m.component
                  << std::right << std::setw(10) << format_bytes(m.bytes)
                  << "  (" << std::setw(5) << std::setprecision(1) << pct << "%)\n";
    }
    std::cout << "  " << std::string(50, '-') << "\n";
    std::cout << "  " << std::setw(30) << std::left << "Estimated Total"
              << std::right << std::setw(10) << format_bytes(total_est) << "\n";

    if (mem_after_init > 0) {
        std::cout << "  " << std::setw(30) << std::left << "Process (after init)"
                  << std::right << std::setw(10) << format_bytes(mem_after_init) << "\n";
    }
    if (mem_after_run > 0) {
        std::cout << "  " << std::setw(30) << std::left << "Process (after run)"
                  << std::right << std::setw(10) << format_bytes(mem_after_run) << "\n";
    }

    // === 可扩展性预测 ===
    std::cout << "\n========================================\n";
    std::cout << "  SCALABILITY PROJECTION\n";
    std::cout << "========================================\n\n";

    double durations[] = {10, 30, 60, 120, 300, 600};
    std::cout << "  " << std::setw(12) << std::left << "Sim (s)"
              << std::setw(12) << "Steps"
              << std::setw(12) << "Wall (s)"
              << std::setw(12) << "Wall (min)"
              << "Feasible\n";
    std::cout << "  " << std::string(56, '-') << "\n";

    for (double d : durations) {
        int steps = (int)(d * 1000.0 / sim.dt());
        double wall_s = steps * mean_us / 1e6;
        double wall_min = wall_s / 60.0;
        std::string feasible;
        if (wall_s < 10) feasible = "instant";
        else if (wall_s < 60) feasible = "quick";
        else if (wall_min < 5) feasible = "OK";
        else if (wall_min < 30) feasible = "slow";
        else feasible = "LONG";

        std::cout << "  " << std::setw(12) << std::left << std::setprecision(0) << d
                  << std::setw(12) << steps
                  << std::setw(12) << std::setprecision(1) << wall_s
                  << std::setw(12) << std::setprecision(1) << wall_min
                  << feasible << "\n";
    }

    // 直方图
    if (detailed) {
        std::cout << "\n--- Step Time Histogram ---\n";
        int n_bins = 20;
        double bin_width = (max_us - min_us) / n_bins;
        if (bin_width < 0.1) bin_width = 0.1;
        std::vector<int> hist(n_bins, 0);
        for (double t : step_times_us) {
            int bin = (int)((t - min_us) / bin_width);
            if (bin >= n_bins) bin = n_bins - 1;
            if (bin < 0) bin = 0;
            hist[bin]++;
        }
        int max_count = *std::max_element(hist.begin(), hist.end());
        for (int b = 0; b < n_bins; ++b) {
            double lo = min_us + b * bin_width;
            int bar_len = (max_count > 0) ? (int)(40.0 * hist[b] / max_count) : 0;
            std::cout << "  " << std::setw(7) << std::setprecision(0) << std::right << lo
                      << " us |" << std::string(bar_len, '#')
                      << " " << hist[b] << "\n";
        }
    }

    return 0;
}
