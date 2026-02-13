#include "visualization_v2/panels/neuron_heatmap_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace celegans {

// Voltage to RGB color (same palette as neuron browser)
static ImU32 voltage_to_color(double v) {
    double t = (v + 80.0) / 100.0;
    t = std::max(0.0, std::min(1.0, t));
    float r, g, b;
    if (t < 0.33) {
        float f = (float)(t / 0.33);
        r = 0; g = 0.1f + 0.4f * f; b = 0.6f + 0.4f * (1 - f);
    } else if (t < 0.66) {
        float f = (float)((t - 0.33) / 0.33);
        r = f * 0.8f; g = 0.5f + 0.3f * f; b = 0.2f * (1 - f);
    } else {
        float f = (float)((t - 0.66) / 0.34);
        r = 0.8f + 0.2f * f; g = 0.8f - 0.5f * f; b = 0;
    }
    return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
}

void NeuronHeatmapPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    const auto& snapshots = bus.neuron_snapshots();
    int n = (int)snapshots.size();
    if (n == 0) { ImGui::End(); return; }

    // Group by type
    struct TypeGroup {
        const char* label;
        NeuronType type;
        ImVec4 header_color;
    };
    TypeGroup groups[] = {
        {u8"\u611f\u89c9", NeuronType::SENSORY, ImVec4(0.3f, 0.9f, 0.9f, 1)},
        {u8"\u4e2d\u95f4", NeuronType::INTER, ImVec4(0.9f, 0.7f, 0.2f, 1)},
        {u8"\u8fd0\u52a8", NeuronType::MOTOR, ImVec4(0.4f, 0.9f, 0.4f, 1)},
        {u8"\u54bd\u90e8", NeuronType::PHARYNGEAL, ImVec4(0.9f, 0.4f, 0.7f, 1)},
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;

    float cell_size = 12.0f;
    int cols_per_row = std::max(1, (int)(avail_w / (cell_size + 1)));

    float y_offset = 0;

    for (auto& grp : groups) {
        // Collect neurons of this type
        std::vector<int> indices;
        for (int i = 0; i < n; ++i) {
            if (snapshots[i].type == grp.type) indices.push_back(i);
        }
        if (indices.empty()) continue;

        // Group header
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x, win_pos.y + y_offset));
        ImGui::TextColored(grp.header_color, "%s (%d)", grp.label, (int)indices.size());
        y_offset += 18.0f;

        // Draw cells
        int row = 0, col = 0;
        for (int idx : indices) {
            float cx = win_pos.x + col * (cell_size + 1);
            float cy = win_pos.y + y_offset + row * (cell_size + 1);

            ImU32 color = voltage_to_color(snapshots[idx].voltage);
            if (snapshots[idx].ablated) {
                color = IM_COL32(60, 60, 60, 255);
            }
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cell_size, cy + cell_size), color);

            // Tooltip on hover
            if (ImGui::IsMouseHoveringRect(ImVec2(cx, cy), ImVec2(cx + cell_size, cy + cell_size))) {
                ImGui::BeginTooltip();
                ImGui::Text("%s: %.1f mV, S=%.3f%s",
                    snapshots[idx].name.c_str(),
                    snapshots[idx].voltage,
                    snapshots[idx].release_rate,
                    snapshots[idx].ablated ? u8" [\u6d88\u878d]" : "");
                ImGui::EndTooltip();

                // Click to toggle trace
                if (ImGui::IsMouseClicked(0)) {
                    if (bus.is_traced(snapshots[idx].id)) {
                        const_cast<DataBus&>(bus).remove_trace(snapshots[idx].id);
                    } else {
                        const_cast<DataBus&>(bus).add_trace(snapshots[idx].id, snapshots[idx].name);
                    }
                }
            }

            col++;
            if (col >= cols_per_row) { col = 0; row++; }
        }

        int total_rows = (col > 0) ? row + 1 : row;
        y_offset += total_rows * (cell_size + 1) + 4;
    }

    // Color legend
    float legend_y = win_pos.y + y_offset + 4;
    if (legend_y + 16 < win_pos.y + avail_h) {
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x, legend_y));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"-80mV");
        // Draw gradient bar
        float bar_x = win_pos.x + 50;
        float bar_w = std::min(150.0f, avail_w - 120);
        for (int i = 0; i < (int)bar_w; ++i) {
            double v = -80.0 + 100.0 * i / bar_w;
            ImU32 c = voltage_to_color(v);
            dl->AddRectFilled(
                ImVec2(bar_x + i, legend_y),
                ImVec2(bar_x + i + 1, legend_y + 12), c);
        }
        ImGui::SetCursorScreenPos(ImVec2(bar_x + bar_w + 4, legend_y));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"+20mV");
    }

    ImGui::End();
}

} // namespace celegans