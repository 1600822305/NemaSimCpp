#pragma once
// ================================================================
// Panel — Abstract base class for all visualization panels
// 
// Each panel is a self-contained ImGui window that reads from DataBus.
// Panels can be enabled/disabled, resized, and arranged by PanelManager.
// New panels can be added by inheriting from Panel and registering
// with PanelManager — no changes to existing code needed.
// ================================================================

#include <string>

namespace celegans {

class DataBus;
class SimulationEngine;

class Panel {
public:
    Panel(const std::string& title, bool visible = true)
        : title_(title), visible_(visible) {}
    virtual ~Panel() = default;

    // Called once after engine + DataBus initialization
    virtual void initialize(const SimulationEngine& engine, const DataBus& bus) { (void)engine; (void)bus; }

    // Called every frame to render the panel
    virtual void render(const DataBus& bus, SimulationEngine& engine) = 0;

    // Panel metadata
    const std::string& title() const { return title_; }
    bool visible() const { return visible_; }
    void set_visible(bool v) { visible_ = v; }
    void toggle_visible() { visible_ = !visible_; }

    // Layout hints (set by PanelManager or user)
    struct LayoutHint {
        float x = 0, y = 0;
        float w = 400, h = 300;
    };
    LayoutHint layout;

protected:
    std::string title_;
    bool visible_;
};

} // namespace celegans