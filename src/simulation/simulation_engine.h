#pragma once

#include "core/types.h"
#include "core/config.h"
#include "neuron/single_compartment.h"
#include "neuron/neuron_factory.h"
#include "connectome/connectome.h"
#include "connectome/connectome_loader.h"
#include "body/body_model.h"
#include "motor/motor_controller.h"
#include "environment/environment.h"
#include "environment/sensory_transducer.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace celegans {

class SimulationEngine {
public:
    SimulationEngine();

    // Initialize with config or defaults
    void initialize(const Config& config);
    void initialize_default();

    // Run simulation
    void step();
    void run(double duration_ms);

    // Access
    double current_time() const { return current_time_; }
    double dt() const { return dt_; }
    const BodyModel& body() const { return body_; }
    const Environment& environment() const { return environment_; }
    const Connectome& connectome() const { return connectome_; }
    const std::vector<std::unique_ptr<Neuron>>& neurons() const { return neurons_; }

    // Callback for each step (for logging/visualization)
    using StepCallback = std::function<void(const SimulationEngine&, int step_num)>;
    void set_step_callback(StepCallback cb) { step_callback_ = std::move(cb); }

private:
    double dt_ = 0.5;          // simulation timestep (ms)
    double current_time_ = 0.0;
    int step_count_ = 0;

    Environment environment_;
    BodyModel body_;
    Connectome connectome_;
    MotorController motor_controller_;
    std::vector<std::unique_ptr<Neuron>> neurons_;

    StepCallback step_callback_;

    void create_neurons();

    // Step 13: Biologically grounded locomotion (replaces Step 12 placeholders)
    void apply_sensory_input();          // chemosensory neurons sample gradient, others get baseline
    void apply_weathervane();            // gradient ⊥ heading → SMD bias (Iino & Yoshida 2009)
    void apply_proprioceptive_stretch(); // body curvature → MEC channels in motor neurons
    void apply_head_tonic();             // tonic drive to head motor neurons (from upstream)

    // Chemosensory transduction: neuron_id → transducer
    struct ChemoMapping {
        int neuron_id;
        ChemoTransducer transducer;
    };
    std::vector<ChemoMapping> chemo_mappings_;

    // Non-chemosensory neurons (touch, etc.) get fixed baseline
    std::vector<int> other_sensory_ids_;
    double sensory_baseline_ = 3.0; // pA, low baseline for touch neurons (no stimulus)

    // Head motor neuron IDs (for tonic upstream drive)
    std::vector<int> head_motor_ids_;
    double head_tonic_ = 3.0;  // pA, near CCA-1 window for rebound oscillation

    // Proprioceptive mapping: motor neuron → body segment
    struct ProprioMapping {
        int neuron_id;
        int sample_segment;   // anterior segment to sense curvature from
        bool is_dorsal;       // true=dorsal (negative curv excites), false=ventral
    };
    std::vector<ProprioMapping> proprio_mappings_;
};

} // namespace celegans
