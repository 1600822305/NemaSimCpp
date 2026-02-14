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

    const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() const { return segments_; }
    std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() { return segments_; }


private:
    MuscleSystem muscles_;
    std::array<BodySegment, NUM_BODY_SEGMENTS> segments_;
    double body_length_ = 1.0;       // mm
    double segment_length_ = 0.0;    // mm per segment
    double body_radius_ = 0.04;      // mm (~40 μm)
    double stiffness_ = 10.0;        // body stiffness (nN·mm²)
    double damping_ = 0.5;           // damping coefficient
    double curvature_diffusion_ = 0.5; // Step 29: gentle elastic coupling (Boyle 2012)
    double curvature_gain_ = 4.0;    // curvature per unit muscle force differential (1/mm, calibrated for RFT)

    // Resistive Force Theory (RFT) drag coefficients
    // At low Reynolds number (Re ~ 0.01), anisotropic drag converts
    // undulatory body waves into net thrust. C_N > C_T is essential.
    // REF: Gray & Hancock 1955 — slender body RFT
    //      Boyle 2012 — C. elegans neuromechanical model
    //      Fang-Yen 2010 — C_N/C_T ≈ 1.5 on agar
    double drag_tangential_ = 3.4;   // C_T — drag along body axis
    double drag_normal_ = 5.1;       // C_N — drag perpendicular to body (ratio ≈ 1.5)

    double speed_ = 0.0;             // current locomotion speed (mm/s)
    double direction_ = 1.0;         // +1 forward, -1 backward (from velocity · heading)
    Vector2d prev_head_pos_;

    void compute_curvatures(double dt);
    void update_positions(double dt);

    // Solve 3×3 linear system Ax=b by Gaussian elimination with partial pivoting
    static bool solve_3x3(double A[3][3], double b[3], double& x0, double& x1, double& x2);
};

} // namespace celegans
