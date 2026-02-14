// gain_profiler_main.cpp — 信号链增益剖析器
// 测量关键信号链每一级的动态范围和传递增益，定位饱和/衰减瓶颈。
// 跟踪三条主要信号链：klinokinesis、klinotaxis、omega方向。
//
// Usage:
//   gain_profiler --duration 60 --seed 42
//   gain_profiler --chain klinokinesis      # 仅分析 klinokinesis 链
//   gain_profiler --chain omega             # 仅分析 omega 方向链
//   gain_profiler --chain klinotaxis        # 仅分析 klinotaxis 链
//   gain_profiler --chain all               # 全部 (默认)
#include "simulation/simulation_engine.h"
#include "neuron/multi_compartment.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace celegans;

// ================================================================
// Stage tracker — tracks min/max/mean/std of a named variable
// ================================================================
struct StageTracker {
    std::string name;
    std::string unit;
    double min_val = 1e30, max_val = -1e30;
    double sum = 0, sum_sq = 0;
    int count = 0;

    void record(double v) {
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
        sum += v;
        sum_sq += v * v;
        count++;
    }

    double mean() const { return count > 0 ? sum / count : 0; }
    double stddev() const {
        if (count < 2) return 0;
        double m = mean();
        double var = sum_sq / count - m * m;
        return var > 0 ? std::sqrt(var) : 0;
    }
    double range() const { return max_val - min_val; }
};

// ================================================================
// Signal chain definition
// ================================================================
struct SignalChain {
    std::string name;
    std::vector<StageTracker> stages;
};

// ================================================================
// Helper
// ================================================================
static double wrap_angle(double a) {
    while (a > M_PI) a -= 2 * M_PI;
    while (a < -M_PI) a += 2 * M_PI;
    return a;
}

// ================================================================
// Print chain analysis
// ================================================================
static void print_chain(const SignalChain& chain) {
    std::cout << "\n+================================================================+\n";
    std::cout << "|  Signal Chain: " << chain.name << "\n";
    std::cout << "+================================================================+\n";

    // Header
    std::cout << "|  " << std::left << std::setw(24) << "Stage"
              << std::right << std::setw(12) << "Min"
              << std::setw(12) << "Max"
              << std::setw(12) << "Range"
              << std::setw(10) << "Mean"
              << std::setw(10) << "StdDev"
              << "  Status\n";
    std::cout << "|  " << std::string(80, '-') << "\n";

    for (size_t i = 0; i < chain.stages.size(); ++i) {
        const auto& s = chain.stages[i];
        if (s.count == 0) continue;

        // Determine status
        std::string status = "OK";
        double r = s.range();

        // Check for saturation: if range is very small relative to mean
        if (r < 0.01 * std::abs(s.mean()) && std::abs(s.mean()) > 0.01) {
            status = "!! SATURATED";
        }
        // Check for dead signal: no variation
        if (r < 1e-6 && i > 0) {
            status = "!! DEAD";
        }

        // Compute inter-stage gain if possible
        std::string gain_str = "";
        if (i > 0 && chain.stages[i-1].count > 0) {
            double prev_range = chain.stages[i-1].range();
            if (prev_range > 1e-8) {
                double gain = r / prev_range;
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(2) << gain << "x";
                gain_str = oss.str();

                if (gain < 0.01) status = "!! BOTTLENECK (gain<0.01)";
                else if (gain < 0.1) status = "?  WEAK (gain<0.1)";
                else if (gain > 100) status = "?  AMPLIFIED (gain>100)";
            }
        }

        std::cout << std::fixed;
        std::cout << "|  " << std::left << std::setw(24) << (s.name + " [" + s.unit + "]");

        // Adaptive precision based on magnitude
        int prec = (std::abs(s.max_val) < 0.01 && std::abs(s.min_val) < 0.01) ? 6 : 3;
        std::cout << std::setprecision(prec);
        std::cout << std::right << std::setw(12) << s.min_val
                  << std::setw(12) << s.max_val
                  << std::setw(12) << r;
        std::cout << std::setprecision(3);
        std::cout << std::setw(10) << s.mean()
                  << std::setw(10) << s.stddev();
        if (!gain_str.empty()) {
            std::cout << "  " << std::setw(8) << gain_str;
        } else {
            std::cout << "  " << std::setw(8) << "";
        }
        std::cout << "  " << status << "\n";
    }

    std::cout << "+================================================================+\n";
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::ERROR);

    double duration_s = 60.0;
    int seed = 42;
    std::string chain_filter = "all";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) duration_s = std::atof(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::atoi(argv[++i]);
        else if (arg == "--chain" && i + 1 < argc) chain_filter = argv[++i];
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: gain_profiler [options]\n\n"
                      << "Measures transfer function gain at each signal chain stage.\n\n"
                      << "Options:\n"
                      << "  --duration <sec>  Simulation duration (default: 60)\n"
                      << "  --seed <n>        Random seed (default: 42)\n"
                      << "  --chain <name>    klinokinesis|klinotaxis|omega|all (default: all)\n"
                      << "  --help / -h       Show this help\n";
            return 0;
        }
    }

    bool do_kk = (chain_filter == "all" || chain_filter == "klinokinesis");
    bool do_ktx = (chain_filter == "all" || chain_filter == "klinotaxis");
    bool do_omega = (chain_filter == "all" || chain_filter == "omega");

    std::cout << "========================================\n";
    std::cout << "  Gain Profiler\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration:  " << duration_s << " s\n";
    std::cout << "  Seed:      " << seed << "\n";
    std::cout << "  Chains:    " << chain_filter << "\n";
    std::cout << "\n  Running simulation... " << std::flush;

    // --- Initialize simulation ---
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    Vector2d food_pos = {35.0, 35.0};

    // --- Define signal chain stages ---
    // Klinokinesis chain: gradient → dC/dt → ASER → AIB → AVA → reversal_rate
    SignalChain kk_chain;
    kk_chain.name = "Klinokinesis (dC/dt -> reversal modulation)";
    kk_chain.stages = {
        {"gradient_mag",       "conc/mm"},
        {"dC/dt_raw",          "conc/s"},
        {"dC/dt_filtered",     "conc/s"},
        {"kk_inject_AVA",      "pA"},
        {"ASER_release",       "rate"},
        {"AIBL_release",       "rate"},
        {"AVAL_release",       "rate"},
        {"AVA_mean_release",   "rate"},
        {"AVA-AVB_balance",    "rate"},
    };

    // Klinotaxis chain: gradient_perp → RIA → SMD → curvature
    SignalChain ktx_chain;
    ktx_chain.name = "Klinotaxis (grad_perp -> head curvature)";
    ktx_chain.stages = {
        {"grad_perp",          "conc/mm"},
        {"AWCL-AWCR_diff",     "rate"},
        {"ASEL-ASER_diff",     "rate"},
        {"AIYL-AIYR_diff",     "rate"},
        {"RIA_Ca_AC",          "a.u."},
        {"head_force_diff",    "pN"},
        {"head_curvature",     "/mm"},
    };

    // Omega chain: gradient → RIV amp → omega peak → muscle boost → heading change
    SignalChain omega_chain;
    omega_chain.name = "Omega Direction (gradient -> turn direction)";
    omega_chain.stages = {
        {"grad_perp",              "conc/mm"},
        {"riv_amp_L-R",            "a.u."},
        {"RIVL-RIVR_release",      "rate"},
        {"omega_peak_L-R",         "a.u."},
        {"head_force_diff_omega",  "pN"},
    };

    // --- Neuron ID resolution ---
    const auto& neurons = sim.neurons();
    int nn = static_cast<int>(neurons.size());
    auto fid = [&](const char* name) -> int {
        for (int i = 0; i < nn; ++i) if (neurons[i]->info().name == name) return i;
        return -1;
    };
    auto rel = [&](int id) -> double {
        return (id >= 0 && id < nn) ? neurons[id]->get_transmitter_release_rate() : 0.0;
    };

    int awcl = fid("AWCL"), awcr = fid("AWCR");
    int asel = fid("ASEL"), aser = fid("ASER");
    int aibl = fid("AIBL"), aibr = fid("AIBR");
    int aiyl = fid("AIYL"), aiyr = fid("AIYR");
    int aval = fid("AVAL"), avar = fid("AVAR");
    int avbl = fid("AVBL"), avbr = fid("AVBR");
    int rivl = fid("RIVL"), rivr = fid("RIVR");

    // --- Run simulation and sample ---
    double duration_ms = duration_s * 1000.0;
    int total_steps = static_cast<int>(duration_ms / sim.dt());
    int sample_interval = static_cast<int>(50.0 / sim.dt());
    double warmup_ms = 3000.0;
    double prev_concentration = 0;
    bool warmup_done = false;

    for (int s = 0; s < total_steps; ++s) {
        sim.step();

        if (s % sample_interval != 0) continue;
        if (sim.current_time() < warmup_ms) {
            prev_concentration = sim.prev_concentration();
            continue;
        }

        if (!warmup_done) {
            warmup_done = true;
            prev_concentration = sim.prev_concentration();
        }

        auto hp = sim.body().get_head_position();
        double heading = sim.body().get_head_angle();
        auto grad = sim.environment().chemical_field().gradient(hp);
        double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);
        double grad_perp = -std::sin(heading) * grad.x + std::cos(heading) * grad.y;
        double concentration = sim.environment().sample_chemical(hp);

        // --- Klinokinesis chain ---
        if (do_kk) {
            kk_chain.stages[0].record(grad_mag);

            double raw_dCdt = (concentration - prev_concentration) / (sim.dt() * sample_interval * 0.001);
            kk_chain.stages[1].record(raw_dCdt);

            double dCdt_f = sim.dCdt_filtered();
            kk_chain.stages[2].record(dCdt_f);

            // Reconstructed klinokinesis injection
            double pref = sim.awc_pref_cached();
            double kk_dCdt_gain = 300.0;
            double kk_inject = 0.0;
            if (pref >= 0.0 && dCdt_f < 0.0) {
                kk_inject = std::min(-dCdt_f * kk_dCdt_gain, 3.0);
            } else if (pref < 0.0 && dCdt_f > 0.0) {
                kk_inject = std::min(dCdt_f * kk_dCdt_gain, 3.0);
            }
            kk_chain.stages[3].record(kk_inject);

            kk_chain.stages[4].record(rel(aser));
            kk_chain.stages[5].record(rel(aibl));
            kk_chain.stages[6].record(rel(aval));

            double ava_mean = (rel(aval) + rel(avar)) * 0.5;
            kk_chain.stages[7].record(ava_mean);

            double avb_mean = (rel(avbl) + rel(avbr)) * 0.5;
            kk_chain.stages[8].record(ava_mean - avb_mean);
        }

        // --- Klinotaxis chain ---
        if (do_ktx && !sim.is_reversing() && !sim.is_omega_turning()) {
            ktx_chain.stages[0].record(grad_perp);
            ktx_chain.stages[1].record(rel(awcl) - rel(awcr));
            ktx_chain.stages[2].record(rel(asel) - rel(aser));
            ktx_chain.stages[3].record(rel(aiyl) - rel(aiyr));
            ktx_chain.stages[4].record(sim.ria_ca_diff_filtered());

            double fd_sum = 0, curv_sum = 0;
            for (int i = 0; i < 6; ++i) {
                fd_sum += sim.body().muscles().get_force_differential(i);
                curv_sum += sim.body().get_local_curvature(i);
            }
            ktx_chain.stages[5].record(fd_sum / 6.0);
            ktx_chain.stages[6].record(curv_sum / 6.0);
        }

        // --- Omega chain (only during omega) ---
        if (do_omega && sim.is_omega_turning()) {
            omega_chain.stages[0].record(grad_perp);
            omega_chain.stages[1].record(sim.riv_post_rev_amp_l() - sim.riv_post_rev_amp_r());
            omega_chain.stages[2].record(rel(rivl) - rel(rivr));
            omega_chain.stages[3].record(sim.riv_omega_peak_l() - sim.riv_omega_peak_r());

            double fd_sum = 0;
            for (int i = 0; i < 6; ++i) {
                fd_sum += sim.body().muscles().get_force_differential(i);
            }
            omega_chain.stages[4].record(fd_sum / 6.0);
        }

        prev_concentration = concentration;
    }

    std::cout << "Done!\n";

    // --- Print results ---
    if (do_kk) print_chain(kk_chain);
    if (do_ktx) print_chain(ktx_chain);
    if (do_omega) print_chain(omega_chain);

    // --- Summary: bottleneck detection ---
    std::cout << "\n========================================\n";
    std::cout << "  BOTTLENECK SUMMARY\n";
    std::cout << "========================================\n\n";

    auto check_chain = [](const SignalChain& chain) {
        bool found = false;
        for (size_t i = 1; i < chain.stages.size(); ++i) {
            if (chain.stages[i].count == 0 || chain.stages[i-1].count == 0) continue;
            double prev_r = chain.stages[i-1].range();
            double curr_r = chain.stages[i].range();
            if (prev_r < 1e-8) continue;
            double gain = curr_r / prev_r;
            if (gain < 0.01) {
                std::cout << "  !! " << chain.name << ": "
                          << chain.stages[i-1].name << " -> " << chain.stages[i].name
                          << "  gain=" << std::fixed << std::setprecision(4) << gain
                          << "  BOTTLENECK\n";
                found = true;
            } else if (chain.stages[i].range() < 1e-6 && i > 0) {
                std::cout << "  !! " << chain.name << ": "
                          << chain.stages[i].name << "  DEAD SIGNAL\n";
                found = true;
            }
        }
        if (!found) {
            std::cout << "  OK " << chain.name << ": no bottlenecks detected\n";
        }
    };

    if (do_kk) check_chain(kk_chain);
    if (do_ktx) check_chain(ktx_chain);
    if (do_omega) check_chain(omega_chain);

    return 0;
}
