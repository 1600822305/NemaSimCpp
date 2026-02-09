#pragma once

#include <cmath>

namespace celegans {

class CalciumDynamics {
public:
    CalciumDynamics(double baseline = 0.05, double tau = 200.0, double buffer_ratio = 0.01)
        : Ca_(baseline), Ca_baseline_(baseline), Ca_tau_(tau), buffer_ratio_(buffer_ratio) {}

    void update(double I_Ca, double dt) {
        // I_Ca is inward calcium current (negative = inward in our convention: I = g*(V-E))
        // For calcium, inward current increases [Ca]_i
        // dCa/dt = -buffer_ratio * I_Ca / (2*F*V_cell) - (Ca - Ca_baseline) / tau
        // Simplified: dCa/dt = -alpha * I_Ca - (Ca - Ca_baseline) / tau
        double alpha = buffer_ratio_ * 0.01;  // scaling factor (pA -> μM/ms)
        double dCa = -alpha * I_Ca - (Ca_ - Ca_baseline_) / Ca_tau_;
        Ca_ += dCa * dt;
        if (Ca_ < 0.0) Ca_ = 0.0;
    }

    double get_concentration() const { return Ca_; }
    void set_concentration(double Ca) { Ca_ = Ca; }

    double get_baseline() const { return Ca_baseline_; }

private:
    double Ca_;              // intracellular [Ca²⁺] (μM)
    double Ca_baseline_;     // resting [Ca²⁺] (~0.05 μM)
    double Ca_tau_;          // calcium decay time constant (ms)
    double buffer_ratio_;    // fraction of free calcium (rest is buffered)
};

} // namespace celegans
