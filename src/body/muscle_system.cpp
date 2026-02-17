#include "body/muscle_system.h"
#include <cmath>
#include <algorithm>

namespace celegans {

void MuscleSystem::reset_inputs() {
    for (auto& m : dorsal_)  m.reset_inputs();
    for (auto& m : ventral_) m.reset_inputs();
}

void MuscleSystem::add_excitatory(int segment, bool dorsal, double input) {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return;
    // Max semantics: dominant motor neuron sets activation level
    // Prevents saturation when multiple MNs innervate same segment
    if (dorsal)
        dorsal_[segment].excitatory_input = std::max(dorsal_[segment].excitatory_input, input);
    else
        ventral_[segment].excitatory_input = std::max(ventral_[segment].excitatory_input, input);
}

void MuscleSystem::add_boost(int segment, bool dorsal, double input) {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return;
    // Sum semantics: specialized MNs (RIV, SMB) add on top of normal drive
    // Stacks with excitatory_input (max) in MuscleCell::step()
    if (dorsal)
        dorsal_[segment].boost_input += input;
    else
        ventral_[segment].boost_input += input;
}

void MuscleSystem::add_inhibitory(int segment, bool dorsal, double input) {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return;
    if (dorsal)
        dorsal_[segment].inhibitory_input += input;
    else
        ventral_[segment].inhibitory_input += input;
}

void MuscleSystem::step(double dt_ms) {
    for (auto& m : dorsal_)  m.step(dt_ms);
    for (auto& m : ventral_) m.step(dt_ms);
}

double MuscleSystem::get_dorsal_force(int seg) const {
    if (seg < 0 || seg >= NUM_BODY_SEGMENTS) return 0.0;
    return dorsal_[seg].activation * neuromod_gain_;
}

double MuscleSystem::get_ventral_force(int seg) const {
    if (seg < 0 || seg >= NUM_BODY_SEGMENTS) return 0.0;
    return ventral_[seg].activation * neuromod_gain_;
}

double MuscleSystem::get_mean_abs_force() const {
    double sum = 0.0;
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        sum += std::abs(dorsal_[i].activation - ventral_[i].activation);
    }
    return (sum / NUM_BODY_SEGMENTS) * neuromod_gain_;
}

double MuscleSystem::get_dorsal_activation(int seg) const {
    if (seg < 0 || seg >= NUM_BODY_SEGMENTS) return 0.0;
    return dorsal_[seg].activation;
}

double MuscleSystem::get_ventral_activation(int seg) const {
    if (seg < 0 || seg >= NUM_BODY_SEGMENTS) return 0.0;
    return ventral_[seg].activation;
}

void MuscleSystem::set_muscle_tau(double tau_ms) {
    for (auto& m : dorsal_)  m.tau = tau_ms;
    for (auto& m : ventral_) m.tau = tau_ms;
}

} // namespace celegans
