#include "visualization_v2/panels/neuron_browser_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <implot.h>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace celegans {

static bool str_contains_ci(const std::string& haystack, const char* needle) {
    if (!needle || !needle[0]) return true;
    std::string h = haystack, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::toupper);
    std::transform(n.begin(), n.end(), n.begin(), ::toupper);
    return h.find(n) != std::string::npos;
}

static const char* type_str(NeuronType t) {
    switch (t) {
        case NeuronType::SENSORY: return u8"\u611f\u89c9";
        case NeuronType::INTER: return u8"\u4e2d\u95f4";
        case NeuronType::MOTOR: return u8"\u8fd0\u52a8";
        case NeuronType::PHARYNGEAL: return u8"\u54bd\u90e8";
        default: return "?";
    }
}

static ImVec4 type_color(NeuronType t) {
    switch (t) {
        case NeuronType::SENSORY: return ImVec4(0.3f, 0.9f, 0.9f, 1);
        case NeuronType::INTER: return ImVec4(0.9f, 0.7f, 0.2f, 1);
        case NeuronType::MOTOR: return ImVec4(0.4f, 0.9f, 0.4f, 1);
        case NeuronType::PHARYNGEAL: return ImVec4(0.9f, 0.4f, 0.7f, 1);
        default: return ImVec4(1, 1, 1, 1);
    }
}

static ImVec4 voltage_color(double v) {
    // -80mV = blue, -40mV = green, 0mV = yellow, +20mV = red
    double t = (v + 80.0) / 100.0;  // normalize to [0,1] approx
    t = std::max(0.0, std::min(1.0, t));
    if (t < 0.5) {
        float f = (float)(t * 2.0);
        return ImVec4(0, f, 1.0f - f, 1);
    } else {
        float f = (float)((t - 0.5) * 2.0);
        return ImVec4(f, 1.0f - f * 0.5f, 0, 1);
    }
}

void NeuronBrowserPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // Search + filter bar
    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##search", u8"\u641c\u7d22...", search_buf_, sizeof(search_buf_));
    ImGui::SameLine();
    const char* type_opts[] = {u8"\u5168\u90e8", u8"\u611f\u89c9", u8"\u4e2d\u95f4", u8"\u8fd0\u52a8", u8"\u54bd\u90e8"};
    ImGui::SetNextItemWidth(80);
    ImGui::Combo("##type", &type_filter_, type_opts, 5);
    ImGui::SameLine();
    ImGui::Text(u8"(%d \u8ddf\u8e2a)", (int)bus.traces().size());

    float avail_h = ImGui::GetContentRegionAvail().y;
    float list_h = avail_h * 0.45f;
    float wave_h = avail_h * 0.55f;

    // Neuron list
    if (ImGui::BeginChild("##neuron_list", ImVec2(-1, list_h), true)) {
        const auto& snapshots = bus.neuron_snapshots();
        for (size_t i = 0; i < snapshots.size(); ++i) {
            const auto& ns = snapshots[i];

            // Type filter
            if (type_filter_ == 1 && ns.type != NeuronType::SENSORY) continue;
            if (type_filter_ == 2 && ns.type != NeuronType::INTER) continue;
            if (type_filter_ == 3 && ns.type != NeuronType::MOTOR) continue;
            if (type_filter_ == 4 && ns.type != NeuronType::PHARYNGEAL) continue;

            // Search filter
            if (!str_contains_ci(ns.name, search_buf_)) continue;

            // Color by voltage
            ImVec4 vc = voltage_color(ns.voltage);
            ImGui::PushStyleColor(ImGuiCol_Text, vc);

            bool is_traced = bus.is_traced(ns.id);
            bool selected = is_traced;

            char label[128];
            snprintf(label, sizeof(label), "%s%-8s %s  %6.1f mV  S=%.3f%s",
                is_traced ? u8"\u25c9 " : "  ",
                ns.name.c_str(),
                type_str(ns.type),
                ns.voltage,
                ns.release_rate,
                ns.ablated ? u8" [\u6d88\u878d]" : "");

            if (ImGui::Selectable(label, &selected)) {
                if (selected && !is_traced) {
                    const_cast<DataBus&>(bus).add_trace(ns.id, ns.name);
                } else if (!selected && is_traced) {
                    const_cast<DataBus&>(bus).remove_trace(ns.id);
                }
            }

            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    // Waveform plots for traced neurons
    const auto& traces = bus.traces();
    if (!traces.empty() && traces[0].times.size() > 0) {
        double t_now = traces[0].times.back();
        double t_window = 5000.0;  // 5 seconds

        int n_traces = (int)traces.size();
        float per_plot_h = std::max(60.0f, (wave_h - 10.0f) / std::max(1, std::min(n_traces, 4)));

        // Group traces into max 4 plots
        int plots_to_show = std::min(n_traces, 4);
        int traces_per_plot = (n_traces + plots_to_show - 1) / plots_to_show;

        static const ImVec4 trace_colors[] = {
            {0.2f, 0.6f, 1, 1}, {1, 0.4f, 0.2f, 1}, {0.2f, 1, 0.2f, 1},
            {1, 1, 0.2f, 1}, {1, 0.2f, 1, 1}, {0.2f, 1, 1, 1},
            {0.8f, 0.6f, 0.2f, 1}, {0.6f, 0.2f, 1, 1},
        };

        for (int p = 0; p < plots_to_show; ++p) {
            char plot_id[32];
            snprintf(plot_id, sizeof(plot_id), "##wave%d", p);

            if (ImPlot::BeginPlot(plot_id, ImVec2(-1, per_plot_h))) {
                ImPlot::SetupAxes("", "mV", ImPlotAxisFlags_NoLabel, 0);
                ImPlot::SetupAxesLimits(t_now - t_window, t_now, -80, -10, ImPlotCond_Always);
                ImPlot::SetupLegend(ImPlotLocation_NorthEast);

                int start = p * traces_per_plot;
                int end = std::min(start + traces_per_plot, n_traces);
                for (int i = start; i < end; ++i) {
                    const auto& tr = traces[i];
                    if (tr.times.empty()) continue;
                    auto tv = tr.times.to_vector();
                    auto vv = tr.voltages.to_vector();
                    ImPlot::SetNextLineStyle(trace_colors[i % 8], 1.5f);
                    ImPlot::PlotLine(tr.name.c_str(), tv.data(), vv.data(), (int)tv.size());
                }

                ImPlot::EndPlot();
            }
        }
    }

    ImGui::End();
}

} // namespace celegans