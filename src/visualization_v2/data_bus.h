#pragma once
// ================================================================
// DataBus — Central data collection hub for visualization
// 
// Samples SimulationEngine state at configurable intervals and stores
// time-series in ring buffers. All panels read from DataBus instead
// of directly querying the engine, ensuring consistent snapshots.
// ================================================================

#include "visualization_v2/ring_buffer.h"
#include "simulation/simulation_engine.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace celegans {

struct TrajectoryPoint { double x, y, t; };

struct NeuronSnapshot {
    int id;
    std::string name;
    NeuronType type;
    double voltage;       // mV
    double release_rate;  // [0,1]
    bool ablated;
};

class DataBus {
public:
    DataBus();

    // Call once after engine initialization
    void initialize(const SimulationEngine& engine);

    // Call every frame — samples engine at appropriate intervals
    void update(const SimulationEngine& engine, int steps_elapsed);

    // ---- Trajectory ----
    const std::vector<TrajectoryPoint>& trajectory() const { return trajectory_; }
    static constexpr size_t MAX_TRAJECTORY = 100000;

    // ---- Neuron voltages (all 302, sampled every step) ----
    // Current snapshot of all neurons
    const std::vector<NeuronSnapshot>& neuron_snapshots() const { return neuron_snapshots_; }

    // Tracked neuron traces (user-selected subset for waveform plotting)
    struct NeuronTrace {
        int neuron_id;
        std::string name;
        RingBuffer<double> voltages{20000};
        RingBuffer<double> times{20000};
    };
    const std::vector<NeuronTrace>& traces() const { return traces_; }
    void add_trace(int neuron_id, const std::string& name);
    void remove_trace(int neuron_id);
    bool is_traced(int neuron_id) const;

    // ---- Neuromodulation (7 modulators, sampled every 20 steps = 10ms) ----
    struct ModulatorHistory {
        std::string name;
        RingBuffer<double> concentration{60000};
    };
    const std::vector<ModulatorHistory>& modulator_histories() const { return mod_histories_; }
    RingBuffer<double>& neuromod_times() { return neuromod_times_; }
    const RingBuffer<double>& neuromod_times() const { return neuromod_times_; }

    // ---- Internal states (sampled every 20 steps) ----
    struct InternalStates {
        RingBuffer<double> times{60000};
        RingBuffer<double> satiety{60000};
        RingBuffer<double> food_memory{60000};
        RingBuffer<double> fatigue{60000};
        RingBuffer<double> sickness{60000};
        RingBuffer<double> ins1{60000};
        RingBuffer<double> dauer_signal{60000};
        RingBuffer<double> sensitization{60000};
        RingBuffer<double> egg_pressure{60000};
        RingBuffer<double> molt_hormone{60000};
        RingBuffer<double> arousal_threshold{60000};
        RingBuffer<double> awc_adapt_gain{60000};
        RingBuffer<double> egl4_nuclear{60000};
        RingBuffer<double> learning_sleep_drive{60000};
    };
    const InternalStates& internal_states() const { return internal_; }

    // ---- Behavior states ----
    struct BehaviorSnapshot {
        bool is_reversing = false;
        bool is_omega = false;
        bool is_sleeping = false;
        bool is_dauer = false;
        bool nictation_waving = false;
        bool in_lethargus = false;
        bool dmp_active = false;
        bool tap_active = false;
        bool exo_5ht = false;
    };
    const BehaviorSnapshot& behavior() const { return behavior_; }

    struct BehaviorHistory {
        RingBuffer<double> times{30000};
        RingBuffer<uint8_t> reversing{30000};
        RingBuffer<uint8_t> omega{30000};
        RingBuffer<uint8_t> sleeping{30000};
        RingBuffer<uint8_t> dauer{30000};
        RingBuffer<uint8_t> nictation{30000};
        RingBuffer<uint8_t> lethargus{30000};
        RingBuffer<uint8_t> dmp{30000};
    };
    const BehaviorHistory& behavior_history() const { return behavior_history_; }

    // ---- Stats ----
    struct Stats {
        RingBuffer<double> times{30000};
        RingBuffer<double> distance_to_food{30000};
        RingBuffer<double> speed{30000};
        RingBuffer<double> ci_running{30000};
        double ci_sum = 0.0;
        int ci_count = 0;
        int total_reversals = 0;
        int total_omegas = 0;
        double pump_rate_hz = 0.0;
        int total_pumps = 0;
        int eggs_laid = 0;
        int dmp_count = 0;
    };
    const Stats& stats() const { return stats_; }

    // ---- Heading ----
    RingBuffer<double>& heading_times() { return heading_times_; }
    const RingBuffer<double>& heading_times() const { return heading_times_; }
    RingBuffer<double>& heading_values() { return heading_values_; }
    const RingBuffer<double>& heading_values() const { return heading_values_; }

    // ---- Environment field snapshot (for heatmap) ----
    enum class FieldType {
        ATTRACTANT, SOLUBLE, REPELLENT, TEMPERATURE, PHEROMONE, LIGHT, OSMOLARITY
    };
    void set_active_field(FieldType ft) { active_field_ = ft; }
    FieldType active_field() const { return active_field_; }
    const std::vector<double>& field_data() const { return field_data_; }
    int field_nx() const { return field_nx_; }
    int field_ny() const { return field_ny_; }

    // ---- Food position (for CI calculation) ----
    Vector2d food_position() const { return food_pos_; }
    void set_food_position(Vector2d pos) { food_pos_ = pos; }

    // ---- Timing ----
    double current_time() const { return current_time_; }
    int step_count() const { return step_count_; }

    // ---- Reset ----
    void reset();

private:
    void sample_trajectory(const SimulationEngine& engine);
    void sample_neurons(const SimulationEngine& engine);
    void sample_neuromod(const SimulationEngine& engine);
    void sample_internal(const SimulationEngine& engine);
    void sample_behavior(const SimulationEngine& engine);
    void sample_stats(const SimulationEngine& engine);
    void sample_heading(const SimulationEngine& engine);
    void sample_field(const SimulationEngine& engine);

    double current_time_ = 0.0;
    int step_count_ = 0;
    int steps_since_last_slow_ = 0;  // for 20-step interval sampling

    // Trajectory
    std::vector<TrajectoryPoint> trajectory_;

    // Neuron state
    std::vector<NeuronSnapshot> neuron_snapshots_;
    std::vector<NeuronTrace> traces_;
    std::unordered_set<int> traced_ids_;

    // Neuromod
    std::vector<ModulatorHistory> mod_histories_;
    RingBuffer<double> neuromod_times_{60000};

    // Internal states
    InternalStates internal_;

    // Behavior
    BehaviorSnapshot behavior_;
    BehaviorHistory behavior_history_;
    bool prev_reversing_ = false;
    bool prev_omega_ = false;

    // Stats
    Stats stats_;

    // Heading
    RingBuffer<double> heading_times_{20000};
    RingBuffer<double> heading_values_{20000};

    // Field
    FieldType active_field_ = FieldType::ATTRACTANT;
    std::vector<double> field_data_;
    int field_nx_ = 50, field_ny_ = 50;

    Vector2d food_pos_{35.0, 35.0};
};

} // namespace celegans