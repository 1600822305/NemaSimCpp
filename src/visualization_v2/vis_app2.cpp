#include "visualization_v2/vis_app2.h"
#include "visualization_v2/panels/arena_panel.h"
#include "visualization_v2/panels/neuron_browser_panel.h"
#include "visualization_v2/panels/neuron_heatmap_panel.h"
#include "visualization_v2/panels/neuromod_panel.h"
#include "visualization_v2/panels/behavior_panel.h"
#include "visualization_v2/panels/motor_array_panel.h"
#include "visualization_v2/panels/worm_body_panel.h"
#include "visualization_v2/panels/control_panel.h"
#include "visualization_v2/panels/stats_panel.h"
#include "visualization_v2/panels/pharynx_panel.h"
#include "core/logger.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <GLFW/glfw3.h>

#include <cstdio>

namespace celegans {

VisApp2::VisApp2() {}

VisApp2::~VisApp2() {
    shutdown();
}

bool VisApp2::initialize(int width, int height) {
    window_width_ = width;
    window_height_ = height;

    // GLFW init
    glfwSetErrorCallback([](int error, const char* desc) {
        (void)error; (void)desc;
    });
    if (!glfwInit()) {
        LOG_ERROR("glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    window_ = glfwCreateWindow(width, height,
        "C. elegans 302 Neuron Simulation — v2", nullptr, nullptr);
    if (!window_) {
        LOG_ERROR("glfwCreateWindow failed");
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.FontGlobalScale = 1.1f;

    // Modern dark theme
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowPadding = ImVec2(8, 8);
    style.ItemSpacing = ImVec2(8, 4);
    // Custom accent colors
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.14f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.14f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.25f, 0.40f, 1.0f);

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Load Chinese font
    const char* font_paths[] = {
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/simsun.ttc",
    };
    for (auto* path : font_paths) {
        FILE* f = fopen(path, "rb");
        if (f) {
            fclose(f);
            io.Fonts->AddFontFromFileTTF(path, 16.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseFull());
            break;
        }
    }

    // Initialize simulation engine
    engine_.initialize_default();

    // Initialize DataBus
    bus_.initialize(engine_);

    // Register all panels
    register_panels();

    // Initialize panels
    for (auto& p : panels_) {
        p->initialize(engine_, bus_);
    }

    // Compute initial layout
    glfwGetFramebufferSize(window_, &window_width_, &window_height_);
    compute_layout();

    LOG_INFO("VisApp2 initialized: ", window_width_, "x", window_height_,
             ", ", engine_.neurons().size(), " neurons, ",
             panels_.size(), " panels");
    return true;
}

void VisApp2::register_panels() {
    // Column 1: Arena + WormBody (left)
    panels_.push_back(std::make_unique<ArenaPanel>());
    panels_.push_back(std::make_unique<WormBodyPanel>());

    // Column 2: Neuron views (center-left)
    panels_.push_back(std::make_unique<NeuronBrowserPanel>());
    panels_.push_back(std::make_unique<NeuronHeatmapPanel>());
    panels_.push_back(std::make_unique<MotorArrayPanel>());

    // Column 3: Neuromod + Behavior (center-right)
    panels_.push_back(std::make_unique<NeuromodPanel>());
    panels_.push_back(std::make_unique<BehaviorPanel>());
    panels_.push_back(std::make_unique<PharynxPanel>());

    // Column 4: Stats + Control (right)
    panels_.push_back(std::make_unique<StatsPanel>());
    panels_.push_back(std::make_unique<ControlPanel>());
}

void VisApp2::compute_layout() {
    float w = (float)window_width_;
    float h = (float)window_height_;
    float menu_h = 22.0f;  // menu bar height

    // 4-column layout with docking
    float col_widths[] = {w * 0.22f, w * 0.28f, w * 0.25f, w * 0.25f};
    float x = 0;

    // Col 1: Arena (60%) + WormBody (40%)
    if (panels_.size() > 0) { panels_[0]->layout = {x, menu_h, col_widths[0], (h - menu_h) * 0.60f}; }
    if (panels_.size() > 1) { panels_[1]->layout = {x, menu_h + (h - menu_h) * 0.60f, col_widths[0], (h - menu_h) * 0.40f}; }
    x += col_widths[0];

    // Col 2: NeuronBrowser (40%) + NeuronHeatmap (35%) + MotorArray (25%)
    if (panels_.size() > 2) { panels_[2]->layout = {x, menu_h, col_widths[1], (h - menu_h) * 0.40f}; }
    if (panels_.size() > 3) { panels_[3]->layout = {x, menu_h + (h - menu_h) * 0.40f, col_widths[1], (h - menu_h) * 0.35f}; }
    if (panels_.size() > 4) { panels_[4]->layout = {x, menu_h + (h - menu_h) * 0.75f, col_widths[1], (h - menu_h) * 0.25f}; }
    x += col_widths[1];

    // Col 3: Neuromod (40%) + Behavior (35%) + Pharynx (25%)
    if (panels_.size() > 5) { panels_[5]->layout = {x, menu_h, col_widths[2], (h - menu_h) * 0.40f}; }
    if (panels_.size() > 6) { panels_[6]->layout = {x, menu_h + (h - menu_h) * 0.40f, col_widths[2], (h - menu_h) * 0.35f}; }
    if (panels_.size() > 7) { panels_[7]->layout = {x, menu_h + (h - menu_h) * 0.75f, col_widths[2], (h - menu_h) * 0.25f}; }
    x += col_widths[2];

    // Col 4: Stats (55%) + Control (45%)
    if (panels_.size() > 8) { panels_[8]->layout = {x, menu_h, col_widths[3], (h - menu_h) * 0.55f}; }
    if (panels_.size() > 9) { panels_[9]->layout = {x, menu_h + (h - menu_h) * 0.55f, col_widths[3], (h - menu_h) * 0.45f}; }
}

void VisApp2::shutdown() {
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

void VisApp2::run() {
    while (running_ && !glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Keyboard shortcuts
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard) {
            if (glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS) {
                static bool space_was = false;
                if (!space_was) { sim_paused_ = !sim_paused_; space_was = true; }
            } else {
                static bool space_was = false;
                space_was = false;
            }
            if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS)
                running_ = false;
        }

        // Simulation
        if (!sim_paused_) {
            sim_step_batch(steps_per_frame_);
        }

        // Render
        render_frame();
    }
}

void VisApp2::sim_step_batch(int steps) {
    for (int i = 0; i < steps; ++i) {
        engine_.step();
        bus_.update(engine_, 1);
    }
}

void VisApp2::render_frame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Update window size
    glfwGetFramebufferSize(window_, &window_width_, &window_height_);
    compute_layout();

    // Menu bar
    render_menu_bar();

    // Render all visible panels
    for (auto& panel : panels_) {
        if (!panel->visible()) continue;

        ImGui::SetNextWindowPos(ImVec2(panel->layout.x, panel->layout.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(panel->layout.w, panel->layout.h), ImGuiCond_Always);
        panel->render(bus_, engine_);
    }

    // Rendering
    ImGui::Render();
    glViewport(0, 0, window_width_, window_height_);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void VisApp2::render_menu_bar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(u8"\u89c6\u56fe")) {  // 视图
            for (auto& panel : panels_) {
                bool vis = panel->visible();
                if (ImGui::MenuItem(panel->title().c_str(), nullptr, &vis)) {
                    panel->set_visible(vis);
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu(u8"\u4eff\u771f")) {  // 仿真
            if (ImGui::MenuItem(sim_paused_ ? u8"\u7ee7\u7eed [Space]" : u8"\u6682\u505c [Space]")) {
                sim_paused_ = !sim_paused_;
            }
            ImGui::SliderInt(u8"\u6bcf\u5e27\u6b65\u6570", &steps_per_frame_, 1, 200);
            ImGui::EndMenu();
        }

        // Status bar
        ImGui::Separator();
        double t = bus_.current_time();
        ImGui::Text(u8"  t=%.1fs  N=%d  Syn=%d  GJ=%d  %s",
            t / 1000.0,
            (int)engine_.neurons().size(),
            (int)engine_.connectome().num_synapses(),
            (int)engine_.connectome().num_gap_junctions(),
            sim_paused_ ? u8"\u23f8 \u6682\u505c" : u8"\u25b6 \u8fd0\u884c");

        ImGui::EndMainMenuBar();
    }
}

} // namespace celegans