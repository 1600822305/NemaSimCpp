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
// 时间序列数据采集
// ================================================================
struct NeuronTrace {
    std::string name;
    int id;
    std::vector<double> voltage;    // 离散化电压序列
    std::vector<double> release;    // 离散化释放率序列
};

// ================================================================
// Transfer Entropy: T_{X→Y} = H(Y_t | Y_{t-1..t-k}) - H(Y_t | Y_{t-1..t-k}, X_{t-1..t-k})
// 使用 binning 方法离散化连续值
// ================================================================
class TransferEntropyCalculator {
public:
    TransferEntropyCalculator(int n_bins = 4, int lag = 1)
        : n_bins_(n_bins), lag_(lag) {}

    // 计算 X→Y 的 Transfer Entropy (bits)
    double compute(const std::vector<double>& x, const std::vector<double>& y) const {
        if (x.size() != y.size() || (int)x.size() <= lag_ * 2) return 0;

        // 离散化
        auto dx = discretize(x);
        auto dy = discretize(y);
        int N = (int)dx.size();

        // 计算联合概率和条件概率
        // P(y_t, y_past, x_past), P(y_t, y_past), P(y_past, x_past), P(y_past)
        std::map<int, int> count_ypast;           // P(y_past)
        std::map<int, int> count_yt_ypast;        // P(y_t, y_past)
        std::map<int, int> count_ypast_xpast;     // P(y_past, x_past)
        std::map<int, int> count_yt_ypast_xpast;  // P(y_t, y_past, x_past)

        for (int t = lag_; t < N; ++t) {
            int yt = dy[t];
            int yp = dy[t - lag_];
            int xp = dx[t - lag_];

            int key_yp = yp;
            int key_yt_yp = yt * n_bins_ + yp;
            int key_yp_xp = yp * n_bins_ + xp;
            int key_yt_yp_xp = yt * n_bins_ * n_bins_ + yp * n_bins_ + xp;

            count_ypast[key_yp]++;
            count_yt_ypast[key_yt_yp]++;
            count_ypast_xpast[key_yp_xp]++;
            count_yt_ypast_xpast[key_yt_yp_xp]++;
        }

        int total = N - lag_;
        if (total <= 0) return 0;

        // TE = sum P(yt, yp, xp) * log2( P(yt|yp,xp) / P(yt|yp) )
        //    = sum P(yt, yp, xp) * log2( P(yt,yp,xp)*P(yp) / (P(yp,xp)*P(yt,yp)) )
        double te = 0;
        for (const auto& it : count_yt_ypast_xpast) {
            int key = it.first;
            int c_yt_yp_xp = it.second;

            int yt = key / (n_bins_ * n_bins_);
            int yp = (key / n_bins_) % n_bins_;
            int xp = key % n_bins_;

            int key_yp = yp;
            int key_yt_yp = yt * n_bins_ + yp;
            int key_yp_xp = yp * n_bins_ + xp;

            auto it1 = count_ypast.find(key_yp);
            auto it2 = count_yt_ypast.find(key_yt_yp);
            auto it3 = count_ypast_xpast.find(key_yp_xp);

            if (it1 == count_ypast.end() || it2 == count_yt_ypast.end() ||
                it3 == count_ypast_xpast.end()) continue;

            double p_yt_yp_xp = (double)c_yt_yp_xp / total;
            double p_yp = (double)it1->second / total;
            double p_yt_yp = (double)it2->second / total;
            double p_yp_xp = (double)it3->second / total;

            if (p_yt_yp > 0 && p_yp_xp > 0 && p_yp > 0) {
                double ratio = (p_yt_yp_xp * p_yp) / (p_yp_xp * p_yt_yp);
                if (ratio > 0) {
                    te += p_yt_yp_xp * std::log2(ratio);
                }
            }
        }

        return std::max(0.0, te);
    }

private:
    int n_bins_;
    int lag_;

    std::vector<int> discretize(const std::vector<double>& data) const {
        double min_val = *std::min_element(data.begin(), data.end());
        double max_val = *std::max_element(data.begin(), data.end());
        double range = max_val - min_val;
        if (range < 1e-10) range = 1.0;

        std::vector<int> result(data.size());
        for (size_t i = 0; i < data.size(); ++i) {
            int bin = (int)((data[i] - min_val) / range * n_bins_);
            if (bin >= n_bins_) bin = n_bins_ - 1;
            result[i] = bin;
        }
        return result;
    }
};

// ================================================================
// Granger Causality: 用 X 的过去值是否能改善对 Y 的预测
// 简化版：比较 AR(k) 模型残差方差
// ================================================================
class GrangerCausalityCalculator {
public:
    GrangerCausalityCalculator(int lag = 5) : lag_(lag) {}

    // 返回 F-statistic 类似的比值: var_restricted / var_unrestricted
    // >1 表示 X 对 Y 有 Granger 因果
    double compute(const std::vector<double>& x, const std::vector<double>& y) const {
        if ((int)y.size() <= lag_ * 2) return 0;

        // Restricted model: Y_t = sum(a_i * Y_{t-i})
        double var_r = fit_ar_variance(y, {}, lag_);

        // Unrestricted model: Y_t = sum(a_i * Y_{t-i}) + sum(b_i * X_{t-i})
        double var_u = fit_ar_variance(y, x, lag_);

        if (var_u < 1e-15) return 0;
        return (var_r - var_u) / var_u;  // F-ratio proxy
    }

private:
    int lag_;

    // 简化 AR 模型：用均值预测的残差方差
    // 带 exogenous: 用 Y_past 和 X_past 的加权均值预测
    double fit_ar_variance(const std::vector<double>& y,
                           const std::vector<double>& x_exo,
                           int lag) const {
        bool has_exo = !x_exo.empty();
        int N = (int)y.size();
        double sse = 0;
        int count = 0;

        for (int t = lag; t < N; ++t) {
            // 简单预测: Y_past 均值
            double pred = 0;
            for (int k = 1; k <= lag; ++k) pred += y[t - k];
            pred /= lag;

            if (has_exo) {
                // 加入 X_past 均值作为修正
                double x_mean = 0;
                for (int k = 1; k <= lag; ++k) x_mean += x_exo[t - k];
                x_mean /= lag;
                // 简单线性组合: pred = 0.7*y_past_mean + 0.3*x_past_mean
                // (简化版, 真正的 GC 需要 OLS 回归)
                pred = 0.7 * pred + 0.3 * x_mean;
            }

            double err = y[t] - pred;
            sse += err * err;
            count++;
        }

        return (count > 0) ? sse / count : 0;
    }
};

// ================================================================
// 干预因果: 对比 wild-type vs ablation 的行为差异
// ================================================================
struct InterventionResult {
    std::string ablated_neuron;
    double wt_mean_speed;
    double ab_mean_speed;
    double speed_change_pct;
    int wt_reversals;
    int ab_reversals;
    double wt_ci;
    double ab_ci;
    double ci_change;
};

InterventionResult run_intervention(const std::string& neuron_name,
                                     double duration_s, unsigned int seed) {
    InterventionResult result;
    result.ablated_neuron = neuron_name;

    auto run_sim = [&](bool do_ablate) {
        SimulationEngine sim;
        sim.initialize_default();
        sim.set_rng_seed(seed);

        if (do_ablate) {
            sim.ablate_neuron(neuron_name);
        }

        sim.environment().chemical_field().clear();
        Vector2d food{35.0, 25.0};
        sim.environment().chemical_field().add_point_source(food, 1.0);
        sim.environment().soluble_field().add_point_source(food, 0.4);
        sim.reset_transducers();

        double duration_ms = duration_s * 1000.0;
        int total_steps = (int)(duration_ms / sim.dt());
        int sample_interval = (int)(100.0 / sim.dt());

        double speed_sum = 0;
        int speed_count = 0;
        int rev_count = 0;
        bool prev_rev = false;
        Vector2d first_pos{0, 0}, last_pos{0, 0};
        bool first = true;
        double first_dist = 0, final_dist = 0;

        for (int s = 0; s < total_steps; ++s) {
            sim.step();
            if ((s + 1) % sample_interval == 0) {
                auto pos = sim.body().get_head_position();
                double spd = sim.body().get_speed();
                speed_sum += spd;
                speed_count++;

                if (first) { first_pos = pos; first = false; }
                last_pos = pos;

                bool curr_rev = sim.is_reversing();
                if (curr_rev && !prev_rev) rev_count++;
                prev_rev = curr_rev;
            }
        }

        double mean_speed = speed_count > 0 ? speed_sum / speed_count : 0;
        first_dist = std::sqrt((first_pos.x - 35.0) * (first_pos.x - 35.0) +
                               (first_pos.y - 25.0) * (first_pos.y - 25.0));
        final_dist = std::sqrt((last_pos.x - 35.0) * (last_pos.x - 35.0) +
                               (last_pos.y - 25.0) * (last_pos.y - 25.0));
        double ci = (first_dist > 0.1) ? (first_dist - final_dist) / first_dist : 0;

        return std::make_tuple(mean_speed, rev_count, ci);
    };

    auto [wt_spd, wt_rev, wt_ci] = run_sim(false);
    auto [ab_spd, ab_rev, ab_ci] = run_sim(true);

    result.wt_mean_speed = wt_spd;
    result.ab_mean_speed = ab_spd;
    result.speed_change_pct = (wt_spd > 0.001) ? 100.0 * (ab_spd - wt_spd) / wt_spd : 0;
    result.wt_reversals = wt_rev;
    result.ab_reversals = ab_rev;
    result.wt_ci = wt_ci;
    result.ab_ci = ab_ci;
    result.ci_change = ab_ci - wt_ci;

    return result;
}

// ================================================================
// 因果边: 一条因果关系
// ================================================================
struct CausalEdge {
    std::string source;
    std::string target;
    double te_score;     // Transfer Entropy (bits)
    double gc_score;     // Granger Causality F-ratio
    std::string strength; // STRONG, MODERATE, WEAK, NONE
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
    std::vector<std::string> target_neurons;
    std::vector<std::string> ablate_neurons;
    std::string export_file;
    bool run_te = true;
    bool run_gc = true;
    bool run_intervention = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i+1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--neuron" && i+1 < argc) {
            target_neurons.push_back(argv[++i]);
        } else if (arg == "--ablate" && i+1 < argc) {
            ablate_neurons.push_back(argv[++i]);
            run_intervention = true;
        } else if (arg == "--export" && i+1 < argc) {
            export_file = argv[++i];
        } else if (arg == "--no-te") {
            run_te = false;
        } else if (arg == "--no-gc") {
            run_gc = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: causal_analyzer [options]\n\n"
                      << "Causal analysis between neurons\n\n"
                      << "Options:\n"
                      << "  --neuron <name>      Add neuron to analysis (repeatable)\n"
                      << "  --ablate <name>      Ablation intervention (repeatable)\n"
                      << "  --duration <sec>     Simulation duration (default: 30)\n"
                      << "  --seed <n>           RNG seed (default: 123)\n"
                      << "  --no-te              Skip Transfer Entropy\n"
                      << "  --no-gc              Skip Granger Causality\n"
                      << "  --export <csv>       Export causal edges to CSV\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Examples:\n"
                      << "  causal_analyzer --neuron ASEL --neuron AIAL --neuron AIBL --neuron AVAL\n"
                      << "  causal_analyzer --neuron ASEL --neuron ASER --ablate ASEL\n"
                      << "  causal_analyzer --neuron PLML --neuron AVAL --neuron AVBL --duration 60\n\n"
                      << "Methods:\n"
                      << "  Transfer Entropy:    Information-theoretic causality (bits)\n"
                      << "  Granger Causality:   Predictive causality (F-ratio)\n"
                      << "  Intervention:        Ablation counterfactual comparison\n";
            return 0;
        }
    }

    // 默认神经元集
    if (target_neurons.empty()) {
        target_neurons = {"ASEL", "ASER", "AIAL", "AIBL", "AVAL", "AVBL", "SMDDL", "SMDVL"};
    }

    std::cout << "========================================\n";
    std::cout << "  Causal Analyzer\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration: " << duration << " s\n";
    std::cout << "  Neurons:  ";
    for (size_t i = 0; i < target_neurons.size(); ++i) {
        std::cout << target_neurons[i];
        if (i + 1 < target_neurons.size()) std::cout << ", ";
    }
    std::cout << "\n\n";

    // === Step 1: 数据采集 ===
    std::cout << "  [1/3] Collecting time series... " << std::flush;

    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    sim.reset_transducers();

    const auto& conn = sim.connectome();
    std::vector<NeuronTrace> traces;
    for (const auto& name : target_neurons) {
        int id = conn.get_neuron_id(name);
        if (id < 0) {
            std::cerr << "\nWarning: neuron '" << name << "' not found, skipping\n";
            continue;
        }
        NeuronTrace t;
        t.name = name;
        t.id = id;
        traces.push_back(t);
    }

    double duration_ms = duration * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(20.0 / sim.dt());  // 20ms sampling for TE/GC

    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        if ((s + 1) % sample_interval == 0) {
            const auto& neurons = sim.neurons();
            for (auto& tr : traces) {
                if (tr.id < (int)neurons.size()) {
                    double V = neurons[tr.id]->get_membrane_potential();
                    double S = 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0));
                    tr.voltage.push_back(V);
                    tr.release.push_back(S);
                }
            }
        }
    }

    std::cout << "Done! (" << traces[0].voltage.size() << " samples)\n";

    // === Step 2: 因果分析 ===
    std::vector<CausalEdge> edges;

    if (run_te) {
        std::cout << "  [2/3] Computing Transfer Entropy... " << std::flush;
        TransferEntropyCalculator te_calc(4, 2);  // 4 bins, lag=2

        for (size_t i = 0; i < traces.size(); ++i) {
            for (size_t j = 0; j < traces.size(); ++j) {
                if (i == j) continue;

                double te = te_calc.compute(traces[i].release, traces[j].release);

                CausalEdge edge;
                edge.source = traces[i].name;
                edge.target = traces[j].name;
                edge.te_score = te;
                edge.gc_score = 0;

                if (te > 0.1) edge.strength = "STRONG";
                else if (te > 0.03) edge.strength = "MODERATE";
                else if (te > 0.01) edge.strength = "WEAK";
                else edge.strength = "NONE";

                edges.push_back(edge);
            }
        }
        std::cout << "Done!\n";
    }

    if (run_gc) {
        std::cout << "  [" << (run_te ? "2" : "2") << "/3] Computing Granger Causality... " << std::flush;
        GrangerCausalityCalculator gc_calc(5);  // lag=5

        for (auto& edge : edges) {
            // 找到对应的 traces
            int si = -1, ti = -1;
            for (size_t k = 0; k < traces.size(); ++k) {
                if (traces[k].name == edge.source) si = (int)k;
                if (traces[k].name == edge.target) ti = (int)k;
            }
            if (si >= 0 && ti >= 0) {
                edge.gc_score = gc_calc.compute(traces[si].release, traces[ti].release);
            }
        }

        // 如果没有 TE 跑, 需要单独生成 edges
        if (!run_te) {
            for (size_t i = 0; i < traces.size(); ++i) {
                for (size_t j = 0; j < traces.size(); ++j) {
                    if (i == j) continue;
                    CausalEdge edge;
                    edge.source = traces[i].name;
                    edge.target = traces[j].name;
                    edge.te_score = 0;
                    edge.gc_score = gc_calc.compute(traces[i].release, traces[j].release);
                    if (edge.gc_score > 0.5) edge.strength = "STRONG";
                    else if (edge.gc_score > 0.1) edge.strength = "MODERATE";
                    else if (edge.gc_score > 0.02) edge.strength = "WEAK";
                    else edge.strength = "NONE";
                    edges.push_back(edge);
                }
            }
        }
        std::cout << "Done!\n";
    }

    // === 输出因果图谱 ===
    std::cout << "\n========================================\n";
    std::cout << "  CAUSAL GRAPH\n";
    std::cout << "========================================\n\n";

    // 只显示有意义的边
    std::vector<CausalEdge> significant_edges;
    for (const auto& e : edges) {
        if (e.strength != "NONE") {
            significant_edges.push_back(e);
        }
    }

    // 按 TE 排序
    std::sort(significant_edges.begin(), significant_edges.end(),
              [](const CausalEdge& a, const CausalEdge& b) {
                  return a.te_score > b.te_score;
              });

    if (significant_edges.empty()) {
        std::cout << "  No significant causal edges detected.\n";
    } else {
        std::cout << std::left
                  << std::setw(10) << "Source"
                  << std::setw(10) << "Target"
                  << std::right
                  << std::setw(10) << "TE(bits)"
                  << std::setw(10) << "GC(F)"
                  << "  Strength\n";
        std::cout << std::string(50, '-') << "\n";

        for (const auto& e : significant_edges) {
            std::cout << std::left
                      << std::setw(10) << e.source
                      << std::setw(10) << e.target
                      << std::right << std::fixed
                      << std::setw(10) << std::setprecision(4) << e.te_score
                      << std::setw(10) << std::setprecision(4) << e.gc_score
                      << "  " << e.strength << "\n";
        }

        // 连接组中的已知突触对比
        std::cout << "\n--- Connectome Verification ---\n";
        for (const auto& e : significant_edges) {
            int src_id = conn.get_neuron_id(e.source);
            int tgt_id = conn.get_neuron_id(e.target);
            bool has_chem = false, has_gap = false;
            std::string syn_type;

            for (const auto& syn : conn.synapses()) {
                if (syn.pre_id() == src_id && syn.post_id() == tgt_id) {
                    has_chem = true;
                    syn_type = syn.is_excitatory() ? "exc" : "inh";
                    break;
                }
            }
            for (const auto& gap : conn.gap_junctions()) {
                if ((gap.neuron_a() == src_id && gap.neuron_b() == tgt_id) ||
                    (gap.neuron_a() == tgt_id && gap.neuron_b() == src_id)) {
                    has_gap = true;
                    break;
                }
            }

            std::string conn_str;
            if (has_chem && has_gap) conn_str = "chem(" + syn_type + ")+gap";
            else if (has_chem) conn_str = "chem(" + syn_type + ")";
            else if (has_gap) conn_str = "gap";
            else conn_str = "NO DIRECT CONNECTION";

            std::cout << "  " << std::setw(8) << std::left << e.source
                      << " -> " << std::setw(8) << e.target
                      << "  " << conn_str << "\n";
        }
    }

    // === Step 3: 干预因果 ===
    if (run_intervention && !ablate_neurons.empty()) {
        std::cout << "\n========================================\n";
        std::cout << "  INTERVENTION ANALYSIS\n";
        std::cout << "========================================\n\n";

        for (const auto& abl : ablate_neurons) {
            std::cout << "  Ablating " << abl << "... " << std::flush;
            auto result = ::run_intervention(abl, duration, seed);
            std::cout << "Done!\n\n";

            std::cout << "  " << std::setw(20) << std::left << ""
                      << std::setw(12) << "Wild-type"
                      << std::setw(12) << "Ablated"
                      << "Change\n";
            std::cout << "  " << std::string(56, '-') << "\n";

            std::cout << "  " << std::setw(20) << std::left << "Speed (mm/s)"
                      << std::fixed << std::setprecision(3)
                      << std::setw(12) << result.wt_mean_speed
                      << std::setw(12) << result.ab_mean_speed
                      << std::setprecision(1) << std::showpos << result.speed_change_pct << "%" << std::noshowpos << "\n";

            std::cout << "  " << std::setw(20) << std::left << "Reversals"
                      << std::setw(12) << result.wt_reversals
                      << std::setw(12) << result.ab_reversals
                      << std::showpos << (result.ab_reversals - result.wt_reversals) << std::noshowpos << "\n";

            std::cout << "  " << std::setw(20) << std::left << "Chemotaxis Index"
                      << std::fixed << std::setprecision(3)
                      << std::setw(12) << result.wt_ci
                      << std::setw(12) << result.ab_ci
                      << std::setprecision(3) << std::showpos << result.ci_change << std::noshowpos << "\n";

            // 因果判断
            std::cout << "\n  Causal Role of " << abl << ":\n";
            if (std::abs(result.ci_change) > 0.1) {
                std::cout << "    -> CAUSAL for chemotaxis"
                          << (result.ci_change < 0 ? " (ablation impairs)" : " (ablation enhances)")
                          << "\n";
            }
            if (std::abs(result.speed_change_pct) > 10) {
                std::cout << "    -> CAUSAL for locomotion speed"
                          << (result.speed_change_pct < 0 ? " (ablation slows)" : " (ablation speeds up)")
                          << "\n";
            }
            int rev_diff = result.ab_reversals - result.wt_reversals;
            if (std::abs(rev_diff) > 3) {
                std::cout << "    -> CAUSAL for reversal frequency"
                          << (rev_diff < 0 ? " (ablation reduces)" : " (ablation increases)")
                          << "\n";
            }
            std::cout << "\n";
        }
    }

    // === CSV 导出 ===
    if (!export_file.empty()) {
        std::ofstream ofs(export_file);
        ofs << "source,target,te_bits,gc_f_ratio,strength\n";
        for (const auto& e : edges) {
            ofs << e.source << "," << e.target << ","
                << std::fixed << std::setprecision(6) << e.te_score << ","
                << e.gc_score << "," << e.strength << "\n";
        }
        std::cout << "Exported: " << export_file << "\n";
    }

    return 0;
}
