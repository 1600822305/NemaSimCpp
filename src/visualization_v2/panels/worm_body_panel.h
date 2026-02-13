#pragma once
#include "visualization_v2/panel.h"
#include "visualization/worm_renderer_3d.h"

namespace celegans {

class WormBodyPanel : public Panel {
public:
    WormBodyPanel() : Panel(u8"\u866b\u4f53 (302\u795e\u7ecf\u5143\u9a71\u52a8)") {}  // 虫体 (302神经元驱动)
    void initialize(const SimulationEngine& engine, const DataBus& bus) override;
    void render(const DataBus& bus, SimulationEngine& engine) override;

private:
    WormRenderer3D renderer_;
    bool initialized_ = false;
};

} // namespace celegans