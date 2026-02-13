#include "body/body_model_3d.h"
#include <algorithm>
#include <cmath>

namespace celegans {

// ================================================================
// Step 129: 3D Biomechanical Body Model
// REF: Boyle, Berri & Cohen 2012 (Frontiers Comput Neurosci)
//      Palyanov et al. 2018 (Phil Trans B)
//      Park et al. 2008 PNAS (body mechanics measurements)
// ================================================================

BodyModel3D::BodyModel3D() {
    segment_length_ = body_length_ / NUM_BODY_SEGMENTS;
    init_muscles();
    compute_radii();
}

void BodyModel3D::initialize(Vector3d head_pos, double heading_xy, double heading_z) {
    // Place nodes along a straight line from head to tail
    Vector3d dir = {std::cos(heading_xy) * std::cos(heading_z),
                    std::sin(heading_xy) * std::cos(heading_z),
                    std::sin(heading_z)};

    for (int i = 0; i < NUM_3D_NODES; ++i) {
        double s = i * segment_length_;
        nodes_[i].pos = head_pos - dir * s;  // head at index 0, tail at end
        nodes_[i].vel = {0, 0, 0};
        nodes_[i].tangent = dir * (-1.0);     // tangent points head→tail initially
        // Dorsal = +z by default (worm crawls on x-y plane, dorsal side up)
        nodes_[i].dorsal = {0, 0, 1};
        nodes_[i].lateral = dir.cross(nodes_[i].dorsal).normalized();
        if (nodes_[i].lateral.norm() < 1e-6) {
            nodes_[i].lateral = {0, 1, 0};
        }
    }

    compute_radii();
    update_surface_points();

    // Initialize muscle rest lengths
    for (auto& m : muscles_) {
        int i = m.seg_start;
        // Muscle connects adjacent surface points on same side
        Vector3d p0, p1;
        switch (m.quadrant) {
            case MuscleQuadrant::DORSAL_LEFT:  p0 = nodes_[i].pos_dl; p1 = nodes_[i+1].pos_dl; break;
            case MuscleQuadrant::DORSAL_RIGHT: p0 = nodes_[i].pos_dr; p1 = nodes_[i+1].pos_dr; break;
            case MuscleQuadrant::VENTRAL_LEFT: p0 = nodes_[i].pos_vl; p1 = nodes_[i+1].pos_vl; break;
            case MuscleQuadrant::VENTRAL_RIGHT:p0 = nodes_[i].pos_vr; p1 = nodes_[i+1].pos_vr; break;
        }
        m.rest_length = (p1 - p0).norm();
        m.length = m.rest_length;
        m.prev_length = m.rest_length;
    }

    speed_ = 0.0;
    smooth_fwd_ = 0.5;
    smooth_rev_ = 0.0;
}

// ================================================================
// Muscle initialization: 95 body wall muscle cells
// C. elegans has 95 BWMs: 24 DL + 24 DR + 24 VL + 23 VR
// Each muscle spans ~2 segments but for simplicity we map 1:1
// Muscles are distributed along the 48 segments
// REF: Altun & Hall, WormAtlas
// ================================================================
void BodyModel3D::init_muscles() {
    muscles_.clear();
    muscles_.reserve(NUM_MUSCLES);

    // DL: 24 muscles spanning segments 0-47 (every 2 segments)
    for (int i = 0; i < 24; ++i) {
        MuscleCell3D m;
        m.seg_start = i * 2;
        m.quadrant = MuscleQuadrant::DORSAL_LEFT;
        muscles_.push_back(m);
    }
    // DR: 24 muscles
    for (int i = 0; i < 24; ++i) {
        MuscleCell3D m;
        m.seg_start = i * 2;
        m.quadrant = MuscleQuadrant::DORSAL_RIGHT;
        muscles_.push_back(m);
    }
    // VL: 24 muscles
    for (int i = 0; i < 24; ++i) {
        MuscleCell3D m;
        m.seg_start = i * 2;
        m.quadrant = MuscleQuadrant::VENTRAL_LEFT;
        muscles_.push_back(m);
    }
    // VR: 23 muscles (1 fewer on ventral-right, biological asymmetry)
    for (int i = 0; i < 23; ++i) {
        MuscleCell3D m;
        m.seg_start = i * 2;
        m.quadrant = MuscleQuadrant::VENTRAL_RIGHT;
        muscles_.push_back(m);
    }
}

// ================================================================
// Prolate ellipsoid body shape
// Radius tapers from 0 at head/tail to max at mid-body
// REF: Boyle 2012 — R_i = R * sqrt(1 - ((i-L/2)/a)^2)
// ================================================================
void BodyModel3D::compute_radii() {
    double half_n = (NUM_3D_NODES - 1) * 0.5;
    double a = half_n * 1.05;  // slightly > half-length to avoid zero at ends
    for (int i = 0; i < NUM_3D_NODES; ++i) {
        double s = (i - half_n) / a;
        double r2 = 1.0 - s * s;
        if (r2 < 0.01) r2 = 0.01;  // minimum radius to prevent singularity
        nodes_[i].radius = body_radius_ * std::sqrt(r2);
    }
}

// ================================================================
// Update surface points from centerline + frame + radius
// 4 surface points per node: DL, DR, VL, VR at 45° from D/V/L axes
// ================================================================
void BodyModel3D::update_surface_points() {
    for (int i = 0; i < NUM_3D_NODES; ++i) {
        double r = nodes_[i].radius;
        Vector3d d = nodes_[i].dorsal * r;
        Vector3d l = nodes_[i].lateral * r;
        // Surface points at ±45° from dorsal/lateral
        double c45 = 0.7071;  // cos(45°)
        nodes_[i].pos_dl = nodes_[i].pos + d * c45 + l * c45;
        nodes_[i].pos_dr = nodes_[i].pos + d * c45 - l * c45;
        nodes_[i].pos_vl = nodes_[i].pos - d * c45 + l * c45;
        nodes_[i].pos_vr = nodes_[i].pos - d * c45 - l * c45;
    }
}

// ================================================================
// Hill-type muscle force computation
// Force = f(activation, length, velocity)
// REF: Boyle 2012 Eq. 5-8, Hill 1938
// ================================================================
void BodyModel3D::compute_muscle_forces(double dt) {
    update_surface_points();

    for (auto& m : muscles_) {
        int i = m.seg_start;
        if (i + 1 >= NUM_3D_NODES) continue;

        // Current muscle length from surface points
        Vector3d p0, p1;
        switch (m.quadrant) {
            case MuscleQuadrant::DORSAL_LEFT:  p0 = nodes_[i].pos_dl; p1 = nodes_[i+1].pos_dl; break;
            case MuscleQuadrant::DORSAL_RIGHT: p0 = nodes_[i].pos_dr; p1 = nodes_[i+1].pos_dr; break;
            case MuscleQuadrant::VENTRAL_LEFT: p0 = nodes_[i].pos_vl; p1 = nodes_[i+1].pos_vl; break;
            case MuscleQuadrant::VENTRAL_RIGHT:p0 = nodes_[i].pos_vr; p1 = nodes_[i+1].pos_vr; break;
        }

        m.prev_length = m.length;
        m.length = (p1 - p0).norm();
        if (m.length < 1e-9) m.length = 1e-9;

        double velocity = (m.length - m.prev_length) / (dt * 0.001);  // mm/s

        // Activation clamp [0, 1]
        double a = std::clamp(m.activation, 0.0, 1.0);

        // Hill force-length: F decreases as muscle shortens beyond optimal
        double l_ratio = m.length / m.rest_length;
        double fl = 1.0;
        if (l_ratio < muscle_lmin_ratio_) {
            fl = 0.0;  // fully contracted, no more force
        } else if (l_ratio < 1.0) {
            fl = (l_ratio - muscle_lmin_ratio_) / (1.0 - muscle_lmin_ratio_);
        } else {
            fl = 1.0 + 0.5 * (l_ratio - 1.0);  // passive stretch increases force slightly
        }

        // Hill force-velocity: F decreases with contraction speed
        double fv = 1.0;
        if (velocity < 0) {
            // Contracting: force decreases with speed
            double v_max = 2.0;  // mm/s max contraction velocity
            fv = (v_max + velocity) / (v_max - velocity * 0.25);
            if (fv < 0) fv = 0;
        } else {
            // Lengthening: eccentric force increase (up to 1.5x)
            fv = 1.0 + 0.5 * velocity / (velocity + 2.0);
        }

        // Anterior-posterior gradient: muscles weaker towards tail
        double ap_gradient = 1.0 - 0.3 * (double(m.seg_start) / NUM_BODY_SEGMENTS);

        // Total muscle force (contractile, negative = shortening)
        m.force = -a * muscle_fmax_ * fl * fv * ap_gradient;

        // Spring + damper parallel element (passive muscle stiffness)
        double spring_f = kappa_muscle_ * a * (m.length - m.rest_length * (1.0 - 0.2 * a));
        double damp_f = beta_muscle_ * a * velocity;
        m.force += spring_f + damp_f;
    }
}

// ================================================================
// Passive body forces: cuticle elasticity + internal pressure
// REF: Boyle 2012 Eq. 1-4
// ================================================================
void BodyModel3D::compute_passive_forces(std::array<Vector3d, NUM_3D_NODES>& forces) {
    for (int i = 1; i < NUM_3D_NODES - 1; ++i) {
        // === Bending stiffness ===
        // Curvature = angle between adjacent segments / segment_length
        Vector3d t_prev = (nodes_[i].pos - nodes_[i-1].pos).normalized();
        Vector3d t_next = (nodes_[i+1].pos - nodes_[i].pos).normalized();

        // Bending moment: restoring force proportional to angle between segments
        Vector3d bend_axis = t_prev.cross(t_next);
        double sin_angle = bend_axis.norm();
        if (sin_angle > 1e-9) {
            bend_axis = bend_axis / sin_angle;
            double angle = std::asin(std::min(sin_angle, 1.0));

            // Restoring force perpendicular to body axis
            double moment = bend_stiffness_ * angle / segment_length_;
            Vector3d f_bend = bend_axis.cross(t_prev) * moment;
            forces[i-1] -= f_bend * 0.5;
            forces[i]   += f_bend;
            forces[i+1] -= f_bend * 0.5;
        }

        // === Lateral curvature tracking ===
        // Dorso-ventral curvature
        Vector3d delta = t_next - t_prev;
        nodes_[i].curvature_dv = delta.dot(nodes_[i].dorsal) / segment_length_;
        nodes_[i].curvature_lr = delta.dot(nodes_[i].lateral) / segment_length_;
    }
    nodes_[0].curvature_dv = nodes_[1].curvature_dv;
    nodes_[0].curvature_lr = nodes_[1].curvature_lr;
    nodes_[NUM_3D_NODES-1].curvature_dv = nodes_[NUM_3D_NODES-2].curvature_dv;
    nodes_[NUM_3D_NODES-1].curvature_lr = nodes_[NUM_3D_NODES-2].curvature_lr;

    // === Lateral springs (cuticle + internal pressure) ===
    // Lateral elements connect adjacent nodes along the body surface
    // They resist changes in segment length (complement PBD constraint)
    for (int i = 0; i < NUM_3D_NODES - 1; ++i) {
        Vector3d d = nodes_[i+1].pos - nodes_[i].pos;
        double len = d.norm();
        if (len < 1e-12) continue;
        double stretch = (len - segment_length_) / segment_length_;
        // Restoring force along body axis
        Vector3d f_lat = d * (kappa_lateral_ * stretch / len);
        forces[i]   += f_lat;
        forces[i+1] -= f_lat;
    }
}

// ================================================================
// Anisotropic drag (Resistive Force Theory)
// C_tangential << C_normal for crawling on agar
// C_tangential ≈ C_normal for swimming in liquid
// REF: Gray & Lissmann 1964, Niebur & Erdos 1991, Boyle 2012
// ================================================================
void BodyModel3D::compute_drag_forces(std::array<Vector3d, NUM_3D_NODES>& forces, double dt) {
    double ct, cn;
    if (loco_mode_ == LocomotionMode::CRAWLING) {
        ct = drag_tangent_;     // 3.3e-3 nN·s/mm²
        cn = drag_normal_;      // 128e-3 nN·s/mm²
    } else {
        // Swimming: much lower drag, less anisotropic
        ct = 1.0e-3;
        cn = 1.5e-3;
    }

    for (int i = 0; i < NUM_3D_NODES; ++i) {
        Vector3d t = nodes_[i].tangent;
        Vector3d v = nodes_[i].vel;

        // Decompose velocity into tangential and normal components
        double v_t = v.dot(t);
        Vector3d v_tang = t * v_t;
        Vector3d v_norm = v - v_tang;

        // Surface area per node (circumference × segment_length)
        double area = 2.0 * PI * nodes_[i].radius * segment_length_;

        // Drag forces (opposing motion)
        Vector3d f_drag = v_tang * (-ct * area) + v_norm * (-cn * area);
        forces[i] += f_drag;
    }
}

// ================================================================
// Apply muscle forces as bending moments on centerline
// Dorsal-ventral differential activation → torque → force couple
// This avoids the positive-feedback instability of spring-based muscles
// REF: Boyle 2012 — muscles as lateral elements create bending
// ================================================================
void BodyModel3D::apply_muscle_forces(std::array<Vector3d, NUM_3D_NODES>& forces) {
    // For each segment, compute net dorsal-ventral activation difference
    // and apply as a perpendicular force couple
    for (int seg = 0; seg < NUM_BODY_SEGMENTS; ++seg) {
        double dorsal_act = 0.0, ventral_act = 0.0;
        int dorsal_count = 0, ventral_count = 0;

        for (const auto& m : muscles_) {
            if (m.seg_start / 2 != seg / 2) continue;
            if (m.quadrant == MuscleQuadrant::DORSAL_LEFT ||
                m.quadrant == MuscleQuadrant::DORSAL_RIGHT) {
                dorsal_act += m.activation;
                dorsal_count++;
            } else {
                ventral_act += m.activation;
                ventral_count++;
            }
        }

        if (dorsal_count > 0) dorsal_act /= dorsal_count;
        if (ventral_count > 0) ventral_act /= ventral_count;

        // Net bending moment = (dorsal - ventral) * moment_arm * max_force
        // Positive = dorsal contraction = bend ventrally
        double dv_diff = dorsal_act - ventral_act;
        if (std::abs(dv_diff) < 1e-6) continue;

        // Anterior-posterior gradient (head muscles weaker to prevent whipping)
        double ap_grad = 1.0 - 0.3 * (double(seg) / NUM_BODY_SEGMENTS);

        // Torque = force * moment_arm, moment_arm = body_radius
        int i = seg;
        if (i + 1 >= NUM_3D_NODES) continue;
        double r = 0.5 * (nodes_[i].radius + nodes_[i+1].radius);
        double moment = dv_diff * muscle_fmax_ * ap_grad;

        // Apply as perpendicular force in dorsal direction
        // Force couple: +F on node i, -F on node i+1 (creates bending)
        Vector3d f_perp = nodes_[i].dorsal * (moment / segment_length_);
        forces[i]   += f_perp;
        forces[i+1] -= f_perp;
    }
}

// ================================================================
// Update material frames from current node positions
// Tangent from finite difference, dorsal from parallel transport
// ================================================================
void BodyModel3D::update_frames() {
    // Tangent vectors
    for (int i = 0; i < NUM_3D_NODES; ++i) {
        if (i == 0) {
            nodes_[i].tangent = (nodes_[1].pos - nodes_[0].pos).normalized();
        } else if (i == NUM_3D_NODES - 1) {
            nodes_[i].tangent = (nodes_[i].pos - nodes_[i-1].pos).normalized();
        } else {
            nodes_[i].tangent = (nodes_[i+1].pos - nodes_[i-1].pos).normalized();
        }
    }

    // Parallel transport dorsal vector along body
    // Keep dorsal perpendicular to tangent via Gram-Schmidt
    for (int i = 0; i < NUM_3D_NODES; ++i) {
        Vector3d t = nodes_[i].tangent;
        Vector3d d = nodes_[i].dorsal;

        // Remove tangent component from dorsal
        d = d - t * d.dot(t);
        double dn = d.norm();
        if (dn > 1e-9) {
            nodes_[i].dorsal = d / dn;
        } else {
            // Fallback: choose arbitrary perpendicular
            if (std::abs(t.x) < 0.9)
                nodes_[i].dorsal = t.cross(Vector3d{1,0,0}).normalized();
            else
                nodes_[i].dorsal = t.cross(Vector3d{0,1,0}).normalized();
        }

        // Lateral = tangent × dorsal
        nodes_[i].lateral = t.cross(nodes_[i].dorsal).normalized();
    }
}

// ================================================================
// Overdamped integration (low Reynolds number regime)
// At Re ~ 0.01, inertia is negligible: velocity = Force / drag_coeff
// This is the correct physics for C. elegans locomotion
// REF: Boyle 2012 — overdamped dynamics, Purcell 1977 "Life at low Re"
// ================================================================
void BodyModel3D::integrate(const std::array<Vector3d, NUM_3D_NODES>& forces, double dt) {
    double dt_s = dt * 0.001;  // ms → seconds

    for (int i = 0; i < NUM_3D_NODES; ++i) {
        Vector3d t = nodes_[i].tangent;
        double area = 2.0 * PI * nodes_[i].radius * segment_length_;

        // Effective drag coefficient per node (with minimum to prevent div-by-zero at ends)
        double gamma_t = std::max(drag_tangent_ * area, 1e-6);  // tangential
        double gamma_n = std::max(drag_normal_ * area, 1e-6);   // normal

        // Decompose force into tangential and normal
        double f_t = forces[i].dot(t);
        Vector3d f_tang = t * f_t;
        Vector3d f_norm = forces[i] - f_tang;

        // Velocity = force / drag (overdamped, no inertia)
        Vector3d vel_tang = f_tang / gamma_t;
        Vector3d vel_norm = f_norm / gamma_n;

        nodes_[i].vel = vel_tang + vel_norm;

        // Velocity clamp to prevent instability (max 5 mm/s)
        double v_mag = nodes_[i].vel.norm();
        if (v_mag > 5.0 || std::isnan(v_mag)) {
            if (std::isnan(v_mag)) {
                nodes_[i].vel = {0, 0, 0};
            } else {
                nodes_[i].vel = nodes_[i].vel * (5.0 / v_mag);
            }
        }

        // Position update
        nodes_[i].pos += nodes_[i].vel * dt_s;
    }
}

// ================================================================
// Enforce inextensibility: constrain segment lengths to segment_length_
// Using position-based dynamics (PBD) constraint projection
// REF: Müller et al. 2007 "Position Based Dynamics"
// ================================================================
void BodyModel3D::enforce_inextensibility() {
    for (int iter = 0; iter < 5; ++iter) {
        for (int i = 0; i < NUM_3D_NODES - 1; ++i) {
            Vector3d d = nodes_[i+1].pos - nodes_[i].pos;
            double len = d.norm();
            if (len < 1e-12) continue;

            double error = len - segment_length_;
            Vector3d correction = d * (error / (2.0 * len));

            nodes_[i].pos   += correction;
            nodes_[i+1].pos -= correction;
        }
    }
}

// ================================================================
// Main physics update — CURVATURE-DRIVEN formulation
// Same approach as 2D BodyModel (inherently stable, no stiff ODE)
//
// 1. Compute target DV/LR curvature from muscle activations
// 2. Evolve curvature via semi-implicit Euler (unconditionally stable)
// 3. Reconstruct 3D positions geometrically from curvature profile
// 4. Compute speed from head displacement
//
// REF: Padmanabhan 2012 — curvature wave representation
//      Boyle 2012 — cuticle stiffness + damping
// ================================================================
void BodyModel3D::update_physics(double dt) {
    // Smooth locomotion drives (tau=100ms)
    double tau = 100.0;
    double alpha = dt / (dt + tau);
    smooth_fwd_ += alpha * (forward_drive_ - smooth_fwd_);
    smooth_rev_ += alpha * (reverse_drive_ - smooth_rev_);

    // --- 1. Compute target curvature from muscle activations ---
    for (int i = 0; i < NUM_3D_NODES; ++i) {
        double dorsal_a = 0.0, ventral_a = 0.0;
        int d_count = 0, v_count = 0;
        double left_a = 0.0, right_a = 0.0;
        int l_count = 0, r_count = 0;

        for (const auto& m : muscles_) {
            if (m.seg_start / 2 != i / 2) continue;
            switch (m.quadrant) {
                case MuscleQuadrant::DORSAL_LEFT:  dorsal_a += m.activation; d_count++; left_a += m.activation; l_count++; break;
                case MuscleQuadrant::DORSAL_RIGHT: dorsal_a += m.activation; d_count++; right_a += m.activation; r_count++; break;
                case MuscleQuadrant::VENTRAL_LEFT: ventral_a += m.activation; v_count++; left_a += m.activation; l_count++; break;
                case MuscleQuadrant::VENTRAL_RIGHT:ventral_a += m.activation; v_count++; right_a += m.activation; r_count++; break;
            }
        }
        if (d_count > 0) dorsal_a /= d_count;
        if (v_count > 0) ventral_a /= v_count;
        if (l_count > 0) left_a /= l_count;
        if (r_count > 0) right_a /= r_count;

        // Target curvature: muscle_gain * (dorsal - ventral)
        double muscle_gain = 3.0;  // max curvature per unit activation (1/mm)
        double target_dv = muscle_gain * (dorsal_a - ventral_a);
        double target_lr = muscle_gain * (left_a - right_a) * 0.3;  // lateral bending weaker

        // Elastic coupling (diffusion between adjacent segments)
        double curv_diff = 0.5;
        double dv_left  = (i > 0) ? nodes_[i-1].curvature_dv : nodes_[i].curvature_dv;
        double dv_right = (i < NUM_3D_NODES-1) ? nodes_[i+1].curvature_dv : nodes_[i].curvature_dv;
        double diffusion_dv = curv_diff * (dv_left - 2.0 * nodes_[i].curvature_dv + dv_right);

        double lr_left  = (i > 0) ? nodes_[i-1].curvature_lr : nodes_[i].curvature_lr;
        double lr_right = (i < NUM_3D_NODES-1) ? nodes_[i+1].curvature_lr : nodes_[i].curvature_lr;
        double diffusion_lr = curv_diff * (lr_left - 2.0 * nodes_[i].curvature_lr + lr_right);

        // Semi-implicit Euler (unconditionally stable)
        double stiffness = 10.0;
        double damping = 0.5;
        double denom = 1.0 + (stiffness + damping) * dt;
        nodes_[i].curvature_dv = (nodes_[i].curvature_dv + dt * (stiffness * target_dv + diffusion_dv)) / denom;
        nodes_[i].curvature_lr = (nodes_[i].curvature_lr + dt * (stiffness * target_lr + diffusion_lr)) / denom;

        // Clamp curvature (normal ~3/mm, omega ~15/mm)
        double max_curv = (omega_mode_ && i < 4) ? 15.0 : 5.0;
        nodes_[i].curvature_dv = std::clamp(nodes_[i].curvature_dv, -max_curv, max_curv);
        nodes_[i].curvature_lr = std::clamp(nodes_[i].curvature_lr, -max_curv * 0.3, max_curv * 0.3);
    }

    // --- 2. Forward speed from muscle activity ---
    double muscle_work = 0.0;
    for (const auto& m : muscles_) {
        muscle_work += m.activation;
    }
    muscle_work /= std::max(1, (int)muscles_.size());

    double v_max = 0.6 * speed_scale_;
    double forward_speed = v_max * muscle_work;

    // Direction from command neuron balance
    double direction = 1.0;
    if (smooth_rev_ > smooth_fwd_ + 0.1) {
        direction = -1.0;
        forward_speed *= 0.6;
    }

    // --- 3. Update head angle from curvature ---
    double head_curv_dv = nodes_[0].curvature_dv + curvature_bias_;
    double head_curv_lr = nodes_[0].curvature_lr;

    // Head angle change (in XY plane, from DV curvature — worm lies on side)
    double dtheta = forward_speed * direction * head_curv_dv * dt;
    double max_dtheta = (omega_mode_ ? 5.24 : 0.87) * dt;
    dtheta = std::clamp(dtheta, -max_dtheta, max_dtheta);

    // Heading change in XZ plane (from LR curvature)
    double dphi = forward_speed * direction * head_curv_lr * dt;
    dphi = std::clamp(dphi, -max_dtheta * 0.3, max_dtheta * 0.3);

    // Rotate head tangent
    double heading_xy = std::atan2(nodes_[0].tangent.y, nodes_[0].tangent.x);
    double heading_z = std::asin(std::clamp(nodes_[0].tangent.z, -1.0, 1.0));
    heading_xy += dtheta;
    heading_z += dphi;
    heading_z = std::clamp(heading_z, -0.3, 0.3);  // limit Z angle

    nodes_[0].tangent = {std::cos(heading_xy) * std::cos(heading_z),
                         std::sin(heading_xy) * std::cos(heading_z),
                         std::sin(heading_z)};

    // --- 4. Move head ---
    Vector3d prev_head = nodes_[0].pos;
    nodes_[0].pos += nodes_[0].tangent * (forward_speed * direction * dt);

    // Ground constraint
    if (nodes_[0].pos.z < nodes_[0].radius) {
        nodes_[0].pos.z = nodes_[0].radius;
    }

    // --- 5. Reconstruct body from curvature profile ---
    // theta_i = theta_{i-1} - kappa_dv_i * ds (Padmanabhan 2012)
    for (int i = 1; i < NUM_3D_NODES; ++i) {
        // Angle from DV curvature (primary bending plane)
        double prev_xy = std::atan2(nodes_[i-1].tangent.y, nodes_[i-1].tangent.x);
        double prev_z = std::asin(std::clamp(nodes_[i-1].tangent.z, -1.0, 1.0));

        double new_xy = prev_xy - nodes_[i].curvature_dv * segment_length_;
        double new_z = prev_z - nodes_[i].curvature_lr * segment_length_;
        new_z = std::clamp(new_z, -0.3, 0.3);

        nodes_[i].tangent = {std::cos(new_xy) * std::cos(new_z),
                             std::sin(new_xy) * std::cos(new_z),
                             std::sin(new_z)};
        nodes_[i].pos = nodes_[i-1].pos - nodes_[i].tangent * segment_length_;

        // Ground constraint
        if (nodes_[i].pos.z < nodes_[i].radius) {
            nodes_[i].pos.z = nodes_[i].radius;
        }
    }

    // --- 6. Update frames and surface ---
    update_frames();
    update_surface_points();

    // --- 7. Compute speed ---
    speed_ = (nodes_[0].pos - prev_head).norm() / dt;
}

// ================================================================
// Public interface implementations
// ================================================================

void BodyModel3D::set_muscle_activation(int muscle_id, double activation) {
    if (muscle_id >= 0 && muscle_id < static_cast<int>(muscles_.size())) {
        muscles_[muscle_id].activation = std::clamp(activation, 0.0, 1.0);
    }
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
        if (dorsal && (m.quadrant == MuscleQuadrant::DORSAL_LEFT ||
                       m.quadrant == MuscleQuadrant::DORSAL_RIGHT)) {
            m.activation = a;
        }
        if (!dorsal && (m.quadrant == MuscleQuadrant::VENTRAL_LEFT ||
                        m.quadrant == MuscleQuadrant::VENTRAL_RIGHT)) {
            m.activation = a;
        }
    }
}

void BodyModel3D::reset_activations() {
    for (auto& m : muscles_) {
        m.activation = 0.0;
    }
}

Vector3d BodyModel3D::get_head_position_3d() const { return nodes_[0].pos; }
Vector2d BodyModel3D::get_head_position() const { return nodes_[0].pos.xy(); }
double BodyModel3D::get_head_angle() const {
    return std::atan2(nodes_[0].tangent.y, nodes_[0].tangent.x);
}
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
    // Rotate the first few nodes around z-axis
    double c = std::cos(dtheta), s = std::sin(dtheta);
    for (int i = 0; i < 3 && i < NUM_3D_NODES; ++i) {
        double w = 1.0 - i * 0.3;
        double dx = nodes_[i].tangent.x;
        double dy = nodes_[i].tangent.y;
        nodes_[i].tangent.x = dx * (1.0 + w*(c-1.0)) - dy * w * s;
        nodes_[i].tangent.y = dx * w * s + dy * (1.0 + w*(c-1.0));
    }
}

void BodyModel3D::set_position(double x, double y) {
    Vector3d offset = {x - nodes_[0].pos.x, y - nodes_[0].pos.y, 0};
    for (auto& n : nodes_) n.pos += offset;
    update_surface_points();
}

void BodyModel3D::set_heading(double angle) {
    // Rotate entire body to match new heading
    double current = get_head_angle();
    double dtheta = angle - current;
    double c = std::cos(dtheta), s = std::sin(dtheta);
    Vector3d pivot = nodes_[0].pos;
    for (auto& n : nodes_) {
        double dx = n.pos.x - pivot.x;
        double dy = n.pos.y - pivot.y;
        n.pos.x = pivot.x + dx * c - dy * s;
        n.pos.y = pivot.y + dx * s + dy * c;
        double tx = n.tangent.x, ty = n.tangent.y;
        n.tangent.x = tx * c - ty * s;
        n.tangent.y = tx * s + ty * c;
    }
    update_frames();
    update_surface_points();
}

void BodyModel3D::nudge_position(double dx, double dy) {
    for (auto& n : nodes_) {
        n.pos.x += dx;
        n.pos.y += dy;
    }
    update_surface_points();
}

} // namespace celegans
