#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class StatsPanel : public Panel {
public:
    StatsPanel() : Panel(u8"\u7edf\u8ba1\u6307\u6807") {}  // 统计指标
    void render(const DataBus& bus, SimulationEngine& engine) override;
};

} // namespace celegans