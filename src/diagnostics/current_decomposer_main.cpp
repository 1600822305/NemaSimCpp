// current_decomposer_main.cpp — 突触电流归因分解器
// 对指定神经元分解全部输入电流来源：connectome 突触、gap junction、模块注入。
// 支持事件触发快照和周期性采样两种模式。
//
// Usage:
//   current_decomposer --neurons AVAL,AIBL,RIVL --duration 30
//   current_decomposer --neurons AVAL --at-event omega    # omega 启动时快照
//   current_decomposer --neurons AVAL --continuous 500    # 每500ms采样
//   current_decomposer --neurons all-cmd                  # AVA+AVB+AVD+PVC 全部命令神经元
#include "simulation/simulation_engine.h"
#include "neuron/multi_compartment.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <map>

using namespace celegans;

// ================================================================
// Per-neuron current budget at one moment
// ================================================================
struct CurrentBudget {
    double time_s;
    std::string neuron_name;
    int neuron_id;
    double V_membrane;
    double release_rate;
    double total_I_syn;   // total synaptic current on the neuron

    // Connectome-mediated currents (from trace_inputs)
    struct Source {
        std::string name;
        std::string type;     // chem_exc, chem_inh, gap
        double current_pA;
    };
    std::vector<Source> connectome_sources;
    double connectome_total;

    // Module-injected currents (computed from state)
    struct ModuleInjection {
        std::string module_name;
        double current_pA;
    };
    std::vector<ModuleInjection> module_injections;
    double module_total;

    double unaccounted;   // total_I_syn - connectome_total - module_total
};

// ================================================================
// Known module injection formulas
// These mirror the injection logic in the simulation code.
// ================================================================
static std::vector<CurrentBudget::ModuleInjection> compute_module_injections(
    const std::string& neuron_name, const SimulationEngine& sim) {

    std::vector<CurrentBudget::ModuleInjection> inj;

    double satiety = sim.satiety();
    double sickness = sim.sickness();
    double food_memory = sim.food_memory();
    double serotonin = sim.neuromodulation().get_concentration("5-HT");
    double dCdt = sim.dCdt_filtered();

    // --- RIC baseline + satiety ---
    if (neuron_name == "RICL" || neuron_name == "RICR") {
        double ric_baseline = 15.0;
        double ric_satiety = 10.0 * satiety;
        inj.push_back({"RIC_baseline", ric_baseline});
        if (std::abs(ric_satiety) > 0.001)
            inj.push_back({"RIC_satiety", ric_satiety});
    }

    // --- ESR (enhanced slowing): MOD-1 on AIY, PVC, RIC ---
    if (neuron_name.substr(0, 3) == "AIY" || neuron_name.substr(0, 3) == "PVC") {
        double food_sensory = sim.environment().sample_chemical(sim.body().get_head_position());
        bool on_food = food_sensory > 0.1;
        if (on_food && serotonin > 0.1) {
            double esr_current = -8.0 * serotonin;
            inj.push_back({"ESR_MOD1", esr_current});
        }
    }
    if (neuron_name == "RICL" || neuron_name == "RICR") {
        double food_sensory = sim.environment().sample_chemical(sim.body().get_head_position());
        bool on_food = food_sensory > 0.1;
        if (on_food && serotonin > 0.1) {
            double esr_current = -8.0 * serotonin * 0.5;
            inj.push_back({"ESR_SER4", esr_current});
        }
    }

    // --- NPR-1 tonic inhibition on AUA and RMG ---
    if (neuron_name.substr(0, 3) == "AUA") {
        inj.push_back({"NPR1_tonic", -15.0});
    }
    if (neuron_name.substr(0, 3) == "RMG") {
        inj.push_back({"NPR1_tonic", -20.0});
    }

    // --- ARS (area-restricted search): food_memory → AVA, DVA ---
    if (neuron_name == "AVAL") {
        double ars_current = 4.0 * food_memory;
        if (std::abs(ars_current) > 0.001)
            inj.push_back({"ARS_food_mem", ars_current});
    }
    if (neuron_name == "DVA" || neuron_name == "DVAL" || neuron_name == "DVAR") {
        double ars_dva = 5.0 * food_memory;
        if (std::abs(ars_dva) > 0.001)
            inj.push_back({"ARS_DVA", ars_dva});
    }

    // --- Klinokinesis dC/dt → AVA ---
    if (neuron_name == "AVAL" || neuron_name == "AVAR") {
        // Reconstruct klinokinesis injection from dCdt_filtered
        double pref = sim.awc_pref_cached();
        double kk_dCdt_gain = 300.0;
        double kk_dCdt_current = 0.0;
        if (pref >= 0.0) {
            if (dCdt < 0.0) {
                kk_dCdt_current = -dCdt * kk_dCdt_gain;
                kk_dCdt_current = std::min(kk_dCdt_current, 3.0);
            }
        } else {
            if (dCdt > 0.0) {
                kk_dCdt_current = dCdt * kk_dCdt_gain;
                kk_dCdt_current = std::min(kk_dCdt_current, 3.0);
            }
        }
        if (std::abs(kk_dCdt_current) > 0.001)
            inj.push_back({"klinokinesis_dCdt", kk_dCdt_current});

        // Gradient magnitude component (harder to reconstruct exactly without
        // knowing the internal kk_mag_current, but we can approximate)
        auto hp = sim.body().get_head_position();
        auto grad = sim.environment().chemical_field().gradient(hp);
        double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);
        double kk_mag = -15.0 * grad_mag;  // approximate
        if (std::abs(kk_mag) > 0.01)
            inj.push_back({"klinokinesis_mag", kk_mag});
    }

    // --- MOD-1 sickness → AIY, AIZ ---
    if (neuron_name.substr(0, 3) == "AIY") {
        double I_mod1 = -12.0 * sickness;
        if (std::abs(I_mod1) > 0.001)
            inj.push_back({"sickness_MOD1", I_mod1});
    }
    if (neuron_name.substr(0, 3) == "AIZ") {
        double I_mod1_aiz = -8.0 * sickness;
        if (std::abs(I_mod1_aiz) > 0.001)
            inj.push_back({"sickness_MOD1_AIZ", I_mod1_aiz});
    }

    return inj;
}

// ================================================================
// Capture current budget for one neuron
// ================================================================
static CurrentBudget capture_budget(const SimulationEngine& sim, int nid, const std::string& name) {
    CurrentBudget b;
    b.time_s = sim.current_time() / 1000.0;
    b.neuron_name = name;
    b.neuron_id = nid;

    const auto& neurons = sim.neurons();
    b.V_membrane = neurons[nid]->get_membrane_potential();
    b.release_rate = neurons[nid]->get_transmitter_release_rate();
    b.total_I_syn = neurons[nid]->get_I_syn();

    // Connectome-mediated currents
    auto sources = sim.connectome().trace_inputs(nid, neurons);
    b.connectome_total = 0;
    for (const auto& s : sources) {
        b.connectome_sources.push_back({s.source_name, s.type, s.current_pA});
        b.connectome_total += s.current_pA;
    }

    // Module-injected currents
    b.module_injections = compute_module_injections(name, sim);
    b.module_total = 0;
    for (const auto& m : b.module_injections) {
        b.module_total += m.current_pA;
    }

    b.unaccounted = b.total_I_syn - b.connectome_total - b.module_total;

    return b;
}

// ================================================================
// Print budget
// ================================================================
static void print_budget(const CurrentBudget& b) {
    std::cout << "\n+--------------------------------------------------------------+\n";
    std::cout << "|  " << b.neuron_name << " (id=" << b.neuron_id
              << ")  t=" << std::fixed << std::setprecision(2) << b.time_s << "s\n";
    std::cout << "|  V=" << std::setprecision(1) << b.V_membrane
              << " mV  rel=" << std::setprecision(4) << b.release_rate
              << "  I_total=" << std::setprecision(2) << b.total_I_syn << " pA\n";
    std::cout << "+--------------------------------------------------------------+\n";

    double abs_total = std::abs(b.total_I_syn);
    auto pct = [&](double v) -> double {
        return (abs_total > 0.01) ? 100.0 * v / abs_total : 0.0;
    };

    // Connectome sources
    std::cout << "|  --- Connectome Synaptic Inputs ---\n";
    int rank = 1;
    for (const auto& s : b.connectome_sources) {
        if (rank > 15) { std::cout << "|    ... (" << (b.connectome_sources.size() - 15) << " more)\n"; break; }
        std::cout << "|  #" << std::setw(2) << rank++
                  << "  " << std::left << std::setw(8) << s.name
                  << " -> " << std::setw(8) << b.neuron_name
                  << std::right << std::setw(8) << std::setprecision(2) << s.current_pA << " pA"
                  << "  (" << std::setw(6) << std::setprecision(1) << pct(s.current_pA) << "%)"
                  << "  " << s.type << "\n";
    }
    std::cout << "|  subtotal: " << std::setprecision(2) << b.connectome_total << " pA\n";

    // Module injections
    if (!b.module_injections.empty()) {
        std::cout << "|  --- Module Injections ---\n";
        for (const auto& m : b.module_injections) {
            std::cout << "|  " << std::left << std::setw(22) << m.module_name
                      << std::right << std::setw(8) << std::setprecision(2) << m.current_pA << " pA"
                      << "  (" << std::setw(6) << std::setprecision(1) << pct(m.current_pA) << "%)\n";
        }
        std::cout << "|  subtotal: " << std::setprecision(2) << b.module_total << " pA\n";
    }

    // Unaccounted
    if (std::abs(b.unaccounted) > 0.1) {
        std::cout << "|  --- Unaccounted ---\n";
        std::cout << "|  other/sensory/baseline: " << std::setprecision(2) << b.unaccounted << " pA"
                  << "  (" << std::setprecision(1) << pct(b.unaccounted) << "%)\n";
    }

    std::cout << "+--------------------------------------------------------------+\n";
}

// ================================================================
// Aggregate statistics across time
// ================================================================
struct NeuronStats {
    std::string name;
    int count = 0;
    double I_total_sum = 0, I_total_sq = 0;
    std::map<std::string, double> source_sum;   // source_name -> sum of |current|
    std::map<std::string, int> source_count;
};

static void print_aggregate(const std::map<std::string, NeuronStats>& stats) {
    std::cout << "\n========================================\n";
    std::cout << "  AGGREGATE CURRENT BUDGET\n";
    std::cout << "========================================\n";

    for (const auto& [name, st] : stats) {
        if (st.count == 0) continue;
        double mean = st.I_total_sum / st.count;
        double var = st.I_total_sq / st.count - mean * mean;
        double sd = (var > 0) ? std::sqrt(var) : 0;

        std::cout << "\n  " << name << "  (n=" << st.count << " samples)\n";
        std::cout << "  mean I_total: " << std::fixed << std::setprecision(2) << mean
                  << " +/- " << sd << " pA\n";

        // Rank sources by mean |current|
        std::vector<std::pair<std::string, double>> ranked;
        for (const auto& [src, sum] : st.source_sum) {
            int n = st.source_count.at(src);
            ranked.push_back({src, sum / n});
        }
        std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return std::abs(a.second) > std::abs(b.second); });

        std::cout << "  Top sources (mean |I|):\n";
        int r = 1;
        for (const auto& [src, mean_I] : ranked) {
            if (r > 10) break;
            std::cout << "    #" << r++ << "  " << std::left << std::setw(22) << src
                      << std::right << std::setw(8) << std::setprecision(2) << mean_I << " pA\n";
        }
    }
}

// ================================================================
// Parse neuron name list
// ================================================================
static std::vector<std::string> parse_neuron_list(const std::string& input) {
    std::vector<std::string> result;
    if (input == "all-cmd") {
        return {"AVAL", "AVAR", "AVBL", "AVBR", "AVDL", "AVDR", "PVCL", "PVCR"};
    }
    if (input == "all-sensory") {
        return {"AWCL", "AWCR", "ASEL", "ASER", "AWAL", "AWAR"};
    }
    if (input == "all-inter") {
        return {"AIAL", "AIAR", "AIBL", "AIBR", "AIYL", "AIYR", "AIZL", "AIZR"};
    }
    if (input == "all-riv") {
        return {"RIVL", "RIVR"};
    }
    std::stringstream ss(input);
    std::string token;
    while (std::getline(ss, token, ',')) {
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::ERROR);

    double duration_s = 30.0;
    int seed = 42;
    std::string neuron_str = "AVAL,AVAR,AIBL,RIVL";
    std::string at_event = "";     // "omega", "reversal", "" (continuous/final)
    int continuous_ms = 0;         // 0 = snapshot at end only
    double snapshot_time_s = -1;   // specific time to snapshot (-1 = end)
    bool aggregate_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--neurons" || arg == "-n") && i + 1 < argc) neuron_str = argv[++i];
        else if (arg == "--duration" && i + 1 < argc) duration_s = std::atof(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::atoi(argv[++i]);
        else if (arg == "--at-event" && i + 1 < argc) at_event = argv[++i];
        else if (arg == "--continuous" && i + 1 < argc) continuous_ms = std::atoi(argv[++i]);
        else if (arg == "--at-time" && i + 1 < argc) snapshot_time_s = std::atof(argv[++i]);
        else if (arg == "--aggregate") aggregate_only = true;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: current_decomposer [options]\n\n"
                      << "Decomposes input currents for target neurons by source.\n\n"
                      << "Options:\n"
                      << "  --neurons/-n <list>   Neuron names (comma-separated or preset)\n"
                      << "                        Presets: all-cmd, all-sensory, all-inter, all-riv\n"
                      << "  --duration <sec>      Simulation duration (default: 30)\n"
                      << "  --seed <n>            Random seed (default: 42)\n"
                      << "  --at-event <type>     Snapshot at omega|reversal events\n"
                      << "  --at-time <sec>       Snapshot at specific time\n"
                      << "  --continuous <ms>     Periodic sampling interval\n"
                      << "  --aggregate           Only show aggregate statistics\n"
                      << "  --help / -h           Show this help\n";
            return 0;
        }
    }

    auto target_names = parse_neuron_list(neuron_str);

    std::cout << "========================================\n";
    std::cout << "  Current Decomposer\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration:  " << duration_s << " s\n";
    std::cout << "  Seed:      " << seed << "\n";
    std::cout << "  Neurons:   ";
    for (size_t i = 0; i < target_names.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << target_names[i];
    }
    std::cout << "\n";
    if (!at_event.empty()) std::cout << "  At event:  " << at_event << "\n";
    if (continuous_ms > 0) std::cout << "  Continuous: " << continuous_ms << " ms\n";
    std::cout << "\n  Running simulation... " << std::flush;

    // --- Initialize simulation ---
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    // Resolve neuron IDs
    std::vector<int> target_ids;
    for (const auto& name : target_names) {
        int id = sim.connectome().get_neuron_id(name);
        if (id < 0) {
            std::cerr << "Warning: neuron '" << name << "' not found\n";
        }
        target_ids.push_back(id);
    }

    double duration_ms = duration_s * 1000.0;
    int total_steps = static_cast<int>(duration_ms / sim.dt());
    int sample_interval = static_cast<int>(50.0 / sim.dt());  // 50ms
    double warmup_ms = 3000.0;

    bool prev_rev = false, prev_omega = false;
    int continuous_interval = (continuous_ms > 0)
        ? static_cast<int>(continuous_ms / sim.dt()) : 0;

    std::map<std::string, NeuronStats> agg_stats;
    for (const auto& name : target_names) {
        agg_stats[name].name = name;
    }

    auto do_snapshot = [&](const std::string& reason) {
        for (size_t i = 0; i < target_names.size(); ++i) {
            if (target_ids[i] < 0) continue;
            auto budget = capture_budget(sim, target_ids[i], target_names[i]);
            if (!aggregate_only) {
                print_budget(budget);
            }
            // Aggregate
            auto& st = agg_stats[target_names[i]];
            st.count++;
            st.I_total_sum += budget.total_I_syn;
            st.I_total_sq += budget.total_I_syn * budget.total_I_syn;
            for (const auto& s : budget.connectome_sources) {
                st.source_sum[s.name + " (" + s.type + ")"] += std::abs(s.current_pA);
                st.source_count[s.name + " (" + s.type + ")"]++;
            }
            for (const auto& m : budget.module_injections) {
                st.source_sum[m.module_name] += std::abs(m.current_pA);
                st.source_count[m.module_name]++;
            }
            if (std::abs(budget.unaccounted) > 0.1) {
                st.source_sum["[unaccounted]"] += std::abs(budget.unaccounted);
                st.source_count["[unaccounted]"]++;
            }
        }
    };

    for (int s = 0; s < total_steps; ++s) {
        sim.step();

        if (s % sample_interval != 0) continue;

        if (sim.current_time() < warmup_ms) {
            prev_rev = sim.is_reversing();
            prev_omega = sim.is_omega_turning();
            continue;
        }

        bool curr_rev = sim.is_reversing();
        bool curr_omega = sim.is_omega_turning();

        // Event-triggered snapshots
        if (at_event == "omega" && curr_omega && !prev_omega) {
            do_snapshot("OMEGA_START");
        }
        if (at_event == "reversal" && curr_rev && !prev_rev) {
            do_snapshot("REV_START");
        }

        // Continuous snapshots
        if (continuous_interval > 0) {
            int steps_since_warmup = s - static_cast<int>(warmup_ms / sim.dt());
            if (steps_since_warmup > 0 && steps_since_warmup % continuous_interval == 0) {
                do_snapshot("PERIODIC");
            }
        }

        // Specific time snapshot
        if (snapshot_time_s > 0) {
            double t = sim.current_time() / 1000.0;
            if (std::abs(t - snapshot_time_s) < 0.03) {
                do_snapshot("AT_TIME");
                snapshot_time_s = -1;  // only once
            }
        }

        prev_rev = curr_rev;
        prev_omega = curr_omega;
    }

    // End-of-sim snapshot if no event/continuous mode
    if (at_event.empty() && continuous_ms == 0 && snapshot_time_s < 0) {
        do_snapshot("END");
    }

    std::cout << "Done!\n";

    // Print aggregate
    print_aggregate(agg_stats);

    return 0;
}
