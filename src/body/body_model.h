#pragma once

#include "core/types.h"
#include "body/muscle_system.h"
#include <array>
#include <vector>

namespace celegans {

struct BodySegment {
    Vector2d position;
    double angle = 0.0;           // orientation angle (rad)
    double curvature = 0.0;       // local curvature (1/mm)
    double prev_curvature = 0.0;  // previous frame curvature (for RFT shape change)
    double dorsal_activation = 0.0;  // synced from MuscleSystem (for visualization)
    double ventral_activation = 0.0; // synced from MuscleSystem (for visualization)
};

class BodyModel {
public:
    BodyModel();

    void initialize(Vector2d head_pos, double heading);

    void update_physics(double dt);

    // --- Muscle system access (motor controller writes here) ---
    MuscleSystem& muscles() { return muscles_; }
    const MuscleSystem& muscles() const { return muscles_; }

    // Getters for sensory feedback
    Vector2d get_head_position() const;
    double get_head_angle() const;
    Vector2d get_tail_position() const;
    double get_local_curvature(int segment) const;
    double get_local_stretch(int segment) const;
    Vector2d get_segment_position(int segment) const;
    double get_speed() const { return speed_; }
    // +1 forward, -1 backward — emergent from RFT velocity projection on heading
    double get_direction() const { return direction_; }

    double get_body_length() const { return body_length_; }

    // Nose tip position: amphid neurons (ASE, AWC, etc.) are at the very tip
    // of the nose, ~50μm ahead of the first body segment.
    // REF: White 1986 — amphid opening at nose tip
    Vector2d get_nose_position() const {
        Vector2d dir = Vector2d::from_angle(segments_[0].angle);
        return segments_[0].position + dir * nose_protrusion_;
    }

    // Swimming gait: medium viscosity controls muscle dynamics + drag
    // REF: Fang-Yen 2010 JEM, Berri 2009, Pierce-Shimomura 2008 PNAS
    //   1.0 = agar (crawling ~0.5 Hz), 0.01 = water (swimming ~1.7 Hz)
    void set_medium_viscosity(double v);
    // Step 130: External angular velocity for weathervane (rad/s, bypasses muscles)
    void set_external_angular_velocity(double omega) { external_angular_velocity_ = omega; }
    double get_medium_viscosity() const { return medium_viscosity_; }

    const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() const { return segments_; }
    std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() { return segments_; }


private:
    MuscleSystem muscles_;
    std::array<BodySegment, NUM_BODY_SEGMENTS> segments_;
    double body_length_ = 1.0;       // mm
    double segment_length_ = 0.0;    // mm per segment
    double body_radius_ = 0.04;      // mm (~40 μm)
    double nose_protrusion_ = 0.05;   // mm (~50 μm, amphid opening ahead of seg[0])
    double stiffness_ = 10.0;        // body stiffness (nN·mm²)
    double damping_ = 0.5;           // damping coefficient
    double curvature_diffusion_ = 0.5; // Step 29: gentle elastic coupling (Boyle 2012)
    double curvature_gain_ = 4.0;    // curvature per unit muscle force differential (1/mm)

    // Resistive Force Theory (RFT) drag coefficients
    // At low Reynolds number (Re ~ 0.01), anisotropic drag converts
    // undulatory body waves into net thrust. C_N > C_T is essential.
    // REF: Gray & Hancock 1955 — slender body RFT
    //      Boyle 2012 — C. elegans neuromechanical model
    //      Fang-Yen 2010 — C_N/C_T ≈ 1.5 on agar
    double drag_tangential_ = 3.4;   // C_T — drag along body axis
    double drag_normal_ = 5.1;       // C_N — drag perpendicular to body (ratio ≈ 1.5)

    double medium_viscosity_ = 1.0;  // 1.0 = agar, 0.01 = water
    double speed_cap_ = 0.8;         // mm/s, adjusts with medium

    double speed_ = 0.0;             // current locomotion speed (mm/s)
    double direction_ = 1.0;         // +1 forward, -1 backward (from velocity · heading)
    Vector2d prev_head_pos_;

    // Step 130: External angular velocity for weathervane heading correction
    // Applied AFTER RFT solve, does NOT feed back to muscle/proprioception.
    // Represents aggregate contribution of non-SMD weathervane pathways.
    double external_angular_velocity_ = 0.0;  // rad/s

    void compute_curvatures(double dt);
    void update_positions(double dt);

    // Solve 3×3 linear system Ax=b by Gaussian elimination with partial pivoting
    static bool solve_3x3(double A[3][3], double b[3], double& x0, double& x1, double& x2);
};

} // namespace celegans
