#include "connectome/connectome.h"
#include "core/logger.h"

namespace celegans {

void Connectome::build(const std::vector<NeuronInfo>& neuron_infos,
                       const std::vector<SynapseInfo>& synapse_infos,
                       const std::vector<GapJunctionInfo>& gj_infos) {
    neuron_infos_ = neuron_infos;
    name_to_id_.clear();
    for (auto& ni : neuron_infos_) {
        name_to_id_[ni.name] = ni.id;
    }

    // Build chemical synapses
    synapses_.clear();
    synapses_.reserve(synapse_infos.size());
    for (auto& si : synapse_infos) {
        NeurotransmitterType nt = si.neurotransmitter;
        if (nt == NeurotransmitterType::UNKNOWN && si.pre_neuron_id >= 0 &&
            si.pre_neuron_id < static_cast<int>(neuron_infos_.size())) {
            nt = neuron_infos_[si.pre_neuron_id].neurotransmitter;
        }

        double E_syn = ChemicalSynapse::default_reversal(nt);
        double weight = si.num_sections * synapse_weight_scale_;

        ChemicalSynapse syn(si.pre_neuron_id, si.post_neuron_id, weight, nt, E_syn);
        synapses_.push_back(syn);
    }

    // Build gap junctions
    gap_junctions_.clear();
    gap_junctions_.reserve(gj_infos.size());
    for (auto& gi : gj_infos) {
        double conductance = gi.num_sections * gap_conductance_scale_;
        gap_junctions_.emplace_back(gi.neuron_a_id, gi.neuron_b_id, conductance);
    }

    LOG_INFO("Connectome built: ", neuron_infos_.size(), " neurons, ",
             synapses_.size(), " synapses, ", gap_junctions_.size(), " gap junctions");
}

void Connectome::compute_synaptic_currents(std::vector<std::unique_ptr<Neuron>>& neurons) {
    // Reset all synaptic currents
    for (auto& n : neurons) {
        n->reset_synaptic_current();
    }

    int n_size = static_cast<int>(neurons.size());

    // Chemical synapses: graded transmission
    for (auto& syn : synapses_) {
        int pre = syn.pre_id();
        int post = syn.post_id();
        if (pre < 0 || pre >= n_size || post < 0 || post >= n_size) continue;

        double V_pre = neurons[pre]->get_membrane_potential();
        double V_post = neurons[post]->get_membrane_potential();
        double I = syn.compute_current(V_pre, V_post);
        neurons[post]->add_synaptic_current(I);
    }

    // Gap junctions: bidirectional ohmic coupling
    for (auto& gj : gap_junctions_) {
        int a = gj.neuron_a();
        int b = gj.neuron_b();
        if (a < 0 || a >= n_size || b < 0 || b >= n_size) continue;

        double V_a = neurons[a]->get_membrane_potential();
        double V_b = neurons[b]->get_membrane_potential();
        double I = gj.compute_current(V_a, V_b);
        // Current flows from A to B: A loses current, B gains current
        neurons[a]->add_synaptic_current(-I);
        neurons[b]->add_synaptic_current(I);
    }
}

} // namespace celegans
