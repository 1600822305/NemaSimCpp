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
// Gradient-Dependent Klinokinesis (Step 21d)
// ================================================================
void SimulationEngine::apply_gradient_klinokinesis() {
    Vector2d head_pos = body_.get_head_position();
    Vector2d grad = environment_.chemical_field().gradient(head_pos);
    double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

    // Step 93: Pathogen learning flips klinokinesis polarity
    // Naive (awc_pref>0): no gradient → high pirouette → local search (Calhoun 2014)
    // Sick  (awc_pref<0): ON gradient → high pirouette → escape food zone
    // REF: Zhang 2005 Nature — learned aversion reverses chemotaxis strategy
    //      Ha 2010 Neuron — AWC→AIB pathway mediates aversive pirouettes
    double pref = awc_pref_cached_;
    double kk_current = 0.0;
    if (pref >= 0.0) {
        // Naive: weak/no gradient → excite AVA → more pirouettes (explore)
        double no_signal_factor = fast_exp(-grad_mag / 0.002);
        kk_current = 1.0 * no_signal_factor;
    } else {
        // Sick: strong gradient (near food) → excite AVA → pirouettes to escape
        // Inverted: grad_mag high → kk_current high (opposite of naive)
        // Step 93: 2→5 pA base, stronger escape drive near food
        // REF: Ha 2010 — AWC→AIB aversive pirouettes are vigorous
        double on_signal_factor = 1.0 - fast_exp(-grad_mag / 0.002);
        kk_current = 5.0 * on_signal_factor * (-pref);  // scale by aversion strength
    }

    int n = static_cast<int>(neurons_.size());
    if (nid("AVAL") >= 0 && nid("AVAL") < n) neurons_[nid("AVAL")]->add_synaptic_current(kk_current);
    if (nid("AVAR") >= 0 && nid("AVAR") < n) neurons_[nid("AVAR")]->add_synaptic_current(kk_current);
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

// ================================================================
// Step 127: Olfactory adaptation — AWC EGL-4/PKG pathway
// Prolonged odor → cGMP → EGL-4 activation → TAX-2 phosphorylation (short-term)
//                                            → nuclear translocation (long-term)
// Two-phase model:
//   Phase 1 (short-term, ~30s): EGL-4 cytoplasmic activation → AWC gain ×0.5
//   Phase 2 (long-term, ~90s): EGL-4 nuclear entry → AWC gain ×0.15
// Recovery: odor removal → EGL-4 nuclear exit (slow, ~60s) → gain recovery
//
// REF: L'Etoile 2002 Neuron — EGL-4 required for olfactory adaptation
//      O'Halloran 2010 PNAS — EGL-4 nuclear translocation instructs long-term
//      Colbert & Bargmann 1995 — odor-specific adaptation in AWC
// ================================================================
void SimulationEngine::update_olfactory_adaptation() {
    int n = static_cast<int>(neurons_.size());

    // 1. Measure AWC activity (proxy for odor stimulation)
    double awc_activity = 0.0;
    int awc_count = 0;
    for (int id : nids("AWC")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            awc_activity += 1.0 / (1.0 + fast_exp(-(v - (-35.0)) / 5.0));
            awc_count++;
        }
    }
    if (awc_count > 0) awc_activity /= awc_count;

    // 2. Accumulate odor exposure (cGMP buildup in AWC)
    // Rises when AWC is active (odor present), decays when inactive
    double exposure_tau_rise = 15000.0;   // 15s to build up (compressed from ~10min)
    double exposure_tau_decay = 30000.0;  // 30s to clear (compressed from ~60min)
    if (awc_activity > 0.3) {
        awc_odor_exposure_ += (awc_activity - awc_odor_exposure_) * dt_ / exposure_tau_rise;
    } else {
        awc_odor_exposure_ -= awc_odor_exposure_ * dt_ / exposure_tau_decay;
    }
    if (awc_odor_exposure_ < 0.0) awc_odor_exposure_ = 0.0;
    if (awc_odor_exposure_ > 1.0) awc_odor_exposure_ = 1.0;

    // 3. EGL-4 dynamics: cytoplasmic → nuclear translocation
    // Short-term: cytoplasmic EGL-4 activates when exposure > 0.3
    // Long-term: nuclear entry when exposure > 0.6 (sustained stimulation)
    double nuclear_rate = 0.0;
    if (awc_odor_exposure_ > 0.6) {
        // High sustained exposure → EGL-4 enters nucleus
        nuclear_rate = (awc_odor_exposure_ - 0.6) / 0.4;  // 0→1 as exposure 0.6→1.0
        double tau_nuclear_entry = 30000.0;  // 30s (compressed from 60-90min)
        egl4_nuclear_ += nuclear_rate * (1.0 - egl4_nuclear_) * dt_ / tau_nuclear_entry;
        egl4_cytoplasmic_ = 1.0 - egl4_nuclear_;
    } else if (awc_odor_exposure_ < 0.2) {
        // Low exposure → EGL-4 exits nucleus (recovery)
        double tau_nuclear_exit = 60000.0;  // 60s recovery (compressed from hours)
        egl4_nuclear_ -= egl4_nuclear_ * dt_ / tau_nuclear_exit;
        if (egl4_nuclear_ < 0.0) egl4_nuclear_ = 0.0;
        egl4_cytoplasmic_ = 1.0 - egl4_nuclear_;
    }

    // 4. Compute AWC adaptation gain
    // Short-term: cytoplasmic EGL-4 phosphorylates TAX-2 → moderate reduction
    double short_term_factor = 1.0;
    if (awc_odor_exposure_ > 0.3) {
        // TAX-2 phosphorylation: gain drops to ~0.5 at full short-term
        double st_strength = std::min(1.0, (awc_odor_exposure_ - 0.3) / 0.3);
        short_term_factor = 1.0 - 0.5 * st_strength;  // 1.0 → 0.5
    }
    // Long-term: nuclear EGL-4 → further reduction
    double long_term_factor = 1.0 - 0.7 * egl4_nuclear_;  // 1.0 → 0.3 at full nuclear

    awc_adapt_gain_ = short_term_factor * long_term_factor;
    if (awc_adapt_gain_ < 0.1) awc_adapt_gain_ = 0.1;  // never fully zero

    // 5. Apply gain modulation to AWC neurons
    // Hyperpolarize AWC proportional to adaptation (reduce responsiveness)
    double adapt_current = -12.0 * (1.0 - awc_adapt_gain_);  // 0 to -10.8pA
    for (int id : nids("AWC")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(adapt_current);
        }
    }
}

// ================================================================
// Step 125: Molting quiescence / Lethargus (Raizen 2008, Monsalve 2011)
// LIN-42/Period oscillator → ecdysone-like steroid hormone → RIS/ALA activation → lethargus
// Lethargus occurs before each molt: locomotion+feeding suppressed, arousal reduced.
// Real cycle: ~6-8hr per larval stage. Compressed: 200s cycle in simulation.
// Hormone peaks → RIS drive + ALA drive → FLP-11 + FLP-13 → global quiescence
// Lethargus phase: ~20% of cycle (40s out of 200s, matching ~1-2hr of 8hr)
//
// REF: Monsalve 2011 — LIN-42/Period controls molting timing + quiescence
//      Raizen 2008 — lethargus satisfies all criteria for sleep
//      Katz 2018 — CEPsh glia modulate ALA→AVE during lethargus
//      Singh 2011 — Notch signaling in larval molting quiescence
// ================================================================
void SimulationEngine::update_molting_cycle() {
    // LIN-42/Period oscillator: advances phase at constant rate
    double omega = 2.0 * 3.14159265 / molt_period_;
    molt_phase_ += omega * dt_;
    if (molt_phase_ > 2.0 * 3.14159265) molt_phase_ -= 2.0 * 3.14159265;

    // Ecdysone-like steroid hormone: peaks just before molt
    // Modeled as sharp peak near phase=0 (using cos²)
    // Lethargus occupies ~20% of cycle centered on phase=0
    double cos_phase = std::cos(molt_phase_);
    double hormone_raw = (cos_phase > 0.8) ? (cos_phase - 0.8) / 0.2 : 0.0;  // narrow peak
    molt_hormone_ += (hormone_raw - molt_hormone_) * dt_ / 3000.0;  // 3s smoothing
    if (molt_hormone_ < 0.0) molt_hormone_ = 0.0;
    if (molt_hormone_ > 1.0) molt_hormone_ = 1.0;

    // Lethargus entry/exit based on hormone level
    if (!in_lethargus_ && molt_hormone_ > 0.4) {
        in_lethargus_ = true;
    } else if (in_lethargus_ && molt_hormone_ < 0.1) {
        in_lethargus_ = false;
    }

    if (!in_lethargus_) return;

    int n = static_cast<int>(neurons_.size());

    // 1. Drive RIS (sleep neuron) via molting hormone
    // Hormone → RIS activation → FLP-11 release → systemic quiescence
    double ris_molt_drive = 15.0 * molt_hormone_;
    if (nid("RIS") >= 0 && nid("RIS") < n) {
        neurons_[nid("RIS")]->add_synaptic_current(ris_molt_drive);
    }

    // 2. Drive ALA (stress-sleep neuron) — also active during lethargus
    // ALA → AVE inhibition → locomotion suppression (Katz 2018)
    double ala_molt_drive = 10.0 * molt_hormone_;
    for (int id : nids("ALA")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(ala_molt_drive);
    }

    // 3. Suppress pharyngeal pumping during lethargus
    // Feeding is suppressed during molt (pharynx remodeling)
    double mc_suppress = -20.0 * molt_hormone_;
    for (int id : nids("MC")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(mc_suppress);
    }

    // 4. Force sleep state if hormone is high enough
    // Lethargus is obligatory sleep — overrides normal fatigue threshold
    if (molt_hormone_ > 0.5) {
        is_sleeping_ = true;
        fatigue_ = std::max(fatigue_, 0.6);
    }
}

// ================================================================
// Step 124: Nictation — Dauer-specific dispersal behavior (Lee 2011 Nat Neurosci)
// IL2 sensory neurons → RIG interneurons → cholinergic motor output
// Dauer worms stand on tail and wave head for host-finding/dispersal.
// In 2D: periodic bouts of enhanced head oscillation + locomotion pauses.
// Cycle: 4s waving (large SMD amplitude) + 4s pause (AVB suppressed)
// REF: Lee 2011 Nat Neurosci — IL2 ablation reduces nictation
//      Yim 2024 — RIG downstream of IL2, dauer-specific rewiring
//      Cassada & Russell 1975 — dauer locomotion characteristics
// ================================================================
void SimulationEngine::apply_nictation() {
    if (!is_dauer()) return;  // nictation is dauer-specific

    int n = static_cast<int>(neurons_.size());

    // Update nictation cycle timer
    nictation_timer_ += dt_;
    if (nictation_timer_ > nictation_period_) {
        nictation_timer_ -= nictation_period_;
    }
    // First half: waving phase; second half: pause/standing phase
    nictation_waving_ = (nictation_timer_ < nictation_period_ * 0.5);

    // 1. IL2 activation during dauer (dauer-specific cilia arborization)
    // IL2 neurons become mechanosensitive in dauer via OSM-9
    // Periodic bursting pattern drives nictation bouts
    double il2_drive = nictation_waving_ ? 12.0 : 3.0;  // waving: 12pA, pause: 3pA
    for (int id : nids("IL2")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(il2_drive);
    }

    // 2. RIG activation (downstream of IL2, dauer-strengthened connection)
    // RIG integrates IL2 input → drives head motor output
    int rigl = nid("RIGL");
    int rigr = nid("RIGR");
    if (nictation_waving_) {
        double rig_drive = 8.0;
        if (rigl >= 0 && rigl < n) neurons_[rigl]->add_synaptic_current(rig_drive);
        if (rigr >= 0 && rigr < n) neurons_[rigr]->add_synaptic_current(rig_drive);
    }

    // 3. Motor effects
    if (nictation_waving_) {
        // Waving phase: enhance head oscillation (increase SMD drive amplitude)
        // This creates large-amplitude head waving (scanning for hosts)
        double smd_boost = 6.0;
        for (int id : nids("SMD")) {
            if (id >= 0 && id < n)
                neurons_[id]->add_synaptic_current(smd_boost);
        }
    } else {
        // Pause/standing phase: suppress forward locomotion
        // Worm "stands" — no forward movement, just maintains position
        double avb_pause = -12.0;
        for (int id : nids("AVB")) {
            if (id >= 0 && id < n)
                neurons_[id]->add_synaptic_current(avb_pause);
        }
        // Also suppress AVA to prevent reversals during standing
        double ava_pause = -5.0;
        for (int id : nids("AVA")) {
            if (id >= 0 && id < n)
                neurons_[id]->add_synaptic_current(ava_pause);
        }
    }
}

// ================================================================
// Step 123: Arousal threshold modulation (Schwarz 2011 Cell Rep)
// FLP-11 concentration determines arousal threshold during sleep.
// Multilevel circuit depression: ASH dampened + AVA/AVD desynchronized.
// Strong stimuli (touch, nociception) can overcome threshold → wake.
// REF: Schwarz 2011 Cell Rep — multilevel modulation of ASH avoidance circuit
//      Raizen 2008 — arousal threshold during lethargus
//      Cho & Bhatt 2006 — graded sensory gating
// ================================================================
void SimulationEngine::update_arousal_threshold() {
    if (!is_sleeping_) {
        // Awake: threshold decays to zero
        arousal_threshold_ -= arousal_threshold_ * dt_ / 2000.0;  // 2s decay
        if (arousal_threshold_ < 0.0) arousal_threshold_ = 0.0;
        return;
    }

    // Sleeping: arousal threshold tracks FLP-11 concentration
    // FLP-11 conc from neuromodulation manager
    double flp11 = neuromod_.get_concentration("FLP-11");
    // Threshold = FLP-11 × fatigue (deeper sleep = higher threshold)
    double target = flp11 * std::min(1.0, fatigue_ / fatigue_threshold_);
    arousal_threshold_ += (target - arousal_threshold_) * dt_ / 5000.0;  // 5s smoothing
    if (arousal_threshold_ > 1.0) arousal_threshold_ = 1.0;
}

void SimulationEngine::apply_arousal_gating() {
    if (arousal_threshold_ < 0.05) return;  // awake or very light sleep

    int n = static_cast<int>(neurons_.size());
    double gate = arousal_threshold_;  // 0=no gating, 1=max gating

    // 1. ASH sensory dampening (Schwarz 2011: Ca2+ response reduced in lethargus)
    // Hyperpolarize ASH proportional to sleep depth
    // At deep sleep (gate=0.7): -15pA × 0.7 = -10.5pA → ASH needs >10.5pA extra to respond
    double ash_dampen = -15.0 * gate;
    for (int id : nids("ASH")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(ash_dampen);
    }

    // 2. AVA/AVD interneuron dampening (loss of synchrony modeled as inhibition)
    // "activity of corresponding interneurons becomes asynchronous"
    // At deep sleep: -8pA per neuron → raises reversal threshold
    double cmd_dampen = -8.0 * gate;
    for (int id : nids("AVA")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(cmd_dampen);
    }
    for (int id : nids("AVD")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(cmd_dampen);
    }

    // 3. Stimulus-dependent arousal: check if sensory drive exceeds threshold
    // Touch neurons (ALM/PLM/AVM) provide strong input (~80pA)
    // If total sensory drive to AVA exceeds arousal threshold → wake up
    double touch_activity = 0.0;
    int touch_count = 0;
    for (int id : nids("ALM")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            touch_activity += 1.0 / (1.0 + fast_exp(-(v - (-30.0)) / 5.0));
            touch_count++;
        }
    }
    for (int id : nids("PLM")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            touch_activity += 1.0 / (1.0 + fast_exp(-(v - (-30.0)) / 5.0));
            touch_count++;
        }
    }
    if (touch_count > 0) touch_activity /= touch_count;

    // ASH nociceptive drive
    double ash_activity = 0.0;
    int ash_count = 0;
    for (int id : nids("ASH")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            ash_activity += 1.0 / (1.0 + fast_exp(-(v - (-30.0)) / 5.0));
            ash_count++;
        }
    }
    if (ash_count > 0) ash_activity /= ash_count;

    // Combined sensory arousal signal
    double arousal_signal = std::max(touch_activity, ash_activity);

    // If sensory signal exceeds arousal threshold → force wake
    // Touch at 80pA → activity ~0.9 >> threshold ~0.5 → wakes
    // Weak gradient change → activity ~0.1 < threshold → stays asleep
    if (arousal_signal > arousal_threshold_ * 0.8 && arousal_signal > 0.3) {
        is_sleeping_ = false;
        fatigue_ = std::max(fatigue_ * 0.5, 0.1);  // partial fatigue clear on forced wake
    }
}

// ================================================================
// Step 122: Dauer formation decision
// Environmental signals → ASI neuroendocrine output → DAF-2/DAF-16 → dauer/reproductive
//
// Dauer-promoting signals:
//   1. Low food (satiety < 0.2) → DAF-7/DAF-28 from ASI decrease
//   2. High pheromone (ascaroside) → ASJ/ASI detect crowding
//   3. High temperature (≥25°C) → stress signal
//
// Dauer-preventing signals:
//   1. Food present → ASI DAF-7↑, DAF-28↑ → DAF-2 active → DAF-16 cytoplasmic
//   2. Low pheromone → no crowding signal
//   3. Normal temperature (15-22°C)
//
// Decision: dauer_signal_ = integrated pro-dauer evidence [0,1]
//   dauer_signal_ > 0.8 → dauer state (cease feeding, reduce locomotion)
//
// REF: Golden & Riddle 1984 — pheromone/food/temp dauer decision
//      Hu 2007 PLoS Genet — DAF-7/DAF-28 from ASI
//      Fielenbach & Antebi 2008 — DAF-2/DAF-16 insulin/FOXO pathway
// ================================================================
void SimulationEngine::update_dauer_decision() {
    Vector2d head_pos = body_.get_head_position();
    double food_here = environment_.sample_food_density(head_pos);

    // --- 1. DAF-7/TGF-β from ASI: food-dependent ---
    // Food present → DAF-7 high (1.0) → reproductive
    // No food → DAF-7 low (→0.0) → promotes dauer
    double daf7_target = (food_here > 0.1) ? 1.0 : 0.1;
    daf7_level_ += (daf7_target - daf7_level_) * dt_ / 30000.0;  // 30s integration

    // --- 2. DAF-28/insulin from ASI: satiety-dependent ---
    // Well-fed → DAF-28 high → DAF-2 active → DAF-16 cytoplasmic → reproductive
    // Starving → DAF-28 low → DAF-2 inactive → DAF-16 nuclear → dauer
    double daf28_target = satiety_;
    daf28_level_ += (daf28_target - daf28_level_) * dt_ / 30000.0;

    // --- 3. Integrate pro-dauer signals ---
    // Low DAF-7 + low DAF-28 + high pheromone + high temp → dauer
    double food_pro_dauer = 1.0 - 0.5 * (daf7_level_ + daf28_level_);  // [0,1]

    // Pheromone contribution (ascaroside crowding signal)
    double pheromone_pro_dauer = 0.0;
    if (environment_.has_pheromone()) {
        double phero = environment_.sample_pheromone(head_pos);
        pheromone_pro_dauer = phero / (phero + 0.3);  // saturating, half-max at 0.3
    }

    // Temperature contribution: high temp (≥25°C) promotes dauer
    double temp = environment_.sample_temperature(head_pos);
    double temp_pro_dauer = 0.0;
    if (temp > 25.0) temp_pro_dauer = std::min(1.0, (temp - 25.0) / 2.0);

    // Combined pro-dauer signal (weighted average)
    // Food/starvation is dominant factor (weight 0.6)
    double pro_dauer = 0.6 * food_pro_dauer + 0.25 * pheromone_pro_dauer + 0.15 * temp_pro_dauer;

    // Slow integration toward decision (developmental timescale)
    dauer_signal_ += (pro_dauer - dauer_signal_) * dt_ / dauer_tau_;
    if (dauer_signal_ < 0.0) dauer_signal_ = 0.0;
    if (dauer_signal_ > 1.0) dauer_signal_ = 1.0;
}

// ================================================================
// Step 122: Dauer behavioral effects
// When dauer_signal_ > 0.8:
//   1. Pharyngeal pumping ceases (mouth sealed)
//   2. Locomotion reduced (energy conservation)
//   3. Chemosensory sensitivity altered
//   4. Stress resistance increased (modeled as sickness decay reduction)
// ================================================================
void SimulationEngine::apply_dauer_effects() {
    if (dauer_signal_ < 0.3) return;  // no effects below threshold

    int n = static_cast<int>(neurons_.size());

    // Graded suppression: starts at 0.3, full at 1.0
    double dauer_strength = (dauer_signal_ - 0.3) / 0.7;
    if (dauer_strength > 1.0) dauer_strength = 1.0;

    // 1. Suppress pharyngeal pumping (MC motor neuron inhibition)
    // Dauer larvae have sealed buccal cavity — no feeding
    double mc_suppress = -30.0 * dauer_strength;
    for (int id : nids("MC")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(mc_suppress);
    }

    // 2. Reduce locomotion (suppress AVB forward command)
    // Dauer larvae are quiescent, conserving energy
    double avb_suppress = -8.0 * dauer_strength;
    for (int id : nids("AVB")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(avb_suppress);
    }

    // 3. Enhance ASJ sensitivity (dauer pheromone detection for exit decision)
    // ASJ neurons mediate dauer recovery when conditions improve
    double asj_boost = 5.0 * dauer_strength;
    for (int id : nids("ASJ")) {
        if (id >= 0 && id < n)
            neurons_[id]->add_synaptic_current(asj_boost);
    }
}

} // namespace celegans
