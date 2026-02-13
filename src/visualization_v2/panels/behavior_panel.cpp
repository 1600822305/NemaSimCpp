#include "visualization_v2/panels/behavior_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <implot.h>
#include <algorithm>

namespace celegans {

void BehaviorPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    const auto& beh = bus.behavior();

    // === Current behavior state indicators ===
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.2f, 1), u8"\u5f53\u524d\u884c\u4e3a:");

    auto state_led = [](const char* label, bool active, ImVec4 on_color) {
        ImVec4 color = active ? on_color : ImVec4(0.3f, 0.3f, 0.3f, 1);
        ImGui::TextColored(color, "%s %s", active ? u8"\u25cf" : u8"\u25cb", label);
    };

    state_led(u8"\u524d\u8fdb", !beh.is_reversing && !beh.is_omega && !beh.is_sleeping,
              ImVec4(0.3f, 1, 0.3f, 1));
    ImGui::SameLine();
    state_led(u8"\u540e\u9000", beh.is_reversing, ImVec4(1, 0.3f, 0.3f, 1));
    ImGui::SameLine();
    state_led(u8"Omega", beh.is_omega, ImVec4(1, 0.2f, 1, 1));

    state_led(u8"\u7761\u7720", beh.is_sleeping, ImVec4(0.4f, 0.4f, 1, 1));
    ImGui::SameLine();
    state_led(u8"Dauer", beh.is_dauer, ImVec4(0.6f, 0.4f, 0.2f, 1));
    ImGui::SameLine();
    state_led(u8"Nictation", beh.nictation_waving, ImVec4(0.8f, 0.6f, 0.2f, 1));

    state_led(u8"\u877d\u76ae\u9759\u606f", beh.in_lethargus, ImVec4(0.5f, 0.7f, 0.5f, 1));
    ImGui::SameLine();
    state_led(u8"DMP", beh.dmp_active, ImVec4(0.7f, 0.5f, 0.3f, 1));
    ImGui::SameLine();
    state_led(u8"Tap", beh.tap_active, ImVec4(0.9f, 0.9f, 0.3f, 1));
    ImGui::SameLine();
    state_led(u8"Exo-5HT", beh.exo_5ht, ImVec4(1, 0.3f, 0.7f, 1));

    ImGui::Separator();

    // === Behavior raster (time axis, one row per behavior type) ===
    const auto& bh = bus.behavior_history();
    float avail_h = ImGui::GetContentRegionAvail().y;

    if (!bh.times.empty() && avail_h > 60) {
        double t_now = bus.current_time();
        double window = 60000.0;  // 60s window

        struct RasterRow {
            const char* name;
            const RingBuffer<uint8_t>* data;
            ImVec4 color;
        };
        RasterRow rows[] = {
            {u8"\u540e\u9000", &bh.reversing, ImVec4(1, 0.3f, 0.3f, 1)},
            {u8"Omega", &bh.omega, ImVec4(1, 0.2f, 1, 1)},
            {u8"\u7761\u7720", &bh.sleeping, ImVec4(0.4f, 0.4f, 1, 1)},
            {u8"Dauer", &bh.dauer, ImVec4(0.6f, 0.4f, 0.2f, 1)},
            {u8"Nictation", &bh.nictation, ImVec4(0.8f, 0.6f, 0.2f, 1)},
            {u8"\u877d\u76ae", &bh.lethargus, ImVec4(0.5f, 0.7f, 0.5f, 1)},
            {u8"DMP", &bh.dmp, ImVec4(0.7f, 0.5f, 0.3f, 1)},
        };
        int n_rows = 7;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float raster_w = ImGui::GetContentRegionAvail().x;
        float row_h = std::min(20.0f, (avail_h - 20) / n_rows);

        // Labels
        float label_w = 60;

        for (int r = 0; r < n_rows; ++r) {
            float ry = pos.y + r * row_h;
            ImGui::SetCursorScreenPos(ImVec2(pos.x, ry));
            ImGui::TextColored(rows[r].color, "%s", rows[r].name);

            // Draw raster blocks
            size_t n = rows[r].data->size();
            size_t tn = bh.times.size();
            if (n == 0 || tn == 0) continue;

            float bar_x = pos.x + label_w;
            float bar_w = raster_w - label_w;

            for (size_t i = 0; i < n && i < tn; ++i) {
                if ((*rows[r].data)[i] == 0) continue;
                double t = bh.times[i];
                if (t < t_now - window) continue;
                float frac = (float)((t - (t_now - window)) / window);
                float px = bar_x + frac * bar_w;
                ImU32 col = ImGui::GetColorU32(rows[r].color);
                dl->AddRectFilled(ImVec2(px, ry + 2), ImVec2(px + 2, ry + row_h - 2), col);
            }
        }

        // Time axis
        float axis_y = pos.y + n_rows * row_h;
        ImGui::SetCursorScreenPos(ImVec2(pos.x + label_w, axis_y));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "%.0fs", (t_now - window) / 1000.0);
        ImGui::SameLine();
        float end_x = pos.x + raster_w - 40;
        ImGui::SetCursorScreenPos(ImVec2(end_x, axis_y));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "%.0fs", t_now / 1000.0);
    }

    // === Cumulative stats ===
    ImGui::Separator();
    const auto& stats = bus.stats();
    ImGui::Text(u8"\u7d2f\u8ba1: \u540e\u9000=%d  Omega=%d  DMP=%d  \u4ea7\u5375=%d",
        stats.total_reversals, stats.total_omegas, stats.dmp_count, stats.eggs_laid);

    ImGui::End();
}

} // namespace celegans