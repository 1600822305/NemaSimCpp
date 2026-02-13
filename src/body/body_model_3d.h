#pragma once

// ================================================================
// Step 129: 3D Biomechanical Body Model for C. elegans
//
// Based on Boyle, Berri & Cohen 2012 (Frontiers Comput Neurosci)
// + Palyanov 2018 (Phil Trans B) — Sibernetic framework
//
// Physical model:
//   - 49 nodes along centerline (48 segments)
//   - Each node: 3D position + local material frame (tangent, dorsal, lateral)
//   - 95 body wall muscles in 4 quadrants (DL=24, DR=24, VL=24, VR=23)
//   - Prolate ellipsoid tapering for realistic body shape
//
// Physics:
//   - Passive cuticle: bending stiffness + torsional stiffness
//   - Internal hydrostatic pressure: diagonal springs preserving volume
//   - Hill-type muscle model: force depends on activation, length, velocity
//   - Anisotropic drag: tangential << normal (Resistive Force Theory)
//   - Ground contact forces for crawling locomotion
//
// REF: Boyle & Cohen 2008 Biosystems, Boyle, Berri & Cohen 2012,
//      Palyanov et al. 2018 Phil Trans B, Niebur & Erdos 1991,
//      Park et al. 2008 PNAS (body mechanics measurements)
// ================================================================

#include "core/types.h"
#include <array>
#include <vector>
#include <cmath>

namespace celegans {

// Number of nodes along the centerline (segments + 1)
constexpr int NUM_3D_NODES = NUM_BODY_SEGMENTS + 1;  // 49

// Muscle quadrants
enum class MuscleQuadrant { DORSAL_LEFT, DORSAL_RIGHT, VENTRAL_LEFT, VENTRAL_RIGHT };

// A single body wall muscle cell
struct MuscleCell3D {
    int seg_start;            // segment index (0-based)
    MuscleQuadrant quadrant;
    double activation = 0.0;  // neural input [0, 1]
    double length = 0.0;      // current muscle length (mm)
    double prev_length = 0.0; // previous length (for velocity)
    double rest_length = 0.0; // resting length (mm)
    double force = 0.0;       // current contractile force (nN)
};

// Node along the worm centerline
struct Node3D {
    Vector3d pos;             // 3D position (mm)
    Vector3d vel;             // velocity (mm/s)
    Vector3d tangent;         // unit tangent (along body axis)
    Vector3d dorsal;          // unit dorsal direction
    Vector3d lateral;         // unit lateral direction (tangent × dorsal)

    // Surface points (computed from centerline + radius + frame)
    Vector3d pos_dl;          // dorsal-left surface point
    Vector3d pos_dr;          // dorsal-right surface point
    Vector3d pos_vl;          // ventral-left surface point
    Vector3d pos_vr;          // ventral-right surface point

    double radius;            // local body radius (tapering, mm)
    double curvature_dv = 0.0; // dorso-ventral curvature (1/mm)
    double curvature_lr = 0.0; // lateral curvature (1/mm)
};

class BodyModel3D {
public:
    BodyModel3D();

    // Initialize body at given position and heading
    void initialize(Vector3d head_pos, double heading_xy, double heading_z = 0.0);

    // Physics step
    void update_physics(double dt);

    // === Muscle activation interface (from motor controller) ===
    // Set individual muscle cell activation (0-94)
    void set_muscle_activation(int muscle_id, double activation);
    // Set by segment + quadrant
    void set_segment_activation(int segment, MuscleQuadrant quad, double activation);
    // Set dorsal/ventral (maps to DL+DR / VL+VR) — compatible with 2D interface
    void set_dorsal_ventral_activation(int segment, bool dorsal, double activation);
    // Reset all activations
    void reset_activations();

    // === Getters for sensory feedback ===
    Vector3d get_head_position_3d() const;
    Vector2d get_head_position() const;  // 2D projection (x,y) for compatibility
    double get_head_angle() const;
    Vector3d get_tail_position_3d() const;
    Vector2d get_tail_position() const;
    double get_local_curvature(int segment) const;  // dorso-ventral (primary)
    double get_lateral_curvature(int segment) const;
    double get_local_stretch(int segment) const;
    Vector2d get_segment_position(int segment) const;
    Vector3d get_segment_position_3d(int segment) const;
    double get_speed() const { return speed_; }
    double get_body_length() const { return body_length_; }

    // === Locomotion state ===
    void set_locomotion_state(double forward_drive, double reverse_drive) {
        forward_drive_ = forward_drive;
        reverse_drive_ = reverse_drive;
    }
    void set_speed_scale(double s) { speed_scale_ = s; }
    void set_curvature_bias(double b) { curvature_bias_ = b; }
    double get_curvature_bias() const { return curvature_bias_; }
    void set_omega_mode(bool on) { omega_mode_ = on; }
    void perturb_heading(double dtheta);

    // === Multi-worm support ===
    void set_position(double x, double y);
    void set_heading(double angle);
    void nudge_position(double dx, double dy);

    // === Environment mode ===
    enum class LocomotionMode { CRAWLING, SWIMMING };
    void set_locomotion_mode(LocomotionMode mode) { loco_mode_ = mode; }

    // === Access to internal state ===
    const std::array<Node3D, NUM_3D_NODES>& nodes() const { return nodes_; }
    const std::vector<MuscleCell3D>& muscles() const { return muscles_; }
    int num_muscles() const { return static_cast<int>(muscles_.size()); }

private:
    // === Body geometry ===
    double body_length_ = 1.0;        // mm (adult hermaphrodite)
    double body_radius_ = 0.04;       // mm (40 μm max radius)
    double segment_length_ = 0.0;     // mm per segment (L / NUM_BODY_SEGMENTS)

    // === Nodes and muscles ===
    std::array<Node3D, NUM_3D_NODES> nodes_;
    std::vector<MuscleCell3D> muscles_;  // 95 muscles

    // === Passive body parameters (Boyle 2012 Table 1) ===
    // Per-segment spring constants — chosen to match flaccid body relaxation
    // in water (fast) and agar (slow) — Sauvage 2007
    double kappa_lateral_ = 100000.0;  // lateral spring constant (nN/mm)
    double beta_lateral_ = 5000.0;     // lateral damping (nN·s/mm)
    double kappa_diagonal_ = 200000.0; // diagonal spring (internal pressure, nN/mm)
    double beta_diagonal_ = 5000.0;    // diagonal damping (nN·s/mm)

    // === Muscle parameters (Hill-type, Boyle 2012 Table 1) ===
    double kappa_muscle_ = 800.0;     // muscle spring constant (nN/mm)
    double beta_muscle_ = 200.0;      // muscle damping (nN·s/mm)
    double muscle_fmax_ = 5.0;        // peak bending moment per segment (nN·mm)
    double muscle_lmin_ratio_ = 0.6;  // minimum length ratio (prevents over-contraction)

    // === Drag parameters (Resistive Force Theory, Boyle 2012 Section 2.1) ===
    // Boyle 2012: C_t = 3.2e-3 kg/s (whole worm agar), C_n = 128e-3 kg/s
    // Per unit area: c_t = C_t / (L × 2πR_avg) ≈ 12700 nN·s/mm³
    // c_n = C_n / (L × 2πR_avg) ≈ 509000 nN·s/mm³
    // REF: Niebur & Erdos 1991, Wallace 1969, Berri 2009
    double drag_tangent_ = 12700.0;    // tangential drag (nN·s/mm³) — agar
    double drag_normal_ = 509000.0;    // normal drag (nN·s/mm³) — agar (K=40)
    LocomotionMode loco_mode_ = LocomotionMode::CRAWLING;

    // === Cuticle bending ===
    // Park 2008 PNAS: whole-body stiffness ~3.77 nN/μm = 3770 nN/mm
    // Bending stiffness EI ~ 10 nN·mm² (from cuticle elasticity measurements)
    double bend_stiffness_ = 500.0;   // nN·mm² (effective, tuned for ~5/mm peak curvature)
    double twist_stiffness_ = 100.0;  // torsional stiffness (nN·mm²)

    // === Locomotion state ===
    double speed_ = 0.0;
    double speed_scale_ = 1.0;
    double curvature_bias_ = 0.0;
    bool omega_mode_ = false;
    double forward_drive_ = 0.5;
    double reverse_drive_ = 0.0;
    double smooth_fwd_ = 0.5;
    double smooth_rev_ = 0.0;

    // === Internal methods ===
    void init_muscles();
    void compute_radii();           // prolate ellipsoid tapering
    void update_surface_points();   // compute DL/DR/VL/VR from centerline+frame
    void compute_muscle_forces(double dt);
    void compute_passive_forces(std::array<Vector3d, NUM_3D_NODES>& forces);
    void compute_drag_forces(std::array<Vector3d, NUM_3D_NODES>& forces, double dt);
    void apply_muscle_forces(std::array<Vector3d, NUM_3D_NODES>& forces);
    void update_frames();           // recompute tangent/dorsal/lateral from positions
    void integrate(const std::array<Vector3d, NUM_3D_NODES>& forces, double dt);
    void enforce_inextensibility(); // preserve segment lengths (rigid rod constraint)
};

} // namespace celegans
