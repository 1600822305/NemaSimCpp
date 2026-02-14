// apply_motor_control.cpp — Split from simulation_engine.cpp (Step 92)
// Contains: apply_head_tonic, apply_weathervane, apply_smb_neck_bias,
//           apply_ria_smd_modulation, apply_proprioceptive_stretch, apply_riv_omega
#include "simulation/simulation_engine.h"
#include "neuron/multi_compartment.h"
#include "core/logger.h"
#include <cmath>

namespace celegans {

void SimulationEngine::apply_head_tonic() {
    // Head motor neurons (SMD/RMD) receive tonic excitatory input
    // from upstream interneurons (RIA synapses already in connectome)
    // This small tonic keeps the head circuit near oscillation threshold
    // The actual oscillation emerges from: CCA-1 rebound + cross-inhibition
    // REF: Hendricks 2012, Shen 2016
    //
    // Step 27: During sleep, FLP-11 suppresses upstream interneuron drive
    // RIS inhibits RIA/RIB (approach circuit) → tonic drive drops
    // REF: Konietzka 2020 — RIS depolarization → cessation of head movement
    double tonic = head_tonic_;
    if (is_sleeping_ && nid("RIS") >= 0 && nid("RIS") < static_cast<int>(neurons_.size())) {
        double rv = neurons_[nid("RIS")]->get_membrane_potential();
        double flp11 = 1.0 / (1.0 + fast_exp(-(rv - (-35.0)) / 5.0));
        tonic *= (1.0 - 0.95 * flp11);  // near-zero tonic during deep sleep
    }
    int n = static_cast<int>(neurons_.size());
    for (int id : nids("head_motor")) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(tonic);
        }
    }

    // Step 31: RIV baseline + post-reversal pulse
    //
    // Tonic: 2 pA (below CCA-1 oscillation threshold ~3 pA)
    //   During reversal: TA→-20×[TA] suppresses RIV → CCA-1 h deinactivates
    //   After reversal: TA decays → tonic pushes V toward CCA-1 window
    //
    // Post-reversal pulse: models reversal→forward transition signal
    //   Biological basis (Donnelly 2013, Neural Sequences 2024):
    //   "omega initiated when animal reinitiates forward locomotion"
    //   Pulse amplitude ∝ [TA] at reversal end (set in update_pirouette_state)
    //   Decays with tau=200ms — enough to trigger CCA-1 burst within h recovery window
    //
    double riv_tonic = static_cast<double>(params.riv_tonic);  // pA baseline
    if (is_sleeping_) riv_tonic *= 0.1;

    // Post-reversal pulse: decaying excitation for ~500ms after reversal ends
    // L/R asymmetric pulse → gradient-dependent omega direction
    double riv_pulse_l = 0.0, riv_pulse_r = 0.0;
    double dt_since_rev = current_time_ - riv_post_rev_time_;
    if (dt_since_rev >= 0.0 && dt_since_rev < 600.0) {
        double decay = fast_exp(-dt_since_rev / 400.0);
        if (riv_post_rev_amp_l_ > 1.0) riv_pulse_l = riv_post_rev_amp_l_ * decay;
        if (riv_post_rev_amp_r_ > 1.0) riv_pulse_r = riv_post_rev_amp_r_ * decay;
    }

    if (nid("RIVL") >= 0 && nid("RIVL") < n) neurons_[nid("RIVL")]->set_external_current(riv_tonic + riv_pulse_l);
    if (nid("RIVR") >= 0 && nid("RIVR") < n) neurons_[nid("RIVR")]->set_external_current(riv_tonic + riv_pulse_r);
}

void SimulationEngine::apply_weathervane() {
    // Weathervane mechanism: gradient ⊥ heading → differential SMD drive
    // REF: Iino & Yoshida 2009 — curving rate bias = 12.7 °/mm × ∇C_normal
    // Implementation: compute gradient perpendicular to heading direction,
    // then apply differential current to dorsal vs ventral SMD neurons.
    // This biases the half-center oscillator, causing gradual curving toward food.
    //
    // Neural basis: head oscillation samples gradient laterally → ASE → AIZ → SMD
    // We approximate this by directly biasing SMD based on the normal gradient component.

    Vector2d head_pos = body_.get_head_position();
    double heading = body_.get_head_angle();
    double cos_h = std::cos(heading);
    double sin_h = std::sin(heading);
    double weathervane_gain = static_cast<double>(params.weathervane_gain);

    // Step 23c: Satiety modulates chemotaxis weathervane gain
    double sat_switch_wv = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    double chemo_wv_gain = 1.0 - 0.85 * sat_switch_wv;  // fed: 0.15, hungry: 1.0

    // Step 26b: DUAL-CHANNEL WEATHERVANE
    // Channel 1: Food odor (volatile, AWC/AWA) — modulated by learned preference
    // Channel 2: Soluble (salt/amino acids, ASE) — NOT affected by pathogen learning
    // REF: Bargmann 2006 — AWC and ASE detect independent chemical modalities

    // --- Channel 1: Food odor weathervane (learnable) ---
    Vector2d grad = environment_.chemical_field().gradient(head_pos);
    double grad_normal = -sin_h * grad.x + cos_h * grad.y;

    // AWC preference: derived from mean AWC→AIY w_mod
    // Asymmetric scaling: avoidance stronger than attraction
    // (missing food = minor cost; eating toxin = sickness = high cost)
    // w_mod=1.0 → pref=+1.0 (naive, attract to food odor)
    // w_mod=0.5 → pref=-0.15 (slight avoidance)
    // w_mod=0.1 → pref=-1.35 (strong repulsion, 1.35× attract gain)
    double awc_pref = awc_pref_cached_;  // updated by update_awc_pref_cache() after learning
    double odor_bias = weathervane_gain * grad_normal * chemo_wv_gain * awc_pref;

    // --- Channel 2: Soluble (ASE) ---
    // ASE drives klinokinesis (pirouette rate), NOT klinotaxis (weathervane)
    // REF: Iino & Yoshida 2009 — weathervane primarily AWC-mediated
    // Soluble gradient computed for curvature bias only (not SMD drive)
    Vector2d sol_grad = environment_.soluble_field().gradient(head_pos);
    double sol_grad_normal = -sin_h * sol_grad.x + cos_h * sol_grad.y;
    double sol_wv_scale = 0.0;  // ASE→pirouettes, not weathervane
    double sol_bias = 0.0;      // no soluble weathervane contribution

    double bias_current = odor_bias + sol_bias;

    // Step 25: Repellent weathervane — turn AWAY from repellent gradient
    // Symmetric to attractant weathervane but with reversed sign
    // Without this: worm bounces back and forth (hit→reverse→attract→hit)
    // With this: worm continuously deflects around repellent zone
    Vector2d rep_grad = environment_.repellent_field().gradient(head_pos);
    double rep_grad_normal = -sin_h * rep_grad.x + cos_h * rep_grad.y;
    // Negative sign: curve AWAY from repellent gradient (opposite to attractant)
    // Gain matches attractant weathervane so forces compete symmetrically
    // Not modulated by satiety: nociceptive avoidance is unconditional
    double rep_bias = -weathervane_gain * rep_grad_normal;
    bias_current += rep_bias;

    // Step 23c: Temperature weathervane — turn toward learned Tc when fed
    // Navigate to minimize |T - Tc|: bias = -sign(T-Tc) × grad_T_normal
    // This steers toward Tc regardless of which side the worm is on
    // Step 101: use learned Tc (adapt_tc) instead of fixed cultivation_temp_
    // Hedgecock & Russell 1975: Tc is updated by food-temperature pairing
    Vector2d tgrad = environment_.temperature_gradient(head_pos);
    double temp_grad_normal = -sin_h * tgrad.x + cos_h * tgrad.y;
    double temp_at_head = environment_.sample_temperature(head_pos);
    double tc = learned_tc();
    double temp_sign = (temp_at_head > tc) ? -1.0 : 1.0;  // toward Tc
    double thermo_wv_gain = 0.0 + 2.0 * sat_switch_wv;    // hungry: 0, fed: 2.0
    // Temperature weathervane gain: 30 pA per °C/mm
    // At 0.5°C/mm gradient, fed(×2.0): 30×0.25×2.0 = 15 pA (competes with chemo ~5-20 pA)
    double temp_bias = 30.0 * temp_sign * temp_grad_normal * thermo_wv_gain;
    bias_current += temp_bias;

    // Clamp to ±bias_clamp pA (should not overwhelm the half-center oscillator)
    double clamp = static_cast<double>(params.bias_clamp);
    if (bias_current > clamp) bias_current = clamp;
    if (bias_current < -clamp) bias_current = -clamp;

    int n = static_cast<int>(neurons_.size());

    // Step 65: SMD is now the PRIMARY turning mechanism (curvature_bias bypass REMOVED)
    // With SMD amplitude calibrated to ~49mV (Nicoletti 2019), ±5pA bias shifts
    // duty cycle by ~8%, sufficient for weathervane steering.
    // Previous: SMD=110mV → bias drowned → needed curvature_bias bypass → CI=0.76
    // Now: SMD=49mV → bias effective → CI from neural circuit → emergent!
    //
    // Skip during reversal/omega (Iino 2009: klinotaxis = run-phase behavior)
    // 5-HT modulation: on-food → slightly reduced weathervane (dwelling = less exploration)
    if (!is_reversing_ && !riv_omega_active_) {
        double sht_conc_wv = neuromod_.get_concentration("5-HT");
        double smd_wv_scale = 0.7 + 0.3 * std::max(0.0, 1.0 - sht_conc_wv / 0.7);
        if (smd_wv_scale > 1.0) smd_wv_scale = 1.0;
        double smd_drive = bias_current * smd_wv_scale;
        // Sign: positive grad_normal (food to left) → positive smd_drive
        // SMDD gets -drive: suppress dorsal → extend ventral phase → curve LEFT toward food
        // SMDV gets +drive: enhance ventral → same effect
        // (Inverted vs naive expectation because SMD→muscle→curvature chain has sign inversion)
        if (nid("SMDDL") >= 0 && nid("SMDDL") < n) neurons_[nid("SMDDL")]->add_synaptic_current(-smd_drive);
        if (nid("SMDDR") >= 0 && nid("SMDDR") < n) neurons_[nid("SMDDR")]->add_synaptic_current(-smd_drive);
        if (nid("SMDVL") >= 0 && nid("SMDVL") < n) neurons_[nid("SMDVL")]->add_synaptic_current( smd_drive);
        if (nid("SMDVR") >= 0 && nid("SMDVR") < n) neurons_[nid("SMDVR")]->add_synaptic_current( smd_drive);
    }

    // Step 117: curvature_drive removed — RIV/SMB now drive muscles directly
}

void SimulationEngine::apply_smb_neck_bias() {
    // Step 28: RIA multi-compartment Ca²⁺ gate-and-switch → SMB neck curvature bias
    //
    // Replaces Step 19 AC/DC approximation with true subcellular computation:
    //   RIA nrV: receives SMDVL ACh → GAR-3 → local Ca²⁺ during ventral bend
    //   RIA nrD: receives SMDDL ACh → GAR-3 → local Ca²⁺ during dorsal bend
    //   RIA soma: receives global sensory glutamate (AWC/ASE → AIY → RIA)
    //
    // The multiplication happens physically:
    //   - Sensory → soma → spreads to nrV and nrD via axial coupling
    //   - Motor feedback → only nrV OR nrD (compartment-specific)
    //   - Both present → high local Ca²⁺ (additive: Hendricks 2012)
    //   - Ca_nrD - Ca_nrV encodes perpendicular gradient component
    //
    // REF: Hendricks 2012 Nature — compartmentalized Ca²⁺ in RIA axon
    //      Ouellette 2018 eNeuro — RIA subcellular domains for navigation
    //      Iino & Yoshida 2009 — curving rate ∝ ∇C_⊥

    int n = static_cast<int>(neurons_.size());

    // Read RIA nrV (comp 1) and nrD (comp 2) calcium from multi-compartment neurons
    double ca_diff = 0.0;
    int count = 0;

    // Uses cached MultiCompartmentNeuron* pointers (avoid per-step dynamic_cast)
    for (int i = 0; i < 2; ++i) {
        auto* mc = ria_mcn_[i];
        if (!mc || mc->num_compartments() < 3) continue;
        double ca_nrV = mc->get_compartment_calcium(1);  // nrV = compartment 1
        double ca_nrD = mc->get_compartment_calcium(2);  // nrD = compartment 2
        ca_diff += (ca_nrV - ca_nrD);  // sign: ventral Ca > dorsal → curve toward food
        count++;
    }

    if (count > 0) ca_diff /= count;  // average L/R

    // DC removal: track slow baseline (2s tau) and subtract
    // Only the oscillatory (AC) component carries perpendicular gradient info:
    //   AC = phase-locked to head oscillation via SMD feedback
    //   DC = tonic level, creates positive feedback loop if not removed
    ria_ca_diff_mean_ += (ca_diff - ria_ca_diff_mean_) * dt_ / 2000.0;
    double ca_diff_ac = ca_diff - ria_ca_diff_mean_;

    // Low-pass filter: ~300ms (half oscillation cycle, removes 2f ripple)
    ria_ca_diff_filtered_ += (ca_diff_ac - ria_ca_diff_filtered_) * dt_ / 300.0;

    // Convert Ca2+ AC difference to curvature bias
    // AC amplitude ~0.01-0.03 uM, gain calibrated for heading ~15 deg/s
    double klinotaxis_gain = 3000.0;  // /mm per uM Ca2+ AC difference
    double curvature_offset = klinotaxis_gain * ria_ca_diff_filtered_;

    // Clamp
    // Step 28: reduced from 2.0 to 0.9 because Ca2+ signal is cleaner
    // than old AC/DC approximation (less noise -> hits clamp more often)
    double max_bias = 0.5;
    if (curvature_offset > max_bias) curvature_offset = max_bias;
    if (curvature_offset < -max_bias) curvature_offset = -max_bias;

    // Step 117: Inject klinotaxis signal into head muscles via boost channel
    // RIA Ca²⁺ → curvature_offset → asymmetric muscle boost → curvature emerges
    // Goes through muscle dynamics (30ms tau) + physics integrator
    // Skip during omega: RIV dominates head muscles
    if (!riv_omega_active_) {
        // Convert curvature offset (/mm) to muscle force via boost
        // Gain calibrated so ±0.5/mm offset → force_diff ~1.5 → curvature ~0.45/mm
        double smb_muscle_gain = 15.0; // Step 119: 5x increase for RFT torque dilution (head lever arm ~12% of body)
        double dorsal_boost = curvature_offset > 0 ? curvature_offset * smb_muscle_gain : 0.0;
        double ventral_boost = curvature_offset < 0 ? -curvature_offset * smb_muscle_gain : 0.0;
        for (int seg = 0; seg < 6; ++seg) {
            body_.muscles().add_boost(seg, true,  dorsal_boost);
            body_.muscles().add_boost(seg, false, ventral_boost);
        }
    }
}

void SimulationEngine::apply_ria_smd_modulation() {
    // Step 19: RIA → SMD neuromodulation via CCA-1 threshold shift
    // NOT current injection! This modulates the oscillator's intrinsic property.
    //
    // Connectome: RIAL → SMDDL(3), SMDVL(4) and RIAR → SMDDR(3), SMDVR(4)
    // These synaptic currents are already computed by compute_synaptic_currents().
    // But the DC synaptic current can't shift duty cycle of a 100mV oscillation.
    //
    // Biological mechanism: RIA release → metabotropic receptor → second messenger
    // → modulates CCA-1 (T-type Ca²⁺) activation threshold
    // → lower threshold → burst starts earlier → longer burst → higher duty cycle
    //
    // REF: Hendricks 2012, Mellem 2002 — metabotropic modulation of ion channels
    int n = static_cast<int>(neurons_.size());

    double ria_release_L = 0.0, ria_release_R = 0.0;
    if (nid("RIAL") >= 0 && nid("RIAL") < n)
        ria_release_L = neurons_[nid("RIAL")]->get_transmitter_release_rate();
    if (nid("RIAR") >= 0 && nid("RIAR") < n)
        ria_release_R = neurons_[nid("RIAR")]->get_transmitter_release_rate();

    // Modulation gain: how much RIA release shifts CCA-1 V_half (mV)
    // At release=0.5 (baseline): shift=0 (symmetric)
    // At release=0.7: shift = +3mV → easier burst → longer duty cycle
    // At release=0.3: shift = -3mV → harder burst → shorter duty cycle
    // 15 mV/unit: calibrated so ±0.1 release diff → ±1.5mV CCA-1 shift
    // CCA-1 V_half is -48mV, slope=5mV, so 1.5mV shift changes m_inf significantly
    // Step 28: reduced from 15 to 8 to compensate for SMD-RIA feedback loop
    double mod_gain = 5.0;  // mV per unit release rate deviation from 0.5
    double shift_L = mod_gain * (ria_release_L - 0.5);
    double shift_R = mod_gain * (ria_release_R - 0.5);

    // Apply to SMD neurons: RIAL drives SMDDL/SMDVL, RIAR drives SMDDR/SMDVR
    // Uses cached SingleCompartmentNeuron* pointers (avoid per-step dynamic_cast)
    if (smd_scn_[0]) smd_scn_[0]->set_cca1_activation_shift(shift_L);  // SMDDL
    if (smd_scn_[1]) smd_scn_[1]->set_cca1_activation_shift(shift_L);  // SMDVL
    if (smd_scn_[2]) smd_scn_[2]->set_cca1_activation_shift(shift_R);  // SMDDR
    if (smd_scn_[3]) smd_scn_[3]->set_cca1_activation_shift(shift_R);  // SMDVR
}

void SimulationEngine::apply_proprioceptive_stretch() {
    // Step 29: Proprioceptive wave propagation (Wen 2012, Boyle 2012)
    // Each motor neuron senses curvature at its sample_segment.
    // Step 119: Command neuron gating of proprioceptive coupling
    // B-class: AVB-gated (Wen 2012 — "AVB-B electrical couplings work
    //   synergistically with proprioceptive couplings to enhance sequential
    //   activation and facilitate wave propagation from head to tail")
    // A-class: AVA-gated (Gao 2018 — AVA-A gap junctions entrain A-class
    //   oscillators for backward wave propagation)
    // Without command neuron drive, proprioception alone is insufficient
    // to sustain the traveling wave → prevents forward wave during reversal
    // and backward wave during forward locomotion.
    int n = static_cast<int>(neurons_.size());

    // Gate proprioception by neural reversal state (Schmitt trigger latch)
    // The reversal state captures the AVA flip-flop transition (Roberts 2016).
    // Using the latched state rather than instantaneous AVA/AVB ratio because:
    //   - AVA may briefly exceed AVB to trigger reversal, then drop back
    //   - The Schmitt trigger holds the reversal state for the episode
    //   - This models AVA-A gap junction persistent drive (Gao 2018):
    //     once reversal starts, A-class proprioception stays active
    // Forward: B-class proprioception ON (head→tail wave)
    // Reverse: A-class proprioception ON (tail→head wave)
    double avb_gate = is_reversing_ ? 0.0 : 1.0;
    double ava_gate = is_reversing_ ? 1.0 : 0.0;

    for (auto& pm : proprio_mappings_) {
        if (pm.neuron_id < 0 || pm.neuron_id >= n) continue;

        double curv = body_.get_local_curvature(pm.sample_segment);
        // Dorsal MN: excited by ventral bend (negative curv)
        // Ventral MN: excited by dorsal bend (positive curv)
        double stretch = pm.is_dorsal ? -curv : curv;
        if (stretch < 0.0) stretch = 0.0;

        // Gate by command neuron state
        stretch *= pm.is_forward ? avb_gate : ava_gate;

        auto* scn = dynamic_cast<SingleCompartmentNeuron*>(neurons_[pm.neuron_id].get());
        if (scn) {
            scn->set_stretch_input(stretch);
        }
    }
}

void SimulationEngine::apply_riv_omega() {
    // Step 117: RIV-driven omega turn — fully through muscles
    //
    // Mechanism:
    //   During reversal: AVA active → RIM→TA→LGC-55→RIV(-20pA) = suppressed
    //   Reversal ends:   AVA quiet → TA decays (τ=2s) → RIV released → burst
    //   RIV burst → motor_controller (NMJ gain 40x) → head muscles → deep bend
    //   Burst self-terminates via Ca²⁺→SLO-1 adaptation (same as SMD)
    //
    // Direction: RIVL vs RIVR asymmetry from upstream gradient signals
    //   RIVL → ventral head muscles (L→V in 2D), RIVR → dorsal head muscles (R→D)
    //   REF: RIV innervates ventral muscles (Gray 2005). 2D: L→V, R→D.
    //
    // This function only manages omega STATE (for weathervane/klinotaxis suppression).
    // The actual muscle drive comes from motor_controller automatically.
    //
    // REF: Gray 2005 PNAS — RIV specifies ventral bias of omega turns
    //      Donnelly 2013 — TA gates omega timing via LGC-55 on RIV

    int n = static_cast<int>(neurons_.size());
    if (nid("RIVL") < 0 || nid("RIVR") < 0 || nid("RIVL") >= n || nid("RIVR") >= n) return;

    double rivl_rel = neurons_[nid("RIVL")]->get_transmitter_release_rate();
    double rivr_rel = neurons_[nid("RIVR")]->get_transmitter_release_rate();
    double riv_max = std::max(rivl_rel, rivr_rel);

    // --- Omega INITIATION: peak detection + pre-reversal AS resistance ---
    double prev_max = riv_prev_max_;
    riv_prev_max_ = riv_max;

    if (!riv_omega_active_) {
        bool at_peak = (riv_max < prev_max && prev_max > static_cast<double>(params.omega_threshold));
        if (at_peak) {
            double effective_riv = prev_max - pre_rev_dorsal_tone_ * static_cast<double>(params.as_factor);
            if (effective_riv > static_cast<double>(params.omega_threshold)) {
                riv_omega_active_ = true;
                riv_omega_start_ = current_time_;
                // Latch peak release for sustained omega curvature
                // Apply gradient-directed L/R bias directly to peak values:
                // CCA-1 all-or-nothing bursting equalizes RIVL/RIVR release (~0.8),
                // so the asymmetric post-rev pulse doesn't create proportional
                // release asymmetry. We directly scale peaks by the post-rev
                // amplitude ratio to ensure gradient controls omega direction.
                double mean_rel = (rivl_rel + rivr_rel) * 0.5;
                double amp_total = riv_post_rev_amp_l_ + riv_post_rev_amp_r_;
                if (amp_total > 0.01) {
                    riv_omega_peak_l_ = mean_rel * 2.0 * (riv_post_rev_amp_l_ / amp_total);
                    riv_omega_peak_r_ = mean_rel * 2.0 * (riv_post_rev_amp_r_ / amp_total);
                } else {
                    riv_omega_peak_l_ = rivl_rel;
                    riv_omega_peak_r_ = rivr_rel;
                }
            }
        }
    }

    // --- Omega EXECUTION + TERMINATION ---
    if (riv_omega_active_) {
        // Inject latched RIV burst force into head muscles via boost channel
        // Exponential decay models muscle Ca²⁺ clearance after neural burst
        // Without decay, curvature saturates at max_curv (25/mm) for entire omega
        // → turns overshoot 130-170° vs biological target ~60° (Gray 2005)
        double omega_elapsed = current_time_ - riv_omega_start_;
        double decay = std::exp(-omega_elapsed / 150.0);  // 150ms tau: muscle Ca²⁺ clearance (RFT calibrated)
        double omega_nmj_gain = 300.0 * decay;
        for (int seg = 0; seg < 6; ++seg) {
            double taper = 1.0 - 0.5 * (seg / 5.0);  // 100% at head, 50% at seg 5
            body_.muscles().add_boost(seg, false, riv_omega_peak_l_ * omega_nmj_gain * taper);
            body_.muscles().add_boost(seg, true,  riv_omega_peak_r_ * omega_nmj_gain * taper);
        }

        // Termination: min 400ms, then end when RIV drops below threshold
        // OR when boost decays below effective threshold (gain < 10 → ~1% of peak)
        if ((omega_elapsed > 400.0 && riv_max < static_cast<double>(params.omega_threshold))
            || omega_nmj_gain < 10.0) {
            riv_omega_active_ = false;
        }
    }
}

} // namespace celegans
