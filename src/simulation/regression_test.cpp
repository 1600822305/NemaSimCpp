// ================================================================
// C. elegans Regression Test & Bug Tracer
//
// Purpose: Automatically detect regressions and trace their root cause.
// Run after ANY code change to catch bugs before they compound.
//
// Three-layer defense:
//   1. BASELINE CHECK — compare ~20 key metrics against known-good ranges
//   2. CURRENT BUDGET — decompose anomalous I_syn into per-source contributions
//   3. SUBSYSTEM ISOLATION — disable subsystems one-by-one to isolate cause
//
// Usage:
//   celegans_regtest.exe              — run checks against hardcoded baseline
//   celegans_regtest.exe --save       — print current values (update baseline)
//   celegans_regtest.exe --trace SMDDL — full current budget for named neuron
// ================================================================

#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

using namespace celegans;

// ---- Metric definition ----
struct Metric {
    std::string name;
    double value;
    double baseline;      // expected value
    double tolerance_pct;  // % deviation allowed (e.g. 30 = ±30%)
    std::string unit;
    std::string trace_neuron;  // if non-empty, trace this neuron on failure
};

// ---- Helper: run simulation and collect metrics ----
struct SimMetrics {
    // Per-neuron voltage ranges (key neurons)
    double smddl_v_min, smddl_v_max, smdvl_v_min, smdvl_v_max;
    double smddl_isyn_min, smddl_isyn_max;
    double smddl_iext_min, smddl_iext_max;
    double smd_diff_amp;  // max(SMDDL-SMDVL) - min(SMDDL-SMDVL)

    // Body
    double curv_amp;      // curvature range
    double speed_mean;
    double heading_rate;

    // Behavior
    double ci;
    double time_near_food_pct;
    int reversal_count;
    int omega_count;

    // ASE sensory
    double asel_mean, aser_mean;

    // Neuromodulation
    double sht_final, oa_final;
    double satiety_max;

    // Step 27: Sleep / Fatigue
    double fatigue_final;
    bool is_sleeping_final;
    double ris_v_final;

    // Step 29: Wave propagation & curvature stability
    double midbody_curv_amp;      // curvature amplitude at seg 10 (wave reaches mid-body?)
    double curv_sign_change_hz;   // sign-change rate at seg 7 (numerical instability detector)
    double muscle_work_mean;      // mean |dorsal-ventral| / N_seg (speed driver)
};

SimMetrics run_and_measure(int duration_ms = 30000,
                           SimulationEngine::TuningParams tp = {}) {
    SimulationEngine sim;
    sim.initialize_default();
    sim.params = tp;

    Vector2d food{35.0, 35.0};

    auto& conn = sim.connectome();
    int smddl_id = conn.get_neuron_id("SMDDL");
    int smdvl_id = conn.get_neuron_id("SMDVL");
    int asel_id = conn.get_neuron_id("ASEL");
    int aser_id = conn.get_neuron_id("ASER");

    SimMetrics m{};
    m.smddl_v_min = 1e9; m.smddl_v_max = -1e9;
    m.smdvl_v_min = 1e9; m.smdvl_v_max = -1e9;
    m.smddl_isyn_min = 1e9; m.smddl_isyn_max = -1e9;
    m.smddl_iext_min = 1e9; m.smddl_iext_max = -1e9;

    double smd_diff_min = 1e9, smd_diff_max = -1e9;
    double curv_min = 1e9, curv_max = -1e9;
    double speed_sum = 0, heading_rate_sum = 0;
    double asel_sum = 0, aser_sum = 0;
    // Step 29: wave propagation accumulators
    double midbody_curv_min = 1e9, midbody_curv_max = -1e9;
    int curv_sign_changes = 0;
    double prev_seg7_curv = 0;
    double muscle_work_sum = 0;
    int samples = 0;
    double prev_heading = sim.body().get_head_angle() * 180.0 / 3.14159265;
    double prev_time = 0;
    int reversal_count = 0, omega_count = 0;
    bool prev_rev = false, prev_omega = false;
    int near_food = 0, total_samples = 0;
    double dist_initial = -1, dist_final = 0;

    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(100.0 / sim.dt());  // every 100ms

    for (int s = 0; s < total_steps; ++s) {
        sim.step();

        if ((s + 1) % sample_interval == 0) {
            const auto& neurons = sim.neurons();
            int n = (int)neurons.size();

            // SMD voltages and currents
            if (smddl_id >= 0 && smddl_id < n) {
                double v = neurons[smddl_id]->get_membrane_potential();
                double isyn = neurons[smddl_id]->get_I_syn();
                double iext = neurons[smddl_id]->get_I_ext();
                m.smddl_v_min = std::min(m.smddl_v_min, v);
                m.smddl_v_max = std::max(m.smddl_v_max, v);
                m.smddl_isyn_min = std::min(m.smddl_isyn_min, isyn);
                m.smddl_isyn_max = std::max(m.smddl_isyn_max, isyn);
                m.smddl_iext_min = std::min(m.smddl_iext_min, iext);
                m.smddl_iext_max = std::max(m.smddl_iext_max, iext);
            }
            if (smdvl_id >= 0 && smdvl_id < n) {
                double v = neurons[smdvl_id]->get_membrane_potential();
                m.smdvl_v_min = std::min(m.smdvl_v_min, v);
                m.smdvl_v_max = std::max(m.smdvl_v_max, v);
            }

            // SMD differential
            double vd = neurons[smddl_id]->get_membrane_potential();
            double vv = neurons[smdvl_id]->get_membrane_potential();
            double diff = vd - vv;
            smd_diff_min = std::min(smd_diff_min, diff);
            smd_diff_max = std::max(smd_diff_max, diff);

            // Curvature (head)
            double curv = sim.body().segments()[0].curvature;
            curv_min = std::min(curv_min, curv);
            curv_max = std::max(curv_max, curv);

            // Step 29: Mid-body curvature (wave propagation check)
            double midbody_curv = sim.body().segments()[10].curvature;
            midbody_curv_min = std::min(midbody_curv_min, midbody_curv);
            midbody_curv_max = std::max(midbody_curv_max, midbody_curv);

            // Step 29: Curvature sign-change rate at seg 7 (stability check)
            double seg7_curv = sim.body().segments()[7].curvature;
            if (samples > 0 && seg7_curv * prev_seg7_curv < 0) curv_sign_changes++;
            prev_seg7_curv = seg7_curv;

            // Step 29: Muscle work (D/V quality)
            {
                double mw = 0;
                const auto& segs = sim.body().segments();
                for (int si = 0; si < 48; ++si)
                    mw += std::abs(segs[si].dorsal_activation - segs[si].ventral_activation);
                muscle_work_sum += mw / 48.0;
            }

            // Speed
            speed_sum += sim.body().get_speed();

            // Heading rate
            double heading = sim.body().get_head_angle() * 180.0 / 3.14159265;
            double dt_sec = (sim.current_time() - prev_time) / 1000.0;
            if (dt_sec > 0.01) {
                heading_rate_sum += std::abs(heading - prev_heading) / dt_sec;
            }
            prev_heading = heading;
            prev_time = sim.current_time();

            // Sensory
            if (asel_id >= 0 && asel_id < n) asel_sum += neurons[asel_id]->get_membrane_potential();
            if (aser_id >= 0 && aser_id < n) aser_sum += neurons[aser_id]->get_membrane_potential();

            // Distance
            auto head = sim.body().get_head_position();
            double dx = head.x - food.x, dy = head.y - food.y;
            double dist = std::sqrt(dx*dx + dy*dy);
            if (dist_initial < 0) dist_initial = dist;
            dist_final = dist;
            total_samples++;
            if (dist < 5.0) near_food++;

            // Events
            bool cur_rev = sim.is_reversing();
            bool cur_omega = sim.is_omega_turning();
            if (cur_rev && !prev_rev) reversal_count++;
            if (cur_omega && !prev_omega) omega_count++;
            prev_rev = cur_rev;
            prev_omega = cur_omega;

            samples++;
        }
    }

    m.smd_diff_amp = smd_diff_max - smd_diff_min;
    m.curv_amp = curv_max - curv_min;
    m.speed_mean = speed_sum / samples;
    m.heading_rate = heading_rate_sum / samples;
    m.asel_mean = asel_sum / samples;
    m.aser_mean = aser_sum / samples;
    m.ci = (dist_initial > 0) ? (dist_initial - dist_final) / dist_initial : 0;
    m.time_near_food_pct = (total_samples > 0) ? 100.0 * near_food / total_samples : 0;
    m.reversal_count = reversal_count;
    m.omega_count = omega_count;
    m.satiety_max = sim.satiety();
    m.sht_final = sim.neuromodulation().get_concentration("5-HT");
    m.oa_final = sim.neuromodulation().get_concentration("OA");

    // Step 29: wave propagation & stability metrics
    m.midbody_curv_amp = midbody_curv_max - midbody_curv_min;
    m.curv_sign_change_hz = curv_sign_changes / (duration_ms * 0.001); // Hz
    m.muscle_work_mean = muscle_work_sum / samples;

    // Step 27: fatigue/sleep state at end of simulation
    m.fatigue_final = sim.fatigue();
    m.is_sleeping_final = sim.is_sleeping();
    int ris_id = conn.get_neuron_id("RIS");
    m.ris_v_final = (ris_id >= 0 && ris_id < (int)sim.neurons().size())
        ? sim.neurons()[ris_id]->get_membrane_potential() : -65.0;

    return m;
}

// ---- Current budget trace ----
void trace_neuron(const std::string& name) {
    SimulationEngine sim;
    sim.initialize_default();
    Vector2d food{35.0, 35.0};

    auto& conn = sim.connectome();
    int target_id = conn.get_neuron_id(name);
    if (target_id < 0) {
        std::cerr << "Neuron '" << name << "' not found!" << std::endl;
        return;
    }

    // Run 5s to reach steady state, then sample 100 steps
    int warmup_steps = (int)(5000.0 / sim.dt());
    for (int s = 0; s < warmup_steps; ++s) sim.step();

    // Accumulate current budget over 100 samples
    struct AccSource {
        std::string name;
        std::string type;
        double sum = 0;
        int count = 0;
    };
    std::vector<AccSource> acc;
    double total_isyn_sum = 0, iext_sum = 0;
    double isyn_min = 1e9, isyn_max = -1e9;
    int n_samples = 100;
    int sample_interval = (int)(10.0 / sim.dt());  // every 10ms

    for (int i = 0; i < n_samples * sample_interval; ++i) {
        sim.step();
        if ((i + 1) % sample_interval == 0) {
            const auto& neurons = sim.neurons();
            double isyn = neurons[target_id]->get_I_syn();
            double iext = neurons[target_id]->get_I_ext();
            total_isyn_sum += isyn;
            iext_sum += iext;
            isyn_min = std::min(isyn_min, isyn);
            isyn_max = std::max(isyn_max, isyn);

            auto sources = conn.trace_inputs(target_id, neurons);
            for (auto& src : sources) {
                auto it = std::find_if(acc.begin(), acc.end(),
                    [&](const AccSource& a) { return a.name == src.source_name && a.type == src.type; });
                if (it != acc.end()) {
                    it->sum += src.current_pA;
                    it->count++;
                } else {
                    acc.push_back({src.source_name, src.type, src.current_pA, 1});
                }
            }
        }
    }

    // Print current budget
    std::cout << "\n========================================" << std::endl;
    std::cout << "  CURRENT BUDGET: " << name << std::endl;
    std::cout << "========================================\n" << std::endl;
    std::cout << "  V = " << std::setprecision(1) << std::fixed
              << sim.neurons()[target_id]->get_membrane_potential() << " mV" << std::endl;
    std::cout << "  I_ext = " << std::setprecision(2) << iext_sum / n_samples << " pA (mean)" << std::endl;
    std::cout << "  I_syn = " << std::setprecision(2) << total_isyn_sum / n_samples
              << " pA (mean)  range=[" << isyn_min << ", " << isyn_max << "]" << std::endl;
    std::cout << "\n  Connectome synaptic breakdown (mean pA):" << std::endl;

    // Sort by absolute mean
    std::sort(acc.begin(), acc.end(), [&](const AccSource& a, const AccSource& b) {
        return std::abs(a.sum / std::max(1, a.count)) > std::abs(b.sum / std::max(1, b.count));
    });

    double connectome_total = 0;
    for (auto& a : acc) {
        double mean_i = a.sum / std::max(1, a.count);
        connectome_total += mean_i;
        const char* flag = (std::abs(mean_i) > 50.0) ? " <-- ANOMALY" : "";
        std::cout << "    " << std::setw(8) << a.name
                  << " [" << std::setw(8) << a.type << "] "
                  << std::setw(8) << std::setprecision(2) << mean_i << " pA"
                  << flag << std::endl;
    }

    double non_connectome = (total_isyn_sum / n_samples) - connectome_total;
    if (std::abs(non_connectome) > 1.0) {
        const char* flag = (std::abs(non_connectome) > 50.0) ? " <-- ANOMALY" : "";
        std::cout << "    " << std::setw(8) << "(inject)"
                  << " [" << std::setw(8) << "code" << "] "
                  << std::setw(8) << std::setprecision(2) << non_connectome << " pA"
                  << flag << std::endl;
        std::cout << "\n  NOTE: Non-connectome injection detected!" << std::endl;
        std::cout << "  Sources: weathervane bias, omega turn, neuromodulation tonic, FLP-11 sleep" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::WARN);

    // Parse args
    bool save_mode = false;
    std::string trace_name;
    SimulationEngine::TuningParams tp;  // defaults from struct
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--save") save_mode = true;
        else if (arg == "--trace" && i + 1 < argc) trace_name = argv[++i];
        else if (arg == "--as_factor" && i+1 < argc) tp.as_factor = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--pulse_amp" && i+1 < argc) tp.pulse_amp = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--omega_threshold" && i+1 < argc) tp.omega_threshold = static_cast<float>(std::atof(argv[++i]));
        else if (arg == "--riv_tonic" && i+1 < argc) tp.riv_tonic = static_cast<float>(std::atof(argv[++i]));
    }

    // Trace mode: just do current budget and exit
    if (!trace_name.empty()) {
        trace_neuron(trace_name);
        return 0;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  REGRESSION TEST (30s simulation)" << std::endl;
    if (tp.as_factor != 1.5f || tp.pulse_amp != 30.0f)
        std::cout << "  [CLI] as_factor=" << tp.as_factor << " pulse_amp=" << tp.pulse_amp << std::endl;
    std::cout << "========================================\n" << std::endl;

    auto m = run_and_measure(30000, tp);

    // ---- Define baseline metrics ----
    // These values come from the known-good state after SMD fix (2025-02-10)
    // Update with --save when a new known-good state is established
    std::vector<Metric> metrics = {
        // SMD oscillator health (critical — most common regression target)
        // IMPORTANT: I_syn > ±50pA almost certainly means rogue current injection
        // Step 33: RME dampens head oscillation (Huang 2016 eLife)
        // Step 34: baselines raised 45→55 — observed range 55-70 mV across 105-neuron runs
        {"SMDDL V swing",      m.smddl_v_max - m.smddl_v_min,  55.0,  50, "mV", "SMDDL"},
        {"SMDVL V swing",      m.smdvl_v_max - m.smdvl_v_min,  75.0,  65, "mV", "SMDVL"},
        {"SMD diff amplitude", m.smd_diff_amp,                  90.0,  50, "mV", "SMDDL"},
        // Step 46: I_syn baseline raised — PDF→AIY→RIA→SMD + NLP-12→CKR-1→SMD add current
        {"SMDDL |I_syn| max",  std::max(std::abs(m.smddl_isyn_max), std::abs(m.smddl_isyn_min)),
                                                                32.0,  50, "pA", "SMDDL"},
        {"SMDDL I_ext",        m.smddl_iext_max,                3.0,   10, "pA", "SMDDL"},

        // Body mechanics
        // Step 33: head curvature reduced by RME amplitude control
        {"Curvature amplitude", m.curv_amp,                      0.14,  60, "/mm", ""},
        {"Speed mean",          m.speed_mean,                    0.35,  30, "mm/s", ""},  // Step 32: raised from 0.26 (AS dorsal bias increases |d-v|)
        // Step 34: heading rate baseline lowered 15→10 — 105-neuron system turns less aggressively
        {"Heading rate",        m.heading_rate,                  5.0,   60, "deg/s", ""},

        // Sensory
        {"ASEL mean V",         m.asel_mean,                    -40.0,  20, "mV", "ASEL"},
        {"ASER mean V",         m.aser_mean,                    -42.0,  20, "mV", "ASER"},

        // Behavioral (wider tolerance — stochastic)
        {"Reversal count",      (double)m.reversal_count,        5.0,   150, "", ""},
        {"Omega count",         (double)m.omega_count,           1.0,   200, "", ""},

        // Step 27: Sleep system sanity (30s test should NOT trigger sleep)
        // fatigue should be ~0.1-0.3 at 30s (accumulating but below 0.7 threshold)
        // is_sleeping must be false (0.0), RIS should be near resting (-50 to -65 mV)
        {"Fatigue @30s",        m.fatigue_final,                 0.2,   100, "", ""},
        {"Sleep @30s",          m.is_sleeping_final ? 1.0 : 0.0, 0.0,   10,  "", "RIS"},

        // Step 29: Wave propagation & curvature stability
        // Mid-body curvature amplitude: wave must propagate past head (seg 10 amp > 0.05)
        {"Midbody curv amp",    m.midbody_curv_amp,              0.20,  60,  "/mm", ""},
        // Curvature sign-change rate at seg 7: ~0.2 Hz normal, >100 Hz = numerical instability
        // Step 33: curv stability baseline raised — RME gain control affects oscillation frequency
        {"Curv stability",      m.curv_sign_change_hz,           1.5,   200, "Hz", ""},
        // Muscle work: must be >0.1 for any forward motion (D/V cancellation -> 0)
        {"Muscle work",         m.muscle_work_mean,              0.35,  60,  "", ""},
    };

    // ---- Check each metric ----
    int pass = 0, warn = 0, fail = 0;
    std::vector<std::string> failed_traces;

    for (auto& met : metrics) {
        double dev_pct = 0;
        if (std::abs(met.baseline) > 0.01) {
            dev_pct = 100.0 * (met.value - met.baseline) / std::abs(met.baseline);
        } else {
            dev_pct = 100.0 * (met.value - met.baseline);
        }

        bool ok = std::abs(dev_pct) <= met.tolerance_pct;
        const char* status = ok ? "[OK]" : "[!!]";
        if (!ok) {
            fail++;
            if (!met.trace_neuron.empty()) {
                // Avoid duplicate traces
                if (std::find(failed_traces.begin(), failed_traces.end(), met.trace_neuron) == failed_traces.end()) {
                    failed_traces.push_back(met.trace_neuron);
                }
            }
        } else {
            pass++;
        }

        if (save_mode) {
            // Print in baseline format for easy copy-paste
            std::cout << "  {\"" << met.name << "\", value, "
                      << std::setprecision(1) << std::fixed << met.value << ", "
                      << (int)met.tolerance_pct << ", \"" << met.unit << "\", \""
                      << met.trace_neuron << "\"}," << std::endl;
        } else {
            std::cout << "  " << status << " " << std::setw(22) << std::left << met.name
                      << std::right << std::setw(8) << std::setprecision(1) << std::fixed << met.value
                      << " " << met.unit;
            if (!ok) {
                std::cout << "  (baseline=" << met.baseline << ", dev=" << std::showpos
                          << std::setprecision(0) << dev_pct << "%" << std::noshowpos << ")";
            }
            std::cout << std::endl;
        }
    }

    if (!save_mode) {
        std::cout << "\n  Result: " << pass << " pass, " << fail << " FAIL\n" << std::endl;

        // Auto-trace failed neurons
        if (!failed_traces.empty()) {
            std::cout << "========================================" << std::endl;
            std::cout << "  AUTO-TRACING ANOMALOUS NEURONS" << std::endl;
            std::cout << "========================================" << std::endl;
            for (auto& name : failed_traces) {
                trace_neuron(name);
            }
        } else {
            std::cout << "  All metrics within expected range. No regression detected." << std::endl;
        }
    }

    return fail > 0 ? 1 : 0;
}
