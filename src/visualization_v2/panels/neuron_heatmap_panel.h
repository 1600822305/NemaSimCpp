#pragma once
#include "visualization_v2/panel.h"

namespace celegans {

class NeuronHeatmapPanel : public Panel {
public:
    NeuronHeatmapPanel() : Panel(u8"\u795e\u7ecf\u5143\u6d3b\u6027\u77e9\u9635") {}  // 神经元活性矩阵
    void render(const DataBus& bus, SimulationEngine& engine) override;
};

} // namespace celegans