#include "visualization/vis_app.h"
#include "core/logger.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <algorithm>
#include <numeric>

namespace celegans {

VisApp::VisApp() {}

VisApp::~VisApp() {
    shutdown();
}

bool VisApp::initialize(int width, int height) {
    window_width_ = width;
    window_height_ = height;

    // GLFW init
    glfwSetErrorCallback([](int error, const char* desc) {
        (void)error; (void)desc; // suppress unused warning
    });
    if (!glfwInit()) {
        LOG_ERROR("glfwInit failed");
        return false;
    }

    // OpenGL 3.3 Core
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(width, height,
        "C. elegans Neural Simulation - Real-time Visualization", nullptr, nullptr);
    if (!window_) {
        LOG_ERROR("glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // vsync

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.2f;

    // Dark theme with custom colors
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Initialize simulation engine
    engine_.initialize_default();

    // Setup neuron traces for key neurons
    auto add_trace = [&](const char* name) {
        int id = engine_.connectome().get_neuron_id(name);
        if (id >= 0) {
            traces_.push_back({name, id, {}, {}});
        }
    };
    add_trace("SMDDL");
    add_trace("SMDVL");
    add_trace("AVAL");
    add_trace("AVBL");
    add_trace("AIBL");
    add_trace("AIYL");

    // Initialize chemical field data
    update_chemical_field();

    // Record initial position
    auto head = engine_.body().get_head_position();
    trajectory_.push_back({head.x, head.y, 0.0});

    LOG_INFO("Visualization initialized: ", width, "x", height);
    return true;
}

void VisApp::shutdown() {
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

void VisApp::run() {
    while (running_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Keyboard shortcuts (only when ImGui doesn't want keyboard)
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard) {
            if (glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS) {
                static bool space_was_pressed = false;
                if (!space_was_pressed) { sim_paused_ = !sim_paused_; space_was_pressed = true; }
            } else {
                static bool space_was_pressed = false;
                space_was_pressed = false;
            }
            if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                running_ = false;
        }

        // Simulation steps
        if (!sim_paused_) {
            sim_step_batch(steps_per_frame_);
        }

        // Render
        render_frame();
    }
}

void VisApp::sim_step_batch(int steps) {
    Vector2d food_pos{35.0, 35.0};
    for (int i = 0; i < steps; ++i) {
        engine_.step();

        // Record trajectory every 10 steps (5ms)
        if (engine_.get_step_count() % 10 == 0) {
            auto head = engine_.body().get_head_position();
            double t = engine_.current_time();
            trajectory_.push_back({head.x, head.y, t});
            if (trajectory_.size() > MAX_TRAJECTORY) {
                trajectory_.erase(trajectory_.begin(),
                    trajectory_.begin() + (trajectory_.size() - MAX_TRAJECTORY));
            }

            // Distance & CI
            double dx = head.x - food_pos.x;
            double dy = head.y - food_pos.y;
            double dist = std::sqrt(dx * dx + dy * dy);

            // Record every 100ms
            if (engine_.get_step_count() % 200 == 0) {
                dist_history_.push_back(dist);
                speed_history_.push_back(engine_.body().get_speed());

                // Simple running CI: cos(angle to food)
                if (trajectory_.size() >= 2) {
                    auto& p1 = trajectory_[trajectory_.size() - 2];
                    auto& p2 = trajectory_.back();
                    double mvx = p2.x - p1.x;
                    double mvy = p2.y - p1.y;
                    double mv_len = std::sqrt(mvx * mvx + mvy * mvy);
                    if (mv_len > 1e-8) {
                        double to_food_x = food_pos.x - p2.x;
                        double to_food_y = food_pos.y - p2.y;
                        double tf_len = std::sqrt(to_food_x * to_food_x + to_food_y * to_food_y);
                        if (tf_len > 1e-8) {
                            double ci = (mvx * to_food_x + mvy * to_food_y) / (mv_len * tf_len);
                            ci_sum_ += ci;
                            ci_count_++;
                            ci_history_.push_back(ci_sum_ / ci_count_);
                        }
                    }
                }
            }
        }

        // Update neuron traces every step
        update_traces();
    }
}

void VisApp::update_traces() {
    double t = engine_.current_time();
    const auto& neurons = engine_.neurons();
    int n = static_cast<int>(neurons.size());
    for (auto& tr : traces_) {
        if (tr.neuron_id >= 0 && tr.neuron_id < n) {
            tr.voltages.push_back(neurons[tr.neuron_id]->get_membrane_potential());
            tr.times.push_back(t);
            // Keep last 20000 points (~10s at dt=0.5ms)
            if (tr.voltages.size() > 20000) {
                tr.voltages.erase(tr.voltages.begin(), tr.voltages.begin() + 10000);
                tr.times.erase(tr.times.begin(), tr.times.begin() + 10000);
            }
        }
    }
}

void VisApp::update_chemical_field() {
    const auto& cf = engine_.environment().chemical_field();
    chem_nx_ = 50;
    chem_ny_ = 50;
    chem_field_data_.resize(chem_nx_ * chem_ny_);
    double cell_w = 50.0 / chem_nx_;
    double cell_h = 50.0 / chem_ny_;
    for (int iy = 0; iy < chem_ny_; ++iy) {
        for (int ix = 0; ix < chem_nx_; ++ix) {
            double cx = (ix + 0.5) * cell_w;
            double cy = (iy + 0.5) * cell_h;
            chem_field_data_[iy * chem_nx_ + ix] = cf.sample({cx, cy});
        }
    }
}

void VisApp::render_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Get window size
    glfwGetFramebufferSize(window_, &window_width_, &window_height_);

    // Full-screen dockspace-like layout
    float left_w = window_width_ * 0.55f;
    float right_w = window_width_ - left_w;
    float top_h = window_height_ * 0.6f;
    float bottom_h = window_height_ - top_h;

    // Panel 1: Trajectory + Chemical Field (top-left)
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(left_w, top_h));
    render_trajectory_panel();

    // Panel 2: Neuron activity (top-right)
    ImGui::SetNextWindowPos(ImVec2(left_w, 0));
    ImGui::SetNextWindowSize(ImVec2(right_w, top_h));
    render_neuron_panel();

    // Panel 3: Chemical field heatmap (bottom-left)
    ImGui::SetNextWindowPos(ImVec2(0, top_h));
    ImGui::SetNextWindowSize(ImVec2(left_w, bottom_h));
    render_chemical_field();

    // Panel 4: Control panel (bottom-right)
    ImGui::SetNextWindowPos(ImVec2(left_w, top_h));
    ImGui::SetNextWindowSize(ImVec2(right_w, bottom_h));
    render_control_panel();

    // Rendering
    ImGui::Render();
    glViewport(0, 0, window_width_, window_height_);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void VisApp::render_trajectory_panel() {
    ImGui::Begin("Trajectory & Arena", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (ImPlot::BeginPlot("##trajectory", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("X (mm)", "Y (mm)");
        ImPlot::SetupAxesLimits(0, 50, 0, 50, ImPlotCond_Once);

        // Chemical field as background (scatter heatmap approximation)
        // Draw trajectory
        if (trajectory_.size() > 1) {
            std::vector<double> xs, ys;
            xs.reserve(trajectory_.size());
            ys.reserve(trajectory_.size());
            for (auto& p : trajectory_) {
                xs.push_back(p.x);
                ys.push_back(p.y);
            }
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.8f, 0.4f, 0.7f), 1.5f);
            ImPlot::PlotLine("Trajectory", xs.data(), ys.data(), (int)xs.size());

            // Current head position
            double hx = xs.back(), hy = ys.back();
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6, ImVec4(0, 1, 0, 1), 2);
            ImPlot::PlotScatter("Head", &hx, &hy, 1);
        }

        // Start position
        double sx = 25.0, sy = 25.0;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 6, ImVec4(0.5f, 0.5f, 1, 1), 2);
        ImPlot::PlotScatter("Start", &sx, &sy, 1);

        // Food source
        double fx = 35.0, fy = 35.0;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 10, ImVec4(1, 0.3f, 0.3f, 1), 2);
        ImPlot::PlotScatter("Food", &fx, &fy, 1);

        // Concentration contours (circles at sigma, 2sigma)
        auto draw_circle = [](double cx, double cy, double r, int npts = 64) {
            std::vector<double> xs(npts + 1), ys(npts + 1);
            for (int i = 0; i <= npts; ++i) {
                double a = 2.0 * 3.14159265 * i / npts;
                xs[i] = cx + r * std::cos(a);
                ys[i] = cy + r * std::sin(a);
            }
            ImPlot::PlotLine("##contour", xs.data(), ys.data(), npts + 1);
        };
        ImPlot::SetNextLineStyle(ImVec4(1, 0.3f, 0.3f, 0.2f), 1);
        draw_circle(35, 35, 5.0);  // 1σ
        ImPlot::SetNextLineStyle(ImVec4(1, 0.3f, 0.3f, 0.1f), 1);
        draw_circle(35, 35, 10.0); // 2σ
        ImPlot::SetNextLineStyle(ImVec4(1, 0.3f, 0.3f, 0.05f), 1);
        draw_circle(35, 35, 15.0); // 3σ

        ImPlot::EndPlot();
    }

    ImGui::End();
}

void VisApp::render_neuron_panel() {
    ImGui::Begin("Neuron Activity", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (!traces_.empty() && !traces_[0].times.empty()) {
        double t_now = traces_[0].times.back();
        double t_window = 5000.0; // show last 5 seconds

        // SMD panel (half-center oscillator)
        if (ImPlot::BeginPlot("SMD Half-Center (Head Oscillator)", ImVec2(-1, 0),
                ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Time (ms)", "V (mV)");
            ImPlot::SetupAxesLimits(t_now - t_window, t_now, -80, -10, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);

            static const ImVec4 colors[] = {
                {0.2f, 0.6f, 1.0f, 1.0f},  // SMDDL - blue
                {1.0f, 0.4f, 0.2f, 1.0f},  // SMDVL - orange
            };

            for (int i = 0; i < 2 && i < (int)traces_.size(); ++i) {
                auto& tr = traces_[i];
                if (tr.times.empty()) continue;
                ImPlot::SetNextLineStyle(colors[i], 1.5f);
                ImPlot::PlotLine(tr.name.c_str(),
                    tr.times.data(), tr.voltages.data(), (int)tr.times.size());
            }
            ImPlot::EndPlot();
        }

        // Command interneurons + key interneurons
        if (ImPlot::BeginPlot("Command & Interneurons", ImVec2(-1, 0),
                ImPlotFlags_NoLegend)) {
            ImPlot::SetupAxes("Time (ms)", "V (mV)");
            ImPlot::SetupAxesLimits(t_now - t_window, t_now, -80, -10, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);

            static const ImVec4 colors2[] = {
                {1.0f, 0.2f, 0.2f, 1.0f},  // AVA - red
                {0.2f, 1.0f, 0.2f, 1.0f},  // AVB - green
                {0.8f, 0.6f, 0.2f, 1.0f},  // AIB - yellow
                {0.6f, 0.2f, 1.0f, 1.0f},  // AIY - purple
            };

            for (int i = 2; i < (int)traces_.size(); ++i) {
                auto& tr = traces_[i];
                if (tr.times.empty()) continue;
                ImPlot::SetNextLineStyle(colors2[i - 2], 1.5f);
                ImPlot::PlotLine(tr.name.c_str(),
                    tr.times.data(), tr.voltages.data(), (int)tr.times.size());
            }
            ImPlot::EndPlot();
        }
    }

    ImGui::End();
}

void VisApp::render_chemical_field() {
    ImGui::Begin("Stats & Metrics", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    float plot_h = ImGui::GetContentRegionAvail().y * 0.5f;

    // Distance to food
    if (!dist_history_.empty()) {
        if (ImPlot::BeginPlot("Distance to Food", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes("Sample (×100ms)", "Distance (mm)");
            ImPlot::SetupAxesLimits(0, (double)dist_history_.size(), 0, 20, ImPlotCond_Always);
            ImPlot::SetNextLineStyle(ImVec4(1, 0.6f, 0.2f, 1), 2);
            ImPlot::PlotLine("Dist", dist_history_.data(), (int)dist_history_.size());
            ImPlot::EndPlot();
        }
    }

    // Chemotaxis index
    if (!ci_history_.empty()) {
        if (ImPlot::BeginPlot("Chemotaxis Index (CI)", ImVec2(-1, -1))) {
            ImPlot::SetupAxes("Sample (×100ms)", "CI");
            ImPlot::SetupAxesLimits(0, (double)ci_history_.size(), -1, 1, ImPlotCond_Always);
            // Zero line
            double zx[2] = {0, (double)ci_history_.size()};
            double zy[2] = {0, 0};
            ImPlot::SetNextLineStyle(ImVec4(0.5f, 0.5f, 0.5f, 0.5f), 1);
            ImPlot::PlotLine("##zero", zx, zy, 2);
            // CI
            ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.8f, 1.0f, 1), 2);
            ImPlot::PlotLine("CI", ci_history_.data(), (int)ci_history_.size());
            ImPlot::EndPlot();
        }
    }

    ImGui::End();
}

void VisApp::render_control_panel() {
    ImGui::Begin("Control Panel", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Sim info
    double t = engine_.current_time();
    ImGui::Text("Simulation Time: %.1f ms (%.2f s)", t, t / 1000.0);
    ImGui::Text("Steps: %d", engine_.get_step_count());

    ImGui::Separator();

    // Position info
    auto head = engine_.body().get_head_position();
    Vector2d food{35.0, 35.0};
    double dx = head.x - food.x;
    double dy = head.y - food.y;
    double dist = std::sqrt(dx * dx + dy * dy);
    ImGui::Text("Head: (%.2f, %.2f)", head.x, head.y);
    ImGui::Text("Distance to Food: %.2f mm", dist);
    ImGui::Text("Speed: %.4f mm/s", engine_.body().get_speed());
    if (ci_count_ > 0)
        ImGui::Text("Chemotaxis Index: %.3f", ci_sum_ / ci_count_);

    ImGui::Separator();

    // Controls
    if (ImGui::Button(sim_paused_ ? "Resume [Space]" : "Pause [Space]")) {
        sim_paused_ = !sim_paused_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        engine_.initialize_default();
        trajectory_.clear();
        for (auto& tr : traces_) {
            tr.voltages.clear();
            tr.times.clear();
        }
        dist_history_.clear();
        ci_history_.clear();
        speed_history_.clear();
        ci_sum_ = 0;
        ci_count_ = 0;
        auto h = engine_.body().get_head_position();
        trajectory_.push_back({h.x, h.y, 0.0});
        update_chemical_field();
    }

    ImGui::SliderInt("Steps/Frame", &steps_per_frame_, 1, 200);
    ImGui::Text("Sim Speed: %.0fx realtime", steps_per_frame_ * 0.5 * 60.0 / 1000.0);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Keyboard:");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "  Space = Pause/Resume");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "  Esc = Quit");

    ImGui::Separator();

    // Neuron count summary
    ImGui::Text("Neurons: %d", (int)engine_.neurons().size());
    ImGui::Text("Synapses: %d chem + %d gap",
        engine_.connectome().num_synapses(),
        engine_.connectome().num_gap_junctions());

    ImGui::End();
}

} // namespace celegans
