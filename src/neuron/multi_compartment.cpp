#include "neuron/multi_compartment.h"

namespace celegans {

int MultiCompartmentNeuron::add_compartment(const std::string& label,
                                             double C_m, double g_leak, double E_leak) {
    Compartment comp;
    comp.label = label;
    comp.V = E_leak;  // start at resting potential
    comp.C_m = C_m;
    comp.g_leak = g_leak;
    comp.E_leak = E_leak;
    compartments_.push_back(std::move(comp));
    return static_cast<int>(compartments_.size()) - 1;
}

void MultiCompartmentNeuron::add_channel_to_compartment(int comp,
                                                         std::unique_ptr<IonChannel> channel) {
    if (comp >= 0 && comp < (int)compartments_.size()) {
        compartments_[comp].channels.push_back(std::move(channel));
    }
}

void MultiCompartmentNeuron::add_axial_coupling(int comp_a, int comp_b, double g_axial) {
    couplings_.push_back({comp_a, comp_b, g_axial});
}

void MultiCompartmentNeuron::set_compartment_calcium_params(int comp,
                                                             double baseline, double tau, double buffer_ratio) {
    if (comp >= 0 && comp < (int)compartments_.size()) {
        compartments_[comp].calcium = CalciumDynamics(baseline, tau, buffer_ratio);
    }
}

void MultiCompartmentNeuron::set_compartment_noise(int comp, double amplitude) {
    if (comp >= 0 && comp < (int)compartments_.size()) {
        compartments_[comp].noise_amplitude = amplitude;
    }
}

void MultiCompartmentNeuron::step(double dt) {
    if (compartments_.empty()) return;

    int nc = static_cast<int>(compartments_.size());

    // 1. Compute axial currents between compartments
    //    I_axial = g_axial * (V_a - V_b)
    //    Current flows from high V to low V: A loses, B gains
    //    Store as temporary per-compartment accumulator
    std::vector<double> I_axial(nc, 0.0);
    for (auto& coupling : couplings_) {
        int a = coupling.comp_a;
        int b = coupling.comp_b;
        if (a < 0 || a >= nc || b < 0 || b >= nc) continue;
        double I = coupling.g_axial * (compartments_[a].V - compartments_[b].V);
        I_axial[a] -= I;  // A loses current
        I_axial[b] += I;  // B gains current
    }

    // 2. Step each compartment independently
    for (int i = 0; i < nc; ++i) {
        auto& comp = compartments_[i];

        // Leak current
        double I_leak = comp.g_leak * (comp.V - comp.E_leak);

        // Ion channel currents
        double I_channels = 0.0;
        double I_Ca_total = 0.0;
        double Ca = comp.calcium.get_concentration();

        for (auto& ch : comp.channels) {
            ch->step(comp.V, Ca, dt);
            double I_ch = ch->get_current(comp.V);
            I_channels += I_ch;
            // Track calcium current (E_rev > 40 mV → calcium channel)
            if (ch->get_reversal_potential() > 40.0) {
                I_Ca_total += I_ch;
            }
        }

        // Ion channel noise
        double I_noise = comp.noise_amplitude * comp.noise_dist(comp.rng);

        // Membrane equation:
        // C_m * dV/dt = -(I_leak + I_channels) + I_syn + I_ext + I_noise + I_axial
        double dV = (-(I_leak + I_channels) + comp.I_syn + comp.I_ext + I_noise + I_axial[i]) / comp.C_m;
        comp.V += dV * dt;

        // Update calcium dynamics (voltage-gated channels)
        comp.calcium.update(I_Ca_total, dt);

        // IP3-mediated Ca²⁺ store release (GAR-3 muscarinic pathway)
        // REF: Hendricks 2012 — ACh → GAR-3 → Gq → PLC → IP3 → ER Ca²⁺ release
        // Depolarizing synaptic current (I_syn > 0) triggers local store release
        // This is the key mechanism for compartmentalized calcium signals
        if (comp.store_release_rate > 0.0 && comp.I_syn > 0.0) {
            // Step 129d: Sensory × motor multiplication (Hendricks 2012)
            // Soma voltage (comp 0) reflects AIY sensory input (phase-locked to head sweep).
            // Modulate store release by soma depolarization: when soma is more depolarized
            // (higher sensory input), motor-driven Ca²⁺ store release is amplified.
            // Ca_nrD - Ca_nrV ∝ sensory_mod × motor_diff → encodes ∇C_⊥
            double sensory_mod = 1.0;
            if (i > 0 && !compartments_.empty()) {
                double soma_dV = compartments_[0].V - compartments_[0].E_leak;
                sensory_mod = 1.0 + 0.50 * soma_dV;  // +50% per mV above rest
                if (sensory_mod < 0.1) sensory_mod = 0.1;
            }
            double dCa_store = comp.store_release_rate * sensory_mod * comp.I_syn * dt;
            double current_Ca = comp.calcium.get_concentration();
            comp.calcium.set_concentration(current_Ca + dCa_store);
        }

        // Clamp voltage to physiological range
        if (comp.V < -100.0) comp.V = -100.0;
        if (comp.V > 80.0) comp.V = 80.0;
    }
}

} // namespace celegans
