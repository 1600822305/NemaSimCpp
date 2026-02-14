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

void BodyModel::compute_curvatures(double dt) {
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        auto& seg = segments_[i];
        // Curvature driven by muscle force differential + neural curvature drive
        // force_diff > 0 → dorsal stronger → positive curvature (dorsal bend)
        // force_diff < 0 → ventral stronger → negative curvature (ventral bend)
        double force_diff = muscles_.get_force_differential(i);
        double target_curvature = curvature_gain_ * force_diff;

        // Step 29: Passive elastic coupling between adjacent segments
        // REF: Boyle 2012 — body continuity allows curvature to spread
        double curv_left  = (i > 0) ? segments_[i - 1].curvature : seg.curvature;
        double curv_right = (i < NUM_BODY_SEGMENTS - 1) ? segments_[i + 1].curvature : seg.curvature;
        double diffusion = curvature_diffusion_ * (curv_left - 2.0 * seg.curvature + curv_right);

        // Semi-implicit Euler (unconditionally stable for stiffness/damping):
        // dcurv/dt = stiffness*(target - curv) - damping*curv + diffusion
        double denom = 1.0 + (stiffness_ + damping_) * dt;
        seg.curvature = (seg.curvature + dt * (stiffness_ * target_curvature + diffusion)) / denom;

        // Clamp curvature to physical limit: ~15/mm (head touching body in omega)
        // REF: Gray 2005 PNAS — omega turn curvature ~15/mm
        double max_curv = 15.0;
        if (seg.curvature > max_curv) seg.curvature = max_curv;
        if (seg.curvature < -max_curv) seg.curvature = -max_curv;
    }
}

void BodyModel::update_positions(double dt) {
    // ===================================================================
    // C. elegans locomotion — force-based speed model
    // REF: Pierce-Shimomura 1999 (pirouette model of chemotaxis)
    //      Padmanabhan 2012 (curvature wave representation)
    //      Fang-Yen 2010 (speed ~0.15 mm/s on agar)
    // ===================================================================

    // --- 1. Forward speed from muscle force (low Reynolds number) ---
    // Propulsive force ∝ mean |F_dorsal - F_ventral| (body undulation amplitude)
    // At low Re: F_drag = C × v → v = F_propulsive / C_drag
    // Neuromod gain already applied inside muscles_.get_mean_abs_force()
    double mean_force = muscles_.get_mean_abs_force();
    double forward_speed = mean_force * locomotion_efficiency_
                         / drag_coefficient_;
    // Physical speed limit: adult C. elegans max ~500 μm/s (Fang-Yen 2010)
    // During omega, head muscle boost inflates mean_force but translational
    // speed is limited. Heading change (speed*curvature) still large due to
    // moderate speed × extreme curvature.
    if (forward_speed > 0.5) forward_speed = 0.5;

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
    // During omega: heading change from body deformation, not curved-path translation.
    // Use |direction| so curvature sign directly determines turn direction.
    double head_curv = segments_[0].curvature;
    double effective_dir = omega_active_ ? std::abs(direction) : direction;
    double dtheta = forward_speed * effective_dir * head_curv * dt;
    // Clamp heading change rate to physical limit
    // REF: Gray 2005 PNAS — omega turn angular velocity ~300°/s
    double max_dtheta = 5.24 * dt;
    if (dtheta > max_dtheta) dtheta = max_dtheta;
    if (dtheta < -max_dtheta) dtheta = -max_dtheta;
    segments_[0].angle += dtheta;

    // --- 4. Update head position ---
    // Step 41: direction=-1 during reversal → head moves backward
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
    // 1. Muscle dynamics: neural inputs → activation → force
    muscles_.step(dt * 1000.0);  // dt is in seconds, muscles expect ms

    // 2. Sync segment activations from muscles (for visualization/diagnostics)
    // Clamp to [0,1]: raw activation can be >> 1 during omega (RIV NMJ gain 40x)
    // but segment activation is for display and dorsal_tone snapshot only
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        double da = muscles_.get_dorsal_activation(i);
        double va = muscles_.get_ventral_activation(i);
        segments_[i].dorsal_activation = (da > 1.0) ? 1.0 : da;
        segments_[i].ventral_activation = (va > 1.0) ? 1.0 : va;
    }

    // 3. Body physics: curvature from muscle forces, position from kinematics
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
