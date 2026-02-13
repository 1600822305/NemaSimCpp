#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class ArenaPanel : public Panel {
public:
    ArenaPanel() : Panel(u8"\u7ade\u6280\u573a\u4e0e\u8f68\u8ff9") {}  // 竞技场与轨迹
    void render(const DataBus& bus, SimulationEngine& engine) override;

private:
    int field_type_ = 0;  // dropdown index for field overlay
};

} // namespace celegans