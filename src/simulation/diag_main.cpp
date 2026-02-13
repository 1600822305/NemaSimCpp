#include "simulation/simulation_engine.h"
#include "compute/compute_backend.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <numeric>
#include <thread>
#include <future>
#include <mutex>
#include <algorithm>
#include <chrono>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace celegans;

// Step 42D: Simulation metrics for fitness evaluation
struct SimMetrics {
    double ci;              // chemotaxis index (positive = approaching food)
    double speed;           // mean speed mm/s
    double omega_ratio;     // omega/reversal ratio
    double dv_ratio;        // dorsal/ventral curvature symmetry (1.0 = perfect)
    double near_food_pct;   // % time within 5mm of food
    int reversals;
    int omegas;
};

// Run a single simulation and return key metrics
SimMetrics run_eval(unsigned int seed, double duration_s, bool no_toxin, bool no_food,
                    const SimulationEngine::TuningParams& params,
                    const std::vector<std::string>& ablations = {}) {
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);
    sim.params = params;

    // Step 67: Laser ablation
    for (const auto& name : ablations) {
        sim.ablate_neuron(name);
    }

    // Setup environment (same as diag)
    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    if (!no_toxin && !no_food) {
        sim.environment().repellent_field().add_point_source(food, 0.8, 25.0);
    }
    if (no_food) {
        sim.environment().chemical_field().clear();
        sim.environment().soluble_field().clear();
    }
    sim.reset_transducers();

    double duration_ms = duration_s * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(100.0 / sim.dt());

    int reversal_count = 0, omega_count = 0;
    bool prev_rev = false, prev_omega = false;
    int near_food_samples = 0, total_samples = 0;
    double first_dist = -1;
    std::vector<double> speeds, curvatures;

    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        if ((s + 1) % sample_interval == 0) {
            auto head = sim.body().get_head_position();
            double dx = head.x - food.x, dy = head.y - food.y;
            double dist = std::sqrt(dx*dx + dy*dy);
            if (first_dist < 0) first_dist = dist;

            total_samples++;
            if (dist < 5.0) near_food_samples++;

            bool cr = sim.is_reversing();
            bool co = sim.is_omega_turning();
            if (cr && !prev_rev) reversal_count++;
            if (co && !prev_omega) omega_count++;
            prev_rev = cr; prev_omega = co;

            speeds.push_back(sim.body().get_speed());
            curvatures.push_back(sim.body().segments()[0].curvature);
        }
    }

    auto head = sim.body().get_head_position();
    double dx = head.x - food.x, dy = head.y - food.y;
    double final_dist = std::sqrt(dx*dx + dy*dy);

    SimMetrics m;
    m.ci = (first_dist > 0) ? (first_dist - final_dist) / first_dist : 0;
    m.speed = speeds.empty() ? 0 :
        std::accumulate(speeds.begin(), speeds.end(), 0.0) / speeds.size();
    m.reversals = reversal_count;
    m.omegas = omega_count;
    m.omega_ratio = (reversal_count > 0) ? (double)omega_count / reversal_count : 0;
    m.near_food_pct = (total_samples > 0) ? 100.0 * near_food_samples / total_samples : 0;

    // D/V ratio from curvatures
    double c_max = 0, c_min = 0;
    for (double c : curvatures) { if (c > c_max) c_max = c; if (c < c_min) c_min = c; }
    m.dv_ratio = (std::abs(c_min) > 0.001) ? std::abs(c_max) / std::abs(c_min) : 99.9;

    return m;
}

// Compute fitness from multi-seed multi-scenario results
double compute_fitness(const std::vector<SimMetrics>& notox,
                       const std::vector<SimMetrics>& toxic,
                       const std::vector<SimMetrics>& nofood) {
    auto avg = [](const std::vector<double>& v) {
        return v.empty() ? 0.0 : std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    std::vector<double> ci_nt, ci_tx, or_nt, or_nf, sp_nt, dv_nt, nf_nt;
    for (auto& m : notox) {
        ci_nt.push_back(m.ci); or_nt.push_back(m.omega_ratio);
        sp_nt.push_back(m.speed); dv_nt.push_back(m.dv_ratio);
        nf_nt.push_back(m.near_food_pct);
    }
    for (auto& m : toxic) ci_tx.push_back(m.ci);
    for (auto& m : nofood) or_nf.push_back(m.omega_ratio);

    double f = 0;
    f += 10.0 * avg(ci_nt);                              // positive chemotaxis (most important)
    f -= 5.0  * std::max(0.0, avg(ci_tx));               // toxic CI should be negative
    f -= 3.0  * std::abs(avg(or_nt) - 0.65);             // omega/rev target 0.65
    f -= 3.0  * std::abs(avg(dv_nt) - 1.0);              // D/V symmetry
    f -= 2.0  * std::abs(avg(sp_nt) - 0.18);             // biological speed ~0.18 mm/s
    f += 2.0  * avg(nf_nt) / 100.0;                      // near food dwell time
    return f;
}

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::WARN);

    // CLI parameter overrides (compile once, sweep params without recompile)
    double cli_duration = 300000.0;  // default 300s
    float cli_as_factor = -1.0f;
    float cli_pulse_amp = -1.0f;
    float cli_omega_threshold = -1.0f;
    float cli_riv_tonic = -1.0f;
    bool cli_quiet = false;
    bool cli_no_toxin = false;
    double cli_npr1 = -999.0;  // Step 96: NPR-1 override (-15=N2 default, 0=Hawaiian/social)
    bool cli_no_food = false;
    bool cli_light = false;
    double cli_light_x = 25.0, cli_light_y = 25.0, cli_light_intensity = 1.0;
    bool cli_osm = false;  // Step 118: osmotic barrier
    bool cli_fitness = false;
    int cli_nseeds = 1;
    int cli_jobs = std::min(8, (int)std::thread::hardware_concurrency());
    unsigned int cli_seed = 123;
    double cli_sleep_after_learn = 0.0;  // Step 62: forced sleep after learning (seconds)
    bool cli_pheromone = false;              // Step 64: enable pheromone source
    double cli_pheromone_x = 15.0, cli_pheromone_y = 25.0, cli_pheromone_intensity = 0.8;
    double cli_dishabit_at = -1.0;              // Step 79: dishabituation stimulus time (seconds)
    double cli_food_removal = -1.0;              // Step 98: food removal time (seconds) for ARS test
    std::vector<std::string> cli_ablations;   // Step 67: neurons to ablate
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) cli_duration = std::atof(argv[++i]) * 1000.0;
        else if (arg == "--as_factor" && i+1 < argc) cli_as_factor = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--pulse_amp" && i+1 < argc) cli_pulse_amp = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--omega_threshold" && i+1 < argc) cli_omega_threshold = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--riv_tonic" && i+1 < argc) cli_riv_tonic = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--seed" && i+1 < argc) cli_seed = static_cast<unsigned int>(std::atoi(argv[++i]));
        else if (arg == "--no-toxin" || arg == "--no_toxin") cli_no_toxin = true;
        else if (arg == "--npr1" && i+1 < argc) cli_npr1 = std::atof(argv[++i]);
        else if (arg == "--no-food" || arg == "--no_food") cli_no_food = true;
        else if (arg == "--light") cli_light = true;
        else if (arg == "--osm") cli_osm = true;
        else if (arg == "--light_x" && i+1 < argc) { cli_light = true; cli_light_x = std::atof(argv[++i]); }
        else if (arg == "--light_y" && i+1 < argc) { cli_light = true; cli_light_y = std::atof(argv[++i]); }
        else if (arg == "--light_intensity" && i+1 < argc) { cli_light = true; cli_light_intensity = std::atof(argv[++i]); }
        else if (arg == "--quiet" || arg == "-q") cli_quiet = true;
        else if (arg == "--fitness") cli_fitness = true;
        else if (arg == "--seeds" && i+1 < argc) cli_nseeds = std::atoi(argv[++i]);
        else if (arg == "--jobs" && i+1 < argc) cli_jobs = std::atoi(argv[++i]);
        else if (arg == "-j" && i+1 < argc) cli_jobs = std::atoi(argv[++i]);
        else if (arg == "--sleep-after-learning" && i+1 < argc) cli_sleep_after_learn = std::atof(argv[++i]);
        else if (arg == "--pheromone") cli_pheromone = true;
        else if (arg == "--food-removal" && i+1 < argc) cli_food_removal = std::atof(argv[++i]);
        else if (arg == "--pheromone_x" && i+1 < argc) { cli_pheromone = true; cli_pheromone_x = std::atof(argv[++i]); }
        else if (arg == "--pheromone_y" && i+1 < argc) { cli_pheromone = true; cli_pheromone_y = std::atof(argv[++i]); }
        else if (arg == "--pheromone_intensity" && i+1 < argc) { cli_pheromone = true; cli_pheromone_intensity = std::atof(argv[++i]); }
        else if (arg == "--ablate" && i+1 < argc) { cli_ablations.push_back(argv[++i]); }
        else if (arg == "--dishabit-at" && i+1 < argc) { cli_dishabit_at = std::atof(argv[++i]); }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: celegans_diag [options]\n"
                      << "  --duration <sec>      Simulation duration (default: 300)\n"
                      << "  --as_factor <f>       AS dorsal resistance factor\n"
                      << "  --pulse_amp <f>       RIV pulse amplitude scaling\n"
                      << "  --omega_threshold <f> RIV omega threshold (default: 0.5)\n"
                      << "  --riv_tonic <f>       RIV tonic drive pA (default: 1.0)\n"
                      << "  --seed <n>            RNG seed (default: 123)\n"
                      << "  --no-toxin            Non-toxic food (disable repellent)\n"
                      << "  --no-food             No food (empty arena, random walk)\n"
                      << "  --osm                 Enable osmotic barrier ring (glycerol ring assay)\n"
                      << "  --light               Enable light source at (25,25)\n"
                      << "  --light_x/y <f>       Light source position\n"
                      << "  --light_intensity <f>  Light intensity 0-1 (default: 1.0)\n"
                      << "  --quiet / -q          Only show key metrics\n"
                      << "  --fitness             Multi-seed fitness evaluation mode\n"
                      << "  --seeds <n>           Number of seeds (default: 4, also works without --fitness)\n"
                      << "  --jobs <n> / -j <n>   Parallel threads (default: " << std::thread::hardware_concurrency() << ")\n"
                      << "  --sleep-after-learning <sec>  Force sleep after toxin exposure (Step 62)\n"
                      << "  --pheromone           Enable pheromone source at (15,25) (Step 64)\n"
                      << "  --pheromone_x/y <f>   Pheromone source position\n"
                      << "  --pheromone_intensity <f>  Pheromone intensity 0-1 (default: 0.8)\n"
                      << "  --ablate <name>       Ablate neuron (e.g. AVA, ASE, AIB; repeatable)\n";
            return 0;
        }
    }

    // === FITNESS EVALUATION MODE ===
    if (cli_fitness) {
        SimulationEngine::TuningParams p;
        if (cli_as_factor > 0) p.as_factor = cli_as_factor;
        if (cli_pulse_amp > 0) p.pulse_amp = cli_pulse_amp;
        if (cli_omega_threshold > 0) p.omega_threshold = cli_omega_threshold;
        if (cli_riv_tonic > 0) p.riv_tonic = cli_riv_tonic;
        double dur_s = cli_duration / 1000.0;

        std::vector<SimMetrics> res_notox(cli_nseeds), res_toxic(cli_nseeds), res_nofood(cli_nseeds);
        unsigned int base_seed = cli_seed;
        int jobs = std::max(1, std::min(cli_jobs, cli_nseeds * 3));

        std::cerr << "FITNESS: " << cli_nseeds << " seeds x 3 scenarios, "
                  << dur_s << "s each, " << jobs << " parallel jobs (pa=" << p.pulse_amp
                  << " af=" << p.as_factor << ")" << std::endl;

        // Step 99: parallel multi-seed execution
        std::mutex mtx;
        int done_count = 0;
        auto run_seed = [&](int i) {
            unsigned int s = base_seed + i;
            auto r1 = run_eval(s, dur_s, true,  false, p);
            auto r2 = run_eval(s, dur_s, false, false, p);
            auto r3 = run_eval(s, dur_s, true,  true,  p);
            std::lock_guard<std::mutex> lk(mtx);
            res_notox[i] = r1; res_toxic[i] = r2; res_nofood[i] = r3;
            std::cerr << "  seed " << s << " done (" << ++done_count << "/" << cli_nseeds << ")" << std::endl;
        };

        // Throttled parallel: launch in batches of 'jobs'
        for (int batch_start = 0; batch_start < cli_nseeds; batch_start += jobs) {
            int batch_end = std::min(batch_start + jobs, cli_nseeds);
            std::vector<std::future<void>> futures;
            for (int i = batch_start; i < batch_end; ++i) {
                futures.push_back(std::async(std::launch::async, run_seed, i));
            }
            for (auto& f : futures) f.get();
        }

        // Compute averages
        auto avg = [](const std::vector<SimMetrics>& v, auto fn) {
            double sum = 0; for (auto& m : v) sum += fn(m); return sum / v.size();
        };
        double ci_nt  = avg(res_notox,  [](auto& m){ return m.ci; });
        double ci_tx  = avg(res_toxic,  [](auto& m){ return m.ci; });
        double or_nt  = avg(res_notox,  [](auto& m){ return m.omega_ratio; });
        double or_nf  = avg(res_nofood, [](auto& m){ return m.omega_ratio; });
        double sp_nt  = avg(res_notox,  [](auto& m){ return m.speed; });
        double dv_nt  = avg(res_notox,  [](auto& m){ return m.dv_ratio; });
        double nf_nt  = avg(res_notox,  [](auto& m){ return m.near_food_pct; });

        double fitness = compute_fitness(res_notox, res_toxic, res_nofood);

        // Output: machine-readable on stdout, human-readable breakdown on stderr
        std::cerr << std::fixed << std::setprecision(3)
                  << "\n  CI(notox)=" << ci_nt << "  CI(toxic)=" << ci_tx
                  << "\n  omega/rev(notox)=" << or_nt << "  omega/rev(nofood)=" << or_nf
                  << "\n  speed=" << sp_nt << "  D/V=" << dv_nt
                  << "  near_food=" << nf_nt << "%"
                  << "\n  --- breakdown ---"
                  << "\n  +10*CI_notox     = " << 10.0*ci_nt
                  << "\n  -5*max(0,CI_tox) = " << -5.0*std::max(0.0, ci_tx)
                  << "\n  -3*|ω-0.65|      = " << -3.0*std::abs(or_nt - 0.65)
                  << "\n  -3*|DV-1.0|      = " << -3.0*std::abs(dv_nt - 1.0)
                  << "\n  -2*|spd-0.18|    = " << -2.0*std::abs(sp_nt - 0.18)
                  << "\n  +2*near_food     = " << 2.0*nf_nt/100.0
                  << std::endl;

        // Machine-readable: single line on stdout for script parsing
        std::cout << std::fixed << std::setprecision(4)
                  << "FITNESS=" << fitness
                  << " CI_NT=" << ci_nt << " CI_TX=" << ci_tx
                  << " OR_NT=" << or_nt << " OR_NF=" << or_nf
                  << " SPD=" << sp_nt << " DV=" << dv_nt
                  << " NF=" << nf_nt << std::endl;
        return 0;
    }

    // === MULTI-SEED AGGREGATE MODE (Step 99) ===
    // --seeds N without --fitness: run N seeds in parallel, report aggregate stats
    if (cli_nseeds > 1 && !cli_fitness) {
        SimulationEngine::TuningParams p;
        if (cli_as_factor > 0) p.as_factor = cli_as_factor;
        if (cli_pulse_amp > 0) p.pulse_amp = cli_pulse_amp;
        if (cli_omega_threshold > 0) p.omega_threshold = cli_omega_threshold;
        if (cli_riv_tonic > 0) p.riv_tonic = cli_riv_tonic;
        double dur_s = cli_duration / 1000.0;
        int jobs = std::max(1, std::min(cli_jobs, cli_nseeds));
        unsigned int base_seed = cli_seed;

        std::cerr << "MULTI-SEED: " << cli_nseeds << " seeds, "
                  << dur_s << "s each, " << jobs << " parallel jobs"
                  << (cli_no_toxin ? " (no-toxin)" : "")
                  << (cli_no_food ? " (no-food)" : "") << std::endl;

        std::vector<SimMetrics> results(cli_nseeds);
        std::mutex mtx;
        int done_count = 0;

        auto run_one = [&](int i) {
            unsigned int s = base_seed + i;
            auto r = run_eval(s, dur_s, cli_no_toxin, cli_no_food, p, cli_ablations);
            std::lock_guard<std::mutex> lk(mtx);
            results[i] = r;
            std::cerr << "  seed " << s << " done (" << ++done_count << "/" << cli_nseeds << ")" << std::endl;
        };

        auto t_start = std::chrono::high_resolution_clock::now();
        for (int batch_start = 0; batch_start < cli_nseeds; batch_start += jobs) {
            int batch_end = std::min(batch_start + jobs, cli_nseeds);
            std::vector<std::future<void>> futures;
            for (int i = batch_start; i < batch_end; ++i) {
                futures.push_back(std::async(std::launch::async, run_one, i));
            }
            for (auto& f : futures) f.get();
        }
        auto t_end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t_end - t_start).count();
        std::cerr.flush();

        // Compute mean and stddev
        auto stat = [&](auto fn) -> std::pair<double,double> {
            double sum = 0, sum2 = 0;
            for (auto& m : results) { double v = fn(m); sum += v; sum2 += v*v; }
            double mean = sum / results.size();
            double var = sum2 / results.size() - mean * mean;
            return {mean, std::sqrt(std::max(0.0, var))};
        };

        auto [ci_m, ci_s]   = stat([](auto& m){ return m.ci; });
        auto [sp_m, sp_s]   = stat([](auto& m){ return m.speed; });
        auto [or_m, or_s]   = stat([](auto& m){ return m.omega_ratio; });
        auto [nf_m, nf_s]   = stat([](auto& m){ return m.near_food_pct; });
        auto [rv_m, rv_s]   = stat([](auto& m){ return (double)m.reversals; });
        auto [om_m, om_s]   = stat([](auto& m){ return (double)m.omegas; });
        auto [dv_m, dv_s]   = stat([](auto& m){ return m.dv_ratio; });

        std::cout << "\n========================================" << std::endl;
        std::cout << "  MULTI-SEED RESULTS (" << cli_nseeds << " seeds, "
                  << std::fixed << std::setprecision(1) << elapsed << "s wall time)" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << std::fixed << std::setprecision(3);
        std::cout << "  CI:           " << ci_m << " ± " << ci_s << std::endl;
        std::cout << "  Speed:        " << sp_m << " ± " << sp_s << " mm/s" << std::endl;
        std::cout << "  Reversals:    " << std::setprecision(1) << rv_m << " ± " << rv_s << std::endl;
        std::cout << "  Omegas:       " << om_m << " ± " << om_s << std::endl;
        std::cout << "  Omega/Rev:    " << std::setprecision(3) << or_m << " ± " << or_s << std::endl;
        std::cout << "  Near food:    " << std::setprecision(1) << nf_m << " ± " << nf_s << "%" << std::endl;
        std::cout << "  D/V ratio:    " << std::setprecision(2) << dv_m << " ± " << dv_s << std::endl;

        // Per-seed detail
        std::cout << "\n  Per-seed:" << std::endl;
        std::cout << "  seed    CI     speed  rev  omega  near%" << std::endl;
        for (int i = 0; i < cli_nseeds; ++i) {
            auto& m = results[i];
            std::cout << "  " << std::setw(4) << (base_seed + i)
                      << "  " << std::setprecision(3) << std::setw(6) << m.ci
                      << "  " << std::setprecision(3) << std::setw(5) << m.speed
                      << "  " << std::setw(3) << m.reversals
                      << "  " << std::setw(5) << m.omegas
                      << "  " << std::setprecision(1) << std::setw(5) << m.near_food_pct << std::endl;
        }
        return 0;
    }

    // GPU device detection
    {
        auto devices = ComputeBackend::enumerate_devices();
        std::cout << "========================================" << std::endl;
        std::cout << "  COMPUTE DEVICES" << std::endl;
        std::cout << "========================================\n" << std::endl;
        if (devices.empty()) {
            std::cout << "  No OpenCL devices found" << std::endl;
        }
        for (size_t i = 0; i < devices.size(); ++i) {
            auto& d = devices[i];
            std::cout << "  [" << i << "] " << d.name
                      << (d.is_gpu ? " (GPU)" : " (CPU)")
                      << "\n      " << d.vendor << " | " << d.version
                      << "\n      " << d.global_mem_bytes / (1024*1024) << " MB | "
                      << d.max_compute_units << " CUs | "
                      << "WG " << d.max_work_group_size << std::endl;
        }
        bool has_gpu = ComputeBackend::opencl_available();
        std::cout << "\n  GPU available: " << (has_gpu ? "YES" : "NO")
                  << "\n" << std::endl;
    }

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(cli_seed);
    // Step 67: Apply laser ablations
    for (const auto& name : cli_ablations) {
        sim.ablate_neuron(name);
        std::cout << "  [ABLATION] " << name << " silenced" << std::endl;
    }
    // Apply CLI parameter overrides (only if explicitly set)
    if (cli_as_factor >= 0) sim.params.as_factor = cli_as_factor;
    if (cli_pulse_amp >= 0) sim.params.pulse_amp = cli_pulse_amp;
    if (cli_omega_threshold >= 0) sim.params.omega_threshold = cli_omega_threshold;
    if (cli_riv_tonic >= 0) sim.params.riv_tonic = cli_riv_tonic;
    if (cli_npr1 > -900) { sim.set_npr1_rmg(cli_npr1); std::cout << "  [CLI] npr1_rmg=" << cli_npr1 << " pA" << std::endl; }

    // Step 26b: TOXIC FOOD test — multi-chemical-species
    // Food emits: food_odor (volatile, σ²=144) + soluble (salt, σ²=36)
    // Toxin co-located with food → worm learns to avoid food ODOR specifically
    // REF: Zhang 2005 Nature — PA14 looks like food, worm eats it, then learns
    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);       // food odor (volatile)
    sim.environment().soluble_field().add_point_source(food, 0.4);  // salt/amino acids (same σ², weaker)
    // Toxin AT food source (same location!) — toxic food, not separate repellent
    Vector2d repellent{35.0, 25.0};
    if (!cli_no_toxin && !cli_no_food) {
        sim.environment().repellent_field().add_point_source(repellent, 0.8, 25.0);
    }
    if (cli_no_food) {
        sim.environment().chemical_field().clear();
        sim.environment().soluble_field().clear();
    }

    // Step 118: Osmotic barrier (glycerol ring assay)
    if (cli_osm) {
        // Ring at arena center, radius=15mm, width=1.5mm, strength=1.0
        sim.environment().set_osmotic_barrier({25.0, 25.0}, 15.0, 1.5, 1.0);
    }

    // Step 55: Light source (UV/blue)
    if (cli_light) {
        sim.environment().set_light_source({cli_light_x, cli_light_y}, cli_light_intensity);
    }

    // Step 64: Pheromone source (ascaroside social signal)
    if (cli_pheromone) {
        sim.environment().set_pheromone_source(
            {cli_pheromone_x, cli_pheromone_y}, cli_pheromone_intensity);
    }

    // Step 41: Reset transducers after environment changes
    // initialize_default() food is at (35,35), diag moves it to (35,25)
    // Without reset, NSM transducer fast_ starts at volatile conc (0.50)
    // instead of food_density (0.04) → spurious 5-HT release
    sim.reset_transducers();

    auto& conn = sim.connectome();

    int asel_id = conn.get_neuron_id("ASEL");
    int aser_id = conn.get_neuron_id("ASER");
    int smddl_id = conn.get_neuron_id("SMDDL");
    int smdvl_id = conn.get_neuron_id("SMDVL");
    int aval_id = conn.get_neuron_id("AVAL");
    int avbl_id = conn.get_neuron_id("AVBL");
    // Step 19b: intermediate neuron tracking for pirouette pathway
    int aiyl_id = conn.get_neuron_id("AIYL");
    int aiyr_id = conn.get_neuron_id("AIYR");
    int aibl_id = conn.get_neuron_id("AIBL");
    int aibr_id = conn.get_neuron_id("AIBR");
    int aial_id = conn.get_neuron_id("AIAL");
    int aiar_id = conn.get_neuron_id("AIAR");
    int awcl_id = conn.get_neuron_id("AWCL");
    int awcr_id = conn.get_neuron_id("AWCR");
    int rial_id = conn.get_neuron_id("RIAL");
    int riar_id = conn.get_neuron_id("RIAR");
    int ashl_id = conn.get_neuron_id("ASHL");
    int ashr_id = conn.get_neuron_id("ASHR");
    int adfl_id = conn.get_neuron_id("ADFL");  // Step 26: ADF serotonin neuron
    int ris_id = conn.get_neuron_id("RIS");      // Step 27: RIS sleep neuron
    int rivl_id = conn.get_neuron_id("RIVL");    // Step 31: RIV omega turn neurons
    int rivr_id = conn.get_neuron_id("RIVR");
    int as01_id = conn.get_neuron_id("AS01");    // Step 32: AS dorsal motor neurons
    int as02_id = conn.get_neuron_id("AS02");
    int as03_id = conn.get_neuron_id("AS03");
    int rmed_id = conn.get_neuron_id("RMED");    // Step 33: RME head inhibition
    int rmev_id = conn.get_neuron_id("RMEV");
    int olqdl_id = conn.get_neuron_id("OLQDL");  // Step 33: OLQ nose touch
    int urxl_id = conn.get_neuron_id("URXL");    // Step 34: O₂ sensing
    int aqr_id = conn.get_neuron_id("AQR");
    int pqr_id = conn.get_neuron_id("PQR");
    int aual_id = conn.get_neuron_id("AUAL");
    int bagl_id = conn.get_neuron_id("BAGL");    // Step 35: CO₂ sensing
    int dva_id = conn.get_neuron_id("DVA");       // Step 36: proprioception
    int pvdl_id = conn.get_neuron_id("PVDL");
    int nsml_id = conn.get_neuron_id("NSML");     // Step 45: NSM 5-HT source diagnostic
    int nsmr_id = conn.get_neuron_id("NSMR");
    int hsnl_id = conn.get_neuron_id("HSNL");     // Step 38: egg-laying
    int vc4_id = conn.get_neuron_id("VC4");
    int avl_id = conn.get_neuron_id("AVL");       // Step 56: defecation
    int dvb_id = conn.get_neuron_id("DVB");
    int flpl_id = conn.get_neuron_id("FLPL");     // Step 74: nose touch circuit
    int flpr_id = conn.get_neuron_id("FLPR");
    int il1dl_id = conn.get_neuron_id("IL1DL");
    int rih_id = conn.get_neuron_id("RIH");
    int rmddl_id = conn.get_neuron_id("RMDDL");
    int rmgl_id = conn.get_neuron_id("RMGL");     // Step 75: pathogen aversion hub
    // Step 102-106: new neuron IDs for diagnostics
    int siadl_id = conn.get_neuron_id("SIADL");   // Step 102: SIA head motor
    int sibdl_id = conn.get_neuron_id("SIBDL");   // Step 102: SIB head motor
    int saadl_id = conn.get_neuron_id("SAADL");   // Step 103: SAA turn circuit
    int smbdl_id = conn.get_neuron_id("SMBDL");   // SMB (pre-existing)
    int uradl_id = conn.get_neuron_id("URADL");   // Step 105: URA inner labial motor
    int pvm_id   = conn.get_neuron_id("PVM");      // Step 106: posterior touch
    int sdqr_id  = conn.get_neuron_id("SDQR");    // Step 106: body-side O2
    int ala_id   = conn.get_neuron_id("ALA");      // Step 106: stress sleep
    int urbl_id  = conn.get_neuron_id("URBL");     // Step 107: URB inner labial inter
    int urydl_id = conn.get_neuron_id("URYDL");    // Step 107: URY inner labial sensory
    int alnl_id  = conn.get_neuron_id("ALNL");     // Step 108: ALN tail-spike
    int plnl_id  = conn.get_neuron_id("PLNL");     // Step 108: PLN tail-spike
    int bdul_id  = conn.get_neuron_id("BDUL");     // Step 108: BDU body cavity
    int olll_id  = conn.get_neuron_id("OLLL");     // Step 109: OLL outer labial
    int phcl_id  = conn.get_neuron_id("PHCL");     // Step 109: PHC phasmid tail
    int avg_id   = conn.get_neuron_id("AVG");       // Step 109: AVG ventral cord pioneer
    int rmhl_id  = conn.get_neuron_id("RMHL");     // Step 110: RMH head motor
    int rmfl_id  = conn.get_neuron_id("RMFL");     // Step 110: RMF head motor
    int rid_id   = conn.get_neuron_id("RID");       // Step 110: RID dorsal motor
    int pvql_id  = conn.get_neuron_id("PVQL");     // Step 111: PVQ ventral cord
    int pvnl_id  = conn.get_neuron_id("PVNL");     // Step 111: PVN ventral cord motor
    int aiml_id  = conn.get_neuron_id("AIML");     // Step 111: AIM sexual regulation
    int vc1_id   = conn.get_neuron_id("VC1");       // Step 112: VC1 vulval motor
    int sabd_id  = conn.get_neuron_id("SABD");     // Step 112: SAB sublateral motor
    int asgl_id  = conn.get_neuron_id("ASGL");     // Step 113: ASG amphid sensory
    int adal_id  = conn.get_neuron_id("ADAL");     // Step 113: ADA amphid inter
    int rifl_id  = conn.get_neuron_id("RIFL");     // Step 113: RIF sexual nexus
    int rir_id   = conn.get_neuron_id("RIR");       // Step 113: RIR ring inter
    int il1l_id  = conn.get_neuron_id("IL1L");     // Step 114: IL1 lateral
    int sdql_id  = conn.get_neuron_id("SDQL");     // Step 114: SDQL body sensor
    int rmel_id  = conn.get_neuron_id("RMEL");     // Step 114: RME lateral
    int pda_id   = conn.get_neuron_id("PDA");       // Step 114: tail motor
    int pvwl_id  = conn.get_neuron_id("PVWL");     // Step 114: PVW
    int i2l_id   = conn.get_neuron_id("I2L");       // Step 115: pharyngeal
    int m1_id    = conn.get_neuron_id("M1");        // Step 115: pharyngeal motor
    int rigl_id  = conn.get_neuron_id("RIGL");     // Step 116: RIG bilateral
    int rmdl_id  = conn.get_neuron_id("RMDL");     // Step 116: RMD lateral

    // Accumulators
    std::vector<double> grad_mags, grad_normals, biases;
    std::vector<double> asel_vs, aser_vs, smd_diffs, curvatures;
    std::vector<double> speeds, headings, dists;
    std::vector<double> aiyl_vs, aiyr_vs, aibl_vs, aibr_vs;
    std::vector<double> aial_vs, aiar_vs, awcl_vs, awcr_vs;
    std::vector<double> aval_vs, rial_vs, riar_vs;
    std::vector<double> sht_vs, da_vs, oa_vs, satiety_vs, spd_scale_vs, fmem_vs, dist_vs_time, xpos_vs;
    std::vector<double> pump_rate_vs, pharynx_v_vs;  // Step 24: pharyngeal diagnostics
    std::vector<double> actual_speed_vs;  // Step 27b: actual body speed for sleep verification
    std::vector<double> rep_dist_vs, ypos_vs, ash_i_vs;  // Step 25: nociception tracking
    std::vector<double> sick_vs;  // Step 26: sickness tracking
    std::vector<double> fatigue_vs;  // Step 27: fatigue/sleep tracking
    std::vector<int> sleep_vs;       // Step 27: is_sleeping flag
    // SMD current diagnostics
    std::vector<double> smddl_v_vs, smdvl_v_vs, smddl_isyn_vs, smddl_iext_vs;
    // Step 31: RIV omega turn diagnostics
    std::vector<double> rivl_v_vs, rivr_v_vs;
    // Step 32: AS dorsal motor diagnostics
    std::vector<double> as01_v_vs, as_dorsal_tone_vs;
    // Step 33: RME + OLQ diagnostics
    std::vector<double> rmed_v_vs, rmev_v_vs, olqdl_v_vs;
    // Step 34: O₂ sensing diagnostics
    std::vector<double> urxl_v_vs, aqr_v_vs, aual_v_vs, o2_head_vs;
    // Step 35: CO₂ sensing diagnostics
    std::vector<double> bagl_v_vs, co2_head_vs;
    // Step 36: Proprioception diagnostics
    std::vector<double> dva_v_vs, pvdl_v_vs, mean_abs_curv_vs;
    // Step 45: NSM 5-HT source diagnostics
    std::vector<double> nsml_v_vs, nsml_s_vs;
    // Step 38: Egg-laying diagnostics
    std::vector<double> hsnl_v_vs, vc4_v_vs, egg_pressure_vs;
    double omega_total_duration = 0.0;  // sum of omega durations (ms)
    double omega_start_time = -1.0;     // track current omega start
    // Step 74: Nose touch circuit diagnostics
    std::vector<double> flpl_v_vs, rih_v_vs, il1dl_v_vs, rmddl_v_vs;
    int nose_touch_samples = 0;  // count of samples where nose touch was active
    // Step 75: Pathogen aversion diagnostics
    std::vector<double> rmgl_v_vs;
    // Step 102-106: new neuron voltage accumulators
    std::vector<double> siadl_v_vs, sibdl_v_vs, saadl_v_vs, smbdl_v_vs;
    std::vector<double> uradl_v_vs, pvm_v_vs, sdqr_v_vs, ala_v_vs;
    std::vector<double> urbl_v_vs, urydl_v_vs;
    std::vector<double> alnl_v_vs, plnl_v_vs, bdul_v_vs;
    std::vector<double> olll_v_vs, phcl_v_vs, avg_v_vs;
    std::vector<double> rmhl_v_vs, rmfl_v_vs, rid_v_vs;
    std::vector<double> pvql_v_vs, pvnl_v_vs, aiml_v_vs;
    std::vector<double> vc1_v_vs, sabd_v_vs;
    std::vector<double> asgl_v_vs, adal_v_vs, rifl_v_vs, rir_v_vs;
    std::vector<double> il1l_v_vs, sdql_v_vs, rmel_v_vs, pda_v_vs, pvwl_v_vs;
    std::vector<double> i2l_v_vs, m1_v_vs;
    std::vector<double> rigl_v_vs, rmdl_v_vs;
    // Step 78: Tap habituation tracking
    int prev_tap_count = 0;          // track tap count changes
    double tap_onset_time = -1.0;    // when current tap started
    std::vector<double> tap_pool_at_onset;   // vesicle pool at each tap onset
    std::vector<double> tap_sens_at_onset;   // Step 79: sensitization level at tap onset
    std::vector<bool> tap_got_reversal;      // did reversal occur within 2s of tap?
    double tap_reversal_window = 2000.0;     // ms, window to detect tap-induced reversal
    bool tap_reversal_detected = false;      // flag for current tap window
    // Step 29: Wave propagation diagnostics
    std::vector<double> curv_seg2_vs, curv_seg7_vs, curv_seg15_vs, muscle_work_vs;
    int curv7_sign_changes = 0;
    double prev_curv7 = 0;

    double prev_heading = sim.body().get_head_angle() * 180.0 / 3.14159265;
    double prev_time = 0;
    double heading_rate_sum = 0;
    int heading_rate_count = 0;

    // Run simulation (default 300s, override with --duration)
    double duration = cli_duration;
    int pirouette_count = 0;
    int reversal_count = 0;
    int omega_count = 0;
    bool prev_reversing = false;
    bool prev_omega = false;
    std::vector<double> reversal_times;  // Step 98: timestamps for ARS analysis
    int wall_touch_count = 0;
    int near_food_samples = 0;   // dist < 5mm
    int total_samples = 0;
    int total_steps = (int)(duration / sim.dt());
    int sample_interval = (int)(100.0 / sim.dt()); // every 100ms

    // Step 79: Wire dishabituation stimulus time
    if (cli_dishabit_at > 0) {
        sim.set_dishabit_time(cli_dishabit_at * 1000.0); // convert s → ms
    }

    // Step 62: Sleep-after-learning experiment tracking
    bool sleep_consolidation_triggered = false;
    bool food_removed = false;  // Step 98: flag to prevent double food removal

    for (int s = 0; s < total_steps; ++s) {
        sim.step();

        // Step 98: --food-removal protocol (Hills 2004, López-Cruz 2019)
        // Remove food at specified time to test ARS local→global search transition
        if (cli_food_removal > 0 && !food_removed
            && sim.current_time() >= cli_food_removal * 1000.0) {
            food_removed = true;
            sim.environment().chemical_field().clear();
            sim.environment().soluble_field().clear();
            sim.environment().repellent_field().clear();
            sim.reset_transducers();
            if (!cli_quiet) {
                std::cout << "  [Step 98] Food removed at t="
                          << std::setprecision(1) << sim.current_time() / 1000.0
                          << "s (food_memory=" << std::setprecision(3) << sim.food_memory()
                          << ")" << std::endl;
            }
        }

        // Step 62: --sleep-after-learning protocol (Chouhan 2023 Cell)
        // When sickness exceeds 0.3 (worm has learned), force sleep for N seconds
        // This models: train → sleep → test (memory consolidation experiment)
        if (cli_sleep_after_learn > 0 && !sleep_consolidation_triggered
            && sim.sickness() > 0.3) {
            sim.force_sleep(cli_sleep_after_learn * 1000.0); // convert s → ms
            sleep_consolidation_triggered = true;
            if (!cli_quiet) {
                std::cout << "  [Step 62] Forced sleep triggered at t="
                          << std::setprecision(1) << sim.current_time() / 1000.0
                          << "s (sickness=" << std::setprecision(3) << sim.sickness()
                          << "), duration=" << cli_sleep_after_learn << "s" << std::endl;
            }
        }

        if ((s + 1) % sample_interval == 0) {
            auto head = sim.body().get_head_position();
            const auto& neurons = sim.neurons();
            int n = (int)neurons.size();

            // 1. Gradient
            auto grad = sim.environment().chemical_field().gradient(head);
            double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

            // 2. Gradient normal
            double heading_rad = sim.body().get_head_angle();
            double grad_normal = -std::sin(heading_rad) * grad.x + std::cos(heading_rad) * grad.y;

            // 3. Bias
            double bias = sim.params.weathervane_gain * grad_normal;
            double clamp = sim.params.bias_clamp;
            if (bias > clamp) bias = clamp;
            if (bias < -clamp) bias = -clamp;

            // 4. Neuron potentials
            double v_asel = (asel_id >= 0 && asel_id < n) ? neurons[asel_id]->get_membrane_potential() : 0;
            double v_aser = (aser_id >= 0 && aser_id < n) ? neurons[aser_id]->get_membrane_potential() : 0;
            double v_smddl = (smddl_id >= 0 && smddl_id < n) ? neurons[smddl_id]->get_membrane_potential() : 0;
            double v_smdvl = (smdvl_id >= 0 && smdvl_id < n) ? neurons[smdvl_id]->get_membrane_potential() : 0;

            // SMD current diagnostics
            if (smddl_id >= 0 && smddl_id < n) {
                smddl_v_vs.push_back(v_smddl);
                smdvl_v_vs.push_back(v_smdvl);
                smddl_isyn_vs.push_back(neurons[smddl_id]->get_I_syn());
                smddl_iext_vs.push_back(neurons[smddl_id]->get_I_ext());
            }

            // Intermediate neuron potentials
            auto getV = [&](int id) { return (id >= 0 && id < n) ? neurons[id]->get_membrane_potential() : -65.0; };
            aiyl_vs.push_back(getV(aiyl_id)); aiyr_vs.push_back(getV(aiyr_id));
            aibl_vs.push_back(getV(aibl_id)); aibr_vs.push_back(getV(aibr_id));
            aial_vs.push_back(getV(aial_id)); aiar_vs.push_back(getV(aiar_id));
            awcl_vs.push_back(getV(awcl_id)); awcr_vs.push_back(getV(awcr_id));
            aval_vs.push_back(getV(aval_id));
            rial_vs.push_back(getV(rial_id)); riar_vs.push_back(getV(riar_id));

            // 5. Curvature, speed
            double curv = sim.body().segments()[0].curvature;
            double speed = sim.body().get_speed();

            // Step 29: Wave propagation curvatures
            double c2 = sim.body().segments()[2].curvature;
            double c7 = sim.body().segments()[7].curvature;
            double c15 = sim.body().segments()[15].curvature;
            curv_seg2_vs.push_back(c2);
            curv_seg7_vs.push_back(c7);
            curv_seg15_vs.push_back(c15);
            if (!curv_seg7_vs.empty() && curv_seg7_vs.size() > 1 && c7 * prev_curv7 < 0)
                curv7_sign_changes++;
            prev_curv7 = c7;
            {
                double mw = 0;
                const auto& segs = sim.body().segments();
                for (int si = 0; si < 48; ++si)
                    mw += std::abs(segs[si].dorsal_activation - segs[si].ventral_activation);
                muscle_work_vs.push_back(mw / 48.0);
            }

            // 6. Heading rate
            double heading_deg = heading_rad * 180.0 / 3.14159265;
            double dt_sec = (sim.current_time() - prev_time) / 1000.0;
            double h_rate = 0;
            if (dt_sec > 0.01) {
                h_rate = (heading_deg - prev_heading) / dt_sec;
                heading_rate_sum += std::abs(h_rate);
                heading_rate_count++;
                // Detect pirouette: heading jump > 30° in 100ms
                if (std::abs(heading_deg - prev_heading) > 30.0) {
                    pirouette_count++;
                }
            }
            prev_heading = heading_deg;
            prev_time = sim.current_time();

            // 7. Distance
            double dx = head.x - food.x;
            double dy = head.y - food.y;
            double dist = std::sqrt(dx * dx + dy * dy);

            // Track near-food time
            total_samples++;
            if (dist < 5.0) near_food_samples++;

            // Track touch/reversal/omega events
            bool cur_rev = sim.is_reversing();
            bool cur_omega = sim.is_omega_turning();
            if (cur_rev && !prev_reversing) { reversal_count++; reversal_times.push_back(sim.current_time()); }
            if (cur_omega && !prev_omega) {
                omega_count++;
                omega_start_time = sim.current_time();
            }
            if (!cur_omega && prev_omega && omega_start_time > 0) {
                omega_total_duration += sim.current_time() - omega_start_time;
                omega_start_time = -1.0;
            }
            // Wall proximity check
            if (head.x < 2.0 || head.x > 48.0 || head.y < 2.0 || head.y > 48.0)
                wall_touch_count++;

            // Step 78: Per-tap habituation tracking
            int cur_tap = sim.tap_count();
            if (cur_tap > prev_tap_count) {
                // New tap just started — record vesicle pool of first ALM→AVD synapse
                double pool = 1.0;
                double sens_at_tap = sim.sensitization();
                const auto& syns = sim.connectome().synapses();
                const auto& ni = sim.connectome().neuron_infos();
                int nn = (int)sim.neurons().size();
                for (size_t si = 0; si < syns.size(); ++si) {
                    int pre = syns[si].pre_id(), post = syns[si].post_id();
                    if (pre < 0 || pre >= nn || post < 0 || post >= nn) continue;
                    if (ni[pre].name == "ALML" && ni[post].name == "AVBL") {
                        pool = syns[si].vesicle_pool();
                        break;
                    }
                }
                // Close previous tap window
                if (tap_onset_time > 0 && !tap_got_reversal.empty()) {
                    // Already tracked
                }
                tap_pool_at_onset.push_back(pool);
                tap_sens_at_onset.push_back(sens_at_tap);
                tap_got_reversal.push_back(false);
                tap_reversal_detected = false;
                tap_onset_time = sim.current_time();
                prev_tap_count = cur_tap;
            }
            // Check for reversal within window after tap
            if (tap_onset_time > 0 && !tap_reversal_detected &&
                sim.current_time() < tap_onset_time + tap_reversal_window) {
                if (cur_rev && !prev_reversing) {
                    tap_reversal_detected = true;
                    if (!tap_got_reversal.empty())
                        tap_got_reversal.back() = true;
                }
            }

            prev_reversing = cur_rev;
            prev_omega = cur_omega;

            // Step 31: RIV voltage tracking
            rivl_v_vs.push_back(getV(rivl_id));
            rivr_v_vs.push_back(getV(rivr_id));
            // Step 32: AS + dorsal tone tracking
            as01_v_vs.push_back(getV(as01_id));
            double dtone = 0.0;
            for (int si = 0; si < 6; ++si) dtone += sim.body().segments()[si].dorsal_activation;
            as_dorsal_tone_vs.push_back(dtone / 6.0);
            // Step 33: RME + OLQ tracking
            rmed_v_vs.push_back(getV(rmed_id));
            rmev_v_vs.push_back(getV(rmev_id));
            olqdl_v_vs.push_back(getV(olqdl_id));
            // Step 34: O₂ sensing tracking
            urxl_v_vs.push_back(getV(urxl_id));
            aqr_v_vs.push_back(getV(aqr_id));
            aual_v_vs.push_back(getV(aual_id));
            // Compute O₂ at head from FOOD DENSITY (bacteria, σ≈3mm)
            double food_h = sim.environment().sample_food_density(head);
            o2_head_vs.push_back(21.0 - 13.0 * std::min(food_h, 1.0));
            // Step 35: CO₂ and BAG tracking
            bagl_v_vs.push_back(getV(bagl_id));
            co2_head_vs.push_back(0.04 + 3.0 * std::min(food_h, 1.0));
            // Step 36: DVA + PVD tracking
            dva_v_vs.push_back(getV(dva_id));
            pvdl_v_vs.push_back(getV(pvdl_id));
            {
                const auto& segs = sim.body().segments();
                double sac = 0.0;
                for (size_t si = 0; si < segs.size(); ++si) sac += std::abs(segs[si].curvature);
                mean_abs_curv_vs.push_back(segs.size() > 0 ? sac / segs.size() : 0.0);
            }
            // Step 38: HSN + VC + egg_pressure tracking
            hsnl_v_vs.push_back(getV(hsnl_id));
            vc4_v_vs.push_back(getV(vc4_id));
            egg_pressure_vs.push_back(sim.egg_pressure());

            // Step 45: NSM diagnostics
            nsml_v_vs.push_back(getV(nsml_id));
            { double v = getV(nsml_id); nsml_s_vs.push_back(1.0 / (1.0 + std::exp(-(v - (-35.0)) / 5.0))); }

            // Neuromodulation time series
            sht_vs.push_back(sim.neuromodulation().get_concentration("5-HT"));
            da_vs.push_back(sim.neuromodulation().get_concentration("DA"));
            oa_vs.push_back(sim.neuromodulation().get_concentration("OA"));
            satiety_vs.push_back(sim.satiety());
            spd_scale_vs.push_back(sim.neuromodulation().get_speed_scale());
            fmem_vs.push_back(sim.food_memory());
            dist_vs_time.push_back(dist);
            xpos_vs.push_back(head.x);
            ypos_vs.push_back(head.y);
            {
                double rdx = repellent.x - head.x, rdy = repellent.y - head.y;
                rep_dist_vs.push_back(std::sqrt(rdx*rdx + rdy*rdy));
                ash_i_vs.push_back(ashl_id >= 0 && ashl_id < n ? neurons[ashl_id]->get_I_ext() : 0.0);
            }
            sick_vs.push_back(sim.sickness());  // Step 26
            fatigue_vs.push_back(sim.fatigue());  // Step 27
            sleep_vs.push_back(sim.is_sleeping() ? 1 : 0);  // Step 27
            actual_speed_vs.push_back(speed);  // Step 27b: actual speed
            pump_rate_vs.push_back(sim.pump_rate_hz());
            pharynx_v_vs.push_back(sim.pharynx_V());

            // Step 74: Nose touch circuit tracking
            flpl_v_vs.push_back(getV(flpl_id));
            rih_v_vs.push_back(getV(rih_id));
            il1dl_v_vs.push_back(getV(il1dl_id));
            rmddl_v_vs.push_back(getV(rmddl_id));
            // Detect nose touch activation: FLP I_ext > 5pA means wall proximity
            if (flpl_id >= 0 && flpl_id < n && neurons[flpl_id]->get_I_ext() > 5.0)
                nose_touch_samples++;

            // Step 75: RMG pathogen aversion hub tracking
            rmgl_v_vs.push_back(getV(rmgl_id));

            // Step 102-106: new neuron voltage sampling
            siadl_v_vs.push_back(getV(siadl_id));
            sibdl_v_vs.push_back(getV(sibdl_id));
            saadl_v_vs.push_back(getV(saadl_id));
            smbdl_v_vs.push_back(getV(smbdl_id));
            uradl_v_vs.push_back(getV(uradl_id));
            pvm_v_vs.push_back(getV(pvm_id));
            sdqr_v_vs.push_back(getV(sdqr_id));
            ala_v_vs.push_back(getV(ala_id));
            urbl_v_vs.push_back(getV(urbl_id));
            urydl_v_vs.push_back(getV(urydl_id));
            alnl_v_vs.push_back(getV(alnl_id));
            plnl_v_vs.push_back(getV(plnl_id));
            bdul_v_vs.push_back(getV(bdul_id));
            olll_v_vs.push_back(getV(olll_id));
            phcl_v_vs.push_back(getV(phcl_id));
            avg_v_vs.push_back(getV(avg_id));
            rmhl_v_vs.push_back(getV(rmhl_id));
            rmfl_v_vs.push_back(getV(rmfl_id));
            rid_v_vs.push_back(getV(rid_id));
            pvql_v_vs.push_back(getV(pvql_id));
            pvnl_v_vs.push_back(getV(pvnl_id));
            aiml_v_vs.push_back(getV(aiml_id));
            vc1_v_vs.push_back(getV(vc1_id));
            sabd_v_vs.push_back(getV(sabd_id));
            asgl_v_vs.push_back(getV(asgl_id));
            adal_v_vs.push_back(getV(adal_id));
            rifl_v_vs.push_back(getV(rifl_id));
            rir_v_vs.push_back(getV(rir_id));
            il1l_v_vs.push_back(getV(il1l_id));
            sdql_v_vs.push_back(getV(sdql_id));
            rmel_v_vs.push_back(getV(rmel_id));
            pda_v_vs.push_back(getV(pda_id));
            pvwl_v_vs.push_back(getV(pvwl_id));
            i2l_v_vs.push_back(getV(i2l_id));
            m1_v_vs.push_back(getV(m1_id));
            rigl_v_vs.push_back(getV(rigl_id));
            rmdl_v_vs.push_back(getV(rmdl_id));

            // Store
            grad_mags.push_back(grad_mag);
            grad_normals.push_back(grad_normal);
            biases.push_back(bias);
            asel_vs.push_back(v_asel);
            aser_vs.push_back(v_aser);
            smd_diffs.push_back(v_smddl - v_smdvl);
            curvatures.push_back(curv);
            speeds.push_back(speed);
            headings.push_back(heading_deg);
            dists.push_back(dist);
        }
    }

    // Helper functions
    auto mean = [](const std::vector<double>& v) {
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    auto absmax = [](const std::vector<double>& v) {
        double m = 0;
        for (auto x : v) if (std::abs(x) > m) m = std::abs(x);
        return m;
    };
    auto minmax = [](const std::vector<double>& v) -> std::pair<double,double> {
        double mn = v[0], mx = v[0];
        for (auto x : v) { if (x < mn) mn = x; if (x > mx) mx = x; }
        return {mn, mx};
    };

    std::cout << std::fixed;
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SIGNAL CHAIN DIAGNOSTIC (60s run)" << std::endl;
    std::cout << "========================================\n" << std::endl;

    auto [g_min, g_max] = minmax(grad_mags);
    std::cout << "1. GRADIENT at head:" << std::endl;
    std::cout << "   magnitude: mean=" << std::setprecision(5) << mean(grad_mags)
              << "  range=[" << g_min << ", " << g_max << "] /mm" << std::endl;

    auto [gn_min, gn_max] = minmax(grad_normals);
    std::cout << "\n2. GRADIENT NORMAL (perp to heading):" << std::endl;
    std::cout << "   mean=" << std::setprecision(5) << mean(grad_normals)
              << "  range=[" << gn_min << ", " << gn_max << "]" << std::endl;

    auto [b_min, b_max] = minmax(biases);
    std::cout << "\n3. WEATHERVANE BIAS CURRENT:" << std::endl;
    std::cout << "   mean=" << std::setprecision(3) << mean(biases)
              << " pA  range=[" << b_min << ", " << b_max << "] pA"
              << "  (gain=" << sim.params.weathervane_gain << ", clamp=" << sim.params.bias_clamp << ")" << std::endl;

    std::cout << "\n4. SENSORY NEURONS (ASEL vs ASER):" << std::endl;
    auto [al_min, al_max] = minmax(asel_vs);
    auto [ar_min, ar_max] = minmax(aser_vs);
    std::cout << "   ASEL: mean=" << std::setprecision(2) << mean(asel_vs)
              << " mV  range=[" << al_min << ", " << al_max << "]" << std::endl;
    std::cout << "   ASER: mean=" << std::setprecision(2) << mean(aser_vs)
              << " mV  range=[" << ar_min << ", " << ar_max << "]" << std::endl;
    std::cout << "   L-R diff: " << std::setprecision(3) << (mean(asel_vs) - mean(aser_vs)) << " mV" << std::endl;

    std::cout << "\n5. SMD DIFFERENTIAL (SMDDL - SMDVL):" << std::endl;
    auto [sd_min, sd_max] = minmax(smd_diffs);
    // Individual SMDDL/SMDVL ranges
    if (!smddl_v_vs.empty()) {
        auto [dl_min, dl_max] = minmax(smddl_v_vs);
        auto [vl_min, vl_max] = minmax(smdvl_v_vs);
        auto [is_min, is_max] = minmax(smddl_isyn_vs);
        auto [ie_min, ie_max] = minmax(smddl_iext_vs);
        std::cout << "   SMDDL V: [" << dl_min << ", " << dl_max << "] swing=" << (dl_max-dl_min) << " mV" << std::endl;
        std::cout << "   SMDVL V: [" << vl_min << ", " << vl_max << "] swing=" << (vl_max-vl_min) << " mV" << std::endl;
        std::cout << "   SMDDL I_syn: [" << is_min << ", " << is_max << "] pA" << std::endl;
        std::cout << "   SMDDL I_ext: [" << ie_min << ", " << ie_max << "] pA" << std::endl;
    }
    std::cout << "   mean=" << std::setprecision(3) << mean(smd_diffs)
              << " mV  range=[" << sd_min << ", " << sd_max << "]"
              << "  amplitude=" << std::setprecision(2) << (sd_max - sd_min) << " mV" << std::endl;

    std::cout << "\n6. HEAD CURVATURE:" << std::endl;
    auto [c_min, c_max] = minmax(curvatures);
    std::cout << "   mean=" << std::setprecision(4) << mean(curvatures)
              << "  range=[" << c_min << ", " << c_max << "] /mm"
              << "  amplitude=" << (c_max - c_min) << std::endl;

    std::cout << "\n7. SPEED:" << std::endl;
    auto [sp_min, sp_max] = minmax(speeds);
    std::cout << "   mean=" << std::setprecision(4) << mean(speeds)
              << "  range=[" << sp_min << ", " << sp_max << "] mm/s" << std::endl;

    std::cout << "\n8. HEADING:" << std::endl;
    auto [h_min, h_max] = minmax(headings);
    double avg_rate = (heading_rate_count > 0) ? heading_rate_sum / heading_rate_count : 0;
    std::cout << "   range=[" << std::setprecision(1) << h_min << ", " << h_max << "] deg"
              << "  total_sweep=" << (h_max - h_min) << " deg" << std::endl;
    std::cout << "   avg |dtheta/dt|=" << std::setprecision(3) << avg_rate << " deg/s" << std::endl;
    // Pirouette = reversal (± omega turn). Old heading-jump detection (>30°/100ms)
    // doesn't work with smooth curvature_bias omega turns (~9.6°/100ms).
    std::cout << "   pirouettes (=reversals): " << reversal_count << " (" 
              << std::setprecision(2) << reversal_count / (duration/1000.0) << " Hz)" << std::endl;

    std::cout << "\n10. TOUCH AVOIDANCE:" << std::endl;
    std::cout << "   reversals: " << reversal_count << std::endl;
    std::cout << "   omega turns: " << omega_count << std::endl;
    std::cout << "   wall proximity samples: " << wall_touch_count 
              << " / " << (int)(duration/100.0) << std::endl;

    // Step 31: RIV omega turn diagnostics
    std::cout << "\n20. RIV OMEGA TURN (Step 31):" << std::endl;
    auto release = [](double V) { return 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0)); };
    auto [rivl_min, rivl_max] = minmax(rivl_v_vs);
    auto [rivr_min, rivr_max] = minmax(rivr_v_vs);
    std::cout << "   RIVL: V=[" << std::setprecision(1) << rivl_min << ", " << rivl_max
              << "] mV  mean=" << std::setprecision(1) << mean(rivl_v_vs) << " mV"
              << "  S(release)=" << std::setprecision(3) << release(mean(rivl_v_vs)) << std::endl;
    std::cout << "   RIVR: V=[" << std::setprecision(1) << rivr_min << ", " << rivr_max
              << "] mV  mean=" << std::setprecision(1) << mean(rivr_v_vs) << " mV"
              << "  S(release)=" << std::setprecision(3) << release(mean(rivr_v_vs)) << std::endl;
    double omega_rev_ratio = (reversal_count > 0) ? (double)omega_count / reversal_count : 0;
    double avg_omega_dur = (omega_count > 0) ? omega_total_duration / omega_count : 0;
    std::cout << "   omega/reversal ratio: " << std::setprecision(2) << omega_rev_ratio
              << " (" << omega_count << "/" << reversal_count << ")" << std::endl;
    std::cout << "   avg omega duration: " << std::setprecision(0) << avg_omega_dur << " ms" << std::endl;
    std::cout << "   TA at trace end: " << std::setprecision(3)
              << sim.neuromodulation().get_concentration("TA") << std::endl;
    // Step 32: AS dorsal motor neuron diagnostics
    std::cout << "   AS01: V mean=" << std::setprecision(1) << mean(as01_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(as01_v_vs)) << std::endl;
    auto [dt_min, dt_max] = minmax(as_dorsal_tone_vs);
    std::cout << "   dorsal tone (seg0-5): mean=" << std::setprecision(3) << mean(as_dorsal_tone_vs)
              << "  range=[" << std::setprecision(3) << dt_min << ", " << dt_max << "]" << std::endl;

    // Step 33: RME + OLQ diagnostics
    std::cout << "\n21. RME HEAD CONTROL (Step 33):" << std::endl;
    std::cout << "   RMED: V mean=" << std::setprecision(1) << mean(rmed_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(rmed_v_vs)) << std::endl;
    std::cout << "   RMEV: V mean=" << std::setprecision(1) << mean(rmev_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(rmev_v_vs)) << std::endl;
    // Head curvature symmetry check (AS01 bias vs RME correction)
    auto [hc_min, hc_max] = minmax(curvatures);
    double d_v_ratio = (std::abs(hc_min) > 0.001) ? std::abs(hc_max) / std::abs(hc_min) : 99.9;
    std::cout << "   head curv D/V ratio: " << std::setprecision(2) << d_v_ratio
              << " (target ~1.0, was 3.6 pre-RME)" << std::endl;
    std::cout << "   OLQDL: V mean=" << std::setprecision(1) << mean(olqdl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(olqdl_v_vs)) << std::endl;

    // Step 34: O₂ sensing diagnostics
    std::cout << "\n22. O2 SENSING (Step 34):" << std::endl;
    auto [o2_min, o2_max] = minmax(o2_head_vs);
    std::cout << "   O2 at head: mean=" << std::setprecision(1) << mean(o2_head_vs)
              << "%  range=[" << o2_min << ", " << o2_max << "]%" << std::endl;
    std::cout << "   URXL: V mean=" << std::setprecision(1) << mean(urxl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(urxl_v_vs)) << std::endl;
    std::cout << "   AQR:  V mean=" << std::setprecision(1) << mean(aqr_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(aqr_v_vs)) << std::endl;
    std::cout << "   AUAL: V mean=" << std::setprecision(1) << mean(aual_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(aual_v_vs)) << std::endl;

    // Step 35: CO₂ sensing diagnostics
    std::cout << "\n23. CO2 SENSING (Step 35):" << std::endl;
    auto [co2_min, co2_max] = minmax(co2_head_vs);
    std::cout << "   CO2 at head: mean=" << std::setprecision(2) << mean(co2_head_vs)
              << "%  range=[" << co2_min << ", " << co2_max << "]%" << std::endl;
    std::cout << "   BAGL: V mean=" << std::setprecision(1) << mean(bagl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(bagl_v_vs)) << std::endl;

    // Step 36: Proprioception diagnostics
    std::cout << "\n24. PROPRIOCEPTION (Step 36):" << std::endl;
    auto [mac_min, mac_max] = minmax(mean_abs_curv_vs);
    std::cout << "   mean |curv|: mean=" << std::setprecision(3) << mean(mean_abs_curv_vs)
              << " /mm  range=[" << mac_min << ", " << mac_max << "]" << std::endl;
    std::cout << "   DVA:  V mean=" << std::setprecision(1) << mean(dva_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(dva_v_vs)) << std::endl;
    std::cout << "   PVDL: V mean=" << std::setprecision(1) << mean(pvdl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(pvdl_v_vs)) << std::endl;

    // Step 38: Egg-laying diagnostics
    std::cout << "\n25. EGG-LAYING (Step 38):" << std::endl;
    std::cout << "   egg_pressure: final=" << std::setprecision(3) << egg_pressure_vs.back()
              << "  range=[" << std::setprecision(3) << minmax(egg_pressure_vs).first
              << ", " << minmax(egg_pressure_vs).second << "]" << std::endl;
    std::cout << "   eggs_laid: " << std::setprecision(0) << sim.egg_laid_count() << std::endl;
    std::cout << "   HSNL: V mean=" << std::setprecision(1) << mean(hsnl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(hsnl_v_vs)) << std::endl;
    std::cout << "   VC4:  V mean=" << std::setprecision(1) << mean(vc4_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(vc4_v_vs)) << std::endl;

    // Step 56: DMP diagnostics
    std::cout << "\n26. DEFECATION MOTOR PROGRAM (Step 56):" << std::endl;
    // Step 59: fix expectation — DMP only fires when timer resets ON food
    // Expected ≈ (duration/period) × near_food_fraction, NOT duration/period
    double dmp_max = (duration / 1000.0) / 45.0;
    double near_food_frac = (total_samples > 0) ? (double)near_food_samples / total_samples : 0.5;
    std::cout << "   DMP cycles: " << sim.dmp_count()
              << "  (max ~" << std::setprecision(0) << dmp_max
              << " at 45s period, adjusted for " << std::setprecision(0) << (near_food_frac * 100)
              << "% on food: ~" << std::setprecision(1) << (dmp_max * near_food_frac) << ")" << std::endl;
    {
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        double avl_v = (avl_id >= 0 && avl_id < nn) ? ns[avl_id]->get_membrane_potential() : -65.0;
        double dvb_v = (dvb_id >= 0 && dvb_id < nn) ? ns[dvb_id]->get_membrane_potential() : -65.0;
        std::cout << "   AVL:  V=" << std::setprecision(1) << avl_v
                  << " mV  S(release)=" << std::setprecision(3) << release(avl_v) << std::endl;
        std::cout << "   DVB:  V=" << std::setprecision(1) << dvb_v
                  << " mV  S(release)=" << std::setprecision(3) << release(dvb_v) << std::endl;
    }

    // Step 74: Nose touch circuit diagnostics
    std::cout << "\n27. NOSE TOUCH CIRCUIT (Step 73/74):" << std::endl;
    std::cout << "   FLPL:  V mean=" << std::setprecision(1) << mean(flpl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(flpl_v_vs)) << std::endl;
    std::cout << "   RIH:   V mean=" << std::setprecision(1) << mean(rih_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(rih_v_vs)) << std::endl;
    std::cout << "   IL1DL: V mean=" << std::setprecision(1) << mean(il1dl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(il1dl_v_vs)) << std::endl;
    std::cout << "   RMDDL: V mean=" << std::setprecision(1) << mean(rmddl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(rmddl_v_vs)) << std::endl;
    double nose_touch_pct = (total_samples > 0) ? 100.0 * nose_touch_samples / total_samples : 0;
    std::cout << "   nose_touch_active: " << std::setprecision(1) << nose_touch_pct
              << "% (" << nose_touch_samples << "/" << total_samples << " samples)" << std::endl;
    std::cout << "   connectome: " << conn.num_neurons() << " neurons, "
              << conn.num_synapses() << " synapses, "
              << conn.num_gap_junctions() << " gap junctions" << std::endl;

    // Step 120: Multisensory threat-reward decision diagnostic
    std::cout << "\n37. MULTISENSORY DECISION (Step 120, Ghosh 2016):" << std::endl;
    {
        double sat = sim.satiety();
        double sat_gate = 1.0 / (1.0 + std::exp(-12.0 * (sat - 0.4)));
        std::cout << "   satiety=" << std::setprecision(3) << sat
                  << "  sat_gate=" << std::setprecision(3) << sat_gate;
        if (sat_gate > 0.5) std::cout << " (THREAT-SENSITIVE: well-fed, ASH boosted)";
        else std::cout << " (THREAT-TOLERANT: hungry, ASH normal)";
        std::cout << std::endl;
        std::cout << "   RIM→ASH TYRA-2 top-down: satiety gates threat sensitivity" << std::endl;
        std::cout << "   Fed: TA×sat→TYRA-2→ASH↑ (avoid danger)"  << std::endl;
        std::cout << "   Hungry: pathway suppressed (cross barrier for food)" << std::endl;
    }

    // Step 119: FLP-20/RID cross-modal sensitization diagnostic
    std::cout << "\n36. CROSS-MODAL SENSITIZATION (Step 119, Chew 2018):" << std::endl;
    {
        int rid_id = conn.get_neuron_id("RID");
        int nn = (int)sim.neurons().size();
        std::cout << "   sensitization=" << std::setprecision(3) << sim.sensitization() << std::endl;
        if (rid_id >= 0 && rid_id < nn) {
            double rv = sim.neurons()[rid_id]->get_membrane_potential();
            std::cout << "   RID: V=" << std::setprecision(1) << rv << " mV"
                      << "  S(release)=" << std::setprecision(3) << release(rv) << std::endl;
        }
        std::cout << "   FLP-20→FRPR-3→RID: touch neurons→neuropeptide→neuroendocrine" << std::endl;
        std::cout << "   RID→ASH boost: " << std::setprecision(1) 
                  << (sim.sensitization() > 0.05 ? 12.0 * sim.sensitization() : 0.0)
                  << " pA (cross-modal)" << std::endl;
    }

    // Step 75: Pathogen aversion hub diagnostics
    std::cout << "\n28. PATHOGEN AVERSION (Step 75, AWB→AUA/RMG→AVA):" << std::endl;
    std::cout << "   RMGL:  V mean=" << std::setprecision(1) << mean(rmgl_v_vs)
              << " mV  S(release)=" << std::setprecision(3) << release(mean(rmgl_v_vs)) << std::endl;
    // AWB voltage from existing accumulator (via getV in loop)
    {
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        int awbl = conn.get_neuron_id("AWBL");
        int aual = conn.get_neuron_id("AUAL");
        double awb_v = (awbl >= 0 && awbl < nn) ? ns[awbl]->get_membrane_potential() : -65.0;
        double aua_v = (aual >= 0 && aual < nn) ? ns[aual]->get_membrane_potential() : -65.0;
        std::cout << "   AWBL:  V final=" << std::setprecision(1) << awb_v
                  << " mV  S(release)=" << std::setprecision(3) << release(awb_v) << std::endl;
        std::cout << "   AUAL:  V final=" << std::setprecision(1) << aua_v
                  << " mV  S(release)=" << std::setprecision(3) << release(aua_v) << std::endl;
    }
    std::cout << "   sickness: " << std::setprecision(3) << sim.sickness() << std::endl;

    // Step 76: Enhanced Slowing Response diagnostics
    std::cout << "\n29. ENHANCED SLOWING RESPONSE (Step 76, Sawin 2000):" << std::endl;
    std::cout << "   satiety: " << std::setprecision(3) << sim.satiety()
              << "  hunger: " << std::setprecision(3) << (1.0 - sim.satiety())
              << "  esr_receptor: " << std::setprecision(3) << sim.esr_receptor_level() << std::endl;
    double sht_conc = sim.neuromodulation().get_concentration("5-HT");
    std::cout << "   5-HT: " << std::setprecision(3) << sht_conc
              << "  esr_current: " << std::setprecision(2)
              << (-8.0 * sim.esr_receptor_level() * sht_conc) << " pA" << std::endl;
    std::cout << "   speed_scale: " << std::setprecision(3) << sim.neuromodulation().get_speed_scale() << std::endl;

    // Step 78: Tap habituation diagnostic
    std::cout << "\n31. TAP HABITUATION (Step 78, Rankin 1990):" << std::endl;
    std::cout << "   taps delivered: " << tap_pool_at_onset.size() << std::endl;
    if (!tap_pool_at_onset.empty()) {
        // Print per-tap data
        std::cout << "   tap#  pool    sens   reversal" << std::endl;
        for (size_t i = 0; i < tap_pool_at_onset.size(); ++i) {
            std::cout << "   " << std::setw(3) << (i+1)
                      << "   " << std::setprecision(3) << std::fixed << tap_pool_at_onset[i]
                      << "  " << std::setprecision(3) << (i < tap_sens_at_onset.size() ? tap_sens_at_onset[i] : 0.0)
                      << "   " << (tap_got_reversal[i] ? "YES" : "no") << std::endl;
        }
        // Summary: first 5 vs last 5 reversal rate
        int n_taps = (int)tap_pool_at_onset.size();
        int first_n = std::min(5, n_taps);
        int last_n = std::min(5, n_taps);
        int first_rev = 0, last_rev = 0;
        for (int i = 0; i < first_n; ++i)
            if (tap_got_reversal[i]) first_rev++;
        for (int i = n_taps - last_n; i < n_taps; ++i)
            if (tap_got_reversal[i]) last_rev++;
        double first_pct = 100.0 * first_rev / first_n;
        double last_pct = 100.0 * last_rev / last_n;
        std::cout << std::defaultfloat;
        std::cout << "   first " << first_n << " taps: " << first_rev << "/" << first_n
                  << " reversals (" << std::setprecision(0) << first_pct << "%)" << std::endl;
        std::cout << "   last  " << last_n << " taps: " << last_rev << "/" << last_n
                  << " reversals (" << std::setprecision(0) << last_pct << "%)" << std::endl;
        std::cout << "   pool first=" << std::setprecision(3) << tap_pool_at_onset.front()
                  << " last=" << tap_pool_at_onset.back()
                  << " (Δ=" << std::setprecision(1)
                  << (100.0*(tap_pool_at_onset.back() - tap_pool_at_onset.front())/tap_pool_at_onset.front())
                  << "%)" << std::endl;
        // Step 79: Dishabituation analysis — find tap immediately after dishabit stimulus
        if (cli_dishabit_at > 0) {
            int pre_dh_count = 0, post_dh_count = 0;
            int pre_dh_rev = 0, post_dh_rev = 0;
            for (size_t i = 0; i < tap_pool_at_onset.size(); ++i) {
                double tap_time = (i + 1) * 10.0; // approx tap time in seconds
                if (tap_time < cli_dishabit_at - 20 && tap_time > cli_dishabit_at - 70) {
                    // Last 5 taps before dishabit (habituated)
                    pre_dh_count++;
                    if (tap_got_reversal[i]) pre_dh_rev++;
                }
                if (tap_time > cli_dishabit_at && tap_time < cli_dishabit_at + 50) {
                    // First 5 taps after dishabit
                    post_dh_count++;
                    if (tap_got_reversal[i]) post_dh_rev++;
                }
            }
            std::cout << std::defaultfloat;
            std::cout << "   --- DISHABITUATION (stimulus at " << cli_dishabit_at << "s) ---" << std::endl;
            std::cout << "   sensitization: " << std::setprecision(3) << sim.sensitization() << std::endl;
            if (pre_dh_count > 0)
                std::cout << "   pre-dishabit:  " << pre_dh_rev << "/" << pre_dh_count
                          << " reversals (" << std::setprecision(0) << (100.0*pre_dh_rev/pre_dh_count) << "%)" << std::endl;
            if (post_dh_count > 0)
                std::cout << "   post-dishabit: " << post_dh_rev << "/" << post_dh_count
                          << " reversals (" << std::setprecision(0) << (100.0*post_dh_rev/post_dh_count) << "%)" << std::endl;
        }
    }

    std::cout << "\n9. DISTANCE TO FOOD:" << std::endl;
    std::cout << "   initial=" << std::setprecision(2) << dists.front()
              << "  final=" << dists.back() << " mm" << std::endl;
    double ci = (dists.front() - dists.back()) / dists.front();
    std::cout << "   CI=" << std::setprecision(3) << ci << std::endl;
    double near_pct = (total_samples > 0) ? 100.0 * near_food_samples / total_samples : 0;
    std::cout << "   time_near_food(<5mm): " << std::setprecision(1) << near_pct << "% ("
              << std::setprecision(1) << near_food_samples * 0.1 << "s / " << duration/1000.0 << "s)" << std::endl;
    std::cout << "   reversal_rate: " << std::setprecision(2) << reversal_count / (duration/1000.0) << " /s ("
              << reversal_count << " total, target ~0.1/s)" << std::endl;

    // Step 97: O₂ SPATIAL DISTRIBUTION (Gray 2004, Chang 2006, Cheung 2005)
    // Gaussian food model: O₂ = 21% - 13% × food_density
    // Center (<5mm): O₂ ≈ 8-12%, border (5-12mm): 12-18%, open (>12mm): 18-21%
    // NOTE: "bordering" requires flat lawn; gaussian model shows O₂-modulated exploration
    // Key difference: Hawaiian (npr-1 lf, RMG active) shows more open-field exploration
    //   due to RMG→AVA reversal drive, while N2 stays near food via chemotaxis
    {
        int center_n = 0, border_n = 0, open_n = 0;
        double center_r = 5.0, border_r = 12.0;
        for (double d : dists) {
            if (d < center_r) center_n++;
            else if (d < border_r) border_n++;
            else open_n++;
        }
        int tot = (int)dists.size();
        double center_pct = tot > 0 ? 100.0 * center_n / tot : 0;
        double border_pct = tot > 0 ? 100.0 * border_n / tot : 0;
        double open_pct   = tot > 0 ? 100.0 * open_n / tot : 0;
        std::cout << "\n35. O2 SPATIAL DISTRIBUTION (Step 97, Chang 2006):" << std::endl;
        std::cout << "   Lawn center (<" << center_r << "mm): "
                  << std::setprecision(1) << center_pct << "%" << std::endl;
        std::cout << "   Lawn border (" << center_r << "-" << border_r << "mm): "
                  << std::setprecision(1) << border_pct << "%" << std::endl;
        std::cout << "   Open field (>" << border_r << "mm): "
                  << std::setprecision(1) << open_pct << "%" << std::endl;
        std::cout << "   (Compare --npr1 0 for Hawaiian: expect more open-field time)" << std::endl;
    }

    // Step 98: AREA-RESTRICTED SEARCH (Hills 2004, López-Cruz 2019)
    // After food removal, reversal rate should decay: local search → global search
    // food_memory_ tau_decay=90s drives the transition
    // Use --food-removal <sec> to test
    if (cli_food_removal > 0) {
        double removal_ms = cli_food_removal * 1000.0;
        double bin_s = 30.0;  // 30-second bins
        std::cout << "\n36. AREA-RESTRICTED SEARCH (Step 98, Hills 2004):" << std::endl;
        std::cout << "   Food removed at t=" << std::setprecision(0) << cli_food_removal << ", food_memory tau_decay=" << 300 << "s" << std::endl;
        // Count reversals in time bins after food removal
        int n_bins = std::min(6, (int)((duration - removal_ms) / (bin_s * 1000.0)));
        int prev_count = 0;
        for (double t_ms = 0; t_ms < removal_ms; ++t_ms) {}
        // Count reversals BEFORE food removal
        int pre_rev = 0;
        double pre_window = std::min(removal_ms, 60000.0);  // last 60s before removal
        for (double rt : reversal_times) {
            if (rt >= removal_ms - pre_window && rt < removal_ms) pre_rev++;
        }
        double pre_rate = pre_rev / (pre_window / 1000.0);
        std::cout << "   Pre-removal (last 60s): " << std::setprecision(2) << pre_rate
                  << " rev/s (" << pre_rev << " reversals)" << std::endl;
        // Count reversals in post-removal bins
        double first_bin_rate = 0, last_bin_rate = 0;
        for (int b = 0; b < n_bins; ++b) {
            double bin_start = removal_ms + b * bin_s * 1000.0;
            double bin_end = bin_start + bin_s * 1000.0;
            int bin_rev = 0;
            for (double rt : reversal_times) {
                if (rt >= bin_start && rt < bin_end) bin_rev++;
            }
            double bin_rate = bin_rev / bin_s;
            if (b == 0) first_bin_rate = bin_rate;
            if (b == n_bins - 1) last_bin_rate = bin_rate;
            std::cout << "   t+" << std::setprecision(0) << b * bin_s << "-"
                      << (b + 1) * bin_s << "s: " << std::setprecision(2) << bin_rate
                      << " rev/s (" << bin_rev << ")" << std::endl;
        }
        // ARS assessment
        if (n_bins >= 2) {
            double ratio = (first_bin_rate > 0.01) ? last_bin_rate / first_bin_rate : 1.0;
            if (ratio < 0.6) {
                std::cout << "   [OK] Local→Global transition detected (rate decay "
                          << std::setprecision(0) << (1.0 - ratio) * 100 << "%)" << std::endl;
            } else if (first_bin_rate > pre_rate * 0.8) {
                std::cout << "   [..] Reversal rate elevated but no clear decay" << std::endl;
            } else {
                std::cout << "   [..] No clear ARS pattern" << std::endl;
            }
        }
    }

    // Step 19b: Intermediate neuron diagnostic — pirouette pathway
    std::cout << "\n11. PIROUETTE SIGNAL CHAIN:" << std::endl;
    std::cout << "   AWC (OFF): L=" << std::setprecision(2) << mean(awcl_vs)
              << "  R=" << mean(awcr_vs) << " mV" << std::endl;
    std::cout << "   AIA:       L=" << mean(aial_vs) << "  R=" << mean(aiar_vs) << " mV" << std::endl;
    std::cout << "   AIB:       L=" << mean(aibl_vs) << "  R=" << mean(aibr_vs) << " mV" << std::endl;
    std::cout << "   AIY:       L=" << mean(aiyl_vs) << "  R=" << mean(aiyr_vs) << " mV" << std::endl;
    std::cout << "   RIA:       L=" << mean(rial_vs) << "  R=" << mean(riar_vs) << " mV" << std::endl;
    std::cout << "   AVA:       L=" << mean(aval_vs) << " mV" << std::endl;
    // Release rates at V_thresh=-35, slope=5 (reuses 'release' lambda from Step 31 section)
    std::cout << "   Release rates (S = sigmoid(V)):" << std::endl;
    std::cout << "     ASEL=" << std::setprecision(3) << release(mean(asel_vs))
              << "  ASER=" << release(mean(aser_vs))
              << "  AWCL=" << release(mean(awcl_vs))
              << "  AWCR=" << release(mean(awcr_vs)) << std::endl;
    std::cout << "     AIAL=" << release(mean(aial_vs))
              << "  AIAR=" << release(mean(aiar_vs))
              << "  AIBL=" << release(mean(aibl_vs))
              << "  AIBR=" << release(mean(aibr_vs)) << std::endl;
    std::cout << "     AVAL=" << release(mean(aval_vs)) << std::endl;

    // Step 20: Neuromodulation diagnostic
    std::cout << "\n12. NEUROMODULATION (Layer 6):" << std::endl;
    const auto& mods = sim.neuromodulation().modulators();
    for (const auto& mod : mods) {
        std::cout << "   " << mod.name << ": conc=" << std::setprecision(4) << mod.concentration
                  << "  sources=" << mod.source_neuron_ids.size()
                  << "  targets=" << mod.targets.size() << std::endl;
    }
    std::cout << "   speed_scale=" << std::setprecision(3) << sim.neuromodulation().get_speed_scale()
              << "  (effective=" << sim.neuromodulation().get_speed_scale() * sim.params.speed_scale << ")" << std::endl;

    // Step 45: NSM 5-HT source diagnostic
    {
        double nsml_v_mean = mean(nsml_v_vs);
        double nsml_s_mean = mean(nsml_s_vs);
        double nsml_s_max = *std::max_element(nsml_s_vs.begin(), nsml_s_vs.end());
        double nsml_v_max = *std::max_element(nsml_v_vs.begin(), nsml_v_vs.end());
        double nsml_v_min = *std::min_element(nsml_v_vs.begin(), nsml_v_vs.end());
        // Compute how much above threshold the release rate is
        double threshold = 0.25;  // 5-HT release_threshold (Step 59: 0.30→0.25)
        double above_thresh = nsml_s_mean - threshold;
        double max_possible = 1.0 - threshold;
        double release_drive_est = (above_thresh > 0 && max_possible > 0) ? above_thresh / max_possible : 0.0;
        std::cout << "   NSM (5-HT source): V mean=" << std::setprecision(1) << nsml_v_mean
                  << " range=[" << nsml_v_min << ", " << nsml_v_max << "] mV" << std::endl;
        std::cout << "     S(release) mean=" << std::setprecision(3) << nsml_s_mean
                  << " max=" << nsml_s_max
                  << "  threshold=" << threshold
                  << "  above=" << std::setprecision(3) << above_thresh
                  << "  est_drive=" << release_drive_est << std::endl;
        std::cout << "     satiety=" << std::setprecision(2) << sim.satiety()
                  << "  (Step 45: NSM not suppressed by satiety — Randi 2018)" << std::endl;
    }

    // Time series: 5-HT, DA, OA, satiety, distance every 20s
    std::cout << "   Time series (every 20s):" << std::endl;
    std::cout << "     t(s)  dist   x_pos  y_pos  r_dist ASH_I  5-HT   sat   sick  fmem  fatg  slp  speed" << std::endl;
    int samples_per_20s = (int)(20000.0 / 100.0); // 200 samples per 20s
    for (int t = 0; t < 15; ++t) {
        int idx = (t + 1) * samples_per_20s - 1;
        if (idx < (int)sht_vs.size()) {
            // Compute satiety gain mode for this time point
            double s_sw = 1.0 / (1.0 + std::exp(-10.0 * (satiety_vs[idx] - 0.5)));
            const char* mode = (satiety_vs[idx] > 0.5) ? "<-Tc" : "->Fd";
            std::cout << "     " << std::setw(4) << (t + 1) * 20 << "  "
                      << std::setprecision(2) << std::setw(5) << dist_vs_time[idx] << "  "
                      << std::setprecision(1) << std::setw(5) << xpos_vs[idx] << "  "
                      << std::setprecision(1) << std::setw(5) << ypos_vs[idx] << "  "
                      << std::setprecision(1) << std::setw(5) << rep_dist_vs[idx] << "  "
                      << std::setprecision(1) << std::setw(5) << ash_i_vs[idx] << "  "
                      << std::setprecision(3) << std::setw(5) << sht_vs[idx] << "  "
                      << std::setw(5) << satiety_vs[idx] << "  "
                      << std::setprecision(3) << std::setw(5) << sick_vs[idx] << "  "
                      << std::setprecision(3) << std::setw(5) << fmem_vs[idx] << "  "
                      << std::setprecision(3) << std::setw(5) << fatigue_vs[idx] << "  "
                      << std::setw(3) << sleep_vs[idx] << "  "
                      << std::setprecision(4) << std::setw(6) << actual_speed_vs[idx]
                      << "  " << mode << std::endl;
        }
    }

    // Step 23: Thermotaxis diagnostic + gradient conflict analysis
    {
        std::cout << "\n14. THERMOTAXIS (Step 23):" << std::endl;
        Vector2d hp = sim.body().get_head_position();
        double temp_now = sim.environment().sample_temperature(hp);
        Vector2d tgrad = sim.environment().temperature_gradient(hp);
        double tgrad_mag = std::sqrt(tgrad.x * tgrad.x + tgrad.y * tgrad.y);
        int afdl_id = conn.get_neuron_id("AFDL");
        int afdr_id = conn.get_neuron_id("AFDR");
        double afdl_v = (afdl_id >= 0) ? sim.neurons()[afdl_id]->get_membrane_potential() : 0;
        double afdr_v = (afdr_id >= 0) ? sim.neurons()[afdr_id]->get_membrane_potential() : 0;
        double afd_S = 1.0 / (1.0 + std::exp(-(afdl_v - (-35.0)) / 5.0));
        double tc_learned = sim.learned_tc();
        std::cout << "   Temperature at head: " << std::setprecision(1) << std::fixed << temp_now << " C"
                  << "  Tc(initial)=22.5  Tc(learned)=" << std::setprecision(2) << tc_learned << " C"
                  << "  dTc=" << std::setprecision(2) << (tc_learned - 22.5) << std::endl;
        std::cout << "   Temp gradient: " << std::setprecision(3) << tgrad_mag << " C/mm"
                  << " (dir: " << std::setprecision(2) << tgrad.x << ", " << tgrad.y << ")"
                  << "  Tc target: x~" << std::setprecision(0) << (25.0 + (22.5 - 20.0) / 0.5) << "mm (LEFT)" << std::endl;
        std::cout << "   AFD: L=" << std::setprecision(2) << afdl_v << " mV  R=" << afdr_v << " mV"
                  << "  S(release)=" << std::setprecision(3) << afd_S << std::defaultfloat << std::endl;
        // Compare AFD vs ASE signal strength
        int asel_id2 = conn.get_neuron_id("ASEL");
        double asel_v2 = (asel_id2 >= 0) ? sim.neurons()[asel_id2]->get_membrane_potential() : 0;
        double ase_S = 1.0 / (1.0 + std::exp(-(asel_v2 - (-35.0)) / 5.0));
        std::cout << "   Signal strength: AFD_S=" << std::setprecision(3) << afd_S
                  << " vs ASE_S=" << ase_S
                  << " (ratio=" << std::setprecision(2) << (ase_S > 0.01 ? afd_S / ase_S : 0) << ")" << std::endl;
        // Satiety gain modulation (Step 23c — sigmoid switch)
        double sat = sim.satiety();
        double sw = 1.0 / (1.0 + std::exp(-10.0 * (sat - 0.5)));
        double chemo_g = 1.0 - 0.85 * sw;
        double thermo_g = 0.2 + 1.8 * sw;
        std::cout << "   Satiety gain: sat=" << std::setprecision(2) << sat
                  << " → chemo×" << std::setprecision(2) << chemo_g
                  << " thermo×" << std::setprecision(2) << thermo_g
                  << (sat > 0.5 ? " [TEMP mode]" : " [FOOD mode]") << std::endl;
        // Step 24: Pharyngeal pumping diagnostics
        std::cout << "   Pharynx: pump_rate=" << std::setprecision(1) << sim.pump_rate_hz() << " Hz"
                  << "  total_pumps=" << sim.total_pumps()
                  << "  V_muscle=" << std::setprecision(1) << sim.pharynx_V() << " mV"
                  << "  phase=";
        switch (sim.pharynx().phase()) {
            case PharyngealPump::Phase::RESTING: std::cout << "REST"; break;
            case PharyngealPump::Phase::EXCITATION: std::cout << "E"; break;
            case PharyngealPump::Phase::PLATEAU: std::cout << "P"; break;
            case PharyngealPump::Phase::REPOLARIZATION: std::cout << "R"; break;
        }
        std::cout << std::endl;
        // MC/M3 neuron voltages
        int mcl_id = conn.get_neuron_id("MCL");
        int m3l_id = conn.get_neuron_id("M3L");
        int m4_id = conn.get_neuron_id("M4");
        double mcl_v = (mcl_id >= 0) ? sim.neurons()[mcl_id]->get_membrane_potential() : 0;
        double m3l_v = (m3l_id >= 0) ? sim.neurons()[m3l_id]->get_membrane_potential() : 0;
        double m4_v = (m4_id >= 0) ? sim.neurons()[m4_id]->get_membrane_potential() : 0;
        std::cout << "   MC=" << std::setprecision(1) << mcl_v << "mV"
                  << "  M3=" << m3l_v << "mV"
                  << "  M4=" << m4_v << "mV"
                  << "  5-HT→MC: +" << std::setprecision(1) << 15.0 * sim.neuromodulation().get_concentration("5-HT") << "pA"
                  << "  OA→MC: " << std::setprecision(1) << -10.0 * sim.neuromodulation().get_concentration("OA") << "pA"
                  << std::endl;
        // Conflict analysis: did worm go toward food (right) or Tc (left)?
        // Tc target: T(x)=20+(-0.5)(x-25)=Tc → x=25-(Tc-20)/0.5
        double tc_x = 25.0 - (22.5 - 20.0) / 0.5;  // = 20mm
        double dx = hp.x - 25.0;  // positive = went RIGHT (food), negative = went LEFT (Tc)
        std::cout << "   X displacement: " << std::setprecision(1) << std::fixed << dx << " mm"
                  << (dx > 1.0 ? " -> FOOD wins" : (dx < -1.0 ? " -> TEMP wins" : " -> undecided"))
                  << "  (food@right x=35, Tc@left x=" << std::setprecision(0) << tc_x << ")"
                  << std::defaultfloat << std::endl;
    }

    // Step 81: PHB/PHA tail chemosensation diagnostic
    {
        auto tail = sim.body().get_tail_position();
        double rep_tail = sim.environment().sample_repellent(tail);
        double food_tail = sim.environment().sample_food_density(tail);
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        int phbl_id = conn.get_neuron_id("PHBL");
        int phal_id = conn.get_neuron_id("PHAL");
        double phbl_v = (phbl_id >= 0 && phbl_id < nn) ? ns[phbl_id]->get_membrane_potential() : 0;
        double phal_v = (phal_id >= 0 && phal_id < nn) ? ns[phal_id]->get_membrane_potential() : 0;
        std::cout << "\n32. TAIL CHEMOSENSATION (Step 81):" << std::endl;
        std::cout << "   Tail pos: (" << std::setprecision(1) << tail.x << ", " << tail.y << ")"
                  << "  rep@tail=" << std::setprecision(4) << rep_tail
                  << "  food@tail=" << std::setprecision(3) << food_tail << std::endl;
        std::cout << "   PHBL: " << std::setprecision(1) << phbl_v << " mV"
                  << "  PHAL: " << phal_v << " mV" << std::defaultfloat << std::endl;
    }

    // Step 25: ASH nociception diagnostic
    std::cout << "\n16. NOCICEPTION (Step 25):" << std::endl;
    std::cout << "   Repellent source: (" << repellent.x << ", " << repellent.y << ")" << std::endl;
    {
        auto head = sim.body().get_head_position();
        double rep_conc = sim.environment().sample_repellent(head);
        double rep_dx = repellent.x - head.x, rep_dy = repellent.y - head.y;
        double rep_dist = std::sqrt(rep_dx*rep_dx + rep_dy*rep_dy);
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        std::cout << "   Repellent at head: " << std::setprecision(4) << rep_conc
                  << "  dist_to_repellent=" << std::setprecision(1) << rep_dist << " mm" << std::endl;
        if (ashl_id >= 0 && ashl_id < nn) {
            std::cout << "   ASHL: " << std::setprecision(1)
                      << ns[ashl_id]->get_membrane_potential() << " mV"
                      << "  I_ext=" << std::setprecision(2) << ns[ashl_id]->get_I_ext() << " pA" << std::endl;
        }
        if (aibl_id >= 0 && aibl_id < nn) {
            std::cout << "   AIBL: " << std::setprecision(1)
                      << ns[aibl_id]->get_membrane_potential() << " mV"
                      << "  I_syn=" << std::setprecision(2) << ns[aibl_id]->get_I_syn() << " pA" << std::endl;
        }
    }

    // Step 26: Learned pathogen avoidance diagnostic
    std::cout << "\n17. PATHOGEN LEARNING (Step 26):" << std::endl;
    std::cout << "   sickness=" << std::setprecision(4) << sim.sickness() << std::endl;
    {
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        if (adfl_id >= 0 && adfl_id < nn) {
            std::cout << "   ADFL: " << std::setprecision(1)
                      << ns[adfl_id]->get_membrane_potential() << " mV"
                      << "  I_ext=" << std::setprecision(2) << ns[adfl_id]->get_I_ext() << " pA" << std::endl;
        }
        // Show AWC→AIY and AWC→AIB w_mod values (learning locus)
        for (const auto& syn : sim.connectome().synapses()) {
            int pre = syn.pre_id(), post = syn.post_id();
            if (pre < 0 || pre >= nn || post < 0 || post >= nn) continue;
            const std::string& prn = sim.connectome().neuron_infos()[pre].name;
            const std::string& pon = sim.connectome().neuron_infos()[post].name;
            if (prn == "AWCL" && (pon == "AIYL" || pon == "AIBL")) {
                std::cout << "   " << prn << "->" << pon << ": w_mod="
                          << std::setprecision(4) << syn.weight_mod() << std::endl;
            }
        }
    }

    // Step 77: Salt chemotaxis learning diagnostic (ASER w_mod)
    std::cout << "\n30. SALT CHEMOTAXIS LEARNING (Step 21c/77):" << std::endl;
    {
        const auto& syns = sim.connectome().synapses();
        const auto& ni = sim.connectome().neuron_infos();
        int nn = (int)sim.neurons().size();
        for (size_t i = 0; i < syns.size(); ++i) {
            int pre = syns[i].pre_id(), post = syns[i].post_id();
            if (pre < 0 || pre >= nn || post < 0 || post >= nn) continue;
            const std::string& pn = ni[pre].name;
            const std::string& qn = ni[post].name;
            if (pn == "ASER" && (qn == "AIBL" || qn == "AIBR" || qn == "AIAL" || qn == "AIAR")) {
                std::cout << "   " << pn << "->" << qn << ": w_mod="
                          << std::setprecision(4) << syns[i].weight_mod()
                          << " (weight=" << std::setprecision(2) << syns[i].weight() << ")"
                          << std::endl;
            }
        }
        std::cout << "   satiety=" << std::setprecision(3) << sim.satiety()
                  << "  learn_signal=" << std::setprecision(3) << (sim.satiety() - 0.5) << std::endl;
    }

    // Step 117: Associative odor-food conditioning diagnostic
    std::cout << "\n35. ASSOCIATIVE ODOR CONDITIONING (Step 117):" << std::endl;
    {
        const auto& syns = sim.connectome().synapses();
        const auto& ni = sim.connectome().neuron_infos();
        int nn = (int)sim.neurons().size();
        double wmod_sum = 0.0; int wmod_n = 0;
        for (size_t i = 0; i < syns.size(); ++i) {
            int pre = syns[i].pre_id(), post = syns[i].post_id();
            if (pre < 0 || pre >= nn || post < 0 || post >= nn) continue;
            const std::string& pn = ni[pre].name;
            const std::string& qn = ni[post].name;
            if ((pn == "AWCL" || pn == "AWCR") && (qn == "AIYL" || qn == "AIYR")) {
                std::cout << "   " << pn << "->" << qn << ": w_mod="
                          << std::setprecision(4) << syns[i].weight_mod() << std::endl;
                wmod_sum += syns[i].weight_mod(); wmod_n++;
            }
        }
        if (wmod_n > 0) {
            double avg = wmod_sum / wmod_n;
            std::cout << "   AWC->AIY mean w_mod=" << std::setprecision(4) << avg;
            if (avg > 1.05) std::cout << " (POSITIVE conditioning: enhanced attraction)";
            else if (avg < 0.95) std::cout << " (NEGATIVE conditioning: learned aversion)";
            else std::cout << " (neutral: no significant conditioning)";
            std::cout << std::endl;
        }
        std::cout << "   INS-1=" << std::setprecision(3) << sim.ins1_conc()
                  << "  satiety=" << std::setprecision(3) << sim.satiety() << std::endl;
    }

    // Step 27: Sleep / Quiescence diagnostic
    std::cout << "\n18. SLEEP / QUIESCENCE (Step 27):" << std::endl;
    std::cout << "   fatigue=" << std::setprecision(4) << sim.fatigue()
              << "  is_sleeping=" << (sim.is_sleeping() ? "YES" : "NO") << std::endl;
    {
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        if (ris_id >= 0 && ris_id < nn) {
            double rv = ns[ris_id]->get_membrane_potential();
            double flp11 = 1.0 / (1.0 + std::exp(-(rv - (-35.0)) / 5.0));
            std::cout << "   RIS: " << std::setprecision(1) << rv << " mV"
                      << "  I_ext=" << std::setprecision(2) << ns[ris_id]->get_I_ext() << " pA"
                      << "  FLP-11=" << std::setprecision(3) << flp11 << std::endl;
        }
        // Sleep episode analysis
        int sleep_episodes = 0;
        double total_sleep_time = 0;
        bool was_sleeping = false;
        for (size_t i = 0; i < sleep_vs.size(); ++i) {
            if (sleep_vs[i] && !was_sleeping) sleep_episodes++;
            if (sleep_vs[i]) total_sleep_time += 0.1;  // 100ms per sample
            was_sleeping = sleep_vs[i];
        }
        double sleep_pct = (sleep_vs.size() > 0) ? 100.0 * total_sleep_time / (sleep_vs.size() * 0.1) : 0;
        std::cout << "   Sleep episodes: " << sleep_episodes
                  << "  total_sleep=" << std::setprecision(1) << total_sleep_time << "s"
                  << " (" << std::setprecision(1) << sleep_pct << "%)" << std::endl;
        // Fatigue time course summary
        double max_fatigue = 0, min_fatigue = 1.0;
        for (double f : fatigue_vs) {
            if (f > max_fatigue) max_fatigue = f;
            if (f < min_fatigue) min_fatigue = f;
        }
        std::cout << "   Fatigue range: [" << std::setprecision(3) << min_fatigue
                  << ", " << max_fatigue << "]" << std::endl;
    }

    // Step 21: Short-term plasticity diagnostic
    std::cout << "\n15. SYNAPTIC PLASTICITY (Step 21):" << std::endl;
    const auto& synapses = sim.connectome().synapses();
    const auto& ninfos = sim.connectome().neuron_infos();
    double avg_vesicle = 0, min_vesicle = 1.0;
    int syn_count = 0;
    for (const auto& syn : synapses) {
        avg_vesicle += syn.vesicle_pool();
        if (syn.vesicle_pool() < min_vesicle) min_vesicle = syn.vesicle_pool();
        syn_count++;
    }
    if (syn_count > 0) avg_vesicle /= syn_count;
    std::cout << "   vesicle_pool: mean=" << std::setprecision(3) << avg_vesicle
              << "  min=" << min_vesicle << "  (1.0=full, 0.01=depleted)" << std::endl;
    // Show representative synapses by circuit
    for (const auto& syn : synapses) {
        int pre = syn.pre_id(), post = syn.post_id();
        if (pre < 0 || post < 0 || pre >= (int)ninfos.size() || post >= (int)ninfos.size()) continue;
        const auto& pn = ninfos[pre].name;
        const auto& qn = ninfos[post].name;
        // Show one from each circuit type
        if ((pn == "SMDDL" && qn == "SMDVL") ||
            (pn == "ALML" && (qn == "AVDL" || qn == "AVAL")) ||
            (pn == "ASER" && (qn == "AIAR" || qn == "AIYR"))) {
            std::cout << "   " << pn << "->" << qn
                      << ": n=" << std::setprecision(3) << syn.vesicle_pool()
                      << " p=" << syn.release_prob()
                      << " w_mod=" << syn.weight_mod() << std::endl;
        }
    }

    // Step 29: WAVE PROPAGATION DIAGNOSTICS
    std::cout << "\n19. WAVE PROPAGATION (Step 29):" << std::endl;
    {
        auto [c2_min, c2_max] = minmax(curv_seg2_vs);
        auto [c7_min, c7_max] = minmax(curv_seg7_vs);
        auto [c15_min, c15_max] = minmax(curv_seg15_vs);
        std::cout << "   Curvature at seg  2 (head): range=[" << std::setprecision(3) << c2_min << ", " << c2_max
                  << "]  amp=" << (c2_max - c2_min) << " /mm" << std::endl;
        std::cout << "   Curvature at seg  7 (mid1): range=[" << c7_min << ", " << c7_max
                  << "]  amp=" << (c7_max - c7_min) << " /mm" << std::endl;
        std::cout << "   Curvature at seg 15 (mid2): range=[" << c15_min << ", " << c15_max
                  << "]  amp=" << (c15_max - c15_min) << " /mm" << std::endl;
        double sign_change_hz = curv7_sign_changes / (duration * 0.001);  // duration in ms -> seconds
        std::cout << "   Seg 7 sign-change rate: " << std::setprecision(1) << sign_change_hz
                  << " Hz  (normal: 0.5-5, unstable: >100)" << std::endl;
        std::cout << "   Muscle work: mean=" << std::setprecision(3) << mean(muscle_work_vs);
        auto [mw_min, mw_max] = minmax(muscle_work_vs);
        std::cout << "  range=[" << mw_min << ", " << mw_max << "]" << std::endl;
        // Wave propagation quality assessment
        double head_amp = c2_max - c2_min;
        double mid_amp = c7_max - c7_min;
        double tail_amp = c15_max - c15_min;
        if (mid_amp > 0.3 * head_amp && tail_amp > 0.2 * head_amp) {
            std::cout << "   Wave quality: GOOD (propagates to tail)" << std::endl;
        } else if (mid_amp > 0.1 * head_amp) {
            std::cout << "   Wave quality: PARTIAL (weakens before tail)" << std::endl;
        } else {
            std::cout << "   Wave quality: POOR (stuck at head)" << std::endl;
        }
    }

    // Step 95: ROAMING/DWELLING STATE ANALYSIS (Flavell 2013 Cell)
    // Primary discriminator: 5-HT concentration (NSM activity = dwelling marker)
    // Roaming: 5-HT < 0.35 AND not sleeping (PDF↑, fast runs)
    // Dwelling: 5-HT ≥ 0.35 AND not sleeping (5-HT↑, slow + many reversals)
    // Sleep: is_sleeping = true (FLP-11↑)
    // REF: Flavell 2013 — NSM Ca²⁺ activity anti-correlates with roaming
    //      Ben Arous 2009 — roaming/dwelling on bacterial lawn
    {
        const double sht_threshold = 0.35;  // 5-HT boundary between states
        int roam_samples = 0, dwell_samples = 0, sleep_samples = 0;
        int roam_bouts = 0, dwell_bouts = 0;
        int prev_state = -1; // 0=roam, 1=dwell, 2=sleep
        double roam_speed_sum = 0, dwell_speed_sum = 0;
        double roam_5ht_sum = 0, dwell_5ht_sum = 0;
        size_t n_ts = std::min({actual_speed_vs.size(), sleep_vs.size(), sht_vs.size()});
        for (size_t i = 0; i < n_ts; ++i) {
            int state;
            if (sleep_vs[i]) {
                state = 2;
                sleep_samples++;
            } else if (sht_vs[i] < sht_threshold) {
                state = 0; // roaming (low 5-HT)
                roam_samples++;
                roam_speed_sum += actual_speed_vs[i];
                roam_5ht_sum += sht_vs[i];
            } else {
                state = 1; // dwelling (high 5-HT)
                dwell_samples++;
                dwell_speed_sum += actual_speed_vs[i];
                dwell_5ht_sum += sht_vs[i];
            }
            if (state != prev_state && state < 2 && prev_state < 2 && prev_state >= 0) {
                if (state == 0) roam_bouts++;
                else dwell_bouts++;
            }
            if (state < 2) prev_state = state;
        }
        double total_awake = roam_samples + dwell_samples;
        double roam_pct = total_awake > 0 ? 100.0 * roam_samples / total_awake : 0;
        double dwell_pct = total_awake > 0 ? 100.0 * dwell_samples / total_awake : 0;
        double sleep_pct = n_ts > 0 ? 100.0 * sleep_samples / n_ts : 0;
        double roam_speed = roam_samples > 0 ? roam_speed_sum / roam_samples : 0;
        double dwell_speed = dwell_samples > 0 ? dwell_speed_sum / dwell_samples : 0;
        double roam_5ht = roam_samples > 0 ? roam_5ht_sum / roam_samples : 0;
        double dwell_5ht = dwell_samples > 0 ? dwell_5ht_sum / dwell_samples : 0;
        std::cout << "\n33. ROAMING/DWELLING STATES (Step 95, Flavell 2013):" << std::endl;
        std::cout << "   Roaming:  " << std::setprecision(1) << roam_pct << "% of awake time ("
                  << roam_bouts << " bouts, speed=" << std::setprecision(3) << roam_speed
                  << " mm/s, 5-HT=" << std::setprecision(3) << roam_5ht << ")" << std::endl;
        std::cout << "   Dwelling: " << std::setprecision(1) << dwell_pct << "% of awake time ("
                  << dwell_bouts << " bouts, speed=" << std::setprecision(3) << dwell_speed
                  << " mm/s, 5-HT=" << std::setprecision(3) << dwell_5ht << ")" << std::endl;
        std::cout << "   Sleep:    " << std::setprecision(1) << sleep_pct << "% of total time" << std::endl;
        std::cout << "   5-HT final=" << std::setprecision(3) << sim.neuromodulation().get_concentration("5-HT")
                  << "  PDF final=" << std::setprecision(3) << sim.neuromodulation().get_concentration("PDF") << std::endl;
        // Validation: roaming speed should be > dwelling speed (Flavell 2013)
        bool speed_diff = roam_speed > dwell_speed * 1.15;
        int transitions = roam_bouts + dwell_bouts;
        double awake_sec = total_awake * 0.1; // 100ms sample interval
        if (transitions >= 2 && roam_pct > 8 && dwell_pct > 8 && speed_diff) {
            std::cout << "   [OK] Bistable: " << transitions << " R↔D transitions, speed ratio="
                      << std::setprecision(2) << (dwell_speed > 0 ? roam_speed/dwell_speed : 0)
                      << "x (awake " << std::setprecision(0) << awake_sec << "s)" << std::endl;
        } else if (transitions >= 1 && roam_pct > 5 && dwell_pct > 5) {
            std::cout << "   [..] Weak bistability: " << transitions << " transitions"
                      << (speed_diff ? "" : " (speed ratio too small)") << std::endl;
        } else {
            std::cout << "   [!!] No clear state switching (roam=" << std::setprecision(0)
                      << roam_pct << "%, dwell=" << dwell_pct << "%)" << std::endl;
        }
    }

    // Step 96: SOCIAL/SOLITARY BEHAVIOR (NPR-1/RMG hub)
    // N2 (npr-1 gof): RMG suppressed → solitary (low RMG activity)
    // Hawaiian (npr-1 lf): RMG active → social aggregation (high RMG activity)
    // Diagnostic: report RMG voltage, hub connectivity effect, npr1 setting
    {
        auto& conn = sim.connectome();
        int rmgl = conn.get_neuron_id("RMGL");
        int rmgr = conn.get_neuron_id("RMGR");
        int nn = static_cast<int>(sim.neurons().size());
        std::cout << "\n34. SOCIAL/SOLITARY BEHAVIOR (Step 96, Macosko 2009):" << std::endl;
        std::cout << "   NPR-1 on RMG: " << std::setprecision(1) << sim.npr1_rmg()
                  << " pA (N2=-15, Hawaiian=0)" << std::endl;
        if (rmgl >= 0 && rmgl < nn && rmgr >= 0 && rmgr < nn) {
            double vl = sim.neurons()[rmgl]->get_membrane_potential();
            double vr = sim.neurons()[rmgr]->get_membrane_potential();
            double sl = 1.0 / (1.0 + std::exp(-(vl - (-35.0)) / 5.0));
            double sr = 1.0 / (1.0 + std::exp(-(vr - (-35.0)) / 5.0));
            std::cout << "   RMGL: V=" << std::setprecision(1) << vl << " mV, S="
                      << std::setprecision(3) << sl << std::endl;
            std::cout << "   RMGR: V=" << std::setprecision(1) << vr << " mV, S="
                      << std::setprecision(3) << sr << std::endl;
            double mean_s = (sl + sr) / 2.0;
            if (mean_s < 0.05) {
                std::cout << "   [OK] Solitary phenotype (RMG hub suppressed, N2-like)" << std::endl;
            } else if (mean_s > 0.15) {
                std::cout << "   [OK] Social phenotype (RMG hub active, Hawaiian-like)" << std::endl;
            } else {
                std::cout << "   [..] Intermediate RMG activity" << std::endl;
            }
        }
    }

    // Step 102-106: NEW NEURON DIAGNOSTICS
    {
        std::cout << "\n36. HEAD MOTOR & MISC NEURONS (Step 102-106):" << std::endl;
        // Head motor neurons (Step 102)
        std::cout << "   SIA head motor:" << std::endl;
        std::cout << "     SIADL: V mean=" << std::setprecision(1) << mean(siadl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(siadl_v_vs)) << std::endl;
        std::cout << "     SIBDL: V mean=" << std::setprecision(1) << mean(sibdl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(sibdl_v_vs)) << std::endl;
        // Turn circuit (Step 103)
        std::cout << "   SAA turn circuit:" << std::endl;
        std::cout << "     SAADL: V mean=" << std::setprecision(1) << mean(saadl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(saadl_v_vs)) << std::endl;
        std::cout << "     SMBDL: V mean=" << std::setprecision(1) << mean(smbdl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(smbdl_v_vs)) << std::endl;
        // Inner labial motor (Step 105)
        std::cout << "   URA inner labial motor:" << std::endl;
        std::cout << "     URADL: V mean=" << std::setprecision(1) << mean(uradl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(uradl_v_vs)) << std::endl;
        // Misc neurons (Step 106)
        std::cout << "   Misc neurons:" << std::endl;
        std::cout << "     PVM:   V mean=" << std::setprecision(1) << mean(pvm_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(pvm_v_vs)) << std::endl;
        std::cout << "     SDQR:  V mean=" << std::setprecision(1) << mean(sdqr_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(sdqr_v_vs)) << std::endl;
        std::cout << "     ALA:   V mean=" << std::setprecision(1) << mean(ala_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(ala_v_vs)) << std::endl;
        // Inner labial circuit (Step 107)
        std::cout << "   Inner labial circuit (Step 107):" << std::endl;
        std::cout << "     URBL:  V mean=" << std::setprecision(1) << mean(urbl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(urbl_v_vs)) << std::endl;
        std::cout << "     URYDL: V mean=" << std::setprecision(1) << mean(urydl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(urydl_v_vs)) << std::endl;
        // Tail-spike + body cavity (Step 108)
        std::cout << "   Tail-spike & body cavity (Step 108):" << std::endl;
        std::cout << "     ALNL:  V mean=" << std::setprecision(1) << mean(alnl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(alnl_v_vs)) << std::endl;
        std::cout << "     PLNL:  V mean=" << std::setprecision(1) << mean(plnl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(plnl_v_vs)) << std::endl;
        std::cout << "     BDUL:  V mean=" << std::setprecision(1) << mean(bdul_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(bdul_v_vs)) << std::endl;
        // Outer labial + phasmid + VNC pioneer (Step 109)
        std::cout << "   Outer labial + tail + VNC (Step 109):" << std::endl;
        std::cout << "     OLLL:  V mean=" << std::setprecision(1) << mean(olll_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(olll_v_vs)) << std::endl;
        std::cout << "     PHCL:  V mean=" << std::setprecision(1) << mean(phcl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(phcl_v_vs)) << std::endl;
        std::cout << "     AVG:   V mean=" << std::setprecision(1) << mean(avg_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(avg_v_vs)) << std::endl;
        // Head motor + dorsal MN (Step 110)
        std::cout << "   Head motor & RID (Step 110):" << std::endl;
        std::cout << "     RMHL:  V mean=" << std::setprecision(1) << mean(rmhl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rmhl_v_vs)) << std::endl;
        std::cout << "     RMFL:  V mean=" << std::setprecision(1) << mean(rmfl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rmfl_v_vs)) << std::endl;
        std::cout << "     RID:   V mean=" << std::setprecision(1) << mean(rid_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rid_v_vs)) << std::endl;
        // VNC + sexual regulation (Step 111)
        std::cout << "   VNC + sexual (Step 111):" << std::endl;
        std::cout << "     PVQL:  V mean=" << std::setprecision(1) << mean(pvql_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(pvql_v_vs)) << std::endl;
        std::cout << "     PVNL:  V mean=" << std::setprecision(1) << mean(pvnl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(pvnl_v_vs)) << std::endl;
        std::cout << "     AIML:  V mean=" << std::setprecision(1) << mean(aiml_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(aiml_v_vs)) << std::endl;
        // Vulval + sublateral motor (Step 112)
        std::cout << "   Vulval + sublateral (Step 112):" << std::endl;
        std::cout << "     VC1:   V mean=" << std::setprecision(1) << mean(vc1_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(vc1_v_vs)) << std::endl;
        std::cout << "     SABD:  V mean=" << std::setprecision(1) << mean(sabd_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(sabd_v_vs)) << std::endl;
        // Amphid + ring inter (Step 113)
        std::cout << "   Amphid + ring (Step 113):" << std::endl;
        std::cout << "     ASGL:  V mean=" << std::setprecision(1) << mean(asgl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(asgl_v_vs)) << std::endl;
        std::cout << "     ADAL:  V mean=" << std::setprecision(1) << mean(adal_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(adal_v_vs)) << std::endl;
        std::cout << "     RIFL:  V mean=" << std::setprecision(1) << mean(rifl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rifl_v_vs)) << std::endl;
        std::cout << "     RIR:   V mean=" << std::setprecision(1) << mean(rir_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rir_v_vs)) << std::endl;
        // Batch fill (Step 114)
        std::cout << "   Batch fill (Step 114):" << std::endl;
        std::cout << "     IL1L:  V mean=" << std::setprecision(1) << mean(il1l_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(il1l_v_vs)) << std::endl;
        std::cout << "     SDQL:  V mean=" << std::setprecision(1) << mean(sdql_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(sdql_v_vs)) << std::endl;
        std::cout << "     RMEL:  V mean=" << std::setprecision(1) << mean(rmel_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rmel_v_vs)) << std::endl;
        std::cout << "     PDA:   V mean=" << std::setprecision(1) << mean(pda_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(pda_v_vs)) << std::endl;
        std::cout << "     PVWL:  V mean=" << std::setprecision(1) << mean(pvwl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(pvwl_v_vs)) << std::endl;
        // Pharyngeal fill (Step 115)
        std::cout << "   Pharyngeal (Step 115):" << std::endl;
        std::cout << "     I2L:   V mean=" << std::setprecision(1) << mean(i2l_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(i2l_v_vs)) << std::endl;
        std::cout << "     M1:    V mean=" << std::setprecision(1) << mean(m1_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(m1_v_vs)) << std::endl;
        // Final 302 (Step 116)
        std::cout << "   Final 302 (Step 116):" << std::endl;
        std::cout << "     RIGL:  V mean=" << std::setprecision(1) << mean(rigl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rigl_v_vs)) << std::endl;
        std::cout << "     RMDL:  V mean=" << std::setprecision(1) << mean(rmdl_v_vs)
                  << " mV  S(release)=" << std::setprecision(3) << release(mean(rmdl_v_vs)) << std::endl;
        // Check for dead neurons (resting at -65mV with no input)
        auto check_alive = [](const std::string& name, double v_mean) {
            if (v_mean < -60.0) {
                std::cout << "   [!!] " << name << " may be DEAD (V=" << std::setprecision(1)
                          << v_mean << " mV, near resting)" << std::endl;
                return false;
            }
            return true;
        };
        bool all_alive = true;
        all_alive &= check_alive("SIADL", mean(siadl_v_vs));
        all_alive &= check_alive("SIBDL", mean(sibdl_v_vs));
        all_alive &= check_alive("SAADL", mean(saadl_v_vs));
        all_alive &= check_alive("URADL", mean(uradl_v_vs));
        all_alive &= check_alive("PVM", mean(pvm_v_vs));
        all_alive &= check_alive("SDQR", mean(sdqr_v_vs));
        all_alive &= check_alive("ALA", mean(ala_v_vs));
        all_alive &= check_alive("URBL", mean(urbl_v_vs));
        all_alive &= check_alive("URYDL", mean(urydl_v_vs));
        all_alive &= check_alive("ALNL", mean(alnl_v_vs));
        all_alive &= check_alive("PLNL", mean(plnl_v_vs));
        all_alive &= check_alive("BDUL", mean(bdul_v_vs));
        all_alive &= check_alive("OLLL", mean(olll_v_vs));
        all_alive &= check_alive("PHCL", mean(phcl_v_vs));
        all_alive &= check_alive("AVG", mean(avg_v_vs));
        all_alive &= check_alive("RMHL", mean(rmhl_v_vs));
        all_alive &= check_alive("RMFL", mean(rmfl_v_vs));
        all_alive &= check_alive("RID", mean(rid_v_vs));
        all_alive &= check_alive("PVQL", mean(pvql_v_vs));
        all_alive &= check_alive("PVNL", mean(pvnl_v_vs));
        all_alive &= check_alive("AIML", mean(aiml_v_vs));
        all_alive &= check_alive("VC1", mean(vc1_v_vs));
        all_alive &= check_alive("SABD", mean(sabd_v_vs));
        all_alive &= check_alive("ASGL", mean(asgl_v_vs));
        all_alive &= check_alive("ADAL", mean(adal_v_vs));
        all_alive &= check_alive("RIFL", mean(rifl_v_vs));
        all_alive &= check_alive("RIR", mean(rir_v_vs));
        all_alive &= check_alive("IL1L", mean(il1l_v_vs));
        all_alive &= check_alive("SDQL", mean(sdql_v_vs));
        all_alive &= check_alive("RMEL", mean(rmel_v_vs));
        all_alive &= check_alive("PDA", mean(pda_v_vs));
        all_alive &= check_alive("PVWL", mean(pvwl_v_vs));
        all_alive &= check_alive("I2L", mean(i2l_v_vs));
        all_alive &= check_alive("M1", mean(m1_v_vs));
        all_alive &= check_alive("RIGL", mean(rigl_v_vs));
        all_alive &= check_alive("RMDL", mean(rmdl_v_vs));
        if (all_alive) {
            std::cout << "   [OK] All new neurons receiving input (V > -60 mV)" << std::endl;
        }
    }

    // BOTTLENECK ANALYSIS
    std::cout << "\n========================================" << std::endl;
    std::cout << "  BOTTLENECK ANALYSIS" << std::endl;
    std::cout << "========================================\n" << std::endl;

    bool has_bottleneck = false;

    if (mean(grad_mags) < 0.005) {
        std::cout << "  [!!] GRADIENT too small (" << mean(grad_mags) << " /mm)" << std::endl;
        std::cout << "       -> Worm may be too far from food, or sigma too large" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Gradient magnitude adequate" << std::endl;
    }

    if (absmax(biases) < 0.5) {
        std::cout << "  [!!] BIAS CURRENT too weak (max " << absmax(biases) << " pA)" << std::endl;
        std::cout << "       -> Increase weathervane_gain (try 200-500)" << std::endl;
        has_bottleneck = true;
    } else if (absmax(biases) >= sim.params.bias_clamp * 0.9) {
        std::cout << "  [!!] BIAS CURRENT hitting clamp (" << sim.params.bias_clamp << " pA)" << std::endl;
        std::cout << "       -> Increase bias_clamp (try 20-50)" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Bias current adequate" << std::endl;
    }

    double smd_amp = sd_max - sd_min;
    if (smd_amp < 2.0) {
        std::cout << "  [!!] SMD DIFFERENTIAL too small (" << smd_amp << " mV)" << std::endl;
        std::cout << "       -> Increase synapse_scale or weathervane_gain" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] SMD differential " << smd_amp << " mV" << std::endl;
    }

    double curv_amp = c_max - c_min;
    if (curv_amp < 0.04) {  // Step 103: 0.1→0.04 (12 new head MN mappings → D/V balance ↑ → lower net curvature)
        std::cout << "  [!!] CURVATURE too small (" << curv_amp << " /mm)" << std::endl;
        std::cout << "       -> Check muscle_gain in body_model or synapse_scale" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Curvature amplitude " << curv_amp << " /mm" << std::endl;
    }

    if (mean(speeds) < 0.1) {
        std::cout << "  [!!] SPEED too slow (" << mean(speeds) << " mm/s)" << std::endl;
        std::cout << "       -> Increase speed_scale" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Speed " << mean(speeds) << " mm/s" << std::endl;
    }

    if (avg_rate < 1.0) {
        std::cout << "  [!!] HEADING RATE too low (" << avg_rate << " deg/s, need 1-5)" << std::endl;
        std::cout << "       -> Product of speed * curvature too small" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Heading rate " << avg_rate << " deg/s" << std::endl;
    }

    // CI interpretation depends on whether food is toxic (Step 26 pathogen learning)
    // Toxic food: CI < 0 = CORRECT (worm learned to avoid)
    // Safe food:  CI > 0.5 = expected chemotaxis
    double sickness_final = sim.sickness();
    if (sickness_final > 0.5) {
        // Pathogen learning active — low/negative CI is expected
        if (ci < 0.0) {
            std::cout << "  [OK] CI=" << ci << " (pathogen avoidance learned, sickness=" 
                      << std::setprecision(2) << sickness_final << ")" << std::endl;
        } else {
            std::cout << "  [..] CI=" << ci << " (expected negative with sickness="
                      << std::setprecision(2) << sickness_final << ")" << std::endl;
        }
    } else {
        if (ci < 0.3) {
            std::cout << "  [!!] CI poor (" << ci << ", target >0.5)" << std::endl;
            has_bottleneck = true;
        } else if (ci >= 0.5) {
            std::cout << "  [OK] CI good (" << ci << " >= 0.5)" << std::endl;
        } else {
            std::cout << "  [..] CI moderate (" << ci << ", target >0.5)" << std::endl;
        }
    }

    // Step 29: Wave propagation checks
    {
        double sign_hz = curv7_sign_changes / (duration * 0.001);
        if (sign_hz > 50.0) {
            std::cout << "  [!!] CURVATURE UNSTABLE (seg7 sign-change " << sign_hz << " Hz)" << std::endl;
            std::cout << "       -> Check body_model curvature integrator (semi-implicit Euler?)" << std::endl;
            has_bottleneck = true;
        } else {
            std::cout << "  [OK] Curvature stability " << std::setprecision(1) << sign_hz << " Hz" << std::endl;
        }
        if (mean(muscle_work_vs) < 0.1) {
            std::cout << "  [!!] MUSCLE WORK too low (" << std::setprecision(3) << mean(muscle_work_vs) << ")" << std::endl;
            std::cout << "       -> D/V cancellation? Check MEC channels and curvature stability" << std::endl;
            has_bottleneck = true;
        } else {
            std::cout << "  [OK] Muscle work " << std::setprecision(3) << mean(muscle_work_vs) << std::endl;
        }
    }

    if (!has_bottleneck) {
        std::cout << "\n  All stages look healthy!" << std::endl;
    }

    std::cout << std::endl;
    return 0;
}
