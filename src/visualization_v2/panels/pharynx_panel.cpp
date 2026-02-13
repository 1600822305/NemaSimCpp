#include "visualization_v2/panels/pharynx_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>

namespace celegans {

void PharynxPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    const auto& pharynx = engine.pharynx();
    const auto& stats = bus.stats();
    const auto& beh = bus.behavior();

    // === Pump status ===
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.7f, 1), u8"\u54bd\u90e8\u6cf5:");
    ImGui::Text(u8"\u6cf5\u7387: %.1f Hz", stats.pump_rate_hz);
    ImGui::Text(u8"\u603b\u6cf5\u6b21: %d", stats.total_pumps);
    ImGui::Text(u8"\u808c\u8089\u7535\u4f4d: %.1f mV", pharynx.V_muscle());

    // Pump state indicator
    const char* phase_names[] = {
        u8"\u9759\u606f", u8"\u6536\u7f29", u8"\u4fdd\u6301", u8"\u8212\u5f20"  // 静息, 收缩, 保持, 舒张
    };
    int phase = static_cast<int>(pharynx.phase());
    if (phase >= 0 && phase < 4) {
        ImVec4 phase_colors[] = {
            {0.5f, 0.5f, 0.5f, 1},  // rest
            {1, 0.4f, 0.2f, 1},     // contraction
            {1, 0.7f, 0.3f, 1},     // hold
            {0.3f, 0.7f, 1, 1},     // relaxation
        };
        ImGui::TextColored(phase_colors[phase], u8"\u76f8\u4f4d: %s", phase_names[phase]);
    }

    ImGui::Separator();

    // === Satiety & feeding ===
    ImGui::Text(u8"\u9971\u98df\u5ea6: %.3f", engine.satiety());
    float sat = (float)engine.satiety();
    ImGui::ProgressBar(sat, ImVec2(-1, 14), "");

    ImGui::Separator();

    // === DMP (defecation motor program) ===
    ImGui::TextColored(ImVec4(0.7f, 0.5f, 0.3f, 1), u8"\u6392\u4fbf\u7a0b\u5e8f (DMP):");
    ImGui::Text(u8"\u603b\u6b21\u6570: %d", stats.dmp_count);
    if (beh.dmp_active) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), u8"  >> DMP \u6d3b\u8dc3 <<");
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1), u8"  \u7b49\u5f85\u4e0b\u4e00\u5468\u671f");
    }

    ImGui::Separator();

    // === Egg-laying ===
    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.9f, 1), u8"\u4ea7\u5375:");
    ImGui::Text(u8"\u5375\u538b: %.3f", engine.egg_pressure());
    ImGui::Text(u8"\u5df2\u4ea7\u5375: %d", stats.eggs_laid);
    if (beh.exo_5ht) {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.7f, 1), u8"  \u5916\u6e905-HT \u6fc0\u6d3b");
    }

    ImGui::Separator();

    // === Pharyngeal neurons activity ===
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), u8"\u54bd\u90e8\u795e\u7ecf\u5143 (20):");
    const auto& snapshots = bus.neuron_snapshots();
    int count = 0;
    for (const auto& ns : snapshots) {
        if (ns.type == NeuronType::PHARYNGEAL) {
            ImVec4 color;
            float s = (float)ns.release_rate;
            if (s > 0.3f) color = ImVec4(1, 0.5f + 0.5f * s, 0.3f, 1);
            else color = ImVec4(0.4f, 0.4f, 0.5f, 1);

            if (count > 0 && count % 5 != 0) ImGui::SameLine();
            ImGui::TextColored(color, "%-5s", ns.name.c_str());
            count++;
        }
    }

    ImGui::End();
}

} // namespace celegans