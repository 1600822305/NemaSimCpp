#pragma once

// ================================================================
// Body model — stub interface
// All physics implementations have been removed; will be re-implemented.
// Public API is preserved so the rest of the codebase compiles.
// ================================================================

#include "core/types.h"
#include <algorithm>
#include <array>

namespace celegans {

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
    double get_body_length() const { return body_length_; }

    // Steering
    void set_curvature_bias(double b) { curvature_bias_ = b; }
    double get_curvature_bias() const { return curvature_bias_; }
    void set_omega_mode(bool on) { omega_mode_ = on; }
    void perturb_heading(double dtheta);

    // Locomotion state (AVB/AVA command neuron balance)
    void set_locomotion_state(double forward_drive, double reverse_drive);
    void set_speed_scale(double s) { speed_scale_ = s; }

    // Medium: 0.0=water, 1.0=agar
    void set_medium(double m) { medium_ = std::clamp(m, 0.0, 1.0); }
    double get_medium() const { return medium_; }

    // Multi-worm support
    void set_position(double x, double y);
    void set_heading(double angle);
    void nudge_position(double dx, double dy);

    // Segment access
    const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() const { return segments_; }
    std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() { return segments_; }

private:
    std::array<BodySegment, NUM_BODY_SEGMENTS> segments_;
    double body_length_ = 1.0;       // mm
    double segment_length_ = 0.0;    // mm per segment
    double speed_ = 0.0;             // mm/s
    double speed_scale_ = 1.0;
    double curvature_bias_ = 0.0;
    bool omega_mode_ = false;
    double medium_ = 1.0;
    Vector2d prev_head_pos_;
    double forward_drive_ = 0.5;
    double reverse_drive_ = 0.0;
};

} // namespace celegans
