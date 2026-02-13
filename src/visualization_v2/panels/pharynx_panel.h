#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class PharynxPanel : public Panel {
public:
    PharynxPanel() : Panel(u8"\u54bd\u90e8\u7cfb\u7edf") {}  // 咽部系统
    void render(const DataBus& bus, SimulationEngine& engine) override;
};

} // namespace celegans