#pragma once

// ================================================================
// Step 28: Multi-Compartment Neuron Model
//
// Extends the single-compartment HH model to support subcellular
// compartmentalization. Each compartment has its own:
//   - Membrane potential (V)
//   - Ion channels
//   - Calcium dynamics
//   - Synaptic input
//
// Compartments are coupled by axial resistance:
//   I_axial = g_axial * (V_a - V_b)
//
// Primary use case: RIA interneuron (Hendricks 2012 Nature)
//   - Soma: receives global glutamate input (AWC/ASE sensory)
//   - nrV (ventral axon): receives SMDVL ACh → GAR-3 → local Ca²⁺
//   - nrD (dorsal axon): receives SMDDL ACh → GAR-3 → local Ca²⁺
//   - Two signals are independent and additive → hardware multiply
//
// REF: Hendricks et al. 2012 Nature — compartmentalized calcium
//      dynamics in RIA encode head movement
// ================================================================

#include "neuron/single_compartment.h"
#include "neuron/ion_channel.h"
#include "neuron/calcium_dynamics.h"
#include <vector>
#include <memory>
#include <string>
#include <random>

namespace celegans {

struct Compartment {
    std::string label;            // e.g. "soma", "nrV", "nrD"
    double V = -60.0;            // membrane potential (mV)
    double C_m = 1.5;            // capacitance (pF)
    double g_leak = 0.3;         // leak conductance (nS)
    double E_leak = -55.0;       // leak reversal (mV)
    double I_syn = 0.0;          // synaptic current into this compartment
    double I_ext = 0.0;          // external current into this compartment

    std::vector<std::unique_ptr<IonChannel>> channels;
    CalciumDynamics calcium{0.05, 200.0, 0.01};

    // IP3-mediated Ca2+ store release (GAR-3 muscarinic pathway)
    // REF: Hendricks 2012 -- ACh -> GAR-3 -> IP3 -> intracellular Ca2+ stores
    // Depolarizing synaptic current directly triggers local Ca2+ release
    // This is NOT voltage-gated; it couples I_syn -> Ca2+
    double store_release_rate = 0.0;  // uM/ms per pA of depolarizing I_syn

    // Ion channel noise (thermal fluctuations)
    double noise_amplitude = 3.0;  // pA
    std::mt19937 rng{std::random_device{}()};
    std::normal_distribution<double> noise_dist{0.0, 1.0};

    double get_calcium() const { return calcium.get_concentration(); }
};

struct AxialCoupling {
    int comp_a;       // compartment index
    int comp_b;       // compartment index
    double g_axial;   // coupling conductance (nS)
};

class MultiCompartmentNeuron : public Neuron {
public:
    MultiCompartmentNeuron() = default;

    void step(double dt) override;

    // Returns soma (compartment 0) membrane potential for backward compatibility
    double get_membrane_potential() const override {
        return compartments_.empty() ? -60.0 : compartments_[0].V;
    }

    // Returns soma transmitter release rate
    double get_transmitter_release_rate() const override {
        double V = get_membrane_potential();
        return 1.0 / (1.0 + fast_exp(-(V - release_threshold_) / release_slope_));
    }

    double get_calcium() const override {
        return compartments_.empty() ? 0.0 : compartments_[0].get_calcium();
    }

    // --- Override base class current routing ---
    // All default (non-compartment) calls route to soma (compartment 0)
    void set_external_current(double I_ext) override {
        if (!compartments_.empty()) compartments_[0].I_ext = I_ext;
    }
    void add_synaptic_current(double I_syn) override {
        if (!compartments_.empty()) compartments_[0].I_syn += I_syn;
    }
    void reset_synaptic_current() override {
        for (auto& comp : compartments_) comp.I_syn = 0.0;
    }
    double get_I_ext() const override {
        return compartments_.empty() ? 0.0 : compartments_[0].I_ext;
    }
    double get_I_syn() const override {
        return compartments_.empty() ? 0.0 : compartments_[0].I_syn;
    }

    // Compartment-targeted synaptic current
    void add_compartment_current(int compartment, double I) override {
        if (compartment >= 0 && compartment < (int)compartments_.size())
            compartments_[compartment].I_syn += I;
    }

    // --- Compartment management ---
    int add_compartment(const std::string& label, double C_m, double g_leak, double E_leak);
    void add_channel_to_compartment(int comp, std::unique_ptr<IonChannel> channel);
    void add_axial_coupling(int comp_a, int comp_b, double g_axial);
    void set_compartment_calcium_params(int comp, double baseline, double tau, double buffer_ratio);
    void set_compartment_noise(int comp, double amplitude);

    // --- Access ---
    int num_compartments() const { return static_cast<int>(compartments_.size()); }
    const Compartment& compartment(int i) const { return compartments_[i]; }
    Compartment& compartment_mut(int i) { return compartments_[i]; }

    // Compartment-specific calcium
    double get_compartment_calcium(int comp) const {
        if (comp >= 0 && comp < (int)compartments_.size())
            return compartments_[comp].get_calcium();
        return 0.0;
    }

    // Compartment-specific voltage
    double get_compartment_V(int comp) const {
        if (comp >= 0 && comp < (int)compartments_.size())
            return compartments_[comp].V;
        return -60.0;
    }

    // Compartment-specific transmitter release rate
    double get_compartment_release_rate(int comp) const {
        double V = get_compartment_V(comp);
        return 1.0 / (1.0 + fast_exp(-(V - release_threshold_) / release_slope_));
    }

    // Compartment-specific external current
    void set_compartment_external_current(int comp, double I_ext) {
        if (comp >= 0 && comp < (int)compartments_.size())
            compartments_[comp].I_ext = I_ext;
    }

    void set_release_params(double threshold, double slope) {
        release_threshold_ = threshold;
        release_slope_ = slope;
    }

private:
    std::vector<Compartment> compartments_;
    std::vector<AxialCoupling> couplings_;

    double release_threshold_ = -35.0;
    double release_slope_ = 5.0;
};

} // namespace celegans
