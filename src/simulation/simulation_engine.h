#pragma once

#include "core/types.h"
#include "core/config.h"
#include "core/fast_math.h"
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
#include <unordered_map>
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
        float speed_scale     = 2.0f;    // Keep at 2.0 for off-food chemotaxis; 5-HT=-0.80 handles on-food slowing
        float sensory_gain    = 1.0f;    // chemosensory transducer gain multiplier
        float bias_clamp      = 5.0f;    // Step 65: 50→5 pA (SMD 49mV: 5pA→4mV→8% duty shift, preserves burst)
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
    // ESR receptor upregulation level (Step 76)
    double esr_receptor_level() const { return esr_receptor_level_; }
    // Food memory / ARS (Step 20d)
    double food_memory() const { return food_memory_; }
    // Egg-laying (Step 38)
    double egg_pressure() const { return egg_pressure_; }
    double egg_laid_count() const { return egg_laid_count_; }
    // Defecation (Step 56)
    int dmp_count() const { return dmp_count_; }
    bool dmp_active() const { return dmp_active_; }
    // Tap habituation (Step 60/78)
    int tap_count() const { return tap_count_; }
    bool tap_active() const { return tap_active_; }
    // Sensitization / dishabituation (Step 79)
    double sensitization() const { return sensitization_; }
    void set_dishabit_time(double t_ms) { dishabit_time_ = t_ms; }
    // Step 80: Tc learning accessor (returns current learned Tc from first AFD transducer)
    double learned_tc() const { return thermo_mappings_.empty() ? cultivation_temp_ : thermo_mappings_[0].transducer.cultivation_temp(); }
    // Pharyngeal pump (Step 24)
    double pump_rate_hz() const { return pharynx_.pump_rate_hz(); }
    int total_pumps() const { return pharynx_.total_pumps(); }
    double pharynx_V() const { return pharynx_.V_muscle(); }
    const PharyngealPump& pharynx() const { return pharynx_; }
    // Sleep / fatigue (Step 27)
    double fatigue() const { return fatigue_; }
    bool is_sleeping() const { return is_sleeping_; }
    // Step 62: Force sleep for consolidation experiments
    void force_sleep(double duration_ms) { forced_sleep_end_ = current_time_ + duration_ms; }
    double learning_sleep_drive() const { return learning_sleep_drive_; }
    // Step 63: INS-1 insulin concentration
    double ins1_conc() const { return ins1_conc_; }

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
    void apply_sensitization();          // Step 79: nociceptive sensitization → touch pool boost
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

    // Non-chemosensory neurons (touch, etc.) get fixed baseline
    std::vector<int> other_sensory_ids_;
    double sensory_baseline_ = 3.0; // pA, low baseline for touch neurons (no stimulus)

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

    double o2_gain_ = 30.0;    // max pA for O₂ transduction
    double npr1_tonic_ = -28.0; // NPR-1 tonic inhibition on URX (N2 = constitutively active)
    double npr1_aua_ = -12.0;  // NPR-1 inhibition on AUA (proxy for missing RMG suppression)
    double co2_gain_ = 40.0;   // max pA for CO₂ transduction
    double co2_threshold_ = 0.5; // % CO₂ activation threshold
    double prev_co2_head_ = 0.04; // previous CO₂ for phasic response
    double dva_gain_ = 15.0;       // pA per unit mean |curvature| (TRP-4 sensitivity)
    double pvd_harsh_thresh_ = 1.0; // mm, harsh touch distance threshold (closer than ALM 2mm)
    double pvd_harsh_current_ = 60.0; // pA, harsh touch stimulus (PVD→AVA 2 sec already strong)
    double pvd_proprio_gain_ = 8.0;  // pA per unit posterior |curvature|
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
    // Step 60: Tap habituation — periodic mechanical taps (Rankin 1990)
    // Tap activates ALM+PLM simultaneously (plate vibration, not directional)
    // STP vesicle depletion at glutamatergic synapses → decreased reversal response
    double tap_interval_ = 10000.0; // ms between taps (10s ISI, Rankin 1990)
    double tap_timer_ = 0.0;        // time since last tap
    double tap_duration_ = 200.0;   // ms, tap pulse duration (brief mechanical stimulus)
    double tap_current_ = 60.0;     // pA, tap stimulus strength (weaker than wall collision)
    int tap_count_ = 0;             // number of taps delivered
    bool tap_active_ = false;       // is a tap pulse currently active?
    double tap_pulse_end_ = 0.0;    // when current tap pulse ends
    // Step 79: Nociceptive sensitization / dishabituation (Groves & Thompson 1970)
    // Dual-process theory: habituation (S-process, STP) + sensitization (R-process)
    // Strong ASH activation → slow sensitization state → boosts touch vesicle recovery
    // REF: Groves & Thompson 1970 Psychol Rev — dual-process theory
    //      Rankin & Broster 1992 — dishabituation in C. elegans
    //      Greer 2008 — SER-2/PKC modulation of mechanosensory synapses
    double sensitization_ = 0.0;           // [0,1] nociceptive sensitization level
    double sensitization_tau_decay_ = 30000.0;  // ms, slow decay (~30s, Rankin 1992)
    double sensitization_rise_rate_ = 0.005;    // per ms of strong ASH activity
    double sensitization_pool_boost_ = 0.0003;  // vesicle pool boost per ms when sensitized
    double dishabit_time_ = -1.0;          // ms, when to deliver dishabituating stimulus (-1=off)
    double dishabit_duration_ = 2000.0;    // ms, harsh stimulus duration (2s train)
    double dishabit_current_ = 100.0;      // pA, harsh stimulus to ASH (strong nociceptive)
    std::vector<size_t> touch_syn_indices_; // cached indices of touch circuit synapses for pool boost
    // Step 66: Pirouette Poisson REMOVED — reversals emerge from AVA neural circuit
    // REF: Roberts 2016 eLife — stochastic switch, Piggott 2011 Cell — dual circuits
    // Food edge reversal preserved: inject AVA current instead of setting is_reversing_
    double food_edge_ava_end_ = 0.0;       // end time for food-edge AVA injection pulse (ms)
    double reversal_refractory_end_ = 0.0; // no new reversal until this time (Schmitt trigger)
    double prev_concentration_ = 0.0;      // previous head concentration (kept for diagnostics)
    double dCdt_filtered_ = 0.0;           // filtered dC/dt (kept for diagnostics)
    double prev_temp_dev_ = 0.0;           // previous |T-Tc| (kept for diagnostics)
    double dTdev_filtered_ = 0.0;          // filtered d|T-Tc|/dt (kept for diagnostics)
    double riv_omega_threshold_ = 0.5;     // RIV release rate threshold for omega mode

    // Klinotaxis: Step 28 — RIA multi-compartment Ca²⁺ gate-and-switch
    // REF: Hendricks 2012 Nature — nrV/nrD compartmentalized calcium
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
    void update_pharynx();             // Step 24: pharyngeal CPG + real feeding
    void apply_pharyngeal_modulation(); // 5-HT→MC excitation, OA→MC inhibition

    // Satiety internal state (Step 20c → Step 24: now driven by pharyngeal pumping)
    double satiety_ = 0.0;             // [0,1] — 0=hungry, 1=full
    double satiety_tau_deplete_ = 40000.0; // ms to get hungry (40s off food)
    void update_satiety();              // called each step (now uses pharynx pump rate)

    // Step 76: Enhanced Slowing Response — ESR (Sawin 2000 Neuron)
    // Starvation → MOD-1/SER-4 receptor upregulation → amplified 5-HT sensitivity
    // When starved worm encounters food: 5-HT + upregulated receptors
    //   → extra inhibition of AIY/PVC → less forward drive → speed drops EMERGENTLY
    // NOT a direct speed change — emerges from circuit-level neural effects
    // REF: Sawin 2000 — tph-1 required; Gürel 2012 — mod-1;ser-4 double mutant abolishes ESR
    double esr_mod1_gain_ = -8.0;      // pA, extra MOD-1 on AIY/PVC when starved × 5-HT
    double esr_receptor_level_ = 0.0;   // [0,1] slow MOD-1 upregulation during starvation
    double esr_upregulate_tau_ = 60000.0; // ms, receptor upregulation time (60s starvation)
    double esr_downregulate_tau_ = 30000.0; // ms, receptor downregulation when fed (30s)
    void apply_esr_modulation();        // hunger × 5-HT → amplified MOD-1 on circuit

    // Step 47: Food-edge head poke reversal (eLife 2024, Flavell lab)
    // When head exits food boundary → reversal with high probability (dwelling)
    // REF: Flavell 2024 eLife — head poke reversal 1.1/min, leaving 1/95min
    double prev_food_at_head_ = 0.0;    // previous step food density at head
    bool was_on_lawn_ = false;          // Step 54: latch for food edge crossing detector

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
    double awb_pathogen_gain_ = 25.0;    // pA, AWB drive from pathogen odor × sickness
    double mod1_aiy_gain_ = -12.0;       // pA, ADF sickness 5-HT → MOD-1 ⊣ AIY (max at sickness=1)
    double mod1_aiz_gain_ = -6.0;        // pA, ADF sickness 5-HT → MOD-1 ⊣ AIZ (half of AIY)
    void update_sickness();              // accumulate sickness from toxic food intake
    void update_pathogen_learning();     // AWC→AIY w_mod↓, AWC→AIB w_mod↑

    // Step 63: INS-1 insulin signaling (Lin 2010 JNeurosci, Comm Bio 2022)
    // INS-1 released from ASI/AIA as starvation/sickness signal.
    // Acts via DAF-2 on AWC (attraction→avoidance switch), AIA, AIY (reduce chemotaxis).
    // Sickness enhances INS-1 → "anorexia" (reduced feeding when sick).
    // REF: Lin 2010 — INS-1 from ASI+AIA → DAF-2 on AWC
    //      Comm Bio 2022 — INS-1 from AIA → DAF-2c on ASER (taste avoidance)
    //      You 2008 — insulin pathway in pathogen avoidance
    double ins1_conc_ = 0.0;             // [0,1] INS-1 concentration (virtual neuromodulator)
    double ins1_tau_ = 10000.0;          // ms, 10s time constant (neuropeptide, slow)
    double ins1_sickness_gain_ = 3.0;    // sickness amplification of INS-1 release
    double ins1_aiy_gain_ = -8.0;        // pA, INS-1 → DAF-2 ⊣ AIY (reduce forward drive)
    double ins1_aia_gain_ = -5.0;        // pA, INS-1 → DAF-2 ⊣ AIA (reduce chemotaxis relay)
    double ins1_awc_gain_ = -6.0;        // pA, INS-1 → DAF-2 ⊣ AWC (attraction→avoidance)
    double sickness_mc_suppress_ = -20.0; // pA, sickness → MC suppression (anorexia)
    void update_ins1();                  // compute INS-1 from satiety + sickness
    void apply_ins1_modulation();        // apply INS-1 effects on target neurons

    // Step 62: Sleep-dependent memory consolidation (Chouhan 2023 Cell)
    // "Sleep is required to consolidate odor memory and remodel olfactory synapses"
    // Weight_mod slowly forgets (drifts back to 1.0). Sleep suppresses forgetting
    // and boosts learning rate → memory encoded during wake is consolidated in sleep.
    // REF: Chouhan 2023 Cell, Zhang 2005 Nature, Iannacone 2017 JNeurosci
    double w_mod_forget_rate_ = 0.000002;  // per ms (~0.002/s): w_mod → 1.0 drift (forgetting)
    double sleep_learn_boost_ = 2.0;       // learning rate ×2 during sleep (Chouhan 2023)
    double sleep_forget_suppress_ = 0.3;   // forgetting ×0.3 during sleep (consolidation)
    double sleep_sickness_protect_ = 0.2;  // sickness decay ×0.2 during sleep (memory protection)
    double learning_sleep_drive_ = 0.0;    // [0,1] learning-induced sleep pressure
    double learning_sleep_tau_ = 120000.0; // ms, 120s decay for learning-induced sleep drive
    double forced_sleep_end_ = 0.0;        // ms, if > current_time_ → force is_sleeping_=true
    void apply_synaptic_forgetting();      // slow drift of w_mod toward 1.0

    // Step 27: Sleep / Quiescence (Lethargus)
    // REF: Turek 2016 eLife — RIS releases FLP-11 to systemically induce sleep
    //      Konietzka 2020 Nat Commun — RIS as locomotion stop neuron
    //      Nagy 2014 eLife — sleep homeostasis (micro/macro)
    double fatigue_ = 0.0;              // [0,1] homeostatic sleep drive
    double fatigue_tau_rise_ = 240000.0;  // ms, ~240s to accumulate when active
    double fatigue_tau_decay_ = 45000.0;  // ms, ~45s to clear during sleep
    double fatigue_threshold_ = 0.7;      // RIS activation threshold
    bool is_sleeping_ = false;            // current sleep state
    void update_fatigue();                // fatigue accumulation / decay
    void apply_sleep_effects();           // FLP-11 global inhibition during sleep

    // Step 56: Defecation Motor Program (DMP)
    // Intestinal Ca²⁺ oscillator (IP3/ITR-1) generates ~45s rhythm — non-neural pacemaker
    // Three motor steps: pBoc (posterior body contraction), aBoc (anterior), Exp (enteric)
    // AVL+DVB fire synchronized GABA APs → EXP-1 excitatory receptor → enteric muscle contraction
    // REF: Thomas 1990, Dal Santo 1999, Jiang 2022 Nat Commun
    double dmp_timer_ = 0.0;              // ms since last DMP (intestinal Ca²⁺ oscillator)
    double dmp_period_ = 45000.0;         // ms, ~45s cycle (temperature-compensated)
    double dmp_phase_timer_ = -1.0;       // ms into current DMP execution (-1 = inactive)
    int dmp_count_ = 0;                   // total DMP cycles completed
    bool dmp_active_ = false;             // true during DMP motor execution
    // Step 71: dmp_speed_factor_ REMOVED (P0-5 fix)
    // Speed reduction now emerges from AVL/DVB GABA → B-class MN inhibition
    void update_defecation();             // called each step

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

    // === Neuron ID cache (auto-populated from connectome, Step 52) ===
    std::unordered_map<std::string, int> nid_;                    // exact name → ID
    std::unordered_map<std::string, std::vector<int>> nids_;      // group key → IDs
    int nid(const char* name) const {
        auto it = nid_.find(name);
        return (it != nid_.end()) ? it->second : -1;
    }
    const std::vector<int>& nids(const char* key) const {
        static const std::vector<int> empty;
        auto it = nids_.find(key);
        return (it != nids_.end()) ? it->second : empty;
    }

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

    // Step 67: Laser ablation — silence named neuron(s)
    // REF: Chalfie 1985, Bargmann & Horvitz 1991
    // Ablates both L/R if base name given (e.g. "AVA" → AVAL+AVAR)
    void ablate_neuron(const std::string& name) {
        int n = static_cast<int>(neurons_.size());
        // Try exact name first
        int id = nid(name.c_str());
        if (id >= 0 && id < n) {
            neurons_[id]->ablate();
            return;
        }
        // Try L/R pair
        int idL = nid((name + "L").c_str());
        int idR = nid((name + "R").c_str());
        if (idL >= 0 && idL < n) neurons_[idL]->ablate();
        if (idR >= 0 && idR < n) neurons_[idR]->ablate();
    }
};

} // namespace celegans
