#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class NeuromodPanel : public Panel {
public:
    NeuromodPanel() : Panel(u8"\u795e\u7ecf\u8c03\u8d28\u4e0e\u5185\u90e8\u72b6\u6001") {}  // 神经调质与内部状态
    void render(const DataBus& bus, SimulationEngine& engine) override;

private:
    bool show_internal_ = true;
};

} // namespace celegans