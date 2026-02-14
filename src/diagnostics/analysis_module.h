#pragma once

#include "diagnostics/diagnostic_tracker.h"
#include "simulation/simulation_engine.h"
#include <string>
#include <vector>
#include <map>

namespace celegans {

// 实验协议配置
struct ExperimentalProtocol {
    std::vector<std::string> ablations;
    bool enable_light = false;
    Vector2d light_position{25.0, 25.0};
    double light_intensity = 1.0;
    
    bool enable_pheromone = false;
    Vector2d pheromone_position{15.0, 25.0};
    double pheromone_intensity = 0.8;
    
    double sleep_after_learn_s = 0.0;
    double dishabit_time_s = -1.0;
    double food_removal_time_s = -1.0;
    
    double npr1_override = -999.0;
    bool no_toxin = false;
    bool no_food = false;
};

// 可插拔分析模块基类
class AnalysisModule {
public:
    virtual ~AnalysisModule() = default;
    
    virtual std::string name() const = 0;
    virtual void run(SimulationEngine& sim, DiagnosticTracker& tracker) = 0;
    virtual void print_results(std::ostream& out) const = 0;
};

}  // namespace celegans
