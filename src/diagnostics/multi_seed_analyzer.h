#pragma once

#include "diagnostics/analysis_module.h"
#include <mutex>
#include <future>

namespace celegans {

class MultiSeedAnalyzer : public AnalysisModule {
public:
    struct Config {
        unsigned int base_seed = 123;
        int num_seeds = 4;
        int num_jobs = 8;
        double duration_s = 300.0;
        Vector2d target{35.0, 25.0};
        ExperimentalProtocol protocol;
    };
    
    struct SeedResult {
        unsigned int seed;
        DiagnosticTracker::BehaviorMetrics metrics;
        double final_5ht;
        double final_satiety;
        double final_sickness;
    };
    
    MultiSeedAnalyzer(const Config& cfg);
    
    std::string name() const override { return "MultiSeedAnalyzer"; }
    void run(SimulationEngine& sim, DiagnosticTracker& tracker) override;
    void print_results(std::ostream& out) const override;
    
    const std::vector<SeedResult>& get_results() const { return results_; }
    
private:
    Config config_;
    std::vector<SeedResult> results_;
    
    SeedResult run_single_seed(unsigned int seed);
};

}  // namespace celegans
