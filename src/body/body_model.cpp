#include "body/body_model.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace celegans {

using namespace BodyParams;

BodyModel::BodyModel() {
    compute_radii();
    seg_len_ = BODY_LENGTH / NSEG;
}

// ================================================================
// Geometry: prolate ellipsoid radii
// ================================================================
void BodyModel::compute_radii() {
    for (int i = 0; i < NBAR; ++i) {
        double s = static_cast<double>(i - NSEG / 2) / (NSEG / 2.0 + 0.2);
        double r = R_MAX * std::abs(std::sin(std::acos(std::clamp(s, -1.0, 1.0))));
        r = std::max(r, R_MIN_RATIO * R_MAX);
        rods_[i].radius = r;
    }
}

// ================================================================
// Rest lengths from actual initial geometry (accounts for varying radii)
// ================================================================
void BodyModel::compute_rest_lengths() {
    for (int i = 0; i < NSEG; ++i) {
        const auto& r0 = rods_[i];
        const auto& r1 = rods_[i + 1];

        // Lateral dorsal: D_i to D_{i+1}
        double ddx = r1.dx() - r0.dx();
        double ddy = r1.dy() - r0.dy();
        rest_len_dorsal_[i] = std::sqrt(ddx * ddx + ddy * ddy);

        // Lateral ventral: V_i to V_{i+1}
        double dvx = r1.vx() - r0.vx();
        double dvy = r1.vy() - r0.vy();
        rest_len_ventral_[i] = std::sqrt(dvx * dvx + dvy * dvy);

        // Diagonal D_i → V_{i+1}
        double dv_x = r1.vx() - r0.dx();
        double dv_y = r1.vy() - r0.dy();
        rest_len_diag_dv_[i] = std::sqrt(dv_x * dv_x + dv_y * dv_y);

        // Diagonal V_i → D_{i+1}
        double vd_x = r1.dx() - r0.vx();
        double vd_y = r1.dy() - r0.vy();
        rest_len_diag_vd_[i] = std::sqrt(vd_x * vd_x + vd_y * vd_y);

        // Boyle 2012: L_min depends on local radii
        // scale = 0.65 * (R[i] + R[i+1]) / D, where D = max diameter = 2*R_MAX
        double scale = 0.65 * (r0.radius + r1.radius) / (2.0 * R_MAX);
        double L0_avg = 0.5 * (rest_len_dorsal_[i] + rest_len_ventral_[i]);
        L_min_[i] = (1.0 - scale) * L0_avg;
        L0_minus_Lmin_[i] = L0_avg - L_min_[i];
    }
}

// ================================================================
// Initialize: place rods along heading direction
// ================================================================
void BodyModel::initialize(Vector2d head_pos, double heading) {
    compute_radii();
    seg_len_ = BODY_LENGTH / NSEG;

    // Head position in meters (input is mm)
    double hx = head_pos.x * 1.0e-3;
    double hy = head_pos.y * 1.0e-3;
    double cos_h = std::cos(heading);
    double sin_h = std::sin(heading);

    for (int i = 0; i < NBAR; ++i) {
        rods_[i].cx = hx - cos_h * (i * seg_len_);
        rods_[i].cy = hy - sin_h * (i * seg_len_);
        rods_[i].phi = heading + PI * 0.5;
    }

    // Rest lengths from straight geometry (zero initial stress)
    compute_rest_lengths();

    // Copy to prev for velocity init
    prev_rods_ = rods_;

    for (auto& m : muscles_) {
        m.dorsal_activation = 0.0;
        m.ventral_activation = 0.0;
        m.dorsal_input = 0.0;
        m.ventral_input = 0.0;
    }

    // Update drag coefficients
    cn_ = ((1.0 - medium_) * CN_WATER + medium_ * CN_AGAR) / (2.0 * NBAR);
    ct_ = ((1.0 - medium_) * CT_WATER + medium_ * CT_AGAR) / (2.0 * NBAR);

    initialized_ = true;
    sync_segments_from_rods();
    prev_head_pos_ = head_pos;
    speed_ = 0.0;
}

// ================================================================
// Spring-damper force between two points
// ================================================================
BodyModel::SpringForce BodyModel::spring_damper(
    double x0, double y0, double x1, double y1,
    double vx0, double vy0, double vx1, double vy1,
    double K, double D, double L0) const
{
    double dx = x1 - x0;
    double dy = y1 - y0;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0e-15) return {0, 0, 0, 0};

    double ux = dx / len;
    double uy = dy / len;

    // Relative velocity along spring direction
    double dvx = vx1 - vx0;
    double dvy = vy1 - vy0;
    double v_along = dvx * ux + dvy * uy;

    double f = K * (len - L0) + D * v_along;

    return {f * ux, f * uy, -f * ux, -f * uy};
}

// ================================================================
// Lateral passive forces (cuticle) — endpoint-driven
// Uses spring_damper: f = K*(L-L0) + D*v_along
//   f > 0 → attraction (restoring stretch), f < 0 → repulsion (restoring compression)
// ================================================================
void BodyModel::apply_lateral_forces(int seg, double* fdx, double* fdy, double* fvx, double* fvy) {
    const auto& r0 = rods_[seg];
    const auto& r1 = rods_[seg + 1];
    const auto& p0 = prev_rods_[seg];
    const auto& p1 = prev_rods_[seg + 1];
    double inv_dt = 1.0e4;  // 1/0.1ms

    // Dorsal: D_i → D_{i+1}
    auto sf_d = spring_damper(r0.dx(), r0.dy(), r1.dx(), r1.dy(),
        (r0.dx()-p0.dx())*inv_dt, (r0.dy()-p0.dy())*inv_dt,
        (r1.dx()-p1.dx())*inv_dt, (r1.dy()-p1.dy())*inv_dt,
        K_PE, D_PE, rest_len_dorsal_[seg]);
    fdx[seg]   += sf_d.fx0; fdy[seg]   += sf_d.fy0;
    fdx[seg+1] += sf_d.fx1; fdy[seg+1] += sf_d.fy1;

    // Ventral: V_i → V_{i+1}
    auto sf_v = spring_damper(r0.vx(), r0.vy(), r1.vx(), r1.vy(),
        (r0.vx()-p0.vx())*inv_dt, (r0.vy()-p0.vy())*inv_dt,
        (r1.vx()-p1.vx())*inv_dt, (r1.vy()-p1.vy())*inv_dt,
        K_PE, D_PE, rest_len_ventral_[seg]);
    fvx[seg]   += sf_v.fx0; fvy[seg]   += sf_v.fy0;
    fvx[seg+1] += sf_v.fx1; fvy[seg+1] += sf_v.fy1;
}

// ================================================================
// Active muscle forces — Hill-type, parallel to lateral springs.
// Boyle 2012: muscle activation shortens rest length → contraction force.
// Convention: same as spring_damper (f > 0 = attraction = contraction)
// F_muscle = K_AE * A * (L - L0_AE) + A * D_AE * v_along
// where L0_AE = L0 - A * (L0 - L_min) < L0
// At rest (L=L0), F_muscle = K_AE * A * (L0 - L0_AE) > 0 → contraction ✓
// ================================================================
void BodyModel::apply_muscle_forces(int seg, double* fdx, double* fdy, double* fvx, double* fvy) {
    const auto& r0 = rods_[seg];
    const auto& r1 = rods_[seg + 1];
    const auto& p0 = prev_rods_[seg];
    const auto& p1 = prev_rods_[seg + 1];
    double inv_dt = 1.0e4;

    const auto& m = muscles_[seg];
    double A_d = std::max(m.dorsal_activation, 0.0);
    double A_v = std::max(m.ventral_activation, 0.0);

    // Track per-segment D/V force magnitudes for rotation computation
    double f_d_mag = 0.0, f_v_mag = 0.0;

    // ---- Dorsal muscle ----
    if (A_d > 1e-6) {
        double dx = r1.dx() - r0.dx();
        double dy = r1.dy() - r0.dy();
        double L = std::sqrt(dx * dx + dy * dy);
        if (L < 1e-15) L = 1e-15;
        double ux = dx / L, uy = dy / L;

        double vdx0 = (r0.dx() - p0.dx()) * inv_dt;
        double vdy0 = (r0.dy() - p0.dy()) * inv_dt;
        double vdx1 = (r1.dx() - p1.dx()) * inv_dt;
        double vdy1 = (r1.dy() - p1.dy()) * inv_dt;
        double v_along = (vdx1 - vdx0) * ux + (vdy1 - vdy0) * uy;

        // Activation-dependent rest length (shorter = more contraction)
        double L0 = rest_len_dorsal_[seg];
        double L0_AE = L0 - A_d * L0_minus_Lmin_[seg];

        // Muscle spring + damping (same convention as spring_damper: f>0 = attraction)
        double f = K_AE * A_d * (L - L0_AE) + A_d * D_AE * v_along;
        f_d_mag = f;

        fdx[seg]   += f * ux;   fdy[seg]   += f * uy;
        fdx[seg+1] -= f * ux;   fdy[seg+1] -= f * uy;
    }

    // ---- Ventral muscle ----
    if (A_v > 1e-6) {
        double dx = r1.vx() - r0.vx();
        double dy = r1.vy() - r0.vy();
        double L = std::sqrt(dx * dx + dy * dy);
        if (L < 1e-15) L = 1e-15;
        double ux = dx / L, uy = dy / L;

        double vvx0 = (r0.vx() - p0.vx()) * inv_dt;
        double vvy0 = (r0.vy() - p0.vy()) * inv_dt;
        double vvx1 = (r1.vx() - p1.vx()) * inv_dt;
        double vvy1 = (r1.vy() - p1.vy()) * inv_dt;
        double v_along = (vvx1 - vvx0) * ux + (vvy1 - vvy0) * uy;

        double L0 = rest_len_ventral_[seg];
        double L0_AE = L0 - A_v * L0_minus_Lmin_[seg];

        double f = K_AE * A_v * (L - L0_AE) + A_v * D_AE * v_along;
        f_v_mag = f;

        fvx[seg]   += f * ux;   fvy[seg]   += f * uy;
        fvx[seg+1] -= f * ux;   fvy[seg+1] -= f * uy;
    }

    // Store per-segment D/V force difference for rotation computation
    seg_torque_[seg] += f_d_mag - f_v_mag;
}

// ================================================================
// Diagonal passive forces (internal pressure) — endpoint-driven
// ================================================================
void BodyModel::apply_diagonal_forces(int seg, double* fdx, double* fdy, double* fvx, double* fvy) {
    const auto& r0 = rods_[seg];
    const auto& r1 = rods_[seg + 1];
    const auto& p0 = prev_rods_[seg];
    const auto& p1 = prev_rods_[seg + 1];
    double inv_dt = 1.0e4;

    // D_i → V_{i+1}
    auto sf_dv = spring_damper(r0.dx(), r0.dy(), r1.vx(), r1.vy(),
        (r0.dx()-p0.dx())*inv_dt, (r0.dy()-p0.dy())*inv_dt,
        (r1.vx()-p1.vx())*inv_dt, (r1.vy()-p1.vy())*inv_dt,
        K_DE, D_DE, rest_len_diag_dv_[seg]);
    fdx[seg]   += sf_dv.fx0; fdy[seg]   += sf_dv.fy0;
    fvx[seg+1] += sf_dv.fx1; fvy[seg+1] += sf_dv.fy1;

    // V_i → D_{i+1}
    auto sf_vd = spring_damper(r0.vx(), r0.vy(), r1.dx(), r1.dy(),
        (r0.vx()-p0.vx())*inv_dt, (r0.vy()-p0.vy())*inv_dt,
        (r1.dx()-p1.dx())*inv_dt, (r1.dy()-p1.dy())*inv_dt,
        K_DE, D_DE, rest_len_diag_vd_[seg]);
    fvx[seg]   += sf_vd.fx0; fvy[seg]   += sf_vd.fy0;
    fdx[seg+1] += sf_vd.fx1; fdy[seg+1] += sf_vd.fy1;
}


// ================================================================
// Self-collision repulsion — applied to both endpoints equally
// ================================================================
void BodyModel::apply_self_collision(double* fdx, double* fdy, double* fvx, double* fvy) {
    for (int i = 0; i < NBAR; ++i) {
        for (int j = i + 4; j < NBAR; ++j) {
            double dx = rods_[i].cx - rods_[j].cx;
            double dy = rods_[i].cy - rods_[j].cy;
            double d2 = dx * dx + dy * dy;
            double threshold = 2.0 * R_MAX;
            double t2 = threshold * threshold;
            if (d2 < t2 && d2 > 1e-20) {
                double d = std::sqrt(d2);
                double f = K_CONTACT * (threshold - d);
                double ux = dx / d;
                double uy = dy / d;
                double fhx = 0.5 * f * ux;
                double fhy = 0.5 * f * uy;
                fdx[i] += fhx; fdy[i] += fhy;
                fvx[i] += fhx; fvy[i] += fhy;
                fdx[j] -= fhx; fdy[j] -= fhy;
                fvx[j] -= fhx; fvy[j] -= fhy;
            }
        }
    }
}

// ================================================================
// Reconstruct rod center & angle from endpoint positions
// ================================================================
void BodyModel::reconstruct_rod(int i, double Dx, double Dy, double Vx, double Vy) {
    rods_[i].cx = (Dx + Vx) * 0.5;
    rods_[i].cy = (Dy + Vy) * 0.5;

    double ddx = Dx - Vx;
    double ddy = Dy - Vy;
    rods_[i].phi = std::atan2(-ddx, ddy);

    // Enforce rod length = 2*radius
    double len = std::sqrt(ddx * ddx + ddy * ddy);
    double target = 2.0 * rods_[i].radius;
    if (len > 1e-15 && std::abs(len - target) > 1e-15) {
        double scale = target / len;
        double mx = rods_[i].cx, my = rods_[i].cy;
        double hddx = (Dx - mx) * scale;
        double hddy = (Dy - my) * scale;
        // Update cx,cy,phi from corrected endpoints
        rods_[i].cx = mx;
        rods_[i].cy = my;
        // phi is already correct (direction unchanged by scaling)
    }
}

// ================================================================
// Muscle activation leaky integrator
// ================================================================
void BodyModel::update_muscle_activations(double dt) {
    for (int i = 0; i < NSEG; ++i) {
        auto& m = muscles_[i];

        // Scale by typical max input to keep activations in [0,1]
        // while preserving D/V ratio.
        // Head segments: 10 D + 10 V neurons (SMD+URA+SAA+SIA+SIB), input ~4-5
        // Body segments: 2-3 MNs per side (DB/DA/AS + VB/VA), input ~1-2
        // Must be large enough to prevent head saturation (which kills D-V oscillation)
        constexpr double INPUT_SCALE = 6.0;
        double d_in = std::clamp(m.dorsal_input / INPUT_SCALE, 0.0, 1.0);
        double v_in = std::clamp(m.ventral_input / INPUT_SCALE, 0.0, 1.0);

        // Boyle 2012: tau=100ms for muscle low-pass filter
        double alpha = dt / TAU_MUSCLE;
        // TAU_MUSCLE = 0.1s defined in BodyParams
        if (alpha > 1.0) alpha = 1.0;

        m.dorsal_activation  += alpha * (-m.dorsal_activation + d_in);
        m.ventral_activation += alpha * (-m.ventral_activation + v_in);

        m.dorsal_activation  = std::clamp(m.dorsal_activation, 0.0, 1.0);
        m.ventral_activation = std::clamp(m.ventral_activation, 0.0, 1.0);
    }
}

// ================================================================
// Main physics update — endpoint-driven semi-implicit Euler
// ================================================================
void BodyModel::update_physics(double dt_seconds) {
    if (dt_seconds <= 0.0 || !initialized_) return;

    // Per-point drag (total body drag / (2 * N_bar) points)
    double cn_pt = ((1.0 - medium_) * CN_WATER + medium_ * CN_AGAR) / (2.0 * NBAR);
    double ct_pt = ((1.0 - medium_) * CT_WATER + medium_ * CT_AGAR) / (2.0 * NBAR);
    cn_ = cn_pt;
    ct_ = ct_pt;

    // Sub-stepping: 0.1ms body step for stability
    constexpr double DT_BODY = 1.0e-4;
    int n_steps = std::max(1, static_cast<int>(std::ceil(dt_seconds / DT_BODY)));
    double dt_sub = dt_seconds / n_steps;

    // Update muscle activations (once per call, not per sub-step)
    update_muscle_activations(dt_seconds);

    for (int step = 0; step < n_steps; ++step) {
        // Per-endpoint force arrays
        double fdx[NBAR] = {}, fdy[NBAR] = {};
        double fvx[NBAR] = {}, fvy[NBAR] = {};

        // Zero per-segment torque array (accumulated by force functions)
        seg_torque_.fill(0.0);

        // Elastic + muscle forces at endpoints
        for (int s = 0; s < NSEG; ++s) {
            apply_lateral_forces(s, fdx, fdy, fvx, fvy);
            apply_diagonal_forces(s, fdx, fdy, fvx, fvy);
            apply_muscle_forces(s, fdx, fdy, fvx, fvy);
        }

        // Self-collision
        apply_self_collision(fdx, fdy, fvx, fvy);

        // Curvature bias (omega turn) — force scaled to passive spring level
        if (std::abs(curvature_bias_) > 1e-6) {
            double bias_f = curvature_bias_ * K_PE * R_MAX * 2.0;
            for (int i = 0; i < std::min(6, NBAR); ++i) {
                double w = 1.0 - static_cast<double>(i) / 6.0;
                double cos_phi = std::cos(rods_[i].phi);
                double sin_phi = std::sin(rods_[i].phi);
                double bf = bias_f * w;
                fdx[i] +=  bf * cos_phi; fdy[i] +=  bf * sin_phi;
                fvx[i] += -bf * cos_phi; fvy[i] += -bf * sin_phi;
            }
        }

        // Save current state for next velocity computation
        prev_rods_ = rods_;

        // === Boyle-style CoM + Rotation decomposition ===
        // Translation: CoM force (symmetric D+V component) → update cx, cy
        // Rotation: Per-segment D/V torque → update phi
        // These control DIFFERENT variables so they don't fight.

        // --- CoM translation (per rod) ---
        for (int i = 0; i < NBAR; ++i) {
            // CoM force = average of dorsal and ventral forces
            double f_cx = (fdx[i] + fvx[i]) * 0.5;
            double f_cy = (fdy[i] + fvy[i]) * 0.5;

            // Local body tangent
            double tx, ty;
            if (i == 0) {
                tx = rods_[0].cx - rods_[1].cx;
                ty = rods_[0].cy - rods_[1].cy;
            } else if (i == NBAR - 1) {
                tx = rods_[NBAR-2].cx - rods_[NBAR-1].cx;
                ty = rods_[NBAR-2].cy - rods_[NBAR-1].cy;
            } else {
                tx = rods_[i-1].cx - rods_[i+1].cx;
                ty = rods_[i-1].cy - rods_[i+1].cy;
            }
            double tlen = std::sqrt(tx*tx + ty*ty);
            if (tlen < 1e-15) { tx = 1.0; ty = 0.0; }
            else { tx /= tlen; ty /= tlen; }
            double nx = -ty, ny = tx;

            // Anisotropic drag → translational velocity
            double ft = f_cx * tx + f_cy * ty;
            double fn = f_cx * nx + f_cy * ny;
            double vcx = (ft / std::max(ct_pt, 1e-15)) * tx
                       + (fn / std::max(cn_pt, 1e-15)) * nx;
            double vcy = (ft / std::max(ct_pt, 1e-15)) * ty
                       + (fn / std::max(cn_pt, 1e-15)) * ny;

            constexpr double V_MAX = 0.01;  // 10 mm/s
            vcx = std::clamp(vcx, -V_MAX, V_MAX);
            vcy = std::clamp(vcy, -V_MAX, V_MAX);

            rods_[i].cx += vcx * dt_sub;
            rods_[i].cy += vcy * dt_sub;

            // NaN safety
            if (std::isnan(rods_[i].cx) || std::isnan(rods_[i].cy)) {
                rods_[i].cx = prev_rods_[i].cx;
                rods_[i].cy = prev_rods_[i].cy;
            }
        }

        // --- Per-segment rotation (inside sub-step: safe, phi only here) ---
        // Drive rotation from D/V ACTIVATION difference (neural command),
        // not from muscle force difference (which has geometric artifacts
        // on bent bodies: unequal D/V spring lengths → self-reinforcing equilibrium).
        for (int s = 0; s < NSEG; ++s) {
            double R_avg = 0.5 * (rods_[s].radius + rods_[s + 1].radius);
            double dphi = rods_[s].phi - rods_[s + 1].phi;
            while (dphi >  PI) dphi -= 2.0 * PI;
            while (dphi < -PI) dphi += 2.0 * PI;

            // D/V input → low-pass filtered drive signal
            // Raw neural input preserves full oscillation amplitude from SMD,
            // but changes every 0.5ms (too fast → twitching).
            // Low-pass filter with tau=50ms smooths to ~1Hz locomotion band.
            double dv_raw = muscles_[s].dorsal_input - muscles_[s].ventral_input;
            constexpr double TAU_DRIVE = 0.05;  // 50ms — matches locomotion frequency
            double alpha_drive = dt_sub / TAU_DRIVE;
            if (alpha_drive > 1.0) alpha_drive = 1.0;
            muscles_[s].dv_drive += alpha_drive * (dv_raw - muscles_[s].dv_drive);

            // Head-to-tail gradient (Boyle 2012: anterior muscles stronger)
            double grad = 1.0 - 0.4 * static_cast<double>(s) / NSEG;
            // K_BEND: filtered_input → torque gain
            constexpr double K_BEND = 6e-7;  // N
            double tau_drive = K_BEND * muscles_[s].dv_drive * grad * R_avg;

            // Diagonal restoring torque (antisymmetric component lost in CoM integration)
            double tau_restore = 2.0 * K_DE * R_avg * R_avg * dphi;
            double tau_net = tau_drive - tau_restore;

            // Rotational drag (Boyle 2012): γ = 4π × cn_pt × R²
            // Structural damping (2×) for explicit Euler stability
            double gamma_rot = 2.0 * 4.0 * PI * cn_pt * R_avg * R_avg;
            double omega_seg = tau_net / std::max(gamma_rot, 1e-15);

            // Hard clamp on dphi
            constexpr double DPHI_MAX = 0.04;  // ~1.9 /mm
            double new_dphi = dphi + omega_seg * dt_sub;
            new_dphi = std::clamp(new_dphi, -DPHI_MAX, DPHI_MAX);
            double actual_omega = (new_dphi - dphi) / dt_sub;

            rods_[s].phi     += actual_omega * dt_sub * 0.5;
            rods_[s + 1].phi -= actual_omega * dt_sub * 0.5;
        }
    }

    // Compute speed BEFORE phi-drive and center correction,
    // so speed reflects only force-integrated locomotion.
    {
        Vector2d new_head = get_head_position();
        double dx_mm = new_head.x - prev_head_pos_.x;
        double dy_mm = new_head.y - prev_head_pos_.y;
        speed_ = std::sqrt(dx_mm * dx_mm + dy_mm * dy_mm) / (dt_seconds > 0 ? dt_seconds : 1.0);
        prev_head_pos_ = new_head;
    }

    // Sync backward-compat segments (speed already computed above)
    sync_segments_from_rods();
}

// ================================================================
// Sync rod state → backward-compat BodySegment array
// ================================================================
void BodyModel::sync_segments_from_rods() {
    for (int i = 0; i < NUM_BODY_SEGMENTS && i < NSEG; ++i) {
        auto& seg = segments_[i];
        // Segment position = midpoint between rod i and rod i+1 centers
        seg.position.x = (rods_[i].cx + rods_[i + 1].cx) * 0.5 * 1e3;  // m → mm
        seg.position.y = (rods_[i].cy + rods_[i + 1].cy) * 0.5 * 1e3;
        // Segment angle = forward (heading) direction: rod i+1 → rod i
        double dx = rods_[i].cx - rods_[i + 1].cx;
        double dy = rods_[i].cy - rods_[i + 1].cy;
        seg.angle = std::atan2(dy, dx);
        // Curvature from PHI differences (stable for proprioception).
        // Center correction moves cx/cy for heading changes but must NOT
        // feed back into proprioception (would create positive feedback).
        seg.prev_curvature = seg.curvature;
        if (i > 0) {
            double dphi_curv = rods_[i].phi - rods_[i + 1].phi;
            while (dphi_curv >  PI) dphi_curv -= 2.0 * PI;
            while (dphi_curv < -PI) dphi_curv += 2.0 * PI;
            double seg_len_mm = BODY_LENGTH * 1e3 / NSEG;
            seg.curvature = dphi_curv / seg_len_mm;  // 1/mm
        }
        seg.dorsal_activation = (i < NSEG) ? muscles_[i].dorsal_activation : 0.0;
        seg.ventral_activation = (i < NSEG) ? muscles_[i].ventral_activation : 0.0;
    }
    // Clamp all curvatures to prevent positive feedback explosion
    constexpr double CURV_MAX = 15.0;  // /mm — biological range ~5-10
    for (int i = 1; i < NUM_BODY_SEGMENTS && i < NSEG; ++i) {
        segments_[i].curvature = std::clamp(segments_[i].curvature, -CURV_MAX, CURV_MAX);
    }
    // Segment 0 = segment 1 (enables head proprioceptive wave propagation)
    if (NSEG > 1) {
        segments_[0].curvature = segments_[1].curvature;
    }
}

// ================================================================
// Muscle activation interface (backward compat)
// Motor controller calls these — maps to muscle inputs
// ================================================================
void BodyModel::reset_activations() {
    for (auto& m : muscles_) {
        m.dorsal_input = 0.0;
        m.ventral_input = 0.0;
    }
    for (auto& seg : segments_) {
        seg.dorsal_activation = 0.0;
        seg.ventral_activation = 0.0;
    }
}

void BodyModel::set_muscle_activation(int segment, bool dorsal, double activation) {
    if (segment < 0 || segment >= NSEG) return;
    if (dorsal)
        muscles_[segment].dorsal_input += activation;
    else
        muscles_[segment].ventral_input += activation;
}

void BodyModel::set_muscle_activation_direct(int segment, bool dorsal, double activation) {
    // Write to segments_ (display/readback), NOT muscles_ (physics input).
    // The D-class inhibitory pass reads segments_[].dorsal_activation (which is 0 after reset)
    // and would overwrite muscles_[].dorsal_input with 0, destroying excitatory drive.
    if (segment < 0 || segment >= NSEG) return;
    activation = std::clamp(activation, 0.0, 1.0);
    auto& seg = segments_[segment];
    if (dorsal)
        seg.dorsal_activation = activation;
    else
        seg.ventral_activation = activation;
}

// ================================================================
// Backward-compat API
// ================================================================
void BodyModel::perturb_heading(double dtheta) {
    // In rod model: rotate head rod (non-physical but retained for compat)
    rods_[0].phi += dtheta;
    sync_segments_from_rods();
}

void BodyModel::set_locomotion_state(double forward_drive, double reverse_drive) {
    forward_drive_ = forward_drive;
    reverse_drive_ = reverse_drive;
}

void BodyModel::set_position(double x, double y) {
    // Shift all rods so head is at (x,y) in mm
    double dx_m = (x - get_head_position().x) * 1e-3;
    double dy_m = (y - get_head_position().y) * 1e-3;
    for (auto& r : rods_) {
        r.cx += dx_m;
        r.cy += dy_m;
    }
    prev_head_pos_ = {x, y};
    sync_segments_from_rods();
}

void BodyModel::set_heading(double angle) {
    // Rotate head rod to match angle
    rods_[0].phi = angle + PI * 0.5;
    sync_segments_from_rods();
}

void BodyModel::nudge_position(double dx, double dy) {
    double dx_m = dx * 1e-3;
    double dy_m = dy * 1e-3;
    for (auto& r : rods_) {
        r.cx += dx_m;
        r.cy += dy_m;
    }
    prev_head_pos_.x += dx;
    prev_head_pos_.y += dy;
    sync_segments_from_rods();
}

Vector2d BodyModel::get_head_position() const {
    return {rods_[0].cx * 1e3, rods_[0].cy * 1e3};  // m → mm
}

double BodyModel::get_head_angle() const {
    // Forward direction: tail→head (rod 1 → rod 0)
    if (NBAR < 2) return 0.0;
    double dx = rods_[0].cx - rods_[1].cx;
    double dy = rods_[0].cy - rods_[1].cy;
    return std::atan2(dy, dx);
}

Vector2d BodyModel::get_tail_position() const {
    return {rods_[NBAR - 1].cx * 1e3, rods_[NBAR - 1].cy * 1e3};
}

double BodyModel::get_local_curvature(int segment) const {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return 0.0;
    return segments_[segment].curvature;
}

double BodyModel::get_local_stretch(int segment) const {
    if (segment < 0 || segment >= NSEG) return 0.0;
    // Stretch = (current_lateral_length - rest) / rest
    const auto& r0 = rods_[segment];
    const auto& r1 = rods_[segment + 1];
    double dx_d = r1.dx() - r0.dx();
    double dy_d = r1.dy() - r0.dy();
    double len_d = std::sqrt(dx_d * dx_d + dy_d * dy_d);
    return (len_d - rest_len_dorsal_[segment]) / rest_len_dorsal_[segment];
}

Vector2d BodyModel::get_segment_position(int segment) const {
    if (segment < 0 || segment >= NUM_BODY_SEGMENTS) return {0, 0};
    return segments_[segment].position;
}

} // namespace celegans
