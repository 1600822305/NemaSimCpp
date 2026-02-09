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
        seg.dorsal_activation = 0.0;
        seg.ventral_activation = 0.0;
    }
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
    if (dorsal) {
        segments_[segment].dorsal_activation = std::max(segments_[segment].dorsal_activation, activation);
    } else {
        segments_[segment].ventral_activation = std::max(segments_[segment].ventral_activation, activation);
    }
}

void BodyModel::set_muscle_activation_direct(int segment, bool dorsal, double activation) {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return;
    activation = std::clamp(activation, 0.0, 1.0);
    if (dorsal) {
        segments_[segment].dorsal_activation = activation;
    } else {
        segments_[segment].ventral_activation = activation;
    }
}

void BodyModel::compute_curvatures(double dt) {
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        auto& seg = segments_[i];
        // Curvature driven by differential muscle activation:
        // dorsal > ventral -> positive curvature (dorsal bend)
        // ventral > dorsal -> negative curvature (ventral bend)
        double target_curvature = muscle_gain_ * (seg.dorsal_activation - seg.ventral_activation);

        // Damped spring toward target curvature
        double dcurv = stiffness_ * (target_curvature - seg.curvature) - damping_ * seg.curvature;
        seg.curvature += dcurv * dt;
    }
}

void BodyModel::update_positions(double dt) {
    // Head segment: curvature drives heading change (kinematic steering)
    // dθ/dt = curvature * forward_speed — a curved body moving forward naturally turns
    // REF: Boyle et al. 2012 - worm body kinematics in viscous medium
    double head_curv = segments_[0].curvature;
    segments_[0].angle += head_curv * dt * 5.0; // angular rate scaling

    // Subsequent segments: angle from anterior neighbor's angle minus local curvature
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].angle = segments_[i - 1].angle - segments_[i].curvature * segment_length_;
    }

    // Muscle power model for forward velocity
    // REF: Fang-Yen et al. 2010, Boyle et al. 2012
    // Speed = muscle_work × wave_efficiency × (C_N/C_T - 1) / C_T
    // muscle_work: mean |dorsal - ventral| activation (fraction of max muscle force)
    // wave_efficiency: spatial curvature variance → traveling wave is more efficient
    // C_N/C_T ratio: anisotropic drag converts lateral undulation to forward thrust

    // 1. Muscle work: mean differential activation across body
    double muscle_work = 0.0;
    for (auto& seg : segments_) {
        muscle_work += std::abs(seg.dorsal_activation - seg.ventral_activation);
    }
    muscle_work /= NUM_BODY_SEGMENTS;

    // 2. Wave efficiency: curvature spatial variance (1.0 for perfect S-wave, ~0 for uniform)
    // A traveling wave has alternating positive/negative curvature → high variance
    double mean_curv = 0.0;
    for (auto& seg : segments_) mean_curv += seg.curvature;
    mean_curv /= NUM_BODY_SEGMENTS;
    double curv_variance = 0.0;
    for (auto& seg : segments_) {
        double dc = seg.curvature - mean_curv;
        curv_variance += dc * dc;
    }
    curv_variance /= NUM_BODY_SEGMENTS;
    // Normalize: variance of 0.01 → efficiency 0.5, variance of 0.1 → efficiency ~1.0
    double wave_eff = 1.0 - std::exp(-curv_variance * 100.0);
    if (wave_eff < 0.1) wave_eff = 0.1; // minimum efficiency for any bending

    // 3. Temporal activity: curvature change rate boosts efficiency (true undulation)
    double temporal_activity = 0.0;
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        double dc = segments_[i].curvature - segments_[i].prev_curvature;
        temporal_activity += dc * dc;
    }
    temporal_activity = std::sqrt(temporal_activity / NUM_BODY_SEGMENTS) / dt;
    double temporal_boost = 1.0 + std::min(temporal_activity * 10.0, 2.0);

    // 4. Forward speed: v_max × muscle_work × efficiency × temporal_boost
    // v_max ≈ 0.3 mm/s for C. elegans on agar (Fang-Yen et al. 2010)
    double v_max = 0.8; // mm/s (compensates for sparse motor mapping: 30/48 segments)
    double forward_speed = v_max * muscle_work * wave_eff * temporal_boost;

    Vector2d head_dir = Vector2d::from_angle(segments_[0].angle);
    segments_[0].position += head_dir * forward_speed * dt;

    // Save curvatures for temporal activity calculation
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].prev_curvature = segments_[i].curvature;
    }

    // Each subsequent segment follows the one in front
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        Vector2d dir = Vector2d::from_angle(segments_[i].angle);
        segments_[i].position = segments_[i - 1].position - dir * segment_length_;
    }

    // Compute speed
    Vector2d head_pos = segments_[0].position;
    speed_ = (head_pos - prev_head_pos_).norm() / dt;
    prev_head_pos_ = head_pos;
}

void BodyModel::update_physics(double dt) {
    compute_curvatures(dt);
    update_positions(dt);
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
