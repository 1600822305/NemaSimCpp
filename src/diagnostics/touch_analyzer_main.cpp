// touch_analyzer_main.cpp — Touch Avoidance Circuit Verification
//
// Delivers controlled touch stimuli (anterior ALM / posterior PLM) and
// measures reversal/acceleration response vs spontaneous baseline.
// Uses paired stimulus/control design to distinguish touch-evoked from
// spontaneous reversals. Also tests laser ablation.
//
// Usage: touch_analyzer [--seed N] [--verbose] [--help]
//
// REF: Chalfie 1985 J Neurosci — touch circuit identification
//      Wicks & Rankin 1997 — tap habituation
//      Piggott 2011 Cell — mechanosensory transduction

#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#undef WARNING
#undef min
#undef max
#endif

using namespace celegans;

// ================================================================
// Paired trial design: stimulus window vs control window
// ================================================================
// Each trial:
//   1. Wait for stable forward locomotion (≥500ms forward)
//   2. Record 500ms CONTROL window (no stimulus) → count state transitions
//   3. Wait 500ms gap
//   4. Check still forward; if not, skip trial
//   5. Deliver 200ms stimulus pulse
//   6. Record 500ms STIMULUS window → count state transitions
// Compare reversal probability: P(rev|stim) vs P(rev|control)
// ================================================================

struct TrialResult {
    bool valid;                 // trial was valid (worm in forward state at both windows)
    bool control_reversed;      // reversed during control window
    bool stim_reversed;         // reversed during stimulus+response window
    double stim_latency_ms;     // time from stimulus onset to reversal (-1 if none)
};

struct ExperimentResult {
    std::string label;
    int total_trials;
    int valid_trials;
    int control_reversals;
    int stim_reversals;
    double control_rate;        // P(rev | no stimulus)
    double stim_rate;           // P(rev | stimulus)
    double rate_increase;       // stim_rate - control_rate
    double mean_latency_ms;
    std::vector<TrialResult> trials;
};

// ================================================================
// Run one experiment
// ================================================================
static ExperimentResult run_experiment(
    const std::string& label,
    const std::vector<std::pair<std::string,double>>& stimuli, // {neuron_name, current_pA}
    double stim_duration_ms,
    double window_ms,                   // observation window
    int n_trials,
    int seed,
    bool ablate_alm = false,
    bool verbose = false)
{
    ExperimentResult res;
    res.label = label;
    res.total_trials = n_trials;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    if (ablate_alm) {
        sim.ablate_neuron("ALM");
        sim.ablate_neuron("AVM");
    }

    double dt = sim.dt();
    double time = 0.0;

    // Warmup 5s
    for (int i = 0; i < static_cast<int>(5000.0 / dt); i++) {
        sim.step(); time += dt;
    }

    int valid = 0, ctrl_rev = 0, stim_rev = 0;
    std::vector<double> latencies;

    for (int trial = 0; trial < n_trials; trial++) {
        TrialResult tr{};

        // Step 1: Wait for stable forward locomotion (≥500ms consecutive forward)
        int consec_fwd = 0;
        int required_fwd = static_cast<int>(500.0 / dt);
        int max_wait = static_cast<int>(10000.0 / dt);  // 10s max wait
        for (int i = 0; i < max_wait; i++) {
            sim.step(); time += dt;
            if (sim.is_reversing()) consec_fwd = 0; else consec_fwd++;
            if (consec_fwd >= required_fwd) break;
        }
        if (consec_fwd < required_fwd) {
            // Could not find stable forward state, skip trial
            tr.valid = false;
            res.trials.push_back(tr);
            continue;
        }

        // Step 2: CONTROL window — observe for window_ms without stimulus
        bool ctrl_reversed = false;
        int window_steps = static_cast<int>(window_ms / dt);
        for (int i = 0; i < window_steps; i++) {
            sim.step(); time += dt;
            if (sim.is_reversing()) { ctrl_reversed = true; break; }
        }

        // Step 3: Gap (500ms)
        for (int i = 0; i < static_cast<int>(500.0 / dt); i++) {
            sim.step(); time += dt;
        }

        // Step 4: Check still forward
        if (sim.is_reversing()) {
            // Worm started reversing during gap, skip trial
            tr.valid = false;
            res.trials.push_back(tr);
            continue;
        }

        // Step 5: Deliver stimulus pulse
        bool stimulus_reversed = false;
        double latency = -1;
        double stim_start = time;
        int stim_steps = static_cast<int>(stim_duration_ms / dt);

        for (int i = 0; i < stim_steps; i++) {
            sim.clear_injections();
            for (auto& [name, current] : stimuli)
                sim.inject_neuron_current(name, current);
            sim.step(); time += dt;
            if (!stimulus_reversed && sim.is_reversing()) {
                stimulus_reversed = true;
                latency = time - stim_start;
            }
        }
        sim.clear_injections();

        // Step 6: Post-stimulus observation (rest of window)
        int post_steps = static_cast<int>((window_ms - stim_duration_ms) / dt);
        for (int i = 0; i < post_steps; i++) {
            sim.step(); time += dt;
            if (!stimulus_reversed && sim.is_reversing()) {
                stimulus_reversed = true;
                latency = time - stim_start;
            }
        }

        tr.valid = true;
        tr.control_reversed = ctrl_reversed;
        tr.stim_reversed = stimulus_reversed;
        tr.stim_latency_ms = latency;
        res.trials.push_back(tr);

        valid++;
        if (ctrl_reversed) ctrl_rev++;
        if (stimulus_reversed) { stim_rev++; if (latency > 0) latencies.push_back(latency); }

        if (verbose) {
            std::cout << "    Trial " << std::setw(2) << (trial + 1)
                      << ": ctrl=" << (ctrl_reversed ? "rev" : "fwd")
                      << "  stim=" << (stimulus_reversed ? "rev" : "fwd");
            if (stimulus_reversed && latency > 0)
                std::cout << " (" << std::fixed << std::setprecision(0) << latency << "ms)";
            std::cout << "\n";
        }
    }

    res.valid_trials = valid;
    res.control_reversals = ctrl_rev;
    res.stim_reversals = stim_rev;
    res.control_rate = valid > 0 ? static_cast<double>(ctrl_rev) / valid : 0;
    res.stim_rate = valid > 0 ? static_cast<double>(stim_rev) / valid : 0;
    res.rate_increase = res.stim_rate - res.control_rate;

    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        double sum = 0; for (double l : latencies) sum += l;
        res.mean_latency_ms = sum / latencies.size();
    } else {
        res.mean_latency_ms = 0;
    }

    return res;
}

// ================================================================
// Print experiment result
// ================================================================
static void print_result(const ExperimentResult& r) {
    std::cout << "  有效试验:      " << r.valid_trials << "/" << r.total_trials << "\n";
    std::cout << "  对照反转率:    " << std::fixed << std::setprecision(0)
              << r.control_rate * 100 << "% (" << r.control_reversals
              << "/" << r.valid_trials << ") — 自发反转基线\n";
    std::cout << "  刺激反转率:    " << r.stim_rate * 100 << "% (" << r.stim_reversals
              << "/" << r.valid_trials << ") — 触觉诱发\n";
    std::cout << "  反转率提升:    ";
    if (r.rate_increase > 0)
        std::cout << "+" << r.rate_increase * 100 << " 百分点\n";
    else
        std::cout << r.rate_increase * 100 << " 百分点\n";
    if (r.mean_latency_ms > 0) {
        std::cout << "  平均延迟:      " << std::setprecision(0)
                  << r.mean_latency_ms << " ms\n";
    }
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    int seed = 42;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--seed" || arg == "-s") && i + 1 < argc) {
            seed = std::stoi(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: touch_analyzer [OPTIONS]\n"
                      << "  --seed N        Random seed (default: 42)\n"
                      << "  --verbose       Show per-trial details\n"
                      << "  --help          Show this help\n";
            return 0;
        }
    }

    Logger::instance().set_level(LogLevel::WARN);

    // Parameters
    const int N_TRIALS = 30;
    const double STIM_DURATION = 200.0;     // 200ms touch pulse (Chalfie 1985)
    const double WINDOW = 1000.0;           // 1s observation window (Porto 2019: peak ~200ms, decay ~400ms)

    std::cout << "========================================\n";
    std::cout << "  触觉回避分析器\n";
    std::cout << "  Touch Avoidance Analyzer\n";
    std::cout << "========================================\n\n";
    std::cout << "  随机种子:      " << seed << "\n";
    std::cout << "  试验次数:      " << N_TRIALS << "\n";
    std::cout << "  刺激时长:      " << STIM_DURATION << " ms\n";
    std::cout << "  观察窗口:      " << WINDOW << " ms\n";
    std::cout << "  实验设计:      配对对照 (control→gap→stimulus)\n";
    std::cout << "  REF: Porto 2019 — peak latency ~200ms\n";
    std::cout << "       Kumar 2023 — AVA activation bypasses gating\n\n";

    // Define stimulus configurations
    // Test 1: ALM only (minimal, may be weak)
    std::vector<std::pair<std::string,double>> stim_alm = {
        {"ALM", 50.0}, {"AVM", 50.0}
    };
    // Test 2: Full anterior touch (realistic wall collision)
    // ALM(50) + AVM(50) + OLQ(30) + FLP(50) + IL1(21) = what happens at wall
    std::vector<std::pair<std::string,double>> stim_full = {
        {"ALM", 50.0}, {"AVM", 50.0}, {"OLQ", 30.0}, {"FLP", 50.0}, {"IL1", 21.0}
    };
    // Test 3: Direct AVA injection (positive control — Kumar 2023)
    // Should ALWAYS trigger reversal regardless of circuit state
    std::vector<std::pair<std::string,double>> stim_ava = {
        {"AVA", 60.0}
    };
    // Test 4: Posterior touch (PLM)
    std::vector<std::pair<std::string,double>> stim_plm = {
        {"PLM", 50.0}
    };

    // ================================================================
    // Test 1: AVA direct injection (positive control)
    // ================================================================
    std::cout << "========================================\n";
    std::cout << "  1. AVA 直接注入 (正控)\n";
    std::cout << "========================================\n\n";
    std::cout << "  刺激: AVA 60pA — 绕过所有上游回路\n";
    std::cout << "  预期: 高反转率 (Kumar 2023: AVA 不受门控)\n\n";

    auto ava_ctrl = run_experiment(
        "AVA\xe6\xb3\xa8\xe5\x85\xa5", stim_ava, STIM_DURATION, WINDOW,
        N_TRIALS, seed, false, verbose);

    std::cout << "\n";
    print_result(ava_ctrl);

    if (ava_ctrl.rate_increase > 0.15) {
        std::cout << "  \xe2\x9c\x93 AVA 正控通过 — 注入机制工作正常\n";
    } else {
        std::cout << "  \xe2\x9c\x97 AVA 正控失败 — 注入机制或反转检测有问题\n";
    }

    // ================================================================
    // Test 2: ALM-only touch
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  2. ALM 单独刺激 (50pA)\n";
    std::cout << "========================================\n\n";
    std::cout << "  回路: ALM \xe2\x86\x92(gj) AVD \xe2\x86\x92(syn) AVA\n";
    std::cout << "  注: ALM \xe5\x8d\x95\xe7\x8b\xac可能不足以驱动反转\n\n";

    auto alm_only = run_experiment(
        "ALM\xe5\x8d\x95\xe7\x8b\xac", stim_alm, STIM_DURATION, WINDOW,
        N_TRIALS, seed, false, verbose);

    std::cout << "\n";
    print_result(alm_only);

    if (alm_only.rate_increase > 0.10) {
        std::cout << "  \xe2\x9c\x93 ALM 单独可驱动反转\n";
    } else {
        std::cout << "  \xe2\x96\xb3 ALM 单独增益不足 — 符合多神经元协同预期\n";
    }

    // ================================================================
    // Test 3: Full anterior touch (realistic collision)
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  3. \xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6 (ALM+AVM+OLQ+FLP+IL1)\n";
    std::cout << "========================================\n\n";
    std::cout << "  \xe5\x88\xba\xe6\xbf\x80: ALM(50) + AVM(50) + OLQ(30) + FLP(50) + IL1(21) pA\n";
    std::cout << "  = \xe7\x9c\x9f\xe5\xae\x9e\xe5\xa3\x81\xe7\xa2\xb0\xe6\x92\x9e\xe6\x97\xb6\xe7\x9a\x84\xe5\xa4\x9a\xe7\xa5\x9e\xe7\xbb\x8f\xe5\x85\x83\xe5\x8d\x8f\xe5\x90\x8c\xe6\xbf\x80\xe6\xb4\xbb\n\n";

    auto full_touch = run_experiment(
        "\xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6", stim_full, STIM_DURATION, WINDOW,
        N_TRIALS, seed, false, verbose);

    std::cout << "\n";
    print_result(full_touch);

    if (full_touch.rate_increase > 0.15) {
        std::cout << "  \xe2\x9c\x93 \xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6\xe6\x98\xbe\xe8\x91\x97\xe6\x8f\x90\xe5\x8d\x87\xe5\x8f\x8d\xe8\xbd\xac\xe7\x8e\x87\n";
    } else if (full_touch.rate_increase > 0.05) {
        std::cout << "  \xe2\x96\xb3 \xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6\xe6\x9c\x89\xe4\xb8\x80\xe5\xae\x9a\xe6\x95\x88\xe6\x9e\x9c\n";
    } else {
        std::cout << "  \xe2\x9c\x97 \xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6\xe6\x95\x88\xe6\x9e\x9c\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97 \xe2\x80\x94 \xe4\xbf\xa1\xe5\x8f\xb7\xe9\x93\xbe\xe5\xa2\x9e\xe7\x9b\x8a\xe4\xb8\x8d\xe8\xb6\xb3\n";
    }

    // ================================================================
    // Test 4: ALM-ablated full touch (ablation control)
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  4. ALM \xe6\xb6\x88\xe8\x9e\x8d\xe5\xaf\xb9\xe7\x85\xa7 (\xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6 - ALM/AVM)\n";
    std::cout << "========================================\n\n";
    std::cout << "  \xe6\xb6\x88\xe8\x9e\x8d: ALML + ALMR + AVM\n";
    std::cout << "  \xe5\x88\xba\xe6\xbf\x80: \xe4\xbb\x8d\xe5\x90\xab OLQ+FLP+IL1 (\xe6\xb5\x8b ALM \xe8\xb4\xa1\xe7\x8c\xae)\n\n";

    auto ablated = run_experiment(
        "ALM\xe6\xb6\x88\xe8\x9e\x8d", stim_full, STIM_DURATION, WINDOW,
        N_TRIALS, seed, true, verbose);

    std::cout << "\n";
    print_result(ablated);

    double ft_inc = full_touch.rate_increase;
    double ab_inc = ablated.rate_increase;
    if (ft_inc > 0.05 && ab_inc < ft_inc * 0.5) {
        std::cout << "  \xe2\x9c\x93 ALM \xe6\xb6\x88\xe8\x9e\x8d\xe6\x98\xbe\xe8\x91\x97\xe9\x99\x8d\xe4\xbd\x8e\xe5\x89\x8d\xe8\xa7\xa6\xe6\x95\x88\xe6\x9e\x9c (Chalfie 1985)\n";
    } else {
        std::cout << "  \xe2\x96\xb3 OLQ/FLP/IL1 \xe6\x8f\x90\xe4\xbe\x9b\xe9\x83\xa8\xe5\x88\x86\xe5\x86\x97\xe4\xbd\x99 (Kaplan 1993: FLP=29%)\n";
    }

    // ================================================================
    // Summary
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  \xe8\xa7\xa6\xe8\xa7\x89\xe5\x9b\x9e\xe9\x81\xbf\xe6\x80\xbb\xe7\xbb\x93\n";
    std::cout << "========================================\n\n";

    std::cout << "  \xe5\xae\x9e\xe9\xaa\x8c            \xe5\xaf\xb9\xe7\x85\xa7    \xe5\x88\xba\xe6\xbf\x80    \xe6\x8f\x90\xe5\x8d\x87    \xe5\xbb\xb6\xe8\xbf\x9f      \xe5\x88\xa4\xe5\xae\x9a\n";
    std::cout << "  ----------    -----   -----   -----   ------    ----\n";

    auto pr = [](const ExperimentResult& r) {
        std::cout << "  " << std::setw(12) << std::left << r.label << std::right
                  << std::fixed << std::setprecision(0)
                  << std::setw(4) << r.control_rate * 100 << "%"
                  << "   " << std::setw(4) << r.stim_rate * 100 << "%"
                  << "   ";
        if (r.rate_increase >= 0)
            std::cout << "+" << std::setw(3) << static_cast<int>(r.rate_increase * 100) << "%";
        else
            std::cout << std::setw(4) << static_cast<int>(r.rate_increase * 100) << "%";
        std::cout << "   ";
        if (r.mean_latency_ms > 0)
            std::cout << std::setw(4) << static_cast<int>(r.mean_latency_ms) << " ms";
        else
            std::cout << "   N/A";
        std::cout << "    ";
        if (r.rate_increase > 0.15) std::cout << "\xe2\x9c\x93";
        else if (r.rate_increase > 0.05) std::cout << "\xe2\x96\xb3";
        else std::cout << "\xe2\x9c\x97";
        std::cout << "\n";
    };

    pr(ava_ctrl);
    pr(alm_only);
    pr(full_touch);
    pr(ablated);

    // Overall verdict
    bool ava_ok = ava_ctrl.rate_increase > 0.10;
    bool full_ok = full_touch.rate_increase > 0.10;
    std::cout << "\n  \xe6\x80\xbb\xe4\xbd\x93: ";
    if (ava_ok && full_ok)
        std::cout << "\xe2\x9c\x93 \xe8\xa7\xa6\xe8\xa7\x89\xe5\x9b\x9e\xe9\x81\xbf\xe5\x9b\x9e\xe8\xb7\xaf\xe5\x8a\x9f\xe8\x83\xbd\xe6\xad\xa3\xe5\xb8\xb8 (AVA \xe6\xad\xa3\xe6\x8e\xa7 + \xe5\xae\x8c\xe6\x95\xb4\xe5\x89\x8d\xe8\xa7\xa6\xe6\xb6\x8c\xe7\x8e\xb0)\n";
    else if (ava_ok && !full_ok && alm_only.rate_increase <= 0.05)
        std::cout << "\xe2\x96\xb3 AVA \xe6\xad\xa3\xe6\x8e\xa7\xe9\x80\x9a\xe8\xbf\x87, \xe4\xbd\x86\xe8\xa7\xa6\xe8\xa7\x89" "\xe2\x86\x92" "AVA \xe4\xbf\xa1\xe5\x8f\xb7\xe9\x93\xbe\xe5\xa2\x9e\xe7\x9b\x8a\xe4\xb8\x8d\xe8\xb6\xb3\n"
              << "    \xe5\xbb\xba\xe8\xae\xae: \xe5\xa2\x9e\xe5\xbc\xba ALM" "\xe2\x86\x92" "AVD gap junction \xe6\x88\x96 AVD" "\xe2\x86\x92" "AVA synapse \xe6\x9d\x83\xe9\x87\x8d\n";
    else if (!ava_ok)
        std::cout << "\xe2\x9c\x97 AVA \xe6\xad\xa3\xe6\x8e\xa7\xe5\xa4\xb1\xe8\xb4\xa5 \xe2\x80\x94 \xe6\xb3\xa8\xe5\x85\xa5\xe6\x9c\xba\xe5\x88\xb6\xe6\x88\x96\xe5\x8f\x8d\xe8\xbd\xac\xe6\xa3\x80\xe6\xb5\x8b\xe5\xbc\x82\xe5\xb8\xb8\n";
    else
        std::cout << "\xe2\x96\xb3 \xe9\x83\xa8\xe5\x88\x86\xe5\x8a\x9f\xe8\x83\xbd\xe6\xad\xa3\xe5\xb8\xb8, \xe9\x9c\x80\xe8\xbf\x9b\xe4\xb8\x80\xe6\xad\xa5\xe8\xb0\x83\xe5\x8f\x82\n";

    std::cout << "\n";
    return 0;
}
