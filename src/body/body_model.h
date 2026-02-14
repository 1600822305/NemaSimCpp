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
    double prev_curvature = 0.0;  // previous frame curvature (for RFT)
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

    double get_body_length() const { return body_length_; }

    // Forward/reverse state from command neuron balance
    // forward_drive: AVB release rate, reverse_drive: AVA release rate
    void set_locomotion_state(double forward_drive, double reverse_drive) {
        forward_drive_ = forward_drive;
        reverse_drive_ = reverse_drive;
    }

    // Omega state: heading change uses |direction| during omega (body deformation,
    // not translation along curved path — direction sign irrelevant)
    void set_omega_active(bool active) { omega_active_ = active; }

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
    double curvature_gain_ = 0.3;    // curvature per unit muscle force differential (1/mm)
    double locomotion_efficiency_ = 0.7; // propulsive efficiency (low Re undulation, calibrated for boost architecture)
    double drag_coefficient_ = 1.0;     // effective drag (low Reynolds number)
    double speed_ = 0.0;             // current locomotion speed (mm/s)
    Vector2d prev_head_pos_;
    double forward_drive_ = 0.5;     // AVB release rate (instantaneous)
    double reverse_drive_ = 0.0;     // AVA release rate (instantaneous)
    double smooth_fwd_ = 0.5;        // smoothed forward drive (100ms tau)
    double smooth_rev_ = 0.0;        // smoothed reverse drive (100ms tau)
    double mean_rev_ = 0.0;          // running mean of AVA (2s tau, for adaptive threshold)
    bool was_reversing_ = false;     // for detecting reversal transitions
    bool omega_active_ = false;      // heading uses |direction| during omega

    void compute_curvatures(double dt);
    void update_positions(double dt);
};

} // namespace celegans
