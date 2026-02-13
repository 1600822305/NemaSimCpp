#include "visualization_v2/panels/motor_array_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace celegans {

void MotorArrayPanel::initialize(const SimulationEngine& engine, const DataBus& bus) {
    // Collect all motor neurons from VNC classes
    const char* mn_prefixes[] = {"DA", "VA", "DB", "VB", "DD", "VD", "AS"};
    const bool dorsal[] = {true, false, true, false, true, false, true};

    const auto& infos = engine.connectome().neuron_infos();
    for (size_t pi = 0; pi < 7; ++pi) {
        for (const auto& info : infos) {
            if (info.type != NeuronType::MOTOR) continue;
            if (info.name.compare(0, strlen(mn_prefixes[pi]), mn_prefixes[pi]) == 0) {
                // Parse number suffix
                std::string num_str = info.name.substr(strlen(mn_prefixes[pi]));
                // Skip non-VNC motor neurons that happen to start with these prefixes
                bool all_digits = !num_str.empty();
                for (char c : num_str) if (!isdigit(c)) all_digits = false;
                if (!all_digits) continue;

                int num = std::stoi(num_str);
                MNEntry e;
                e.neuron_id = info.id;
                e.name = info.name;
                e.mn_class = mn_prefixes[pi];
                e.is_dorsal = dorsal[pi];
                // Approximate segment mapping
                int seg_per_mn = 48 / std::max(1, 12);  // rough
                e.seg_start = (num - 1) * seg_per_mn;
                e.seg_end = num * seg_per_mn;
                mn_entries_.push_back(e);
            }
        }
    }

    // Sort by class then by number
    std::sort(mn_entries_.begin(), mn_entries_.end(), [](const MNEntry& a, const MNEntry& b) {
        if (a.mn_class != b.mn_class) return a.mn_class < b.mn_class;
        return a.name < b.name;
    });
}

void MotorArrayPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (mn_entries_.empty()) {
        ImGui::Text(u8"\u65e0\u8fd0\u52a8\u795e\u7ecf\u5143\u6570\u636e");
        ImGui::End();
        return;
    }

    const auto& snapshots = bus.neuron_snapshots();

    // Group by class
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float avail_w = ImGui::GetContentRegionAvail().x;
    float avail_h = ImGui::GetContentRegionAvail().y;

    // Find unique classes preserving order
    std::vector<std::string> classes;
    for (auto& e : mn_entries_) {
        if (classes.empty() || classes.back() != e.mn_class)
            classes.push_back(e.mn_class);
    }

    float label_w = 30;
    float cell_w = std::max(8.0f, (avail_w - label_w) / 13.0f);
    float row_h = std::min(20.0f, avail_h / (float)classes.size());

    float y = 0;
    for (const auto& cls : classes) {
        // Class label
        ImVec4 cls_color = ImVec4(0.6f, 0.6f, 0.6f, 1);
        if (cls == "DA" || cls == "DB" || cls == "DD" || cls == "AS")
            cls_color = ImVec4(1.0f, 0.6f, 0.3f, 1);  // dorsal: orange
        else
            cls_color = ImVec4(0.3f, 0.6f, 1.0f, 1);   // ventral: blue

        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + y));
        ImGui::TextColored(cls_color, "%s", cls.c_str());

        // Draw cells for each MN in this class
        float x = label_w;
        for (auto& e : mn_entries_) {
            if (e.mn_class != cls) continue;
            if (e.neuron_id < 0 || e.neuron_id >= (int)snapshots.size()) continue;

            double v = snapshots[e.neuron_id].voltage;
            double s = snapshots[e.neuron_id].release_rate;

            // Color by release rate (activation intensity)
            float intensity = (float)std::min(1.0, s * 2.0);
            ImU32 color;
            if (e.is_dorsal) {
                color = IM_COL32(
                    (int)(40 + 215 * intensity),
                    (int)(30 + 100 * intensity),
                    30, 255);
            } else {
                color = IM_COL32(
                    30,
                    (int)(30 + 100 * intensity),
                    (int)(40 + 215 * intensity), 255);
            }

            float cx = pos.x + x;
            float cy = pos.y + y;
            dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + cell_w - 1, cy + row_h - 1), color);

            // Number label
            std::string num = e.name.substr(e.mn_class.size());
            ImVec2 text_size = ImGui::CalcTextSize(num.c_str());
            if (text_size.x < cell_w - 2) {
                dl->AddText(ImVec2(cx + 1, cy + 1), IM_COL32(200, 200, 200, 180), num.c_str());
            }

            // Tooltip
            if (ImGui::IsMouseHoveringRect(ImVec2(cx, cy), ImVec2(cx + cell_w, cy + row_h))) {
                ImGui::BeginTooltip();
                ImGui::Text("%s: V=%.1f mV, S=%.3f", e.name.c_str(), v, s);
                ImGui::EndTooltip();
            }

            x += cell_w;
        }

        y += row_h;
    }

    // Legend
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + y + 4));
    ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), u8"\u2588 \u80cc\u4fa7(D)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.3f, 0.6f, 1, 1), u8"\u2588 \u8179\u4fa7(V)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"  \u4eae\u5ea6=\u91ca\u653e\u7387");

    ImGui::End();
}

} // namespace celegans