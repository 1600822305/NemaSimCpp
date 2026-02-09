#include "neuron/neuron_factory.h"
#include "core/logger.h"

namespace celegans {

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create_default(const NeuronInfo& info) {
    auto neuron = std::make_unique<SingleCompartmentNeuron>();
    neuron->info() = info;

    neuron->set_capacitance(1.5);    // pF
    neuron->set_leak(0.3, -55.0);    // nS, mV
    neuron->set_resting_potential(-55.0);

    // Default channel complement: EGL-19 (Ca_L) + SHL-1 (K_A) + NCA (Na_leak)
    neuron->add_channel(std::make_unique<EGL19Channel>(0.8));
    neuron->add_channel(std::make_unique<SHL1Channel>(1.0));
    neuron->add_channel(std::make_unique<NCAChannel>(0.15));

    return neuron;
}

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create_motor(const NeuronInfo& info) {
    auto neuron = std::make_unique<SingleCompartmentNeuron>();
    neuron->info() = info;

    neuron->set_capacitance(2.0);
    neuron->set_leak(0.4, -55.0);
    neuron->set_resting_potential(-55.0);

    // Motor neurons: stronger calcium channels for muscle drive
    neuron->add_channel(std::make_unique<EGL19Channel>(1.2));
    neuron->add_channel(std::make_unique<UNC2Channel>(0.8));
    neuron->add_channel(std::make_unique<SHL1Channel>(1.5));
    neuron->add_channel(std::make_unique<KQT3Channel>(0.3));
    neuron->add_channel(std::make_unique<NCAChannel>(0.12));

    return neuron;
}

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create_sensory(const NeuronInfo& info) {
    auto neuron = std::make_unique<SingleCompartmentNeuron>();
    neuron->info() = info;

    neuron->set_capacitance(1.2);
    neuron->set_leak(0.25, -60.0);
    neuron->set_resting_potential(-60.0);

    // Sensory neurons: moderate channels, more hyperpolarized rest
    neuron->add_channel(std::make_unique<EGL19Channel>(0.6));
    neuron->add_channel(std::make_unique<SHL1Channel>(0.8));
    neuron->add_channel(std::make_unique<KQT3Channel>(0.4));
    neuron->add_channel(std::make_unique<NCAChannel>(0.10));

    return neuron;
}

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create_inter(const NeuronInfo& info) {
    auto neuron = std::make_unique<SingleCompartmentNeuron>();
    neuron->info() = info;

    neuron->set_capacitance(1.5);
    neuron->set_leak(0.3, -55.0);
    neuron->set_resting_potential(-55.0);

    // Interneurons: balanced channel complement
    neuron->add_channel(std::make_unique<EGL19Channel>(0.8));
    neuron->add_channel(std::make_unique<SHL1Channel>(1.2));
    neuron->add_channel(std::make_unique<KQT3Channel>(0.3));
    neuron->add_channel(std::make_unique<SLO1Channel>(1.0));
    neuron->add_channel(std::make_unique<NCAChannel>(0.15));

    return neuron;
}

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create_motor_b_class(const NeuronInfo& info) {
    auto neuron = std::make_unique<SingleCompartmentNeuron>();
    neuron->info() = info;

    neuron->set_capacitance(2.0);
    neuron->set_leak(0.4, -55.0);
    neuron->set_resting_potential(-55.0);

    // B-class: standard motor channels + MechanoSensitive channel for proprioception
    // REF: Wen et al. 2012 - proprioceptive coupling within motor neurons
    neuron->add_channel(std::make_unique<EGL19Channel>(1.2));
    neuron->add_channel(std::make_unique<UNC2Channel>(0.8));
    neuron->add_channel(std::make_unique<SHL1Channel>(1.5));
    neuron->add_channel(std::make_unique<KQT3Channel>(0.3));
    neuron->add_channel(std::make_unique<NCAChannel>(0.12));
    neuron->add_channel(std::make_unique<MechanoSensitiveChannel>(3.0, -10.0));

    return neuron;
}

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create_motor_head(const NeuronInfo& info) {
    auto neuron = std::make_unique<SingleCompartmentNeuron>();
    neuron->info() = info;

    neuron->set_capacitance(1.8);
    neuron->set_leak(1.0, -60.0);  // strong leak → CCA-1 must overcome it for burst
    neuron->set_resting_potential(-60.0);

    // Head motor neurons (RMD/SMD): CCA-1 intrinsic bursting oscillator
    // Oscillation mechanism: CCA-1 burst → Ca²⁺ rise → SLO-1 adaptation → repolarize → Ca decay → repeat
    // REF: Hendricks 2012, Nicoletti 2019 - CCA-1 critical for head oscillation
    neuron->add_channel(std::make_unique<EGL19Channel>(0.3));
    neuron->add_channel(std::make_unique<CCA1Channel>(5.0));   // dominant inward for burst
    neuron->add_channel(std::make_unique<SHL1Channel>(1.5));
    neuron->add_channel(std::make_unique<KQT3Channel>(0.3));
    neuron->add_channel(std::make_unique<SLO1Channel>(5.0));   // strong BK for Ca-dependent adaptation
    neuron->add_channel(std::make_unique<NCAChannel>(0.02));   // minimal persistent inward

    // Fast calcium dynamics for bursting: 10x sensitivity, 100ms decay
    // Standard neurons: buffer_ratio=0.01, tau=200ms → too slow for oscillation
    // Head motor: buffer_ratio=0.1, tau=100ms → Ca rises fast during burst, decays in ~300ms
    neuron->set_calcium_params(0.05, 100.0, 0.1);

    return neuron;
}

static bool name_starts_with(const std::string& name, const char* prefix) {
    return name.compare(0, std::strlen(prefix), prefix) == 0;
}

std::unique_ptr<SingleCompartmentNeuron> NeuronFactory::create(const NeuronInfo& info) {
    if (info.type == NeuronType::MOTOR) {
        // B-class (DB/VB) and A-class (DA/VA) motor neurons get MEC channels
        if (name_starts_with(info.name, "DB") || name_starts_with(info.name, "VB") ||
            name_starts_with(info.name, "DA") || name_starts_with(info.name, "VA")) {
            return create_motor_b_class(info);
        }
        // Head motor neurons (SMD/RMD) get strong CCA-1 for oscillation
        if (name_starts_with(info.name, "SMD") || name_starts_with(info.name, "RMD")) {
            return create_motor_head(info);
        }
        return create_motor(info);
    }
    switch (info.type) {
        case NeuronType::SENSORY:    return create_sensory(info);
        case NeuronType::INTER:      return create_inter(info);
        default:                     return create_default(info);
    }
}

} // namespace celegans
