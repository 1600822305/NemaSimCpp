#include "body/body_model.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace celegans {

BodyModel::BodyModel() {
    segment_length_ = body_length_ / NUM_BODY_SEGMENTS;

    // ============================================================
    // Precompute effective bending parameters from Boyle 2012
    //
    // Boyle's spring constants (SI, worm.cc:46-60):
    //   k_PE  = 0.02 N/m    (passive horizontal)
    //   k_AE  = 0.4  N/m    (active horizontal, = 20 × k_PE)
    //   k_DE  = 7.0  N/m    (diagonal, = 350 × k_PE)
    //   D_PE  = 5e-4 N·s/m  (passive damping)
    //   D_AE  = 0.05 N·s/m  (active damping)
    //   D_DE  = 0.07 N·s/m  (diagonal damping)
    //
    // Geometry (SI):
    //   D     = 80e-6 m     (body diameter)
    //   L_seg = 1e-3/48 m   (segment length ≈ 20.83 μm)
    //   R[i]  = elliptical radius (m)
    //
    // Effective bending stiffness per segment:
    //   When body bends by κ, angle change δθ = κ × L_seg
    //   Horizontal spring ΔL = R × δθ → torque = k × R² × L_seg × κ
    //   Diagonal spring ΔL ≈ 2R × δθ → torque = k × 4R² × L_seg × κ
    //   k_bend = (2×k_PE×R² + 2×k_DE×4R²) × L_seg  (both sides)
    //
    // Effective muscle torque:
    //   τ_muscle = k_AE × R × L_seg × ΔV  (per unit ΔV)
    //   (dorsal contracts, ventral stretches → net bending moment)
    //
    // Rotational drag:
    //   C_rot = CN_per_rod × R² × L_seg
    //
    // Semi-implicit ODE:
    //   κ_new = (κ_old + dt × τ_muscle/C_rot) / (1 + dt × k_bend/C_rot + d_ratio)
    // ============================================================
    constexpr double D_SI   = 80e-6;
    constexpr double L_seg  = 1e-3 / NUM_BODY_SEGMENTS;
    constexpr double k_PE   = (NUM_BODY_SEGMENTS / 24.0) * 10.0e-3;  // 0.02 N/m
    constexpr double k_AE   = 20.0 * k_PE;                           // 0.4  N/m
    constexpr double k_DE   = 350.0 * k_PE;                          // 7.0  N/m
    constexpr double D_PE   = 0.025 * k_PE;
    constexpr double D_AE   = 5.0 * 20.0 * D_PE;
    constexpr double D_DE   = 0.01 * k_DE;

    // Elliptical body radius [worm.cc:180]
    double R[NBAR];
    for (int i = 0; i < NBAR; ++i) {
        double pos = (i - NUM_BODY_SEGMENTS / 2.0) / (NUM_BODY_SEGMENTS / 2.0 + 0.2);
        pos = std::clamp(pos, -1.0, 1.0);
        R[i] = D_SI / 2.0 * std::abs(std::sin(std::acos(pos)));
    }

    // Drag coefficients (agar default)
    compute_drag_coefficients();

    // Per-segment effective parameters
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        double Ri = (R[i] + R[i + 1]) * 0.5;  // average radius for segment
        if (Ri < 1e-10) Ri = D_SI / 4.0;

        // Bending stiffness: horizontal (both sides) + diagonal (both sides)
        double k_bend = (2.0 * k_PE * Ri * Ri + 2.0 * k_DE * 4.0 * Ri * Ri) * L_seg;
        // Bending damping
        double D_bend = (2.0 * D_PE * Ri * Ri + 2.0 * D_DE * 4.0 * Ri * Ri) * L_seg;
        // Rotational drag
        double C_rot = CN_[std::min(i, NBAR - 1)] * Ri * Ri * L_seg;
        if (C_rot < 1e-30) C_rot = 1e-30;

        // Muscle torque coefficient: τ = k_AE × R × L_seg × ΔV
        // dκ/dt contribution = τ / C_rot = k_AE × R × L_seg / C_rot × ΔV
        tau_coeff_[i] = k_AE * Ri * L_seg / C_rot;

        // Stiffness ratio: k_bend / C_rot (1/s)
        k_ratio_[i] = k_bend / C_rot;

        // Damping ratio: D_bend / C_rot (dimensionless)
        d_ratio_[i] = D_bend / C_rot;
    }

    // NMJ weight gradient [worm.cc:377]
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        nmj_weight_[i] = 0.7 * (1.0 - i * 0.6 / NUM_BODY_SEGMENTS);
    }
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
        seg.V_muscle_dorsal = 0.0;
        seg.V_muscle_ventral = 0.0;
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

// ===================================================================
// Step 135: Muscle low-pass filter (Boyle worm.cc:493-500)
// V_muscle tracks neural activation with time constant T_muscle = 0.1s
// ===================================================================
void BodyModel::update_muscles(double dt) {
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        auto& seg = segments_[i];
        seg.V_muscle_dorsal  += (seg.dorsal_activation  * nmj_weight_[i] - seg.V_muscle_dorsal)  / T_muscle_ * dt;
        seg.V_muscle_ventral += (seg.ventral_activation * nmj_weight_[i] - seg.V_muscle_ventral) / T_muscle_ * dt;
    }
}

// ===================================================================
// Step 135: Semi-implicit curvature ODE + RFT translation
//
// Physics (per segment):
//   C_rot × dκ/dt = τ_muscle(ΔV) - k_bend × κ - D_bend × dκ/dt
//
// Semi-implicit discretization (unconditionally stable):
//   κ_new = (κ_old + dt × τ_muscle / C_rot) / (1 + dt × k_bend/C_rot)
//
// All effective parameters (tau_coeff_, k_ratio_, d_ratio_) are
// precomputed in constructor from Boyle 2012 worm.cc SI values:
//   k_PE=0.02 N/m, k_AE=0.4 N/m, k_DE=7.0 N/m, R=elliptical
//
// Translation via RFT 2×2 matrix solve (Gray & Lissmann 1964).
//
// REF: Boyle, Berri & Cohen 2012, Front Comput Neurosci 6:10
// ===================================================================
void BodyModel::compute_forces_and_integrate(double dt) {
    const int N = NUM_BODY_SEGMENTS;
    const double ds = segment_length_;

    // --- 1. Semi-implicit curvature update ---
    for (int i = 0; i < N; ++i) {
        auto& seg = segments_[i];
        double dV = seg.V_muscle_dorsal - seg.V_muscle_ventral;

        // τ_muscle / C_rot = tau_coeff × ΔV  (precomputed, 1/s per unit ΔV)
        double drive = tau_coeff_[i] * dV;

        // Semi-implicit: treat stiffness implicitly, drive explicitly
        // κ_new = (κ_old + dt × drive) / (1 + dt × k_ratio + d_ratio)
        double denom = 1.0 + dt * k_ratio_[i] + d_ratio_[i];
        seg.prev_curvature = seg.curvature;
        seg.curvature = (seg.curvature + dt * drive) / denom;

        // Clamp: crawling ~10/mm, omega ~20/mm
        double max_curv = (omega_mode_ && i < 4) ? 20.0 : 10.0;
        seg.curvature = std::clamp(seg.curvature, -max_curv, max_curv);
    }

    // --- 2. Update angles from curvatures ---
    for (int i = 1; i < N; ++i) {
        segments_[i].angle = segments_[i - 1].angle - segments_[i].curvature * ds;
    }

    // --- 3. RFT-based forward speed (Gray & Lissmann 1964) ---
    double theta[N];
    for (int i = 0; i < N; ++i) theta[i] = segments_[i].angle;

    // Curvature change rates → shape velocities
    double dkappa[N];
    for (int i = 0; i < N; ++i)
        dkappa[i] = (segments_[i].curvature - segments_[i].prev_curvature) / dt;

    double omega[N];
    omega[0] = 0.0;
    for (int j = 1; j < N; ++j)
        omega[j] = omega[j - 1] - dkappa[j] * ds;

    double vsx[N], vsy[N];
    vsx[0] = vsy[0] = 0.0;
    for (int i = 1; i < N; ++i) {
        double nx = -std::sin(theta[i - 1]);
        double ny =  std::cos(theta[i - 1]);
        vsx[i] = vsx[i - 1] - ds * omega[i - 1] * nx;
        vsy[i] = vsy[i - 1] - ds * omega[i - 1] * ny;
    }

    // 2×2 RFT drag matrix (ratio-based)
    double K = CN_[0] / std::max(CL_[0], 1e-20);
    double C_T = 1.0, C_N = K;
    double A00 = 0, A01 = 0, A11 = 0, b0 = 0, b1 = 0;
    for (int i = 0; i < N; ++i) {
        double tx = std::cos(theta[i]), ty = std::sin(theta[i]);
        double nx = -ty, ny = tx;
        A00 += C_T * tx * tx + C_N * nx * nx;
        A01 += C_T * tx * ty + C_N * nx * ny;
        A11 += C_T * ty * ty + C_N * ny * ny;
        double vs_t = vsx[i] * tx + vsy[i] * ty;
        double vs_n = vsx[i] * nx + vsy[i] * ny;
        b0 -= C_T * vs_t * tx + C_N * vs_n * nx;
        b1 -= C_T * vs_t * ty + C_N * vs_n * ny;
    }

    double det = A00 * A11 - A01 * A01;
    double Vx = 0.0, Vy = 0.0;
    if (std::abs(det) > 1e-20) {
        Vx = ( A11 * b0 - A01 * b1) / det;
        Vy = (-A01 * b0 + A00 * b1) / det;
    }

    double head_tx = std::cos(theta[0]), head_ty = std::sin(theta[0]);
    double forward_speed = (Vx * head_tx + Vy * head_ty) * speed_scale_;
    forward_speed = std::clamp(forward_speed, -1.0, 1.0);

    // --- 4. Direction from command neurons ---
    smooth_fwd_ += (forward_drive_ - smooth_fwd_) * dt / 0.1;
    smooth_rev_ += (reverse_drive_ - smooth_rev_) * dt / 0.1;
    mean_rev_ += (smooth_rev_ - mean_rev_) * dt / 5.0;

    double direction = 1.0;
    if (smooth_rev_ > smooth_fwd_ + 0.1) {
        direction = -1.0;
        forward_speed = std::abs(forward_speed) * 0.6;
    }

    // --- 5. Heading update ---
    double head_curv = segments_[0].curvature + curvature_bias_;
    double dtheta = forward_speed * direction * head_curv * dt;
    double max_dtheta = (omega_mode_ ? 5.24 : 0.87) * dt;
    dtheta = std::clamp(dtheta, -max_dtheta, max_dtheta);
    segments_[0].angle += dtheta;

    // --- 6. Update head position ---
    Vector2d head_dir = Vector2d::from_angle(segments_[0].angle);
    segments_[0].position += head_dir * forward_speed * direction * dt;

    // --- 7. Body segments follow head ---
    for (int i = 1; i < N; ++i) {
        segments_[i].angle = segments_[i - 1].angle - segments_[i].curvature * ds;
        Vector2d dir = Vector2d::from_angle(segments_[i].angle);
        segments_[i].position = segments_[i - 1].position - dir * ds;
    }

    // --- 8. Compute speed ---
    Vector2d head_pos = segments_[0].position;
    speed_ = (head_pos - prev_head_pos_).norm() / dt;
    prev_head_pos_ = head_pos;
}

void BodyModel::update_physics(double dt) {
    update_muscles(dt);
    compute_forces_and_integrate(dt);
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
    // Stretch from horizontal element length vs rest length
    double dist = (segments_[segment].position - segments_[segment - 1].position).norm();
    return (dist - segment_length_) / segment_length_;
}

Vector2d BodyModel::get_segment_position(int segment) const {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return {0, 0};
    return segments_[segment].position;
}

} // namespace celegans
