#include "visualization_v2/panels/stats_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <implot.h>
#include <cmath>

namespace celegans {

void StatsPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    const auto& stats = bus.stats();
    float avail_h = ImGui::GetContentRegionAvail().y;
    float plot_h = avail_h * 0.28f;

    // === Distance to food ===
    if (!stats.distance_to_food.empty()) {
        if (ImPlot::BeginPlot(u8"\u8ddd\u98df\u7269\u8ddd\u79bb", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u91c7\u6837(\u00d7100ms)", u8"\u8ddd\u79bb(mm)");
            auto dv = stats.distance_to_food.to_vector();
            ImPlot::SetupAxesLimits(0, (double)dv.size(), 0, 25, ImPlotCond_Always);
            ImPlot::SetNextLineStyle(ImVec4(1, 0.6f, 0.2f, 1), 2);
            ImPlot::PlotLine(u8"\u8ddd\u79bb", dv.data(), (int)dv.size());
            ImPlot::EndPlot();
        }
    }

    // === CI ===
    if (!stats.ci_running.empty()) {
        if (ImPlot::BeginPlot(u8"\u8d8b\u5316\u6307\u6570 (CI)", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u91c7\u6837(\u00d7100ms)", "CI");
            auto cv = stats.ci_running.to_vector();
            ImPlot::SetupAxesLimits(0, (double)cv.size(), -1, 1, ImPlotCond_Always);
            // Zero line
            double zx[2] = {0, (double)cv.size()};
            double zy[2] = {0, 0};
            ImPlot::SetNextLineStyle(ImVec4(0.5f, 0.5f, 0.5f, 0.5f), 1);
            ImPlot::PlotLine("##zero", zx, zy, 2);
            // CI
            ImPlot::SetNextLineStyle(ImVec4(0.3f, 0.8f, 1.0f, 1), 2);
            ImPlot::PlotLine("CI", cv.data(), (int)cv.size());
            ImPlot::EndPlot();
        }
    }

    // === Speed ===
    if (!stats.speed.empty()) {
        if (ImPlot::BeginPlot(u8"\u901f\u5ea6", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u91c7\u6837(\u00d7100ms)", u8"mm/s");
            auto sv = stats.speed.to_vector();
            ImPlot::SetupAxesLimits(0, (double)sv.size(), 0, 0.5, ImPlotCond_Always);
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 1, 0.4f, 1), 2);
            ImPlot::PlotLine(u8"\u901f\u5ea6", sv.data(), (int)sv.size());
            ImPlot::EndPlot();
        }
    }

    // === Summary numbers ===
    ImGui::Separator();
    double t = bus.current_time();
    auto head = engine.body().get_head_position();
    auto fp = bus.food_position();
    double dist = std::sqrt(std::pow(head.x - fp.x, 2) + std::pow(head.y - fp.y, 2));

    ImGui::Text(u8"\u4eff\u771f\u65f6\u95f4: %.1f s", t / 1000.0);
    ImGui::Text(u8"\u5934\u90e8\u4f4d\u7f6e: (%.1f, %.1f)", head.x, head.y);
    ImGui::Text(u8"\u8ddd\u98df\u7269: %.1f mm", dist);
    ImGui::Text(u8"\u901f\u5ea6: %.4f mm/s", engine.body().get_speed());

    if (stats.ci_count > 0) {
        double ci = stats.ci_sum / stats.ci_count;
        ImVec4 ci_col = ci > 0.3 ? ImVec4(0.2f, 1, 0.2f, 1) :
                         ci > 0 ? ImVec4(1, 1, 0.2f, 1) : ImVec4(1, 0.3f, 0.3f, 1);
        ImGui::TextColored(ci_col, u8"CI: %.3f", ci);
    }

    ImGui::Text(u8"\u6cf5\u7387: %.1f Hz  \u603b\u6cf5: %d", stats.pump_rate_hz, stats.total_pumps);

    ImGui::End();
}

} // namespace celegans