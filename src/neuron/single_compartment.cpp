#include "neuron/single_compartment.h"

namespace celegans {

SingleCompartmentNeuron::SingleCompartmentNeuron()
    : calcium_(0.05, 200.0, 0.01) {
}

void SingleCompartmentNeuron::add_channel(std::unique_ptr<IonChannel> channel) {
    channels_.push_back(std::move(channel));
}

void SingleCompartmentNeuron::set_leak(double g_leak, double E_leak) {
    g_leak_ = g_leak;
    E_leak_ = E_leak;
}

void SingleCompartmentNeuron::step(double dt) {
    // Step 67: Ablated neurons skip all dynamics — dead cell
    if (ablated_) {
        V_ = E_leak_;
        I_syn_ = 0.0;
        I_ext_ = 0.0;
        return;
    }

    // 1. Compute leak current
    double I_leak = g_leak_ * (V_ - E_leak_);

    // 2. Compute ion channel currents
    double I_channels = 0.0;
    double I_Ca_total = 0.0;

    double Ca = calcium_.get_concentration();
    for (auto& ch : channels_) {
        ch->step(V_, Ca, dt);
        double I_ch = ch->get_current(V_);
        I_channels += I_ch;

        // Track calcium current for calcium dynamics
        if (ch->get_reversal_potential() > 40.0) {
            I_Ca_total += I_ch;
        }
    }

    // 3. Ion channel noise (thermal fluctuations in small neurons)
    // REF: White 1998, Faisal 2008 - stochastic channel gating
    double I_noise = noise_amplitude_ * noise_dist_(rng_);

    // 4. Membrane potential equation:
    // C_m * dV/dt = -(I_leak + I_channels) + I_syn + I_ext + I_noise
    double dV = (-(I_leak + I_channels) + I_syn_ + I_ext_ + I_noise) / C_m_;
    V_ += dV * dt;

    // 4. Update calcium dynamics
    calcium_.update(I_Ca_total, dt);

    // 5. Clamp voltage to physiological range
    if (V_ < -100.0) V_ = -100.0;
    if (V_ > 80.0) V_ = 80.0;
}

void SingleCompartmentNeuron::set_stretch_input(double stretch) {
    for (auto& ch : channels_) {
        auto* mec = dynamic_cast<MechanoSensitiveChannel*>(ch.get());
        if (mec) {
            mec->set_stretch(stretch);
        }
    }
}

} // namespace celegans
