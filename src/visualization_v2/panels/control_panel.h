#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class ControlPanel : public Panel {
public:
    ControlPanel() : Panel(u8"\u63a7\u5236\u9762\u677f") {}  // 控制面板
    void render(const DataBus& bus, SimulationEngine& engine) override;

private:
    char ablate_buf_[32] = {};
    int stim_type_ = 0;  // 0=touch, 1=light, 2=temp, 3=osmotic
};

} // namespace celegans