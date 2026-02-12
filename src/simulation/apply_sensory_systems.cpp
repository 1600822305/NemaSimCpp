// apply_sensory_systems.cpp — Split from simulation_engine.cpp (Step 92)
// Contains: apply_sensory_input, apply_thermo_input, apply_tail_chemosensation,
//           apply_touch_stimulus, apply_sensitization
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <cstring>
#include <algorithm>
#include <cmath>

namespace celegans {

void SimulationEngine::apply_sensory_input() {
    int n = static_cast<int>(neurons_.size());

    // Head sweep sampling: nose position displaced by head curvature
    // This serves BOTH klinokinesis (slow trend) AND klinotaxis (phase-locked).
    // The same ASE neurons carry both signals; downstream circuits separate them:
    //   - Klinokinesis: ASE → AIA → AIB → AVA (slow pirouette modulation, 5s tau)
    //   - Klinotaxis: ASE → AIY → AIZ → SMB (fast neck bias, no adaptation)
    // REF: Izquierdo & Lockery 2010
    Vector2d head_pos = body_.get_head_position();
    double head_angle = body_.get_head_angle();
    double head_curv = body_.get_local_curvature(0);
    double sweep_radius = 1.5;  // mm, curv→displacement gain
    double lateral_offset = head_curv * sweep_radius;
    double nx = -std::sin(head_angle);
    double ny =  std::cos(head_angle);
    Vector2d sample_pos = {head_pos.x + lateral_offset * nx,
                           head_pos.y + lateral_offset * ny};
    double concentration = environment_.sample_chemical(sample_pos);

    // Step 23c: Satiety modulates chemosensory gain (Mori 1995, Tomioka 2006)
    // Sharp sigmoid switch at satiety=0.5:
    //   Hungry (sat<0.3): full chemotaxis (find food!)
    //   Fed (sat>0.7): chemotaxis nearly off (temperature priority)
    double sat_switch = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    double chemo_sat_gain = 1.0 - 0.85 * sat_switch;  // hungry: 1.0, fed: 0.15

    // Step 26b: Sickness suppresses chemosensory gain (illness-induced anorexia)
    // REF: DAF-7 (TGF-β) from ASI reduces food attraction during pathogen exposure
    // Reduces ASE/AWC/AWA neural drive when sick — weathervane unaffected (separate)
    double sick_suppression = 1.0 - 0.85 * sickness_;  // sick: 15% of normal drive

    // Food density at head (narrow σ=3mm for NSM/CEP food detectors)
    double food_density = environment_.sample_food_density(head_pos);

    for (auto& cm : chemo_mappings_) {
        if (cm.neuron_id < 0 || cm.neuron_id >= n) continue;
        // NSM/CEP: use narrow food density, NOT suppressed by sickness OR satiety
        // NSM/CEP detect physical food contact → drive 5-HT/DA unconditionally on food
        // Satiety modulation acts DOWNSTREAM (ASE/AWC/AWA chemotaxis gain), not on NSM
        // BUG FIX: chemo_sat_gain was suppressing NSM to 0.15 when fed → 5-HT=0.019
        double input_conc = cm.uses_food_density ? food_density : concentration;
        double I_sensory = cm.transducer.update(input_conc, dt_);
        double gain_mod = cm.uses_food_density ? 1.0 : (chemo_sat_gain * sick_suppression);
        I_sensory *= static_cast<double>(params.sensory_gain) * gain_mod;
        neurons_[cm.neuron_id]->set_external_current(I_sensory);
    }

    // Step 26b: ASE samples SOLUBLE field (salt/amino acids — independent of food odor)
    // REF: Bargmann 2006 — ASE detects water-soluble ions, not volatile odors
    double soluble_conc = environment_.sample_soluble(sample_pos);
    for (auto& sm : soluble_mappings_) {
        if (sm.neuron_id < 0 || sm.neuron_id >= n) continue;
        double I_sol = sm.transducer.update(soluble_conc, dt_);
        I_sol *= static_cast<double>(params.sensory_gain) * chemo_sat_gain;
        neurons_[sm.neuron_id]->set_external_current(I_sol);
    }

    // Step 25: ASH nociceptors sample repellent field
    // ASH is ON-type: excited by repellent concentration increase
    // REF: Summers 2015 — ASH→AIB→AVA nociceptive avoidance circuit
    double repellent_conc = environment_.sample_repellent(sample_pos);
    for (auto& nm : noci_mappings_) {
        if (nm.neuron_id < 0 || nm.neuron_id >= n) continue;
        double I_noci = nm.transducer.update(repellent_conc, dt_);
        I_noci *= static_cast<double>(params.sensory_gain);
        // No satiety modulation: nociception is not suppressed by feeding state
        // (5-HT suppresses downstream AIB instead — Summers 2015)
        neurons_[nm.neuron_id]->set_external_current(I_noci);
    }

    // Step 26: ADF serotonin neurons — driven by sickness state
    // REF: Zhang 2005 Nature — PA14 exposure → TPH-1 upregulation → ADF 5-HT↑
    // ADF baseline=2pA (low), sickness drives strong depolarization → 5-HT release
    for (int adf_id : nids("ADF")) {
        if (adf_id >= 0 && adf_id < n) {
            double I_adf = 0.5 + 30.0 * sickness_;  // 0.5pA baseline (silent), up to 30.5pA when sick
            neurons_[adf_id]->set_external_current(I_adf);
        }
    }

    // Step 43: AWB repulsive olfactory neurons — sense pathogen volatiles
    // AWB detects repulsive odors (1-undecene, serrawettin) at the food/toxin source
    // After aversive learning (sickness > 0), AWB response is amplified
    // AWB→AUA→AVA drives reflexive backward locomotion away from pathogen
    // AWB ⊣ AIY further suppresses approach
    // REF: Troemel 1997 Cell, Ha 2010 Neuron, BMC Biology 2022
    {
        Vector2d head_pos = body_.get_head_position();
        double repellent = environment_.sample_repellent(head_pos);
        for (int awb_id : nids("AWB")) {
            if (awb_id >= 0 && awb_id < n) {
                // Base response: low (2pA) — AWB mainly activated after learning
                // Learned amplification: sickness × repellent → strong AWB drive
                double I_awb = 2.0 + awb_pathogen_gain_ * sickness_ * repellent;
                neurons_[awb_id]->set_external_current(I_awb);
            }
        }
    }

    // Step 55: Light avoidance — LITE-1 photoreceptor on ASJ/ASK/AWB/ASH
    // C. elegans detects UV/blue light despite lacking eyes
    // LITE-1 → Gα → guanylate cyclase → cGMP → TAX-2/TAX-4 CNG → depolarization
    // REF: Ward 2008 Nat Neurosci — ASJ primary photoreceptor
    //      Liu 2010 — ASJ+ASK+AWB+ASH combinatorial light sensing
    //      Edwards 2008 — LITE-1 identified in genetic screen
    if (environment_.has_light()) {
        Vector2d head_pos = body_.get_head_position();
        double light = environment_.sample_light(head_pos);
        if (light > 0.01) {
            // ASJ: primary photoreceptor — strongest light response
            // Ward 2008: light evokes ~20pA inward current in ASJ via CNG channels
            // gain=60: at light=0.5 → 30pA, at light=1.0 → 60pA (strong reversal drive)
            // baseline=1pA: low spontaneous activity
            for (int asj_id : nids("ASJ")) {
                if (asj_id >= 0 && asj_id < n) {
                    double I_asj = 1.0 + 60.0 * light;
                    neurons_[asj_id]->set_external_current(I_asj);
                }
            }
            // ASK: secondary photoreceptor — weaker light response
            // Liu 2010: ASK contributes to phototaxis but less than ASJ
            // gain=30: half of ASJ (secondary role)
            for (int ask_id : nids("ASK")) {
                if (ask_id >= 0 && ask_id < n) {
                    double I_ask = 1.0 + 30.0 * light;
                    neurons_[ask_id]->set_external_current(I_ask);
                }
            }
            // AWB: additive light drive on top of repellent/pathogen drive
            // AWB LITE-1 expression confirmed (Liu 2010, eLife 2025)
            // Weaker than ASJ (tertiary role), gain=20
            for (int awb_id : nids("AWB")) {
                if (awb_id >= 0 && awb_id < n) {
                    double I_existing = neurons_[awb_id]->get_I_ext();
                    neurons_[awb_id]->set_external_current(I_existing + 20.0 * light);
                }
            }
            // ASH: additive light drive on top of nociceptive drive
            // ASH responds to light but less than ASJ (polymodal nociceptor)
            // gain=15: weakest photosensory contribution
            for (int ash_id : nids("ASH")) {
                if (ash_id >= 0 && ash_id < n) {
                    double I_existing = neurons_[ash_id]->get_I_ext();
                    neurons_[ash_id]->set_external_current(I_existing + 15.0 * light);
                }
            }
        }
    }

    // Step 64: Pheromone sensing via ADL (Jang 2012, Srinivasan 2008)
    // ADL detects ascaroside pheromones (ascr#3/C9) → avoidance in hermaphrodites
    // ADL already has repellent ON transduction (Step 61); pheromone is ADDITIVE
    // Circuit: ADL→AVA (reversal), ADL→AIA/AIB (chemotaxis modulation)
    // REF: Jang 2012 — ADL is primary ascr#3 sensor
    //      Srinivasan 2008 — ascarosides as social signals
    if (environment_.has_pheromone()) {
        Vector2d head_pos = body_.get_head_position();
        double pheromone = environment_.sample_pheromone(head_pos);
        if (pheromone > 0.01) {
            // ADL: TONIC pheromone response — sustained avoidance drive
            // gain=40 pA at saturating pheromone (half-max at conc=0.2)
            // REF: Jang 2012 — ADL calcium imaging shows sustained response to ascr#3
            for (int adl_id : nids("ADL")) {
                if (adl_id >= 0 && adl_id < n) {
                    double phr_drive = 40.0 * pheromone / (pheromone + 0.2);
                    double I_existing = neurons_[adl_id]->get_I_ext();
                    neurons_[adl_id]->set_external_current(I_existing + phr_drive);
                }
            }
        }
    }

    // Step 43: ADF sickness 5-HT → MOD-1 ⊣ AIY/AIZ
    // MOVED to post-reset section in step() — add_synaptic_current() would be
    // wiped by reset_synaptic_current() if called here.

    // Touch/other sensory: low baseline (no active stimulus)
    for (int id : other_sensory_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(sensory_baseline_);
        }
    }
}

void SimulationEngine::apply_thermo_input() {
    // Step 23: AFD thermosensory neurons sample temperature at head position
    // AFD responds to temperature relative to cultivation temperature (Tc)
    // AFD→AIY: excitatory, drives thermotaxis via shared AIY→RIA→SMD pathway
    // REF: Mori & Ohshima 1995, Clark 2006, Luo 2014 PNAS
    int n = static_cast<int>(neurons_.size());
    Vector2d head_pos = body_.get_head_position();
    double temperature = environment_.sample_temperature(head_pos);

    // Step 23c: Satiety modulates thermosensory gain (Mori 1995)
    // Sharp sigmoid switch at satiety=0.5:
    //   Hungry: weak thermotaxis (food priority)
    //   Fed: strong thermotaxis (navigate to cultivation temperature)
    double sat_switch_t = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    double thermo_sat_gain = 0.2 + 1.8 * sat_switch_t;  // hungry: 0.2, fed: 2.0

    // Step 80: Feeding-state gated Tc adaptation (Hedgecock & Russell 1975)
    // learn_signal > 0 (on food): Tc → current temp (positive association)
    // learn_signal < 0 (off food): Tc ← away from current temp (aversion)
    // Uses food presence at head (not satiety) — Chi 2007: food conditions affect
    // WHICH behavior is exhibited, and Tc memory is established at food location
    // REF: Chi 2007 J Exp Biol — food/temp independent mechanisms
    double food_here = environment_.sample_food_density(head_pos);
    double thermo_learn_signal = (food_here > 0.1) ? 0.5 : -0.3;
    bool thermo_learn_tick = (static_cast<int>(current_time_ / dt_) % 200 == 0);

    for (auto& tm : thermo_mappings_) {
        if (tm.neuron_id < 0 || tm.neuron_id >= n) continue;
        double I_thermo = tm.transducer.update(temperature, dt_);
        I_thermo *= thermo_sat_gain;
        // AFD current adds to (not replaces) any existing external current
        neurons_[tm.neuron_id]->add_synaptic_current(I_thermo);

        // Step 80: Tc learning (cell-autonomous in AFD, Nishida 2011)
        if (thermo_learn_tick) {
            tm.transducer.adapt_tc(thermo_learn_signal, temperature, 100.0);
        }
    }
}

// ================================================================
// Step 81: Tail chemosensation — PHB/PHA phasmid neurons
// PHB senses repellent at TAIL position → suppresses reversal (Hilliard 2002)
// PHA senses food at TAIL position → weak modulation
// Key function: directional escape via head-tail antagonism
//   ASH (head) detects repellent → reversal
//   PHB (tail) detects repellent → suppresses reversal → continue forward
// REF: Hilliard 2002 Curr Biol, Zou 2017 Sci Rep
// ================================================================
void SimulationEngine::apply_tail_chemosensation() {
    int n = static_cast<int>(neurons_.size());
    Vector2d tail_pos = body_.get_tail_position();

    // PHB: TONIC response to repellent at tail
    // gain=40: weaker than ASH (80) — tail is secondary nociceptor
    // half_max=0.3: more sensitive than ASH (0.5) — lower threshold
    double rep_at_tail = environment_.sample_repellent(tail_pos);
    double phb_drive = 40.0 * rep_at_tail / (rep_at_tail + 0.3) + 2.0;  // baseline 2pA
    if (phb_drive > 60.0) phb_drive = 60.0;
    for (int id : nids("PHB")) {
        if (id >= 0 && id < n) neurons_[id]->set_external_current(phb_drive);
    }

    // PHA: TONIC response to food at tail (weak, neuroendocrine)
    // Senses food quality / pheromone at tail position
    double food_at_tail = environment_.sample_food_density(tail_pos);
    double pha_drive = 10.0 * food_at_tail / (food_at_tail + 0.5) + 1.0;  // baseline 1pA
    for (int id : nids("PHA")) {
        if (id >= 0 && id < n) neurons_[id]->set_external_current(pha_drive);
    }
}

void SimulationEngine::apply_touch_stimulus() {
    // Step 18: Wall collision → touch neuron activation (Chalfie 1985)
    // Arena is 50×50 mm. When head approaches wall → anterior touch (ALM).
    // When tail approaches wall → posterior touch (PLM).
    int n = static_cast<int>(neurons_.size());
    auto head = body_.get_head_position();
    auto tail = body_.get_tail_position();
    double arena_w = 50.0, arena_h = 50.0;

    // Step 78: Reset all touch neuron I_ext to 0 at start of each step.
    // BUG FIX: set_external_current persists until changed. Without reset,
    // tap current (60pA) persists between taps → permanent vesicle depletion
    // → prevents STP-based habituation from emerging.
    for (int id : nids("ALM")) { if (id >= 0 && id < n) neurons_[id]->set_external_current(0.0); }
    for (int id : nids("PLM")) { if (id >= 0 && id < n) neurons_[id]->set_external_current(0.0); }
    { int avm = nid("AVM"); if (avm >= 0 && avm < n) neurons_[avm]->set_external_current(0.0); }
    for (int id : nids("OLQ")) { if (id >= 0 && id < n) neurons_[id]->set_external_current(0.0); }
    for (int id : nids("FLP")) { if (id >= 0 && id < n) neurons_[id]->set_external_current(0.0); }
    for (int id : nids("IL1")) { if (id >= 0 && id < n) neurons_[id]->set_external_current(0.0); }

    // Anterior touch: head near wall
    bool front_touch = (head.x < arena_margin_ || head.x > arena_w - arena_margin_ ||
                        head.y < arena_margin_ || head.y > arena_h - arena_margin_);

    // Posterior touch: tail near wall
    bool rear_touch = (tail.x < arena_margin_ || tail.x > arena_w - arena_margin_ ||
                       tail.y < arena_margin_ || tail.y > arena_h - arena_margin_);

    // Step 33: OLQ nose touch — closer range than ALM body touch
    // OLQ detects head proximity to wall (dist < 0.3mm vs ALM's 2mm)
    // 4 quadrant neurons: directional sensitivity based on which wall
    // OLQ→RMD head withdrawal, OLQ→RIC indirect reversal (weak)
    // NOT triggering full reversal — just head withdrawal + direction change
    // REF: Kaplan & Horvitz 1993, Hart 1995
    double nose_current = 30.0;  // pA, weaker than body touch (80pA)
    double dx_left  = head.x;                  // distance to left wall
    double dx_right = arena_w - head.x;        // distance to right wall
    double dy_bottom = head.y;                 // distance to bottom wall
    double dy_top   = arena_h - head.y;        // distance to top wall
    double heading = body_.get_head_angle();
    double cos_h = std::cos(heading);
    double sin_h = std::sin(heading);

    // Check each wall: activate quadrant-specific OLQ based on heading
    // OLQ naming: DL=dorsal-left, DR=dorsal-right, VL=ventral-left, VR=ventral-right
    // Simplified: activate all 4 OLQ when nose is near any wall
    // Direction selectivity emerges from OLQ→RMD ipsilateral mapping
    bool nose_touch = false;
    double min_wall_dist = std::min({dx_left, dx_right, dy_bottom, dy_top});
    if (min_wall_dist < nose_margin_ && !front_touch) {
        // Nose close to wall but not yet body-touch range
        // Scale current by proximity: closer = stronger
        double prox = 1.0 - min_wall_dist / nose_margin_;  // 0→1
        double olq_drive = nose_current * prox;
        for (int id : nids("OLQ")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(olq_drive);
            }
        }
        // Step 73: IL1 — directional nose touch for head withdrawal (Hart 1995)
        // IL1 has similar sensitivity range as OLQ; both drive RMD
        // IL1 current slightly weaker than OLQ (OLQ is "majority" of response)
        double il1_drive = nose_current * 0.7 * prox;
        for (int id : nids("IL1")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(il1_drive);
            }
        }
        // Step 73: FLP — gentle nose touch (facilitated by OLQ/CEP via RIH)
        // FLP gentle touch: weak intrinsic drive, amplified by hub-spoke network
        // Only responds to nose-range touch, NOT body-range (different from harsh)
        // 15pA × prox: weaker than OLQ (30pA); facilitation from RIH gap junctions
        //   provides the additional current to reach threshold
        // REF: Chatzigeorgiou 2011 — FLP nose touch requires OLQ facilitation
        double flp_gentle = 15.0 * prox;
        for (int id : nids("FLP")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(flp_gentle);
            }
        }
        nose_touch = true;
    }

    if (front_touch) {
        // Strong current pulse to ALM+AVM neurons → triggers reversal via ALM/AVM→AVD→AVA
        for (int id : nids("ALM")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(touch_current_);
            }
        }
        // Step 61: AVM anterior gentle touch (single neuron, same modality as ALM)
        int avm = nid("AVM");
        if (avm >= 0 && avm < n) neurons_[avm]->set_external_current(touch_current_);
        // Also activate OLQ at full strength during body touch
        for (int id : nids("OLQ")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(nose_current);
            }
        }
        // Step 73: FLP harsh touch — cell-autonomous via MEC-10 (DEG/ENaC)
        // Harsh touch (body collision): FLP responds strongly without facilitation
        // FLP accounts for 29% of nose touch avoidance (Kaplan 1993)
        // At body collision range: strong current drives AVA/AVD reversal directly
        // REF: Chatzigeorgiou 2011 — FLP harsh touch is MEC-10 dependent, cell-autonomous
        double flp_harsh = 50.0;  // pA, strong cell-autonomous response
        for (int id : nids("FLP")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(flp_harsh);
            }
        }
        // IL1 also activated at full strength during body touch
        for (int id : nids("IL1")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(nose_current * 0.7);
            }
        }
    }

    if (rear_touch) {
        // Strong current pulse to PLM neurons → triggers forward acceleration
        for (int id : nids("PLM")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(touch_current_);
            }
        }
    }

    // Step 47b: CEP binary tactile drive REMOVED.
    // CEP↔OLQ gap junctions cause cascade: 40pA CEP → OLQ→RMD (head disruption)
    // + OLQ→RIC (OA release) that destroys chemotaxis. CEP is now driven modestly
    // via chemo_mappings_ (gain=20, for DA→DVA/NLP-12 priming only).
    // Step 68: Basal slowing now via DA→DOP-3→B-class motor neurons (extrasynaptic).

    // Step 60: Periodic tap habituation (Rankin 1990)
    // Tap = plate vibration → activates ALM+PLM simultaneously (non-directional)
    // Repeated taps → STP vesicle depletion at ALM→AVD, PLM→AVA synapses
    // → decreased reversal response (= habituation, emergent from STP)
    // REF: Rankin 1990 J Comp Physiol A — tap habituation protocol
    //      Rankin & Broster 1992 — ISI determines habituation rate
    //      Maricq 1995 Nature — GLR-1 mediates mechanosensory signaling
    tap_timer_ += dt_;
    if (tap_timer_ >= tap_interval_) {
        tap_timer_ = 0.0;
        tap_active_ = true;
        tap_pulse_end_ = current_time_ + tap_duration_;
        tap_count_++;
    }
    if (tap_active_) {
        if (current_time_ < tap_pulse_end_) {
            // Deliver tap pulse to ALL touch neurons simultaneously
            for (int id : nids("ALM")) {
                if (id >= 0 && id < n) neurons_[id]->set_external_current(tap_current_);
            }
            for (int id : nids("PLM")) {
                if (id >= 0 && id < n) neurons_[id]->set_external_current(tap_current_);
            }
            // Step 61: AVM in tap (anterior gentle touch, like ALM)
            int avm_tap = nid("AVM");
            if (avm_tap >= 0 && avm_tap < n) neurons_[avm_tap]->set_external_current(tap_current_);
        } else {
            tap_active_ = false;
        }
    }

    // ======================================================================
    // Step 34: O₂ sensing — URX/AQR/PQR transduction
    // O₂ derived from food field: bacteria consume O₂ → low O₂ at food
    // O₂(x) = 21% - 13% × food_density(x) (Gray 2004)
    // URX: activated by HIGH O₂ (>14%), drives hyperoxia avoidance
    // AQR: head O₂, PQR: tail O₂ (body cavity sensors)
    // NPR-1 215V (N2): tonic inhibition scales with satiety
    // REF: Gray 2004 Nature, Cheung 2005, Chang 2006 PLoS Biology
    // ======================================================================
    {
        // Compute O₂ at head and tail from FOOD DENSITY (bacteria, σ≈3mm)
        // NOT sample_chemical (volatile odor, σ≈12mm) — O₂ depletion is local
        double food_at_head = environment_.sample_food_density(head);
        double food_at_tail = environment_.sample_food_density(tail);
        // Normalize food concentration (peak ~1.0 at source center)
        // O₂ = 21% - 13% × food_density → range [8%, 21%]
        double o2_head = 21.0 - 13.0 * std::min(food_at_head, 1.0);
        double o2_tail = 21.0 - 13.0 * std::min(food_at_tail, 1.0);

        // URX transduction: activated when O₂ > 14% (hyperoxia threshold)
        // Linear ramp: 0 at 14%, max (o2_gain_) at 21%
        // gcy-35/gcy-36 → cGMP → TAX-2/TAX-4 channel opening
        double urx_drive = 0.0;
        if (o2_head > 14.0) {
            urx_drive = o2_gain_ * (o2_head - 14.0) / 7.0;  // 0→30 pA
        }

        // NPR-1 tonic inhibition (N2 215V = constitutively active)
        // N2: NPR-1 is always on → strongly suppresses O₂ circuit
        // At 21% O₂: 30pA drive - 25pA NPR-1 = 5pA net (barely active)
        // REF: Chang 2006 — "N2 is indifferent to high O₂ when food is present"
        //       Laurent 2015 — NPR-1 inhibits RMG output downstream of Ca2+
        double npr1_inh = npr1_tonic_;  // constant for N2 (future: modulate for Hawaiian)

        // Net URX drive = O₂ excitation + NPR-1 inhibition
        double urx_net = std::max(urx_drive + npr1_inh, 0.0);

        for (int id : nids("URX")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(urx_net);
            }
        }

        // AQR: head O₂ sensor (same threshold, weaker gain)
        // AQR is unpaired, same location as URX (head pseudocoelom)
        if (nid("AQR") >= 0 && nid("AQR") < n) {
            double aqr_drive = 0.0;
            if (o2_head > 14.0) {
                aqr_drive = (o2_gain_ * 0.5) * (o2_head - 14.0) / 7.0;  // 50% of URX
            }
            double aqr_net = std::max(aqr_drive + npr1_inh * 0.5, 0.0);
            neurons_[nid("AQR")]->set_external_current(aqr_net);
        }

        // PQR: tail O₂ sensor
        // Tail high O₂ → PQR activates → AVA → accelerate forward (escape)
        // REF: Busch 2012 — PQR tail position facilitates forward escape
        if (nid("PQR") >= 0 && nid("PQR") < n) {
            double pqr_drive = 0.0;
            if (o2_tail > 14.0) {
                pqr_drive = (o2_gain_ * 0.5) * (o2_tail - 14.0) / 7.0;
            }
            double pqr_net = std::max(pqr_drive + npr1_inh * 0.5, 0.0);
            neurons_[nid("PQR")]->set_external_current(pqr_net);
        }

        // AUA + RMG NPR-1 inhibition: moved to step() AFTER I_syn_ reset
        // (add_synaptic_current here would be cleared by compute_synaptic_currents)
    }

    // ======================================================================
    // Step 35: CO₂ sensing — BAG transduction
    // CO₂ derived from food field: bacteria produce CO₂
    // CO₂(x) = 0.04% + 3% × food_density(x) (ambient + bacterial)
    // BAG: activated by CO₂ > 0.5%, phasic response (dCO₂/dt sensitive)
    // OFF rebound: CO₂ decrease → transient burst (like AWC OFF)
    // N2: NPR-1 suppresses URX → URX doesn't inhibit CO₂ circuit → avoids CO₂
    // REF: Hallem & Sternberg 2008, Bretscher 2011, Carrillo 2013
    // ======================================================================
    {
        double food_at_head = environment_.sample_food_density(head);
        double co2_head = 0.04 + 3.0 * std::min(food_at_head, 1.0);  // range [0.04%, 3.04%]

        // Phasic component: BAG responds to CO₂ CHANGES more than absolute level
        // dCO₂/dt > 0 (entering food) → strong activation
        // dCO₂/dt < 0 (leaving food) → OFF rebound burst
        double dco2 = (co2_head - prev_co2_head_) / (dt_ * 0.001);  // %/s
        prev_co2_head_ = co2_head;

        // Tonic component: sustained drive when CO₂ > threshold
        double tonic_drive = 0.0;
        if (co2_head > co2_threshold_) {
            tonic_drive = co2_gain_ * (co2_head - co2_threshold_) / 3.0;  // 0→40 pA
        }

        // Phasic component: sensitive to rate of change
        // Rising CO₂ → strong activation; falling CO₂ → OFF rebound
        double phasic_drive = 0.0;
        if (dco2 > 0.0) {
            // Entering high CO₂ zone: strong phasic response
            phasic_drive = 20.0 * dco2;  // 20 pA per %/s
        } else if (dco2 < 0.0) {
            // OFF rebound: leaving CO₂ zone → transient burst (escape acceleration)
            // REF: Bretscher 2011 — BAG OFF response drives escape from CO₂
            phasic_drive = -10.0 * dco2;  // positive current from negative dco2
        }

        // Total BAG drive = tonic + phasic (clamped)
        double bag_drive = std::max(tonic_drive + phasic_drive, 0.0);
        if (bag_drive > 60.0) bag_drive = 60.0;  // clamp

        // URX cross-inhibition: in npr-1(lf), active URX suppresses CO₂ circuit
        // In N2: URX is suppressed by NPR-1 → no cross-inhibition → BAG works
        // Carrillo 2013: "ablating URX in npr-1(lf) restores CO₂ avoidance"
        double urx_inhibition = 0.0;
        for (int id : nids("URX")) {
            if (id >= 0 && id < n) {
                urx_inhibition += neurons_[id]->get_transmitter_release_rate();
            }
        }
        urx_inhibition *= 30.0;  // scale: URX S=0.15 → 4.5pA inhibition (weak in N2)

        double bag_net = std::max(bag_drive - urx_inhibition, 0.0);

        for (int id : nids("BAG")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(bag_net);
            }
        }
    }

    // ======================================================================
    // Step 36: Proprioception — DVA + PVD transduction
    // DVA: whole-body stretch receptor (TRP-4 TRPN channel)
    //   Senses mean |curvature| across all segments → modulates B-class MN gain
    //   trp-4 mutant: exaggerated body bends (Li 2006 Nature)
    // PVD: harsh touch (stronger than ALM) + posterior body proprioception
    //   Dendrites tile body wall → dual-mode sensory neuron
    // REF: Li 2006 Nature, Way & Chalfie 1989, Albeg 2011
    // ======================================================================
    {
        // --- DVA: whole-body curvature integration ---
        if (nid("DVA") >= 0 && nid("DVA") < n) {
            const auto& segs = body_.segments();
            int nseg = static_cast<int>(segs.size());
            double sum_abs_curv = 0.0;
            for (int si = 0; si < nseg; ++si) {
                sum_abs_curv += std::abs(segs[si].curvature);
            }
            double mean_abs_curv = (nseg > 0) ? sum_abs_curv / nseg : 0.0;

            // TRP-4 transduction: stretch → depolarization
            // mean_abs_curv typical range: 0.05-0.3 /mm during normal locomotion
            // DVA drive: proportional to mean |curvature|
            double dva_drive = dva_gain_ * mean_abs_curv;
            if (dva_drive > 30.0) dva_drive = 30.0;  // clamp

            neurons_[nid("DVA")]->set_external_current(dva_drive);
        }

        // --- PVD: harsh touch + posterior proprioception ---
        Vector2d head = body_.get_head_position();
        double wall_dist = std::max(0.0, std::min({head.x, 50.0 - head.x, head.y, 50.0 - head.y}));

        for (int id : nids("PVD")) {
            if (id < 0 || id >= n) continue;
            double I_pvd = 0.0;

            // Mode 1: Harsh touch — wall collision at closer range than ALM
            // PVD responds to stronger mechanical stimuli (platinum wire vs eyelash)
            // Use wall proximity as proxy: PVD fires when very close to wall
            if (wall_dist < pvd_harsh_thresh_) {
                double proximity = 1.0 - wall_dist / pvd_harsh_thresh_;
                I_pvd += pvd_harsh_current_ * proximity;
            }

            // Mode 2: Posterior body proprioception
            // PVD dendrites cover posterior body → sense posterior curvature
            const auto& segs = body_.segments();
            int nseg = static_cast<int>(segs.size());
            double post_curv = 0.0;
            int post_start = nseg / 2;  // posterior half
            int post_count = 0;
            for (int si = post_start; si < nseg; ++si) {
                post_curv += std::abs(segs[si].curvature);
                post_count++;
            }
            if (post_count > 0) {
                post_curv /= post_count;
                I_pvd += pvd_proprio_gain_ * post_curv;
            }

            neurons_[id]->set_external_current(I_pvd);
        }
    }

    // ======================================================================
    // Step 38: Egg-laying — HSN/VC transduction
    // egg_pressure ramps up slowly (tau=120s), simulating egg accumulation
    // When egg_pressure > threshold → HSN burst → 5-HT release → egg laid
    // Tyramine feedback via LGC-55 inhibits HSN (already in TA system)
    // REF: Collins 2016 eLife, Waggoner 1998 Neuron
    // ======================================================================
    {
        // egg_pressure ramps toward 1.0 (tau_fill = 120s)
        double egg_target = 1.0;
        double alpha_fill = dt_ / egg_tau_fill_;
        egg_pressure_ += alpha_fill * (egg_target - egg_pressure_);
        if (egg_pressure_ > 1.0) egg_pressure_ = 1.0;

        // HSN activation: sigmoid of (egg_pressure - threshold)
        double hsn_sigmoid = 1.0 / (1.0 + fast_exp(-(egg_pressure_ - egg_threshold_) / 0.05));
        double I_hsn = hsn_egg_gain_ * hsn_sigmoid;

        // Tyramine inhibition on HSN via LGC-55 (same receptor as RIV/SMD)
        // REF: Collins 2016 — uv1 tyramine → LGC-55 → HSN hyperpolarization
        double ta_conc = neuromod_.get_concentration("TA");
        double ta_inh = -20.0 * ta_conc;  // -20pA at max TA
        I_hsn = std::max(I_hsn + ta_inh, 0.0);

        for (int id : nids("HSN")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(I_hsn);
            }
        }

        // Egg-laying event: HSN active + egg_pressure high → lay egg
        // Check if we're in active state or should start one
        if (hsn_sigmoid > 0.5 && current_time_ > egg_active_end_) {
            // Start active state
            egg_active_end_ = current_time_ + egg_active_duration_;
        }

        // During active state: VC gets excitation from HSN (via gap junction + 5-HT)
        if (current_time_ < egg_active_end_) {
            for (int id : nids("VC")) {
                if (id >= 0 && id < n) {
                    neurons_[id]->set_external_current(15.0);  // 5-HT potentiation
                }
            }
            // Egg laid at end of active state
            if (current_time_ + dt_ >= egg_active_end_ && egg_pressure_ > egg_threshold_) {
                egg_laid_count_ += 1;
                egg_pressure_ = 0.1;  // reset (not zero — some eggs remain)
            }
        }
    }

    // ======================================================================
    // Step 66: Food edge reversal (via AVA current injection)
    // ======================================================================
    // Pirouette Poisson process REMOVED — reversals now emerge from:
    //   1. ASE→AIB→AVA neural pathway (sensory-driven klinokinesis)
    //   2. Ion channel noise (3pA) → stochastic AVA switching (Roberts 2016)
    //   3. Food edge: transient AVA injection (below)
    // Reversal state detection moved to step() L606 (AVA release rate threshold)
    // RIV omega pulse also moved to step() L621
    //
    // REF: Piggott 2011 Cell — stimulatory + disinhibitory circuits
    //      Roberts 2016 eLife — stochastic switch model (bistable AVA)
    //      Kuramochi 2018 Front Mol Neurosci — ASE→AIB E/I switch
    // ======================================================================
    {
        // Step 70: Emergent food edge reversal (P1 violation 1.3 fixed)
        // REMOVED: p_edge_rev = 0.50 + 0.30×5HT - 0.30×PDF probability formula
        // Reversal probability now EMERGES from AVA-AVB mutual inhibition balance:
        //   Dwelling (5-HT↑ → MOD-1→AIY -5pA → AVB↓ → less AVA suppression):
        //     25pA kick + low AVB = AVA crosses Schmitt threshold → reversal ✅
        //   Roaming (PDF↑ → PDFR-1→AIY +3pA → AVB↑ → strong AVA suppression):
        //     25pA kick insufficient vs AVB mutual inhibition → no reversal → leaving ✅
        // REF: Flavell 2024 eLife — head poke reversal ~55%, leaving ~0.5%
        //      Leaving coupled to roaming state (20× higher in roaming vs dwelling)
        //      cat-2/tph-1 mutants: more leaving (more roaming), dynamics preserved
        double food_at_head = environment_.sample_food_density(body_.get_head_position());
        bool currently_on_lawn = (food_at_head > 0.4);
        bool food_edge_exit = (was_on_lawn_ && food_at_head < 0.3);
        if (currently_on_lawn) was_on_lawn_ = true;
        if (food_at_head < 0.3) was_on_lawn_ = false;

        // Always inject AVA on food edge exit (no probability gate)
        // 2s refractory prevents rapid re-triggering
        if (food_edge_exit && current_time_ > food_edge_ava_end_ + 2000.0) {
            food_edge_ava_end_ = current_time_ + 500.0;  // 500ms pulse
        }

        // Sustained food-edge AVA injection (during pulse window)
        // 25pA: moderate — whether reversal occurs depends on AVA-AVB balance
        // (emergent from 5-HT/PDF/DA neuromodulation on circuit)
        if (current_time_ < food_edge_ava_end_) {
            int n = static_cast<int>(neurons_.size());
            double ava_pulse = 40.0;  // Step 70: 40pA reliable activation (state-dependence from AVA-AVB balance)
            if (nid("AVAL") >= 0 && nid("AVAL") < n) neurons_[nid("AVAL")]->add_synaptic_current(ava_pulse);
            if (nid("AVAR") >= 0 && nid("AVAR") < n) neurons_[nid("AVAR")]->add_synaptic_current(ava_pulse);
        }
    }
}

// ================================================================
// Step 79: Nociceptive sensitization / dishabituation
// Dual-process theory (Groves & Thompson 1970):
//   S-process: stimulus-specific habituation (STP vesicle depletion)
//   R-process: state-dependent sensitization (this function)
//   Net response = S × R
//
// Mechanism:
//   1. Strong ASH activation (nociceptive) → sensitization_ rises
//   2. sensitization_ decays slowly (τ=30s, longer than TA's 2s)
//   3. When sensitized: boost vesicle recovery at touch synapses
//      → partially restores depleted pool → next tap triggers reversal
//
// Dishabituating stimulus:
//   --dishabit-at <sec>: inject 100pA to ASH for 2s (harsh mechanical)
//   This mimics electric shock or train stimulus (Rankin & Broster 1992)
//
// REF: Groves & Thompson 1970 Psychol Rev — dual-process theory
//      Rankin & Broster 1992 — dishabituation in C. elegans
//      Greer 2008 — SER-2/PKC modulates mechanosensory synapses
//      Marcus 1988 — sensitization ≠ dishabituation (independent)
// ================================================================
void SimulationEngine::apply_sensitization() {
    int n = static_cast<int>(neurons_.size());

    // --- 1. Dishabituating stimulus delivery ---
    if (dishabit_time_ > 0 && current_time_ >= dishabit_time_ &&
        current_time_ < dishabit_time_ + dishabit_duration_) {
        for (int id : nids("ASH")) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(dishabit_current_);
            }
        }
    }

    // --- 2. Monitor ASH activity → update sensitization ---
    double ash_activity = 0.0;
    int ash_count = 0;
    for (int id : nids("ASH")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            double s = 1.0 / (1.0 + std::exp(-(v - (-25.0)) / 5.0));
            ash_activity += s;
            ash_count++;
        }
    }
    if (ash_count > 0) ash_activity /= ash_count;

    if (ash_activity > 0.3) {
        sensitization_ += sensitization_rise_rate_ * ash_activity * dt_;
        if (sensitization_ > 1.0) sensitization_ = 1.0;
    }
    sensitization_ -= sensitization_ / sensitization_tau_decay_ * dt_;
    if (sensitization_ < 0.0) sensitization_ = 0.0;

    // --- 3. Boost touch synapse vesicle recovery when sensitized ---
    if (sensitization_ > 0.05) {
        if (touch_syn_indices_.empty()) {
            auto& synapses = connectome_.synapses_mut();
            const auto& ni = connectome_.neuron_infos();
            for (size_t i = 0; i < synapses.size(); ++i) {
                int pre = synapses[i].pre_id(), post = synapses[i].post_id();
                if (pre < 0 || pre >= n || post < 0 || post >= n) continue;
                const auto& pn = ni[pre].name;
                const auto& qn = ni[post].name;
                bool is_touch_pre = (pn.substr(0,3) == "ALM" || pn.substr(0,3) == "PLM" || pn.substr(0,3) == "ASH");
                bool is_touch_post = (qn.substr(0,3) == "AVD" || qn.substr(0,3) == "AVA" ||
                                      qn.substr(0,3) == "AVB" || qn.substr(0,3) == "PVC" ||
                                      qn.substr(0,3) == "AIB" || qn.substr(0,3) == "RIM");
                if (is_touch_pre && is_touch_post) {
                    touch_syn_indices_.push_back(i);
                }
            }
        }
        auto& synapses = connectome_.synapses_mut();
        for (size_t idx : touch_syn_indices_) {
            double pool = synapses[idx].vesicle_pool();
            double boost = sensitization_ * sensitization_pool_boost_ * dt_;
            synapses[idx].set_vesicle_pool(pool + boost);
        }
    }
}

} // namespace celegans
