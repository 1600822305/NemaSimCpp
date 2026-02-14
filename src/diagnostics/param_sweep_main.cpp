#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <chrono>
#include <functional>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#undef ERROR
#endif

using namespace celegans;

// ================================================================
// 扫描参数定义
// ================================================================
struct SweepParam {
    std::string name;
    std::vector<double> values;
};

// ================================================================
// 仿真运行结果指标
// ================================================================
struct RunMetrics {
    double param_value;
    double mean_speed;
    double path_efficiency;
    double chemotaxis_index;
    int reversal_count;
    int omega_count;
    double pump_rate;
    double forward_pct;
    double reverse_pct;
    double wall_time_ms;
};

// ================================================================
// 参数范围解析: "start:end:step" 或 "v1,v2,v3"
// ================================================================
static std::vector<double> parse_range(const std::string& spec) {
    std::vector<double> values;

    // 尝试 start:end:step 格式
    size_t c1 = spec.find(':');
    size_t c2 = spec.find(':', c1 + 1);
    if (c1 != std::string::npos && c2 != std::string::npos) {
        double start = std::atof(spec.substr(0, c1).c_str());
        double end = std::atof(spec.substr(c1 + 1, c2 - c1 - 1).c_str());
        double step = std::atof(spec.substr(c2 + 1).c_str());
        if (step <= 0) step = 0.1;
        for (double v = start; v <= end + step * 0.01; v += step) {
            values.push_back(v);
        }
        return values;
    }

    // 尝试逗号分隔格式
    std::istringstream iss(spec);
    std::string token;
    while (std::getline(iss, token, ',')) {
        values.push_back(std::atof(token.c_str()));
    }
    return values;
}

// ================================================================
// 将参数名映射到 TuningParams 字段
// ================================================================
static bool set_param(SimulationEngine& sim, const std::string& name, double value) {
    if (name == "synapse_scale") { sim.params.synapse_scale = (float)value; return true; }
    if (name == "speed_scale") { sim.params.speed_scale = (float)value; return true; }
    if (name == "sensory_gain") { sim.params.sensory_gain = (float)value; return true; }
    if (name == "weathervane_gain") { sim.params.weathervane_gain = (float)value; return true; }
    if (name == "bias_clamp") { sim.params.bias_clamp = (float)value; return true; }
    if (name == "as_factor") { sim.params.as_factor = (float)value; return true; }
    if (name == "pulse_amp") { sim.params.pulse_amp = (float)value; return true; }
    if (name == "omega_threshold") { sim.params.omega_threshold = (float)value; return true; }
    if (name == "riv_tonic") { sim.params.riv_tonic = (float)value; return true; }
    return false;
}

// ================================================================
// 运行单次仿真，采集指标
// ================================================================
static RunMetrics run_single(const std::string& param_name, double param_value,
                              double duration_s, unsigned int seed) {
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    if (!set_param(sim, param_name, param_value)) {
        std::cerr << "Unknown parameter: " << param_name << "\n";
    }

    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    sim.reset_transducers();

    double duration_ms = duration_s * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(100.0 / sim.dt());

    double speed_sum = 0;
    int speed_count = 0;
    int rev_count = 0, omega_count = 0;
    bool prev_rev = false, prev_omega = false;
    double forward_time = 0, reverse_time = 0, total_time = 0;
    Vector2d first_pos{0, 0}, last_pos{0, 0};
    double total_dist = 0;
    bool first = true;
    Vector2d prev_pos{0, 0};

    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        if ((s + 1) % sample_interval == 0) {
            auto pos = sim.body().get_head_position();
            double spd = sim.body().get_speed();
            speed_sum += spd;
            speed_count++;

            if (first) { first_pos = pos; prev_pos = pos; first = false; }
            else {
                double dx = pos.x - prev_pos.x;
                double dy = pos.y - prev_pos.y;
                total_dist += std::sqrt(dx * dx + dy * dy);
            }
            last_pos = pos;
            prev_pos = pos;

            bool curr_rev = sim.is_reversing();
            bool curr_omega = sim.is_omega_turning();
            if (curr_rev && !prev_rev) rev_count++;
            if (curr_omega && !prev_omega) omega_count++;
            prev_rev = curr_rev;
            prev_omega = curr_omega;

            if (curr_rev) reverse_time += 100.0;
            else forward_time += 100.0;
            total_time += 100.0;
        }
    }

    auto t1 = Clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    RunMetrics m;
    m.param_value = param_value;
    m.mean_speed = speed_count > 0 ? speed_sum / speed_count : 0;
    m.reversal_count = rev_count;
    m.omega_count = omega_count;
    m.pump_rate = sim.pump_rate_hz();
    m.wall_time_ms = wall_ms;

    double dx = last_pos.x - first_pos.x;
    double dy = last_pos.y - first_pos.y;
    double net = std::sqrt(dx * dx + dy * dy);
    m.path_efficiency = (total_dist > 0.001) ? net / total_dist : 0;

    double first_dist = std::sqrt((first_pos.x - 35.0) * (first_pos.x - 35.0) +
                                  (first_pos.y - 25.0) * (first_pos.y - 25.0));
    double final_dist = std::sqrt((last_pos.x - 35.0) * (last_pos.x - 35.0) +
                                  (last_pos.y - 25.0) * (last_pos.y - 25.0));
    m.chemotaxis_index = (first_dist > 0.1) ? (first_dist - final_dist) / first_dist : 0;

    m.forward_pct = total_time > 0 ? 100.0 * forward_time / total_time : 0;
    m.reverse_pct = total_time > 0 ? 100.0 * reverse_time / total_time : 0;

    return m;
}

// ================================================================
// 灵敏度分析: 每个指标对参数的偏导数 (有限差分)
// ================================================================
struct Sensitivity {
    std::string metric_name;
    double d_metric_d_param;  // Δmetric / Δparam (归一化)
    double r_squared;         // 线性拟合 R²
};

static std::vector<Sensitivity> compute_sensitivity(const std::vector<RunMetrics>& results) {
    if (results.size() < 3) return {};

    // 提取各指标的时间序列
    struct MetricExtractor {
        std::string name;
        std::function<double(const RunMetrics&)> extract;
    };
    std::vector<MetricExtractor> extractors = {
        {"speed", [](const RunMetrics& m) { return m.mean_speed; }},
        {"path_eff", [](const RunMetrics& m) { return m.path_efficiency; }},
        {"CI", [](const RunMetrics& m) { return m.chemotaxis_index; }},
        {"reversals", [](const RunMetrics& m) { return (double)m.reversal_count; }},
        {"omegas", [](const RunMetrics& m) { return (double)m.omega_count; }},
        {"pump_rate", [](const RunMetrics& m) { return m.pump_rate; }},
        {"fwd_pct", [](const RunMetrics& m) { return m.forward_pct; }},
    };

    // 参数值
    std::vector<double> x;
    for (const auto& r : results) x.push_back(r.param_value);
    double x_mean = std::accumulate(x.begin(), x.end(), 0.0) / x.size();
    double x_range = *std::max_element(x.begin(), x.end()) - *std::min_element(x.begin(), x.end());
    if (x_range < 1e-10) x_range = 1.0;

    std::vector<Sensitivity> sensitivities;

    for (const auto& ext : extractors) {
        std::vector<double> y;
        for (const auto& r : results) y.push_back(ext.extract(r));
        double y_mean = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
        double y_range = *std::max_element(y.begin(), y.end()) - *std::min_element(y.begin(), y.end());
        if (y_range < 1e-10) y_range = 1.0;

        // 线性回归: y = a*x + b
        double Sxx = 0, Sxy = 0, Syy = 0;
        for (size_t i = 0; i < x.size(); ++i) {
            double dx = x[i] - x_mean;
            double dy = y[i] - y_mean;
            Sxx += dx * dx;
            Sxy += dx * dy;
            Syy += dy * dy;
        }

        double slope = (Sxx > 1e-15) ? Sxy / Sxx : 0;
        double r2 = (Sxx > 1e-15 && Syy > 1e-15) ? (Sxy * Sxy) / (Sxx * Syy) : 0;

        // 归一化灵敏度: (dy/y_range) / (dx/x_range)
        double norm_slope = slope * x_range / y_range;

        Sensitivity s;
        s.metric_name = ext.name;
        s.d_metric_d_param = norm_slope;
        s.r_squared = r2;
        sensitivities.push_back(s);
    }

    // 按 R² 排序
    std::sort(sensitivities.begin(), sensitivities.end(),
              [](const Sensitivity& a, const Sensitivity& b) { return a.r_squared > b.r_squared; });

    return sensitivities;
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Logger::instance().set_level(LogLevel::ERROR);

    double duration = 30.0;
    unsigned int seed = 123;
    int n_seeds = 1;
    std::string param_name;
    std::string range_spec;
    std::string export_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--param" && i+1 < argc) {
            param_name = argv[++i];
        } else if (arg == "--range" && i+1 < argc) {
            range_spec = argv[++i];
        } else if (arg == "--duration" && i+1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i+1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--seeds" && i+1 < argc) {
            n_seeds = std::atoi(argv[++i]);
        } else if (arg == "--export" && i+1 < argc) {
            export_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: param_sweep --param <name> --range <spec> [options]\n\n"
                      << "Sweep a parameter across a range and collect metrics\n\n"
                      << "Required:\n"
                      << "  --param <name>       Parameter name (see list below)\n"
                      << "  --range <spec>       Range: start:end:step or v1,v2,v3\n\n"
                      << "Options:\n"
                      << "  --duration <sec>     Sim duration per run (default: 30)\n"
                      << "  --seed <n>           Base RNG seed (default: 123)\n"
                      << "  --seeds <n>          Number of seeds to average (default: 1)\n"
                      << "  --export <csv>       Export results to CSV\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Parameters:\n"
                      << "  synapse_scale        Global synapse weight multiplier\n"
                      << "  speed_scale          Locomotion speed multiplier\n"
                      << "  sensory_gain         Chemosensory transducer gain\n"
                      << "  weathervane_gain     Gradient->SMD bias (pA/(conc/mm))\n"
                      << "  bias_clamp           SMD bias clamp (pA)\n"
                      << "  as_factor            AS dorsal resistance factor\n"
                      << "  pulse_amp            RIV post-reversal pulse amplitude\n"
                      << "  omega_threshold      RIV release threshold for omega\n"
                      << "  riv_tonic            RIV baseline tonic drive (pA)\n\n"
                      << "Examples:\n"
                      << "  param_sweep --param synapse_scale --range 0.5:2.0:0.25\n"
                      << "  param_sweep --param speed_scale --range 0.5,1.0,2.0,4.0\n"
                      << "  param_sweep --param pulse_amp --range 10:100:10 --seeds 3\n";
            return 0;
        }
    }

    if (param_name.empty() || range_spec.empty()) {
        std::cerr << "Error: --param and --range are required. Use --help for usage.\n";
        return 1;
    }

    auto values = parse_range(range_spec);
    if (values.empty()) {
        std::cerr << "Error: invalid range spec '" << range_spec << "'\n";
        return 1;
    }

    int total_runs = (int)values.size() * n_seeds;

    std::cout << "========================================\n";
    std::cout << "  Parameter Sweep\n";
    std::cout << "========================================\n\n";
    std::cout << "  Parameter:  " << param_name << "\n";
    std::cout << "  Range:      " << values.front() << " -> " << values.back()
              << " (" << values.size() << " values)\n";
    std::cout << "  Duration:   " << duration << " s per run\n";
    std::cout << "  Seeds:      " << n_seeds << "\n";
    std::cout << "  Total runs: " << total_runs << "\n\n";

    // === 运行扫描 ===
    std::vector<RunMetrics> results;
    int run_idx = 0;

    for (double v : values) {
        RunMetrics avg;
        avg.param_value = v;
        avg.mean_speed = 0;
        avg.path_efficiency = 0;
        avg.chemotaxis_index = 0;
        avg.reversal_count = 0;
        avg.omega_count = 0;
        avg.pump_rate = 0;
        avg.forward_pct = 0;
        avg.reverse_pct = 0;
        avg.wall_time_ms = 0;

        for (int s = 0; s < n_seeds; ++s) {
            run_idx++;
            std::cout << "\r  Running " << run_idx << "/" << total_runs
                      << " (" << param_name << "=" << std::fixed << std::setprecision(2) << v
                      << ", seed=" << (seed + s) << ")... " << std::flush;

            auto m = run_single(param_name, v, duration, seed + s);
            avg.mean_speed += m.mean_speed;
            avg.path_efficiency += m.path_efficiency;
            avg.chemotaxis_index += m.chemotaxis_index;
            avg.reversal_count += m.reversal_count;
            avg.omega_count += m.omega_count;
            avg.pump_rate += m.pump_rate;
            avg.forward_pct += m.forward_pct;
            avg.reverse_pct += m.reverse_pct;
            avg.wall_time_ms += m.wall_time_ms;
        }

        avg.mean_speed /= n_seeds;
        avg.path_efficiency /= n_seeds;
        avg.chemotaxis_index /= n_seeds;
        avg.reversal_count /= n_seeds;
        avg.omega_count /= n_seeds;
        avg.pump_rate /= n_seeds;
        avg.forward_pct /= n_seeds;
        avg.reverse_pct /= n_seeds;
        avg.wall_time_ms /= n_seeds;

        results.push_back(avg);
    }

    std::cout << "\r  All " << total_runs << " runs complete!                              \n\n";

    // === 结果矩阵 ===
    std::cout << "========================================\n";
    std::cout << "  RESULTS MATRIX\n";
    std::cout << "========================================\n\n";

    std::cout << std::right
              << std::setw(10) << param_name
              << std::setw(8) << "Speed"
              << std::setw(8) << "PathE"
              << std::setw(8) << "CI"
              << std::setw(6) << "Rev"
              << std::setw(6) << "Omg"
              << std::setw(8) << "Pump"
              << std::setw(8) << "Fwd%"
              << std::setw(8) << "Rev%"
              << "\n";
    std::cout << "  " << std::string(68, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::fixed
                  << std::setw(10) << std::setprecision(2) << r.param_value
                  << std::setw(8) << std::setprecision(3) << r.mean_speed
                  << std::setw(8) << std::setprecision(3) << r.path_efficiency
                  << std::setw(8) << std::setprecision(3) << r.chemotaxis_index
                  << std::setw(6) << r.reversal_count
                  << std::setw(6) << r.omega_count
                  << std::setw(8) << std::setprecision(1) << r.pump_rate
                  << std::setw(8) << std::setprecision(1) << r.forward_pct
                  << std::setw(8) << std::setprecision(1) << r.reverse_pct
                  << "\n";
    }

    // === 最优值 ===
    std::cout << "\n--- Optimal Values ---\n";
    auto best_ci = std::max_element(results.begin(), results.end(),
        [](const RunMetrics& a, const RunMetrics& b) { return a.chemotaxis_index < b.chemotaxis_index; });
    auto best_speed = std::max_element(results.begin(), results.end(),
        [](const RunMetrics& a, const RunMetrics& b) { return a.mean_speed < b.mean_speed; });
    auto best_path = std::max_element(results.begin(), results.end(),
        [](const RunMetrics& a, const RunMetrics& b) { return a.path_efficiency < b.path_efficiency; });

    std::cout << "  Best CI:    " << param_name << "=" << std::setprecision(2) << best_ci->param_value
              << " (CI=" << std::setprecision(3) << best_ci->chemotaxis_index << ")\n";
    std::cout << "  Best Speed: " << param_name << "=" << std::setprecision(2) << best_speed->param_value
              << " (speed=" << std::setprecision(3) << best_speed->mean_speed << ")\n";
    std::cout << "  Best Path:  " << param_name << "=" << std::setprecision(2) << best_path->param_value
              << " (eff=" << std::setprecision(3) << best_path->path_efficiency << ")\n";

    // === 灵敏度分析 ===
    auto sensitivities = compute_sensitivity(results);
    if (!sensitivities.empty()) {
        std::cout << "\n--- Sensitivity Analysis ---\n";
        std::cout << "  " << std::setw(12) << std::left << "Metric"
                  << std::setw(12) << std::right << "dM/dP"
                  << std::setw(8) << "R^2"
                  << "  Impact\n";
        std::cout << "  " << std::string(44, '-') << "\n";

        for (const auto& s : sensitivities) {
            std::string impact;
            if (s.r_squared > 0.7) impact = "*** STRONG";
            else if (s.r_squared > 0.3) impact = "**  MODERATE";
            else if (s.r_squared > 0.1) impact = "*   WEAK";
            else impact = "    NONE";

            std::cout << "  " << std::setw(12) << std::left << s.metric_name
                      << std::setw(12) << std::right << std::setprecision(3) << std::showpos << s.d_metric_d_param << std::noshowpos
                      << std::setw(8) << std::setprecision(3) << s.r_squared
                      << "  " << impact << "\n";
        }
    }

    // === ASCII 图表: CI vs 参数 ===
    std::cout << "\n--- " << param_name << " vs Chemotaxis Index ---\n";
    double ci_min = 1e30, ci_max = -1e30;
    for (const auto& r : results) {
        ci_min = std::min(ci_min, r.chemotaxis_index);
        ci_max = std::max(ci_max, r.chemotaxis_index);
    }
    double ci_range = ci_max - ci_min;
    if (ci_range < 0.001) ci_range = 0.001;
    int bar_width = 40;

    for (const auto& r : results) {
        int bar = (int)(bar_width * (r.chemotaxis_index - ci_min) / ci_range);
        if (bar < 0) bar = 0;
        if (bar > bar_width) bar = bar_width;
        std::cout << "  " << std::setw(6) << std::setprecision(2) << r.param_value << " |"
                  << std::string(bar, '#') << std::string(bar_width - bar, ' ')
                  << "| " << std::setprecision(3) << r.chemotaxis_index << "\n";
    }

    // === CSV 导出 ===
    if (!export_file.empty()) {
        std::ofstream ofs(export_file);
        ofs << param_name << ",speed,path_efficiency,chemotaxis_index,reversals,omegas,pump_rate,fwd_pct,rev_pct,wall_ms\n";
        for (const auto& r : results) {
            ofs << std::fixed << std::setprecision(4)
                << r.param_value << ","
                << r.mean_speed << ","
                << r.path_efficiency << ","
                << r.chemotaxis_index << ","
                << r.reversal_count << ","
                << r.omega_count << ","
                << std::setprecision(1) << r.pump_rate << ","
                << r.forward_pct << ","
                << r.reverse_pct << ","
                << std::setprecision(0) << r.wall_time_ms << "\n";
        }
        std::cout << "\nExported: " << export_file << "\n";
    }

    return 0;
}
