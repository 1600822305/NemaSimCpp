#pragma once

#include "core/types.h"

namespace celegans {

class GapJunction {
public:
    GapJunction() = default;

    GapJunction(int neuron_a, int neuron_b, double conductance)
        : neuron_a_(neuron_a), neuron_b_(neuron_b), conductance_(conductance) {}

    // Current flowing from A to B: I = g * (V_a - V_b)
    // Positive means current flows from A to B
    double compute_current(double V_a, double V_b) const {
        return conductance_ * (V_a - V_b);
    }

    int neuron_a() const { return neuron_a_; }
    int neuron_b() const { return neuron_b_; }
    double conductance() const { return conductance_; }
    void set_conductance(double g) { conductance_ = g; }

private:
    int neuron_a_ = -1;
    int neuron_b_ = -1;
    double conductance_ = 0.1; // gap junction conductance (nS)
};

} // namespace celegans
