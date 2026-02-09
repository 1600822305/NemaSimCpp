#pragma once

#include "core/types.h"
#include "neuron/ion_channel.h"
#include "neuron/calcium_dynamics.h"
#include <vector>
#include <memory>
#include <string>
#include <random>

namespace celegans {

class Neuron {
public:
    virtual ~Neuron() = default;

    virtual void step(double dt) = 0;

    virtual double get_membrane_potential() const = 0;

    // Graded transmitter release rate: sigmoid function of V
    virtual double get_transmitter_release_rate() const = 0;

    virtual double get_calcium() const { return 0.0; }

    void set_external_current(double I_ext) { I_ext_ = I_ext; }
    void add_synaptic_current(double I_syn) { I_syn_ += I_syn; }
    void reset_synaptic_current() { I_syn_ = 0.0; }

    const NeuronInfo& info() const { return info_; }
    NeuronInfo& info() { return info_; }
    int id() const { return info_.id; }
    const std::string& name() const { return info_.name; }

protected:
    NeuronInfo info_;
    double I_ext_ = 0.0;   // external input current (pA)
    double I_syn_ = 0.0;   // total synaptic current (pA)
};

class SingleCompartmentNeuron : public Neuron {
public:
    SingleCompartmentNeuron();

    void step(double dt) override;

    double get_membrane_potential() const override { return V_; }

    double get_transmitter_release_rate() const override {
        // Graded release: sigmoid(V) centered around threshold
        return 1.0 / (1.0 + std::exp(-(V_ - release_threshold_) / release_slope_));
    }

    double get_calcium() const override { return calcium_.get_concentration(); }

    // Channel management
    void add_channel(std::unique_ptr<IonChannel> channel);
    void set_leak(double g_leak, double E_leak);
    void set_capacitance(double C_m) { C_m_ = C_m; }
    void set_resting_potential(double V_rest) { V_ = V_rest; E_leak_ = V_rest; }

    // Configure release function
    void set_release_params(double threshold, double slope) {
        release_threshold_ = threshold;
        release_slope_ = slope;
    }

    double V() const { return V_; }

    // Configure calcium dynamics (for bursting neurons)
    void set_calcium_params(double baseline, double tau, double buffer_ratio) {
        calcium_ = CalciumDynamics(baseline, tau, buffer_ratio);
    }

    // Set stretch input for mechanosensitive channels (proprioception)
    void set_stretch_input(double stretch);

    // Access channels by type (for stretch setting)
    const std::vector<std::unique_ptr<IonChannel>>& channels() const { return channels_; }

private:
    double V_ = -60.0;        // membrane potential (mV)
    double C_m_ = 1.5;        // membrane capacitance (pF)
    double g_leak_ = 0.3;     // leak conductance (nS)
    double E_leak_ = -60.0;   // leak reversal potential (mV)

    double release_threshold_ = -35.0;  // mV
    double release_slope_ = 5.0;        // mV

    std::vector<std::unique_ptr<IonChannel>> channels_;
    CalciumDynamics calcium_;

    // Ion channel noise (thermal fluctuations)
    // REF: White 1998, Faisal 2008 - channel noise is significant in small neurons
    double noise_amplitude_ = 3.0;  // pA, stochastic ion channel noise
    std::mt19937 rng_{std::random_device{}()};
    std::normal_distribution<double> noise_dist_{0.0, 1.0};
};

} // namespace celegans
