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
    direction_ = 1.0;
    speed_ = 0.0;
}

void BodyModel::compute_curvatures(double dt) {
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        auto& seg = segments_[i];
        // Curvature driven by muscle force differential
        // force_diff > 0 → dorsal stronger → positive curvature (dorsal bend)
        // force_diff < 0 → ventral stronger → negative curvature (ventral bend)
        // NOTE: neuromod_gain NOT applied here. In RFT, speed ∝ curvature²,
        // so linear neuromod on curvature → quadratic speed scaling (too aggressive).
        // Speed modulation comes from wave frequency (neural oscillation rate).
        double force_diff = muscles_.get_force_differential(i);
        double target_curvature = curvature_gain_ * force_diff;

        // Step 29: Passive elastic coupling between adjacent segments
        // REF: Boyle 2012 — body continuity allows curvature to spread
        double curv_left  = (i > 0) ? segments_[i - 1].curvature : seg.curvature;
        double curv_right = (i < NUM_BODY_SEGMENTS - 1) ? segments_[i + 1].curvature : seg.curvature;
        double diffusion = curvature_diffusion_ * (curv_left - 2.0 * seg.curvature + curv_right);

        // Semi-implicit Euler (unconditionally stable for stiffness/damping):
        // dcurv/dt = stiffness*(target - curv) - damping*curv + diffusion
        //
        // Medium viscosity effect on curvature dynamics:
        // In water: less external friction → body shape responds faster to muscle
        // forces → effective stiffness increases (body follows target more quickly).
        // This is the primary mechanism for crawling→swimming gait transition.
        // REF: Fang-Yen 2010 — curvature response scales with medium load
        //      Boyle 2012 — external load modulates body wave frequency
        double visc_factor = 1.0 + 2.0 * (1.0 - medium_viscosity_);  // 1.0 agar, 2.98 water
        double effective_stiffness = stiffness_ * visc_factor;
        double effective_damping = damping_ * (0.3 + 0.7 * medium_viscosity_);
        double effective_diffusion = diffusion * visc_factor;  // wave propagates faster in water
        double denom = 1.0 + (effective_stiffness + effective_damping) * dt;
        seg.curvature = (seg.curvature + dt * (effective_stiffness * target_curvature + effective_diffusion)) / denom;

        // Clamp curvature to physical limit: ~25/mm (head touching body in omega)
        // REF: Gray 2005 PNAS — omega turn curvature 20-25/mm
        double max_curv = 25.0;
        if (seg.curvature > max_curv) seg.curvature = max_curv;
        if (seg.curvature < -max_curv) seg.curvature = -max_curv;
    }
}

void BodyModel::update_positions(double dt) {
    // ===================================================================
    // Resistive Force Theory (RFT) — distributed locomotion mechanics
    //
    // At low Reynolds number (Re ~ 0.01), inertia is negligible.
    // The force-free condition (Σ F_drag = 0, Σ τ_drag = 0) determines
    // the rigid body motion (Vx, Vy, Ω) of the worm.
    //
    // Anisotropic drag (C_N > C_T) converts body undulation into net
    // thrust. Forward/reverse direction emerges naturally from B/A-class
    // motor neuron wave propagation direction. No direction flag needed.
    //
    // REF: Gray & Hancock 1955 — slender body RFT
    //      Boyle et al. 2012 — C. elegans neuromechanical model
    //      Fang-Yen et al. 2010 — locomotion on agar (C_N/C_T ≈ 1.5)
    // ===================================================================

    // --- 1. Save old positions (before shape change) ---
    std::array<Vector2d, NUM_BODY_SEGMENTS> old_pos;
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        old_pos[i] = segments_[i].position;
    }

    // --- 2. Reconstruct body shape with NEW curvatures (head fixed) ---
    // This captures internal deformation from curvature changes.
    // Head position and angle stay at old values (reference frame).
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].angle = segments_[i - 1].angle - segments_[i].curvature * segment_length_;
        Vector2d dir = Vector2d::from_angle(segments_[i].angle);
        segments_[i].position = segments_[i - 1].position - dir * segment_length_;
    }

    // --- 3. Compute shape change velocities ---
    // v_shape[i] = velocity of segment i due to curvature changes alone
    // (with head pinned in place — rigid body motion is solved separately)
    std::array<Vector2d, NUM_BODY_SEGMENTS> v_shape;
    v_shape[0] = {0.0, 0.0};  // head is reference point
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        v_shape[i] = (segments_[i].position - old_pos[i]) * (1.0 / dt);
    }

    // --- 4. Build 3×3 linear system for rigid body motion ---
    // Unknowns: Vx, Vy (head translation), Ω (body angular velocity)
    //
    // Total velocity of segment i:
    //   v_i = (Vx, Vy) + Ω × d_i + v_shape_i
    //   where d_i = segments_[i].position - head_position
    //         Ω × d = (-Ω*d.y, Ω*d.x)
    //
    // RFT drag per segment (force per unit length × segment_length):
    //   f_i = -ds × [C_T × (v_i·t_i) × t_i + C_N × (v_i·n_i) × n_i]
    //   where t_i = tangent, n_i = normal (90° CCW from tangent)
    //
    // Force-free:  Σ f_i = 0        (2 equations)
    // Torque-free: Σ (d_i × f_i) = 0 (1 equation)
    //
    // This is linear in (Vx, Vy, Ω) → standard 3×3 system.

    double A[3][3] = {};
    double b[3] = {};

    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        Vector2d t = Vector2d::from_angle(segments_[i].angle);  // tangent
        Vector2d n = {-t.y, t.x};                                // normal (CCW)
        Vector2d d = segments_[i].position - segments_[0].position; // relative to head

        // Cross products: d × t and d × n (z-component of 2D cross product)
        double cdt = d.cross(t);  // d.x*t.y - d.y*t.x
        double cdn = d.cross(n);  // d.x*n.y - d.y*n.x

        // Shape velocity projections
        double vs_t = v_shape[i].dot(t);  // tangential component
        double vs_n = v_shape[i].dot(n);  // normal component

        double ds = segment_length_;
        double CT = drag_tangential_;
        double CN = drag_normal_;

        // Row 0: Force balance (x-component)
        //   Σ [CT*vt*tx + CN*vn*nx] = 0
        A[0][0] += ds * (CT * t.x * t.x + CN * n.x * n.x);
        A[0][1] += ds * (CT * t.y * t.x + CN * n.y * n.x);
        A[0][2] += ds * (CT * cdt * t.x + CN * cdn * n.x);
        b[0]    -= ds * (CT * vs_t * t.x + CN * vs_n * n.x);

        // Row 1: Force balance (y-component)
        A[1][0] += ds * (CT * t.x * t.y + CN * n.x * n.y);
        A[1][1] += ds * (CT * t.y * t.y + CN * n.y * n.y);
        A[1][2] += ds * (CT * cdt * t.y + CN * cdn * n.y);
        b[1]    -= ds * (CT * vs_t * t.y + CN * vs_n * n.y);

        // Row 2: Torque balance about head
        //   Σ (d × f) = 0
        //   τ = d.x*fy - d.y*fx = -ds*[CT*vt*(d×t) + CN*vn*(d×n)]
        A[2][0] += ds * (CT * t.x * cdt + CN * n.x * cdn);
        A[2][1] += ds * (CT * t.y * cdt + CN * n.y * cdn);
        A[2][2] += ds * (CT * cdt * cdt + CN * cdn * cdn);
        b[2]    -= ds * (CT * vs_t * cdt + CN * vs_n * cdn);
    }

    // --- 5. Solve 3×3 system: A × [Vx, Vy, Ω]ᵀ = b ---
    double Vx = 0.0, Vy = 0.0, Omega = 0.0;
    solve_3x3(A, b, Vx, Vy, Omega);

    // Step 130: Add external angular velocity (weathervane heading correction)
    // Applied post-solve: does NOT change force/torque balance, does NOT feed
    // back to muscle dynamics or proprioception. Bypasses SMD oscillator entirely.
    Omega += external_angular_velocity_;

    // Proportional speed cap: scale ALL components equally to maintain
    // force balance self-consistency. At very high speeds, nonlinear drag
    // (not modeled) would limit motion. Cap at 0.8 mm/s translational.
    double v_mag = std::sqrt(Vx * Vx + Vy * Vy);
    if (v_mag > speed_cap_) {
        double scale = speed_cap_ / v_mag;
        Vx *= scale;
        Vy *= scale;
        Omega *= scale;
    }

    // --- 6. Apply rigid body motion to head ---
    segments_[0].position.x += Vx * dt;
    segments_[0].position.y += Vy * dt;
    segments_[0].angle += Omega * dt;

    // --- 7. Save prev curvatures (for next frame's shape change) ---
    for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].prev_curvature = segments_[i].curvature;
    }

    // --- 8. Reconstruct body from head (kinematic chain) ---
    // REF: Padmanabhan 2012 — θ_i = θ_{i-1} - κ_i × ds
    for (int i = 1; i < NUM_BODY_SEGMENTS; ++i) {
        segments_[i].angle = segments_[i - 1].angle - segments_[i].curvature * segment_length_;
        Vector2d dir = Vector2d::from_angle(segments_[i].angle);
        segments_[i].position = segments_[i - 1].position - dir * segment_length_;
    }

    // --- 9. Compute speed and direction (emergent from physics) ---
    Vector2d head_pos = segments_[0].position;
    Vector2d velocity = (head_pos - prev_head_pos_) * (1.0 / dt);
    speed_ = velocity.norm();

    // Direction: project velocity onto heading vector
    // +1 if moving in heading direction (forward), -1 if opposite (backward)
    Vector2d heading_vec = Vector2d::from_angle(segments_[0].angle);
    double v_along = velocity.dot(heading_vec);
    direction_ = (v_along >= 0.0) ? 1.0 : -1.0;

    prev_head_pos_ = head_pos;
}

// 3×3 Gaussian elimination with partial pivoting
bool BodyModel::solve_3x3(double A[3][3], double b[3],
                           double& x0, double& x1, double& x2) {
    // Forward elimination with partial pivoting
    for (int col = 0; col < 3; ++col) {
        // Find pivot
        int pivot = col;
        double max_val = std::abs(A[col][col]);
        for (int row = col + 1; row < 3; ++row) {
            if (std::abs(A[row][col]) > max_val) {
                max_val = std::abs(A[row][col]);
                pivot = row;
            }
        }
        if (max_val < 1e-15) {
            x0 = x1 = x2 = 0.0;
            return false;  // singular
        }
        // Swap rows
        if (pivot != col) {
            std::swap(A[col], A[pivot]);
            std::swap(b[col], b[pivot]);
        }
        // Eliminate
        for (int row = col + 1; row < 3; ++row) {
            double factor = A[row][col] / A[col][col];
            for (int j = col; j < 3; ++j) {
                A[row][j] -= factor * A[col][j];
            }
            b[row] -= factor * b[col];
        }
    }
    // Back substitution
    x2 = b[2] / A[2][2];
    x1 = (b[1] - A[1][2] * x2) / A[1][1];
    x0 = (b[0] - A[0][1] * x1 - A[0][2] * x2) / A[0][0];
    return true;
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

    // 3. Body physics: curvature from muscle forces, RFT locomotion
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

void BodyModel::set_medium_viscosity(double v) {
    medium_viscosity_ = v;

    // Muscle dynamics: less external load → faster contraction in water
    // Agar (v=1.0): tau=30ms. Water (v=0.01): tau≈9ms.
    // REF: Fang-Yen 2010 — muscle dynamics scale with external load
    double tau = 30.0 * (0.3 + 0.7 * v);
    muscles_.set_muscle_tau(tau);

    // Drag ratio: agar C_N/C_T≈1.5, water C_N/C_T≈2.0 (Lighthill 1976)
    drag_tangential_ = 3.4;
    drag_normal_ = (v < 0.5) ? drag_tangential_ * 2.0 : drag_tangential_ * 1.5;

    // Speed cap: swimming can achieve higher linear velocity
    speed_cap_ = 0.8 + 1.2 * (1.0 - v);  // 0.8 agar → 2.0 water
}

} // namespace celegans
