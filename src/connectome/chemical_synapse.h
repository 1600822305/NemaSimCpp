#pragma once

#include "core/types.h"
#include <cmath>

namespace celegans {

class ChemicalSynapse {
public:
    ChemicalSynapse() = default;

    ChemicalSynapse(int pre_id, int post_id, double weight,
                    NeurotransmitterType nt, double E_syn)
        : pre_id_(pre_id), post_id_(post_id), weight_(weight),
          nt_(nt), E_syn_(E_syn) {}

    // Compute synaptic current flowing into post-synaptic neuron
    // Graded synapse: conductance depends on presynaptic V via sigmoid
    double compute_current(double V_pre, double V_post) const {
        // Graded transmitter release: S(V_pre)
        double S = 1.0 / (1.0 + std::exp(-(V_pre - V_thresh_) / V_slope_));
        // Synaptic current: I = g_max * S * (V_post - E_syn)
        // Return current INTO post neuron (positive = depolarizing if E_syn > V_post)
        return -weight_ * g_max_ * S * (V_post - E_syn_);
    }

    int pre_id() const { return pre_id_; }
    int post_id() const { return post_id_; }
    double weight() const { return weight_; }
    void set_weight(double w) { weight_ = w; }
    NeurotransmitterType neurotransmitter() const { return nt_; }
    double reversal_potential() const { return E_syn_; }

    void set_params(double g_max, double V_thresh, double V_slope) {
        g_max_ = g_max;
        V_thresh_ = V_thresh;
        V_slope_ = V_slope;
    }

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
    double weight_ = 1.0;          // weight scaling (proportional to EM section count)
    NeurotransmitterType nt_ = NeurotransmitterType::UNKNOWN;
    double E_syn_ = -10.0;         // synaptic reversal potential (mV)
    double g_max_ = 0.5;           // max synaptic conductance per unit weight (nS)
    double V_thresh_ = -35.0;      // presynaptic voltage threshold for release (mV)
    double V_slope_ = 5.0;         // steepness of release sigmoid (mV)
};

} // namespace celegans
