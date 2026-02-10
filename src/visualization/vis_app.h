#pragma once

#include "simulation/simulation_engine.h"
#include <vector>
#include <string>

struct GLFWwindow;

namespace celegans {

class VisApp {
public:
    VisApp();
    ~VisApp();

    bool initialize(int width = 1400, int height = 900);
    void run();
    void shutdown();

private:
    void render_frame();
    void render_trajectory_panel();
    void render_neuron_panel();
    void render_control_panel();
    void render_tuning_panel();
    void render_chemical_field();

    void sim_step_batch(int steps);

    GLFWwindow* window_ = nullptr;

    SimulationEngine engine_;
    bool running_ = true;
    bool sim_paused_ = false;
    int sim_speed_ = 1;          // steps per frame
    int steps_per_frame_ = 20;   // default: 20 steps × 0.5ms = 10ms per frame at 60fps

    // Trajectory history
    struct TrajectoryPoint { double x, y, t; };
    std::vector<TrajectoryPoint> trajectory_;
    static constexpr int MAX_TRAJECTORY = 100000;

    // Neuron trace history (ring buffer)
    struct NeuronTrace {
        std::string name;
        int neuron_id;
        std::vector<double> voltages;
        std::vector<double> times;
    };
    std::vector<NeuronTrace> traces_;
    void update_traces();

    // Chemical field snapshot for heatmap
    std::vector<double> chem_field_data_;
    int chem_nx_ = 0, chem_ny_ = 0;
    void update_chemical_field();

    // Heading history
    std::vector<double> heading_times_;
    std::vector<double> heading_values_;  // degrees
    void update_heading();

    // Neuromodulation history (Step 20)
    std::vector<double> neuromod_times_;
    std::vector<double> sht_history_;     // 5-HT concentration [0,1]
    std::vector<double> da_history_;      // DA concentration [0,1]
    std::vector<double> speed_mod_history_; // effective speed scale
    void update_neuromod();

    // Stats
    double ci_sum_ = 0.0;
    int ci_count_ = 0;
    std::vector<double> ci_history_;
    std::vector<double> dist_history_;
    std::vector<double> speed_history_;

    int window_width_ = 1400;
    int window_height_ = 900;
};

} // namespace celegans
