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
    // ===================================================================
    // Step 132: Proprioceptive body wave (Boyle & Cohen 2012)
    //
    // MECHANISM: B-class motor neurons are independently bistable.
    // Stretch receptors sense anterior bending → trigger local B-neuron
    // → full muscle activation → full curvature. The stretch receptor
    // determines TIMING (phase), not AMPLITUDE. So the wave propagates
    // without amplitude decay.
    //
    // Implementation:
    //   Head (seg 0-3): SMD-driven oscillation (wave source)
    //   Body (seg 4-47): exponential tracking of anterior curvature
    //     - No amplitude loss (each segment reaches full curvature)
    //     - Phase delay τ_prop per segment (~60ms → ~0.65 body wavelength)
    //     - Amplitude gated by local muscle activity (DB/VB must be active)
    //
    // REF: Boyle & Cohen 2012, Front Comput Neurosci
    //      Wen et al. 2012, Neuron
    //      Fang-Yen 2010: crawl λ ≈ 0.65 body lengths, f ≈ 0.5 Hz
    // ===================================================================

    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        auto& seg = segments_[i];

        if (i < 4) {
            // HEAD: driven by SMD muscle differential (wave source)
            double muscle_diff = seg.dorsal_activation - seg.ventral_activation;
            double target = muscle_gain_ * muscle_diff;

            // Semi-implicit integration
            double denom = 1.0 + (stiffness_ + damping_) * dt;
            seg.curvature = (seg.curvature + dt * stiffness_ * target) / denom;
        } else {
            // BODY: proprioceptive wave — track anterior curvature
            // Exponential filter: curv += (anterior - curv) * (dt / τ)
            // This gives ZERO steady-state error (no amplitude decay)
            double anterior_curv = segments_[i - 1].curvature;

            // Muscle gating: DB/VB must be active for segment to respond
            double muscle_amp = seg.dorsal_activation + seg.ventral_activation;
            double gate = std::min(1.0, muscle_amp * 2.5);

            // Direct muscle bias (for omega turns, reversal wave direction)
            double muscle_diff = seg.dorsal_activation - seg.ventral_activation;
            double muscle_bias = muscle_gain_ * muscle_diff * 0.15;

            // Proprioceptive tracking with phase delay
            double alpha = dt / prop_tau_ * gate;  // tracking rate, gated
            seg.curvature += (anterior_curv - seg.curvature) * alpha + muscle_bias * dt;
        }

        // Clamp: crawling ~5/mm, omega ~15/mm
        double max_curv = (omega_mode_ && i < 4) ? 15.0 : 5.0;
        seg.curvature = std::clamp(seg.curvature, -max_curv, max_curv);
    }
}

void BodyModel::update_positions(double dt) {
    // ===================================================================
    // C. elegans locomotion kinematics
    // REF: Pierce-Shimomura 1999 (pirouette model of chemotaxis)
    //      Padmanabhan 2012 (curvature wave representation)
    //      Fang-Yen 2010 (speed ~0.15 mm/s on agar)
    // ===================================================================

    // --- 1. RFT-based forward speed (Gray & Lissmann 1964, Boyle 2012) ---
    // At low Re (~10⁻⁴), inertia negligible → quasi-static force balance.
    // Drag anisotropy (C_N >> C_T on agar) converts undulation into thrust.
    //
    // Algorithm:
    //   1. Compute body angles from current curvatures
    //   2. Compute dκ/dt from curvature change (current - prev)
    //   3. Compute joint angular velocities (cumulative dκ chain)
    //   4. Compute shape velocities for each segment
    //   5. Build 2×2 drag matrix A and thrust vector b
    //   6. Solve A × V = b for body translation velocity
    //   7. Forward speed = V · t_head
    //
    // REF: Gray & Lissmann 1964 (RFT for nematodes)
    //      Boyle et al. 2012 (C. elegans neuromechanical model, K_agar=40)
    //      Berri et al. 2009 (swim-crawl transition)
    //      Backholm et al. 2014 (direct drag force measurements)
    double ds = segment_length_;
    double C_T = drag_coeff_tangent_;
    double C_N = drag_coeff_normal_;

    // 1a. Body angles from current curvatures
    double theta[NUM_BODY_SEGMENTS];
    theta[0] = segments_[0].angle;
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i)
        theta[i] = theta[i - 1] - segments_[i].curvature * ds;

    // 1b. Curvature change rates
    double dkappa[NUM_BODY_SEGMENTS];
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i)
        dkappa[i] = (segments_[i].curvature - segments_[i].prev_curvature) / dt;

    // 1c. Joint angular velocities (cumulative from curvature chain)
    // dθ_j/dt = -Σ_{k=1}^{j} dκ_k × ds
    double omega[NUM_BODY_SEGMENTS];
    omega[0] = 0.0;
    for (int j = 1; j < NUM_BODY_SEGMENTS; ++j)
        omega[j] = omega[j - 1] - dkappa[j] * ds;

    // 1d. Shape velocities (cumulative from angular velocity × normal)
    // v_shape_i = -Σ_{j=0}^{i-1} ds × ω_j × n_j
    double vsx[NUM_BODY_SEGMENTS], vsy[NUM_BODY_SEGMENTS];
    vsx[0] = vsy[0] = 0.0;
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        double nx = -std::sin(theta[i - 1]);
        double ny =  std::cos(theta[i - 1]);
        vsx[i] = vsx[i - 1] - ds * omega[i - 1] * nx;
        vsy[i] = vsy[i - 1] - ds * omega[i - 1] * ny;
    }

    // 1e. Build 2×2 RFT drag matrix A and thrust vector b
    // For each segment: F_drag = -C_T*(v·t)*t - C_N*(v·n)*n
    // Total velocity: v_i = V + v_shape_i
    // Force balance: A × V = b
    double A00 = 0, A01 = 0, A11 = 0;
    double b0 = 0, b1 = 0;
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        double tx = std::cos(theta[i]), ty = std::sin(theta[i]);
        double nx = -ty, ny = tx;
        // Symmetric drag matrix
        A00 += C_T * tx * tx + C_N * nx * nx;
        A01 += C_T * tx * ty + C_N * nx * ny;
        A11 += C_T * ty * ty + C_N * ny * ny;
        // Thrust from shape change
        double vs_t = vsx[i] * tx + vsy[i] * ty;
        double vs_n = vsx[i] * nx + vsy[i] * ny;
        b0 -= C_T * vs_t * tx + C_N * vs_n * nx;
        b1 -= C_T * vs_t * ty + C_N * vs_n * ny;
    }

    // 1f. Solve 2×2: A × V = b (A is symmetric: A10 = A01)
    double det = A00 * A11 - A01 * A01;
    double Vx = 0.0, Vy = 0.0;
    if (std::abs(det) > 1e-20) {
        Vx = ( A11 * b0 - A01 * b1) / det;
        Vy = (-A01 * b0 + A00 * b1) / det;
    }

    // 1g. Forward speed = V · t_head × calibration
    double head_tx = std::cos(theta[0]), head_ty = std::sin(theta[0]);
    double forward_speed = (Vx * head_tx + Vy * head_ty) * rft_gain_ * speed_scale_;

    // Clamp: prevent unrealistic speeds from transients (startup, omega turns)
    // Fang-Yen 2010: crawl ~0.15-0.3 mm/s, max burst ~0.5 mm/s
    forward_speed = std::clamp(forward_speed, 0.0, 1.0);

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
