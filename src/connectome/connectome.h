#pragma once

#include "core/types.h"
#include "neuron/single_compartment.h"
#include "connectome/chemical_synapse.h"
#include "connectome/gap_junction.h"
#include <vector>
#include <memory>
#include <unordered_map>

namespace celegans {

class Connectome {
public:
    Connectome() = default;

    // Build the network from loaded data
    void build(const std::vector<NeuronInfo>& neuron_infos,
               const std::vector<SynapseInfo>& synapse_infos,
               const std::vector<GapJunctionInfo>& gj_infos);

    // Compute all synaptic currents and apply to neurons (with STP update)
    void compute_synaptic_currents(std::vector<std::unique_ptr<Neuron>>& neurons, double dt);

    // Access
    size_t num_neurons() const { return neuron_infos_.size(); }
    size_t num_synapses() const { return synapses_.size(); }
    size_t num_gap_junctions() const { return gap_junctions_.size(); }

    const std::vector<ChemicalSynapse>& synapses() const { return synapses_; }
    std::vector<ChemicalSynapse>& synapses_mut() { return synapses_; }
    const std::vector<GapJunction>& gap_junctions() const { return gap_junctions_; }
    const std::vector<NeuronInfo>& neuron_infos() const { return neuron_infos_; }

    // Name -> ID lookup
    int get_neuron_id(const std::string& name) const {
        auto it = name_to_id_.find(name);
        return (it != name_to_id_.end()) ? it->second : -1;
    }

private:
    std::vector<NeuronInfo> neuron_infos_;
    std::vector<ChemicalSynapse> synapses_;
    std::vector<GapJunction> gap_junctions_;
    std::unordered_map<std::string, int> name_to_id_;

    // Synapse weight scaling factor (EM sections -> conductance)
    double synapse_weight_scale_ = 0.3;   // nS per EM section
    double gap_conductance_scale_ = 0.05; // nS per EM section
public:
    void set_synapse_scale(double s) { synapse_runtime_scale_ = s; }
    double get_synapse_scale() const { return synapse_runtime_scale_; }
private:
    double synapse_runtime_scale_ = 1.0;  // runtime multiplier on all synapse weights
};

} // namespace celegans
