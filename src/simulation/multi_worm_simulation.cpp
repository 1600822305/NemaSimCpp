// ================================================================
// Step 122: Multi-Worm Social Simulation — Implementation
// REF: Ding 2019 eLife — aggregation via O₂-mediated density sensing
//      Macosko 2009 Nature — RMG hub-and-spoke circuit
//      Laurent 2015 eLife — RMG → AVB/AIY forward drive
// ================================================================

#include "multi_worm_simulation.h"
#include "../core/logger.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace celegans {

void MultiWormSimulation::initialize(
    const std::vector<WormConfig>& configs,
    const SocialParams& social)
{
    social_params_ = social;
    worms_.clear();
    snapshots_.clear();
    step_count_ = 0;

    for (size_t i = 0; i < configs.size(); ++i) {
        auto engine = std::make_unique<SimulationEngine>();
        engine->initialize_default();
        // Set distinct RNG seed for each worm
        engine->set_rng_seed(configs[i].seed);
        engine->set_npr1_rmg(configs[i].npr1_rmg);
        engine->reinitialize_body(configs[i].start_position, configs[i].start_heading);
        worms_.push_back(std::move(engine));
    }

    LOG_INFO("MultiWormSimulation: initialized ", worms_.size(), " worms");
}

void MultiWormSimulation::step() {
    // 1. Compute and apply social O₂ interactions
    apply_social_interactions();

    // 2. Step each worm independently
    for (auto& w : worms_) {
        w->step();
    }

    step_count_++;

    if (callback_) {
        callback_(*this, step_count_);
    }
}

void MultiWormSimulation::run(double duration_s, double snapshot_interval_s) {
    if (worms_.empty()) return;

    double dt_ms = 0.5;  // match SimulationEngine default
    int total_steps = static_cast<int>(duration_s * 1000.0 / dt_ms);
    int snapshot_every = static_cast<int>(snapshot_interval_s * 1000.0 / dt_ms);
    if (snapshot_every < 1) snapshot_every = 1;

    for (int s = 0; s < total_steps; ++s) {
        step();

        // Take snapshot at intervals
        if (s > 0 && (s % snapshot_every == 0)) {
            Snapshot snap;
            snap.time_s = (s * dt_ms) / 1000.0;
            snap.positions.resize(worms_.size());
            snap.speeds.resize(worms_.size());
            for (size_t i = 0; i < worms_.size(); ++i) {
                snap.positions[i] = worms_[i]->body().get_head_position();
                snap.speeds[i] = worms_[i]->body().get_speed();
            }
            snap.metrics = compute_metrics();
            snapshots_.push_back(snap);
        }
    }
}

double MultiWormSimulation::current_time_s() const {
    if (worms_.empty()) return 0;
    return worms_[0]->current_time() / 1000.0;
}

void MultiWormSimulation::apply_social_interactions() {
    int n = static_cast<int>(worms_.size());
    if (n <= 1) return;

    // Collect head AND tail positions
    struct WormPose {
        Vector2d head;
        Vector2d tail;
    };
    std::vector<WormPose> poses(n);
    for (int i = 0; i < n; ++i) {
        poses[i].head = worms_[i]->body().get_head_position();
        poses[i].tail = worms_[i]->body().get_tail_position();
    }

    double R_o2 = social_params_.o2_interaction_radius;
    double R_o2_sq = R_o2 * R_o2;
    double o2_per = social_params_.o2_per_worm;

    double R_soc = social_params_.social_radius;
    double R_soc_sq = R_soc * R_soc;

    for (int i = 0; i < n; ++i) {
        double o2_head = 0.0;
        double o2_tail = 0.0;
        double density_head = 0.0;
        double density_tail = 0.0;

        for (int j = 0; j < n; ++j) {
            if (i == j) continue;

            // Distance from worm i's HEAD to worm j's head
            double dxh = poses[i].head.x - poses[j].head.x;
            double dyh = poses[i].head.y - poses[j].head.y;
            double d2h = dxh * dxh + dyh * dyh;

            // Distance from worm i's TAIL to worm j's head
            double dxt = poses[i].tail.x - poses[j].head.x;
            double dyt = poses[i].tail.y - poses[j].head.y;
            double d2t = dxt * dxt + dyt * dyt;

            // (A) O₂ reduction at HEAD
            if (d2h < R_o2_sq) {
                double dist = std::sqrt(d2h);
                o2_head += o2_per * (1.0 - dist / R_o2);
            }

            // (B) O₂ reduction at TAIL
            if (d2t < R_o2_sq) {
                double dist = std::sqrt(d2t);
                o2_tail += o2_per * (1.0 - dist / R_o2);
            }

            // (C) Neighbor density at HEAD (for dwelling promotion)
            if (d2h < R_soc_sq) {
                double dist = std::sqrt(d2h);
                density_head += 1.0 - (dist / R_soc);
            }

            // (D) Neighbor density at TAIL (for spatial gradient / edge detection)
            // tail_density > head_density → worm heading AWAY from cluster
            if (d2t < R_soc_sq) {
                double dist = std::sqrt(d2t);
                density_tail += 1.0 - (dist / R_soc);
            }
        }

        // Set O₂ reduction (directional gradient for off-food taxis)
        worms_[i]->set_social_o2_reduction(o2_head, o2_tail);

        // Set neighbor density at head AND tail (RMG-gated behavioral modulation)
        worms_[i]->set_social_neighbor_density(density_head, density_tail);
    }
}

MultiWormSimulation::SocialMetrics MultiWormSimulation::compute_metrics() const {
    SocialMetrics m;
    int n = static_cast<int>(worms_.size());
    if (n == 0) return m;

    m.total_steps = step_count_;
    m.time_s = current_time_s();

    // Collect positions and speeds
    std::vector<Vector2d> pos(n);
    std::vector<double> speeds(n);
    for (int i = 0; i < n; ++i) {
        pos[i] = worms_[i]->body().get_head_position();
        speeds[i] = worms_[i]->body().get_speed();
    }

    // Mean speed
    m.mean_speed = std::accumulate(speeds.begin(), speeds.end(), 0.0) / n;

    // Mean nearest-neighbor distance
    double nnd_sum = 0;
    for (int i = 0; i < n; ++i) {
        double min_d = 1e9;
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            double dx = pos[i].x - pos[j].x;
            double dy = pos[i].y - pos[j].y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d < min_d) min_d = d;
        }
        nnd_sum += min_d;
    }
    m.mean_nnd = nnd_sum / n;

    // Cluster fraction: fraction with at least one neighbor within cluster_radius
    double cr = social_params_.cluster_radius;
    double cr2 = cr * cr;
    int in_cluster = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            double dx = pos[i].x - pos[j].x;
            double dy = pos[i].y - pos[j].y;
            if (dx * dx + dy * dy < cr2) {
                in_cluster++;
                break;  // at least one neighbor found
            }
        }
    }
    m.cluster_fraction = static_cast<double>(in_cluster) / n;

    // Cluster count and mean size (union-find)
    m.num_clusters = count_clusters(cr);
    m.mean_cluster_size = (m.num_clusters > 0) ?
        static_cast<double>(n) / m.num_clusters : 0;

    // Pair correlation at short range (<2mm)
    // = (observed pairs within 2mm) / (expected pairs if uniform)
    double pair_r = 2.0;
    double pair_r2 = pair_r * pair_r;
    int pairs_short = 0;
    int total_pairs = n * (n - 1) / 2;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double dx = pos[i].x - pos[j].x;
            double dy = pos[i].y - pos[j].y;
            if (dx * dx + dy * dy < pair_r2) {
                pairs_short++;
            }
        }
    }
    // Expected fraction if uniformly distributed in 50x50 arena
    double arena_area = 50.0 * 50.0;
    double circle_area = 3.14159 * pair_r * pair_r;
    double expected_frac = circle_area / arena_area;
    double observed_frac = (total_pairs > 0) ?
        static_cast<double>(pairs_short) / total_pairs : 0;
    m.pair_correlation_short = (expected_frac > 0) ?
        observed_frac / expected_frac : 0;

    return m;
}

int MultiWormSimulation::count_clusters(double radius) const {
    int n = static_cast<int>(worms_.size());
    if (n == 0) return 0;

    // Union-Find
    std::vector<int> parent(n);
    std::iota(parent.begin(), parent.end(), 0);

    // Find with path compression
    std::function<int(int)> find = [&](int x) -> int {
        if (parent[x] != x) parent[x] = find(parent[x]);
        return parent[x];
    };

    auto unite = [&](int a, int b) {
        int ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    };

    double r2 = radius * radius;
    std::vector<Vector2d> pos(n);
    for (int i = 0; i < n; ++i) {
        pos[i] = worms_[i]->body().get_head_position();
    }

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double dx = pos[i].x - pos[j].x;
            double dy = pos[i].y - pos[j].y;
            if (dx * dx + dy * dy < r2) {
                unite(i, j);
            }
        }
    }

    // Count distinct roots
    std::vector<bool> seen(n, false);
    int clusters = 0;
    for (int i = 0; i < n; ++i) {
        int root = find(i);
        if (!seen[root]) {
            seen[root] = true;
            clusters++;
        }
    }
    return clusters;
}

} // namespace celegans
