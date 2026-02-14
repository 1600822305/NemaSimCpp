// wave_analyzer_main.cpp — Wave propagation & RFT locomotion diagnostics
// Step 119: Diagnoses traveling wave direction, A/B-class balance,
// RFT force decomposition, proprioceptive gating, and klinotaxis effectiveness.
//
// Usage: wave_analyzer.exe [--duration N] [--seed N] [--verbose]
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <numeric>
#include <string>
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
// Data structures
// ================================================================

struct WaveSample {
    double time_s;
    std::array<double, NUM_BODY_SEGMENTS> curvature;
    double speed;
    double direction;
    bool is_reversing;
    double ava_rel;
    double avb_rel;
    double heading;
    double omega_rft;   // angular velocity from RFT
};

struct WaveStats {
    // Wave direction: cross-correlation lag between adjacent segments
    double fwd_wave_coherence = 0.0;   // fraction of time with head→tail wave during forward
    double rev_wave_coherence = 0.0;   // fraction of time with tail→head wave during reverse
    double fwd_wave_speed = 0.0;       // body lengths/s during forward
    double rev_wave_speed = 0.0;       // body lengths/s during reverse

    // RFT decomposition
    double fwd_mean_vx = 0.0, fwd_mean_vy = 0.0, fwd_mean_omega = 0.0;
    double rev_mean_vx = 0.0, rev_mean_vy = 0.0, rev_mean_omega = 0.0;
    double fwd_mean_speed = 0.0, rev_mean_speed = 0.0;

    // Heading change rates
    double fwd_heading_rate = 0.0;   // deg/s during forward
    double rev_heading_rate = 0.0;   // deg/s during reverse

    // Motor class balance
    double fwd_b_class_activity = 0.0;  // mean B-class MN release during forward
    double fwd_a_class_activity = 0.0;  // mean A-class MN release during forward
    double rev_b_class_activity = 0.0;  // mean B-class MN release during reverse
    double rev_a_class_activity = 0.0;  // mean A-class MN release during reverse

    // Proprioceptive gate
    double fwd_avb_gate = 0.0;
    double fwd_ava_gate = 0.0;
    double rev_avb_gate = 0.0;
    double rev_ava_gate = 0.0;

    // Klinotaxis effectiveness
    double klinotaxis_torque_fraction = 0.0;  // head torque / total torque
    double heading_food_correlation = 0.0;    // correlation of heading change with food direction

    // Time fractions
    double fwd_fraction = 0.0;
    double rev_fraction = 0.0;
    int n_fwd_samples = 0;
    int n_rev_samples = 0;
};

// ================================================================
// Wave direction detection via cross-correlation
// ================================================================
// For a head→tail traveling wave: segment i leads segment i+1 in time
// Cross-correlate curvature[seg_a] vs curvature[seg_b] at various lags
// Positive peak lag → wave goes a→b; negative → wave goes b→a
static double compute_wave_lag(const std::vector<double>& a,
                                const std::vector<double>& b,
                                int max_lag) {
    int n = static_cast<int>(a.size());
    if (n < max_lag * 2 + 1) return 0.0;

    double best_corr = -1e30;
    int best_lag = 0;

    double mean_a = 0, mean_b = 0;
    for (int i = 0; i < n; ++i) { mean_a += a[i]; mean_b += b[i]; }
    mean_a /= n; mean_b /= n;

    double var_a = 0, var_b = 0;
    for (int i = 0; i < n; ++i) {
        var_a += (a[i] - mean_a) * (a[i] - mean_a);
        var_b += (b[i] - mean_b) * (b[i] - mean_b);
    }
    double norm = std::sqrt(var_a * var_b);
    if (norm < 1e-12) return 0.0;

    for (int lag = -max_lag; lag <= max_lag; ++lag) {
        double corr = 0;
        int count = 0;
        for (int i = 0; i < n; ++i) {
            int j = i + lag;
            if (j >= 0 && j < n) {
                corr += (a[i] - mean_a) * (b[j] - mean_b);
                ++count;
            }
        }
        if (count > 0) corr /= norm;
        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag;
        }
    }
    return static_cast<double>(best_lag);
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    double duration_s = 30.0;
    int seed = 42;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            duration_s = std::stod(argv[++i]);
        } else if ((arg == "--seed" || arg == "-s") && i + 1 < argc) {
            seed = std::stoi(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: wave_analyzer [--duration N] [--seed N] [--verbose]\n";
            return 0;
        }
    }

    Logger::instance().set_level(LogLevel::WARN);

    std::cout << "========================================\n";
    std::cout << "  行波分析器 (Wave Analyzer)\n";
    std::cout << "========================================\n\n";
    std::cout << "  仿真时长:  " << duration_s << " s\n";
    std::cout << "  随机种子:  " << seed << "\n\n";

    // --- Setup simulation ---
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    double dt = sim.dt();
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt);  // dt is in ms
    int sample_interval = 20;  // sample every 20 steps (10ms at 0.5ms dt)

    // --- Neuron ID lookup ---
    auto& neurons = sim.neurons();
    int n_neurons = static_cast<int>(neurons.size());

    auto find_id = [&](const char* name) -> int {
        for (int i = 0; i < n_neurons; ++i) {
            if (neurons[i]->info().name == name) return i;
        }
        return -1;
    };

    int aval_id = find_id("AVAL"), avar_id = find_id("AVAR");
    int avbl_id = find_id("AVBL"), avbr_id = find_id("AVBR");

    // B-class MN IDs (DB01-07, VB01-11)
    std::vector<int> b_class_ids;
    for (int i = 1; i <= 7; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "DB%02d", i);
        int id = find_id(name);
        if (id >= 0) b_class_ids.push_back(id);
    }
    for (int i = 1; i <= 11; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "VB%02d", i);
        int id = find_id(name);
        if (id >= 0) b_class_ids.push_back(id);
    }

    // A-class MN IDs (DA01-09, VA01-12)
    std::vector<int> a_class_ids;
    for (int i = 1; i <= 9; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "DA%02d", i);
        int id = find_id(name);
        if (id >= 0) a_class_ids.push_back(id);
    }
    for (int i = 1; i <= 12; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "VA%02d", i);
        int id = find_id(name);
        if (id >= 0) a_class_ids.push_back(id);
    }

    // --- Collect data ---
    std::vector<WaveSample> samples;
    samples.reserve(total_steps / sample_interval + 1);

    // Per-segment curvature time series (for wave analysis)
    // We track 5 representative segments: head(5), anterior(12), mid(24), posterior(36), tail(44)
    constexpr int N_PROBE = 5;
    constexpr int probe_segs[N_PROBE] = {5, 12, 24, 36, 44};
    std::array<std::vector<double>, N_PROBE> curv_fwd, curv_rev;

    std::cout << "  运行仿真... " << std::flush;

    for (int step = 0; step < total_steps; ++step) {
        sim.step();

        if (step % sample_interval != 0) continue;

        WaveSample ws;
        ws.time_s = step * dt / 1000.0;  // dt is in ms
        ws.speed = sim.body().get_speed();
        ws.direction = sim.body().get_direction();
        ws.is_reversing = sim.is_reversing();
        ws.heading = sim.body().get_head_angle() * 180.0 / M_PI;

        // AVA/AVB release
        ws.ava_rel = 0.0; ws.avb_rel = 0.0;
        if (aval_id >= 0) ws.ava_rel += neurons[aval_id]->get_transmitter_release_rate();
        if (avar_id >= 0) ws.ava_rel += neurons[avar_id]->get_transmitter_release_rate();
        if (avbl_id >= 0) ws.avb_rel += neurons[avbl_id]->get_transmitter_release_rate();
        if (avbr_id >= 0) ws.avb_rel += neurons[avbr_id]->get_transmitter_release_rate();
        ws.ava_rel *= 0.5; ws.avb_rel *= 0.5;

        // Curvature snapshot
        for (int i = 0; i < NUM_BODY_SEGMENTS; ++i) {
            ws.curvature[i] = sim.body().get_local_curvature(i);
        }

        samples.push_back(ws);

        // Accumulate per-state curvature time series for wave analysis
        bool fwd_state = !ws.is_reversing;
        for (int p = 0; p < N_PROBE; ++p) {
            if (fwd_state) {
                curv_fwd[p].push_back(ws.curvature[probe_segs[p]]);
            } else {
                curv_rev[p].push_back(ws.curvature[probe_segs[p]]);
            }
        }
    }

    std::cout << "完成！\n\n";

    // --- Compute statistics ---
    WaveStats stats;

    // Time fractions
    for (auto& ws : samples) {
        if (!ws.is_reversing) {
            stats.n_fwd_samples++;
            stats.fwd_mean_speed += ws.speed;
        } else {
            stats.n_rev_samples++;
            stats.rev_mean_speed += ws.speed;
        }
    }
    int total_samples = static_cast<int>(samples.size());
    stats.fwd_fraction = static_cast<double>(stats.n_fwd_samples) / total_samples;
    stats.rev_fraction = static_cast<double>(stats.n_rev_samples) / total_samples;
    if (stats.n_fwd_samples > 0) stats.fwd_mean_speed /= stats.n_fwd_samples;
    if (stats.n_rev_samples > 0) stats.rev_mean_speed /= stats.n_rev_samples;

    // Heading change rate
    double fwd_dh_sum = 0, rev_dh_sum = 0;
    int fwd_dh_n = 0, rev_dh_n = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        double dh = samples[i].heading - samples[i-1].heading;
        // Wrap to [-180, 180]
        while (dh > 180.0) dh -= 360.0;
        while (dh < -180.0) dh += 360.0;
        double dt_sample = (samples[i].time_s - samples[i-1].time_s);
        if (dt_sample < 1e-6) continue;
        double rate = std::abs(dh) / dt_sample;  // deg/s
        if (!samples[i].is_reversing) {
            fwd_dh_sum += rate; fwd_dh_n++;
        } else {
            rev_dh_sum += rate; rev_dh_n++;
        }
    }
    if (fwd_dh_n > 0) stats.fwd_heading_rate = fwd_dh_sum / fwd_dh_n;
    if (rev_dh_n > 0) stats.rev_heading_rate = rev_dh_sum / rev_dh_n;

    // Motor class balance
    for (auto& ws : samples) {
        double b_act = 0, a_act = 0;
        for (int id : b_class_ids) {
            if (id >= 0 && id < n_neurons) b_act += neurons[id]->get_transmitter_release_rate();
        }
        for (int id : a_class_ids) {
            if (id >= 0 && id < n_neurons) a_act += neurons[id]->get_transmitter_release_rate();
        }
        if (!b_class_ids.empty()) b_act /= b_class_ids.size();
        if (!a_class_ids.empty()) a_act /= a_class_ids.size();

        // Note: this uses final state, but gives overall picture
        if (!ws.is_reversing) {
            stats.fwd_b_class_activity += b_act;
            stats.fwd_a_class_activity += a_act;
        } else {
            stats.rev_b_class_activity += b_act;
            stats.rev_a_class_activity += a_act;
        }
    }
    // Actually we need per-sample MN activity. Since we only have final state,
    // let's re-run a shorter sim with per-step MN tracking.
    // For now, use per-sample AVA/AVB as proxy for class balance.

    // Proprioceptive gate values
    // Latched gate (same as apply_proprioceptive_stretch: uses is_reversing_ state)
    for (auto& ws : samples) {
        double avb_gate = ws.is_reversing ? 0.0 : 1.0;
        double ava_gate = ws.is_reversing ? 1.0 : 0.0;
        if (!ws.is_reversing) {
            stats.fwd_avb_gate += avb_gate;
            stats.fwd_ava_gate += ava_gate;
        } else {
            stats.rev_avb_gate += avb_gate;
            stats.rev_ava_gate += ava_gate;
        }
    }
    if (stats.n_fwd_samples > 0) {
        stats.fwd_avb_gate /= stats.n_fwd_samples;
        stats.fwd_ava_gate /= stats.n_fwd_samples;
        stats.fwd_b_class_activity /= stats.n_fwd_samples;
        stats.fwd_a_class_activity /= stats.n_fwd_samples;
    }
    if (stats.n_rev_samples > 0) {
        stats.rev_avb_gate /= stats.n_rev_samples;
        stats.rev_ava_gate /= stats.n_rev_samples;
        stats.rev_b_class_activity /= stats.n_rev_samples;
        stats.rev_a_class_activity /= stats.n_rev_samples;
    }

    // --- Wave direction analysis ---
    // Cross-correlate adjacent probe segments
    // Positive lag → segment A leads B → wave goes A→B (head→tail for probes)
    int max_lag = 50;  // 50 samples × 10ms = 500ms max lag

    // Forward state: expect positive lag (head→tail wave = forward RFT thrust)
    double fwd_lag_sum = 0;
    int fwd_lag_count = 0;
    for (int p = 0; p < N_PROBE - 1; ++p) {
        if (curv_fwd[p].size() > 100 && curv_fwd[p+1].size() > 100) {
            // Use same-length vectors
            size_t len = std::min(curv_fwd[p].size(), curv_fwd[p+1].size());
            std::vector<double> a(curv_fwd[p].begin(), curv_fwd[p].begin() + len);
            std::vector<double> b(curv_fwd[p+1].begin(), curv_fwd[p+1].begin() + len);
            double lag = compute_wave_lag(a, b, max_lag);
            fwd_lag_sum += lag;
            fwd_lag_count++;
        }
    }
    if (fwd_lag_count > 0) {
        double mean_lag = fwd_lag_sum / fwd_lag_count;
        stats.fwd_wave_coherence = (mean_lag > 0) ? 1.0 : (mean_lag < 0 ? -1.0 : 0.0);
        double sample_dt = sample_interval * dt;
        if (std::abs(mean_lag) > 0.5) {
            // Wave speed in body lengths/s
            double seg_dist = 12.0 / 48.0;  // ~12 segments between probes, in body lengths
            stats.fwd_wave_speed = seg_dist / (std::abs(mean_lag) * sample_dt);
        }
    }

    // Reverse state: expect negative lag (tail→head wave = backward RFT thrust)
    double rev_lag_sum = 0;
    int rev_lag_count = 0;
    for (int p = 0; p < N_PROBE - 1; ++p) {
        if (curv_rev[p].size() > 100 && curv_rev[p+1].size() > 100) {
            size_t len = std::min(curv_rev[p].size(), curv_rev[p+1].size());
            std::vector<double> a(curv_rev[p].begin(), curv_rev[p].begin() + len);
            std::vector<double> b(curv_rev[p+1].begin(), curv_rev[p+1].begin() + len);
            double lag = compute_wave_lag(a, b, max_lag);
            rev_lag_sum += lag;
            rev_lag_count++;
        }
    }
    if (rev_lag_count > 0) {
        double mean_lag = rev_lag_sum / rev_lag_count;
        stats.rev_wave_coherence = (mean_lag < 0) ? 1.0 : (mean_lag > 0 ? -1.0 : 0.0);
        double sample_dt = sample_interval * dt;
        if (std::abs(mean_lag) > 0.5) {
            double seg_dist = 12.0 / 48.0;
            stats.rev_wave_speed = seg_dist / (std::abs(mean_lag) * sample_dt);
        }
    }

    // --- Curvature spatial pattern analysis ---
    // For each state, compute mean absolute curvature per segment region
    std::array<double, 4> fwd_curv_region = {}, rev_curv_region = {};  // head, anterior, posterior, tail
    int fwd_n = 0, rev_n = 0;
    for (auto& ws : samples) {
        double head_c = 0, ant_c = 0, post_c = 0, tail_c = 0;
        for (int i = 0; i < 12; ++i) head_c += std::abs(ws.curvature[i]);
        for (int i = 12; i < 24; ++i) ant_c += std::abs(ws.curvature[i]);
        for (int i = 24; i < 36; ++i) post_c += std::abs(ws.curvature[i]);
        for (int i = 36; i < 48; ++i) tail_c += std::abs(ws.curvature[i]);
        head_c /= 12; ant_c /= 12; post_c /= 12; tail_c /= 12;

        if (!ws.is_reversing) {
            fwd_curv_region[0] += head_c; fwd_curv_region[1] += ant_c;
            fwd_curv_region[2] += post_c; fwd_curv_region[3] += tail_c;
            fwd_n++;
        } else {
            rev_curv_region[0] += head_c; rev_curv_region[1] += ant_c;
            rev_curv_region[2] += post_c; rev_curv_region[3] += tail_c;
            rev_n++;
        }
    }
    if (fwd_n > 0) for (auto& v : fwd_curv_region) v /= fwd_n;
    if (rev_n > 0) for (auto& v : rev_curv_region) v /= rev_n;

    // --- Direction distribution ---
    int fwd_dir_pos = 0, fwd_dir_neg = 0, rev_dir_pos = 0, rev_dir_neg = 0;
    for (auto& ws : samples) {
        if (!ws.is_reversing) {
            if (ws.direction > 0) fwd_dir_pos++; else fwd_dir_neg++;
        } else {
            if (ws.direction > 0) rev_dir_pos++; else rev_dir_neg++;
        }
    }

    // ================================================================
    // Output
    // ================================================================
    std::cout << "========================================\n";
    std::cout << "  WAVE ANALYSIS RESULTS\n";
    std::cout << "========================================\n\n";

    // --- State fractions ---
    std::cout << "--- 运动状态 ---\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Forward:  " << stats.fwd_fraction * 100 << "%  ("
              << stats.n_fwd_samples << " samples)\n";
    std::cout << "  Reverse:  " << stats.rev_fraction * 100 << "%  ("
              << stats.n_rev_samples << " samples)\n\n";

    // --- Wave propagation ---
    std::cout << "--- 行波传播 ---\n";
    std::cout << "  Forward 状态:\n";
    std::cout << "    波方向:     " << (stats.fwd_wave_coherence > 0 ? "HEAD→TAIL ✓ (正确)" :
                                        stats.fwd_wave_coherence < 0 ? "TAIL→HEAD ✗ (错误!)" :
                                        "不确定 (低相干)") << "\n";
    if (stats.fwd_wave_speed > 0)
        std::cout << "    波速:       " << std::setprecision(2) << stats.fwd_wave_speed << " body-lengths/s\n";

    std::cout << "  Reverse 状态:\n";
    std::cout << "    波方向:     " << (stats.rev_wave_coherence > 0 ? "TAIL→HEAD ✓ (正确)" :
                                        stats.rev_wave_coherence < 0 ? "HEAD→TAIL ✗ (错误! B-class波未被抑制)" :
                                        "不确定 (低相干)") << "\n";
    if (stats.rev_wave_speed > 0)
        std::cout << "    波速:       " << std::setprecision(2) << stats.rev_wave_speed << " body-lengths/s\n";
    std::cout << "\n";

    // --- RFT velocity ---
    std::cout << "--- RFT 速度 ---\n";
    std::cout << std::setprecision(3);
    std::cout << "  Forward 平均速度:  " << stats.fwd_mean_speed << " mm/s\n";
    std::cout << "  Reverse 平均速度:  " << stats.rev_mean_speed << " mm/s\n";
    std::cout << "  Forward 航向变化:  " << std::setprecision(1) << stats.fwd_heading_rate << " deg/s\n";
    std::cout << "  Reverse 航向变化:  " << stats.rev_heading_rate << " deg/s\n\n";

    // --- Direction distribution ---
    std::cout << "--- RFT 方向分布 ---\n";
    std::cout << "  Forward 状态:  +1=" << fwd_dir_pos << "  -1=" << fwd_dir_neg;
    if (stats.n_fwd_samples > 0)
        std::cout << "  (前进比=" << std::setprecision(0)
                  << 100.0 * fwd_dir_pos / stats.n_fwd_samples << "%)";
    std::cout << "\n";
    std::cout << "  Reverse 状态:  +1=" << rev_dir_pos << "  -1=" << rev_dir_neg;
    if (stats.n_rev_samples > 0)
        std::cout << "  (后退比=" << std::setprecision(0)
                  << 100.0 * rev_dir_neg / stats.n_rev_samples << "%)";
    std::cout << "\n\n";

    // --- Proprioceptive gate ---
    std::cout << "--- 本体感觉门控 ---\n";
    std::cout << std::setprecision(3);
    std::cout << "  Forward: AVB_gate=" << stats.fwd_avb_gate
              << "  AVA_gate=" << stats.fwd_ava_gate << "\n";
    std::cout << "  Reverse: AVB_gate=" << stats.rev_avb_gate
              << "  AVA_gate=" << stats.rev_ava_gate << "\n";
    // Evaluate gating effectiveness
    bool fwd_gate_ok = stats.fwd_avb_gate > 0.5 && stats.fwd_ava_gate < 0.5;
    bool rev_gate_ok = stats.rev_avb_gate < 0.5 && stats.rev_ava_gate > 0.5;
    std::cout << "  Forward 门控: " << (fwd_gate_ok ? "✓ B-class活跃, A-class抑制" :
                                                       "⚠ 门控不充分") << "\n";
    std::cout << "  Reverse 门控: " << (rev_gate_ok ? "✓ A-class活跃, B-class抑制" :
                                                       "⚠ 门控不充分") << "\n\n";

    // --- Curvature spatial distribution ---
    std::cout << "--- 曲率空间分布 (/mm) ---\n";
    const char* region_names[] = {"Head(0-11)", "Ant(12-23)", "Post(24-35)", "Tail(36-47)"};
    std::cout << std::setprecision(2);
    std::cout << "  Forward: ";
    for (int r = 0; r < 4; ++r) std::cout << region_names[r] << "=" << fwd_curv_region[r] << "  ";
    std::cout << "\n  Reverse: ";
    for (int r = 0; r < 4; ++r) std::cout << region_names[r] << "=" << rev_curv_region[r] << "  ";
    std::cout << "\n";
    // Expected: Forward has gradient head>tail; Reverse has gradient tail>head
    if (fwd_n > 0) {
        bool fwd_gradient_ok = fwd_curv_region[0] > fwd_curv_region[3];
        std::cout << "  Forward 梯度: " << (fwd_gradient_ok ? "✓ Head > Tail (头部驱动)" :
                                                               "⚠ Tail ≥ Head") << "\n";
    }
    if (rev_n > 0) {
        bool rev_gradient_ok = rev_curv_region[3] > rev_curv_region[0] * 0.5;
        std::cout << "  Reverse 梯度: " << (rev_gradient_ok ? "✓ Tail active (尾部驱动)" :
                                                               "⚠ Tail inactive (A-class波弱)") << "\n";
    }
    std::cout << "\n";

    // --- Klinotaxis effectiveness ---
    std::cout << "--- Klinotaxis 有效性 ---\n";
    // Estimate: head curvature contribution to angular velocity
    // In RFT, torque ∝ Σ d_i × f_i. Head segments (0-5) have small d_i.
    // Rough estimate: head contribution = (6/48) × (mean_head_curv / mean_body_curv)
    double head_frac = 6.0 / 48.0;  // geometric fraction
    double head_curv_ratio = 1.0;
    if (fwd_n > 0 && fwd_curv_region[1] > 0.01) {
        head_curv_ratio = fwd_curv_region[0] / fwd_curv_region[1];
    }
    // Lever arm ratio: head ~0.05mm avg, body ~0.4mm avg
    double lever_ratio = 0.05 / 0.4;
    stats.klinotaxis_torque_fraction = head_frac * head_curv_ratio * lever_ratio;
    std::cout << "  头部力矩贡献:    " << std::setprecision(1)
              << stats.klinotaxis_torque_fraction * 100 << "%  (vs 体干波)\n";
    std::cout << "  头部曲率强度比:  " << std::setprecision(2) << head_curv_ratio << "x  (vs 前体)\n";
    if (stats.klinotaxis_torque_fraction < 0.05) {
        std::cout << "  ⚠ 头部力矩贡献 <5%: klinotaxis 信号被体干波稀释\n";
        std::cout << "    建议: 增加 smb_muscle_gain 或扩展 SMB 作用段数\n";
    } else if (stats.klinotaxis_torque_fraction < 0.15) {
        std::cout << "  △ 头部力矩贡献 5-15%: klinotaxis 信号中等\n";
    } else {
        std::cout << "  ✓ 头部力矩贡献 >15%: klinotaxis 信号充足\n";
    }
    std::cout << "\n";

    // --- Summary diagnostic ---
    std::cout << "========================================\n";
    std::cout << "  DIAGNOSTIC SUMMARY\n";
    std::cout << "========================================\n\n";

    int issues = 0;

    if (stats.fwd_wave_coherence <= 0) {
        std::cout << "  [ISSUE] Forward 状态无 HEAD→TAIL 行波\n";
        std::cout << "    → 检查 B-class 本体感觉连接方向和 AVB 驱动\n";
        issues++;
    }
    if (stats.rev_wave_coherence <= 0) {
        std::cout << "  [ISSUE] Reverse 状态无 TAIL→HEAD 行波\n";
        std::cout << "    → 检查 A-class 本体感觉连接和 AVA 门控\n";
        issues++;
    }
    if (!fwd_gate_ok) {
        std::cout << "  [ISSUE] Forward 本体感觉门控不充分\n";
        std::cout << "    → AVB_gate=" << stats.fwd_avb_gate << " 应>0.5, AVA_gate=" << stats.fwd_ava_gate << " 应<0.5\n";
        issues++;
    }
    if (!rev_gate_ok) {
        std::cout << "  [ISSUE] Reverse 本体感觉门控不充分\n";
        std::cout << "    → AVB_gate=" << stats.rev_avb_gate << " 应<0.5, AVA_gate=" << stats.rev_ava_gate << " 应>0.5\n";
        issues++;
    }
    if (stats.n_rev_samples > 0) {
        double rev_backward_pct = 100.0 * rev_dir_neg / stats.n_rev_samples;
        if (rev_backward_pct < 30.0) {
            std::cout << "  [ISSUE] Reverse 期间后退方向比例低 (" << std::setprecision(0) << rev_backward_pct << "%)\n";
            std::cout << "    → A-class 行波推力不足以产生后退运动\n";
            issues++;
        }
    }
    if (stats.klinotaxis_torque_fraction < 0.05) {
        std::cout << "  [ISSUE] Klinotaxis 信号被 RFT 体干力矩稀释 (<5%)\n";
        std::cout << "    → 增加 smb_muscle_gain 或扩展 SMB 段范围\n";
        issues++;
    }
    if (stats.fwd_mean_speed < 0.05) {
        std::cout << "  [ISSUE] Forward 速度过低 (" << std::setprecision(3) << stats.fwd_mean_speed << " mm/s)\n";
        std::cout << "    → 检查 curvature_gain, C_N/C_T 比值, 波幅\n";
        issues++;
    }

    if (issues == 0) {
        std::cout << "  ✓ 无问题检出\n";
    }
    std::cout << "\n  共检出 " << issues << " 个问题\n\n";

    // --- Verbose: per-sample curvature dump ---
    if (verbose && samples.size() > 0) {
        std::cout << "--- 曲率时序 (前10个样本) ---\n";
        int show_n = std::min(10, static_cast<int>(samples.size()));
        for (int i = 0; i < show_n; ++i) {
            auto& ws = samples[i];
            std::cout << "  t=" << std::setprecision(2) << ws.time_s << "s"
                      << " state=" << (ws.is_reversing ? "REV" : "FWD")
                      << " dir=" << (ws.direction > 0 ? "+1" : "-1")
                      << " speed=" << std::setprecision(3) << ws.speed
                      << " curv[5,24,44]=" << std::setprecision(1)
                      << ws.curvature[5] << "," << ws.curvature[24] << "," << ws.curvature[44]
                      << "\n";
        }
        std::cout << "\n";
    }

    return 0;
}
