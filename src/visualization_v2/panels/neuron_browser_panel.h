#pragma once
#include "visualization_v2/panel.h"
#include <string>

namespace celegans {

class NeuronBrowserPanel : public Panel {
public:
    NeuronBrowserPanel() : Panel(u8"\u795e\u7ecf\u5143\u6d4f\u89c8\u5668") {}  // 神经元浏览器
    void render(const DataBus& bus, SimulationEngine& engine) override;

private:
    char search_buf_[64] = {};
    int type_filter_ = 0;  // 0=all, 1=sensory, 2=inter, 3=motor, 4=pharyngeal
};

} // namespace celegans