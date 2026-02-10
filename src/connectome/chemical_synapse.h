#pragma once

#include "core/types.h"
#include <cmath>

namespace celegans {

class ChemicalSynapse {
public:
    ChemicalSynapse() = default;

    ChemicalSynapse(int pre_id, int post_id, double weight,
                    NeurotransmitterType nt, double E_syn,
                    int post_compartment = 0)
        : pre_id_(pre_id), post_id_(post_id), weight_(weight),
          nt_(nt), E_syn_(E_syn), post_compartment_(post_compartment) {}

    // Compute synaptic current with short-term plasticity (Step 21)
    // Updates vesicle pool and release probability state, then computes current
    // Graded synapse: conductance depends on presynaptic V via sigmoid
    double compute_current(double V_pre, double V_post, double dt) {
        // Graded transmitter release: S(V_pre)
        double S = 1.0 / (1.0 + std::exp(-(V_pre - V_thresh_) / V_slope_));

        // --- Short-Term Plasticity (Tsodyks-Markram for graded synapses) ---
        // STD: vesicle pool recovery and depletion
        // dn/dt = (1 - n) / tau_rec - alpha_d * S * n
        double dn = (1.0 - vesicle_pool_) / tau_recovery_ - alpha_d_ * S * vesicle_pool_;
        vesicle_pool_ += dn * dt;
        if (vesicle_pool_ < 0.01) vesicle_pool_ = 0.01;  // minimum pool
        if (vesicle_pool_ > 1.0) vesicle_pool_ = 1.0;

        // STF: release probability facilitation and decay
        // dp/dt = (p0 - p) / tau_facil + alpha_f * S * (1 - p)
        double dp = (p0_ - release_prob_) / tau_facil_ + alpha_f_ * S * (1.0 - release_prob_);
        release_prob_ += dp * dt;
        if (release_prob_ < p0_ * 0.1) release_prob_ = p0_ * 0.1;  // minimum
        if (release_prob_ > 1.0) release_prob_ = 1.0;

        // Effective synaptic strength: n * (p/p0) modulates baseline
        double stp_factor = vesicle_pool_ * (release_prob_ / p0_);

        // Synaptic current: I = g_max * weight * stp * S * (V_post - E_syn)
        // weight_mod_ allows slow learning-dependent modulation (Step 21c)
        return -weight_ * weight_mod_ * g_max_ * stp_factor * S * (V_post - E_syn_);
    }

    // Legacy const version (no plasticity update, for read-only use)
    double compute_current(double V_pre, double V_post) const {
        double S = 1.0 / (1.0 + std::exp(-(V_pre - V_thresh_) / V_slope_));
        return -weight_ * weight_mod_ * g_max_ * S * (V_post - E_syn_);
    }

    int pre_id() const { return pre_id_; }
    int post_id() const { return post_id_; }
    int post_compartment() const { return post_compartment_; }
    double weight() const { return weight_; }
    void set_weight(double w) { weight_ = w; }
    NeurotransmitterType neurotransmitter() const { return nt_; }
    double reversal_potential() const { return E_syn_; }

    void set_params(double g_max, double V_thresh, double V_slope) {
        g_max_ = g_max;
        V_thresh_ = V_thresh;
        V_slope_ = V_slope;
    }

    // Short-term plasticity accessors (Step 21a/b)
    double vesicle_pool() const { return vesicle_pool_; }
    double release_prob() const { return release_prob_; }
    void set_stp_params(double tau_rec, double alpha_d, double tau_f, double alpha_f, double p0) {
        tau_recovery_ = tau_rec;
        alpha_d_ = alpha_d;
        tau_facil_ = tau_f;
        alpha_f_ = alpha_f;
        p0_ = p0;
        release_prob_ = p0;
    }

    // Learning-dependent weight modulation (Step 21c)
    double weight_mod() const { return weight_mod_; }
    void set_weight_mod(double m) { weight_mod_ = m; }
    void adjust_weight_mod(double delta) { weight_mod_ += delta; if (weight_mod_ < 0.1) weight_mod_ = 0.1; if (weight_mod_ > 3.0) weight_mod_ = 3.0; }

    // Determine if excitatory based on neurotransmitter type
    bool is_excitatory() const {
        switch (nt_) {
            case NeurotransmitterType::ACETYLCHOLINE: return true;
            case NeurotransmitterType::GLUTAMATE:     return true;
            case NeurotransmitterType::GABA:          return false;
            default: return true;
        }
    }

    // Get default reversal potential for neurotransmitter type
    static double default_reversal(NeurotransmitterType nt) {
        switch (nt) {
            case NeurotransmitterType::ACETYLCHOLINE: return -10.0;  // excitatory
            case NeurotransmitterType::GLUTAMATE:     return -10.0;  // excitatory
            case NeurotransmitterType::GABA:          return -70.0;  // inhibitory
            default: return -10.0;
        }
    }

private:
    int pre_id_ = -1;
    int post_id_ = -1;
    int post_compartment_ = 0;     // Step 28: target compartment (0=soma)
    double weight_ = 1.0;          // weight scaling (proportional to EM section count)
    NeurotransmitterType nt_ = NeurotransmitterType::UNKNOWN;
    double E_syn_ = -10.0;         // synaptic reversal potential (mV)
    double g_max_ = 0.5;           // max synaptic conductance per unit weight (nS)
    double V_thresh_ = -35.0;      // presynaptic voltage threshold for release (mV)
    double V_slope_ = 5.0;         // steepness of release sigmoid (mV)

    // --- Short-Term Plasticity state (Step 21a/b) ---
    // Tsodyks-Markram model adapted for graded synapses
    // REF: Liu 2009 PNAS (C. elegans graded NMJ), Tsodyks & Markram 1997
    double vesicle_pool_ = 1.0;    // n(t) ∈ [0.01, 1] — available vesicle fraction
    double release_prob_ = 0.5;    // p(t) — dynamic release probability
    double p0_ = 0.5;              // baseline release probability
    double tau_recovery_ = 2000.0; // ms, vesicle recovery time constant (STD)
    double alpha_d_ = 0.5;         // depletion rate per unit release
    double tau_facil_ = 200.0;     // ms, facilitation decay time constant (STF)
    double alpha_f_ = 0.1;         // facilitation rate per unit release

    // --- Learning-dependent weight modulation (Step 21c) ---
    // Slow timescale weight change (insulin/PI3K pathway, salt learning)
    double weight_mod_ = 1.0;      // [0.1, 3.0] — multiplicative weight modulator
};

} // namespace celegans
