#include "visualization_v2/data_bus.h"
#include <cmath>
#include <algorithm>

namespace celegans {

DataBus::DataBus() {}

void DataBus::initialize(const SimulationEngine& engine) {
    reset();

    // Initialize neuron snapshots for all neurons
    const auto& neurons = engine.neurons();
    const auto& infos = engine.connectome().neuron_infos();
    neuron_snapshots_.resize(neurons.size());
    for (size_t i = 0; i < neurons.size(); ++i) {
        neuron_snapshots_[i].id = infos[i].id;
        neuron_snapshots_[i].name = infos[i].name;
        neuron_snapshots_[i].type = infos[i].type;
        neuron_snapshots_[i].voltage = neurons[i]->get_membrane_potential();
        neuron_snapshots_[i].release_rate = neurons[i]->get_transmitter_release_rate();
        neuron_snapshots_[i].ablated = neurons[i]->is_ablated();
    }

    // Initialize modulator histories
    // We track all 7 known modulators
    const char* mod_names[] = {"5-HT", "DA", "OA", "TA", "NLP-12", "PDF", "FLP-11"};
    mod_histories_.clear();
    for (auto name : mod_names) {
        mod_histories_.push_back({name, RingBuffer<double>(60000)});
    }

    // Add default traces for key neurons
    auto add_default = [&](const char* name) {
        int id = engine.connectome().get_neuron_id(name);
        if (id >= 0) add_trace(id, name);
    };
    add_default("SMDDL");
    add_default("SMDVL");
    add_default("AVAL");
    add_default("AVBL");
    add_default("AIBL");
    add_default("AIYL");
    add_default("ASEL");
    add_default("ASER");

    // Record initial position
    auto head = engine.body().get_head_position();
    trajectory_.push_back({head.x, head.y, 0.0});

    // Initial field snapshot
    sample_field(engine);
}

void DataBus::reset() {
    trajectory_.clear();
    neuron_snapshots_.clear();
    traces_.clear();
    traced_ids_.clear();
    mod_histories_.clear();
    neuromod_times_.clear();
    internal_ = InternalStates{};
    behavior_ = BehaviorSnapshot{};
    behavior_history_ = BehaviorHistory{};
    stats_ = Stats{};
    heading_times_.clear();
    heading_values_.clear();
    field_data_.clear();
    current_time_ = 0.0;
    step_count_ = 0;
    steps_since_last_slow_ = 0;
    prev_reversing_ = false;
    prev_omega_ = false;
}

void DataBus::update(const SimulationEngine& engine, int steps_elapsed) {
    current_time_ = engine.current_time();
    step_count_ = engine.get_step_count();

    // Fast sampling (every step): neuron voltages for traces
    sample_neurons(engine);

    // Medium sampling (every 10 steps = 5ms): trajectory
    if (step_count_ % 10 == 0) {
        sample_trajectory(engine);
    }

    // Slow sampling (every 20 steps = 10ms): neuromod, internal, behavior, heading
    steps_since_last_slow_ += steps_elapsed;
    if (step_count_ % 20 == 0) {
        sample_neuromod(engine);
        sample_internal(engine);
        sample_behavior(engine);
        sample_heading(engine);
    }

    // Very slow sampling (every 200 steps = 100ms): stats, field
    if (step_count_ % 200 == 0) {
        sample_stats(engine);
    }

    // Field update (every 1000 steps = 500ms)
    if (step_count_ % 1000 == 0) {
        sample_field(engine);
    }
}

void DataBus::add_trace(int neuron_id, const std::string& name) {
    if (traced_ids_.count(neuron_id)) return;
    traced_ids_.insert(neuron_id);
    traces_.push_back({neuron_id, name, RingBuffer<double>(20000), RingBuffer<double>(20000)});
}

void DataBus::remove_trace(int neuron_id) {
    traced_ids_.erase(neuron_id);
    traces_.erase(
        std::remove_if(traces_.begin(), traces_.end(),
            [neuron_id](const NeuronTrace& t) { return t.neuron_id == neuron_id; }),
        traces_.end());
}

bool DataBus::is_traced(int neuron_id) const {
    return traced_ids_.count(neuron_id) > 0;
}

void DataBus::sample_trajectory(const SimulationEngine& engine) {
    auto head = engine.body().get_head_position();
    trajectory_.push_back({head.x, head.y, current_time_});
    if (trajectory_.size() > MAX_TRAJECTORY) {
        trajectory_.erase(trajectory_.begin(),
            trajectory_.begin() + (long long)(trajectory_.size() - MAX_TRAJECTORY));
    }
}

void DataBus::sample_neurons(const SimulationEngine& engine) {
    const auto& neurons = engine.neurons();
    int n = static_cast<int>(neurons.size());

    // Update snapshot (cheap)
    for (int i = 0; i < n && i < (int)neuron_snapshots_.size(); ++i) {
        neuron_snapshots_[i].voltage = neurons[i]->get_membrane_potential();
        neuron_snapshots_[i].release_rate = neurons[i]->get_transmitter_release_rate();
        neuron_snapshots_[i].ablated = neurons[i]->is_ablated();
    }

    // Update traced waveforms
    for (auto& tr : traces_) {
        if (tr.neuron_id >= 0 && tr.neuron_id < n) {
            tr.voltages.push(neurons[tr.neuron_id]->get_membrane_potential());
            tr.times.push(current_time_);
        }
    }
}

void DataBus::sample_neuromod(const SimulationEngine& engine) {
    neuromod_times_.push(current_time_);
    const auto& nm = engine.neuromodulation();
    for (auto& mh : mod_histories_) {
        mh.concentration.push(nm.get_concentration(mh.name));
    }
}

void DataBus::sample_internal(const SimulationEngine& engine) {
    internal_.times.push(current_time_);
    internal_.satiety.push(engine.satiety());
    internal_.food_memory.push(engine.food_memory());
    internal_.fatigue.push(engine.fatigue());
    internal_.sickness.push(engine.sickness());
    internal_.ins1.push(engine.ins1_conc());
    internal_.dauer_signal.push(engine.dauer_signal());
    internal_.sensitization.push(engine.sensitization());
    internal_.egg_pressure.push(engine.egg_pressure());
    internal_.molt_hormone.push(engine.molt_hormone());
    internal_.arousal_threshold.push(engine.arousal_threshold());
    internal_.awc_adapt_gain.push(engine.awc_adapt_gain());
    internal_.egl4_nuclear.push(engine.egl4_nuclear());
    internal_.learning_sleep_drive.push(engine.learning_sleep_drive());
}

void DataBus::sample_behavior(const SimulationEngine& engine) {
    behavior_.is_reversing = engine.is_reversing();
    behavior_.is_omega = engine.is_omega_turning();
    behavior_.is_sleeping = engine.is_sleeping();
    behavior_.is_dauer = engine.is_dauer();
    behavior_.nictation_waving = engine.nictation_waving();
    behavior_.in_lethargus = engine.in_lethargus();
    behavior_.dmp_active = engine.dmp_active();
    behavior_.tap_active = engine.tap_active();
    behavior_.exo_5ht = engine.exo_5ht();

    behavior_history_.times.push(current_time_);
    behavior_history_.reversing.push(behavior_.is_reversing ? 1 : 0);
    behavior_history_.omega.push(behavior_.is_omega ? 1 : 0);
    behavior_history_.sleeping.push(behavior_.is_sleeping ? 1 : 0);
    behavior_history_.dauer.push(behavior_.is_dauer ? 1 : 0);
    behavior_history_.nictation.push(behavior_.nictation_waving ? 1 : 0);
    behavior_history_.lethargus.push(behavior_.in_lethargus ? 1 : 0);
    behavior_history_.dmp.push(behavior_.dmp_active ? 1 : 0);

    // Count transitions
    if (behavior_.is_reversing && !prev_reversing_) stats_.total_reversals++;
    if (behavior_.is_omega && !prev_omega_) stats_.total_omegas++;
    prev_reversing_ = behavior_.is_reversing;
    prev_omega_ = behavior_.is_omega;
}

void DataBus::sample_stats(const SimulationEngine& engine) {
    auto head = engine.body().get_head_position();
    double dx = head.x - food_pos_.x;
    double dy = head.y - food_pos_.y;
    double dist = std::sqrt(dx * dx + dy * dy);

    stats_.times.push(current_time_);
    stats_.distance_to_food.push(dist);
    stats_.speed.push(engine.body().get_speed());
    stats_.pump_rate_hz = engine.pump_rate_hz();
    stats_.total_pumps = engine.total_pumps();
    stats_.eggs_laid = static_cast<int>(engine.egg_laid_count());
    stats_.dmp_count = engine.dmp_count();

    // Running CI
    if (trajectory_.size() >= 2) {
        auto& p1 = trajectory_[trajectory_.size() - 2];
        auto& p2 = trajectory_.back();
        double mvx = p2.x - p1.x;
        double mvy = p2.y - p1.y;
        double mv_len = std::sqrt(mvx * mvx + mvy * mvy);
        if (mv_len > 1e-8) {
            double to_food_x = food_pos_.x - p2.x;
            double to_food_y = food_pos_.y - p2.y;
            double tf_len = std::sqrt(to_food_x * to_food_x + to_food_y * to_food_y);
            if (tf_len > 1e-8) {
                double ci = (mvx * to_food_x + mvy * to_food_y) / (mv_len * tf_len);
                stats_.ci_sum += ci;
                stats_.ci_count++;
                stats_.ci_running.push(stats_.ci_sum / stats_.ci_count);
            }
        }
    }
}

void DataBus::sample_heading(const SimulationEngine& engine) {
    heading_times_.push(current_time_);
    heading_values_.push(engine.body().get_head_angle() * 180.0 / 3.14159265);
}

void DataBus::sample_field(const SimulationEngine& engine) {
    field_data_.resize(field_nx_ * field_ny_);
    double cell_w = engine.environment().width() / field_nx_;
    double cell_h = engine.environment().height() / field_ny_;

    for (int iy = 0; iy < field_ny_; ++iy) {
        for (int ix = 0; ix < field_nx_; ++ix) {
            double cx = (ix + 0.5) * cell_w;
            double cy = (iy + 0.5) * cell_h;
            Vector2d pos{cx, cy};
            double val = 0.0;

            switch (active_field_) {
                case FieldType::ATTRACTANT:
                    val = engine.environment().chemical_field().sample(pos);
                    break;
                case FieldType::SOLUBLE:
                    val = engine.environment().soluble_field().sample(pos);
                    break;
                case FieldType::REPELLENT:
                    val = engine.environment().repellent_field().sample(pos);
                    break;
                case FieldType::TEMPERATURE:
                    val = engine.environment().sample_temperature(pos);
                    break;
                case FieldType::PHEROMONE:
                    val = engine.environment().sample_pheromone(pos);
                    break;
                case FieldType::LIGHT:
                    val = engine.environment().sample_light(pos);
                    break;
                case FieldType::OSMOLARITY:
                    val = engine.environment().sample_osmolarity(pos);
                    break;
            }
            field_data_[iy * field_nx_ + ix] = val;
        }
    }
}

} // namespace celegans