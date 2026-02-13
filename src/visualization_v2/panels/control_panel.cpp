#include "visualization_v2/panels/control_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>
#include <cstring>

namespace celegans {

void ControlPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    // === Simulation info ===
    double t = bus.current_time();
    ImGui::Text(u8"\u4eff\u771f\u65f6\u95f4: %.1f ms (%.2f s)", t, t / 1000.0);
    ImGui::Text(u8"\u6b65\u6570: %d", bus.step_count());
    ImGui::Text(u8"\u795e\u7ecf\u5143: %d  \u7a81\u89e6: %d  GJ: %d",
        (int)engine.neurons().size(),
        (int)engine.connectome().num_synapses(),
        (int)engine.connectome().num_gap_junctions());

    ImGui::Separator();

    // === Tuning parameters ===
    ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), u8"\u53c2\u6570\u8c03\u8282:");
    auto& p = engine.params;
    ImGui::SliderFloat(u8"\u68af\u5ea6\u589e\u76ca", &p.weathervane_gain, 1.0f, 1000.0f, "%.0f");
    ImGui::SliderFloat(u8"\u7a81\u89e6\u6743\u91cd", &p.synapse_scale, 0.1f, 5.0f, "%.2f");
    ImGui::SliderFloat(u8"\u901f\u5ea6\u500d\u7387", &p.speed_scale, 0.2f, 5.0f, "%.2f");
    ImGui::SliderFloat(u8"\u611f\u89c9\u589e\u76ca", &p.sensory_gain, 0.1f, 10.0f, "%.2f");
    ImGui::SliderFloat(u8"\u504f\u7f6e\u9650\u5e45(pA)", &p.bias_clamp, 1.0f, 100.0f, "%.1f");

    if (ImGui::Button(u8"\u91cd\u7f6e\u53c2\u6570")) {
        p = SimulationEngine::TuningParams{};
    }

    ImGui::Separator();

    // === Signal chain diagnostics ===
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), u8"\u4fe1\u53f7\u94fe\u8bca\u65ad:");

    auto head = engine.body().get_head_position();
    auto grad = engine.environment().chemical_field().gradient(head);
    double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);
    ImGui::Text(u8"\u2460 \u68af\u5ea6: %.4f /mm", grad_mag);

    double heading = engine.body().get_head_angle();
    double grad_normal = -std::sin(heading) * grad.x + std::cos(heading) * grad.y;
    double bias = p.weathervane_gain * grad_normal;
    if (bias > p.bias_clamp) bias = p.bias_clamp;
    if (bias < -p.bias_clamp) bias = -p.bias_clamp;
    ImGui::Text(u8"\u2461 \u504f\u7f6e: %.2f pA", bias);

    int smddl = engine.connectome().get_neuron_id("SMDDL");
    int smdvl = engine.connectome().get_neuron_id("SMDVL");
    if (smddl >= 0 && smdvl >= 0) {
        double vd = engine.neurons()[smddl]->get_membrane_potential();
        double vv = engine.neurons()[smdvl]->get_membrane_potential();
        ImGui::Text(u8"\u2462 SMD\u5dee: %.1f mV", vd - vv);
    }

    ImGui::Text(u8"\u2463 \u66f2\u7387: %.4f", engine.body().segments()[0].curvature);
    ImGui::Text(u8"\u2464 \u901f\u5ea6: %.4f mm/s", engine.body().get_speed());

    ImGui::Separator();

    // === Neuron ablation ===
    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), u8"\u795e\u7ecf\u5143\u6d88\u878d:");
    ImGui::SetNextItemWidth(100);
    ImGui::InputTextWithHint("##ablate", u8"\u540d\u79f0 (e.g. AVA)", ablate_buf_, sizeof(ablate_buf_));
    ImGui::SameLine();
    if (ImGui::Button(u8"\u6d88\u878d")) {
        std::string name = ablate_buf_;
        if (!name.empty()) {
            // Try exact match first
            int id = engine.connectome().get_neuron_id(name);
            if (id >= 0) {
                engine.neurons_mut()[id]->ablate();
            }
            // Try L/R pair
            int idl = engine.connectome().get_neuron_id(name + "L");
            int idr = engine.connectome().get_neuron_id(name + "R");
            if (idl >= 0) engine.neurons_mut()[idl]->ablate();
            if (idr >= 0) engine.neurons_mut()[idr]->ablate();
        }
    }

    ImGui::Separator();

    // === NPR-1 strain switch ===
    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1), u8"\u83cc\u682a\u5207\u6362:");
    float npr1 = (float)engine.npr1_rmg();
    if (ImGui::SliderFloat("NPR-1 RMG (pA)", &npr1, -30.0f, 0.0f, "%.0f")) {
        engine.set_npr1_rmg(npr1);
    }
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), npr1 < -10 ? "N2" : "Hawaiian");

    // === Keyboard shortcuts reminder ===
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), u8"\u5feb\u6377\u952e: Space=\u6682\u505c  Esc=\u9000\u51fa");

    ImGui::End();
}

} // namespace celegans