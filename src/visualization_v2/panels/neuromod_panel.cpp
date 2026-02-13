#include "visualization_v2/panels/neuromod_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <implot.h>

namespace celegans {

void NeuromodPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Checkbox(u8"\u663e\u793a\u5185\u90e8\u72b6\u6001", &show_internal_);

    float avail_h = ImGui::GetContentRegionAvail().y;
    float plot_h = show_internal_ ? avail_h * 0.50f : avail_h;
    double t_now = bus.current_time();
    double nm_window = 30000.0;

    // === Neuromodulator concentration plot ===
    const auto& mods = bus.modulator_histories();
    const auto& nm_times = bus.neuromod_times();

    if (!nm_times.empty()) {
        if (ImPlot::BeginPlot(u8"\u795e\u7ecf\u8c03\u8d28\u6d53\u5ea6 (7\u79cd)", ImVec2(-1, plot_h))) {
            ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"\u6d53\u5ea6 [0,1]");
            ImPlot::SetupAxesLimits(t_now - nm_window, t_now, 0, 1.1, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);

            static const ImVec4 mod_colors[] = {
                {1.0f, 0.3f, 0.7f, 1},  // 5-HT: magenta
                {0.3f, 0.8f, 1.0f, 1},  // DA: cyan
                {1.0f, 0.6f, 0.1f, 1},  // OA: orange
                {0.8f, 0.2f, 0.2f, 1},  // TA: red
                {0.2f, 1.0f, 0.5f, 1},  // NLP-12: green
                {0.6f, 0.3f, 1.0f, 1},  // PDF: purple
                {0.4f, 0.8f, 0.8f, 1},  // FLP-11: teal
            };

            auto tv = nm_times.to_vector();
            for (size_t i = 0; i < mods.size() && i < 7; ++i) {
                auto cv = mods[i].concentration.to_vector();
                if (cv.size() != tv.size()) continue;
                ImPlot::SetNextLineStyle(mod_colors[i], 2.0f);
                ImPlot::PlotLine(mods[i].name.c_str(), tv.data(), cv.data(), (int)tv.size());
            }

            ImPlot::EndPlot();
        }

        // Current values summary
        for (size_t i = 0; i < mods.size() && i < 7; ++i) {
            if (!mods[i].concentration.empty()) {
                static const ImVec4 mod_colors[] = {
                    {1.0f, 0.3f, 0.7f, 1}, {0.3f, 0.8f, 1.0f, 1},
                    {1.0f, 0.6f, 0.1f, 1}, {0.8f, 0.2f, 0.2f, 1},
                    {0.2f, 1.0f, 0.5f, 1}, {0.6f, 0.3f, 1.0f, 1},
                    {0.4f, 0.8f, 0.8f, 1},
                };
                ImGui::SameLine(i > 0 ? 0.0f : 0.0f);
                if (i > 0) ImGui::SameLine();
                ImGui::TextColored(mod_colors[i], "%s=%.3f",
                    mods[i].name.c_str(), mods[i].concentration.back());
            }
        }
    }

    // === Internal states plot ===
    if (show_internal_ && !bus.internal_states().times.empty()) {
        float internal_h = avail_h - plot_h - 40;

        if (ImPlot::BeginPlot(u8"\u5185\u90e8\u72b6\u6001", ImVec2(-1, internal_h))) {
            ImPlot::SetupAxes(u8"\u65f6\u95f4(ms)", u8"\u503c [0,1]");
            ImPlot::SetupAxesLimits(t_now - nm_window, t_now, 0, 1.1, ImPlotCond_Always);
            ImPlot::SetupLegend(ImPlotLocation_NorthEast);

            const auto& is = bus.internal_states();
            auto tv = is.times.to_vector();

            auto plot_state = [&](const char* name, const RingBuffer<double>& buf, ImVec4 color) {
                auto sv = buf.to_vector();
                if (sv.size() == tv.size()) {
                    ImPlot::SetNextLineStyle(color, 1.5f);
                    ImPlot::PlotLine(name, tv.data(), sv.data(), (int)tv.size());
                }
            };

            plot_state(u8"\u9971\u98df\u5ea6", is.satiety, ImVec4(0.8f, 0.8f, 0.8f, 0.8f));
            plot_state(u8"\u98df\u7269\u8bb0\u5fc6", is.food_memory, ImVec4(1, 1, 0.2f, 0.8f));
            plot_state(u8"\u75b2\u52b3", is.fatigue, ImVec4(0.5f, 0.5f, 1, 0.8f));
            plot_state(u8"\u75c5\u611f", is.sickness, ImVec4(1, 0.2f, 0.2f, 0.8f));
            plot_state(u8"INS-1", is.ins1, ImVec4(0.2f, 0.8f, 0.2f, 0.8f));
            plot_state(u8"Dauer", is.dauer_signal, ImVec4(0.6f, 0.4f, 0.2f, 0.8f));
            plot_state(u8"\u654f\u5316", is.sensitization, ImVec4(1, 0.5f, 0, 0.8f));
            plot_state(u8"\u5375\u538b", is.egg_pressure, ImVec4(0.9f, 0.6f, 0.9f, 0.8f));
            plot_state(u8"\u877d\u76ae\u6fc0\u7d20", is.molt_hormone, ImVec4(0.4f, 0.7f, 0.4f, 0.8f));
            plot_state(u8"AWC\u589e\u76ca", is.awc_adapt_gain, ImVec4(0, 0.9f, 0.9f, 0.8f));

            ImPlot::EndPlot();
        }
    }

    ImGui::End();
}

} // namespace celegans