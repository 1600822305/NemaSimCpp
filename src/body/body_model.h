#pragma once

// ================================================================
// Body model — Boyle 2012 2D rigid-rod neuromechanical model
// 49 rigid rods (NBAR), 98 discrete points, semi-implicit Euler
// REF: Boyle, Berri & Cohen 2012, Front. Comput. Neurosci. 6:10
// ================================================================

#include "core/types.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace celegans {

// Physical constants — Boyle 2012 Table 1, SI units
namespace BodyParams {
    constexpr int NBAR       = 49;                 // number of rods (cross-sections)
    constexpr int NSEG       = NBAR - 1;           // = 48 segments between rods
    constexpr int NPOINTS    = 2 * NBAR;           // = 98 discrete points

    constexpr double BODY_LENGTH = 1.0e-3;         // 1.0 mm in meters
    constexpr double R_MAX       = 40.0e-6;        // 40 μm max radius
    constexpr double R_MIN_RATIO = 0.3;            // min radius = 30% of R_MAX [engineering]

    // Passive lateral (cuticle)
    constexpr double K_PE  = 10.0e-3;             // N/m per rod
    constexpr double D_PE  = 5.0e-4;              // N·s/m per rod

    // Diagonal (internal pressure) — reduced from Boyle 350×K_PE
    // 350× prevents ALL bending (diagonal restoring >> muscle force).
    // 5× allows bending while maintaining body width.
    constexpr double K_DE  = 5.0 * K_PE;          // = 0.05 N/m
    constexpr double D_DE  = 0.01  * K_DE;        // = 0.0005 N·s/m

    // Active muscle
    constexpr double K_AE  = 20.0 * K_PE;         // = 0.2 N/m
    constexpr double D_AE  = 5.0 * 20.0 * D_PE;   // = 0.05 N·s/m

    // Muscle dynamics
    constexpr double TAU_MUSCLE   = 0.1;           // 100 ms in seconds
    constexpr double L_MIN_RATIO  = 0.6;           // min muscle length ratio

    // RFT drag coefficients (total body)
    constexpr double CN_WATER = 5.2e-6;            // kg/s normal (water)
    constexpr double CT_WATER = 3.3e-6;            // kg/s tangential (water)
    constexpr double CN_AGAR  = 128.0e-3;          // kg/s normal (agar)
    constexpr double CT_AGAR  = 3.2e-3;            // kg/s tangential (agar)

    // Self-collision
    constexpr double K_CONTACT = 10.0 * K_PE;     // repulsion stiffness
}

// Per-rod state
struct Rod {
    double cx = 0.0, cy = 0.0;  // midpoint (meters)
    double phi = 0.0;           // angle (radians)
    double radius = 0.0;        // half-length of rod = local body radius

    // Derived endpoints: dorsal (D) and ventral (V)
    double dx() const { return cx - radius * std::sin(phi); }
    double dy() const { return cy + radius * std::cos(phi); }
    double vx() const { return cx + radius * std::sin(phi); }
    double vy() const { return cy - radius * std::cos(phi); }
};

// Per-segment muscle state (between rod i and rod i+1)
struct MuscleState {
    double dorsal_activation  = 0.0;   // A ∈ [0,1]
    double ventral_activation = 0.0;   // A ∈ [0,1]
    double dorsal_input       = 0.0;   // NMJ current (before leaky integrator)
    double ventral_input      = 0.0;
};

// Backward-compat wrapper (maps to rod data)
struct BodySegment {
    Vector2d position;
    double angle = 0.0;
    double curvature = 0.0;
    double prev_curvature = 0.0;
    double dorsal_activation = 0.0;
    double ventral_activation = 0.0;
};

class BodyModel {
public:
    BodyModel();

    void initialize(Vector2d head_pos, double heading);
    void update_physics(double dt);

    // Muscle activation interface (from motor controller)
    void set_muscle_activation(int segment, bool dorsal, double activation);
    void set_muscle_activation_direct(int segment, bool dorsal, double activation);
    void reset_activations();

    // Sensory feedback getters
    Vector2d get_head_position() const;
    double get_head_angle() const;
    Vector2d get_tail_position() const;
    double get_local_curvature(int segment) const;
    double get_local_stretch(int segment) const;
    Vector2d get_segment_position(int segment) const;
    double get_speed() const { return speed_; }
    double get_body_length() const { return body_length_mm_; }

    // Steering (curvature_bias retained for RIV omega only)
    void set_curvature_bias(double b) { curvature_bias_ = b; }
    double get_curvature_bias() const { return curvature_bias_; }
    void set_omega_mode(bool on) { omega_mode_ = on; }
    void perturb_heading(double dtheta);

    // Locomotion state (backward compat — not used by physics)
    void set_locomotion_state(double forward_drive, double reverse_drive);
    void set_speed_scale(double /*s*/) { /* no-op in rod model */ }

    // Medium: 0.0=water, 1.0=agar
    void set_medium(double m) { medium_ = std::clamp(m, 0.0, 1.0); }
    double get_medium() const { return medium_; }

    // Multi-worm support
    void set_position(double x, double y);
    void set_heading(double angle);
    void nudge_position(double dx, double dy);

    // Segment access (backward compat — syncs from rods)
    const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() const { return segments_; }
    std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() { return segments_; }

    // Rod access (new API)
    const std::array<Rod, BodyParams::NBAR>& rods() const { return rods_; }
    const std::array<MuscleState, BodyParams::NSEG>& muscles() const { return muscles_; }

private:
    // Primary state: rods
    std::array<Rod, BodyParams::NBAR> rods_;
    std::array<Rod, BodyParams::NBAR> prev_rods_;  // for velocity computation
    std::array<MuscleState, BodyParams::NSEG> muscles_;
    double seg_len_ = 0.0;  // segment length in meters
    bool initialized_ = false;

    // Rest lengths (computed once at init)
    std::array<double, BodyParams::NSEG> rest_len_dorsal_;   // lateral dorsal
    std::array<double, BodyParams::NSEG> rest_len_ventral_;  // lateral ventral
    std::array<double, BodyParams::NSEG> rest_len_diag_dv_;  // diagonal D_i → V_{i+1}
    std::array<double, BodyParams::NSEG> rest_len_diag_vd_;  // diagonal V_i → D_{i+1}

    // Backward-compat
    std::array<BodySegment, NUM_BODY_SEGMENTS> segments_;
    void sync_segments_from_rods();

    // Physics helpers
    void compute_radii();
    void compute_rest_lengths();
    void update_muscle_activations(double dt);

    // Endpoint force arrays: fdx[i],fdy[i] = force on dorsal point of rod i
    //                        fvx[i],fvy[i] = force on ventral point of rod i
    void apply_lateral_forces(int seg, double* fdx, double* fdy, double* fvx, double* fvy);
    void apply_diagonal_forces(int seg, double* fdx, double* fdy, double* fvx, double* fvy);
    void apply_muscle_forces(int seg, double* fdx, double* fdy, double* fvx, double* fvy);
    void apply_self_collision(double* fdx, double* fdy, double* fvx, double* fvy);

    // Helper: force from spring-damper between two points
    struct SpringForce {
        double fx0, fy0, fx1, fy1;
    };
    SpringForce spring_damper(double x0, double y0, double x1, double y1,
                              double vx0, double vy0, double vx1, double vy1,
                              double K, double D, double L0) const;

    // Reconstruct rod (cx,cy,phi) from endpoint positions
    void reconstruct_rod(int i, double Dx, double Dy, double Vx, double Vy);

    double body_length_mm_ = 1.0;
    double speed_ = 0.0;
    double curvature_bias_ = 0.0;
    bool omega_mode_ = false;
    double medium_ = 1.0;
    Vector2d prev_head_pos_;
    double forward_drive_ = 0.5;
    double reverse_drive_ = 0.0;

    // RFT coefficients (interpolated by medium_)
    double cn_ = 0.0;  // normal drag per rod
    double ct_ = 0.0;  // tangential drag per rod
};

} // namespace celegans
