#pragma once

#include "core/types.h"
#include "core/config.h"
#include "neuron/single_compartment.h"
#include "neuron/neuron_factory.h"
#include "connectome/connectome.h"
#include "connectome/connectome_loader.h"
#include "body/body_model.h"
#include "motor/motor_controller.h"
#include "environment/environment.h"
#include "environment/sensory_transducer.h"
#include "neuromodulation/neuromodulation.h"
#include "compute/compute_backend.h"
#include "pharynx/pharyngeal_pump.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace celegans {

class SimulationEngine {
public:
    SimulationEngine();

    // Initialize with config or defaults
    void initialize(const Config& config);
    void initialize_default();

    // Run simulation
    void step();
    void run(double duration_ms);

    // Tunable parameters (live-adjustable from visualization)
    struct TuningParams {
        float weathervane_gain = 500.0f;  // pA per (conc/mm) — gradient → SMD bias
                                          // Step 19: 300→500. At gradient 0.01: bias=5pA.
        float synapse_scale   = 1.0f;    // global synapse weight multiplier
        float speed_scale     = 2.0f;    // v_max multiplier (was 1.0, target ~0.2 mm/s)
        float sensory_gain    = 1.0f;    // chemosensory transducer gain multiplier
        float bias_clamp      = 50.0f;   // max weathervane bias current (pA)
    };
    TuningParams params;

    // Access
    double current_time() const { return current_time_; }
    double dt() const { return dt_; }
    int get_step_count() const { return step_count_; }
    const BodyModel& body() const { return body_; }
    BodyModel& body_mut() { return body_; }
    const Environment& environment() const { return environment_; }
    Environment& environment() { return environment_; }
    const Connectome& connectome() const { return connectome_; }
    Connectome& connectome_mut() { return connectome_; }
    const std::vector<std::unique_ptr<Neuron>>& neurons() const { return neurons_; }
    const NeuromodulationManager& neuromodulation() const { return neuromod_; }

    // Touch/behavior state (Step 18)
    bool is_reversing() const { return is_reversing_; }
    bool is_omega_turning() const { return omega_pending_; }
    double reversal_duration() const { return reversal_duration_; }

    // Satiety (Step 20c → Step 24: pharyngeal pump-driven)
    double satiety() const { return satiety_; }
    // Food memory / ARS (Step 20d)
    double food_memory() const { return food_memory_; }
    // Pharyngeal pump (Step 24)
    double pump_rate_hz() const { return pharynx_.pump_rate_hz(); }
    int total_pumps() const { return pharynx_.total_pumps(); }
    double pharynx_V() const { return pharynx_.V_muscle(); }
    const PharyngealPump& pharynx() const { return pharynx_; }

    // Callback for each step (for logging/visualization)
    using StepCallback = std::function<void(const SimulationEngine&, int step_num)>;
    void set_step_callback(StepCallback cb) { step_callback_ = std::move(cb); }

private:
    double dt_ = 0.5;          // simulation timestep (ms)
    double current_time_ = 0.0;
    int step_count_ = 0;

    Environment environment_;
    BodyModel body_;
    Connectome connectome_;
    MotorController motor_controller_;
    NeuromodulationManager neuromod_;
    std::vector<std::unique_ptr<Neuron>> neurons_;

    StepCallback step_callback_;

    // GPU compute backend (Step 22)
    std::unique_ptr<ComputeBackend> gpu_backend_;
    bool use_gpu_ = false;
    std::vector<SynapseGPU> gpu_synapses_;   // flat GPU-compatible synapse data
    std::vector<float> gpu_V_;               // neuron voltages for GPU
    std::vector<float> gpu_I_;               // synaptic currents from GPU
    void setup_gpu_backend();                // init GPU + upload synapse data
    void sync_synapses_to_gpu();             // host→GPU synapse state

    void create_neurons();

    // Step 13: Biologically grounded locomotion (replaces Step 12 placeholders)
    void apply_sensory_input();          // chemosensory neurons sample gradient, others get baseline
    void apply_weathervane();            // gradient ⊥ heading → SMD bias (Iino & Yoshida 2009)
    void apply_ria_smd_modulation();     // RIA release → CCA-1 V_half shift on SMD (Step 19)
    void apply_smb_neck_bias();          // SMB D-V balance → neck curvature DC offset (Step 19 P2)
    void apply_proprioceptive_stretch(); // body curvature → MEC channels in motor neurons
    void apply_head_tonic();             // tonic drive to head motor neurons (from upstream)
    void apply_touch_stimulus();         // wall collision → ALM/PLM activation (Chalfie 1985)
    void apply_omega_turn();             // post-reversal deep ventral bend (Gray 2005)
    void setup_neuromodulation();         // configure 5-HT, DA, TA modulators (Step 20)

    // Chemosensory transduction: neuron_id → transducer
    struct ChemoMapping {
        int neuron_id;
        ChemoTransducer transducer;
        bool uses_food_density = false;  // true for NSM/CEP (narrow σ=3mm bacterial colony)
    };
    std::vector<ChemoMapping> chemo_mappings_;
    std::vector<ChemoMapping> noci_mappings_;   // Step 25: ASH nociceptors sample repellent field
    std::vector<int> aib_ids_;                  // Step 25: AIB interneuron IDs (5-HT→MOD-1 inhibition)

    // Non-chemosensory neurons (touch, etc.) get fixed baseline
    std::vector<int> other_sensory_ids_;
    double sensory_baseline_ = 3.0; // pA, low baseline for touch neurons (no stimulus)

    // Head motor neuron IDs (for tonic upstream drive)
    std::vector<int> head_motor_ids_;
    double head_tonic_ = 3.0;  // pA, near CCA-1 window for rebound oscillation

    // Proprioceptive mapping: motor neuron → body segment
    struct ProprioMapping {
        int neuron_id;
        int sample_segment;   // anterior segment to sense curvature from
        bool is_dorsal;       // true=dorsal (negative curv excites), false=ventral
    };
    std::vector<ProprioMapping> proprio_mappings_;

    // Touch avoidance (Step 18, Chalfie 1985)
    std::vector<int> alm_ids_;  // anterior touch neuron IDs
    std::vector<int> plm_ids_;  // posterior touch neuron IDs
    double touch_current_ = 80.0;  // pA, strong pulse for touch stimulus
    double arena_margin_ = 2.0;    // mm, wall collision zone
    // Pirouette model (Pierce-Shimomura 1999 biased random walk)
    // dC/dt modulates pirouette initiation rate: dC/dt<0 → more pirouettes
    // This bypasses the noisy klinokinesis neural pathway (ASE→AIB→AVA)
    // same principle as the curvature_bias_ bypass for weathervane
    double planned_reversal_end_ = 0.0;    // when current reversal should end (ms)
    double reversal_refractory_end_ = 0.0; // no new pirouette until this time (tcrit ≈ 5s)
    double prev_concentration_ = 0.0;      // previous head concentration for dC/dt
    double dCdt_filtered_ = 0.0;           // filtered concentration derivative (tau=4s)
    double prev_temp_dev_ = 0.0;           // previous |T-Tc| for thermal klinokinesis
    double dTdev_filtered_ = 0.0;          // filtered d|T-Tc|/dt (tau=4s, Ryu & Samuel 2002)
    double omega_heading_before_ = 0.0;    // heading when omega starts (debug)
    double omega_dist_before_ = 0.0;       // dist to food when omega starts (debug)

    // Klinotaxis: RIA gate-and-switch
    double ria_curv_filtered_ = 0.0;   // filtered cross-correlation output
    double sensory_diff_mean_ = 0.0;   // DC baseline of sensory ON-OFF diff (2s tau)

    // Thermosensory transduction (Step 23 — Mori 1995)
    struct ThermoMapping {
        int neuron_id;
        ThermoTransducer transducer;
    };
    std::vector<ThermoMapping> thermo_mappings_;
    void apply_thermo_input();              // AFD samples temperature field
    double cultivation_temp_ = 20.0;        // °C, initial cultivation temperature

    // Step 24: Pharyngeal pumping system (replaces placeholder satiety)
    // REF: Avery (WormBook 2012), Raizen & Avery 1994
    PharyngealPump pharynx_;            // pharyngeal muscle AP model
    std::vector<int> mc_ids_;           // MC motor neuron IDs (pacemaker)
    std::vector<int> m3_ids_;           // M3 motor neuron IDs (relaxation)
    int m4_id_ = -1;                    // M4 motor neuron ID (isthmus peristalsis)
    std::vector<int> i1_ids_;           // I1 interneuron IDs (bridge)
    void update_pharynx();             // Step 24: pharyngeal CPG + real feeding
    void apply_pharyngeal_modulation(); // 5-HT→MC excitation, OA→MC inhibition

    // Satiety internal state (Step 20c → Step 24: now driven by pharyngeal pumping)
    double satiety_ = 0.0;             // [0,1] — 0=hungry, 1=full
    double satiety_tau_deplete_ = 40000.0; // ms to get hungry (40s off food)
    void update_satiety();              // called each step (now uses pharynx pump rate)
    std::vector<int> ric_ids_;          // RIC neuron IDs (OA source, tonic drive)

    // Area-Restricted Search (Step 20d)
    // Models DA → DARPP-32 phosphorylation → GLR-1 enhancement → more reversals
    // REF: Hills 2004 J Neurosci, Wakabayashi 2004, Calhoun 2014 eLife
    double food_memory_ = 0.0;          // [0,1] DARPP-32 phosphorylation level
    double food_memory_tau_rise_ = 5000.0;   // ms, fast rise on food (5s)
    double food_memory_tau_decay_ = 90000.0; // ms, slow decay off food (90s, minutes-scale)
    void update_food_memory();          // called each step
    void apply_gradient_klinokinesis();  // no-gradient → high pirouette (Step 21d)

    // Short-term plasticity setup (Step 21)
    void setup_stp_params();            // per-circuit tau_recovery tuning
    // Salt chemotaxis learning (Step 21c)
    void update_salt_learning();        // called each step

    // Reversal & omega turn tracking
    bool is_reversing_ = false;
    double reversal_start_time_ = 0.0;
    double reversal_duration_ = 0.0;
    bool omega_pending_ = false;
    double omega_end_time_ = 0.0;
    double omega_direction_ = 1.0;  // +1 ventral, -1 dorsal
    std::mt19937 touch_rng_{123};
public:
    void set_rng_seed(unsigned int seed) { touch_rng_.seed(seed); }
};

} // namespace celegans
