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

    // Tunable parameters (live-adjustable from visualization or CLI)
    struct TuningParams {
        float weathervane_gain = 500.0f;  // pA per (conc/mm) — gradient → SMD bias
                                          // Step 19: 300→500. At gradient 0.01: bias=5pA.
        float synapse_scale   = 1.0f;    // global synapse weight multiplier
        float speed_scale     = 2.0f;    // v_max multiplier (was 1.0, target ~0.2 mm/s)
        float sensory_gain    = 1.0f;    // chemosensory transducer gain multiplier
        float bias_clamp      = 50.0f;   // max weathervane bias current (pA)
        // Step 32: Runtime-tunable parameters (avoid recompile for parameter sweeps)
        float as_factor       = 1.7f;    // AS dorsal resistance factor (Step 42C: 2.0→1.7, RIA↔RIV feedback loop)
        float pulse_amp       = 50.0f;   // RIV post-reversal pulse amplitude (Step 42C: 80→50, RIA↔RIV provides base drive)
        float omega_threshold = 0.5f;    // RIV release threshold for omega mode
        float riv_tonic       = 1.0f;    // RIV baseline tonic drive (pA)
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

    // Step 41: Reset chemosensory transducers after environment changes
    // Call after modifying food/chemical field positions
    void reset_transducers() {
        double vol = environment_.sample_chemical(body_.get_head_position());
        double food = environment_.sample_food_density(body_.get_head_position());
        for (auto& cm : chemo_mappings_) {
            cm.transducer.reset(cm.uses_food_density ? food : vol);
        }
        double rep = environment_.sample_repellent(body_.get_head_position());
        for (auto& nm : noci_mappings_) {
            nm.transducer.reset(rep);
        }
        neuromod_.reset_concentrations();
    }

    // Touch/behavior state (Step 18)
    bool is_reversing() const { return is_reversing_; }
    bool is_omega_turning() const { return riv_omega_active_; }
    double reversal_duration() const { return reversal_duration_; }

    // Satiety (Step 20c → Step 24: pharyngeal pump-driven)
    double satiety() const { return satiety_; }
    // Sickness (Step 26: learned pathogen avoidance)
    double sickness() const { return sickness_; }
    // Food memory / ARS (Step 20d)
    double food_memory() const { return food_memory_; }
    // Egg-laying (Step 38)
    double egg_pressure() const { return egg_pressure_; }
    double egg_laid_count() const { return egg_laid_count_; }
    // Pharyngeal pump (Step 24)
    double pump_rate_hz() const { return pharynx_.pump_rate_hz(); }
    int total_pumps() const { return pharynx_.total_pumps(); }
    double pharynx_V() const { return pharynx_.V_muscle(); }
    const PharyngealPump& pharynx() const { return pharynx_; }
    // Sleep / fatigue (Step 27)
    double fatigue() const { return fatigue_; }
    bool is_sleeping() const { return is_sleeping_; }

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
    void apply_riv_omega();              // Step 31: RIV-driven omega turn (emergent from TA gating)
    void setup_neuromodulation();         // configure 5-HT, DA, TA modulators (Step 20)

    // Chemosensory transduction: neuron_id → transducer
    struct ChemoMapping {
        int neuron_id;
        ChemoTransducer transducer;
        bool uses_food_density = false;  // true for NSM/CEP (narrow σ=3mm bacterial colony)
    };
    std::vector<ChemoMapping> chemo_mappings_;        // AWC/AWA sample food odor (volatile)
    std::vector<ChemoMapping> soluble_mappings_;     // Step 26b: ASE sample soluble field (salt/amino acids)
    std::vector<ChemoMapping> noci_mappings_;         // Step 25: ASH nociceptors sample repellent field
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
        int sample_segment;   // primary sense point (backward compat)
        int sense_start;      // Step 29: range start for multi-segment integration
        int sense_end;        // Step 29: range end (exclusive)
        bool is_dorsal;       // true=dorsal (negative curv excites), false=ventral
    };
    std::vector<ProprioMapping> proprio_mappings_;

    // Touch avoidance (Step 18, Chalfie 1985)
    std::vector<int> alm_ids_;  // anterior touch neuron IDs
    std::vector<int> plm_ids_;  // posterior touch neuron IDs
    std::vector<int> olq_ids_; // Step 33: nose touch neuron IDs (4 quadrant)
    // Step 34: O₂ sensing neuron IDs
    std::vector<int> urx_ids_;  // URX L/R (high O₂ sensors)
    std::vector<int> aua_ids_;  // AUA L/R (O₂ signal relay/integration)
    int aqr_id_ = -1;          // AQR (anterior body cavity, unpaired)
    int pqr_id_ = -1;          // PQR (posterior body cavity, unpaired)
    double o2_gain_ = 30.0;    // max pA for O₂ transduction
    double npr1_tonic_ = -28.0; // NPR-1 tonic inhibition on URX (N2 = constitutively active)
    double npr1_aua_ = -12.0;  // NPR-1 inhibition on AUA (proxy for missing RMG suppression)
    // Step 35: CO₂ sensing (BAG neurons)
    std::vector<int> bag_ids_;  // BAG L/R (CO₂ sensors)
    double co2_gain_ = 40.0;   // max pA for CO₂ transduction
    double co2_threshold_ = 0.5; // % CO₂ activation threshold
    double prev_co2_head_ = 0.04; // previous CO₂ for phasic response
    // Step 36: Proprioception (DVA + PVD)
    int dva_id_ = -1;              // DVA whole-body proprioceptive interneuron
    std::vector<int> pvd_ids_;     // PVD L/R harsh touch + proprioception
    double dva_gain_ = 15.0;       // pA per unit mean |curvature| (TRP-4 sensitivity)
    double pvd_harsh_thresh_ = 1.0; // mm, harsh touch distance threshold (closer than ALM 2mm)
    double pvd_harsh_current_ = 60.0; // pA, harsh touch stimulus (PVD→AVA 2 sec already strong)
    double pvd_proprio_gain_ = 8.0;  // pA per unit posterior |curvature|
    // Step 38: Egg-laying (HSN/VC)
    std::vector<int> hsn_ids_;     // HSN L/R serotonergic command motor neurons
    std::vector<int> vc_ids_;      // VC4/VC5 cholinergic motor neurons
    double egg_pressure_ = 0.0;    // 0-1, egg accumulation pressure (slow ramp)
    double egg_tau_fill_ = 120000.0;  // ms (120s) to fill — eggs accumulate ~10min/egg
    double egg_threshold_ = 0.7;   // egg_pressure threshold for HSN activation
    double hsn_egg_gain_ = 30.0;   // pA, max HSN drive from egg pressure
    double egg_laid_count_ = 0;    // total eggs laid
    double egg_active_end_ = 0.0;  // end time of current active state (ms)
    double egg_active_duration_ = 2000.0; // ~2s active state (scaled from real 2min)
    double touch_current_ = 80.0;  // pA, strong pulse for touch stimulus
    double arena_margin_ = 2.0;    // mm, wall collision zone
    double nose_margin_ = 0.3;     // Step 33: nose touch zone (mm, closer than body touch)
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
    // Step 31: RIV neuron IDs for emergent omega turn
    int rivl_id_ = -1;
    int rivr_id_ = -1;
    double riv_omega_threshold_ = 0.5;     // RIV release rate threshold for omega mode

    // Klinotaxis: Step 28 — RIA multi-compartment Ca²⁺ gate-and-switch
    // REF: Hendricks 2012 Nature — nrV/nrD compartmentalized calcium
    int rial_id_ = -1;                  // RIAL neuron ID
    int riar_id_ = -1;                  // RIAR neuron ID
    double ria_ca_diff_filtered_ = 0.0; // filtered (nrV-nrD) Ca2+ AC component
    double ria_ca_diff_mean_ = 0.0;     // DC baseline of Ca2+ diff (2s tau, for removal)

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

    // Step 26: Learned pathogen avoidance (Zhang 2005 Nature)
    // Eating toxic food → sickness_ rises → ADF 5-HT ↑ → AWC synapse flip
    double sickness_ = 0.0;             // [0,1] internal malaise state
    double sickness_tau_rise_ = 30000.0; // ms, slow accumulation while eating toxin (~30s)
    double sickness_tau_decay_ = 600000.0; // ms, very slow recovery (~10min, persistent memory)
    std::vector<int> adf_ids_;           // ADF serotonin neuron IDs
    std::vector<int> aiy_ids_;           // AIY interneuron IDs (approach pathway)
    void update_sickness();              // accumulate sickness from toxic food intake
    void update_pathogen_learning();     // AWC→AIY w_mod↓, AWC→AIB w_mod↑

    // Step 27: Sleep / Quiescence (Lethargus)
    // REF: Turek 2016 eLife — RIS releases FLP-11 to systemically induce sleep
    //      Konietzka 2020 Nat Commun — RIS as locomotion stop neuron
    //      Nagy 2014 eLife — sleep homeostasis (micro/macro)
    double fatigue_ = 0.0;              // [0,1] homeostatic sleep drive
    double fatigue_tau_rise_ = 240000.0;  // ms, ~240s to accumulate when active
    double fatigue_tau_decay_ = 45000.0;  // ms, ~45s to clear during sleep
    double fatigue_threshold_ = 0.7;      // RIS activation threshold
    int ris_id_ = -1;                     // RIS neuron ID
    bool is_sleeping_ = false;            // current sleep state
    void update_fatigue();                // fatigue accumulation / decay
    void apply_sleep_effects();           // FLP-11 global inhibition during sleep

    // Reversal & omega turn tracking
    bool is_reversing_ = false;
    double reversal_start_time_ = 0.0;
    double reversal_duration_ = 0.0;
    double riv_prev_max_ = 0.0;       // Step 32: previous RIV max for peak detection
    double pre_rev_dorsal_tone_ = 0.3; // Step 32: dorsal tone snapshot at reversal start
    bool riv_omega_active_ = false;  // Step 31: true when RIV burst drives omega
    double riv_omega_start_ = 0.0;   // timestamp of omega activation (for min duration)
    double riv_post_rev_time_ = -1e9; // timestamp when last reversal ended (for RIV pulse)
    double riv_post_rev_amp_l_ = 0.0;  // RIVL pulse amplitude (gradient-biased)
    double riv_post_rev_amp_r_ = 0.0;  // RIVR pulse amplitude (gradient-biased)
    std::mt19937 touch_rng_{123};

    // === Performance: cached neuron IDs (avoid per-step hash lookups) ===
    int aval_id_ = -1, avar_id_ = -1;
    int avbl_id_ = -1, avbr_id_ = -1;
    int smddl_id_ = -1, smddr_id_ = -1, smdvl_id_ = -1, smdvr_id_ = -1;
    int nsml_id_ = -1, nsmr_id_ = -1;

    // === Performance: cached typed pointers (avoid per-step dynamic_cast) ===
    SingleCompartmentNeuron* smd_scn_[4] = {};  // [0]=SMDDL [1]=SMDVL [2]=SMDDR [3]=SMDVR
    MultiCompartmentNeuron* ria_mcn_[2] = {};   // [0]=RIAL [1]=RIAR

    // === Performance: cached awc_pref + pre-indexed learning synapses ===
    double awc_pref_cached_ = 1.0;
    std::vector<size_t> awc_aiy_syn_indices_;   // synapses_ indices for AWC→AIY (awc_pref)
    std::vector<size_t> aser_syn_indices_;       // synapses_ indices for ASER→* (salt learning)
    std::vector<size_t> awc_syn_indices_;        // synapses_ indices for AWC→* (pathogen learning)

    void cache_neuron_ids_and_synapses();        // called once at init
    void update_awc_pref_cache();                // called after learning updates

public:
    void set_rng_seed(unsigned int seed) { touch_rng_.seed(seed); }
};

} // namespace celegans
