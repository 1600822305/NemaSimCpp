#include "visualization_v2/panels/arena_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <implot.h>
#include <cmath>
#include <vector>

namespace celegans {

void ArenaPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Field type selector
    const char* field_names[] = {
        u8"\u5316\u5b66\u5f15\u8bf1\u7269", u8"\u6eb6\u8d28(\u76d0)",
        u8"\u6392\u65a5\u7269", u8"\u6e29\u5ea6",
        u8"\u4fe1\u606f\u7d20", u8"\u5149\u7167",
        u8"\u6e17\u900f\u538b"
    };
    // 化学引诱物, 溶质(盐), 排斥物, 温度, 信息素, 光照, 渗透压
    if (ImGui::Combo(u8"\u73af\u5883\u573a", &field_type_, field_names, 7)) {
        const_cast<DataBus&>(bus).set_active_field(static_cast<DataBus::FieldType>(field_type_));
    }

    float avail_h = ImGui::GetContentRegionAvail().y;

    if (ImPlot::BeginPlot("##arena", ImVec2(-1, avail_h), ImPlotFlags_Equal)) {
        ImPlot::SetupAxes("X (mm)", "Y (mm)");
        ImPlot::SetupAxesLimits(0, 50, 0, 50, ImPlotCond_Once);

        // Field heatmap overlay (scatter approximation)
        const auto& fd = bus.field_data();
        int nx = bus.field_nx(), ny = bus.field_ny();
        if (!fd.empty() && nx > 0 && ny > 0) {
            double cell_w = 50.0 / nx;
            double cell_h = 50.0 / ny;
            // Find max for normalization
            double fmax = 1e-10;
            for (auto v : fd) if (v > fmax) fmax = v;

            for (int iy = 0; iy < ny; iy += 2) {  // skip every other for performance
                for (int ix = 0; ix < nx; ix += 2) {
                    double val = fd[iy * nx + ix] / fmax;
                    if (val < 0.05) continue;
                    double cx = (ix + 0.5) * cell_w;
                    double cy = (iy + 0.5) * cell_h;
                    float alpha = (float)(val * 0.3);
                    ImPlot::SetNextMarkerStyle(ImPlotMarker_Square, 8,
                        ImVec4(1.0f, (float)(0.3 + 0.5 * val), 0.2f, alpha), 0);
                    ImPlot::PlotScatter("##field", &cx, &cy, 1);
                }
            }
        }

        // Trajectory
        const auto& traj = bus.trajectory();
        if (traj.size() > 1) {
            std::vector<double> xs, ys;
            xs.reserve(traj.size());
            ys.reserve(traj.size());
            for (auto& p : traj) {
                xs.push_back(p.x);
                ys.push_back(p.y);
            }

            // Color-code: older = dim, newer = bright
            ImPlot::SetNextLineStyle(ImVec4(0.2f, 0.8f, 0.4f, 0.7f), 1.5f);
            ImPlot::PlotLine(u8"\u8f68\u8ff9", xs.data(), ys.data(), (int)xs.size());

            // Current head
            double hx = xs.back(), hy = ys.back();
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 7, ImVec4(0, 1, 0, 1), 2);
            ImPlot::PlotScatter(u8"\u5934\u90e8", &hx, &hy, 1);
        }

        // Start position
        double sx = 25.0, sy = 25.0;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Diamond, 6, ImVec4(0.5f, 0.5f, 1, 1), 2);
        ImPlot::PlotScatter(u8"\u8d77\u70b9", &sx, &sy, 1);

        // Food source
        auto fp = bus.food_position();
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Cross, 10, ImVec4(1, 0.3f, 0.3f, 1), 2);
        ImPlot::PlotScatter(u8"\u98df\u7269", &fp.x, &fp.y, 1);

        // Contour circles
        auto draw_circle = [](double cx, double cy, double r) {
            std::vector<double> xs(65), ys(65);
            for (int i = 0; i <= 64; ++i) {
                double a = 2.0 * 3.14159265 * i / 64;
                xs[i] = cx + r * std::cos(a);
                ys[i] = cy + r * std::sin(a);
            }
            ImPlot::PlotLine("##c", xs.data(), ys.data(), 65);
        };
        ImPlot::SetNextLineStyle(ImVec4(1, 0.3f, 0.3f, 0.15f), 1);
        draw_circle(fp.x, fp.y, 5.0);
        ImPlot::SetNextLineStyle(ImVec4(1, 0.3f, 0.3f, 0.08f), 1);
        draw_circle(fp.x, fp.y, 10.0);

        // Behavior state indicator on plot
        const auto& beh = bus.behavior();
        if (beh.is_omega) {
            ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 0.2f, 1, 1));
            ImPlot::PlotText(u8"OMEGA", 25, 48);
            ImPlot::PopStyleColor();
        } else if (beh.is_reversing) {
            ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(1, 0.3f, 0.3f, 1));
            ImPlot::PlotText(u8"\u540e\u9000", 25, 48);
            ImPlot::PopStyleColor();
        } else if (beh.is_sleeping) {
            ImPlot::PushStyleColor(ImPlotCol_InlayText, ImVec4(0.5f, 0.5f, 1, 1));
            ImPlot::PlotText(u8"\u7761\u7720", 25, 48);
            ImPlot::PopStyleColor();
        }

        ImPlot::EndPlot();
    }

    ImGui::End();
}

} // namespace celegans