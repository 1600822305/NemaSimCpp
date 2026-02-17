// multisensory_analyzer_main.cpp — Multi-Sensory Integration Test
//
// Tests decision-making when multiple sensory cues conflict.
// Compares chemotaxis index across 5 scenarios:
//   A. Baseline chemotaxis (food only)
//   B. Food + repellent conflict (AIA coincidence detection)
//   C. Food at non-preferred temperature (chemotaxis vs thermotaxis)
//   D. Food at high O2 edge (chemotaxis vs aerotaxis)
//   E. Osmotic ring barrier (ASH/OSM-9 avoidance, Colbert 1997)
//
// REF: Ghosh 2017 Curr Opin Neurobiol — multisensory integration in C. elegans
//      Shinkai 2011 J Neurosci — thermotaxis/chemotaxis interaction
//      Gray 2004 Nature — O2 sensing and food
//
// Usage: multisensory_analyzer [--duration N] [--seed N] [--verbose] [--help]

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
// Scenario result
// ================================================================
struct ScenarioResult {
    std::string name;
    double ci;              // chemotaxis index: (start_dist - end_dist) / total_path
    double start_distance;  // initial distance to food (mm)
    double mean_distance;   // mean distance to food (mm)
    double final_distance;  // final distance to food (mm)
    double mean_speed;      // mm/s
    int reversals;          // total reversal count
    double total_path;      // total path length (mm)
};

// ================================================================
// Run one scenario
// ================================================================
static ScenarioResult run_scenario(
    const std::string& name,
    double duration_s,
    int seed,
    // Environment config
    Vector2d food_pos,
    double food_strength,
    bool add_repellent,
    Vector2d repellent_pos,
    double repellent_strength,
    bool add_temp_gradient,
    double temp_center,
    Vector2d temp_grad_dir,
    double temp_grad_strength,
    bool verbose,
    // Step 127: osmotic avoidance
    bool add_osmotic = false,
    Vector2d osmo_center = {0,0},
    double osmo_radius = 0,
    double osmo_strength = 0,
    // Step 127: CO₂ avoidance
    bool add_co2 = false,
    Vector2d co2_pos = {0,0},
    double co2_strength = 0)
{
    ScenarioResult res;
    res.name = name;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    // Configure environment (clear defaults from initialize_default)
    auto& env = sim.environment();
    env.chemical_field().clear();
    env.soluble_field().clear();
    env.chemical_field().add_point_source(food_pos, food_strength);
    env.soluble_field().add_point_source(food_pos, 0.4);  // ASE salt/amino acid channel

    if (add_repellent) {
        env.repellent_field().add_point_source(repellent_pos, repellent_strength, 25.0);
    }

    if (add_temp_gradient) {
        env.set_temperature_gradient(temp_center, temp_grad_dir, temp_grad_strength);
    }

    if (add_osmotic) {
        env.set_osmotic_region(osmo_center, osmo_radius, osmo_strength);
    }

    if (add_co2) {
        env.set_co2_source(co2_pos, co2_strength);
    }

    sim.reset_transducers();

    double dt = sim.dt();
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt);
    int warmup_steps = static_cast<int>(3000.0 / dt);  // 3s warmup
    constexpr int SAMPLE_EVERY = 40;  // every 20ms

    double sum_dist = 0;
    double sum_speed = 0;
    double total_path = 0;
    int n_samples = 0;
    int reversals = 0;
    bool was_reversing = false;
    double start_dist = -1;
    Vector2d prev_pos = {0, 0};

    for (int step = 0; step < total_steps; ++step) {
        sim.step();

        if (step < warmup_steps) continue;
        if (step % SAMPLE_EVERY != 0) continue;

        auto head = sim.body().segments()[0].position;
        double dx = head.x - food_pos.x;
        double dy = head.y - food_pos.y;
        double dist = std::sqrt(dx * dx + dy * dy);

        if (n_samples == 0) {
            start_dist = dist;
            prev_pos = head;
        } else {
            double pdx = head.x - prev_pos.x;
            double pdy = head.y - prev_pos.y;
            total_path += std::sqrt(pdx * pdx + pdy * pdy);
            prev_pos = head;
        }

        sum_dist += dist;
        sum_speed += sim.body().get_speed();
        n_samples++;

        // Verbose trajectory: print every 30s
        if (verbose) {
            double t = (step * dt) / 1000.0;
            double prev_t = ((step - SAMPLE_EVERY) * dt) / 1000.0;
            int t30 = static_cast<int>(t) / 30;
            int pt30 = static_cast<int>(prev_t) / 30;
            if (t30 != pt30 || n_samples == 1) {
                std::cout << "    t=" << std::fixed << std::setprecision(0) << t
                          << "s pos=(" << std::setprecision(1) << head.x << "," << head.y
                          << ") dist=" << std::setprecision(1) << dist << "mm"
                          << " spd=" << std::setprecision(3) << sim.body().get_speed() << "\n";
            }
        }

        bool rev = sim.is_reversing();
        if (rev && !was_reversing) reversals++;
        was_reversing = rev;

        res.final_distance = dist;
    }

    res.start_distance = start_dist;
    res.ci = (total_path > 0.01) ? (start_dist - res.final_distance) / total_path : 0;
    res.mean_distance = (n_samples > 0) ? sum_dist / n_samples : 0;
    res.mean_speed = (n_samples > 0) ? sum_speed / n_samples : 0;
    res.reversals = reversals;
    res.total_path = total_path;

    return res;
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    double duration_s = 300.0;
    int seed = 42;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            duration_s = std::stod(argv[++i]);
        } else if ((arg == "--seed" || arg == "-s") && i + 1 < argc) {
            seed = std::stoi(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: multisensory_analyzer [OPTIONS]\n"
                      << "  --duration N    Simulation duration in seconds (default: 60)\n"
                      << "  --seed N        Random seed (default: 42)\n"
                      << "  --verbose       Show detailed info\n"
                      << "  --help          Show this help\n";
            return 0;
        }
    }

    Logger::instance().set_level(LogLevel::WARN);

    // Arena center; worm starts at center, food placed 15mm away
    double cx = 25.0, cy = 25.0;
    Vector2d food_pos = {cx + 20.0, cy};  // 20mm right of center (off-food: Gaussian σ=12mm → C=0.25 at start)

    std::cout << "========================================\n";
    std::cout << "  " "\xe5\xa4\x9a\xe6\x84\x9f\xe8\xa7\x89\xe6\x95\xb4\xe5\x90\x88\xe5\x88\x86\xe6\x9e\x90\xe5\x99\xa8" "\n";
    std::cout << "  Multisensory Integration Analyzer\n";
    std::cout << "========================================\n\n";
    std::cout << "  " "\xe4\xbb\xbf\xe7\x9c\x9f\xe6\x97\xb6\xe9\x95\xbf" ":    " << duration_s << " s\n";
    std::cout << "  " "\xe9\x9a\x8f\xe6\x9c\xba\xe7\xa7\x8d\xe5\xad\x90" ":    " << seed << "\n";
    std::cout << "  " "\xe7\xab\x9e\xe6\x8a\x80\xe5\x9c\xba" ":        50x50 mm\n";
    std::cout << "  " "\xe8\x99\xab\xe8\xb5\xb7\xe7\x82\xb9" ":      (" << cx << "," << cy << ") mm\n";
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9\xe8\xb7\x9d\xe7\xa6\xbb" ":    20 mm\n";
    std::cout << "  REF: Ghosh 2017, Shinkai 2011, Gray 2004\n\n";

    // ================================================================
    // Scenario A: Baseline chemotaxis (food only, 15mm away)
    // ================================================================
    std::cout << "========================================\n";
    std::cout << "  A. " "\xe5\x9f\xba\xe7\xba\xbf\xe8\xb6\x8b\xe5\x8c\x96" " (" "\xe4\xbb\x85\xe9\xa3\x9f\xe7\x89\xa9" ")\n";
    std::cout << "========================================\n\n";
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9" ": (" << food_pos.x << ", " << food_pos.y << ") mm\n";
    std::cout << "  " "\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe6\x97\xa0" "\n\n";

    auto baseline = run_scenario(
        "baseline", duration_s, seed,
        food_pos, 1.0,              // food 15mm from start
        false, {0,0}, 0,
        false, 0, {0,0}, 0,
        verbose);

    std::cout << "  CI:            " << std::fixed << std::setprecision(3) << baseline.ci << "\n";
    std::cout << "  " "\xe5\x9d\x87\xe8\xb7\x9d" ":          " << std::setprecision(1) << baseline.mean_distance << " mm\n";
    std::cout << "  " "\xe8\xb7\xaf\xe5\xbe\x84" ":          " << std::setprecision(1) << baseline.total_path << " mm\n";
    std::cout << "  " "\xe5\x8f\x8d\xe8\xbd\xac" ":          " << baseline.reversals << "\n";

    // ================================================================
    // Scenario B: Food + Repellent co-located
    // AIA must integrate AWA/AWC (attraction) + ASH (avoidance)
    // Repellent at food location → ASH fires as worm approaches food
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  B. " "\xe9\xa3\x9f\xe7\x89\xa9" "+" "\xe6\x96\xa5\xe5\x8a\x9b\xe5\x86\xb2\xe7\xaa\x81" "\n";
    std::cout << "========================================\n\n";
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9" ": (" << food_pos.x << ", " << food_pos.y << ") mm\n";
    std::cout << "  " "\xe6\x96\xa5\xe5\x8a\x9b" ": " "\xe5\x90\x8c\xe4\xbd\x8d\xe7\xbd\xae" " (" "\xe5\xbc\xba\xe5\xba\xa6" " 0.8)\n";
    std::cout << "  " "\xe6\x95\xb4\xe5\x90\x88" ": AIA " "\xe5\xb7\xa7\xe5\x90\x88\xe6\xa3\x80\xe6\xb5\x8b" " (Ghosh 2017)\n\n";

    auto food_repel = run_scenario(
        "food+repel", duration_s, seed,
        food_pos, 1.0,
        true, food_pos, 0.8,        // repellent co-located with food
        false, 0, {0,0}, 0,
        verbose);

    std::cout << "  CI:            " << std::setprecision(3) << food_repel.ci << "\n";
    std::cout << "  " "\xe5\x9d\x87\xe8\xb7\x9d" ":          " << std::setprecision(1) << food_repel.mean_distance << " mm\n";
    std::cout << "  " "\xe8\xb7\xaf\xe5\xbe\x84" ":          " << std::setprecision(1) << food_repel.total_path << " mm\n";
    std::cout << "  " "\xe5\x8f\x8d\xe8\xbd\xac" ":          " << food_repel.reversals << "\n";

    double ci_drop = baseline.ci - food_repel.ci;
    if (ci_drop > 0.05) {
        std::cout << "  " "\xe2\x9c\x93" " " "\xe6\x96\xa5\xe5\x8a\x9b\xe9\x99\x8d\xe4\xbd\x8e\xe8\xb6\x8b\xe5\x8c\x96" " (CI " "\xe4\xb8\x8b\xe9\x99\x8d" " " << std::setprecision(3) << ci_drop << ")\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " " "\xe6\x96\xa5\xe5\x8a\x9b\xe5\xbd\xb1\xe5\x93\x8d\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97" "\n";
    }

    // ================================================================
    // Scenario C: Food at warm side (thermotaxis conflict)
    // Food at same location but with temp gradient making it warm
    // AFD drives away from T>Tc, AWC/AWA drive toward food
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  C. " "\xe9\xa3\x9f\xe7\x89\xa9\xe5\x9c\xa8\xe9\xab\x98\xe6\xb8\xa9\xe5\x8c\xba" "\n";
    std::cout << "========================================\n\n";
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9" ": (" << food_pos.x << ", " << food_pos.y << ") mm\n";
    std::cout << "  " "\xe6\xb8\xa9\xe5\xba\xa6" ": 20" "\xc2\xb0" "C " "\xe4\xb8\xad\xe5\xbf\x83" ", +0.5" "\xc2\xb0" "C/mm " "\xe5\x90\x91\xe5\x8f\xb3" "\n";
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9\xe5\xa4\x84\xe6\xb8\xa9\xe5\xba\xa6" ": ~27.5" "\xc2\xb0" "C (>Tc=20" "\xc2\xb0" "C)\n";
    std::cout << "  " "\xe5\x86\xb2\xe7\xaa\x81" ": AFD " "\xe8\xb6\x8b\xe6\xb8\xa9" " vs AWC " "\xe8\xb6\x8b\xe5\x8c\x96" "\n\n";

    auto food_temp = run_scenario(
        "food+temp", duration_s, seed,
        food_pos, 1.0,
        false, {0,0}, 0,
        true, 20.0, {1.0, 0.0}, 0.5,  // +0.5°C/mm rightward → food at ~27.5°C
        verbose);

    std::cout << "  CI:            " << std::setprecision(3) << food_temp.ci << "\n";
    std::cout << "  " "\xe5\x9d\x87\xe8\xb7\x9d" ":          " << std::setprecision(1) << food_temp.mean_distance << " mm\n";
    std::cout << "  " "\xe8\xb7\xaf\xe5\xbe\x84" ":          " << std::setprecision(1) << food_temp.total_path << " mm\n";
    std::cout << "  " "\xe5\x8f\x8d\xe8\xbd\xac" ":          " << food_temp.reversals << "\n";

    // ================================================================
    // Scenario D: Food only, but far away (distance control)
    // Same food distance as edge scenario, no O2 conflict
    // Tests if reduced CI is from distance or O2
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  D. " "\xe9\xa3\x9f\xe7\x89\xa9\xe5\x9c\xa8\xe8\xbe\xb9\xe7\xbc\x98" " (" "\xe9\xab\x98" "O" "\xe2\x82\x82" ")\n";
    std::cout << "========================================\n\n";
    Vector2d food_edge = {cx + 23.0, cy};  // near right wall (23mm from center, 3mm from wall)
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9" ": (" << food_edge.x << ", " << food_edge.y << ") mm (" "\xe8\xbf\x91\xe5\xa3\x81" ")\n";
    std::cout << "  O" "\xe2\x82\x82" ": " "\xe8\xbe\xb9\xe7\xbc\x98" "=21% (" "\xe6\x97\xa0\xe7\xbb\x86\xe8\x8f\x8c" "), " "\xe4\xb8\xad\xe5\xbf\x83" "=~8%\n";
    std::cout << "  " "\xe5\x86\xb2\xe7\xaa\x81" ": URX " "\xe8\xb6\x8b\xe6\xb0\xa7" " vs AWC " "\xe8\xb6\x8b\xe5\x8c\x96" "\n\n";

    auto food_o2 = run_scenario(
        "food+O2", duration_s, seed,
        food_edge, 1.0,
        false, {0,0}, 0,
        false, 0, {0,0}, 0,
        verbose);

    std::cout << "  CI:            " << std::setprecision(3) << food_o2.ci << "\n";
    std::cout << "  " "\xe5\x9d\x87\xe8\xb7\x9d" ":          " << std::setprecision(1) << food_o2.mean_distance << " mm\n";
    std::cout << "  " "\xe8\xb7\xaf\xe5\xbe\x84" ":          " << std::setprecision(1) << food_o2.total_path << " mm\n";
    std::cout << "  " "\xe5\x8f\x8d\xe8\xbd\xac" ":          " << food_o2.reversals << "\n";

    // ================================================================
    // Summary
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  " "\xe5\xa4\x9a\xe6\x84\x9f\xe8\xa7\x89\xe6\x95\xb4\xe5\x90\x88\xe6\x80\xbb\xe7\xbb\x93" "\n";
    std::cout << "========================================\n\n";

    std::cout << "  " "\xe5\x9c\xba\xe6\x99\xaf" "          CI      " "\xe5\x9d\x87\xe8\xb7\x9d" "(mm)  " "\xe8\xb7\xaf\xe5\xbe\x84" "(mm) " "\xe5\x8f\x8d\xe8\xbd\xac" "\n";
    std::cout << "  ----------  ------  --------  -------  ----\n";

    auto pr = [](const ScenarioResult& r) {
        std::cout << "  " << std::setw(10) << std::left << r.name << std::right
                  << std::fixed
                  << "  " << std::setprecision(3) << std::setw(6) << r.ci
                  << "  " << std::setprecision(1) << std::setw(8) << r.mean_distance
                  << "  " << std::setprecision(1) << std::setw(7) << r.total_path
                  << "  " << std::setw(4) << r.reversals
                  << "\n";
    };

    // ================================================================
    // Scenario E: Osmotic ring assay (Colbert 1997)
    // Osmotic ring centered on WORM start position (not food)
    // Worm tries to leave → encounters high-osmolarity boundary → ASH reversal
    // Classic trapping assay: worm should stay inside ring
    // REF: Colbert 1997 — ring of high osmolarity glycerol traps worm
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  E. " "\xe6\xb8\x97\xe9\x80\x8f\xe5\x8e\x8b\xe7\x8e\xaf\xe5\xbd\xa2\xe5\xae\x9e\xe9\xaa\x8c" " (Colbert 1997)\n";
    std::cout << "========================================\n\n";
    Vector2d worm_start = {cx, cy};
    double ring_radius = 5.0;  // 5mm ring — worm encounters boundary quickly
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9" ": (" << food_pos.x << ", " << food_pos.y << ") mm (" "\xe7\x8e\xaf\xe5\xa4\x96" ")\n";
    std::cout << "  " "\xe6\xb8\x97\xe9\x80\x8f\xe5\x8e\x8b\xe7\x8e\xaf" ": " "\xe4\xb8\xad\xe5\xbf\x83" "=(" << worm_start.x << "," << worm_start.y << ") r=" << ring_radius << "mm\n";
    std::cout << "  " "\xe6\x9c\xba\xe5\x88\xb6" ": ASH/OSM-9 " "\xe6\xa3\x80\xe6\xb5\x8b" " \xe2\x86\x92 " "\xe5\x8f\x8d\xe8\xbd\xac" " (" "\xe8\x99\xab\xe8\xa2\xab\xe5\x9b\xb0\xe5\x9c\xa8\xe7\x8e\xaf\xe5\x86\x85" ")\n\n";

    auto food_osmo = run_scenario(
        "food+osmo", duration_s, seed,
        food_pos, 1.0,
        false, {0,0}, 0,
        false, 0, {0,0}, 0,
        verbose,
        true, worm_start, ring_radius, 0.9);  // osmotic ring around worm start

    std::cout << "  CI:            " << std::setprecision(3) << food_osmo.ci << "\n";
    std::cout << "  " "\xe5\x9d\x87\xe8\xb7\x9d" ":          " << std::setprecision(1) << food_osmo.mean_distance << " mm\n";
    std::cout << "  " "\xe8\xb7\xaf\xe5\xbe\x84" ":          " << std::setprecision(1) << food_osmo.total_path << " mm\n";
    std::cout << "  " "\xe5\x8f\x8d\xe8\xbd\xac" ":          " << food_osmo.reversals << "\n";

    // Check: with ring, worm should stay closer to start → higher mean_distance to food
    if (food_osmo.mean_distance > baseline.mean_distance + 0.5) {
        std::cout << "  " "\xe2\x9c\x93" " " "\xe6\xb8\x97\xe9\x80\x8f\xe5\x8e\x8b\xe5\x9b\x9e\xe9\x81\xbf" ": " "\xe7\x8e\xaf\xe9\x98\xbb\xe6\x8c\xa1\xe8\xb6\x8b\xe5\x8c\x96"
                  << " (" "\xe5\x9d\x87\xe8\xb7\x9d" " +" << std::setprecision(1) << food_osmo.mean_distance - baseline.mean_distance << "mm)\n";
    } else if (food_osmo.ci < baseline.ci - 0.005) {
        std::cout << "  " "\xe2\x9c\x93" " " "\xe6\xb8\x97\xe9\x80\x8f\xe5\x8e\x8b" ": CI" "\xe4\xb8\x8b\xe9\x99\x8d" " (" << std::setprecision(3) << food_osmo.ci << " vs " << baseline.ci << ")\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " " "\xe6\xb8\x97\xe9\x80\x8f\xe5\x8e\x8b" ": " "\xe5\xbd\xb1\xe5\x93\x8d\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97" " (" "\xe8\x99\xab\xe5\x8f\xaf\xe8\x83\xbd\xe7\xa9\xbf\xe8\xb6\x8a\xe4\xba\x86\xe7\x8e\xaf" ")\n";
    }

    // ================================================================
    // Scenario F: CO₂ avoidance — CO₂ source co-located with food
    // BAG detects CO₂ via GCY-9/TAX-2 → reversal+turning
    // Food attracts (AWC/AWA) but CO₂ repels (BAG) → conflict
    // REF: Hallem & Sternberg 2008 — CO₂ gradient avoidance
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  F. CO\xe2\x82\x82" "\xe5\x9b\x9e\xe9\x81\xbf" " (" "\xe9\xa3\x9f\xe7\x89\xa9" "+CO\xe2\x82\x82)\n";
    std::cout << "========================================\n\n";
    std::cout << "  " "\xe9\xa3\x9f\xe7\x89\xa9" ": (" << food_pos.x << ", " << food_pos.y << ") mm\n";
    std::cout << "  CO\xe2\x82\x82: " "\xe5\x90\x8c\xe4\xbd\x8d\xe7\xbd\xae" " (" "\xe5\xbc\xba\xe5\xba\xa6" " 0.8)\n";
    std::cout << "  " "\xe5\x86\xb2\xe7\xaa\x81" ": BAG CO\xe2\x82\x82" "\xe5\x9b\x9e\xe9\x81\xbf" " vs AWC " "\xe8\xb6\x8b\xe5\x8c\x96" "\n\n";

    auto food_co2 = run_scenario(
        "food+CO2", duration_s, seed,
        food_pos, 1.0,
        false, {0,0}, 0,
        false, 0, {0,0}, 0,
        verbose,
        false, {0,0}, 0, 0,
        true, food_pos, 0.8);  // CO₂ source at food position

    std::cout << "  CI:            " << std::setprecision(3) << food_co2.ci << "\n";
    std::cout << "  " "\xe5\x9d\x87\xe8\xb7\x9d" ":          " << std::setprecision(1) << food_co2.mean_distance << " mm\n";
    std::cout << "  " "\xe8\xb7\xaf\xe5\xbe\x84" ":          " << std::setprecision(1) << food_co2.total_path << " mm\n";
    std::cout << "  " "\xe5\x8f\x8d\xe8\xbd\xac" ":          " << food_co2.reversals << "\n";

    if (food_co2.ci < baseline.ci - 0.005) {
        std::cout << "  " "\xe2\x9c\x93" " CO\xe2\x82\x82" "\xe5\x9b\x9e\xe9\x81\xbf" ": CI" "\xe4\xb8\x8b\xe9\x99\x8d" " (" << std::setprecision(3) << food_co2.ci << " vs " << baseline.ci << ")\n";
    } else if (food_co2.mean_distance > baseline.mean_distance + 0.5) {
        std::cout << "  " "\xe2\x9c\x93" " CO\xe2\x82\x82: " "\xe5\x9d\x87\xe8\xb7\x9d\xe5\xa2\x9e\xe5\x8a\xa0" " (+" << std::setprecision(1) << food_co2.mean_distance - baseline.mean_distance << "mm)\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " CO\xe2\x82\x82: " "\xe5\xbd\xb1\xe5\x93\x8d\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97" "\n";
    }

    pr(baseline);
    pr(food_repel);
    pr(food_temp);
    pr(food_o2);
    pr(food_osmo);
    pr(food_co2);

    // Analysis — use mean distance and activity as robust metrics
    std::cout << "\n  " "\xe5\x88\x86\xe6\x9e\x90" ":\n";

    // 1. Baseline navigation
    std::cout << "  " "\xe5\x9f\xba\xe7\xba\xbf" ": CI=" << std::setprecision(3) << baseline.ci
              << ", " "\xe5\x9d\x87\xe8\xb7\x9d" "=" << std::setprecision(1) << baseline.mean_distance
              << "mm, " "\xe8\xb7\xaf\xe5\xbe\x84" "=" << baseline.total_path << "mm\n";
    if (baseline.ci > 0.05) {
        std::cout << "  " "\xe2\x9c\x93" " " "\xe5\x9f\xba\xe7\xba\xbf\xe8\xb6\x8b\xe5\x8c\x96" "\xe6\xad\xa3\xe5\xb8\xb8" "\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " CI" "\xe5\xbc\xb1" " (" "\xe5\x8d\x95\xe7\xa7\x8d\xe5\xad\x90\xe6\xad\xa3\xe5\xb8\xb8" ", " "\xe9\x9c\x80\xe5\xa4\x9a\xe7\xa7\x8d\xe5\xad\x90\xe5\xb9\xb3\xe5\x9d\x87" ")\n";
    }

    // 2. Repellent conflict — compare activity (path length) and distance
    double path_ratio = (baseline.total_path > 0.01) ? food_repel.total_path / baseline.total_path : 1.0;
    double dist_ratio = (baseline.mean_distance > 0.01) ? food_repel.mean_distance / baseline.mean_distance : 1.0;
    if (path_ratio > 1.15 || dist_ratio > 1.1) {
        std::cout << "  " "\xe2\x9c\x93" " " "\xe6\x96\xa5\xe5\x8a\x9b\xe5\x86\xb2\xe7\xaa\x81" ": ASH " "\xe5\xa2\x9e\xe5\x8a\xa0\xe6\xb4\xbb\xe5\x8a\xa8\xe9\x87\x8f"
                  << " (" "\xe8\xb7\xaf\xe5\xbe\x84" " " << std::setprecision(0) << (path_ratio - 1) * 100 << "%"
                  << ", " "\xe5\x9d\x87\xe8\xb7\x9d" " " << std::setprecision(0) << (dist_ratio - 1) * 100 << "%)\n";
    } else if (food_repel.mean_distance > baseline.mean_distance + 0.5) {
        std::cout << "  " "\xe2\x96\xb3" " " "\xe6\x96\xa5\xe5\x8a\x9b\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe5\xb0\x8f\xe5\xb9\x85\xe5\x9d\x87\xe8\xb7\x9d\xe5\xa2\x9e\xe5\x8a\xa0" "\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " " "\xe6\x96\xa5\xe5\x8a\x9b\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe5\xbd\xb1\xe5\x93\x8d\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97" "\n";
    }

    // 3. Temperature conflict — compare mean distance
    double temp_dist_diff = food_temp.mean_distance - baseline.mean_distance;
    if (temp_dist_diff > 1.0) {
        std::cout << "  " "\xe2\x9c\x93" " " "\xe6\xb8\xa9\xe5\xba\xa6\xe5\x86\xb2\xe7\xaa\x81" ": AFD " "\xe8\xb6\x8b\xe6\xb8\xa9\xe6\x8e\xa8\xe7\xa6\xbb\xe9\xa3\x9f\xe7\x89\xa9"
                  << " (+" << std::setprecision(1) << temp_dist_diff << "mm)\n";
    } else if (temp_dist_diff < -1.0) {
        std::cout << "  " "\xe2\x96\xb3" " " "\xe6\xb8\xa9\xe5\xba\xa6\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe8\xb6\x8b\xe5\x8c\x96\xe5\x8d\xa0\xe4\xbc\x98" "\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " " "\xe6\xb8\xa9\xe5\xba\xa6\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe5\xbd\xb1\xe5\x93\x8d\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97" "\n";
    }

    // 4. O2 conflict — note: food at different position (edge), so compare CI or normalized distance
    double o2_ci_diff = baseline.ci - food_o2.ci;
    if (food_o2.mean_distance > food_o2.start_distance * 1.1) {
        std::cout << "  " "\xe2\x9c\x93" " O" "\xe2\x82\x82" "\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe8\x99\xab\xe8\xbf\x9c\xe7\xa6\xbb\xe8\xbe\xb9\xe7\xbc\x98\xe9\xa3\x9f\xe7\x89\xa9"
                  << " (" "\xe5\x9d\x87\xe8\xb7\x9d" "=" << std::setprecision(1) << food_o2.mean_distance
                  << " > " "\xe5\x88\x9d\xe8\xb7\x9d" "=" << food_o2.start_distance << "mm)\n";
    } else if (o2_ci_diff > 0.05) {
        std::cout << "  " "\xe2\x9c\x93" " O" "\xe2\x82\x82" "\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe8\xb6\x8b\xe6\xb0\xa7\xe9\x99\x8d\xe4\xbd\x8e\xe8\xb6\x8b\xe5\x8c\x96" "\n";
    } else {
        std::cout << "  " "\xe2\x96\xb3" " O" "\xe2\x82\x82" "\xe5\x86\xb2\xe7\xaa\x81" ": " "\xe5\xbd\xb1\xe5\x93\x8d\xe4\xb8\x8d\xe6\x98\xbe\xe8\x91\x97" " (N2 NPR-1 " "\xe6\x8a\x91\xe5\x88\xb6" " URX)\n";
    }

    std::cout << "\n";
    return 0;
}
