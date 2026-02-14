#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <map>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#undef ERROR
#endif

using namespace celegans;

// ================================================================
// 时间窗口内的神经群体状态快照
// ================================================================
struct PopulationSnapshot {
    double time_ms;
    std::vector<double> activations;  // 每个神经元的释放率
    double mean_activation;
    double sync_index;                // 同步指数 [0,1]
    double phi;                       // 信息整合度
    double sample_entropy;            // 样本熵
    double metastability;             // 亚稳态指标
};

// ================================================================
// 涌现事件
// ================================================================
struct EmergenceEvent {
    double time_ms;
    std::string type;     // SYNC_BURST, PHI_SPIKE, METASTABLE_TRANSITION, ENTROPY_DROP
    double magnitude;
    std::string detail;
};

// ================================================================
// 信息整合度 Φ (简化版 Tononi IIT)
// Φ = MI(whole) - max(MI(partition))
// 使用互信息近似: MI ≈ -0.5 * ln(det(C) / (det(C_A)*det(C_B)))
// 其中 C 是协方差矩阵
// ================================================================
class PhiCalculator {
public:
    // 从活动矩阵 (time x neurons) 计算 Φ
    static double compute(const std::vector<std::vector<double>>& activity) {
        int N = (int)activity[0].size();  // 神经元数
        int T = (int)activity.size();     // 时间点数
        if (N < 2 || T < N + 1) return 0;

        // 计算协方差矩阵
        auto cov = covariance_matrix(activity);
        double det_whole = log_determinant(cov);

        // 最小信息分割 (MIP): 尝试所有二分割, 找使 Φ 最小的
        // 对 N>10 只随机采样分割
        double min_phi = 1e30;
        int max_partitions = (N <= 10) ? (1 << (N - 1)) : 100;

        for (int p = 1; p < max_partitions; ++p) {
            std::vector<int> group_a, group_b;

            if (N <= 10) {
                // 枚举所有分割
                for (int j = 0; j < N; ++j) {
                    if (p & (1 << j)) group_a.push_back(j);
                    else group_b.push_back(j);
                }
            } else {
                // 随机分割
                for (int j = 0; j < N; ++j) {
                    if ((p * 7 + j * 13) % 3 < 2) group_a.push_back(j);
                    else group_b.push_back(j);
                }
            }

            if (group_a.empty() || group_b.empty()) continue;

            // 子矩阵的行列式
            auto cov_a = extract_submatrix(cov, group_a);
            auto cov_b = extract_submatrix(cov, group_b);

            double det_a = log_determinant(cov_a);
            double det_b = log_determinant(cov_b);

            // Φ = (det_A * det_B) / det_whole 的 log
            // 归一化: 除以 min(|A|, |B|)
            double phi_partition = (det_a + det_b - det_whole);
            int norm = std::min((int)group_a.size(), (int)group_b.size());
            if (norm > 0) phi_partition /= norm;

            if (phi_partition < min_phi) {
                min_phi = phi_partition;
            }
        }

        return std::max(0.0, min_phi);
    }

private:
    static std::vector<std::vector<double>> covariance_matrix(
            const std::vector<std::vector<double>>& data) {
        int N = (int)data[0].size();
        int T = (int)data.size();

        // 均值
        std::vector<double> means(N, 0);
        for (int t = 0; t < T; ++t)
            for (int i = 0; i < N; ++i)
                means[i] += data[t][i];
        for (int i = 0; i < N; ++i) means[i] /= T;

        // 协方差
        std::vector<std::vector<double>> cov(N, std::vector<double>(N, 0));
        for (int t = 0; t < T; ++t) {
            for (int i = 0; i < N; ++i) {
                for (int j = i; j < N; ++j) {
                    cov[i][j] += (data[t][i] - means[i]) * (data[t][j] - means[j]);
                }
            }
        }
        for (int i = 0; i < N; ++i) {
            for (int j = i; j < N; ++j) {
                cov[i][j] /= (T - 1);
                cov[j][i] = cov[i][j];
            }
            // 正则化：防止奇异矩阵
            cov[i][i] += 1e-8;
        }
        return cov;
    }

    static std::vector<std::vector<double>> extract_submatrix(
            const std::vector<std::vector<double>>& mat, const std::vector<int>& indices) {
        int n = (int)indices.size();
        std::vector<std::vector<double>> sub(n, std::vector<double>(n));
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j)
                sub[i][j] = mat[indices[i]][indices[j]];
        return sub;
    }

    // log(|det(A)|) via Cholesky (对称正定矩阵)
    static double log_determinant(const std::vector<std::vector<double>>& A) {
        int n = (int)A.size();
        if (n == 0) return 0;
        if (n == 1) return std::log(std::abs(A[0][0]) + 1e-15);

        // Cholesky: A = L * L^T
        std::vector<std::vector<double>> L(n, std::vector<double>(n, 0));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j <= i; ++j) {
                double sum = 0;
                for (int k = 0; k < j; ++k) sum += L[i][k] * L[j][k];
                if (i == j) {
                    double val = A[i][i] - sum;
                    if (val <= 0) val = 1e-15;
                    L[i][j] = std::sqrt(val);
                } else {
                    L[i][j] = (L[j][j] > 1e-15) ? (A[i][j] - sum) / L[j][j] : 0;
                }
            }
        }

        // log det(A) = 2 * sum(log(L_ii))
        double logdet = 0;
        for (int i = 0; i < n; ++i) {
            logdet += std::log(std::abs(L[i][i]) + 1e-15);
        }
        return 2.0 * logdet;
    }
};

// ================================================================
// 多尺度熵 (Costa 2002, MSE)
// 在不同时间尺度下计算样本熵
// ================================================================
class MultiscaleEntropyCalculator {
public:
    // 样本熵 SampEn(m, r, N)
    static double sample_entropy(const std::vector<double>& data, int m = 2, double r_factor = 0.2) {
        int N = (int)data.size();
        if (N < m + 2) return 0;

        double sd = std_dev(data);
        double r = r_factor * sd;
        if (r < 1e-10) return 0;

        int A = 0, B = 0;  // template matches for m+1 and m

        for (int i = 0; i < N - m; ++i) {
            for (int j = i + 1; j < N - m; ++j) {
                // Check m-length match
                bool match_m = true;
                for (int k = 0; k < m; ++k) {
                    if (std::abs(data[i + k] - data[j + k]) > r) {
                        match_m = false;
                        break;
                    }
                }
                if (match_m) {
                    B++;
                    // Check m+1 length match
                    if (i + m < N && j + m < N) {
                        if (std::abs(data[i + m] - data[j + m]) <= r) {
                            A++;
                        }
                    }
                }
            }
        }

        if (B == 0) return 0;
        return -std::log((double)A / B);
    }

    // 粗粒化: 将时间序列按 scale 因子平均
    static std::vector<double> coarse_grain(const std::vector<double>& data, int scale) {
        int N = (int)data.size() / scale;
        std::vector<double> result(N);
        for (int i = 0; i < N; ++i) {
            double sum = 0;
            for (int j = 0; j < scale; ++j) sum += data[i * scale + j];
            result[i] = sum / scale;
        }
        return result;
    }

    // 多尺度熵: 对不同尺度计算 SampEn
    static std::vector<double> compute(const std::vector<double>& data, int max_scale = 10) {
        std::vector<double> mse;
        for (int s = 1; s <= max_scale; ++s) {
            auto cg = coarse_grain(data, s);
            if (cg.size() < 30) break;  // 数据不够
            double se = sample_entropy(cg, 2, 0.2);
            mse.push_back(se);
        }
        return mse;
    }

private:
    static double std_dev(const std::vector<double>& v) {
        double m = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
        double sq = 0;
        for (double x : v) sq += (x - m) * (x - m);
        return std::sqrt(sq / v.size());
    }
};

// ================================================================
// 亚稳态检测: Kuramoto 序参量的时间变异性
// 同步指数 R(t) 的标准差 = metastability (Shanahan 2010)
// R(t) = |1/N * sum(exp(i*θ_j(t)))|
// θ_j(t) = 由 Hilbert 变换近似 (此处用释放率相位近似)
// ================================================================
class MetastabilityCalculator {
public:
    // 从释放率序列计算同步指数 R(t)
    static double sync_index(const std::vector<double>& activations) {
        int N = (int)activations.size();
        if (N == 0) return 0;

        // 将释放率映射为相位 [0, 2π]
        double sum_cos = 0, sum_sin = 0;
        for (double a : activations) {
            double phase = a * 2.0 * M_PI;  // 释放率 [0,1] → 相位 [0,2π]
            sum_cos += std::cos(phase);
            sum_sin += std::sin(phase);
        }
        return std::sqrt(sum_cos * sum_cos + sum_sin * sum_sin) / N;
    }

    // 亚稳态 = R(t) 的标准差
    static double compute(const std::vector<double>& sync_series) {
        if (sync_series.size() < 2) return 0;
        double mean = std::accumulate(sync_series.begin(), sync_series.end(), 0.0) / sync_series.size();
        double sq = 0;
        for (double r : sync_series) sq += (r - mean) * (r - mean);
        return std::sqrt(sq / sync_series.size());
    }
};

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Logger::instance().set_level(LogLevel::ERROR);

    double duration = 30.0;
    unsigned int seed = 123;
    int window_ms = 2000;     // Φ 计算窗口
    int mse_max_scale = 8;
    std::string export_file;
    std::vector<std::string> target_neurons;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i+1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--window" && i+1 < argc) {
            window_ms = std::atoi(argv[++i]);
        } else if (arg == "--neuron" && i+1 < argc) {
            target_neurons.push_back(argv[++i]);
        } else if (arg == "--export" && i+1 < argc) {
            export_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: emergence_detector [options]\n\n"
                      << "Detect emergent properties in neural population\n\n"
                      << "Options:\n"
                      << "  --neuron <name>      Add neuron to analysis (repeatable)\n"
                      << "  --duration <sec>     Simulation duration (default: 30)\n"
                      << "  --seed <n>           RNG seed (default: 123)\n"
                      << "  --window <ms>        Phi window size (default: 2000)\n"
                      << "  --export <csv>       Export time series to CSV\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Metrics:\n"
                      << "  Phi (IIT):           Information integration\n"
                      << "  MSE:                 Multiscale sample entropy\n"
                      << "  Metastability:       Sync index variance (Shanahan 2010)\n"
                      << "  Emergence Events:    Auto-detected sync/desync transitions\n";
            return 0;
        }
    }

    // 默认: 命令神经元 + 感觉-运动回路核心
    if (target_neurons.empty()) {
        target_neurons = {
            "AVAL", "AVAR", "AVBL", "AVBR",   // 命令神经元
            "ASEL", "ASER",                     // 感觉神经元
            "AIBL", "AIAL",                     // 中间神经元
            "RIVL", "RIVR",                     // omega 控制
            "SMDDL", "SMDVL"                    // 运动神经元
        };
    }

    std::cout << "========================================\n";
    std::cout << "  Emergence Detector\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration:   " << duration << " s\n";
    std::cout << "  Window:     " << window_ms << " ms\n";
    std::cout << "  Population: " << target_neurons.size() << " neurons\n\n";

    // === 数据采集 ===
    std::cout << "  [1/4] Collecting population activity... " << std::flush;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    sim.reset_transducers();

    const auto& conn = sim.connectome();
    std::vector<int> neuron_ids;
    std::vector<std::string> valid_names;
    for (const auto& name : target_neurons) {
        int id = conn.get_neuron_id(name);
        if (id >= 0) {
            neuron_ids.push_back(id);
            valid_names.push_back(name);
        }
    }
    int pop_size = (int)neuron_ids.size();

    double duration_ms = duration * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(20.0 / sim.dt());  // 20ms

    // 全局时间序列 [time][neuron]
    std::vector<std::vector<double>> all_activity;
    std::vector<double> all_times;
    // 群体平均活动
    std::vector<double> pop_mean_series;

    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        if ((s + 1) % sample_interval == 0) {
            double t = sim.current_time();
            all_times.push_back(t);

            std::vector<double> act(pop_size);
            const auto& neurons = sim.neurons();
            double sum = 0;
            for (int i = 0; i < pop_size; ++i) {
                double V = neurons[neuron_ids[i]]->get_membrane_potential();
                act[i] = 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0));
                sum += act[i];
            }
            all_activity.push_back(act);
            pop_mean_series.push_back(sum / pop_size);
        }
    }

    int total_samples = (int)all_times.size();
    std::cout << "Done! (" << total_samples << " samples, " << pop_size << " neurons)\n";

    // === 同步指数和亚稳态 ===
    std::cout << "  [2/4] Computing synchrony & metastability... " << std::flush;

    std::vector<double> sync_series;
    for (int t = 0; t < total_samples; ++t) {
        double r = MetastabilityCalculator::sync_index(all_activity[t]);
        sync_series.push_back(r);
    }

    double mean_sync = std::accumulate(sync_series.begin(), sync_series.end(), 0.0) / sync_series.size();
    double metastability = MetastabilityCalculator::compute(sync_series);

    std::cout << "Done!\n";

    // === Φ (滑动窗口) ===
    std::cout << "  [3/4] Computing Phi (IIT)... " << std::flush;

    int window_samples = window_ms / 20;  // 20ms per sample
    std::vector<double> phi_series;
    std::vector<double> phi_times;

    for (int t = window_samples; t < total_samples; t += window_samples / 2) {
        // 窗口内的活动矩阵
        std::vector<std::vector<double>> window_data;
        for (int w = t - window_samples; w < t && w < total_samples; ++w) {
            window_data.push_back(all_activity[w]);
        }

        double phi = PhiCalculator::compute(window_data);
        phi_series.push_back(phi);
        phi_times.push_back(all_times[t]);
    }

    double mean_phi = phi_series.empty() ? 0 :
        std::accumulate(phi_series.begin(), phi_series.end(), 0.0) / phi_series.size();
    double max_phi = phi_series.empty() ? 0 :
        *std::max_element(phi_series.begin(), phi_series.end());

    std::cout << "Done!\n";

    // === 多尺度熵 ===
    std::cout << "  [4/4] Computing Multiscale Entropy... " << std::flush;

    auto mse = MultiscaleEntropyCalculator::compute(pop_mean_series, mse_max_scale);

    // 单神经元 MSE (对比用)
    std::vector<double> single_mse_avg(mse.size(), 0);
    for (int i = 0; i < pop_size; ++i) {
        std::vector<double> single_series;
        for (int t = 0; t < total_samples; ++t) {
            single_series.push_back(all_activity[t][i]);
        }
        auto s_mse = MultiscaleEntropyCalculator::compute(single_series, mse_max_scale);
        for (size_t s = 0; s < s_mse.size() && s < single_mse_avg.size(); ++s) {
            single_mse_avg[s] += s_mse[s] / pop_size;
        }
    }

    std::cout << "Done!\n";

    // === 涌现事件检测 ===
    std::vector<EmergenceEvent> events;

    // 同步爆发: R(t) 突然升高
    for (int t = 1; t < (int)sync_series.size(); ++t) {
        double dR = sync_series[t] - sync_series[t - 1];
        if (dR > 0.15 && sync_series[t] > mean_sync + 2 * metastability) {
            EmergenceEvent evt;
            evt.time_ms = all_times[t];
            evt.type = "SYNC_BURST";
            evt.magnitude = sync_series[t];
            evt.detail = "R=" + std::to_string(sync_series[t]).substr(0, 5)
                       + " dR=" + std::to_string(dR).substr(0, 5);
            events.push_back(evt);
        }
    }

    // Φ 峰值
    for (size_t t = 1; t + 1 < phi_series.size(); ++t) {
        if (phi_series[t] > phi_series[t-1] && phi_series[t] > phi_series[t+1]
            && phi_series[t] > mean_phi * 1.5) {
            EmergenceEvent evt;
            evt.time_ms = phi_times[t];
            evt.type = "PHI_SPIKE";
            evt.magnitude = phi_series[t];
            evt.detail = "Phi=" + std::to_string(phi_series[t]).substr(0, 5);
            events.push_back(evt);
        }
    }

    // 亚稳态转换: 用滑动窗口平滑后检测持续跨越均值
    // 平滑 R(t) 以过滤高频噪声 (500ms 窗口 = 25 samples)
    int smooth_win = 25;
    std::vector<double> sync_smooth(sync_series.size(), 0);
    for (int t = 0; t < (int)sync_series.size(); ++t) {
        int start = std::max(0, t - smooth_win / 2);
        int end = std::min((int)sync_series.size(), t + smooth_win / 2 + 1);
        double sum = 0;
        for (int k = start; k < end; ++k) sum += sync_series[k];
        sync_smooth[t] = sum / (end - start);
    }
    // 检测平滑后的跨越, 要求持续至少 200ms (10 samples)
    bool in_sync = sync_smooth[0] >= mean_sync;
    double transition_time = 0;
    for (int t = 1; t < (int)sync_smooth.size(); ++t) {
        bool now_sync = sync_smooth[t] >= mean_sync;
        if (now_sync != in_sync) {
            double gap = all_times[t] - transition_time;
            if (gap > 500.0) {  // 至少 500ms 间隔
                EmergenceEvent evt;
                evt.time_ms = all_times[t];
                evt.type = "METASTABLE_TRANSITION";
                evt.magnitude = sync_smooth[t] - sync_smooth[t > 0 ? t-1 : 0];
                evt.detail = now_sync ? "desync->sync" : "sync->desync";
                events.push_back(evt);
                transition_time = all_times[t];
            }
            in_sync = now_sync;
        }
    }

    std::sort(events.begin(), events.end(),
              [](const EmergenceEvent& a, const EmergenceEvent& b) {
                  return a.time_ms < b.time_ms;
              });

    // === 输出 ===
    std::cout << "\n========================================\n";
    std::cout << "  EMERGENCE METRICS\n";
    std::cout << "========================================\n\n";

    std::cout << "--- Information Integration (Phi, IIT) ---\n";
    std::cout << "  Mean Phi:           " << std::fixed << std::setprecision(4) << mean_phi << "\n";
    std::cout << "  Max Phi:            " << max_phi << "\n";
    std::cout << "  Phi > 0 indicates irreducible information integration\n\n";

    std::cout << "--- Synchrony & Metastability ---\n";
    std::cout << "  Mean Sync (R):      " << std::setprecision(3) << mean_sync << "\n";
    std::cout << "  Metastability (sd): " << metastability << "\n";
    if (metastability > 0.05) {
        std::cout << "  -> HIGH metastability: population alternates sync/desync\n";
    } else {
        std::cout << "  -> LOW metastability: stable synchrony state\n";
    }
    std::cout << "\n";

    std::cout << "--- Multiscale Entropy ---\n";
    std::cout << "  Scale  Population   SingleAvg   Emergence\n";
    for (size_t s = 0; s < mse.size() && s < single_mse_avg.size(); ++s) {
        double emergence = mse[s] - single_mse_avg[s];
        std::cout << "    " << std::setw(2) << (s + 1) << "    "
                  << std::setw(8) << std::setprecision(4) << mse[s] << "     "
                  << std::setw(8) << single_mse_avg[s] << "     "
                  << std::setprecision(4) << std::showpos << emergence << std::noshowpos;
        if (emergence > 0.1) std::cout << " *EMERGENT*";
        std::cout << "\n";
    }
    std::cout << "  (Population > SingleAvg = emergent complexity)\n\n";

    // 涌现事件
    std::cout << "--- Emergence Events (" << events.size() << " detected) ---\n";
    if (events.empty()) {
        std::cout << "  No significant emergence events.\n";
    } else {
        std::cout << std::left
                  << std::setw(10) << "  Time(s)"
                  << std::setw(24) << "Type"
                  << std::setw(10) << "Magnitude"
                  << "Detail\n";
        std::cout << "  " << std::string(56, '-') << "\n";
        for (const auto& e : events) {
            std::cout << std::fixed
                      << "  " << std::setw(8) << std::setprecision(2) << std::right << (e.time_ms / 1000.0)
                      << "  " << std::setw(24) << std::left << e.type
                      << std::setw(10) << std::setprecision(4) << std::right << e.magnitude
                      << "  " << e.detail << "\n";
        }
    }

    // === 综合涌现评估 ===
    std::cout << "\n========================================\n";
    std::cout << "  EMERGENCE ASSESSMENT\n";
    std::cout << "========================================\n\n";

    int emergence_score = 0;
    if (mean_phi > 0.01) { emergence_score++; std::cout << "  [+] Phi > 0: irreducible integration present\n"; }
    if (metastability > 0.05) { emergence_score++; std::cout << "  [+] Metastability: flexible dynamics\n"; }

    bool has_mse_emergence = false;
    for (size_t s = 0; s < mse.size() && s < single_mse_avg.size(); ++s) {
        if (mse[s] > single_mse_avg[s] + 0.1) { has_mse_emergence = true; break; }
    }
    if (has_mse_emergence) { emergence_score++; std::cout << "  [+] MSE: population complexity > individual sum\n"; }
    if (!events.empty()) { emergence_score++; std::cout << "  [+] Emergence events detected\n"; }

    std::cout << "\n  Emergence Score: " << emergence_score << "/4\n";
    if (emergence_score >= 3) std::cout << "  -> STRONG emergence in neural population\n";
    else if (emergence_score >= 2) std::cout << "  -> MODERATE emergence signals\n";
    else if (emergence_score >= 1) std::cout << "  -> WEAK emergence indicators\n";
    else std::cout << "  -> NO emergence detected (may need longer duration)\n";

    // === CSV 导出 ===
    if (!export_file.empty()) {
        std::ofstream ofs(export_file);
        ofs << "time_ms,sync_index,phi,pop_mean";
        for (const auto& name : valid_names) ofs << "," << name;
        ofs << "\n";

        int phi_idx = 0;
        for (int t = 0; t < total_samples; ++t) {
            ofs << std::fixed << std::setprecision(1) << all_times[t] << ","
                << std::setprecision(4) << sync_series[t] << ",";

            // 找最近的 phi 值
            double phi_val = 0;
            if (!phi_times.empty()) {
                while (phi_idx + 1 < (int)phi_times.size() &&
                       phi_times[phi_idx + 1] <= all_times[t]) phi_idx++;
                if (phi_idx < (int)phi_series.size()) phi_val = phi_series[phi_idx];
            }
            ofs << phi_val << "," << pop_mean_series[t];

            for (int i = 0; i < pop_size; ++i) {
                ofs << "," << all_activity[t][i];
            }
            ofs << "\n";
        }

        std::cout << "\nExported: " << export_file << "\n";
    }

    return 0;
}
