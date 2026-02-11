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

        // Step 29: Passive elastic coupling between adjacent segments
        // REF: Boyle 2012 — body continuity allows curvature to spread
        double curv_left  = (i > 0) ? segments_[i - 1].curvature : seg.curvature;
        double curv_right = (i < NUM_BODY_SEGMENTS - 1) ? segments_[i + 1].curvature : seg.curvature;
        double diffusion = curvature_diffusion_ * (curv_left - 2.0 * seg.curvature + curv_right);

        // Semi-implicit Euler (unconditionally stable for stiffness/damping):
        // dcurv/dt = stiffness*(target - curv) - damping*curv + diffusion
        // Treat stiffness*curv and damping*curv implicitly:
        // curv_new*(1 + (stiffness+damping)*dt) = curv + dt*(stiffness*target + diffusion)
        double denom = 1.0 + (stiffness_ + damping_) * dt;
        seg.curvature = (seg.curvature + dt * (stiffness_ * target_curvature + diffusion)) / denom;
        // Clamp curvature: normal locomotion ~3/mm, omega turn ~15/mm (Gray 2005)
        // Head segments (0-3) get higher clamp during omega for deep ventral bend
        double max_curv = (omega_mode_ && i < 4) ? 15.0 : 3.0;
        if (seg.curvature > max_curv) seg.curvature = max_curv;
        if (seg.curvature < -max_curv) seg.curvature = -max_curv;
    }
}

void BodyModel::update_positions(double dt) {
    // ===================================================================
    // C. elegans locomotion kinematics
    // REF: Pierce-Shimomura 1999 (pirouette model of chemotaxis)
    //      Padmanabhan 2012 (curvature wave representation)
    //      Fang-Yen 2010 (speed ~0.15 mm/s on agar)
    // ===================================================================

    // --- 1. Forward speed from muscle activity ---
    double muscle_work = 0.0;
    for (auto& seg : segments_) {
        muscle_work += std::abs(seg.dorsal_activation - seg.ventral_activation);
    }
    muscle_work /= NUM_BODY_SEGMENTS;

    // REF: Fang-Yen 2010 — wild-type speed on agar ~0.15 mm/s
    double v_max = 0.6 * speed_scale_; // mm/s; muscle_work ~0.3-0.5 → effective speed ~0.15-0.30
    double forward_speed = v_max * muscle_work;

    // --- 1b. Locomotion direction from command neuron balance ---
    // Step 41: Implement backward locomotion during reversal
    // REF: Fang-Yen 2010 — reverse speed ~60% of forward speed
    //      Chalfie 1985 — AVA command neuron drives backward movement
    // Smooth drives (tau=100ms) to avoid jitter at direction transitions
    smooth_fwd_ += (forward_drive_ - smooth_fwd_) * dt / 0.1;
    smooth_rev_ += (reverse_drive_ - smooth_rev_) * dt / 0.1;
    mean_rev_ += (smooth_rev_ - mean_rev_) * dt / 5.0;

    // Net direction: +1 forward, -1 backward
    // Hysteresis: need 0.1 margin to switch (prevents oscillation at transition)
    double direction = 1.0;
    if (smooth_rev_ > smooth_fwd_ + 0.1) {
        direction = -1.0;
        forward_speed *= 0.6;  // reverse speed is ~60% of forward (Fang-Yen 2010)
    }

    // --- 2. Heading update: dθ/dt = v × direction × κ_head ---
    // REF: Padmanabhan 2012 — body with curvature κ moving at speed v turns at v·κ
    // During reversal (direction=-1): heading change reverses, consistent with
    // tail-first locomotion where the same curvature produces opposite turning
    double head_curv = segments_[0].curvature + curvature_bias_;
    double dtheta = forward_speed * direction * head_curv * dt;
    // Clamp heading change rate
    // Run regime: 50°/s = 0.87 rad/s (Pierce-Shimomura 1999)
    // Omega turn: 300°/s = 5.24 rad/s (deep ventral bend, Gray 2005)
    double max_dtheta = (omega_mode_ ? 5.24 : 0.87) * dt;
    if (dtheta > max_dtheta) dtheta = max_dtheta;
    if (dtheta < -max_dtheta) dtheta = -max_dtheta;
    segments_[0].angle += dtheta;

    // --- 4. Update head position ---
    // Step 41: direction=-1 during reversal → head moves backward
    // This physical backward displacement is essential for pirouette function:
    // the body position at omega turn onset depends on reversal duration,
    // which varies stochastically, creating post-pirouette heading diversity
    // REF: Pierce-Shimomura 1999 — "direction of new run after pirouette
    //      was essentially random" (emerges from backward displacement + omega geometry)
    Vector2d head_dir = Vector2d::from_angle(segments_[0].angle);
    segments_[0].position += head_dir * forward_speed * direction * dt;

    // --- 5. Save curvatures ---
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].prev_curvature = segments_[i].curvature;
    }

    // --- 6. Body segments follow head ---
    // REF: Padmanabhan 2012 — θ_i = θ_{i-1} - κ_i × ds
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].angle = segments_[i - 1].angle - segments_[i].curvature * segment_length_;
        Vector2d dir = Vector2d::from_angle(segments_[i].angle);
        segments_[i].position = segments_[i - 1].position - dir * segment_length_;
    }

    // --- 7. Compute speed ---
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
