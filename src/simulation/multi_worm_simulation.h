#pragma once
// ================================================================
// Step 128: Multi-Worm Simulation Framework
// N independent SimulationEngine instances sharing a pheromone field
// Each worm deposits ascaroside pheromone → shared field → ADL sensing
// Local worm density → NPR-1/RMG speed modulation (Step 96)
// Physical collision → simple repulsion
//
// Three aggregation rules (Ding & Schumacher 2019 eLife):
//   1. Cluster-edge reversals (density drop → AVA activation)
//   2. Density-dependent speed switch (more neighbors → slower)
//   3. Taxis towards neighbors (pheromone gradient → weathervane)
//
// REF: Ding & Schumacher 2019 eLife — shared mechanisms for aggregation
//      de Bono & Bargmann 1998 — NPR-1 social vs solitary
//      Macosko 2009 Nature — hub-and-spoke pheromone circuit
// ================================================================

#include "simulation_engine.h"
#include <vector>
#include <memory>
#include <functional>
#include <thread>

namespace celegans {

struct MultiWormStats {
    int num_worms = 0;
    int num_clusters = 0;           // connected components (distance < 2mm)
    double mean_nearest_neighbor = 0; // mm
    double cluster_fraction = 0;     // fraction of worms in clusters (>1 member)
    double mean_speed = 0;           // mm/s
    int total_eggs = 0;
};

class MultiWormSimulation {
public:
    MultiWormSimulation(int num_worms, unsigned int base_seed = 42, int num_threads = 0);

    // Initialize all worms with default config
    void initialize();

    // Step all worms + update shared pheromone field
    void step();

    // Run for given duration
    void run(double duration_ms);

    // Access
    int num_worms() const { return static_cast<int>(worms_.size()); }
    const SimulationEngine& worm(int i) const { return *worms_[i]; }
    SimulationEngine& worm_mut(int i) { return *worms_[i]; }
    double current_time() const { return current_time_; }

    // Statistics
    MultiWormStats compute_stats() const;

    // Callback per step
    using StepCallback = std::function<void(const MultiWormSimulation&, int step)>;
    void set_step_callback(StepCallback cb) { step_callback_ = std::move(cb); }

    // NPR-1 variant: 0=social (Hawaiian/npr-1 lf), -15=solitary (N2)
    void set_npr1(double pA) { npr1_override_ = pA; }

private:
    std::vector<std::unique_ptr<SimulationEngine>> worms_;
    int num_worms_;
    unsigned int base_seed_;
    double current_time_ = 0.0;
    double dt_ = 0.5;  // ms
    int step_count_ = 0;
    double npr1_override_ = -999.0;  // no override by default
    int num_threads_ = 4;
    StepCallback step_callback_;

    // Shared pheromone field (ascaroside deposited by all worms)
    // Each worm deposits pheromone at its position → diffuses → sensed by others
    void update_shared_pheromone();

    // Worm-worm physical interactions (collision avoidance)
    void apply_worm_interactions();

    // Deposit worm density signal into each worm's environment
    void update_worm_density_signals();
};

} // namespace celegans
