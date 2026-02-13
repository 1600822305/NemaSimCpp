#pragma once
#include "visualization_v2/panel.h"
#include <vector>
#include <string>

namespace celegans {

class MotorArrayPanel : public Panel {
public:
    MotorArrayPanel() : Panel(u8"\u8170\u7d22\u8fd0\u52a8\u795e\u7ecf\u5143") {}  // 腹索运动神经元
    void initialize(const SimulationEngine& engine, const DataBus& bus) override;
    void render(const DataBus& bus, SimulationEngine& engine) override;

private:
    struct MNEntry {
        int neuron_id;
        std::string name;
        std::string mn_class;  // DA, VA, DB, VB, DD, VD, AS
        int seg_start, seg_end;
        bool is_dorsal;
    };
    std::vector<MNEntry> mn_entries_;
};

} // namespace celegans