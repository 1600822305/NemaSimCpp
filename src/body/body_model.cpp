#include "body/body_model.h"
#include <cmath>
#include <algorithm>

namespace celegans {

BodyModel::BodyModel() {
    segment_length_ = body_length_ / NUM_BODY_SEGMENTS;
}

void BodyModel::initialize(Vector2d head_pos, double heading) {
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        auto& seg = segments_[i];
        seg.angle = heading;
        seg.position = head_pos - Vector2d::from_angle(heading) * (i * segment_length_);
        seg.curvature = 0.0;
        seg.prev_curvature = 0.0;
        seg.dorsal_activation = 0.0;
        seg.ventral_activation = 0.0;
    }
    prev_head_pos_ = head_pos;
    speed_ = 0.0;
}

void BodyModel::update_physics(double /*dt*/) {
    // Stub — will be re-implemented
    // For now: just track head position for speed calculation
    Vector2d head_pos = segments_[0].position;
    prev_head_pos_ = head_pos;
}

void BodyModel::reset_activations() {
    for (auto& seg : segments_) {
        seg.dorsal_activation = 0.0;
        seg.ventral_activation = 0.0;
    }
}

void BodyModel::set_muscle_activation(int segment, bool dorsal, double activation) {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return;
    activation = std::clamp(activation, 0.0, 1.0);
    if (dorsal)
        segments_[segment].dorsal_activation = std::max(segments_[segment].dorsal_activation, activation);
    else
        segments_[segment].ventral_activation = std::max(segments_[segment].ventral_activation, activation);
}

void BodyModel::set_muscle_activation_direct(int segment, bool dorsal, double activation) {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return;
    activation = std::clamp(activation, 0.0, 1.0);
    if (dorsal)
        segments_[segment].dorsal_activation = activation;
    else
        segments_[segment].ventral_activation = activation;
}

void BodyModel::perturb_heading(double dtheta) {
    segments_[0].angle += dtheta;
}

void BodyModel::set_locomotion_state(double forward_drive, double reverse_drive) {
    forward_drive_ = forward_drive;
    reverse_drive_ = reverse_drive;
}

void BodyModel::set_position(double x, double y) {
    segments_[0].position = {x, y};
    prev_head_pos_ = {x, y};
}

void BodyModel::set_heading(double angle) {
    segments_[0].angle = angle;
}

void BodyModel::nudge_position(double dx, double dy) {
    for (auto& seg : segments_) {
        seg.position.x += dx;
        seg.position.y += dy;
    }
    prev_head_pos_.x += dx;
    prev_head_pos_.y += dy;
}

Vector2d BodyModel::get_head_position() const {
    return segments_[0].position;
}

double BodyModel::get_head_angle() const {
    return segments_[0].angle;
}

Vector2d BodyModel::get_tail_position() const {
    return segments_[NUM_BODY_SEGMENTS - 1].position;
}

double BodyModel::get_local_curvature(int segment) const {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return 0.0;
    return segments_[segment].curvature;
}

double BodyModel::get_local_stretch(int segment) const {
    if (segment < 1 || segment >= NUM_BODY_SEGMENTS) return 0.0;
    double dist = (segments_[segment].position - segments_[segment - 1].position).norm();
    return (dist - segment_length_) / segment_length_;
}

Vector2d BodyModel::get_segment_position(int segment) const {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return {0, 0};
    return segments_[segment].position;
}

} // namespace celegans
