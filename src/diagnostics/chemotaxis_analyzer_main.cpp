// chemotaxis_analyzer_main.cpp — CI 归因分析 & 信号链瓶颈定位
// Step 119: 按机制分解趋化指数 (CI) 贡献，逐级追踪信号链，自动定位瓶颈。
// 支持多种子并行分析（最多 8 线程），自动汇总统计。
//
// Usage:
//   chemotaxis_analyzer --seeds 42,100,200,777,999 [--duration 60] [--verbose]
//   chemotaxis_analyzer --seed 42 [--duration 60] [--verbose]
#include "simulation/simulation_engine.h"
#include "neuron/multi_compartment.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <numeric>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <atomic>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32
#include <windows.h>
#undef ERROR
#undef WARNING
#undef min
#undef max
#endif

using namespace celegans;

// ================================================================
// Utility
// ================================================================
static double wrap_angle(double a) {
    while (a > M_PI) a -= 2 * M_PI;
    while (a < -M_PI) a += 2 * M_PI;
    return a;
}
static double deg(double rad) { return rad * 180.0 / M_PI; }

// ================================================================
// Per-sample snapshot (internal, not stored across seeds)
// ================================================================
struct ChemoSample {
    double time_s;
    Vector2d pos;
    double heading, speed, direction;
    bool is_reversing, is_omega;
    double food_angle, food_dist, concentration;
    double awc_release, ase_release;
    double ria_ca_diff;
    double smd_dl_V;
    double head_curv;
    double heading_rate;
    double ava_rel, avb_rel;
    double aib_release;   // AIB transmitter release (reversal attribution)
    double aser_release;  // ASER individual release (pathway correlation)
    double asel_release;  // ASEL individual release
    double aiy_release;   // AIY release (klinokinesis: AIY→AVB run promotion)
};

// ================================================================
// Per-seed aggregated result
// ================================================================
struct SeedResult {
    int seed;
    double ci;
    double ktx_corr;           // klinotaxis food_angle-heading_rate correlation
    double heading_bias;        // toward-away heading bias index
    double klinokinesis_index;  // reversal rate modulation
    double run_ratio;           // up/down gradient run length ratio
    double omega_toward_pct;    // % omega turns toward food
    int omega_count;
    double ria_ca_diff_abs;     // mean |RIA Ca²⁺ AC|
    double head_curv;           // mean head curvature (forward)
    double heading_rate_abs;    // mean |dθ/dt| (forward)
    double fwd_fraction;        // forward time fraction
    double mean_speed;          // mean speed
    double time_near_food;      // fraction of time within 5mm of food
    double curving_rate_mean;   // mean |dθ/dt| forward (°/s)
    double weathervane_slope;   // dθ/dt vs food_angle regression slope (°/s per rad, Iino 2009)
    bool converging;            // food angle trend Q4 < Q1
    int bottleneck_count;
    // Enhancement: reversal attribution
    double rev_sensory_pct;     // % reversals with AIB above forward P75 (sensory-driven)
    double rev_stochastic_pct;  // % reversals with AIB below forward P75 (noise-driven)
    double aib_fwd_mean;        // mean AIB release during forward movement
    double aib_fwd_p75;         // 75th percentile AIB release during forward movement
    double aib_at_rev_mean;     // mean AIB release at reversal onset
    // Enhancement: pathway correlation
    double aser_aib_corr;       // ASER↔AIB release rate correlation
    // Enhancement: klinokinesis pathway (AIY→AVB)
    double aiy_fwd_mean;        // mean AIY release during forward runs
    double aiy_toward_mean;     // AIY release when heading toward food
    double aiy_away_mean;       // AIY release when heading away from food
    double aiy_avb_corr;        // AIY↔AVB release correlation
    // Enhancement: CI phase decomposition
    double ci_fwd;              // CI contribution from forward runs
    double ci_rev;              // CI contribution from reversals
    double ci_omega;            // CI contribution from omega turns
};

// ================================================================
// Run single seed analysis — thread-safe, no shared state
// ================================================================
static SeedResult run_single_seed(int seed, double duration_s, double target_x, double target_y) {
    SeedResult res{};
    res.seed = seed;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    double dt_ms = sim.dt();
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt_ms);
    int sample_interval = static_cast<int>(50.0 / dt_ms);
    Vector2d food_pos = {target_x, target_y};

    auto& neurons = sim.neurons();
    int nn = static_cast<int>(neurons.size());
    auto fid = [&](const char* name) -> int {
        for (int i = 0; i < nn; ++i) if (neurons[i]->info().name == name) return i;
        return -1;
    };

    int awcl = fid("AWCL"), awcr = fid("AWCR");
    int asel = fid("ASEL"), aser = fid("ASER");
    int aval = fid("AVAL"), avar = fid("AVAR");
    int avbl = fid("AVBL"), avbr = fid("AVBR");
    int aibl = fid("AIBL"), aibr = fid("AIBR");
    int aiyl = fid("AIYL"), aiyr = fid("AIYR");
    int smddl = fid("SMDDL");
    int rial = fid("RIAL"), riar = fid("RIAR");

    MultiCompartmentNeuron* ria_mc[2] = {nullptr, nullptr};
    if (rial >= 0) ria_mc[0] = dynamic_cast<MultiCompartmentNeuron*>(neurons[rial].get());
    if (riar >= 0) ria_mc[1] = dynamic_cast<MultiCompartmentNeuron*>(neurons[riar].get());

    auto rel = [&](int id) -> double {
        return (id >= 0 && id < nn) ? neurons[id]->get_transmitter_release_rate() : 0.0;
    };

    // --- Collect samples ---
    std::vector<ChemoSample> samples;
    samples.reserve(total_steps / sample_interval + 1);

    for (int step = 0; step < total_steps; ++step) {
        sim.step();
        if (step % sample_interval != 0) continue;

        ChemoSample cs;
        cs.time_s = step * dt_ms / 1000.0;
        cs.pos = sim.body().get_head_position();
        cs.heading = sim.body().get_head_angle();
        cs.speed = sim.body().get_speed();
        cs.direction = sim.body().get_direction();
        cs.is_reversing = sim.is_reversing();
        cs.is_omega = sim.is_omega_turning();

        Vector2d to_food = {food_pos.x - cs.pos.x, food_pos.y - cs.pos.y};
        cs.food_dist = to_food.norm();
        cs.food_angle = wrap_angle(std::atan2(to_food.y, to_food.x) - cs.heading);
        cs.concentration = sim.environment().sample_chemical(cs.pos);
        cs.awc_release = (rel(awcl) + rel(awcr)) * 0.5;
        cs.ase_release = (rel(asel) + rel(aser)) * 0.5;

        cs.ria_ca_diff = 0;
        int rc = 0;
        for (int r = 0; r < 2; ++r) {
            if (ria_mc[r] && ria_mc[r]->num_compartments() >= 3) {
                cs.ria_ca_diff += ria_mc[r]->get_compartment_calcium(1) - ria_mc[r]->get_compartment_calcium(2);
                rc++;
            }
        }
        if (rc > 0) cs.ria_ca_diff /= rc;

        cs.smd_dl_V = (smddl >= 0) ? neurons[smddl]->get_membrane_potential() : -65;
        double hc = 0;
        for (int s = 0; s < 6; ++s) hc += std::abs(sim.body().get_local_curvature(s));
        cs.head_curv = hc / 6.0;
        cs.ava_rel = (rel(aval) + rel(avar)) * 0.5;
        cs.avb_rel = (rel(avbl) + rel(avbr)) * 0.5;
        cs.aib_release = (rel(aibl) + rel(aibr)) * 0.5;
        cs.aser_release = rel(aser);
        cs.asel_release = rel(asel);
        cs.aiy_release = (rel(aiyl) + rel(aiyr)) * 0.5;
        cs.heading_rate = 0;
        samples.push_back(cs);
    }

    // Post-hoc heading rate
    for (size_t i = 1; i < samples.size(); ++i) {
        double dh = wrap_angle(samples[i].heading - samples[i - 1].heading);
        double dt_s = samples[i].time_s - samples[i - 1].time_s;
        if (dt_s > 0) samples[i].heading_rate = deg(dh) / dt_s;
    }

    // --- Analysis ---
    // CI
    double start_dist = samples.front().food_dist;
    double end_dist = samples.back().food_dist;
    double total_path = 0;
    double speed_sum = 0; int speed_n = 0;
    int fwd_samples = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        double dx = samples[i].pos.x - samples[i - 1].pos.x;
        double dy = samples[i].pos.y - samples[i - 1].pos.y;
        total_path += std::sqrt(dx * dx + dy * dy);
        speed_sum += samples[i].speed; speed_n++;
        if (!samples[i].is_reversing && !samples[i].is_omega) fwd_samples++;
    }
    res.ci = (total_path > 0) ? (start_dist - end_dist) / total_path : 0;
    res.mean_speed = (speed_n > 0) ? speed_sum / speed_n : 0;
    res.fwd_fraction = (speed_n > 0) ? static_cast<double>(fwd_samples) / speed_n : 0;

    // Klinotaxis correlation
    double mx = 0, my = 0; int kn = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].is_reversing || samples[i].is_omega) continue;
        mx += samples[i].food_angle; my += samples[i].heading_rate; kn++;
    }
    if (kn > 0) { mx /= kn; my /= kn; }
    double sxy = 0, sx2 = 0, sy2 = 0;
    double toward = 0, away = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].is_reversing || samples[i].is_omega) continue;
        double fx = samples[i].food_angle - mx, fy = samples[i].heading_rate - my;
        sxy += fx * fy; sx2 += fx * fx; sy2 += fy * fy;
        double dh = wrap_angle(samples[i].heading - samples[i - 1].heading);
        bool tw = (samples[i].food_angle > 0 && dh > 0) || (samples[i].food_angle < 0 && dh < 0);
        if (tw) toward += std::abs(deg(dh)); else away += std::abs(deg(dh));
    }
    res.ktx_corr = (sx2 > 0 && sy2 > 0) ? sxy / std::sqrt(sx2 * sy2) : 0;
    res.heading_bias = (toward + away > 0) ? (toward - away) / (toward + away) : 0;

    // Klinokinesis
    int rev_up = 0, rev_down = 0, t_up = 0, t_down = 0;
    bool prev_rev = false;
    for (size_t i = 1; i < samples.size(); ++i) {
        bool up = std::abs(samples[i].food_angle) < M_PI / 2;
        if (!samples[i].is_reversing && !samples[i].is_omega) { if (up) t_up++; else t_down++; }
        if (samples[i].is_reversing && !prev_rev) { if (up) rev_up++; else rev_down++; }
        prev_rev = samples[i].is_reversing;
    }
    double rr_up = (t_up > 0) ? rev_up * 1000.0 / (t_up * 50.0) : 0;
    double rr_dn = (t_down > 0) ? rev_down * 1000.0 / (t_down * 50.0) : 0;
    res.klinokinesis_index = (rr_up + rr_dn > 0) ? (rr_dn - rr_up) / (rr_dn + rr_up) : 0;

    // Run length ratio
    {
        double up_len = 0, dn_len = 0; int up_n = 0, dn_n = 0;
        bool in_run = false; double run_start = 0; double fa_sum = 0; int fa_n = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            bool fwd = !samples[i].is_reversing && !samples[i].is_omega;
            if (fwd && !in_run) { run_start = samples[i].time_s; fa_sum = 0; fa_n = 0; in_run = true; }
            if (fwd && in_run) { fa_sum += samples[i].food_angle; fa_n++; }
            if ((!fwd || i == samples.size() - 1) && in_run) {
                double dur = samples[i - 1].time_s - run_start;
                if (dur > 0.05 && fa_n > 0) {
                    bool up_grad = std::abs(fa_sum / fa_n) < M_PI / 2;
                    if (up_grad) { up_len += dur; up_n++; } else { dn_len += dur; dn_n++; }
                }
                in_run = false;
            }
        }
        double mu = (up_n > 0) ? up_len / up_n : 0;
        double md = (dn_n > 0) ? dn_len / dn_n : 0;
        res.run_ratio = (md > 0) ? mu / md : (mu > 0 ? 99.0 : 0.0);
    }

    // Omega reorientation
    {
        bool in_omega = false; double pre_fa = 0;
        int omega_n = 0, omega_tw = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].is_omega && !in_omega) { pre_fa = samples[i - 1].food_angle; in_omega = true; }
            if (!samples[i].is_omega && in_omega) {
                omega_n++;
                if (std::abs(samples[i].food_angle) < std::abs(pre_fa)) omega_tw++;
                in_omega = false;
            }
        }
        res.omega_count = omega_n;
        res.omega_toward_pct = (omega_n > 0) ? 100.0 * omega_tw / omega_n : 50.0;
    }

    // Signal chain + new metrics (time_near_food, curving_rate, weathervane_slope)
    {
        double ria_abs_sum = 0, hc_sum = 0, hr_sum = 0; int sn = 0;
        int near_food_count = 0;
        constexpr double NEAR_FOOD_RADIUS = 5.0;  // mm (bacterial lawn ~4mm σ)
        // Accumulators for weathervane slope: linear regression of heading_rate on food_angle
        double wv_sx = 0, wv_sy = 0, wv_sxx = 0, wv_sxy = 0; int wv_n = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].food_dist < NEAR_FOOD_RADIUS) near_food_count++;
            if (samples[i].is_reversing || samples[i].is_omega) continue;
            ria_abs_sum += std::abs(samples[i].ria_ca_diff);
            hc_sum += samples[i].head_curv;
            hr_sum += std::abs(samples[i].heading_rate);
            sn++;
            // Weathervane slope: dθ/dt (°/s) vs food_angle (rad)
            // REF: Iino & Yoshida 2009 — curving rate = slope × ∇C_⊥
            // food_angle is a proxy for ∇C_⊥ (proportional when gradient exists)
            if (i > 0) {
                double fa = samples[i].food_angle;
                double hr = samples[i].heading_rate;  // already in °/s
                wv_sx += fa; wv_sy += hr;
                wv_sxx += fa * fa; wv_sxy += fa * hr;
                wv_n++;
            }
        }
        if (sn > 0) {
            res.ria_ca_diff_abs = ria_abs_sum / sn;
            res.head_curv = hc_sum / sn;
            res.heading_rate_abs = hr_sum / sn;
            res.curving_rate_mean = hr_sum / sn;  // same as heading_rate_abs
        }
        res.time_near_food = (samples.size() > 0)
            ? static_cast<double>(near_food_count) / samples.size() : 0;
        // Weathervane slope via least-squares: slope = (n*Σxy - Σx*Σy) / (n*Σx² - (Σx)²)
        if (wv_n > 2) {
            double denom = wv_n * wv_sxx - wv_sx * wv_sx;
            res.weathervane_slope = (denom > 1e-12)
                ? (wv_n * wv_sxy - wv_sx * wv_sy) / denom : 0;
        }
    }

    // --- Enhancement 1: Reversal attribution ---
    // Adaptive threshold: compute P75 of AIB release during forward movement.
    // Reversals where AIB > P75 are "sensory-driven" (AIB elevated above baseline).
    // This avoids the fixed 0.3 threshold which may miss low-amplitude modulation.
    {
        // Collect AIB release during forward movement for baseline distribution
        std::vector<double> aib_fwd;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (!samples[i].is_reversing && !samples[i].is_omega)
                aib_fwd.push_back(samples[i].aib_release);
        }
        std::sort(aib_fwd.begin(), aib_fwd.end());
        double aib_p75 = (!aib_fwd.empty()) ? aib_fwd[aib_fwd.size() * 3 / 4] : 0.3;
        double aib_fwd_sum = 0;
        for (double v : aib_fwd) aib_fwd_sum += v;
        res.aib_fwd_mean = (!aib_fwd.empty()) ? aib_fwd_sum / aib_fwd.size() : 0;
        res.aib_fwd_p75 = aib_p75;

        // Classify reversals
        int rev_sensory = 0, rev_stochastic = 0;
        double aib_at_rev_sum = 0; int rev_count = 0;
        bool was_reversing = false;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].is_reversing && !was_reversing) {
                double aib_val = samples[i].aib_release;
                aib_at_rev_sum += aib_val; rev_count++;
                if (aib_val > aib_p75) rev_sensory++;
                else rev_stochastic++;
            }
            was_reversing = samples[i].is_reversing;
        }
        int rev_total = rev_sensory + rev_stochastic;
        res.rev_sensory_pct = (rev_total > 0) ? 100.0 * rev_sensory / rev_total : 0;
        res.rev_stochastic_pct = (rev_total > 0) ? 100.0 * rev_stochastic / rev_total : 0;
        res.aib_at_rev_mean = (rev_count > 0) ? aib_at_rev_sum / rev_count : 0;
    }

    // --- Enhancement 2: ASER→AIB pathway correlation ---
    // Pearson correlation between ASER release and AIB release (lagged 1 sample = 50ms)
    // Positive correlation means the biological pathway is transmitting
    {
        double sx = 0, sy = 0, sxx = 0, syy = 0, sxy_val = 0; int cn = 0;
        for (size_t i = 2; i < samples.size(); ++i) {
            if (samples[i].is_omega || samples[i-1].is_omega) continue;
            double x = samples[i-1].aser_release;  // ASER at time t-1
            double y = samples[i].aib_release;       // AIB at time t (50ms lag)
            sx += x; sy += y; sxx += x*x; syy += y*y; sxy_val += x*y; cn++;
        }
        if (cn > 2) {
            double mx2 = sx/cn, my2 = sy/cn;
            double vx2 = sxx/cn - mx2*mx2, vy2 = syy/cn - my2*my2;
            double cov = sxy_val/cn - mx2*my2;
            res.aser_aib_corr = (vx2 > 1e-12 && vy2 > 1e-12) ? cov/std::sqrt(vx2*vy2) : 0;
        } else {
            res.aser_aib_corr = 0;
        }
    }

    // --- Enhancement 2b: AIY klinokinesis pathway (Luo 2014, Gray 2005) ---
    // Klinokinesis works through AIY→AVB (run promotion), NOT AIB→AVA.
    // Luo 2014: "AIB peaks at END of reorientation" (omega turn, not reversal trigger).
    // Gray 2005: "AIY ablation → failed to suppress reversals" (AIY is the key node).
    // Pathway: ASEL(ON)→AIY(excit) + ASER(OFF)⊣AIY(inhib) → AIY→AVB → forward drive
    // When heading toward food: ASEL active → AIY↑ → AVB↑ → suppress reversals
    // When heading away: ASER active → AIY↓ → AVB↓ → permit reversals
    {
        double aiy_toward_sum = 0, aiy_away_sum = 0;
        int toward_n = 0, away_n = 0;
        double aiy_fwd_sum = 0; int fwd_n = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].is_reversing || samples[i].is_omega) continue;
            aiy_fwd_sum += samples[i].aiy_release; fwd_n++;
            bool toward = std::abs(samples[i].food_angle) < M_PI / 2;
            if (toward) { aiy_toward_sum += samples[i].aiy_release; toward_n++; }
            else { aiy_away_sum += samples[i].aiy_release; away_n++; }
        }
        res.aiy_fwd_mean = (fwd_n > 0) ? aiy_fwd_sum / fwd_n : 0;
        res.aiy_toward_mean = (toward_n > 0) ? aiy_toward_sum / toward_n : 0;
        res.aiy_away_mean = (away_n > 0) ? aiy_away_sum / away_n : 0;

        // AIY↔AVB correlation (should be positive: AIY promotes forward drive)
        double sx2 = 0, sy2 = 0, sxx2 = 0, syy2 = 0, sxy2 = 0; int cn2 = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].is_omega) continue;
            double x = samples[i].aiy_release;
            double y = samples[i].avb_rel;
            sx2 += x; sy2 += y; sxx2 += x*x; syy2 += y*y; sxy2 += x*y; cn2++;
        }
        if (cn2 > 2) {
            double mx3 = sx2/cn2, my3 = sy2/cn2;
            double vx3 = sxx2/cn2 - mx3*mx3, vy3 = syy2/cn2 - my3*my3;
            double cov2 = sxy2/cn2 - mx3*my3;
            res.aiy_avb_corr = (vx3 > 1e-12 && vy3 > 1e-12) ? cov2/std::sqrt(vx3*vy3) : 0;
        } else {
            res.aiy_avb_corr = 0;
        }
    }

    // --- Enhancement 3: CI phase decomposition ---
    // Break total displacement into forward/reversal/omega contributions
    {
        double fwd_disp = 0, rev_disp = 0, omg_disp = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            double dx = samples[i].pos.x - samples[i-1].pos.x;
            double dy = samples[i].pos.y - samples[i-1].pos.y;
            // Project displacement onto food direction
            double fdx = food_pos.x - samples[i-1].pos.x;
            double fdy = food_pos.y - samples[i-1].pos.y;
            double fd = std::sqrt(fdx*fdx + fdy*fdy);
            double proj = (fd > 0.01) ? (dx*fdx + dy*fdy) / fd : 0;
            if (samples[i].is_omega) omg_disp += proj;
            else if (samples[i].is_reversing) rev_disp += proj;
            else fwd_disp += proj;
        }
        // Normalize by total path length
        if (total_path > 0) {
            res.ci_fwd = fwd_disp / total_path;
            res.ci_rev = rev_disp / total_path;
            res.ci_omega = omg_disp / total_path;
        }
    }

    // Food angle convergence
    {
        int n_total = static_cast<int>(samples.size());
        int q = n_total / 4;
        double q1 = 0, q4 = 0;
        for (int i = 0; i < q; ++i) q1 += std::abs(deg(samples[i].food_angle));
        for (int i = n_total - q; i < n_total; ++i) q4 += std::abs(deg(samples[i].food_angle));
        res.converging = (q > 0) ? (q4 / q < q1 / q) : false;
    }

    // Bottleneck count
    res.bottleneck_count = 0;
    if (res.ria_ca_diff_abs < 0.005) res.bottleneck_count++;
    if (res.ktx_corr < 0.05) res.bottleneck_count++;
    if (res.heading_bias < 0.02) res.bottleneck_count++;
    if (res.klinokinesis_index < 0.1) res.bottleneck_count++;
    if (res.run_ratio < 1.2 && res.run_ratio > 0) res.bottleneck_count++;
    if (res.omega_toward_pct < 55.0) res.bottleneck_count++;

    return res;
}

// ================================================================
// Parse comma-separated seed list
// ================================================================
static std::vector<int> parse_seeds(const std::string& s) {
    std::vector<int> seeds;
    std::istringstream iss(s);
    std::string token;
    while (std::getline(iss, token, ',')) {
        try { seeds.push_back(std::stoi(token)); } catch (...) {}
    }
    return seeds;
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    double duration_s = 60.0;
    std::vector<int> seeds;
    bool verbose = false;
    double target_x = 20.0, target_y = 0.0;
    int max_threads = 8;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc)
            duration_s = std::stod(argv[++i]);
        else if ((arg == "--seed" || arg == "-s") && i + 1 < argc)
            seeds.push_back(std::stoi(argv[++i]));
        else if (arg == "--seeds" && i + 1 < argc)
            seeds = parse_seeds(argv[++i]);
        else if ((arg == "--threads" || arg == "-j") && i + 1 < argc)
            max_threads = std::stoi(argv[++i]);
        else if (arg == "--verbose" || arg == "-v")
            verbose = true;
        else if (arg == "--target-x" && i + 1 < argc)
            target_x = std::stod(argv[++i]);
        else if (arg == "--target-y" && i + 1 < argc)
            target_y = std::stod(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: chemotaxis_analyzer [--seed N | --seeds N,N,...] [--duration N]\n"
                      << "       [--threads N] [--verbose] [--target-x X] [--target-y Y]\n";
            return 0;
        }
    }
    if (seeds.empty()) seeds.push_back(42);
    if (max_threads < 1) max_threads = 1;
    if (max_threads > 8) max_threads = 8;

    Logger::instance().set_level(LogLevel::WARN);

    std::cout << "========================================\n";
    std::cout << "  趋化归因分析器 (Chemotaxis Analyzer)\n";
    std::cout << "========================================\n\n";
    std::cout << "  仿真时长:  " << duration_s << " s\n";
    std::cout << "  种子数量:  " << seeds.size() << "  (";
    for (size_t i = 0; i < seeds.size(); ++i) { if (i) std::cout << ","; std::cout << seeds[i]; }
    std::cout << ")\n";
    std::cout << "  并行线程:  " << std::min(max_threads, static_cast<int>(seeds.size())) << "\n";
    std::cout << "  食物位置:  (" << target_x << ", " << target_y << ")\n\n";

    // --- Parallel execution ---
    std::vector<SeedResult> results(seeds.size());
    std::mutex print_mtx;
    std::atomic<int> completed{0};
    int n_seeds = static_cast<int>(seeds.size());
    int n_threads = std::min(max_threads, n_seeds);

    std::cout << "  运行仿真... " << std::flush;

    auto worker = [&](int thread_id) {
        for (int idx = thread_id; idx < n_seeds; idx += n_threads) {
            results[idx] = run_single_seed(seeds[idx], duration_s, target_x, target_y);
            int done = ++completed;
            std::lock_guard<std::mutex> lock(print_mtx);
            std::cout << "[" << done << "/" << n_seeds << "] " << std::flush;
        }
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < n_threads; ++t) threads.emplace_back(worker, t);
    for (auto& t : threads) t.join();

    std::cout << "完成！\n\n";

    // --- Per-seed table ---
    std::cout << "========================================\n";
    std::cout << "  PER-SEED RESULTS\n";
    std::cout << "========================================\n\n";

    std::cout << std::fixed;
    std::cout << "  Seed   CI      Ktx_r   H_bias  Klnk    RunR    Omg%    Omg#  TNF%    WV_slp  Spd    Conv\n";
    std::cout << "  ----   ------  ------  ------  ------  ------  ------  ----  ------  ------  ------  ----\n";
    for (auto& r : results) {
        auto mark = [](double v, double ok, bool higher) -> const char* {
            return (higher ? v >= ok : v <= ok) ? " " : "*";
        };
        std::cout << "  " << std::setw(4) << r.seed
                  << "   " << std::setw(6) << std::setprecision(3) << r.ci
                  << mark(r.ktx_corr, 0.05, true)
                  << " " << std::setw(5) << std::setprecision(3) << r.ktx_corr
                  << "  " << std::setw(6) << std::setprecision(3) << r.heading_bias
                  << mark(r.klinokinesis_index, 0.1, true)
                  << " " << std::setw(5) << std::setprecision(3) << r.klinokinesis_index
                  << "  " << std::setw(6) << std::setprecision(2) << r.run_ratio
                  << mark(r.omega_toward_pct, 55.0, true)
                  << " " << std::setw(5) << std::setprecision(1) << r.omega_toward_pct
                  << "  " << std::setw(4) << r.omega_count
                  << "  " << std::setw(5) << std::setprecision(1) << r.time_near_food * 100
                  << "%  " << std::setw(5) << std::setprecision(1) << r.weathervane_slope
                  << "  " << std::setw(5) << std::setprecision(3) << r.mean_speed
                  << "  " << (r.converging ? "  Y" : "  N")
                  << "\n";
    }
    std::cout << "\n  (* = below threshold)\n\n";

    // --- Aggregated statistics ---
    auto avg = [&](auto fn) {
        double sum = 0; for (auto& r : results) sum += fn(r);
        return sum / results.size();
    };
    auto stdev = [&](auto fn, double mean) {
        double sum = 0; for (auto& r : results) { double d = fn(r) - mean; sum += d * d; }
        return std::sqrt(sum / results.size());
    };

    double ci_mean = avg([](const SeedResult& r) { return r.ci; });
    double ci_sd   = stdev([](const SeedResult& r) { return r.ci; }, ci_mean);
    double ktx_mean = avg([](const SeedResult& r) { return r.ktx_corr; });
    double hb_mean = avg([](const SeedResult& r) { return r.heading_bias; });
    double kk_mean = avg([](const SeedResult& r) { return r.klinokinesis_index; });
    double rr_mean = avg([](const SeedResult& r) { return r.run_ratio; });
    double ot_mean = avg([](const SeedResult& r) { return r.omega_toward_pct; });
    double spd_mean = avg([](const SeedResult& r) { return r.mean_speed; });
    double ria_mean = avg([](const SeedResult& r) { return r.ria_ca_diff_abs; });
    double tnf_mean = avg([](const SeedResult& r) { return r.time_near_food; });
    double wvs_mean = avg([](const SeedResult& r) { return r.weathervane_slope; });
    double cr_mean = avg([](const SeedResult& r) { return r.curving_rate_mean; });
    int n_converge = 0;
    for (auto& r : results) if (r.converging) n_converge++;

    std::cout << "========================================\n";
    std::cout << "  AGGREGATED SUMMARY (" << n_seeds << " seeds)\n";
    std::cout << "========================================\n\n";
    std::cout << std::setprecision(3);
    std::cout << "  CI:               " << ci_mean << " ± " << ci_sd << "\n";
    std::cout << "  Klinotaxis corr:  " << ktx_mean << "\n";
    std::cout << "  Heading bias:     " << hb_mean << "\n";
    std::cout << "  Klinokinesis:     " << kk_mean << "\n";
    std::cout << "  Run ratio:        " << std::setprecision(2) << rr_mean << "\n";
    std::cout << "  Omega toward%:    " << std::setprecision(1) << ot_mean << "%\n";
    std::cout << "  Mean speed:       " << std::setprecision(3) << spd_mean << " mm/s\n";
    std::cout << "  Time near food:   " << std::setprecision(1) << tnf_mean * 100 << "%  (r<5mm)\n";
    std::cout << "  Curving rate:     " << std::setprecision(1) << cr_mean << " \xc2\xb0/s  (fwd |d\xce\xb8/dt|)\n";
    std::cout << "  WV slope:         " << std::setprecision(2) << wvs_mean << " \xc2\xb0/s/rad  (Iino 2009)\n";
    std::cout << "  RIA |Ca\xc2\xb2\xe2\x81\xba AC|:    " << std::setprecision(4) << ria_mean << "\n";
    std::cout << "  Converging:       " << n_converge << "/" << n_seeds << "\n\n";

    // --- Enhancement output ---
    double rs_mean = avg([](const SeedResult& r) { return r.rev_sensory_pct; });
    double rn_mean = avg([](const SeedResult& r) { return r.rev_stochastic_pct; });
    double aa_mean = avg([](const SeedResult& r) { return r.aser_aib_corr; });
    double cf_mean = avg([](const SeedResult& r) { return r.ci_fwd; });
    double crv_mean = avg([](const SeedResult& r) { return r.ci_rev; });
    double co_mean = avg([](const SeedResult& r) { return r.ci_omega; });

    double aib_fm = avg([](const SeedResult& r) { return r.aib_fwd_mean; });
    double aib_p75m = avg([](const SeedResult& r) { return r.aib_fwd_p75; });
    double aib_rev = avg([](const SeedResult& r) { return r.aib_at_rev_mean; });

    std::cout << "--- \xe5\x8f\x8d\xe8\xbd\xac\xe5\xbd\x92\xe5\x9b\xa0 & CI\xe5\x88\x86\xe8\xa7\xa3 ---\n";
    std::cout << "  AIB release:      fwd_mean=" << std::setprecision(4) << aib_fm
              << "  P75=" << aib_p75m
              << "  at_rev=" << aib_rev << "\n";
    std::cout << "  Rev attribution:  " << std::setprecision(1)
              << rs_mean << "% sensory (AIB>P75)  "
              << rn_mean << "% stochastic\n";
    std::cout << "  ASER\xe2\x86\x92""AIB corr:   " << std::setprecision(3) << aa_mean
              << "  (" << (aa_mean > 0.1 ? "\xe2\x9c\x93 pathway active" : "\xe2\x9c\x97 weak/broken") << ")\n";
    double aiy_fm = avg([](const SeedResult& r) { return r.aiy_fwd_mean; });
    double aiy_tw = avg([](const SeedResult& r) { return r.aiy_toward_mean; });
    double aiy_aw = avg([](const SeedResult& r) { return r.aiy_away_mean; });
    double aiy_avb = avg([](const SeedResult& r) { return r.aiy_avb_corr; });

    std::cout << "  AIY klinokinesis: toward=" << std::setprecision(4) << aiy_tw
              << "  away=" << aiy_aw
              << "  delta=" << std::setprecision(4) << (aiy_tw - aiy_aw)
              << "  (" << (aiy_tw > aiy_aw ? "\xe2\x9c\x93 correct" : "\xe2\x9c\x97 inverted") << ")\n";
    std::cout << "  AIY\xe2\x86\x92""AVB corr:    " << std::setprecision(3) << aiy_avb
              << "  (" << (aiy_avb > 0.1 ? "\xe2\x9c\x93 pathway active" : "\xe2\x9c\x97 weak/broken") << ")\n";
    std::cout << "  CI decomposition: fwd=" << std::setprecision(4) << cf_mean
              << "  rev=" << crv_mean
              << "  omega=" << co_mean
              << "  (sum=" << std::setprecision(3) << (cf_mean + crv_mean + co_mean) << ")\n\n";

    // --- Bottleneck summary ---
    std::cout << "--- 信号链瓶颈汇总 ---\n";
    struct BnCheck { const char* name; double val; double ok; bool higher; const char* hint; };
    BnCheck checks[] = {
        {"RIA Ca²⁺ |AC|", ria_mean, 0.005, true, "AWC/ASE→AIY→RIA 通路"},
        {"Klinotaxis corr", ktx_mean, 0.05, true, "RIA→SMB→肌肉→曲率 信号链"},
        {"Heading bias", hb_mean, 0.02, true, "smb_muscle_gain / klinotaxis_gain"},
        {"Klinokinesis", kk_mean, 0.1, true, "dC/dt→ASER→AIB→AVA 通路"},
        {"Run ratio", rr_mean, 1.2, true, "反转梯度调制"},
        {"Omega toward%", ot_mean, 55.0, true, "RIV L/R gradient bias"},
        {"Time near food", tnf_mean * 100, 5.0, true, "\xe5\xaf\xbc\xe8\x88\xaa\xe6\x95\x88\xe7\x8e\x87 (\xe8\x99\xab\xe6\x9c\xaa\xe5\x88\xb0\xe8\xbe\xbe\xe9\xa3\x9f\xe7\x89\xa9\xe5\x8c\xba)"},
    };

    int total_bn = 0;
    for (int ci = 0; ci < 7; ++ci) {
        BnCheck& c = checks[ci];
        bool ok = c.higher ? c.val >= c.ok : c.val <= c.ok;
        std::cout << "  " << (ok ? "✓" : "✗") << " " << c.name << " = " << std::setprecision(3) << c.val;
        if (!ok) { std::cout << "  → " << c.hint; total_bn++; }
        std::cout << "\n";
    }

    std::cout << "\n  总瓶颈: " << total_bn << " 个\n";
    if (total_bn == 0) std::cout << "  ✓ 所有信号链环节正常\n";
    std::cout << "\n";

    return 0;
}
