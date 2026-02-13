#pragma once
// ================================================================
// VisApp2 — Modular visualization application (replaces VisApp)
// 
// Architecture:
//   VisApp2 owns:
//     - SimulationEngine (the actual 302-neuron simulation)
//     - DataBus (central data collection hub)
//     - PanelManager (vector of Panel*, handles layout + rendering)
//     - GLFW window + ImGui/ImPlot contexts
//
// All panels are self-contained and read from DataBus.
// Adding a new panel requires: 1) create Panel subclass 2) register in init
// ================================================================

#include "simulation/simulation_engine.h"
#include "visualization_v2/data_bus.h"
#include "visualization_v2/panel.h"
#include <vector>
#include <memory>
#include <string>

struct GLFWwindow;

namespace celegans {

class VisApp2 {
public:
    VisApp2();
    ~VisApp2();

    bool initialize(int width = 1920, int height = 1080);
    void run();
    void shutdown();

private:
    void register_panels();
    void compute_layout();
    void render_frame();
    void render_menu_bar();
    void sim_step_batch(int steps);

    GLFWwindow* window_ = nullptr;
    int window_width_ = 1920;
    int window_height_ = 1080;

    SimulationEngine engine_;
    DataBus bus_;
    std::vector<std::unique_ptr<Panel>> panels_;

    bool running_ = true;
    bool sim_paused_ = false;
    int steps_per_frame_ = 20;
};

} // namespace celegans