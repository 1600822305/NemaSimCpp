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
    // Weathervane (klinotaxis) works ENTIRELY through the biological pathway:
    //   Head oscillation → nose samples different lateral positions
    //   → ASE/AWC transducers detect temporal concentration changes
    //   → ASE → AIY/AIZ → RIA soma (sensory glutamate)
    //   → RIA compartmentalized Ca²⁺: sensory × motor (GAR-3 IP3 store release)
    //   → apply_smb_neck_bias() converts Ca²⁺ → SMB muscle boost → steering
    //
    // SMD is a CPG for head oscillation, NOT the weathervane executor.
    // NO current injection into SMD for weathervane.
    //
    // REF: Iino & Yoshida 2009 — AIZ critical for weathervane
    //      Hendricks 2012 Nature — RIA compartmentalized Ca²⁺
    //      Izquierdo & Lockery 2010 — minimal klinotaxis circuit
}

void SimulationEngine::apply_smb_proprioception() {
    // Izquierdo & Beer 2013 Eq 7: SMB receives oscillatory body wave input
    // wPG × sin(2πt/T) — provides phase reference for klinotaxis
    //
    // In biology, this corresponds to proprioceptive feedback:
    //   Head bending → stretch receptors → SMB motor neurons
    //   Dorsal bend → excite SMBDL/SMBDR
    //   Ventral bend → excite SMBVL/SMBVR
    //
    // This coupling is ESSENTIAL: without it, SMB cannot correlate the
    // AIZ sensory signal with head swing phase, and klinotaxis fails.
    // (Tested: SMB D/V difference has correct amplitude but wrong phase
    //  correlation without this input → WV_slope drops from 7 to 3°/s/rad)
    //
    // REF: Izquierdo & Beer 2013 J Neurosci — minimal klinotaxis circuit
    //      Wen et al. 2012 Neuron — proprioceptive feedback in C. elegans

    // Phase multiplication now happens at muscle output level in
    // apply_smb_neck_bias() instead of here, to avoid current propagation
    // through SAA gap junctions → AVA that suppresses reversals.
    // (Tested: neural injection at gain≥0.5 collapsed omega_toward to 50%)
}

void SimulationEngine::apply_smb_neck_bias() {
    // RIA multi-compartment Ca²⁺ gate-and-switch → SMB neck curvature bias
    //
    // Hendricks 2012 Nature: RIA axon has compartmentalized Ca²⁺ dynamics.
    //   nrV: receives SMDVL ACh → GAR-3 → local Ca²⁺ during ventral bend
    //   nrD: receives SMDDL ACh → GAR-3 → local Ca²⁺ during dorsal bend
    //   soma: receives global sensory glutamate (AWC/ASE → AIY → RIA)
    //
    // The multiplication happens physically:
    //   Sensory → soma → spreads to nrV and nrD via axial coupling
    //   Motor feedback → only nrV OR nrD (compartment-specific)
    //   Both present → high local Ca²⁺ (additive)
    //   Ca_nrD - Ca_nrV encodes gradient ⊥ heading
    //
    // This provides phase-selective multiplication WITHOUT injecting
    // current into SMB neurons (which propagates through SAA gap junctions
    // to AVA and destroys reversal dynamics — tested, omega_toward→50%).
    //
    // REF: Hendricks 2012 Nature — compartmentalized Ca²⁺ in RIA axon
    //      Ouellette 2018 eNeuro — RIA subcellular domains for navigation

    int n = static_cast<int>(neurons_.size());

    // Read RIA nrV (comp 1) and nrD (comp 2) calcium
    double ca_diff = 0.0;
    int count = 0;
    for (int i = 0; i < 2; ++i) {
        auto* mc = ria_mcn_[i];
        if (!mc || mc->num_compartments() < 3) continue;
        double ca_nrV = mc->get_compartment_calcium(1);
        double ca_nrD = mc->get_compartment_calcium(2);
        ca_diff += (ca_nrD - ca_nrV);
        count++;
    }
    if (count > 0) ca_diff /= count;

    // Two-stage filtering for gradient extraction:
    //
    // Stage 1: Slow DC removal (τ=30s) — removes CONSTANT circuit offset.
    // The RIA→SMD synapse asymmetry (SMDVL=4 > SMDDL=3, Cook 2019) causes SMDV
    // to be more active → nrV > nrD → constant negative offset (~-0.08).
    // τ=30s (cutoff 0.005Hz) preserves slow gradient signals from IP3-integrated
    // sensory_mod (τ_IP3=3s) while removing the structural circuit offset over ~60s.
    // Previous τ=10s was too aggressive, attenuating IP3-integrated gradient by ~50%.
    ria_ca_diff_mean_ += (ca_diff - ria_ca_diff_mean_) * dt_ / 30000.0;
    double ca_diff_centered = ca_diff - ria_ca_diff_mean_;
    //
    // Stage 2: Low-pass (τ=1s) — retains phase-locked 2Hz AC component (×0.157).
    // The residual AC IS the weathervane mechanism:
    //   - During dorsal phase: ca_diff > 0 → curvature_offset opposes (dampening)
    //   - Gradient makes one half-cycle larger → NET BIAS over full cycle → steering
    //   - This phase-locked amplitude asymmetry is the biological klinotaxis signal
    // sensory_mod × motor I_syn multiplication (Hendricks 2012) encodes ∇C_⊥.
    ria_ca_diff_filtered_ += (ca_diff_centered - ria_ca_diff_filtered_) * dt_ / 1000.0;

    // Convert Ca²⁺ difference to curvature bias
    // Negative gain: nrD>nrV (dorsal motor active) → dampens dorsal bend
    // (negative feedback on oscillation, consistent with gar-3 biology).
    // Gradient asymmetry in Ca²⁺ creates net steering bias (weathervane).
    double klinotaxis_gain = -4.0;
    double curvature_offset = klinotaxis_gain * ria_ca_diff_filtered_;

    double max_bias = 0.8;
    if (curvature_offset > max_bias) curvature_offset = max_bias;
    if (curvature_offset < -max_bias) curvature_offset = -max_bias;

    // Inject klinotaxis signal into head muscles via boost channel
    if (!riv_omega_active_) {
        double smb_muscle_gain = 1.0;
        double dorsal_boost = curvature_offset > 0 ? curvature_offset * smb_muscle_gain : 0.0;
        double ventral_boost = curvature_offset < 0 ? -curvature_offset * smb_muscle_gain : 0.0;
        for (int seg = 0; seg < 6; ++seg) {
            body_.muscles().add_boost(seg, true,  dorsal_boost);
            body_.muscles().add_boost(seg, false, ventral_boost);
        }
    }
}

void SimulationEngine::apply_gradient_curv_bias() {
    // Removed: external angular velocity injection was an engineering bypass.
    // Weathervane now goes through the biological RIA→SMB→muscle pathway.
    body_.set_external_angular_velocity(0.0);
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
                // REPLACED: environment gradient re-sampling (engineering bypass)
                // WITH: natural RIVL/RIVR neural activity at omega initiation
                // The RIV neurons receive input from the neural circuit
                // (AIB, RIA, sensory pathways) which carries gradient info.
                // The inherent L/R asymmetry in RIV release rates at this moment
                // reflects the circuit's directional encoding.
                // Post-rev amplitudes add posture-based bias (biological).
                // REF: Gray 2005 — posture contributes to turn direction
                //      Donnelly 2013 — RIV activity determines omega direction
                double amp_total = riv_post_rev_amp_l_ + riv_post_rev_amp_r_;
                double post_rev_lr = 0.0;
                if (amp_total > 0.01) {
                    post_rev_lr = (riv_post_rev_amp_l_ - riv_post_rev_amp_r_) / amp_total;
                }
                // Use actual RIV release rates (neural circuit output) + posture
                double riv_neural_lr = 0.0;
                if (rivl_rel + rivr_rel > 0.01) {
                    riv_neural_lr = (rivl_rel - rivr_rel) / (rivl_rel + rivr_rel);
                }
                // Blend: 40% natural RIV + 40% post-rev posture + 20% random exploration
                double mean_rel = (rivl_rel + rivr_rel) * 0.5;
                double combined_lr = 0.4 * riv_neural_lr + 0.4 * post_rev_lr;
                riv_omega_peak_l_ = mean_rel * (1.0 + combined_lr);
                riv_omega_peak_r_ = mean_rel * (1.0 - combined_lr);
            }
        }
    }

    // --- Omega EXECUTION + TERMINATION ---
    if (riv_omega_active_) {
        double omega_elapsed = current_time_ - riv_omega_start_;
        double decay = std::exp(-omega_elapsed / 150.0);  // 150ms tau: muscle Ca²⁺ clearance
        double omega_nmj_gain = 300.0 * decay;
        // Latched peak L/R from omega initiation (gradient-sampled direction)
        for (int seg = 0; seg < 6; ++seg) {
            double taper = 1.0 - 0.5 * (seg / 5.0);  // 100% at head, 50% at seg 5
            body_.muscles().add_boost(seg, true,  riv_omega_peak_l_ * omega_nmj_gain * taper);
            body_.muscles().add_boost(seg, false, riv_omega_peak_r_ * omega_nmj_gain * taper);
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
