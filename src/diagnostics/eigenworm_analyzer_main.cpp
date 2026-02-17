// eigenworm_analyzer_main.cpp — Eigenworm PCA + Swimming Gait Analysis
//
// Performs Principal Component Analysis on body posture (tangent angles)
// to extract eigenworms (Stephens 2008). Also measures body wave frequency
// for gait classification (crawling vs swimming).
//
// Usage: eigenworm_analyzer [--duration N] [--seed N] [--viscosity V]
//                           [--verbose] [--export FILE] [--help]
//
// REF: Stephens 2008 PLoS Comput Biol — 4 eigenworms capture >95% variance
//      Brown 2013 PNAS — eigenworm dynamics
//      Fang-Yen 2010 JEM — crawl/swim gait transition
//      Berri 2009 — swimming kinematics
//      Pierce-Shimomura 2008 PNAS — gait modulation by viscosity

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
#include <fstream>

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
// Jacobi eigenvalue decomposition for symmetric matrices
// ================================================================
// Input:  C — NxN symmetric matrix (modified in place to diagonal)
//         V — NxN matrix (output: columns are eigenvectors)
//         d — N eigenvalues (output, sorted descending)
// Returns number of iterations used.
static int jacobi_eigen(std::vector<std::vector<double>>& C,
                         std::vector<std::vector<double>>& V,
                         std::vector<double>& d, int N) {
    // Initialize V to identity
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            V[i][j] = (i == j) ? 1.0 : 0.0;

    const int MAX_ITER = 200;
    int iterations = 0;

    for (int iter = 0; iter < MAX_ITER; iter++) {
        iterations = iter + 1;

        // Sum of off-diagonal elements squared
        double off_diag_sum = 0.0;
        for (int i = 0; i < N; i++)
            for (int j = i + 1; j < N; j++)
                off_diag_sum += C[i][j] * C[i][j];

        if (off_diag_sum < 1e-20) break;  // converged

        // Sweep: rotate each (p,q) pair
        for (int p = 0; p < N - 1; p++) {
            for (int q = p + 1; q < N; q++) {
                if (std::abs(C[p][q]) < 1e-15) continue;

                // Compute rotation angle
                double tau_val, t, c, s;
                double diff = C[q][q] - C[p][p];
                if (std::abs(diff) < 1e-15) {
                    t = 1.0;  // theta = pi/4
                } else {
                    tau_val = diff / (2.0 * C[p][q]);
                    t = ((tau_val >= 0) ? 1.0 : -1.0) /
                        (std::abs(tau_val) + std::sqrt(1.0 + tau_val * tau_val));
                }
                c = 1.0 / std::sqrt(1.0 + t * t);
                s = t * c;
                double tau_rot = s / (1.0 + c);

                // Update matrix C
                double app = C[p][p] - t * C[p][q];
                double aqq = C[q][q] + t * C[p][q];
                C[p][q] = 0.0;
                C[q][p] = 0.0;
                C[p][p] = app;
                C[q][q] = aqq;

                for (int r = 0; r < N; r++) {
                    if (r == p || r == q) continue;
                    double crp = C[r][p];
                    double crq = C[r][q];
                    C[r][p] = crp - s * (crq + tau_rot * crp);
                    C[p][r] = C[r][p];
                    C[r][q] = crq + s * (crp - tau_rot * crq);
                    C[q][r] = C[r][q];
                }

                // Update eigenvector matrix V
                for (int r = 0; r < N; r++) {
                    double vrp = V[r][p];
                    double vrq = V[r][q];
                    V[r][p] = vrp - s * (vrq + tau_rot * vrp);
                    V[r][q] = vrq + s * (vrp - tau_rot * vrq);
                }
            }
        }
    }

    // Extract eigenvalues and sort descending
    d.resize(N);
    for (int i = 0; i < N; i++) d[i] = C[i][i];

    // Sort by eigenvalue descending (bubble sort — N=48, fine)
    for (int i = 0; i < N - 1; i++) {
        for (int j = i + 1; j < N; j++) {
            if (d[j] > d[i]) {
                std::swap(d[i], d[j]);
                for (int r = 0; r < N; r++)
                    std::swap(V[r][i], V[r][j]);
            }
        }
    }

    return iterations;
}

// ================================================================
// Frequency estimation via autocorrelation
// ================================================================
// Returns dominant frequency in Hz given a time series sampled at sample_dt (seconds)
static double estimate_frequency(const std::vector<double>& signal, double sample_dt) {
    int n = static_cast<int>(signal.size());
    if (n < 20) return 0.0;

    // Subtract mean
    double mean = 0.0;
    for (double v : signal) mean += v;
    mean /= n;

    std::vector<double> centered(n);
    for (int i = 0; i < n; i++) centered[i] = signal[i] - mean;

    // Compute variance
    double var = 0.0;
    for (int i = 0; i < n; i++) var += centered[i] * centered[i];
    if (var < 1e-15) return 0.0;

    // Autocorrelation — search for first positive peak after first zero crossing
    // Min lag: 0.1s (10 Hz max), Max lag: 5s (0.2 Hz min)
    int min_lag = std::max(1, static_cast<int>(0.1 / sample_dt));
    int max_lag = std::min(n / 2, static_cast<int>(5.0 / sample_dt));

    // Find first zero crossing (autocorrelation goes negative)
    int zero_cross = min_lag;
    for (int lag = 1; lag < max_lag; lag++) {
        double acf = 0.0;
        for (int i = 0; i < n - lag; i++)
            acf += centered[i] * centered[i + lag];
        if (acf < 0.0) { zero_cross = lag; break; }
    }

    // Find peak after zero crossing
    double best_acf = -1e30;
    int best_lag = zero_cross;
    for (int lag = zero_cross; lag < max_lag; lag++) {
        double acf = 0.0;
        for (int i = 0; i < n - lag; i++)
            acf += centered[i] * centered[i + lag];
        acf /= var;
        if (acf > best_acf) {
            best_acf = acf;
            best_lag = lag;
        }
        // Stop after peak starts declining
        if (acf < best_acf - 0.05 && best_acf > 0.1) break;
    }

    if (best_acf < 0.05) return 0.0;  // no clear periodicity
    return 1.0 / (best_lag * sample_dt);
}

// ================================================================
// Zero-crossing frequency (backup method)
// ================================================================
static double zero_crossing_frequency(const std::vector<double>& signal, double sample_dt) {
    int n = static_cast<int>(signal.size());
    if (n < 10) return 0.0;

    double mean = 0.0;
    for (double v : signal) mean += v;
    mean /= n;

    int crossings = 0;
    for (int i = 1; i < n; i++) {
        if ((signal[i] - mean) * (signal[i-1] - mean) < 0.0)
            crossings++;
    }

    double duration = (n - 1) * sample_dt;
    return (crossings / 2.0) / duration;  // each full cycle has 2 zero crossings
}

// ================================================================
// Compact single-viscosity analysis (for sweep mode)
// ================================================================
struct GaitMetrics {
    double viscosity;
    double frequency;      // median body wave frequency (Hz)
    double speed;           // mean speed (mm/s)
    double pca_top4_pct;   // top-4 eigenworm variance %
    double pca_concentration; // vs uniform baseline
    int fwd_pct;           // forward frame percentage
    std::string gait;      // gait label
};

static GaitMetrics run_gait_analysis(double viscosity, double duration_s, int seed) {
    GaitMetrics m{};
    m.viscosity = viscosity;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);
    if (viscosity != 1.0) sim.set_medium_viscosity(viscosity);

    double dt = sim.dt();
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt);
    int warmup_steps = static_cast<int>(5000.0 / dt);
    constexpr int SAMPLE_EVERY = 20;
    double sample_dt_s = SAMPLE_EVERY * dt / 1000.0;
    constexpr int N = NUM_BODY_SEGMENTS;
    constexpr int D = N;

    std::vector<std::array<double, N>> curv_fwd;
    std::vector<double> curv_seg24, speeds;
    int fwd_total = 0, rev_total = 0, consec_fwd = 0;

    for (int step = 0; step < total_steps; ++step) {
        sim.step();
        if (step < warmup_steps || step % SAMPLE_EVERY != 0) continue;

        auto& segs = sim.body().segments();
        bool rev = sim.is_reversing();
        if (rev) { rev_total++; consec_fwd = 0; } else { fwd_total++; consec_fwd++; }

        std::array<double, N> curvs;
        for (int i = 0; i < N; i++) curvs[i] = segs[i].curvature;
        if (!rev && consec_fwd > 50) curv_fwd.push_back(curvs);

        curv_seg24.push_back(segs[24].curvature);
        speeds.push_back(sim.body().get_speed());
    }

    int M_all = fwd_total + rev_total;
    m.fwd_pct = M_all > 0 ? static_cast<int>(100.0 * fwd_total / M_all) : 0;

    // Frequency (mid-body seg 24 autocorrelation)
    double f_ac = estimate_frequency(curv_seg24, sample_dt_s);
    double f_zc = zero_crossing_frequency(curv_seg24, sample_dt_s);
    m.frequency = (f_ac > 0.1) ? f_ac : f_zc;

    // Speed
    double sum_spd = 0; for (double s : speeds) sum_spd += s;
    m.speed = speeds.empty() ? 0 : sum_spd / speeds.size();

    // PCA (correlation matrix, forward-only)
    int M = static_cast<int>(curv_fwd.size());
    if (M < 50) { m.pca_top4_pct = 0; m.pca_concentration = 0; m.gait = "N/A"; return m; }

    std::vector<double> col_mean(D, 0.0), col_std(D, 0.0);
    for (int j = 0; j < D; j++) {
        for (int i = 0; i < M; i++) col_mean[j] += curv_fwd[i][j];
        col_mean[j] /= M;
    }
    for (int j = 0; j < D; j++) {
        for (int i = 0; i < M; i++) { double d = curv_fwd[i][j] - col_mean[j]; col_std[j] += d*d; }
        col_std[j] = std::sqrt(col_std[j] / (M - 1));
        if (col_std[j] < 1e-12) col_std[j] = 1.0;
    }
    std::vector<std::vector<double>> cov(D, std::vector<double>(D, 0.0));
    for (int j1 = 0; j1 < D; j1++)
        for (int j2 = j1; j2 < D; j2++) {
            double s = 0;
            for (int i = 0; i < M; i++)
                s += ((curv_fwd[i][j1]-col_mean[j1])/col_std[j1]) * ((curv_fwd[i][j2]-col_mean[j2])/col_std[j2]);
            cov[j1][j2] = s / (M-1); cov[j2][j1] = cov[j1][j2];
        }
    std::vector<std::vector<double>> eigvecs(D, std::vector<double>(D, 0.0));
    std::vector<double> eigvals(D, 0.0);
    jacobi_eigen(cov, eigvecs, eigvals, D);

    double total_var = 0;
    for (int i = 0; i < D; i++) if (eigvals[i] > 0) total_var += eigvals[i];
    double top4 = 0;
    for (int i = 0; i < 4 && i < D; i++) top4 += eigvals[i];
    m.pca_top4_pct = total_var > 0 ? 100.0 * top4 / total_var : 0;
    double uniform4 = 100.0 * 4.0 / D;
    m.pca_concentration = uniform4 > 0 ? m.pca_top4_pct / uniform4 : 0;

    // Gait label
    if (m.frequency < 0.3) m.gait = "静止";
    else if (m.frequency < 1.0) m.gait = "爬行";
    else if (m.frequency < 1.4) m.gait = "过渡";
    else m.gait = "游泳";

    return m;
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    double duration_s = 60.0;
    int seed = 42;
    double viscosity = 1.0;
    bool verbose = false;
    bool sweep_mode = false;
    std::string export_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--duration" || arg == "-d") && i + 1 < argc) {
            duration_s = std::stod(argv[++i]);
        } else if ((arg == "--seed" || arg == "-s") && i + 1 < argc) {
            seed = std::stoi(argv[++i]);
        } else if (arg == "--viscosity" && i + 1 < argc) {
            viscosity = std::stod(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--sweep") {
            sweep_mode = true;
        } else if (arg == "--export" && i + 1 < argc) {
            export_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: eigenworm_analyzer [OPTIONS]\n"
                      << "  --duration N    Simulation duration in seconds (default: 60)\n"
                      << "  --seed N        Random seed (default: 42)\n"
                      << "  --viscosity V   Medium viscosity: 1.0=agar, 0.01=water (default: 1.0)\n"
                      << "  --sweep         Sweep viscosity 0.01-1.0 (10 points), output comparison table\n"
                      << "  --verbose       Show per-eigenworm details\n"
                      << "  --export FILE   Export eigenworms to CSV\n"
                      << "  --help          Show this help\n";
            return 0;
        }
    }

    Logger::instance().set_level(LogLevel::WARN);

    // ================================================================
    // Sweep mode: viscosity 0.01 → 1.0
    // ================================================================
    if (sweep_mode) {
        std::vector<double> visc_points = {0.01, 0.03, 0.05, 0.1, 0.2, 0.3, 0.5, 0.7, 0.85, 1.0};
        double sweep_dur = std::min(duration_s, 30.0);  // cap per-point duration

        std::cout << "========================================\n";
        std::cout << "  黏度连续扫描 (Viscosity Sweep)\n";
        std::cout << "========================================\n\n";
        std::cout << "  扫描点数:    " << visc_points.size() << "\n";
        std::cout << "  每点时长:    " << sweep_dur << " s\n";
        std::cout << "  随机种子:    " << seed << "\n\n";

        std::vector<GaitMetrics> results;
        for (size_t idx = 0; idx < visc_points.size(); idx++) {
            double v = visc_points[idx];
            std::cout << "  [" << (idx+1) << "/" << visc_points.size()
                      << "] viscosity=" << std::fixed << std::setprecision(2) << v
                      << " ... " << std::flush;
            auto m = run_gait_analysis(v, sweep_dur, seed);
            results.push_back(m);
            std::cout << std::setprecision(2) << m.frequency << " Hz, "
                      << std::setprecision(3) << m.speed << " mm/s  "
                      << m.gait << "\n";
        }

        // Summary table
        std::cout << "\n========================================\n";
        std::cout << "  黏度-步态对照表\n";
        std::cout << "========================================\n\n";
        std::cout << "  黏度    频率(Hz)  速度(mm/s)  PCA集中度  前进%  步态\n";
        std::cout << "  ------  --------  ----------  ---------  -----  ------\n";
        for (auto& r : results) {
            std::cout << "  " << std::fixed << std::setprecision(2) << std::setw(6) << r.viscosity
                      << "  " << std::setprecision(2) << std::setw(8) << r.frequency
                      << "  " << std::setprecision(3) << std::setw(10) << r.speed
                      << "  " << std::setprecision(1) << std::setw(7) << r.pca_concentration << "x"
                      << "  " << std::setw(4) << r.fwd_pct << "%"
                      << "  " << r.gait << "\n";
        }

        // ASCII frequency-viscosity curve
        std::cout << "\n========================================\n";
        std::cout << "  频率-黏度曲线 (ASCII)\n";
        std::cout << "========================================\n\n";

        double f_max = 0;
        for (auto& r : results) f_max = std::max(f_max, r.frequency);
        f_max = std::max(f_max, 0.5);  // minimum scale
        int chart_h = 12;  // rows
        int chart_w = static_cast<int>(results.size());

        for (int row = chart_h; row >= 0; row--) {
            double threshold = f_max * row / chart_h;
            if (row == chart_h)
                std::cout << "  " << std::fixed << std::setprecision(1) << std::setw(4) << f_max << " Hz |";
            else if (row == chart_h / 2)
                std::cout << "  " << std::setprecision(1) << std::setw(4) << f_max * 0.5 << " Hz |";
            else if (row == 0)
                std::cout << "   0.0 Hz |";
            else
                std::cout << "          |";

            for (int c = 0; c < chart_w; c++) {
                if (results[c].frequency >= threshold)
                    std::cout << " \xe2\x96\x88\xe2\x96\x88";
                else
                    std::cout << "   ";
            }
            std::cout << "\n";
        }
        std::cout << "          +";
        for (int c = 0; c < chart_w; c++) std::cout << "---";
        std::cout << "\n          ";
        for (auto& r : results)
            std::cout << " " << std::setprecision(1) << std::setw(2)
                      << (r.viscosity < 0.095 ? std::to_string(static_cast<int>(r.viscosity * 100))
                         : std::to_string(static_cast<int>(r.viscosity * 10)))
                      << "";
        std::cout << "  (viscosity " "\xc3\x97" "100 or " "\xc3\x97" "10)\n";

        // Conclusion
        std::cout << "\n  结论: ";
        double f_water = results.front().frequency;
        double f_agar  = results.back().frequency;
        if (f_water > f_agar * 1.5) {
            double ratio = f_water / std::max(f_agar, 0.01);
            std::cout << "频率随黏度降低而升高 (" << std::setprecision(1) << ratio
                      << "x), 步态连续过渡 ✓\n";
        } else {
            std::cout << "步态差异不显著, 可能需要调参\n";
        }

        // Export sweep results to CSV if requested
        if (!export_file.empty()) {
            std::ofstream ofs(export_file);
            if (ofs.is_open()) {
                ofs << "viscosity,frequency_hz,speed_mm_s,pca_top4_pct,pca_concentration,fwd_pct,gait\n";
                for (auto& r : results) {
                    ofs << r.viscosity << "," << r.frequency << "," << r.speed << ","
                        << r.pca_top4_pct << "," << r.pca_concentration << ","
                        << r.fwd_pct << "," << r.gait << "\n";
                }
                ofs.close();
                std::cout << "  扫描结果已导出: " << export_file << "\n";
            }
        }

        std::cout << "\n";
        return 0;
    }

    // Determine gait label
    std::string medium_label;
    if (viscosity >= 0.8) medium_label = "agar (crawling)";
    else if (viscosity >= 0.1) medium_label = "intermediate";
    else medium_label = "water (swimming)";

    std::cout << "========================================\n";
    std::cout << "  Eigenworm 分析器\n";
    std::cout << "  Eigenworm Analyzer + Gait Analysis\n";
    std::cout << "========================================\n\n";
    std::cout << "  仿真时长:    " << duration_s << " s\n";
    std::cout << "  随机种子:    " << seed << "\n";
    std::cout << "  介质黏度:    " << viscosity << " (" << medium_label << ")\n";

    // --- Setup simulation ---
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    // Apply medium viscosity AFTER initialization
    if (viscosity != 1.0) {
        sim.set_medium_viscosity(viscosity);
    }

    double dt = sim.dt();
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt);
    int warmup_steps = static_cast<int>(5000.0 / dt);  // 5s warmup
    constexpr int SAMPLE_EVERY = 20;  // every 20 steps = 10ms at dt=0.5ms
    double sample_dt_s = SAMPLE_EVERY * dt / 1000.0;

    std::cout << "  采样间隔:    " << sample_dt_s * 1000.0 << " ms\n";

    // --- Data collection ---
    constexpr int N = NUM_BODY_SEGMENTS;  // 48
    constexpr int D = N;                  // PCA dimensionality (all segments)

    // All-frame curvature + forward-only curvature for PCA
    std::vector<std::array<double, N>> curv_all;
    std::vector<std::array<double, N>> curv_fwd;  // forward locomotion only
    // Frequency analysis: curvature time series at multiple segments
    std::vector<double> curv_seg8;       // anterior body
    std::vector<double> curv_seg16;      // anterior-mid
    std::vector<double> curv_seg24;      // mid body
    std::vector<double> curv_seg36;      // posterior
    std::vector<double> speeds;
    int fwd_samples = 0, rev_samples = 0;
    int consec_fwd = 0;  // consecutive forward frames (skip reversal transitions)

    curv_all.reserve(total_steps / SAMPLE_EVERY);
    curv_fwd.reserve(total_steps / SAMPLE_EVERY);

    std::cout << "\n  运行仿真... " << std::flush;

    for (int step = 0; step < total_steps; ++step) {
        sim.step();

        if (step < warmup_steps) continue;  // skip warmup
        if (step % SAMPLE_EVERY != 0) continue;

        auto& segs = sim.body().segments();
        bool reversing = sim.is_reversing();
        if (reversing) { rev_samples++; consec_fwd = 0; }
        else { fwd_samples++; consec_fwd++; }

        // Curvature profile
        std::array<double, N> curvs;
        for (int i = 0; i < N; i++)
            curvs[i] = segs[i].curvature;
        curv_all.push_back(curvs);

        // Forward-only: skip first 50 frames (~0.5s) after reversal ends
        if (!reversing && consec_fwd > 50) {
            curv_fwd.push_back(curvs);
        }

        // Curvature time series for frequency analysis
        curv_seg8.push_back(segs[8].curvature);
        curv_seg16.push_back(segs[16].curvature);
        curv_seg24.push_back(segs[24].curvature);
        curv_seg36.push_back(segs[36].curvature);
        speeds.push_back(sim.body().get_speed());
    }

    int M_all = static_cast<int>(curv_all.size());
    int M_fwd = static_cast<int>(curv_fwd.size());
    std::cout << "完成\n";
    std::cout << "  总采样:      " << M_all << "\n";
    std::cout << "  前进采样:    " << M_fwd << " (稳态前进, 排除反转过渡)\n\n";

    // Use forward-only if enough samples, otherwise all
    auto& pca_data = (M_fwd >= 100) ? curv_fwd : curv_all;
    int M = static_cast<int>(pca_data.size());
    std::string pca_source = (M_fwd >= 100) ? "forward-only" : "all-frames";

    if (M < 50) {
        std::cerr << "  错误: 采样数量不足 (<50), 请增加仿真时长\n";
        return 1;
    }

    // ================================================================
    // Correlation-based PCA on curvature profiles
    // ================================================================
    // Use CORRELATION matrix (standardized covariance) instead of raw covariance.
    // This normalizes each segment's contribution equally, preventing head
    // curvature (SMD steering) from dominating. Curvature is heading-invariant.
    // REF: Stephens et al. 2008 PLoS Comput Biol — eigenworm decomposition
    //      (adapted: they used tangent angles; we use curvature + correlation)

    std::cout << "  PCA on " << pca_source << " curvature (D=" << D
              << ", correlation matrix)...\n";

    // 1. Compute column means and standard deviations
    std::vector<double> col_mean(D, 0.0);
    std::vector<double> col_std(D, 0.0);
    for (int j = 0; j < D; j++) {
        for (int i = 0; i < M; i++)
            col_mean[j] += pca_data[i][j];
        col_mean[j] /= M;
    }
    for (int j = 0; j < D; j++) {
        for (int i = 0; i < M; i++) {
            double d = pca_data[i][j] - col_mean[j];
            col_std[j] += d * d;
        }
        col_std[j] = std::sqrt(col_std[j] / (M - 1));
        if (col_std[j] < 1e-12) col_std[j] = 1.0;  // avoid div-by-zero for inactive segments
    }

    // 2. Compute DxD correlation matrix (standardized covariance)
    std::vector<std::vector<double>> cov(D, std::vector<double>(D, 0.0));
    for (int j1 = 0; j1 < D; j1++) {
        for (int j2 = j1; j2 < D; j2++) {
            double sum = 0.0;
            for (int i = 0; i < M; i++) {
                sum += ((pca_data[i][j1] - col_mean[j1]) / col_std[j1]) *
                       ((pca_data[i][j2] - col_mean[j2]) / col_std[j2]);
            }
            cov[j1][j2] = sum / (M - 1);
            cov[j2][j1] = cov[j1][j2];
        }
    }
    std::cout << "  协方差矩阵完成. ";

    // 3. Jacobi eigenvalue decomposition
    std::vector<std::vector<double>> eigvecs(D, std::vector<double>(D, 0.0));
    std::vector<double> eigvals(D, 0.0);
    int iters = jacobi_eigen(cov, eigvecs, eigvals, D);
    std::cout << "Jacobi 完成 (" << iters << " 次迭代)\n\n";

    // 4. Compute variance explained
    double total_var = 0.0;
    for (int i = 0; i < D; i++) {
        if (eigvals[i] > 0) total_var += eigvals[i];
    }

    // ================================================================
    // Output: Eigenworm variance table
    // ================================================================
    std::cout << "========================================\n";
    std::cout << "  主成分分析 (PCA)\n";
    std::cout << "========================================\n\n";

    int show_n = verbose ? std::min(D, 20) : std::min(D, 10);
    double cumulative = 0.0;

    std::cout << "  Eigenworm  方差解释    累积方差\n";
    std::cout << "  ---------  ---------  ---------\n";

    double var_top4 = 0.0;
    for (int i = 0; i < show_n; i++) {
        double pct = (total_var > 0) ? 100.0 * eigvals[i] / total_var : 0.0;
        cumulative += pct;
        if (i < 4) var_top4 += pct;

        std::cout << "  EW" << std::setw(2) << (i + 1)
                  << "       " << std::fixed << std::setprecision(1)
                  << std::setw(5) << pct << "%"
                  << "      " << std::setw(5) << cumulative << "%\n";
    }

    // Top-4 summary
    // NOTE: Using correlation PCA (standardized). Stephens 2008 used covariance PCA
    // on tangent angles → >95% in top 4. Correlation PCA distributes variance more
    // evenly, so the threshold is different: top-4 capturing >40% (vs uniform ~8.3%×4)
    // indicates significant low-dimensionality.
    double uniform_top4 = 100.0 * 4.0 / D;  // expected if all components equal
    std::cout << "\n  前 4 个 Eigenworms 解释: " << std::fixed << std::setprecision(1)
              << var_top4 << "% (相关矩阵)\n";
    std::cout << "  均匀基准 (无结构):  " << std::setprecision(1) << uniform_top4 << "%\n";
    std::cout << "  集中度:             " << std::setprecision(1)
              << var_top4 / uniform_top4 << "x (vs 均匀)\n";

    if (var_top4 > uniform_top4 * 5.0) {
        std::cout << "  ✓ 高度低维 — 姿态空间强烈集中于少数模态\n";
    } else if (var_top4 > uniform_top4 * 3.0) {
        std::cout << "  ✓ 低维姿态空间 — 主成分显著高于均匀分布\n";
    } else if (var_top4 > uniform_top4 * 2.0) {
        std::cout << "  △ 中等低维 — 存在主导模态但结构不够集中\n";
    } else {
        std::cout << "  ✗ 接近均匀 — 未观察到低维姿态空间\n";
    }

    std::cout << "\n  PCA 数据源:  " << pca_source << " (" << M << " 帧)\n";
    std::cout << "  前进帧:      " << fwd_samples << " (" << std::setprecision(0) << std::fixed
              << 100.0 * fwd_samples / M_all << "%)\n";
    std::cout << "  后退帧:      " << rev_samples << " (" << 100.0 * rev_samples / M_all << "%)\n\n";

    // ================================================================
    // Output: Wave frequency analysis
    // ================================================================
    std::cout << "========================================\n";
    std::cout << "  波形频率分析\n";
    std::cout << "========================================\n\n";

    // Measure frequency at 4 segments, take median for robustness
    double freq8_ac  = estimate_frequency(curv_seg8, sample_dt_s);
    double freq16_ac = estimate_frequency(curv_seg16, sample_dt_s);
    double freq24_ac = estimate_frequency(curv_seg24, sample_dt_s);
    double freq36_ac = estimate_frequency(curv_seg36, sample_dt_s);
    double freq8_zc  = zero_crossing_frequency(curv_seg8, sample_dt_s);
    double freq16_zc = zero_crossing_frequency(curv_seg16, sample_dt_s);
    double freq24_zc = zero_crossing_frequency(curv_seg24, sample_dt_s);
    double freq36_zc = zero_crossing_frequency(curv_seg36, sample_dt_s);

    // Best estimate per segment: prefer autocorrelation if valid
    auto best_freq = [](double ac, double zc) { return (ac > 0.1) ? ac : zc; };
    double f8  = best_freq(freq8_ac, freq8_zc);
    double f16 = best_freq(freq16_ac, freq16_zc);
    double f24 = best_freq(freq24_ac, freq24_zc);
    double f36 = best_freq(freq36_ac, freq36_zc);

    // Median of 4 segments
    std::vector<double> freqs_all = {f8, f16, f24, f36};
    std::sort(freqs_all.begin(), freqs_all.end());
    double mid_freq = (freqs_all[1] + freqs_all[2]) * 0.5;
    double head_freq = f8;

    double mean_speed = 0.0;
    for (double s : speeds) mean_speed += s;
    mean_speed /= speeds.size();

    // Wavelength = speed / frequency (in body lengths)
    double wavelength_bl = (mid_freq > 0.01) ?
        (mean_speed / mid_freq) / sim.body().get_body_length() : 0.0;

    std::cout << "  各段曲率频率 (AC/ZC):\n";
    std::cout << "    seg 8  (前体):  " << std::fixed << std::setprecision(2)
              << freq8_ac << " / " << freq8_zc << " Hz\n";
    std::cout << "    seg 16 (前中):  " << freq16_ac << " / " << freq16_zc << " Hz\n";
    std::cout << "    seg 24 (中体):  " << freq24_ac << " / " << freq24_zc << " Hz\n";
    std::cout << "    seg 36 (后体):  " << freq36_ac << " / " << freq36_zc << " Hz\n";
    std::cout << "  中位频率:         " << mid_freq << " Hz\n";
    std::cout << "  平均速度:         " << std::setprecision(3) << mean_speed << " mm/s\n";
    std::cout << "  波长:             " << std::setprecision(2) << wavelength_bl << " 体长\n";

    // Muscle tau (inferred from viscosity)
    double muscle_tau = 30.0 * (0.3 + 0.7 * viscosity);
    std::cout << "  肌肉时间常数:     " << std::setprecision(1) << muscle_tau << " ms\n";
    std::cout << "  C_N/C_T 比值:     " << ((viscosity < 0.5) ? "2.0 (水)" : "1.5 (琼脂)") << "\n";

    // ================================================================
    // Gait classification
    // ================================================================
    std::cout << "\n========================================\n";
    std::cout << "  步态判定\n";
    std::cout << "========================================\n\n";

    std::cout << "  爬行步态 (crawling):  ~0.3-1.0 Hz  (琼脂, v=1.0)\n";
    std::cout << "  游泳步态 (swimming):  ~1.5-2.0 Hz  (水, v=0.01)\n\n";

    std::string gait;
    if (mid_freq < 0.3) {
        gait = "静止/异常";
    } else if (mid_freq < 1.0) {
        gait = "爬行 (crawling)";
    } else if (mid_freq < 1.4) {
        gait = "过渡 (intermediate)";
    } else {
        gait = "游泳 (swimming)";
    }
    std::cout << "  当前频率: " << std::setprecision(2) << mid_freq << " Hz → "
              << gait << "\n";

    // Gait transition prediction
    if (viscosity >= 0.8) {
        if (mid_freq >= 0.3 && mid_freq <= 1.2) {
            std::cout << "  ✓ 爬行步态与琼脂黏度一致\n";
        } else if (mid_freq > 1.2) {
            std::cout << "  △ 频率偏高: 琼脂上预期 <1.0 Hz\n";
        } else {
            std::cout << "  △ 频率偏低: 可能运动驱动不足\n";
        }
    } else if (viscosity < 0.1) {
        if (mid_freq >= 1.2) {
            std::cout << "  ✓ 游泳步态涌现 — 频率随黏度降低而升高\n";
        } else {
            std::cout << "  △ 未观察到游泳步态涌现 (预期 >1.2 Hz)\n";
            std::cout << "    可能原因: 本体感觉反馈环路饱和, 需进一步调参\n";
        }
    }

    // ================================================================
    // Export eigenworms to CSV
    // ================================================================
    if (!export_file.empty()) {
        std::ofstream ofs(export_file);
        if (ofs.is_open()) {
            ofs << "segment";
            for (int i = 0; i < std::min(D, 10); i++)
                ofs << ",EW" << (i + 1);
            ofs << "\n";

            for (int j = 0; j < D; j++) {
                ofs << (j + 1);  // segment index (1-based, skipping head)
                for (int i = 0; i < std::min(D, 10); i++)
                    ofs << "," << std::setprecision(6) << eigvecs[j][i];
                ofs << "\n";
            }
            ofs.close();
            std::cout << "\n  Eigenworms 已导出到: " << export_file << "\n";
        }
    }

    // ================================================================
    // Verbose: eigenworm shape preview
    // ================================================================
    if (verbose) {
        std::cout << "\n========================================\n";
        std::cout << "  Eigenworm 形态 (前4个)\n";
        std::cout << "========================================\n\n";

        for (int ew = 0; ew < 4 && ew < D; ew++) {
            std::cout << "  EW" << (ew + 1) << " ("
                      << std::setprecision(1) << 100.0 * eigvals[ew] / total_var << "%): ";

            // Show mini ASCII sparkline of eigenworm shape
            double ew_min = 1e30, ew_max = -1e30;
            for (int j = 0; j < D; j++) {
                ew_min = std::min(ew_min, eigvecs[j][ew]);
                ew_max = std::max(ew_max, eigvecs[j][ew]);
            }
            double range = ew_max - ew_min;
            if (range < 1e-10) range = 1.0;

            const char* blocks[] = {"\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83", "\xe2\x96\x84", "\xe2\x96\x85", "\xe2\x96\x86", "\xe2\x96\x87", "\xe2\x96\x88"};
            for (int j = 0; j < D; j += 2) {  // every other segment for compactness
                int level = static_cast<int>(7.0 * (eigvecs[j][ew] - ew_min) / range);
                level = std::clamp(level, 0, 7);
                std::cout << blocks[level];
            }
            std::cout << " (head\xe2\x86\x92tail)\n";
        }
    }

    std::cout << "\n";
    return 0;
}
