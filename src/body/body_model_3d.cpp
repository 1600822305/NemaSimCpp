#include "body/body_model_3d.h"
#include <algorithm>
#include <cmath>

namespace celegans {

BodyModel3D::BodyModel3D() {
    segment_length_ = body_length_ / NUM_BODY_SEGMENTS;
    init_muscles();
    compute_radii();
}

void BodyModel3D::initialize(Vector3d head_pos, double heading_xy, double heading_z) {
    Vector3d dir = {std::cos(heading_xy) * std::cos(heading_z),
                    std::sin(heading_xy) * std::cos(heading_z),
                    std::sin(heading_z)};

    for (int i = 0; i < NUM_3D_NODES; ++i) {
        double s = i * segment_length_;
        nodes_[i].pos = head_pos - dir * s;
        nodes_[i].vel = {0, 0, 0};
        nodes_[i].tangent = dir * (-1.0);
        nodes_[i].dorsal = {0, 0, 1};
        nodes_[i].lateral = {0, 1, 0};
    }
    compute_radii();
    speed_ = 0.0;
}

void BodyModel3D::init_muscles() {
    muscles_.clear();
    muscles_.reserve(NUM_MUSCLES);
    for (int i = 0; i < 24; ++i) { muscles_.push_back({i * 2, MuscleQuadrant::DORSAL_LEFT}); }
    for (int i = 0; i < 24; ++i) { muscles_.push_back({i * 2, MuscleQuadrant::DORSAL_RIGHT}); }
    for (int i = 0; i < 24; ++i) { muscles_.push_back({i * 2, MuscleQuadrant::VENTRAL_LEFT}); }
    for (int i = 0; i < 23; ++i) { muscles_.push_back({i * 2, MuscleQuadrant::VENTRAL_RIGHT}); }
}

void BodyModel3D::compute_radii() {
    double half_n = (NUM_3D_NODES - 1) * 0.5;
    double a = half_n * 1.05;
    for (int i = 0; i < NUM_3D_NODES; ++i) {
        double s = (i - half_n) / a;
        double r2 = 1.0 - s * s;
        if (r2 < 0.01) r2 = 0.01;
        nodes_[i].radius = body_radius_ * std::sqrt(r2);
    }
}

void BodyModel3D::update_physics(double /*dt*/) {
    // Stub — will be re-implemented
}

void BodyModel3D::set_muscle_activation(int muscle_id, double activation) {
    if (muscle_id >= 0 && muscle_id < static_cast<int>(muscles_.size()))
        muscles_[muscle_id].activation = std::clamp(activation, 0.0, 1.0);
}

void BodyModel3D::set_segment_activation(int segment, MuscleQuadrant quad, double activation) {
    for (auto& m : muscles_) {
        if (m.seg_start / 2 == segment / 2 && m.quadrant == quad) {
            m.activation = std::clamp(activation, 0.0, 1.0);
            return;
        }
    }
}

void BodyModel3D::set_dorsal_ventral_activation(int segment, bool dorsal, double activation) {
    double a = std::clamp(activation, 0.0, 1.0);
    for (auto& m : muscles_) {
        if (m.seg_start / 2 != segment / 2) continue;
        if (dorsal && (m.quadrant == MuscleQuadrant::DORSAL_LEFT || m.quadrant == MuscleQuadrant::DORSAL_RIGHT))
            m.activation = a;
        if (!dorsal && (m.quadrant == MuscleQuadrant::VENTRAL_LEFT || m.quadrant == MuscleQuadrant::VENTRAL_RIGHT))
            m.activation = a;
    }
}

void BodyModel3D::reset_activations() {
    for (auto& m : muscles_) m.activation = 0.0;
}

Vector3d BodyModel3D::get_head_position_3d() const { return nodes_[0].pos; }
Vector2d BodyModel3D::get_head_position() const { return nodes_[0].pos.xy(); }
double BodyModel3D::get_head_angle() const { return std::atan2(nodes_[0].tangent.y, nodes_[0].tangent.x); }
Vector3d BodyModel3D::get_tail_position_3d() const { return nodes_[NUM_3D_NODES-1].pos; }
Vector2d BodyModel3D::get_tail_position() const { return nodes_[NUM_3D_NODES-1].pos.xy(); }

double BodyModel3D::get_local_curvature(int segment) const {
    if (segment < 0 || segment >= NUM_3D_NODES) return 0.0;
    return nodes_[segment].curvature_dv;
}

double BodyModel3D::get_lateral_curvature(int segment) const {
    if (segment < 0 || segment >= NUM_3D_NODES) return 0.0;
    return nodes_[segment].curvature_lr;
}

double BodyModel3D::get_local_stretch(int segment) const {
    if (segment < 0 || segment >= NUM_3D_NODES - 1) return 0.0;
    double len = (nodes_[segment+1].pos - nodes_[segment].pos).norm();
    return (len - segment_length_) / segment_length_;
}

Vector2d BodyModel3D::get_segment_position(int segment) const {
    if (segment < 0 || segment >= NUM_3D_NODES) return {0, 0};
    return nodes_[segment].pos.xy();
}

Vector3d BodyModel3D::get_segment_position_3d(int segment) const {
    if (segment < 0 || segment >= NUM_3D_NODES) return {0, 0, 0};
    return nodes_[segment].pos;
}

void BodyModel3D::perturb_heading(double dtheta) {
    double c = std::cos(dtheta), s = std::sin(dtheta);
    double dx = nodes_[0].tangent.x, dy = nodes_[0].tangent.y;
    nodes_[0].tangent.x = dx * c - dy * s;
    nodes_[0].tangent.y = dx * s + dy * c;
}

void BodyModel3D::set_position(double x, double y) {
    Vector3d offset = {x - nodes_[0].pos.x, y - nodes_[0].pos.y, 0};
    for (auto& n : nodes_) n.pos += offset;
}

void BodyModel3D::set_heading(double angle) {
    double current = get_head_angle();
    double dtheta = angle - current;
    double c = std::cos(dtheta), s = std::sin(dtheta);
    Vector3d pivot = nodes_[0].pos;
    for (auto& n : nodes_) {
        double dx = n.pos.x - pivot.x, dy = n.pos.y - pivot.y;
        n.pos.x = pivot.x + dx * c - dy * s;
        n.pos.y = pivot.y + dx * s + dy * c;
        double tx = n.tangent.x, ty = n.tangent.y;
        n.tangent.x = tx * c - ty * s;
        n.tangent.y = tx * s + ty * c;
    }
}

void BodyModel3D::nudge_position(double dx, double dy) {
    for (auto& n : nodes_) { n.pos.x += dx; n.pos.y += dy; }
}

} // namespace celegans
