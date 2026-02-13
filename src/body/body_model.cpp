#include "body/body_model.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace celegans {

BodyModel::BodyModel() {
    segment_length_ = body_length_ / NUM_BODY_SEGMENTS;

    // Elliptical body radius per rod (Boyle worm.cc:180)
    // R[i] = D/2 * |sin(acos((i - N/2) / (N/2 + 0.2)))|
    for (int i = 0; i < NBAR; ++i) {
        double pos = (i - NUM_BODY_SEGMENTS / 2.0) / (NUM_BODY_SEGMENTS / 2.0 + 0.2);
        pos = std::clamp(pos, -1.0, 1.0);
        rod_radius_[i] = D_ / 2.0 * std::abs(std::sin(std::acos(pos)));
    }

    // NMJ weight gradient (Boyle worm.cc:377)
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        nmj_weight_[i] = 0.7 * (1.0 - i * 0.6 / NUM_BODY_SEGMENTS);
    }
    nmj_weight_[0] /= 1.5;  // Prevent excessive head bending

    // Initialize drag coefficients
    compute_drag_coefficients();
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
// Step 135: Explicit quasi-static force balance (Boyle 2012)
//
// At Re ≈ 0 (no inertia): F_muscle + F_elastic + F_drag = 0
// → v_rod = F_internal / C_drag  (per rod, instantaneous)
//
// Architecture: 49 rigid rods connected by 48 muscle element pairs
// (dorsal + ventral horizontal springs) + 48 diagonal elements.
// Each rod has center-of-mass (x, y) and orientation θ.
// Terminal (D/V) points at ±R from CoM define muscle attachment.
//
// REF: Boyle, Berri & Cohen 2012, Front Comput Neurosci 6:10
//      (resrob function, worm.cc lines 503-728)
// ===================================================================
void BodyModel::compute_forces_and_integrate(double dt) {
    const int N = NUM_BODY_SEGMENTS; // 48
    const double max_v = segment_length_ / dt * 0.5; // velocity clamp for stability

    // --- 1. Build rod state (49 rods) ---
    double rx[NBAR], ry[NBAR], rth[NBAR];
    for (int i = 0; i < N; ++i) {
        rx[i]  = segments_[i].position.x;
        ry[i]  = segments_[i].position.y;
        rth[i] = segments_[i].angle;
    }
    // Tail rod: extends from last segment
    rx[N]  = rx[N-1] - segment_length_ * std::cos(rth[N-1]);
    ry[N]  = ry[N-1] - segment_length_ * std::sin(rth[N-1]);
    rth[N] = rth[N-1];

    // --- 2. Terminal (D/V) positions ---
    double term[NBAR][2][2]; // [rod][d=0/v=1][x=0/y=1]
    for (int i = 0; i < NBAR; ++i) {
        double dx = rod_radius_[i] * std::cos(rth[i]);
        double dy = rod_radius_[i] * std::sin(rth[i]);
        term[i][0][0] = rx[i] + dx;  term[i][0][1] = ry[i] + dy;
        term[i][1][0] = rx[i] - dx;  term[i][1][1] = ry[i] - dy;
    }

    // --- 3. Horizontal element lengths & directions ---
    double Lh[N][2], Dirh[N][2][2];
    for (int i = 0; i < N; ++i) {
        for (int dv = 0; dv < 2; ++dv) {
            double ex = term[i+1][dv][0] - term[i][dv][0];
            double ey = term[i+1][dv][1] - term[i][dv][1];
            Lh[i][dv] = std::sqrt(ex*ex + ey*ey);
            double inv = (Lh[i][dv] > 1e-15) ? 1.0 / Lh[i][dv] : 0.0;
            Dirh[i][dv][0] = ex * inv;
            Dirh[i][dv][1] = ey * inv;
        }
    }

    // --- 4. Diagonal element lengths & directions ---
    double Ld[N][2], Dird[N][2][2];
    for (int i = 0; i < N; ++i) {
        // \ : dorsal[i] → ventral[i+1]
        double ex = term[i+1][1][0] - term[i][0][0];
        double ey = term[i+1][1][1] - term[i][0][1];
        Ld[i][0] = std::sqrt(ex*ex + ey*ey);
        double inv = 1.0 / std::max(Ld[i][0], 1e-15);
        Dird[i][0][0] = ex * inv;  Dird[i][0][1] = ey * inv;
        // / : ventral[i] → dorsal[i+1]
        ex = term[i+1][0][0] - term[i][1][0];
        ey = term[i+1][0][1] - term[i][1][1];
        Ld[i][1] = std::sqrt(ex*ex + ey*ey);
        inv = 1.0 / std::max(Ld[i][1], 1e-15);
        Dird[i][1][0] = ex * inv;  Dird[i][1][1] = ey * inv;
    }

    // --- 5. Rest lengths (Boyle worm.cc:191-197) ---
    double L0P[N], L0Pmm[N], L0Darr[N];
    for (int i = 0; i < N; ++i) {
        double dR = rod_radius_[i] - rod_radius_[i+1];
        L0P[i] = std::sqrt(segment_length_ * segment_length_ + dR * dR);
        double scale = 0.65 * (rod_radius_[i] + rod_radius_[i+1]) / D_;
        L0Pmm[i] = L0P[i] - (1.0 - scale) * L0P[i]; // = scale * L0P = contraction range
        double sR = rod_radius_[i] + rod_radius_[i+1];
        L0Darr[i] = std::sqrt(segment_length_ * segment_length_ + sR * sR);
    }

    // --- 6. Muscle + passive forces (Boyle worm.cc:613-636) ---
    double FH[N][2];
    for (int i = 0; i < N; ++i) {
        for (int dv = 0; dv < 2; ++dv) {
            double Vm = (dv == 0) ? segments_[std::min(i, N-1)].V_muscle_dorsal
                                  : segments_[std::min(i, N-1)].V_muscle_ventral;
            Vm = std::max(Vm, 0.0);

            // Active element rest length (contracts when muscle active)
            double L0_AE = L0P[i] - Vm * L0Pmm[i];
            // Active elastic force
            double F_AE = k_AE_ * Vm * (L0_AE - Lh[i][dv]);
            // Passive elastic force with hardening (Boyle worm.cc:619)
            double F_PE = k_PE_ * (L0P[i] - Lh[i][dv]);
            double over = Lh[i][dv] - L0P[i];
            if (over > 0.0) F_PE += k_PE_ * std::pow(2.0 * over, 4);

            FH[i][dv] = F_PE + F_AE;
        }
    }

    // Diagonal forces (Boyle worm.cc:634)
    double FD[N][2];
    for (int i = 0; i < N; ++i) {
        FD[i][0] = k_DE_ * (L0Darr[i] - Ld[i][0]);
        FD[i][1] = k_DE_ * (L0Darr[i] - Ld[i][1]);
    }

    // --- 7. Accumulate forces at terminals (Boyle worm.cc:672-693) ---
    double Ft[NBAR][2][2]; // [rod][d/v][x/y]
    // Head rod
    Ft[0][0][0] = -FH[0][0]*Dirh[0][0][0] - FD[0][0]*Dird[0][0][0];
    Ft[0][0][1] = -FH[0][0]*Dirh[0][0][1] - FD[0][0]*Dird[0][0][1];
    Ft[0][1][0] = -FH[0][1]*Dirh[0][1][0] - FD[0][1]*Dird[0][1][0];
    Ft[0][1][1] = -FH[0][1]*Dirh[0][1][1] - FD[0][1]*Dird[0][1][1];
    // Interior rods
    for (int i = 1; i < N; ++i) {
        Ft[i][0][0] = FH[i-1][0]*Dirh[i-1][0][0] - FH[i][0]*Dirh[i][0][0]
                    + FD[i-1][1]*Dird[i-1][1][0] - FD[i][0]*Dird[i][0][0];
        Ft[i][0][1] = FH[i-1][0]*Dirh[i-1][0][1] - FH[i][0]*Dirh[i][0][1]
                    + FD[i-1][1]*Dird[i-1][1][1] - FD[i][0]*Dird[i][0][1];
        Ft[i][1][0] = FH[i-1][1]*Dirh[i-1][1][0] - FH[i][1]*Dirh[i][1][0]
                    + FD[i-1][0]*Dird[i-1][0][0] - FD[i][1]*Dird[i][1][0];
        Ft[i][1][1] = FH[i-1][1]*Dirh[i-1][1][1] - FH[i][1]*Dirh[i][1][1]
                    + FD[i-1][0]*Dird[i-1][0][1] - FD[i][1]*Dird[i][1][1];
    }
    // Tail rod
    Ft[N][0][0] = FH[N-1][0]*Dirh[N-1][0][0] + FD[N-1][1]*Dird[N-1][1][0];
    Ft[N][0][1] = FH[N-1][0]*Dirh[N-1][0][1] + FD[N-1][1]*Dird[N-1][1][1];
    Ft[N][1][0] = FH[N-1][1]*Dirh[N-1][1][0] + FD[N-1][0]*Dird[N-1][0][0];
    Ft[N][1][1] = FH[N-1][1]*Dirh[N-1][1][1] + FD[N-1][0]*Dird[N-1][0][1];

    // --- 8. Convert terminal forces → rod velocities via RFT (Boyle worm.cc:696-720) ---
    // Smooth direction drives (for behavioral state tracking)
    smooth_fwd_ += (forward_drive_ - smooth_fwd_) * dt / 0.1;
    smooth_rev_ += (reverse_drive_ - smooth_rev_) * dt / 0.1;
    mean_rev_ += (smooth_rev_ - mean_rev_) * dt / 5.0;
    bool reversing = smooth_rev_ > smooth_fwd_ + 0.1;
    double dir_scale = reversing ? -0.6 : 1.0;  // reverse at 60% speed (Fang-Yen 2010)

    for (int i = 0; i < NBAR; ++i) {
        double ct = std::cos(rth[i]), st = std::sin(rth[i]);
        // Rotate forces to body frame
        double Fr[2][2]; // [d/v][perp/par]
        for (int dv = 0; dv < 2; ++dv) {
            Fr[dv][0] =  Ft[i][dv][0]*ct + Ft[i][dv][1]*st;  // perpendicular (normal)
            Fr[dv][1] =  Ft[i][dv][0]*st - Ft[i][dv][1]*ct;  // parallel (tangential)
        }
        // Normal velocity: F_perp / C_N
        double Vn = (Fr[0][0] + Fr[1][0]) / CN_[i];
        // Tangential velocity + angular velocity
        double Feven = Fr[0][1] + Fr[1][1];
        double Fodd  = (Fr[1][1] - Fr[0][1]) * 0.5;
        double Vt = Feven / CL_[i];
        double w  = (rod_radius_[i] > 1e-10)
                   ? (Fodd / CL_[i]) / (M_PI * 2.0 * rod_radius_[i])
                   : 0.0;
        // Rotate back to lab frame
        double vxi = Vn * ct + Vt * st;
        double vyi = Vn * st - Vt * ct;
        // Apply speed scale (neuromodulation) + direction
        vxi *= speed_scale_ * dir_scale;
        vyi *= speed_scale_ * dir_scale;
        w   *= speed_scale_;  // angular velocity not direction-flipped
        // Velocity clamp for numerical stability
        vxi = std::clamp(vxi, -max_v, max_v);
        vyi = std::clamp(vyi, -max_v, max_v);
        // Integrate position
        rx[i]  += vxi * dt;
        ry[i]  += vyi * dt;
        rth[i] += w * dt;
    }

    // --- 9. Write back to segments ---
    for (int i = 0; i < N; ++i) {
        segments_[i].position.x = rx[i];
        segments_[i].position.y = ry[i];
        segments_[i].angle = rth[i];
    }

    // --- 10. Compute curvatures from rod angles (for feedback & diagnostics) ---
    for (int i = 0; i < N; ++i) {
        segments_[i].prev_curvature = segments_[i].curvature;
        if (i < N - 1) {
            segments_[i].curvature = (segments_[i].angle - segments_[i+1].angle) / segment_length_;
        } else {
            segments_[i].curvature = segments_[i-1].curvature;
        }
    }

    // --- 11. Compute speed ---
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
