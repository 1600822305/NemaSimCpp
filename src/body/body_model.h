#pragma once

#include "core/types.h"
#include <array>
#include <vector>

namespace celegans {

struct BodySegment {
    Vector2d position;
    double angle = 0.0;           // orientation angle (rad)
    double curvature = 0.0;       // local curvature (1/mm)
    double prev_curvature = 0.0;  // previous frame curvature (for RFT)
    double dorsal_activation = 0.0;  // dorsal muscle activation [0,1]
    double ventral_activation = 0.0; // ventral muscle activation [0,1]
};

class BodyModel {
public:
    BodyModel();

    void initialize(Vector2d head_pos, double heading);

    void update_physics(double dt);

    // Set muscle activations from motor controller
    void set_muscle_activation(int segment, bool dorsal, double activation);

    // Getters for sensory feedback
    Vector2d get_head_position() const;
    double get_head_angle() const;
    Vector2d get_tail_position() const;
    double get_local_curvature(int segment) const;
    double get_local_stretch(int segment) const;
    Vector2d get_segment_position(int segment) const;
    double get_speed() const { return speed_; }
    double get_body_length() const { return body_length_; }

    const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() const { return segments_; }
    std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() { return segments_; }

    // Direct set (for inhibitory reduction, bypasses max)
    void set_muscle_activation_direct(int segment, bool dorsal, double activation);

    // Reset all muscle activations
    void reset_activations();

private:
    std::array<BodySegment, NUM_BODY_SEGMENTS> segments_;
    double body_length_ = 1.0;       // mm
    double segment_length_ = 0.0;    // mm per segment
    double body_radius_ = 0.04;      // mm (~40 μm)
    double stiffness_ = 10.0;        // body stiffness (nN·mm²)
    double damping_ = 0.5;           // damping coefficient
    double muscle_gain_ = 0.3;       // max curvature per unit activation
    double drag_coeff_tangent_ = 1.0;   // tangential drag
    double drag_coeff_normal_ = 10.0;   // normal drag (anisotropic for low Re)
    double speed_ = 0.0;             // current locomotion speed (mm/s)
    Vector2d prev_head_pos_;

    void compute_curvatures(double dt);
    void update_positions(double dt);
};

} // namespace celegans
