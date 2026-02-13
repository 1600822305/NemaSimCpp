#include "visualization_v2/panels/worm_body_panel.h"
#include "visualization_v2/data_bus.h"
#include <imgui.h>

namespace celegans {

void WormBodyPanel::initialize(const SimulationEngine& engine, const DataBus& bus) {
    initialized_ = renderer_.initialize(400, 400);
}

void WormBodyPanel::render(const DataBus& bus, SimulationEngine& engine) {
    ImGui::Begin(title_.c_str(), nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (initialized_) {
        renderer_.draw(engine.body().segments());
    } else {
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), u8"\u867e\u4f53\u6e32\u67d3\u5668\u521d\u59cb\u5316\u5931\u8d25");
    }

    ImGui::End();
}

} // namespace celegans