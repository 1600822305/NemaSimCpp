#include "diagnostics/multi_seed_analyzer.h"
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <chrono>

namespace celegans {

MultiSeedAnalyzer::MultiSeedAnalyzer(const Config& cfg) : config_(cfg) {}

void MultiSeedAnalyzer::run(SimulationEngine& sim, DiagnosticTracker& tracker) {
    results_.clear();
    results_.resize(config_.num_seeds);
    
    std::mutex mtx;
    int done_count = 0;
    
    auto run_one = [&](int i) {
        unsigned int seed = config_.base_seed + i;
        auto result = run_single_seed(seed);
        std::lock_guard<std::mutex> lk(mtx);
        results_[i] = result;
        std::cerr << "  seed " << seed << " done (" << ++done_count 
                  << "/" << config_.num_seeds << ")" << std::endl;
    };
    
    std::cerr << "MULTI-SEED: " << config_.num_seeds << " seeds, "
              << config_.duration_s << "s each, " 
              << config_.num_jobs << " parallel jobs" << std::endl;
    
    auto t_start = std::chrono::high_resolution_clock::now();
    
    // 分批并行执行
    for (int batch = 0; batch < config_.num_seeds; batch += config_.num_jobs) {
        int batch_end = std::min(batch + config_.num_jobs, config_.num_seeds);
        std::vector<std::future<void>> futures;
        for (int i = batch; i < batch_end; ++i) {
            futures.push_back(std::async(std::launch::async, run_one, i));
        }
        for (auto& f : futures) f.get();
    }
    
    auto t_end = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t_end - t_start).count();
    std::cerr << "Completed in " << std::fixed << std::setprecision(1) 
              << elapsed << "s wall time" << std::endl;
}

MultiSeedAnalyzer::SeedResult MultiSeedAnalyzer::run_single_seed(unsigned int seed) {
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);
    
    // 应用实验协议
    const auto& proto = config_.protocol;
    for (const auto& name : proto.ablations) {
        sim.ablate_neuron(name);
    }
    
    if (proto.npr1_override > -900) {
        sim.set_npr1_rmg(proto.npr1_override);
    }
    
    // 环境设置
    sim.environment().chemical_field().clear();
    if (!proto.no_food) {
        sim.environment().chemical_field().add_point_source(config_.target, 1.0);
        sim.environment().soluble_field().add_point_source(config_.target, 0.4);
        if (!proto.no_toxin) {
            sim.environment().repellent_field().add_point_source(config_.target, 0.8, 25.0);
        }
    }
    
    if (proto.enable_light) {
        sim.environment().set_light_source(proto.light_position, proto.light_intensity);
    }
    if (proto.enable_pheromone) {
        sim.environment().set_pheromone_source(proto.pheromone_position, proto.pheromone_intensity);
    }
    
    sim.reset_transducers();
    
    // 追踪器
    DiagnosticTracker tracker;
    tracker.set_sample_interval_ms(100.0);
    
    // 运行仿真
    double duration_ms = config_.duration_s * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    
    bool sleep_triggered = false;
    bool food_removed = false;
    
    if (proto.dishabit_time_s > 0) {
        sim.set_dishabit_time(proto.dishabit_time_s * 1000.0);
    }
    
    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        
        // 食物移除协议
        if (proto.food_removal_time_s > 0 && !food_removed &&
            sim.current_time() >= proto.food_removal_time_s * 1000.0) {
            food_removed = true;
            sim.environment().chemical_field().clear();
            sim.environment().soluble_field().clear();
            sim.environment().repellent_field().clear();
            sim.reset_transducers();
        }
        
        // 学习后睡眠协议
        if (proto.sleep_after_learn_s > 0 && !sleep_triggered && sim.sickness() > 0.3) {
            sim.force_sleep(proto.sleep_after_learn_s * 1000.0);
            sleep_triggered = true;
        }
        
        tracker.sample(sim.current_time(), sim.connectome(), sim.neurons(), sim);
    }
    
    // 收集结果
    SeedResult result;
    result.seed = seed;
    result.metrics = tracker.compute_behavior_metrics(config_.target, 5.0);
    result.final_5ht = sim.neuromodulation().get_concentration("5-HT");
    result.final_satiety = sim.satiety();
    result.final_sickness = sim.sickness();
    
    return result;
}

void MultiSeedAnalyzer::print_results(std::ostream& out) const {
    if (results_.empty()) return;
    
    // 计算统计量
    auto stat = [&](auto fn) -> std::pair<double, double> {
        double sum = 0, sum2 = 0;
        for (const auto& r : results_) {
            double v = fn(r);
            sum += v; sum2 += v*v;
        }
        double mean = sum / results_.size();
        double var = sum2 / results_.size() - mean*mean;
        return {mean, std::sqrt(std::max(0.0, var))};
    };
    
    auto [ci_m, ci_s] = stat([](auto& r){ return r.metrics.ci; });
    auto [sp_m, sp_s] = stat([](auto& r){ return r.metrics.mean_speed; });
    auto [rr_m, rr_s] = stat([](auto& r){ return r.metrics.reversal_rate; });
    auto [or_m, or_s] = stat([](auto& r){ return r.metrics.omega_per_reversal; });
    auto [nf_m, nf_s] = stat([](auto& r){ return r.metrics.near_target_pct; });
    auto [dv_m, dv_s] = stat([](auto& r){ return r.metrics.dv_ratio; });
    
    out << "\n========================================\n";
    out << "  MULTI-SEED RESULTS (" << results_.size() << " seeds)\n";
    out << "========================================\n\n";
    out << std::fixed << std::setprecision(3);
    out << "  CI:           " << ci_m << " ± " << ci_s << "\n";
    out << "  Speed:        " << sp_m << " ± " << sp_s << " mm/s\n";
    out << "  Rev Rate:     " << rr_m << " ± " << rr_s << " Hz\n";
    out << "  Omega/Rev:    " << or_m << " ± " << or_s << "\n";
    out << std::setprecision(1);
    out << "  Near Target:  " << nf_m << " ± " << nf_s << "%\n";
    out << std::setprecision(2);
    out << "  D/V Ratio:    " << dv_m << " ± " << dv_s << "\n";
    
    // 每个种子详细
    out << "\n  Per-seed:\n";
    out << "  seed    CI     speed  rev_rate  omega/rev  near%\n";
    for (const auto& r : results_) {
        out << "  " << std::setw(4) << r.seed
            << "  " << std::setprecision(3) << std::setw(6) << r.metrics.ci
            << "  " << std::setw(5) << r.metrics.mean_speed
            << "  " << std::setw(8) << r.metrics.reversal_rate
            << "  " << std::setw(9) << r.metrics.omega_per_reversal
            << "  " << std::setprecision(1) << std::setw(5) << r.metrics.near_target_pct << "\n";
    }
}

}  // namespace celegans
