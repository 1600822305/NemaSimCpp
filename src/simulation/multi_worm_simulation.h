#pragma once
// ================================================================
// Step 122: Multi-Worm Social Simulation
// N independent SimulationEngine instances sharing an environment layout.
// Social interactions mediated through:
//   1. O₂ consumption by nearby worms → URX/RMG circuit responds
//   2. Density-dependent speed/reversal modulation (emergent from RMG)
// REF: Ding 2019 eLife, Macosko 2009 Nature, Laurent 2015 eLife
// ================================================================

#include "simulation_engine.h"
#include "../core/types.h"
#include <vector>
#include <memory>
#include <functional>

namespace celegans {

class MultiWormSimulation {
public:
    // Per-worm configuration
    struct WormConfig {
        Vector2d start_position = {25.0, 25.0};
        double start_heading = 0.0;
        unsigned int seed = 42;
        double npr1_rmg = -20.0;  // N2 default; 0 for Hawaiian npr-1(lf)
    };

    // Social interaction parameters
    struct SocialParams {
        double o2_per_worm = 3.0;           // % O₂ reduction per nearby worm
        double o2_interaction_radius = 5.0;  // mm — worm O₂ consumption range
        double social_radius = 5.0;          // mm — neighbor detection for RMG-gated modulation
        double cluster_radius = 2.0;         // mm — for cluster metric definition
    };

    // Aggregation metrics (computed each snapshot)
    struct SocialMetrics {
        double mean_nnd = 0;              // mean nearest-neighbor distance (mm)
        double mean_speed = 0;            // mean speed across all worms (mm/s)
        double cluster_fraction = 0;      // fraction of worms with ≥1 neighbor within cluster_radius
        int num_clusters = 0;             // number of distinct clusters
        double mean_cluster_size = 0;     // mean worms per cluster
        double pair_correlation_short = 0; // pair correlation at short range (<2mm)
        int total_steps = 0;
        double time_s = 0;
    };

    // Snapshot of all worm positions at a time point
    struct Snapshot {
        double time_s;
        std::vector<Vector2d> positions;  // head positions of all worms
        std::vector<double> speeds;
        SocialMetrics metrics;
    };

    MultiWormSimulation() = default;

    // Initialize with per-worm configs
    void initialize(const std::vector<WormConfig>& configs,
                    const SocialParams& social = {});

    // Single coordinated step: apply social interactions, then step all worms
    void step();

    // Run for duration (seconds)
    void run(double duration_s, double snapshot_interval_s = 1.0);

    // Access
    int num_worms() const { return static_cast<int>(worms_.size()); }
    const SimulationEngine& worm(int i) const { return *worms_[i]; }
    SimulationEngine& worm_mut(int i) { return *worms_[i]; }
    double current_time_s() const;

    // Metrics
    SocialMetrics compute_metrics() const;
    const std::vector<Snapshot>& snapshots() const { return snapshots_; }

    // Step callback for external monitoring
    using SocialCallback = std::function<void(const MultiWormSimulation&, int step)>;
    void set_callback(SocialCallback cb) { callback_ = std::move(cb); }

private:
    std::vector<std::unique_ptr<SimulationEngine>> worms_;
    SocialParams social_params_;
    std::vector<Snapshot> snapshots_;
    SocialCallback callback_;
    int step_count_ = 0;

    // Compute social O₂ reduction for each worm based on neighbor positions
    void apply_social_interactions();

    // Cluster detection (union-find based)
    int count_clusters(double radius) const;
};

} // namespace celegans
