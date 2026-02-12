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
    // Step 57: IRK (resting potential stabilization) + TWK (background leak)
    neuron->add_channel(std::make_unique<IRKChannel>(0.2));
    neuron->add_channel(std::make_unique<TWKChannel>(0.08));

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
    // Step 57: EGL-36 (delayed rectifier) + SLO-2 (Na-activated K⁺) + IRK
    neuron->add_channel(std::make_unique<EGL36Channel>(0.6));
    neuron->add_channel(std::make_unique<SLO2Channel>(0.8));
    neuron->add_channel(std::make_unique<IRKChannel>(0.15));

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
    // Step 57: EGL-36 (repolarization) + IRK (resting) + TWK (background)
    neuron->add_channel(std::make_unique<EGL36Channel>(0.5));
    neuron->add_channel(std::make_unique<IRKChannel>(0.25));
    neuron->add_channel(std::make_unique<TWKChannel>(0.12));

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
    // Step 57: EGL-36 (repolarization) + SLO-2 (Na-activated K⁺) + IRK
    neuron->add_channel(std::make_unique<EGL36Channel>(0.4));
    neuron->add_channel(std::make_unique<SLO2Channel>(0.6));
    neuron->add_channel(std::make_unique<IRKChannel>(0.2));

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
    // Step 57: EGL-36 + SLO-2 + IRK (same as generic motor)
    neuron->add_channel(std::make_unique<EGL36Channel>(0.6));
    neuron->add_channel(std::make_unique<SLO2Channel>(0.8));
    neuron->add_channel(std::make_unique<IRKChannel>(0.15));

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
    // Step 57: EGL-36 (repolarization backup) — low g_max, CCA-1/SLO-1 dominate
    neuron->add_channel(std::make_unique<EGL36Channel>(0.3));

    // Fast calcium dynamics for bursting: 10x sensitivity, 100ms decay
    // Standard neurons: buffer_ratio=0.01, tau=200ms → too slow for oscillation
    // Head motor: buffer_ratio=0.1, tau=100ms → Ca rises fast during burst, decays in ~300ms
    neuron->set_calcium_params(0.05, 100.0, 0.1);

    return neuron;
}

static bool name_starts_with(const std::string& name, const char* prefix) {
    return name.compare(0, std::strlen(prefix), prefix) == 0;
}

// ================================================================
// Step 28: RIA multi-compartment neuron
//
// Hendricks 2012 Nature: RIA axon is divided into nrV (ventral) and
// nrD (dorsal) domains with independent compartmentalized Ca²⁺.
//   - Soma: receives global glutamate input from sensory pathways
//   - nrV: receives ACh from ventral head motor neurons (SMDVL/SMDVR)
//          via GAR-3 muscarinic receptor → IP3 → intracellular Ca²⁺
//   - nrD: receives ACh from dorsal head motor neurons (SMDDL/SMDDR)
//          via GAR-3 muscarinic receptor → IP3 → intracellular Ca²⁺
//
// Axial coupling: soma ↔ nrV, soma ↔ nrD (moderate, allows local)
// nrV ↔ nrD: weak coupling (independent domains, Hendricks 2012)
//
// Compartment indices: 0=soma, 1=nrV, 2=nrD
// ================================================================
std::unique_ptr<MultiCompartmentNeuron> NeuronFactory::create_ria_multi(const NeuronInfo& info) {
    auto neuron = std::make_unique<MultiCompartmentNeuron>();
    neuron->info() = info;

    // Compartment 0: Soma — receives sensory glutamate (AWC/ASE→AIA→AIY→RIA)
    int soma = neuron->add_compartment("soma", 1.5, 0.3, -55.0);
    neuron->add_channel_to_compartment(soma, std::make_unique<EGL19Channel>(0.8));
    neuron->add_channel_to_compartment(soma, std::make_unique<SHL1Channel>(1.2));
    neuron->add_channel_to_compartment(soma, std::make_unique<KQT3Channel>(0.3));
    neuron->add_channel_to_compartment(soma, std::make_unique<NCAChannel>(0.15));

    // Compartment 1: nrV (ventral axon domain)
    // Receives ACh from SMDVL → GAR-3 → IP3 → local Ca²⁺ release
    // Sensitive calcium dynamics: IP3-mediated stores are fast and local
    int nrV = neuron->add_compartment("nrV", 0.8, 0.2, -55.0);
    neuron->add_channel_to_compartment(nrV, std::make_unique<EGL19Channel>(1.2));  // L-type Ca for local signal
    neuron->add_channel_to_compartment(nrV, std::make_unique<SHL1Channel>(0.8));
    neuron->set_compartment_calcium_params(nrV, 0.05, 80.0, 0.15);  // fast, sensitive Ca²⁺
    neuron->set_compartment_noise(nrV, 1.5);  // less noise in axon
    neuron->compartment_mut(nrV).store_release_rate = 0.0003;  // GAR-3 → IP3 → Ca²⁺ store

    // Compartment 2: nrD (dorsal axon domain)
    // Same as nrV but receives ACh from SMDDL
    int nrD = neuron->add_compartment("nrD", 0.8, 0.2, -55.0);
    neuron->add_channel_to_compartment(nrD, std::make_unique<EGL19Channel>(1.2));
    neuron->add_channel_to_compartment(nrD, std::make_unique<SHL1Channel>(0.8));
    neuron->set_compartment_calcium_params(nrD, 0.05, 80.0, 0.15);
    neuron->set_compartment_noise(nrD, 1.5);
    neuron->compartment_mut(nrD).store_release_rate = 0.0003;  // GAR-3 → IP3 → Ca²⁺ store

    // Axial coupling: soma ↔ axon domains
    // Moderate coupling allows global signals to spread but preserves local activity
    // REF: Hendricks 2012 — "simultaneously present and additive"
    neuron->add_axial_coupling(soma, nrV, 0.15);  // soma ↔ nrV: weak (isolate feedback)
    neuron->add_axial_coupling(soma, nrD, 0.15);  // soma ↔ nrD: weak
    neuron->add_axial_coupling(nrV, nrD, 0.01);   // nrV ↔ nrD: very weak (independent)

    return neuron;
}

std::unique_ptr<Neuron> NeuronFactory::create(const NeuronInfo& info) {
    // Step 28: RIA → multi-compartment (Hendricks 2012)
    if (name_starts_with(info.name, "RIA")) {
        return create_ria_multi(info);
    }

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
        // Step 57: AVL/DVB enteric motor neurons — EXP-2 for AP repolarization
        // Compound APs: UNC-2 Ca²⁺ spike + EXP-2 K⁺ repolarization (Jiang 2022)
        if (info.name == "AVL" || info.name == "DVB") {
            auto n = create_motor(info);
            n->add_channel(std::make_unique<EXP2Channel>(2.5)); // strong EXP-2 for AP repolarization
            return n;
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
