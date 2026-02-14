// ================================================================
// Internal States — Split from simulation_engine.cpp (Step 50)
//
// Contains: update_satiety, update_food_memory,
//           update_fatigue, apply_sleep_effects
// ================================================================
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include "core/fast_math.h"

namespace celegans {

static bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

// ================================================================
// Satiety internal state (Step 20c)
// ================================================================
void SimulationEngine::update_satiety() {
    double food_conc = environment_.sample_food_density(body_.get_head_position());
    double on_food = food_conc * food_conc / (food_conc * food_conc + 0.09);

    double depletion_rate = (on_food < 0.3) ? 1.0 : 0.5;
    satiety_ -= satiety_ * dt_ * depletion_rate / satiety_tau_deplete_;
    if (satiety_ < 0.0) satiety_ = 0.0;
    if (satiety_ > 1.0) satiety_ = 1.0;

    int n = static_cast<int>(neurons_.size());

    // Effect 2: Satiety excites RIC
    double ric_baseline = 5.0;
    double ric_satiety = 10.0 * satiety_;
    for (int rid : nids("RIC")) {
        if (rid >= 0 && rid < n) {
            neurons_[rid]->add_synaptic_current(ric_baseline + ric_satiety);
        }
    }

    // Effect 3: Satiety suppresses chemotaxis (ASE/AWC)
    if (satiety_ > 0.3) {
        double suppress = -5.0 * (satiety_ - 0.3) / 0.7;
        const auto& ninfos = connectome_.neuron_infos();
        for (size_t i = 0; i < chemo_mappings_.size(); ++i) {
            int nid = chemo_mappings_[i].neuron_id;
            if (nid < 0 || nid >= n) continue;
            if (starts_with(ninfos[nid].name, "ASE") || starts_with(ninfos[nid].name, "AWC")) {
                neurons_[nid]->add_synaptic_current(suppress);
            }
        }
    }
}

// ================================================================
// Step 76: Enhanced Slowing Response — ESR (Sawin 2000 Neuron)
//
// Biology: Starved worms slow dramatically more on food than well-fed worms.
//   - BSR (well-fed): DA-mediated, ~30% speed reduction (Step 68, emergent)
//   - ESR (starved): 5-HT-mediated, ~80% speed reduction (this step)
//   - Requires tph-1 (5-HT synthesis) and mod-1 + ser-4 receptors
//
// Mechanism: Starvation upregulates MOD-1/SER-4 receptor expression
//   → same 5-HT concentration produces stronger inhibitory effect
//   → AIY/PVC more inhibited → less forward drive → speed drops
//
// Implementation: hunger × 5-HT concentration → extra inhibitory current
//   on AIY (forward interneuron) and PVC (forward command).
//   Speed change EMERGES from circuit effects, not direct manipulation.
//
// REF: Sawin 2000 Neuron — ESR discovery, tph-1/cat-2 dissociation
//      Gürel 2012 Genetics — mod-1;ser-4 double mutant abolishes ESR
//      Flavell 2013 Cell — 5-HT promotes dwelling via MOD-1 on AIY
// ================================================================
void SimulationEngine::apply_esr_modulation() {
    double sht = neuromod_.get_concentration("5-HT");
    double hunger = 1.0 - satiety_;  // 0=well-fed, 1=starved

    // 1. Update slow receptor upregulation level
    // Biology: starvation → transcriptional upregulation of MOD-1/SER-4 receptors
    // This takes tens of minutes in vivo; modeled as 60s tau for simulation timescale
    // esr_receptor_level_ starts at 0 → prevents ESR at simulation start (satiety=0)
    if (hunger > 0.5) {
        // Starving: receptors slowly upregulate
        double target = (hunger - 0.5) / 0.5;  // 0 at hunger=0.5, 1 at hunger=1.0
        esr_receptor_level_ += (target - esr_receptor_level_) * dt_ / esr_upregulate_tau_;
    } else {
        // Well-fed: receptors slowly downregulate
        esr_receptor_level_ -= esr_receptor_level_ * dt_ / esr_downregulate_tau_;
    }
    if (esr_receptor_level_ < 0.0) esr_receptor_level_ = 0.0;
    if (esr_receptor_level_ > 1.0) esr_receptor_level_ = 1.0;

    // 2. ESR current = gain × receptor_level × 5-HT_concentration
    // At full ESR (receptor=1, 5-HT=0.5): -8 × 1.0 × 0.5 = -4pA extra per target
    // Stacks with existing MOD-1 tonic: AIY gets -2.5×0.5 + (-4) = -5.25pA total
    // At simulation start (receptor=0): no extra current → unchanged behavior
    double esr_current = esr_mod1_gain_ * esr_receptor_level_ * sht;

    int n = static_cast<int>(neurons_.size());

    // MOD-1 upregulation on AIY — enhanced forward drive suppression
    // Gürel 2012: mod-1 expressed in AIY, required for ESR
    for (int id : nids("AIY")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(esr_current);
    }

    // MOD-1 upregulation on PVC — enhanced forward command suppression
    // PVC→AVB is a major forward drive pathway; 5-HT↑ + hunger → PVC more inhibited
    for (int id : nids("PVC")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(esr_current);
    }

    // SER-4 upregulation on RIC — enhanced OA suppression when starved on food
    // Prevents roaming state activation during ESR (should stay dwelling)
    // Weaker than MOD-1 effect (×0.5): SER-4 is modulatory, MOD-1 is primary
    for (int id : nids("RIC")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(esr_current * 0.5);
    }
}

// ================================================================
// Area-Restricted Search (Step 20d)
// ================================================================
void SimulationEngine::update_food_memory() {
    double food_conc = environment_.sample_food_density(body_.get_head_position());
    double on_food = food_conc / (food_conc + 0.1);

    double effective_decay_tau = food_memory_tau_decay_;
    // Step 104: sickness → suppress ARS (don't linger near toxic food)
    // Was 5000ms (5s) — too aggressive, food_memory dropped to 1e-24 in 300s
    // 60s gives ~2 time constants in 2min: fmem → ~13% of peak, then near-zero by 5min
    // REF: Zhang 2005 Nature — learned aversion develops over minutes, not seconds
    if (sickness_ > 0.3) {
        effective_decay_tau = 60000.0;
    }

    if (on_food > food_memory_ && sickness_ < 0.3) {
        food_memory_ += (on_food - food_memory_) * dt_ / food_memory_tau_rise_;
    } else {
        food_memory_ -= food_memory_ * dt_ / effective_decay_tau;
    }
    if (food_memory_ < 0.0) food_memory_ = 0.0;
    if (food_memory_ > 1.0) food_memory_ = 1.0;

    int n = static_cast<int>(neurons_.size());
    if (nid("AVAL") >= 0 && nid("AVAL") < n) {
        double ars_current = 4.0 * food_memory_;
        neurons_[nid("AVAL")]->add_synaptic_current(ars_current);
    }
    if (nid("DVA") >= 0 && nid("DVA") < n) {
        double ars_dva_current = 5.0 * food_memory_;
        neurons_[nid("DVA")]->add_synaptic_current(ars_dva_current);
    }
}

// ================================================================
// Gradient-Dependent Klinokinesis (Step 21d + Step 120)
// Two components:
//   A) Gradient-magnitude klinokinesis: no gradient → high pirouette (Calhoun 2014)
//   B) dC/dt directional klinokinesis: heading down-gradient → more reversals
//      REF: Pierce-Shimomura 1999 J Neurosci — pirouette rate ∝ -dC/dt
//           Iino & Yoshida 2009 J Neurosci — ASE temporal derivative sensing
//           Chalasani 2007 Nature — AWC OFF-response triggers pirouettes
// ================================================================
void SimulationEngine::apply_gradient_klinokinesis() {
    Vector2d head_pos = body_.get_head_position();
    Vector2d grad = environment_.chemical_field().gradient(head_pos);
    double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

    // --- Component A: gradient-magnitude (position-dependent local search) ---
    // Step 93: Pathogen learning flips klinokinesis polarity
    // Naive (awc_pref>0): no gradient → high pirouette → local search (Calhoun 2014)
    // Sick  (awc_pref<0): ON gradient → high pirouette → escape food zone
    // REF: Zhang 2005 Nature — learned aversion reverses chemotaxis strategy
    //      Ha 2010 Neuron — AWC→AIB pathway mediates aversive pirouettes
    double pref = awc_pref_cached_;
    double kk_mag_current = 0.0;
    if (pref >= 0.0) {
        double no_signal_factor = fast_exp(-grad_mag / 0.002);
        kk_mag_current = 1.0 * no_signal_factor;
    } else {
        double on_signal_factor = 1.0 - fast_exp(-grad_mag / 0.002);
        kk_mag_current = 5.0 * on_signal_factor * (-pref);
    }

    // --- Component B: dC/dt directional klinokinesis (heading-dependent) ---
    // Temporal derivative of concentration experienced by the worm as it moves.
    // dC/dt = ∇C · v: positive when heading up-gradient, negative when down.
    // AWC is OFF-type: tonically active, suppressed by concentration increase.
    //   dC/dt > 0 → AWC suppressed → less AIB → less AVA → fewer reversals
    //   dC/dt < 0 → AWC activated → more AIB → more AVA → more reversals
    // We model this directly: negative dC/dt → positive AVA current.
    // Filter with τ=2s to match ASE/AWC sensory adaptation timescale.
    // REF: Pierce-Shimomura 1999 — pirouette rate modulated by -dC/dt
    double concentration = environment_.sample_chemical(head_pos);
    double raw_dCdt = (concentration - prev_concentration_) / (dt_ * 0.001);  // per second
    prev_concentration_ = concentration;
    double tau_dCdt = 2000.0;  // 2s filter, matches ASE adaptation (Suzuki 2008)
    dCdt_filtered_ += (raw_dCdt - dCdt_filtered_) * dt_ / tau_dCdt;

    // Convert dC/dt to AVA current — ASYMMETRIC (biological):
    //   dC/dt < 0 (down-gradient) → excite AVA → more reversals (pirouettes)
    //   dC/dt > 0 (up-gradient) → NO suppression (baseline stochastic reversal rate)
    // AWC is OFF-type: tonically active, fires MORE when concentration DROPS.
    // AWC does NOT actively inhibit downstream when concentration rises — it simply
    // returns to tonic baseline. Symmetric suppression would chronically suppress
    // AVA when heading toward food, disrupting motor pattern and reducing speed.
    // REF: Chalasani 2007 Nature — AWC OFF-response triggers pirouettes
    //      Suzuki 2008 — asymmetric sensory processing in C. elegans
    double kk_dCdt_gain = 300.0;  // pA / (conc/s)
    double kk_dCdt_current = 0.0;
    if (pref >= 0.0) {
        // Naive: only excite AVA when dC/dt < 0 (going down-gradient)
        if (dCdt_filtered_ < 0.0) {
            kk_dCdt_current = -dCdt_filtered_ * kk_dCdt_gain;  // positive current
            kk_dCdt_current = std::min(kk_dCdt_current, 3.0);  // clamp max excitation
        }
    } else {
        // Sick/aversive: excite AVA when dC/dt > 0 (approaching noxious source)
        if (dCdt_filtered_ > 0.0) {
            kk_dCdt_current = dCdt_filtered_ * kk_dCdt_gain;
            kk_dCdt_current = std::min(kk_dCdt_current, 3.0);
        }
    }

    double kk_total = kk_mag_current + kk_dCdt_current;

    int n = static_cast<int>(neurons_.size());
    if (nid("AVAL") >= 0 && nid("AVAL") < n) neurons_[nid("AVAL")]->add_synaptic_current(kk_total);
    if (nid("AVAR") >= 0 && nid("AVAR") < n) neurons_[nid("AVAR")]->add_synaptic_current(kk_total);
}

// ================================================================
// Step 27: Sleep / Quiescence (Lethargus)
// ================================================================
void SimulationEngine::update_fatigue() {
    double speed = body_.get_speed();
    double activity = std::min(speed / 0.2, 1.0);

    if (!is_sleeping_) {
        // Step 62: Learning-induced sleep pressure (Chouhan 2023 Cell)
        // Aversive experience (toxin ingestion) → ALA-dependent sleep induction
        // learning_sleep_drive_ accumulates during toxin exposure → adds to fatigue
        double learn_drive = learning_sleep_drive_ * 2.0; // scale: 0.5 max → +1.0 rate
        fatigue_ += (activity + learn_drive) * dt_ / fatigue_tau_rise_;
    } else {
        fatigue_ -= fatigue_ * dt_ / fatigue_tau_decay_;
    }
    if (fatigue_ < 0.0) fatigue_ = 0.0;
    if (fatigue_ > 1.0) fatigue_ = 1.0;

    // Step 62: Forced sleep override (--sleep-after-learning experiment)
    if (forced_sleep_end_ > current_time_) {
        is_sleeping_ = true;
        fatigue_ = std::max(fatigue_, fatigue_threshold_); // keep fatigue high
    } else if (!is_sleeping_ && fatigue_ > fatigue_threshold_) {
        is_sleeping_ = true;
    } else if (is_sleeping_ && fatigue_ < 0.15) {
        is_sleeping_ = false;
    }

    int n = static_cast<int>(neurons_.size());
    if (nid("RIS") >= 0 && nid("RIS") < n) {
        double fatigue_drive = 40.0 / (1.0 + fast_exp(-12.0 * (fatigue_ - fatigue_threshold_)));
        double sleep_maintenance = is_sleeping_ ? 25.0 : 0.0;
        double ris_drive = 2.0 + fatigue_drive + sleep_maintenance;
        double ris_V = neurons_[nid("RIS")]->get_membrane_potential();
        double ris_release = 1.0 / (1.0 + fast_exp(-(ris_V - (-35.0)) / 5.0));
        double self_inhibition = -3.0 * ris_release;
        neurons_[nid("RIS")]->set_external_current(ris_drive + self_inhibition);
    }
}

void SimulationEngine::apply_sleep_effects() {
    // Step 71: P0-6 fix — all FLP-11 sleep effects now handled by
    // NeuromodulationManager (7th modulator: FLP-11).
    // RIS → FLP-11 release → DMSR-1 (Gi/o) on cholinergic neurons → inhibition
    // Self-inhibition: DMSR-1 on RIS → negative feedback → sleep homeostasis
    // REF: Turek 2016 eLife, Rossi 2025 Current Biology
    //
    // REMOVED: direct current injection to AVA/AVB (-15pA), MC (-12pA),
    // head_motor (-20pA), body motor neurons (-30pA).
    // These are now FLP-11 EXCITABILITY targets in setup_neuromodulation().
    //
    // is_sleeping_ state is still maintained by update_fatigue() for:
    // - Sleep-dependent learning (Step 62: learning rate ×2, forgetting ×0.3)
    // - DMP suppression during sleep
    // - Diagnostic reporting
}

// ================================================================
// Step 56: Defecation Motor Program (DMP)
// ================================================================
// Intestinal Ca²⁺ oscillator (IP3/ITR-1) → ~45s rhythm → AVL/DVB activation
// Three motor steps executed sequentially:
//   pBoc (0-1s):   posterior body contraction — non-neural (Ca²⁺ wave direct)
//   aBoc (1.5-2.5s): anterior body contraction — requires AVL (non-GABA)
//   Exp  (2.5-3.5s): enteric muscle contraction — AVL+DVB GABA → EXP-1
// Modulation: off food → no DMP; sleep → suppressed; 5-HT → slightly longer period
// Touch/reversal resets timer (Thomas 1990, Liu & Thomas 1994)
// REF: Thomas 1990 Genetics, Dal Santo 1999, Jiang 2022 Nat Commun
void SimulationEngine::update_defecation() {
    int n = static_cast<int>(neurons_.size());
    int avl_id = nid("AVL");
    int dvb_id = nid("DVB");
    if (avl_id < 0 || dvb_id < 0) return;

    // DMP only active on food (intestinal Ca²⁺ oscillator requires feeding)
    // REF: Liu & Thomas 1994 — off lawn, DMP not expressed
    Vector2d head_pos = body_.get_head_position();
    double food = environment_.sample_food_density(head_pos);
    bool on_food = (food > 0.2);

    // During sleep: suppress DMP (RIS global inhibition already handles AVL)
    if (is_sleeping_) {
        // Timer still advances slowly (biological: clock maintains phase)
        dmp_timer_ += dt_ * 0.3;  // 30% rate during sleep
        return;
    }

    // NOTE: biological touch reset (ALM/PLM gentle touch → timer=0) not modeled here
    // Our reversals are chemotaxis/nociceptive-driven, not touch-specific
    // REF: Thomas 1990 — gentle touch resets defecation phase (separate from reversal)

    // Advance intestinal pacemaker timer (autonomous oscillator, always runs)
    // REF: Liu & Thomas 1994 — clock phase maintained even off food
    dmp_timer_ += dt_;

    // 5-HT modulation: higher serotonin → slightly longer period
    // REF: Ségalat 1995 — exogenous 5-HT inhibits EMCs
    double sht = neuromod_.get_concentration("5-HT");
    double effective_period = dmp_period_ * (1.0 + 0.15 * sht);  // up to ~15% longer

    // Trigger new DMP cycle when timer exceeds period (only on food)
    // Off food: timer resets but motor program not expressed (Liu & Thomas 1994)
    if (dmp_timer_ >= effective_period) {
        dmp_timer_ = 0.0;
        if (!dmp_active_ && on_food) {
            dmp_active_ = true;
            dmp_phase_timer_ = 0.0;
        }
    }

    // Execute 3-phase DMP motor program
    if (dmp_active_) {
        dmp_phase_timer_ += dt_;

        // Phase 1: pBoc (0–1000ms) — posterior body contraction
        // Non-neural: intestinal Ca²⁺ wave directly contracts posterior body wall
        // Modeled as brief speed reduction (body shortens posteriorly)
        if (dmp_phase_timer_ < 1000.0) {
            // Step 71: speed reduction emerges from AVL/DVB GABA → B-class MN inhibition
            // (no direct speed_factor — P0-5 fix)
        }
        // Phase 2: aBoc (1500–2500ms) — anterior body contraction
        // Requires AVL (non-redundant, non-GABA mechanism)
        // AVL receives intestinal signal (AEX-5 peptide → AEX-2 GPCR → Gsα)
        else if (dmp_phase_timer_ >= 1500.0 && dmp_phase_timer_ < 2500.0) {
            double aboc_drive = 50.0;  // Strong activation for AP firing
            if (avl_id < n) neurons_[avl_id]->set_external_current(aboc_drive);
            // Step 71: AVL GABA → VB05/DB05 inhibition → emergent anterior slowing
        }
        // Phase 3: Exp/EMC (2500–3500ms) — enteric muscle contraction
        // AVL + DVB fire synchronized GABA APs → EXP-1 on enteric muscles
        // REF: Jiang 2022 — compound APs (UNC-2 Ca²⁺ + EXP-2 K⁺)
        else if (dmp_phase_timer_ >= 2500.0 && dmp_phase_timer_ < 3500.0) {
            double exp_drive = 70.0;  // Maximal drive for AP burst
            if (avl_id < n) neurons_[avl_id]->set_external_current(exp_drive);
            if (dvb_id < n) neurons_[dvb_id]->set_external_current(exp_drive);
            // Step 71: AVL+DVB maximal GABA → strongest MN inhibition → emergent pause
        }
        // Inter-phase and post-DMP: baseline drive
        else {
            if (avl_id < n) neurons_[avl_id]->set_external_current(1.0);
            if (dvb_id < n) neurons_[dvb_id]->set_external_current(1.0);
        }

        // DMP complete after 4s
        if (dmp_phase_timer_ >= 4000.0) {
            dmp_active_ = false;
            dmp_phase_timer_ = -1.0;
            dmp_count_++;
            // Reset baseline currents
            if (avl_id < n) neurons_[avl_id]->set_external_current(1.0);
            if (dvb_id < n) neurons_[dvb_id]->set_external_current(1.0);
        }
    } else {
        // Between DMP cycles: low baseline (AVL/DVB are quiet)
        if (avl_id < n) neurons_[avl_id]->set_external_current(1.0);
        if (dvb_id < n) neurons_[dvb_id]->set_external_current(1.0);
    }
}

// ================================================================
// Step 63: INS-1 Insulin Signaling (Lin 2010 JNeurosci)
// ================================================================
// INS-1 is released from ASI and AIA as a starvation/sickness signal.
// Release increases when: (1) food is ABSENT (starvation), (2) sickness is HIGH.
// INS-1 acts via DAF-2 receptor on AWC, AIA, AIY to reduce chemotaxis gain.
// This creates "anorexia": sick worms approach food less → avoid toxic food.
// REF: Lin 2010 — INS-1 from ASI+AIA, DAF-2 on AWC switches attraction→avoidance
//      Comm Bio 2022 — INS-1 from AIA → DAF-2c on ASER (taste avoidance)
//      You 2008 — insulin pathway required for pathogen avoidance behavior
void SimulationEngine::update_ins1() {
    // INS-1 release rate:
    //   - Baseline: proportional to (1 - satiety) → more release when hungry
    //   - Sickness boost: × (1 + sickness × gain) → sick worms release more
    //   - On food but sick: satiety stays moderate, sickness drives INS-1 up
    double starvation_signal = 1.0 - satiety_;  // 0 when full, 1 when hungry
    double sickness_boost = 1.0 + sickness_ * ins1_sickness_gain_;  // 1→4 at max sickness
    double target = starvation_signal * sickness_boost;
    if (target > 1.0) target = 1.0;

    // First-order dynamics (neuropeptide: slow, τ=10s)
    ins1_conc_ += (target - ins1_conc_) * dt_ / ins1_tau_;
    if (ins1_conc_ < 0.0) ins1_conc_ = 0.0;
    if (ins1_conc_ > 1.0) ins1_conc_ = 1.0;
}

void SimulationEngine::apply_ins1_modulation() {
    if (ins1_conc_ < 0.01) return;  // skip if negligible

    int n = static_cast<int>(neurons_.size());

    // Target 1: AWC — DAF-2 → reduces excitability (attraction→avoidance switch)
    // High INS-1 → AWC less excitable → weaker chemotaxis toward food odor
    // REF: Lin 2010 — DAF-2 in AWC switches signaling mode
    for (int id : nids("AWC")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(ins1_awc_gain_ * ins1_conc_);
        }
    }

    // Target 2: AIA — DAF-2 → reduces chemotaxis relay
    // AIA is a key hub: AWC→AIA→AIY. INS-1 dampens this relay.
    for (int id : nids("AIA")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(ins1_aia_gain_ * ins1_conc_);
        }
    }

    // Target 3: AIY — DAF-2 → reduces forward drive
    // Lower AIY activity → less AVB drive → slower forward locomotion
    for (int id : nids("AIY")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(ins1_aiy_gain_ * ins1_conc_);
        }
    }
}

} // namespace celegans
