#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class BehaviorPanel : public Panel {
public:
    BehaviorPanel() : Panel(u8"\u884c\u4e3a\u72b6\u6001") {}  // 行为状态
    void render(const DataBus& bus, SimulationEngine& engine) override;
};

} // namespace celegans