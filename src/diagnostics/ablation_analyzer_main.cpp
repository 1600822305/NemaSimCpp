// ablation_analyzer_main.cpp — Step 129: Virtual Laser Ablation CI Attribution
// Systematically ablates individual neurons/pairs and measures CI change.
// Identifies which neurons HELP chemotaxis (CI drops when ablated)
// and which HURT chemotaxis (CI rises when ablated = they are the problem).
//
// Usage: ablation_analyzer [--duration 300] [--seed 42] [--verbose]
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>
#include <mutex>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32
#include <windows.h>
#undef ERROR
#undef WARNING
#undef min
#undef max
#endif

using namespace celegans;

struct AblationResult {
    std::string target;       // neuron(s) ablated
    double ci;
    double mean_speed;
    double fwd_pct;
    double rev_pct;
    double omega_pct;
    int reversals;
    double heading_bias;
    double mean_dist;         // mean distance to food
};

static AblationResult run_ablation(
    const std::string& target,
    const std::vector<std::string>& ablate_names,
    int seed, double duration_s,
    Vector2d food_pos)
{
    AblationResult res;
    res.target = target;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    // Setup environment (same as behavior_analyzer)
    sim.environment().chemical_field().clear();
    sim.environment().chemical_field().add_point_source(food_pos, 1.0);
    sim.environment().soluble_field().clear();
    sim.environment().soluble_field().add_point_source(food_pos, 0.4);
    sim.reset_transducers();

    // Ablate target neurons
    auto& neurons = sim.neurons();
    int nn = static_cast<int>(neurons.size());
    for (const auto& name : ablate_names) {
        for (int i = 0; i < nn; ++i) {
            const std::string& nname = neurons[i]->info().name;
            // Match prefix for L/R pairs: "AWC" matches "AWCL","AWCR"
            // Match exact for specific: "AVAL" matches only "AVAL"
            if (nname == name ||
                (name.size() >= 3 && nname.compare(0, name.size(), name) == 0)) {
                neurons[i]->ablate();
            }
        }
    }

    double dt_ms = sim.dt();
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt_ms);
    int sample_interval = static_cast<int>(100.0 / dt_ms); // every 100ms

    // Collect trajectory
    struct Sample {
        Vector2d pos;
        double heading;
        bool reversing, omega;
    };
    std::vector<Sample> samples;
    samples.reserve(total_steps / sample_interval + 1);

    for (int step = 0; step < total_steps; ++step) {
        sim.step();
        if (step % sample_interval != 0) continue;
        Sample s;
        s.pos = sim.body().get_head_position();
        s.heading = sim.body().get_head_angle();
        s.reversing = sim.is_reversing();
        s.omega = sim.is_omega_turning();
        samples.push_back(s);
    }

    // Compute metrics
    double start_dist = (samples.front().pos - food_pos).norm();
    double end_dist = (samples.back().pos - food_pos).norm();
    double total_path = 0;
    int fwd_n = 0, rev_n = 0, omg_n = 0;
    int rev_events = 0;
    bool prev_rev = false;
    double dist_sum = 0;

    // Heading bias
    double toward = 0, away = 0;

    for (size_t i = 1; i < samples.size(); ++i) {
        double dx = samples[i].pos.x - samples[i-1].pos.x;
        double dy = samples[i].pos.y - samples[i-1].pos.y;
        total_path += std::sqrt(dx*dx + dy*dy);
        dist_sum += (samples[i].pos - food_pos).norm();

        if (samples[i].omega) omg_n++;
        else if (samples[i].reversing) rev_n++;
        else fwd_n++;

        if (samples[i].reversing && !prev_rev) rev_events++;
        prev_rev = samples[i].reversing;

        // Heading bias (forward only)
        if (!samples[i].reversing && !samples[i].omega) {
            Vector2d to_food = food_pos - samples[i].pos;
            double food_angle = std::atan2(to_food.y, to_food.x) - samples[i].heading;
            while (food_angle > M_PI) food_angle -= 2*M_PI;
            while (food_angle < -M_PI) food_angle += 2*M_PI;

            double dh = samples[i].heading - samples[i-1].heading;
            while (dh > M_PI) dh -= 2*M_PI;
            while (dh < -M_PI) dh += 2*M_PI;

            bool tw = (food_angle > 0 && dh > 0) || (food_angle < 0 && dh < 0);
            double adh = std::abs(dh);
            if (tw) toward += adh; else away += adh;
        }
    }

    int total_samples = fwd_n + rev_n + omg_n;
    res.ci = (total_path > 0) ? (start_dist - end_dist) / total_path : 0;
    res.mean_speed = total_path / duration_s;
    res.fwd_pct = (total_samples > 0) ? 100.0 * fwd_n / total_samples : 0;
    res.rev_pct = (total_samples > 0) ? 100.0 * rev_n / total_samples : 0;
    res.omega_pct = (total_samples > 0) ? 100.0 * omg_n / total_samples : 0;
    res.reversals = rev_events;
    res.heading_bias = (toward + away > 0) ? (toward - away) / (toward + away) : 0;
    res.mean_dist = (samples.size() > 1) ? dist_sum / (samples.size() - 1) : 0;

    return res;
}

int main(int argc, char* argv[]) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    double duration_s = 300.0;
    int seed = 42;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i+1 < argc)
            duration_s = std::atof(argv[++i]);
        else if ((arg == "--seed" || arg == "-s") && i+1 < argc)
            seed = std::atoi(argv[++i]);
        else if (arg == "--verbose" || arg == "-v")
            verbose = true;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: ablation_analyzer [--duration N] [--seed N] [--verbose]\n";
            return 0;
        }
    }

    Logger::instance().set_level(LogLevel::ERROR);

    Vector2d food_pos{35.0, 25.0};

    // Define ablation targets: neuron name prefix → ablates all L/R variants
    struct Target {
        std::string label;
        std::vector<std::string> names;
        std::string expected;  // expected phenotype from literature
    };

    std::vector<Target> targets = {
        {"(none)",      {},           "baseline"},
        // --- Sensory ---
        {"AWC",         {"AWC"},      "CI↓↓ (main olfactory, Bargmann 1993)"},
        {"ASE",         {"ASE"},      "CI↓ (taste, Pierce-Shimomura 1999)"},
        {"AWA",         {"AWA"},      "CI↓ (diacetyl attraction, Bargmann 1993)"},
        {"ASH",         {"ASH"},      "CI↑? (remove nociception → less reversal)"},
        // --- First layer interneurons ---
        {"AIY",         {"AIY"},      "CI↓↓ (forward promotion lost, Tsalik 2003)"},
        {"AIB",         {"AIB"},      "CI↑? (reversal promotion lost)"},
        {"AIA",         {"AIA"},      "CI↓ (coincidence detector, Ghosh 2017)"},
        {"AIZ",         {"AIZ"},      "CI↓ (navigation relay)"},
        // --- Klinotaxis circuit ---
        {"RIA",         {"RIA"},      "CI↓ (klinotaxis lost, Hendricks 2012)"},
        {"SMD",         {"SMD"},      "CI↓ (head steering lost)"},
        {"SMB",         {"SMB"},      "CI↓ (neck curvature bias lost)"},
        // --- Command neurons ---
        {"AVA",         {"AVA"},      "no reversal → straight runs (Chalfie 1985)"},
        {"AVB",         {"AVB"},      "no forward → stuck (Chalfie 1985)"},
        {"AVD",         {"AVD"},      "fewer reversals"},
        {"AVE",         {"AVE"},      "fewer reversals"},
        // --- Modulatory ---
        {"RIM",         {"RIM"},      "less TA → fewer omega (Alkema 2005)"},
        {"NSM",         {"NSM"},      "no food 5-HT → no slowing (Sawin 2000)"},
        {"RIS",         {"RIS"},      "no sleep (Turek 2016)"},
        // --- Motor ---
        {"RIV",         {"RIV"},      "no omega turns (Gray 2005)"},
    };

    std::cout << "========================================\n";
    std::cout << "  消融归因分析器 (Ablation Analyzer)\n";
    std::cout << "========================================\n\n";
    std::cout << "  仿真时长:  " << duration_s << " s\n";
    std::cout << "  随机种子:  " << seed << "\n";
    std::cout << "  食物位置:  (" << food_pos.x << ", " << food_pos.y << ")\n";
    std::cout << "  消融目标:  " << targets.size() << " 个\n\n";

    // Run all ablations (parallel)
    std::vector<AblationResult> results(targets.size());
    std::mutex print_mtx;
    std::atomic<int> completed{0};
    int n_targets = static_cast<int>(targets.size());
    int n_threads = std::min(8, n_targets);

    std::cout << "  运行中... " << std::flush;

    auto worker = [&](int tid) {
        for (int idx = tid; idx < n_targets; idx += n_threads) {
            results[idx] = run_ablation(
                targets[idx].label, targets[idx].names,
                seed, duration_s, food_pos);
            int done = ++completed;
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << "[" << done << "/" << n_targets << "] " << std::flush;
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < n_threads; ++t) threads.emplace_back(worker, t);
    for (auto& t : threads) t.join();
    std::cout << "完成！\n\n";

    // Baseline
    double baseline_ci = results[0].ci;

    // Results table
    std::cout << "========================================\n";
    std::cout << "  ABLATION RESULTS\n";
    std::cout << "========================================\n\n";

    std::cout << std::fixed;
    std::cout << "  " << std::left << std::setw(8) << "Target"
              << std::right
              << std::setw(8) << "CI"
              << std::setw(8) << "ΔCI"
              << std::setw(8) << "Fwd%"
              << std::setw(8) << "Rev%"
              << std::setw(8) << "Omg%"
              << std::setw(6) << "Rev#"
              << std::setw(8) << "H_bias"
              << std::setw(8) << "Spd"
              << std::setw(8) << "Dist"
              << "  Effect\n";
    std::cout << "  " << std::string(80, '-') << "\n";

    for (size_t i = 0; i < results.size(); ++i) {
        auto& r = results[i];
        double dci = r.ci - baseline_ci;

        std::string effect;
        if (i == 0) effect = "BASELINE";
        else if (dci > 0.05) effect = "HELPS (CI↑ = neuron was HURTING)";
        else if (dci > 0.02) effect = "helps";
        else if (dci < -0.05) effect = "NEEDED (CI↓↓ = neuron was HELPING)";
        else if (dci < -0.02) effect = "needed";
        else effect = "~neutral";

        std::cout << "  " << std::left << std::setw(8) << r.target
                  << std::right << std::setprecision(3)
                  << std::setw(8) << r.ci
                  << std::setw(8) << (i == 0 ? 0.0 : dci)
                  << std::setprecision(1)
                  << std::setw(8) << r.fwd_pct
                  << std::setw(8) << r.rev_pct
                  << std::setw(8) << r.omega_pct
                  << std::setw(6) << r.reversals
                  << std::setprecision(3)
                  << std::setw(8) << r.heading_bias
                  << std::setprecision(3)
                  << std::setw(8) << r.mean_speed
                  << std::setprecision(1)
                  << std::setw(8) << r.mean_dist
                  << "  " << effect
                  << "\n";
    }

    // Summary
    std::cout << "\n========================================\n";
    std::cout << "  DIAGNOSIS\n";
    std::cout << "========================================\n\n";

    // Sort by ΔCI to find biggest helpers and biggest hurters
    struct Ranked { std::string name; double dci; std::string expected; };
    std::vector<Ranked> ranked;
    for (size_t i = 1; i < results.size(); ++i) {
        ranked.push_back({results[i].target, results[i].ci - baseline_ci, targets[i].expected});
    }
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        return a.dci > b.dci;
    });

    std::cout << "  CI提升 (消融后CI上升 = 该神经元在损害趋化):\n";
    for (auto& r : ranked) {
        if (r.dci > 0.01) {
            std::cout << "    " << std::left << std::setw(8) << r.name
                      << " ΔCI=+" << std::setprecision(3) << r.dci
                      << "  预期: " << r.expected << "\n";
        }
    }

    std::cout << "\n  CI下降 (消融后CI下降 = 该神经元对趋化重要):\n";
    for (auto& r : ranked) {
        if (r.dci < -0.01) {
            std::cout << "    " << std::left << std::setw(8) << r.name
                      << " ΔCI=" << std::setprecision(3) << r.dci
                      << "  预期: " << r.expected << "\n";
        }
    }

    // Phenotype validation
    std::cout << "\n  表型验证 (对照文献):\n";
    bool ava_no_rev = false, avb_stuck = false;
    for (size_t i = 1; i < results.size(); ++i) {
        if (targets[i].label == "AVA" && results[i].rev_pct < 5.0) {
            std::cout << "    ✓ AVA消融: 反转=" << std::setprecision(1) << results[i].rev_pct
                      << "% (预期: ~0%, Chalfie 1985)\n";
            ava_no_rev = true;
        }
        if (targets[i].label == "AVB" && results[i].fwd_pct < 20.0) {
            std::cout << "    ✓ AVB消融: 前进=" << std::setprecision(1) << results[i].fwd_pct
                      << "% (预期: ~0%, Chalfie 1985)\n";
            avb_stuck = true;
        }
        if (targets[i].label == "RIV" && results[i].omega_pct < 3.0) {
            std::cout << "    ✓ RIV消融: Omega=" << std::setprecision(1) << results[i].omega_pct
                      << "% (预期: ~0%, Gray 2005)\n";
        }
        if (targets[i].label == "AIY") {
            double dci = results[i].ci - baseline_ci;
            std::cout << "    " << (dci < -0.02 ? "✓" : "✗") << " AIY消融: ΔCI="
                      << std::setprecision(3) << dci
                      << " (预期: CI↓↓, Tsalik 2003)\n";
        }
    }
    if (!ava_no_rev) std::cout << "    ✗ AVA消融: 反转未消除 (异常)\n";
    if (!avb_stuck) std::cout << "    ✗ AVB消融: 前进未消除 (异常)\n";

    std::cout << "\n";
    return 0;
}
