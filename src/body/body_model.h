#pragma once

#include "core/types.h"
#include <algorithm>
#include <array>
#include <vector>
#include <random>

namespace celegans {

// Step 135: Force-based body model (Boyle 2012 explicit quasi-static)
constexpr int NBAR = NUM_BODY_SEGMENTS + 1;  // 49 rigid rods (Boyle: NBAR=NSEG+1)

struct BodySegment {
    Vector2d position;
    double angle = 0.0;           // orientation angle (rad)
    double curvature = 0.0;       // local curvature (computed from adjacent angles)
    double prev_curvature = 0.0;  // previous frame curvature
    double dorsal_activation = 0.0;  // dorsal muscle activation [0,1]
    double ventral_activation = 0.0; // ventral muscle activation [0,1]
    double V_muscle_dorsal = 0.0;    // muscle voltage dorsal (LPF of activation)
    double V_muscle_ventral = 0.0;   // muscle voltage ventral (LPF of activation)
};

class BodyModel {
public:
    BodyModel();

    void initialize(Vector2d head_pos, double heading);

    void update_physics(double dt);

    // Set muscle activations from motor controller
    void set_muscle_activation(int segment, bool dorsal, double activation);

    // Getters for sensory feedback
    Vector2d get_head_position() const;
    double get_head_angle() const;
    Vector2d get_tail_position() const;
    double get_local_curvature(int segment) const;
    double get_local_stretch(int segment) const;
    Vector2d get_segment_position(int segment) const;
    double get_speed() const { return speed_; }
    void set_curvature_bias(double b) { curvature_bias_ = b; }
    double get_curvature_bias() const { return curvature_bias_; }
    void set_omega_mode(bool on) { omega_mode_ = on; }
    // Step 41: Post-pirouette heading perturbation (Pierce-Shimomura 1999)
    void perturb_heading(double dtheta) {
        segments_[0].angle += dtheta;
    }
    double get_body_length() const { return body_length_; }

    // Step 128: Multi-worm simulation support
    void set_position(double x, double y) {
        segments_[0].position = {x, y};
        prev_head_pos_ = {x, y};
    }
    void set_heading(double angle) {
        segments_[0].angle = angle;
    }
    void nudge_position(double dx, double dy) {
        for (auto& seg : segments_) {
            seg.position.x += dx;
            seg.position.y += dy;
        }
        prev_head_pos_.x += dx;
        prev_head_pos_.y += dy;
    }

    // Forward/reverse state from command neuron balance
    // forward_drive: AVB release rate, reverse_drive: AVA release rate
    void set_locomotion_state(double forward_drive, double reverse_drive) {
        forward_drive_ = forward_drive;
        reverse_drive_ = reverse_drive;
    }

    const std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() const { return segments_; }
    std::array<BodySegment, NUM_BODY_SEGMENTS>& segments() { return segments_; }

    // Direct set (for inhibitory reduction, bypasses max)
    void set_muscle_activation_direct(int segment, bool dorsal, double activation);

    // Reset all muscle activations
    void reset_activations();

private:
    std::array<BodySegment, NUM_BODY_SEGMENTS> segments_;
    double body_length_ = 1.0;       // mm (external)
    double segment_length_ = 0.0;    // mm per segment (external)

    // ============================================================
    // Step 135: Semi-implicit curvature ODE
    //
    // Physics: dκ/dt = (τ_muscle - k_bend × κ - D_bend × dκ/dt) / C_rot
    // Semi-implicit: κ_new = (κ_old + dt × τ_muscle/C_rot) / (1 + dt × k_bend/C_rot)
    // → Unconditionally stable for ANY parameter values.
    //
    // All effective parameters derived from Boyle 2012 worm.cc:
    //   k_PE=0.02 N/m, k_AE=0.4 N/m, k_DE=7.0 N/m
    //   R_mid=40μm, L_seg=20.8μm
    //
    // Per-segment arrays precomputed in constructor.
    // ============================================================
    static constexpr double T_muscle_ = 0.1;  // muscle LPF time constant (s) [worm.cc:72]
    double nmj_weight_[NUM_BODY_SEGMENTS];
    // Per-segment precomputed (SI units internally, see constructor)
    double tau_coeff_[NUM_BODY_SEGMENTS];  // muscle torque coefficient (1/s per unit ΔV)
    double k_ratio_[NUM_BODY_SEGMENTS];    // k_bend / C_rot (1/s)
    double d_ratio_[NUM_BODY_SEGMENTS];    // D_bend / C_rot (dimensionless)
    // Step 134-135: RFT drag coefficients — medium-dependent (Boyle 2012)
    // Absolute values from Boyle 2012 worm.cc lines 75-78 (per rod, SI units):
    //   Water: C_T=3.3e-6/(2*NBAR), C_N=5.2e-6/(2*NBAR)
    //   Agar:  C_T=3.2e-3/(2*NBAR), C_N=128e-3/(2*NBAR)
    // Now using ABSOLUTE values (needed for force-based velocity computation)
    double medium_ = 1.0;              // 0.0=water, 1.0=agar (Boyle 2012 MEDIUM)
    double CL_[NBAR];                  // tangential drag per rod (SI: kg/s)
    double CN_[NBAR];                  // normal drag per rod (SI: kg/s)

    void compute_drag_coefficients() {
        constexpr double CL_water = 3.3e-6  / (2.0 * NBAR);
        constexpr double CN_water = 5.2e-6  / (2.0 * NBAR);
        constexpr double CL_agar  = 3.2e-3  / (2.0 * NBAR);
        constexpr double CN_agar  = 128.0e-3 / (2.0 * NBAR);
        for (int i = 0; i < NBAR; ++i) {
            CL_[i] = CL_water + (CL_agar - CL_water) * medium_;
            CN_[i] = CN_water + (CN_agar - CN_water) * medium_;
        }
    }
    double speed_ = 0.0;             // current locomotion speed (mm/s)
    double smooth_speed_ = 0.0;        // Step 137: LPF of RFT speed (body inertia, τ=50ms)
public:
    void set_speed_scale(double s) { speed_scale_ = s; }
    // Set medium: 0.0=water (swimming), 1.0=agar (crawling)
    // REF: Boyle 2012 — continuous swim-crawl transition
    void set_medium(double m) { medium_ = std::clamp(m, 0.0, 1.0); compute_drag_coefficients(); }
    double get_medium() const { return medium_; }
private:
    double speed_scale_ = 1.0;       // runtime speed multiplier
    double curvature_bias_ = 0.0;    // direct curvature bias from weathervane (1/mm)
    bool omega_mode_ = false;        // omega turn: raise max_dtheta for deep bend
    Vector2d prev_head_pos_;
    double forward_drive_ = 0.5;     // AVB release rate (instantaneous)
    double reverse_drive_ = 0.0;     // AVA release rate (instantaneous)
    double smooth_fwd_ = 0.5;        // smoothed forward drive (100ms tau)
    double smooth_rev_ = 0.0;        // smoothed reverse drive (100ms tau)
    double mean_rev_ = 0.0;          // running mean of AVA (2s tau, for adaptive threshold)
    bool was_reversing_ = false;     // for detecting reversal transitions
    std::mt19937 rng_{42};           // RNG for pirouette random reorientation
    std::uniform_real_distribution<double> angle_dist_{-3.14159, 3.14159}; // ±π

    void update_muscles(double dt);
    void compute_forces_and_integrate(double dt);
};

} // namespace celegans
