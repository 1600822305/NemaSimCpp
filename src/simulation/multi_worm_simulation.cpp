#include "multi_worm_simulation.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace celegans {

// ================================================================
// Step 128: Multi-Worm Simulation
// Each worm gets its own SimulationEngine with independent 302-neuron
// nervous system. Worms interact via:
//   1. Shared pheromone field (ascaroside deposition + sensing)
//   2. Physical collision avoidance (repulsive force)
//   3. Local density signals (neighbor count → NPR-1/RMG modulation)
// ================================================================

MultiWormSimulation::MultiWormSimulation(int num_worms, unsigned int base_seed, int num_threads)
    : num_worms_(num_worms), base_seed_(base_seed) {
    num_threads_ = (num_threads <= 0)
        ? std::min(num_worms, (int)std::thread::hardware_concurrency())
        : num_threads;
    worms_.reserve(num_worms);
    for (int i = 0; i < num_worms; ++i) {
        worms_.push_back(std::make_unique<SimulationEngine>());
    }
}

void MultiWormSimulation::initialize() {
    for (int i = 0; i < num_worms_; ++i) {
        worms_[i]->initialize_default();
        worms_[i]->set_rng_seed(base_seed_ + static_cast<unsigned int>(i) * 137);

        // NPR-1 override for social/solitary phenotype
        if (npr1_override_ > -900.0) {
            worms_[i]->set_npr1_rmg(npr1_override_);
        }

        // Scatter worms across arena (50mm × 50mm)
        // Place in a grid-like pattern near food source (35,25)
        double angle = 2.0 * 3.14159265 * i / num_worms_;
        double radius = 3.0 + 2.0 * (i % 3);  // 3-7mm from food center
        double x = 35.0 + radius * std::cos(angle);
        double y = 25.0 + radius * std::sin(angle);
        worms_[i]->body_mut().set_position(x, y);
        worms_[i]->body_mut().set_heading(angle + 3.14159265);  // face food
    }
}

void MultiWormSimulation::step() {
    // 1. Update shared pheromone field (each worm deposits ascaroside)
    update_shared_pheromone();

    // 2. Update worm density signals for each worm
    update_worm_density_signals();

    // 3. Step all worms in parallel (thread pool)
    if (num_threads_ > 1 && num_worms_ > 1) {
        auto worker = [&](int start, int end) {
            for (int i = start; i < end; ++i)
                worms_[i]->step();
        };
        int chunk = (num_worms_ + num_threads_ - 1) / num_threads_;
        std::vector<std::thread> threads;
        for (int t = 0; t < num_threads_; ++t) {
            int s = t * chunk;
            int e = std::min(s + chunk, num_worms_);
            if (s < e) threads.emplace_back(worker, s, e);
        }
        for (auto& th : threads) th.join();
    } else {
        for (auto& worm : worms_)
            worm->step();
    }

    // 4. Apply physical worm-worm interactions (collision avoidance)
    apply_worm_interactions();

    current_time_ += dt_;
    step_count_++;

    if (step_callback_) {
        step_callback_(*this, step_count_);
    }
}

void MultiWormSimulation::run(double duration_ms) {
    int steps = static_cast<int>(duration_ms / dt_);
    for (int i = 0; i < steps; ++i) {
        step();
    }
}

// ================================================================
// Pheromone deposition: each worm deposits ascaroside at head position
// Pheromone diffuses in shared field → sensed by other worms via ADL
// REF: Srinivasan 2008 — ascarosides as water-soluble social signals
// ================================================================
void MultiWormSimulation::update_shared_pheromone() {
    // Every 200ms (400 steps), update pheromone sources in all worms' environments
    // More frequent than 1s to track worm movement (0.2mm/s × 0.2s = 0.04mm)
    if (step_count_ % 400 != 0) return;

    for (int i = 0; i < num_worms_; ++i) {
        auto& env_i = worms_[i]->environment();

        // Clear old pheromone and rebuild from all OTHER worms
        env_i.pheromone_field().clear();
        for (int j = 0; j < num_worms_; ++j) {
            if (j == i) continue;  // don't sense own pheromone
            Vector2d pos_j = worms_[j]->body().get_head_position();
            // Each worm deposits pheromone (intensity 1.5, σ²=2mm²)
            // Strong local signal: at 1mm distance → 1.5×exp(-0.25) = 1.17
            // At 2mm → 1.5×exp(-1.0) = 0.55 → ADL drive = 40×0.55/0.75 = 29pA
            // REF: Srinivasan 2008 — ascarosides are water-soluble, local
            env_i.pheromone_field().add_point_source(pos_j, 1.5, 2.0);
        }
        // Mark pheromone as present if there are other worms
        if (num_worms_ > 1) {
            env_i.set_pheromone_source({0, 0}, 0.0);  // just set flag
        }
    }
}

// ================================================================
// Local worm density: count neighbors within 2mm → inject into RMG circuit
// Density → speed modulation via NPR-1/RMG (Step 96)
// High density + npr-1(lf) → slowing + aggregation
// REF: Ding 2019 eLife — density-dependent speed switching
// ================================================================
void MultiWormSimulation::update_worm_density_signals() {
    // Every 200ms (400 steps), update density count for each worm
    if (step_count_ % 400 != 0) return;

    for (int i = 0; i < num_worms_; ++i) {
        Vector2d pos_i = worms_[i]->body().get_head_position();
        int neighbor_count = 0;

        for (int j = 0; j < num_worms_; ++j) {
            if (j == i) continue;
            Vector2d pos_j = worms_[j]->body().get_head_position();
            double dx = pos_i.x - pos_j.x;
            double dy = pos_i.y - pos_j.y;
            double dist = std::sqrt(dx * dx + dy * dy);
            if (dist < 3.0) neighbor_count++;  // 3mm neighbor radius
        }

        // Set density via public API → SimulationEngine applies RMG drive internally
        // Rule 2 (Ding 2019): density-dependent speed modulation
        // NPR-1 gating happens inside SimulationEngine::step()
        worms_[i]->set_neighbor_density(neighbor_count);
    }
}

// ================================================================
// Physical collision: simple soft repulsion between worm heads
// Prevents overlapping bodies (no complex 2D body collision)
// ================================================================
void MultiWormSimulation::apply_worm_interactions() {
    const double repulsion_radius = 0.8;  // mm, body width ~ 0.08mm but heads avoid closer
    const double repulsion_strength = 0.02; // mm per step displacement

    for (int i = 0; i < num_worms_; ++i) {
        Vector2d pos_i = worms_[i]->body().get_head_position();

        for (int j = i + 1; j < num_worms_; ++j) {
            Vector2d pos_j = worms_[j]->body().get_head_position();
            double dx = pos_i.x - pos_j.x;
            double dy = pos_i.y - pos_j.y;
            double dist = std::sqrt(dx * dx + dy * dy);

            if (dist < repulsion_radius && dist > 0.001) {
                // Push apart along connecting line
                double push = repulsion_strength * (repulsion_radius - dist) / dist;
                double push_x = dx * push;
                double push_y = dy * push;

                worms_[i]->body_mut().nudge_position(push_x, push_y);
                worms_[j]->body_mut().nudge_position(-push_x, -push_y);
            }
        }
    }
}

// ================================================================
// Statistics: cluster analysis, nearest neighbor, speed
// ================================================================
MultiWormStats MultiWormSimulation::compute_stats() const {
    MultiWormStats stats;
    stats.num_worms = num_worms_;

    if (num_worms_ == 0) return stats;

    // Collect positions
    std::vector<Vector2d> positions(num_worms_);
    std::vector<double> speeds(num_worms_);
    for (int i = 0; i < num_worms_; ++i) {
        positions[i] = worms_[i]->body().get_head_position();
        speeds[i] = worms_[i]->body().get_speed();
    }

    // Nearest neighbor distances
    double nn_sum = 0.0;
    for (int i = 0; i < num_worms_; ++i) {
        double min_dist = 1e9;
        for (int j = 0; j < num_worms_; ++j) {
            if (j == i) continue;
            double dx = positions[i].x - positions[j].x;
            double dy = positions[i].y - positions[j].y;
            double d = std::sqrt(dx * dx + dy * dy);
            if (d < min_dist) min_dist = d;
        }
        nn_sum += min_dist;
    }
    stats.mean_nearest_neighbor = nn_sum / num_worms_;

    // Mean speed
    stats.mean_speed = 0;
    for (double s : speeds) stats.mean_speed += s;
    stats.mean_speed /= num_worms_;

    // Cluster analysis (union-find with 2mm threshold)
    std::vector<int> parent(num_worms_);
    std::iota(parent.begin(), parent.end(), 0);
    auto find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (int i = 0; i < num_worms_; ++i) {
        for (int j = i + 1; j < num_worms_; ++j) {
            double dx = positions[i].x - positions[j].x;
            double dy = positions[i].y - positions[j].y;
            if (std::sqrt(dx * dx + dy * dy) < 2.0) {
                int ri = find(i), rj = find(j);
                if (ri != rj) parent[ri] = rj;
            }
        }
    }
    // Count clusters and members
    std::vector<int> cluster_size(num_worms_, 0);
    for (int i = 0; i < num_worms_; ++i) {
        cluster_size[find(i)]++;
    }
    int num_clusters = 0;
    int in_cluster = 0;
    for (int i = 0; i < num_worms_; ++i) {
        if (cluster_size[i] > 0) num_clusters++;
        if (cluster_size[find(i)] > 1) in_cluster++;
    }
    stats.num_clusters = num_clusters;
    stats.cluster_fraction = static_cast<double>(in_cluster) / num_worms_;

    // Total eggs
    for (int i = 0; i < num_worms_; ++i) {
        stats.total_eggs += static_cast<int>(worms_[i]->egg_laid_count());
    }

    return stats;
}

} // namespace celegans
