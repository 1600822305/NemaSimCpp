#pragma once

#include "neuron/single_compartment.h"
#include "neuron/multi_compartment.h"
#include "neuron/ion_channel.h"
#include "core/types.h"
#include <memory>
#include <vector>

namespace celegans {

class NeuronFactory {
public:
    // Create a default neuron with standard channel complement
    static std::unique_ptr<SingleCompartmentNeuron> create_default(const NeuronInfo& info);

    // Create a neuron specifically configured for motor neurons
    static std::unique_ptr<SingleCompartmentNeuron> create_motor(const NeuronInfo& info);

    // Create a neuron configured for sensory neurons
    static std::unique_ptr<SingleCompartmentNeuron> create_sensory(const NeuronInfo& info);

    // Create a neuron configured for interneurons
    static std::unique_ptr<SingleCompartmentNeuron> create_inter(const NeuronInfo& info);

    // Specialized motor neuron subtypes
    static std::unique_ptr<SingleCompartmentNeuron> create_motor_b_class(const NeuronInfo& info);
    static std::unique_ptr<SingleCompartmentNeuron> create_motor_head(const NeuronInfo& info);

    // Step 28: Multi-compartment neuron (RIA)
    // REF: Hendricks 2012 Nature — compartmentalized Ca²⁺ in RIA axon
    static std::unique_ptr<MultiCompartmentNeuron> create_ria_multi(const NeuronInfo& info);

    // Create based on NeuronType + name-based specialization
    // Returns Neuron* (polymorphic: can be Single or Multi compartment)
    static std::unique_ptr<Neuron> create(const NeuronInfo& info);
};

} // namespace celegans
