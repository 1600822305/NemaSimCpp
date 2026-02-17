#pragma once

#include "core/types.h"
#include <array>

namespace celegans {

// ============================================================================
// MuscleCell — single body wall muscle unit with activation dynamics
//
// Biology:
//   C. elegans body wall muscles are non-spiking, graded contraction cells.
//   Each receives cholinergic excitation (ACh from A/B-class MNs) and
//   GABAergic inhibition (GABA from D-class MNs). Multiple neurons
//   innervating the same muscle SUM their inputs (not max).
//
// Model:
//   drive = clamp(Σ excitatory - Σ inhibitory, 0, 1)
//   da/dt = (drive - a) / τ_muscle        (first-order low-pass)
//   force = a × neuromod_gain              (graded force output)
//
// REF: Richmond & Jorgensen 1999 — muscle activation dynamics
//      Liu et al. 2006 — body wall muscle electrophysiology
// ============================================================================

struct MuscleCell {
    double activation = 0.0;
    double excitatory_input = 0.0;  // max semantics (normal MNs)
    double boost_input = 0.0;       // sum semantics (specialized MNs: RIV, SMB)
    double inhibitory_input = 0.0;  // sum semantics (D-class GABAergic)

    double tau = 30.0;  // ms — contraction time constant (was static constexpr)

    void step(double dt_ms) {
        double drive = excitatory_input + boost_input - inhibitory_input;
        if (drive < 0.0) drive = 0.0;
        activation += (drive - activation) * dt_ms / tau;
        if (activation < 0.0) activation = 0.0;
    }

    void reset_inputs() {
        excitatory_input = 0.0;
        boost_input = 0.0;
        inhibitory_input = 0.0;
    }
};

// ============================================================================
// MuscleSystem — manages 48 dorsal + 48 ventral muscle units
//
// One muscle unit per body segment per side. Maps 1:1 to BodySegment.
// Motor controller writes neural inputs; body model reads force output.
//
// Neuromodulator gain (5-HT, DA, OA, PDF, FLP-11) modulates force
// output globally, replacing the old SPEED_SCALE bypass.
//
// REF: White 1986 — body wall muscle innervation
//      Fang-Yen 2010 — locomotion mechanics
// ============================================================================

class MuscleSystem {
public:
    MuscleSystem() = default;

    // --- Neural input (called by MotorController) ---
    void reset_inputs();
    void add_excitatory(int segment, bool dorsal, double input);
    void add_boost(int segment, bool dorsal, double input);
    void add_inhibitory(int segment, bool dorsal, double input);

    // --- Dynamics ---
    void step(double dt_ms);

    // --- Neuromodulator gain (replaces SPEED_SCALE) ---
    void set_neuromod_gain(double gain) { neuromod_gain_ = gain; }
    double get_neuromod_gain() const { return neuromod_gain_; }

    // --- Force output (used by BodyModel) ---
    // force = activation × neuromod_gain
    double get_dorsal_force(int seg) const;
    double get_ventral_force(int seg) const;
    // Curvature depends on D/V contraction ratio, not absolute force
    // neuromod_gain affects speed (mean_abs_force) but NOT curvature direction
    double get_force_differential(int segment) const {
        return dorsal_[segment].activation - ventral_[segment].activation;
    }
    double get_mean_abs_force() const;              // for speed calculation

    // --- Raw activation (for diagnostics/visualization) ---
    double get_dorsal_activation(int seg) const;
    double get_ventral_activation(int seg) const;

    // Swimming gait: set muscle time constant for all cells
    // REF: Fang-Yen 2010 — muscles contract faster in low-viscosity media
    void set_muscle_tau(double tau_ms);

private:
    std::array<MuscleCell, NUM_BODY_SEGMENTS> dorsal_{};
    std::array<MuscleCell, NUM_BODY_SEGMENTS> ventral_{};
    double neuromod_gain_ = 1.0;
};

} // namespace celegans
