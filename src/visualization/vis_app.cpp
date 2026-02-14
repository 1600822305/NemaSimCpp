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
        "C. elegans Simulation", nullptr, nullptr);
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

    // Load Chinese font (Microsoft YaHei)
    const char* font_paths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    bool font_loaded = false;
    for (auto* path : font_paths) {
        FILE* f = fopen(path, "rb");
        if (f) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseFull());
            font_loaded = true;
            break;
        }
    }
    if (!font_loaded) {
        LOG_INFO("No Chinese font found, using default");
    }

    // Initialize simulation engine
    engine_.initialize_default();

    // Setup neuron traces for key neurons
    auto add_trace = [&](const char* name) {
        int id = engine_.connectome().get_neuron_id(name);
        if (id >= 0) {
            traces_.push_back({name, id, {}, {}});
        }
    };
    // Group 0-1: SMD half-center
    add_trace("SMDDL");  // [0]
    add_trace("SMDVL");  // [1]
    // Group 2-5: Command + interneurons
    add_trace("AVAL");   // [2]
    add_trace("AVBL");   // [3]
    add_trace("AIBL");   // [4]
    add_trace("AIYL");   // [5]
    // Group 6-7: Sensory L/R (for gradient asymmetry)
    add_trace("ASEL");   // [6]
    add_trace("ASER");   // [7]

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

        // Record heading every 20 steps (10ms)
        if (engine_.get_step_count() % 20 == 0) {
            update_heading();
            update_neuromod();
        }
    }
}

void VisApp::update_heading() {
    double t = engine_.current_time();
    double heading_deg = engine_.body().get_head_angle() * 180.0 / 3.14159265;
    heading_times_.push_back(t);
    heading_values_.push_back(heading_deg);
    if (heading_times_.size() > 20000) {
        heading_times_.erase(heading_times_.begin(), heading_times_.begin() + 10000);
        heading_values_.erase(heading_values_.begin(), heading_values_.begin() + 10000);
    }
}

void VisApp::update_neuromod() {
    double t = engine_.current_time();
    neuromod_times_.push_back(t);
    sht_history_.push_back(engine_.neuromodulation().get_concentration("5-HT"));
    da_history_.push_back(engine_.neuromodulation().get_concentration("DA"));
    oa_history_.push_back(engine_.neuromodulation().get_concentration("OA"));
    satiety_history_.push_back(engine_.satiety());
    fmem_history_.push_back(engine_.food_memory());
    speed_mod_history_.push_back(engine_.neuromodulation().get_muscle_gain());
    // Keep last 60000 points (~600s at 10ms interval)
    if (neuromod_times_.size() > 60000) {
        neuromod_times_.erase(neuromod_times_.begin(), neuromod_times_.begin() + 30000);
        sht_history_.erase(sht_history_.begin(), sht_history_.begin() + 30000);
        da_history_.erase(da_history_.begin(), da_history_.begin() + 30000);
        oa_history_.erase(oa_history_.begin(), oa_history_.begin() + 30000);
        satiety_history_.erase(satiety_history_.begin(), satiety_history_.begin() + 30000);
        fmem_history_.erase(fmem_history_.begin(), fmem_history_.begin() + 30000);
        speed_mod_history_.erase(speed_mod_history_.begin(), speed_mod_history_.begin() + 30000);
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

    // 3-column layout: left(trajectory) | middle(waveforms) | right(tuning+control)
    float col1_w = window_width_ * 0.30f;
    float col2_w = window_width_ * 0.42f;
    float col3_w = window_width_ - col1_w - col2_w;
    float full_h = (float)window_height_;

    // Column 1: Trajectory + Stats
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(col1_w, full_h * 0.6f));
    render_trajectory_panel();

    ImGui::SetNextWindowPos(ImVec2(0, full_h * 0.6f));
    ImGui::SetNextWindowSize(ImVec2(col1_w, full_h * 0.4f));
    render_chemical_field();

    // Column 2: All waveforms (SMD, Command, Sensory, Heading)
    ImGui::SetNextWindowPos(ImVec2(col1_w, 0));
    ImGui::SetNextWindowSize(ImVec2(col2_w, full_h));
    render_neuron_panel();

    // Column 3: Tuning + Control
    ImGui::SetNextWindowPos(ImVec2(col1_w + col2_w, 0));
    ImGui::SetNextWindowSize(ImVec2(col3_w, full_h * 0.55f));
    render_tuning_panel();

    ImGui::SetNextWindowPos(ImVec2(col1_w + col2_w, full_h * 0.55f));
    ImGui::SetNextWindowSize(ImVec2(col3_w, full_h * 0.45f));
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
    ImGui::Begin(u8"\u8f68\u8ff9\u4e0e\u7ade\u6280\u573a", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (ImPlot::BeginPlot("##trajectory", ImVec2(-1, -1), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes(u8"X (mm)", u8"Y (mm)");
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
            ImPlot::PlotLine(u8"\u8f68\u8ff9", xs.data(), ys.data(), (int)xs.size());

            // Current head position
            double hx = xs.back(), hy = ys.back();
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 6, ImVec4(0, 1, 0, 1), 2);
            ImPlot::PlotScatter(u8"\u5934\u90e8", &hx, &hy, 1);
        }

        // Start position
        double sx = 25.0, sy = 25.0;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 6, ImVec4(0.5f, 0.5f, 1, 1), 2);
        ImPlot::PlotScatter(u8"\u8d77\u70b9", &sx, &sy, 1);

        // Food source
        double fx = 35.0, fy = 35.0;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 10, ImVec4(1, 0.3f, 0.3f, 1), 2);
        ImPlot::PlotScatter(u8"\u98df\u7269", &fx, &fy, 1);

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
    ImGui::Begin(u8"\u795e\u7ecf\u5143\u6d3b\u52a8", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (!traces_.empty() && !traces_[0].times.empty()) {
        double t_now = traces_[0].times.back();
        double t_window = 5000.0;
        float avail_h = ImGui::GetContentRegionAvail().y;
        float plot_h = avail_h * 0.19f;

        // Helper: get last N values min/max for amplitude annotation
        auto get_range = [](const std::vector<double>& v, int last_n) -> std::pair<double,double> {
            if (v.empty()) return {0,0};
            int start = std::max(0, (int)v.size() - last_n);
            double mn = v[start], mx = v[start];
            for (int i = start; i < (int)v.size(); ++i) {
                if (v[i] < mn) mn = v[i];
                if (v[i] > mx) mx = v[i];
            }
            return {mn, mx};
        };

        // --- Plot 1: SMD half-center ---
        if (ImPlot::BeginPlot(u8"SMD \u534a\u4e2d\u5fc3 (\u5934\u90e8\u632f\u8361)", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"mV");
            ImPlot::SetupAxesLimits(t_now - t_window, t_now, -80, -10, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);
            static const ImVec4 c[] = {{0.2f,0.6f,1,1},{1,0.4f,0.2f,1}};
            for (int i = 0; i < 2 && i < (int)traces_.size(); ++i) {
                auto& tr = traces_[i];
                if (tr.times.empty()) continue;
                ImPlot::SetNextLineStyle(c[i], 1.5f);
                ImPlot::PlotLine(tr.name.c_str(), tr.times.data(), tr.voltages.data(), (int)tr.times.size());
            }
            ImPlot::EndPlot();
        }
        // Amplitude annotation
        if (traces_.size() >= 2) {
            auto [d_min, d_max] = get_range(traces_[0].voltages, 2000);
            auto [v_min, v_max] = get_range(traces_[1].voltages, 2000);
            ImGui::TextColored(ImVec4(0.5f,0.8f,1,1), u8"  SMDDL: %.1f~%.1f mV (\u0394%.1f)  SMDVL: %.1f~%.1f mV (\u0394%.1f)",
                d_min, d_max, d_max-d_min, v_min, v_max, v_max-v_min);
        }

        // --- Plot 2: Command + Interneurons ---
        if (ImPlot::BeginPlot(u8"\u547d\u4ee4\u795e\u7ecf\u5143 (AVA/AVB/AIB/AIY)", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"mV");
            ImPlot::SetupAxesLimits(t_now - t_window, t_now, -80, -10, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);
            static const ImVec4 c2[] = {{1,0.2f,0.2f,1},{0.2f,1,0.2f,1},{0.8f,0.6f,0.2f,1},{0.6f,0.2f,1,1}};
            for (int i = 2; i < 6 && i < (int)traces_.size(); ++i) {
                auto& tr = traces_[i];
                if (tr.times.empty()) continue;
                ImPlot::SetNextLineStyle(c2[i-2], 1.5f);
                ImPlot::PlotLine(tr.name.c_str(), tr.times.data(), tr.voltages.data(), (int)tr.times.size());
            }
            ImPlot::EndPlot();
        }

        // --- Plot 3: ASEL vs ASER (gradient asymmetry) ---
        if (traces_.size() >= 8) {
            if (ImPlot::BeginPlot(u8"\u611f\u89c9\u795e\u7ecf\u5143 ASEL/ASER (\u68af\u5ea6\u4e0d\u5bf9\u79f0)", ImVec2(-1, plot_h))) {
                ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"mV");
                ImPlot::SetupAxesLimits(t_now - t_window, t_now, -80, -10, ImPlotCond_Always);
                ImPlot::SetupLegend(ImPlotLocation_NorthEast);
                ImPlot::SetNextLineStyle(ImVec4(0,0.8f,0.8f,1), 1.5f);
                ImPlot::PlotLine(traces_[6].name.c_str(), traces_[6].times.data(), traces_[6].voltages.data(), (int)traces_[6].times.size());
                ImPlot::SetNextLineStyle(ImVec4(1,0.8f,0,1), 1.5f);
                ImPlot::PlotLine(traces_[7].name.c_str(), traces_[7].times.data(), traces_[7].voltages.data(), (int)traces_[7].times.size());
                ImPlot::EndPlot();
            }
            auto [l_min, l_max] = get_range(traces_[6].voltages, 2000);
            auto [r_min, r_max] = get_range(traces_[7].voltages, 2000);
            double l_mean = (l_min + l_max) * 0.5;
            double r_mean = (r_min + r_max) * 0.5;
            ImGui::TextColored(ImVec4(0.5f,1,0.8f,1), u8"  ASEL: %.1f mV  ASER: %.1f mV  \u5dee\u503c: %.2f mV",
                l_mean, r_mean, l_mean - r_mean);
        }

        // --- Plot 4: Heading angle ---
        if (!heading_times_.empty()) {
            if (ImPlot::BeginPlot(u8"\u5934\u90e8\u65b9\u5411\u89d2 (\u00b0)", ImVec2(-1, plot_h))) {
                ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"\u89d2\u5ea6(\u00b0)");
                double h_min = heading_values_.back() - 90;
                double h_max = heading_values_.back() + 90;
                ImPlot::SetupAxesLimits(t_now - t_window, t_now, h_min, h_max, ImPlotCond_Always);
                ImPlot::SetNextLineStyle(ImVec4(1,1,0.3f,1), 2.0f);
                ImPlot::PlotLine(u8"\u65b9\u5411\u89d2", heading_times_.data(), heading_values_.data(), (int)heading_times_.size());
                ImPlot::EndPlot();
            }
            // Heading change rate
            if (heading_values_.size() >= 100) {
                int n = (int)heading_values_.size();
                double dt_sec = (heading_times_[n-1] - heading_times_[n-100]) / 1000.0;
                double dh = heading_values_[n-1] - heading_values_[n-100];
                double rate = (dt_sec > 0.01) ? std::abs(dh / dt_sec) : 0;
                ImGui::TextColored(ImVec4(1,1,0.3f,1), u8"  \u89d2\u901f\u5ea6: %.2f \u00b0/s   \u5f53\u524d\u65b9\u5411: %.1f\u00b0",
                    rate, heading_values_.back());
            }
        }

        // --- Plot 5: Neuromodulation (Step 20) ---
        // Longer time window (30s) since modulators change on seconds timescale
        if (!neuromod_times_.empty()) {
            double nm_window = 30000.0;  // 30s window
            if (ImPlot::BeginPlot(u8"\u795e\u7ecf\u8c03\u8d28 5-HT/DA (Layer 6)", ImVec2(-1, plot_h))) {
                ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"\u6d53\u5ea6");
                ImPlot::SetupAxesLimits(t_now - nm_window, t_now, 0, 1.1, ImPlotCond_Always);
                ImPlot::SetupLegend(ImPlotLocation_NorthEast);
                // 5-HT: magenta/pink
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.3f, 0.7f, 1), 2.0f);
                ImPlot::PlotLine("5-HT", neuromod_times_.data(), sht_history_.data(), (int)neuromod_times_.size());
                // DA: cyan/blue
                ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.8f, 1.0f, 1), 2.0f);
                ImPlot::PlotLine("DA", neuromod_times_.data(), da_history_.data(), (int)neuromod_times_.size());
                // OA: orange
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 0.6f, 0.1f, 1), 2.0f);
                ImPlot::PlotLine("OA", neuromod_times_.data(), oa_history_.data(), (int)neuromod_times_.size());
                // Satiety: white
                ImPlot::SetNextLineStyle(ImVec4(0.8f, 0.8f, 0.8f, 0.7f), 1.5f);
                ImPlot::PlotLine(u8"\u9971\u98df\u5ea6", neuromod_times_.data(), satiety_history_.data(), (int)neuromod_times_.size());
                // Food memory (DARPP-32): yellow
                ImPlot::SetNextLineStyle(ImVec4(1.0f, 1.0f, 0.2f, 0.8f), 1.5f);
                ImPlot::PlotLine(u8"\u98df\u7269\u8bb0\u5fc6", neuromod_times_.data(), fmem_history_.data(), (int)neuromod_times_.size());
                ImPlot::EndPlot();
            }
            double sht_now = sht_history_.back();
            double da_now = da_history_.back();
            double oa_now = oa_history_.back();
            double sat_now = satiety_history_.back();
            ImGui::TextColored(ImVec4(1,0.3f,0.7f,1), u8"  5-HT=%.3f", sht_now);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f,0.8f,1,1), u8"  DA=%.3f", da_now);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1,0.6f,0.1f,1), u8"  OA=%.3f", oa_now);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f,0.8f,0.8f,1), u8"  \u9971\u98df=%.2f", sat_now);
            ImGui::SameLine();
            double fmem_now = fmem_history_.back();
            ImGui::TextColored(ImVec4(1,1,0.2f,1), u8"  \u8bb0\u5fc6=%.2f", fmem_now);
        }
    }

    ImGui::End();
}

void VisApp::render_chemical_field() {
    ImGui::Begin(u8"\u7edf\u8ba1\u6307\u6807", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    float plot_h = ImGui::GetContentRegionAvail().y * 0.5f;

    // Distance to food
    if (!dist_history_.empty()) {
        if (ImPlot::BeginPlot(u8"\u8ddd\u98df\u7269\u8ddd\u79bb\u56fe", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u91c7\u6837 (\u00d7100ms)", u8"\u8ddd\u79bb (mm)");
            ImPlot::SetupAxesLimits(0, (double)dist_history_.size(), 0, 20, ImPlotCond_Always);
            ImPlot::SetNextLineStyle(ImVec4(1, 0.6f, 0.2f, 1), 2);
            ImPlot::PlotLine(u8"\u8ddd\u79bb", dist_history_.data(), (int)dist_history_.size());
            ImPlot::EndPlot();
        }
    }

    // Chemotaxis index
    if (!ci_history_.empty()) {
        if (ImPlot::BeginPlot(u8"\u8d8b\u5316\u6307\u6570 (CI) \u56fe", ImVec2(-1, -1))) {
            ImPlot::SetupAxes(u8"\u91c7\u6837 (\u00d7100ms)", u8"CI");
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

void VisApp::render_tuning_panel() {
    ImGui::Begin(u8"\u8c03\u53c2\u9762\u677f", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), u8"\u589e\u76ca\u94fe\u8c03\u53c2");
    ImGui::Separator();

    auto& p = engine_.params;
    ImGui::SliderFloat(u8"\u68af\u5ea6\u589e\u76ca (weathervane)", &p.weathervane_gain, 1.0f, 500.0f, "%.0f");
    ImGui::SliderFloat(u8"\u7a81\u89e6\u6743\u91cd\u500d\u7387", &p.synapse_scale, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat(u8"\u901f\u5ea6\u500d\u7387", &p.speed_scale, 0.2f, 5.0f, "%.2f");
    ImGui::SliderFloat(u8"\u611f\u89c9\u589e\u76ca\u500d\u7387", &p.sensory_gain, 0.1f, 10.0f, "%.2f");
    ImGui::SliderFloat(u8"\u504f\u7f6e\u7535\u6d41\u9650\u5e45 (pA)", &p.bias_clamp, 1.0f, 50.0f, "%.1f");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), u8"\u4fe1\u53f7\u94fe\u8bca\u65ad");
    ImGui::Separator();

    // Signal chain: gradient → sensory current → neuron ΔV → SMD diff → dθ/dt → CI
    const auto& neurons = engine_.neurons();
    int n = static_cast<int>(neurons.size());

    // 1. Gradient magnitude at head
    auto head = engine_.body().get_head_position();
    auto grad = engine_.environment().chemical_field().gradient(head);
    double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);
    ImGui::Text(u8"\u2460 \u68af\u5ea6\u5e45\u5ea6: %.4f /mm", grad_mag);

    // 2. Weathervane bias
    double heading = engine_.body().get_head_angle();
    double grad_normal = -std::sin(heading) * grad.x + std::cos(heading) * grad.y;
    double bias = p.weathervane_gain * grad_normal;
    double clamp = p.bias_clamp;
    if (bias > clamp) bias = clamp;
    if (bias < -clamp) bias = -clamp;
    ImGui::Text(u8"\u2461 \u5782\u76f4\u68af\u5ea6: %.4f  \u504f\u7f6e: %.2f pA", grad_normal, bias);

    // 3. SMD differential
    int smddl_id = engine_.connectome().get_neuron_id("SMDDL");
    int smdvl_id = engine_.connectome().get_neuron_id("SMDVL");
    if (smddl_id >= 0 && smdvl_id >= 0 && smddl_id < n && smdvl_id < n) {
        double vd = neurons[smddl_id]->get_membrane_potential();
        double vv = neurons[smdvl_id]->get_membrane_potential();
        ImGui::Text(u8"\u2462 SMD\u5dee\u5f02: %.2f mV (D-V)", vd - vv);
    }

    // 4. Head curvature
    double curv = engine_.body().segments()[0].curvature;
    ImGui::Text(u8"\u2463 \u5934\u90e8\u66f2\u7387: %.4f /mm", curv);

    // 5. Speed
    ImGui::Text(u8"\u2464 \u901f\u5ea6: %.4f mm/s", engine_.body().get_speed());

    // 6. Heading rate
    if (heading_values_.size() >= 100) {
        int sz = (int)heading_values_.size();
        double dt_sec = (heading_times_[sz-1] - heading_times_[sz-100]) / 1000.0;
        double dh = heading_values_[sz-1] - heading_values_[sz-100];
        double rate = (dt_sec > 0.01) ? dh / dt_sec : 0;
        ImGui::Text(u8"\u2465 \u8f6c\u5f2f\u7387: %.2f \u00b0/s", rate);
    }

    // 7. CI
    if (ci_count_ > 0) {
        double ci = ci_sum_ / ci_count_;
        ImVec4 ci_col = ci > 0.3 ? ImVec4(0.2f,1,0.2f,1) : (ci > 0 ? ImVec4(1,1,0.2f,1) : ImVec4(1,0.3f,0.3f,1));
        ImGui::TextColored(ci_col, u8"\u2466 CI: %.3f", ci);
    }

    ImGui::Separator();
    if (ImGui::Button(u8"\u91cd\u7f6e\u53c2\u6570")) {
        p = SimulationEngine::TuningParams{};
    }

    ImGui::End();
}

void VisApp::render_control_panel() {
    ImGui::Begin(u8"\u63a7\u5236\u9762\u677f", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Sim info
    double t = engine_.current_time();
    ImGui::Text(u8"\u4eff\u771f\u65f6\u95f4: %.1f ms (%.2f s)", t, t / 1000.0);
    ImGui::Text(u8"\u6b65\u6570: %d", engine_.get_step_count());

    ImGui::Separator();

    // Position info
    auto head = engine_.body().get_head_position();
    Vector2d food{35.0, 35.0};
    double dx = head.x - food.x;
    double dy = head.y - food.y;
    double dist = std::sqrt(dx * dx + dy * dy);
    ImGui::Text(u8"\u5934\u90e8\u4f4d\u7f6e: (%.2f, %.2f)", head.x, head.y);
    ImGui::Text(u8"\u8ddd\u98df\u7269: %.2f mm", dist);
    ImGui::Text(u8"\u901f\u5ea6: %.4f mm/s", engine_.body().get_speed());
    if (ci_count_ > 0)
        ImGui::Text(u8"\u8d8b\u5316\u6307\u6570: %.3f", ci_sum_ / ci_count_);

    // Behavior state indicator (Step 18)
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), u8"\u884c\u4e3a\u72b6\u6001:");
    if (engine_.is_omega_turning()) {
        ImGui::TextColored(ImVec4(1, 0.2f, 1, 1), u8"  >> OMEGA \u8f6c\u5f2f <<");
    } else if (engine_.is_reversing()) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), u8"  >> \u540e\u9000 <<");
    } else {
        ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), u8"  >> \u524d\u8fdb + \u8d8b\u5316 <<");
    }

    ImGui::Separator();

    // Controls
    if (ImGui::Button(sim_paused_ ? u8"\u7ee7\u7eed [Space]" : u8"\u6682\u505c [Space]")) {
        sim_paused_ = !sim_paused_;
    }
    ImGui::SameLine();
    if (ImGui::Button(u8"\u91cd\u7f6e")) {
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

    ImGui::SliderInt(u8"\u6bcf\u5e27\u6b65\u6570", &steps_per_frame_, 1, 200);
    ImGui::Text(u8"\u4eff\u771f\u901f\u5ea6: %.0fx \u5b9e\u65f6", steps_per_frame_ * 0.5 * 60.0 / 1000.0);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"\u5feb\u6377\u952e:");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"  Space = \u6682\u505c/\u7ee7\u7eed");
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"  Esc = \u9000\u51fa");

    ImGui::Separator();

    // Neuron count summary
    ImGui::Text(u8"\u795e\u7ecf\u5143: %d", (int)engine_.neurons().size());
    ImGui::Text(u8"\u7a81\u89e6: %d \u5316\u5b66 + %d \u7535\u7a81\u89e6",
        engine_.connectome().num_synapses(),
        engine_.connectome().num_gap_junctions());

    ImGui::End();
}

} // namespace celegans
