#pragma once

// ================================================================
// 3D Body model — stub interface
// All physics implementations have been removed; will be re-implemented.
// Public API is preserved so the rest of the codebase compiles.
// ================================================================

#include "core/types.h"
#include <array>
#include <vector>
#include <cmath>

namespace celegans {

constexpr int NUM_3D_NODES = NUM_BODY_SEGMENTS + 1;  // 49

enum class MuscleQuadrant { DORSAL_LEFT, DORSAL_RIGHT, VENTRAL_LEFT, VENTRAL_RIGHT };

struct MuscleCell3D {
    int seg_start = 0;
    MuscleQuadrant quadrant = MuscleQuadrant::DORSAL_LEFT;
    double activation = 0.0;
};

struct Node3D {
    Vector3d pos;
    Vector3d vel;
    Vector3d tangent;
    Vector3d dorsal;
    Vector3d lateral;
    Vector3d pos_dl, pos_dr, pos_vl, pos_vr;
    double radius = 0.0;
    double curvature_dv = 0.0;
    double curvature_lr = 0.0;
};

class BodyModel3D {
public:
    BodyModel3D();
    void initialize(Vector3d head_pos, double heading_xy, double heading_z = 0.0);
    void update_physics(double dt);

    void set_muscle_activation(int muscle_id, double activation);
    void set_segment_activation(int segment, MuscleQuadrant quad, double activation);
    void set_dorsal_ventral_activation(int segment, bool dorsal, double activation);
    void reset_activations();

    Vector3d get_head_position_3d() const;
    Vector2d get_head_position() const;
    double get_head_angle() const;
    Vector3d get_tail_position_3d() const;
    Vector2d get_tail_position() const;
    double get_local_curvature(int segment) const;
    double get_lateral_curvature(int segment) const;
    double get_local_stretch(int segment) const;
    Vector2d get_segment_position(int segment) const;
    Vector3d get_segment_position_3d(int segment) const;
    double get_speed() const { return speed_; }
    double get_body_length() const { return body_length_; }

    void set_locomotion_state(double fwd, double rev) { forward_drive_ = fwd; reverse_drive_ = rev; }
    void set_speed_scale(double s) { speed_scale_ = s; }
    void set_curvature_bias(double b) { curvature_bias_ = b; }
    double get_curvature_bias() const { return curvature_bias_; }
    void set_omega_mode(bool on) { omega_mode_ = on; }
    void perturb_heading(double dtheta);

    void set_position(double x, double y);
    void set_heading(double angle);
    void nudge_position(double dx, double dy);

    const std::array<Node3D, NUM_3D_NODES>& nodes() const { return nodes_; }
    const std::vector<MuscleCell3D>& muscles() const { return muscles_; }
    int num_muscles() const { return static_cast<int>(muscles_.size()); }

private:
    double body_length_ = 1.0;
    double body_radius_ = 0.04;
    double segment_length_ = 0.0;
    std::array<Node3D, NUM_3D_NODES> nodes_;
    std::vector<MuscleCell3D> muscles_;
    double speed_ = 0.0;
    double speed_scale_ = 1.0;
    double curvature_bias_ = 0.0;
    bool omega_mode_ = false;
    double forward_drive_ = 0.5;
    double reverse_drive_ = 0.0;

    void init_muscles();
    void compute_radii();
};

} // namespace celegans
