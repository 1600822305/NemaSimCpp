#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <cstring>
#include <algorithm>

namespace celegans {

SimulationEngine::SimulationEngine() {}

static bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

void SimulationEngine::initialize_default() {
    LOG_INFO("=== C. elegans Simulation Engine ===");
    LOG_INFO("Initializing with default connectome...");

    // 1. Generate default connectome
    std::vector<NeuronInfo> neuron_infos;
    std::vector<SynapseInfo> synapse_infos;
    std::vector<GapJunctionInfo> gj_infos;
    ConnectomeLoader::generate_default_connectome(neuron_infos, synapse_infos, gj_infos);

    // 2. Build connectome
    connectome_.build(neuron_infos, synapse_infos, gj_infos);

    // 3. Create neurons (NeuronFactory now auto-specializes by name)
    neurons_.clear();
    neurons_.reserve(neuron_infos.size());
    for (auto& info : neuron_infos) {
        neurons_.push_back(NeuronFactory::create(info));
    }
    LOG_INFO("Created ", neurons_.size(), " neurons");

    // 4. Initialize body
    body_.initialize(Vector2d{25.0, 25.0}, 0.0);
    LOG_INFO("Body initialized at (25, 25), heading 0 rad");

    // 5. Initialize motor controller
    std::unordered_map<std::string, int> name_to_id;
    for (auto& info : neuron_infos) {
        name_to_id[info.name] = info.id;
    }
    motor_controller_.initialize(name_to_id);

    // 6. Initialize environment
    environment_.initialize(50.0, 50.0);
    environment_.chemical_field().add_point_source(Vector2d{35.0, 35.0}, 1.0);
    LOG_INFO("Environment initialized (50x50 mm), food source at (35, 35)");

    // 7. Collect sensory neuron IDs (they receive baseline environmental input)
    for (auto& info : neuron_infos) {
        if (info.type == NeuronType::SENSORY) {
            sensory_ids_.push_back(info.id);
        }
    }

    // 8. Collect head motor neuron IDs (SMD/RMD receive tonic from upstream)
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "SMD") || starts_with(info.name, "RMD")) {
            head_motor_ids_.push_back(info.id);
        }
    }

    // 9. Build proprioceptive mappings (motor neuron → body segment for MEC channel)
    // Dorsal B-class: sense anterior curvature, negative curv (ventral bend) excites
    auto add_pm = [&](const char* name, int seg, bool dorsal) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0) proprio_mappings_.push_back({id, seg, dorsal});
    };
    add_pm("DB01", 0, true);  add_pm("DB02", 5, true);  add_pm("DB03", 15, true);
    add_pm("VB01", 0, false); add_pm("VB02", 5, false); add_pm("VB03", 15, false);
    add_pm("DA01", 0, true);  add_pm("DA02", 5, true);  add_pm("DA03", 15, true);
    add_pm("VA01", 0, false); add_pm("VA02", 5, false); add_pm("VA03", 15, false);

    LOG_INFO("Sensory baseline: ", sensory_ids_.size(), " sensory neurons, ", sensory_baseline_, " pA");
    LOG_INFO("Head tonic: ", head_motor_ids_.size(), " head motor neurons, ", head_tonic_, " pA");
    LOG_INFO("Proprioceptive MEC: ", proprio_mappings_.size(), " motor neuron stretch mappings");
    LOG_INFO("Initialization complete. dt = ", dt_, " ms");
}

void SimulationEngine::initialize(const Config& config) {
    dt_ = config.get_double("dt", 0.5);

    std::string neurons_file = config.get_string("neurons_file", "");
    if (neurons_file.empty()) {
        initialize_default();
        return;
    }

    // Load from CSV files
    auto neuron_infos = ConnectomeLoader::load_neurons(neurons_file);
    std::unordered_map<std::string, int> name_to_id;
    for (auto& ni : neuron_infos) name_to_id[ni.name] = ni.id;

    std::string synapses_file = config.get_string("synapses_file", "");
    auto synapse_infos = ConnectomeLoader::load_synapses(synapses_file, name_to_id);

    std::string gj_file = config.get_string("gap_junctions_file", "");
    auto gj_infos = ConnectomeLoader::load_gap_junctions(gj_file, name_to_id);

    connectome_.build(neuron_infos, synapse_infos, gj_infos);

    neurons_.clear();
    for (auto& info : neuron_infos) {
        neurons_.push_back(NeuronFactory::create(info));
    }

    body_.initialize(Vector2d{25.0, 25.0}, 0.0);
    motor_controller_.initialize(name_to_id);

    double arena_w = config.get_double("arena_width", 50.0);
    double arena_h = config.get_double("arena_height", 50.0);
    environment_.initialize(arena_w, arena_h);
}

void SimulationEngine::step() {
    // 1. Environment update
    environment_.step(dt_ * 0.001);

    // 2. Sensory baseline: sensory neurons continuously sample environment
    // This is NOT cheating — sensory neurons ARE in contact with the environment
    // REF: Bargmann 2006 - chemosensory neurons have baseline activity
    apply_sensory_baseline();

    // 3. Head motor tonic: upstream interneuron drive (RIA→SMD already in connectome)
    // Small tonic represents the net excitatory input from the head circuit
    apply_head_tonic();

    // 4. Proprioceptive stretch: set MEC channel stretch from body curvature
    // The stretch goes through the ion channel → membrane equation (no current injection)
    // REF: Wen et al. 2012 - proprioceptive coupling within motor neurons
    apply_proprioceptive_stretch();

    // 5. Compute synaptic currents (chemical + electrical)
    connectome_.compute_synaptic_currents(neurons_);

    // 6. Update all neuron membrane potentials
    for (auto& neuron : neurons_) {
        neuron->step(dt_);
    }

    // 7. Motor output: motor neurons → muscle activations
    motor_controller_.update(neurons_, body_);

    // 8. Body physics update
    body_.update_physics(dt_ * 0.001);

    // 9. Callback
    if (step_callback_) {
        step_callback_(*this, step_count_);
    }

    current_time_ += dt_;
    step_count_++;
}

void SimulationEngine::run(double duration_ms) {
    int total_steps = static_cast<int>(duration_ms / dt_);
    LOG_INFO("Running simulation for ", duration_ms, " ms (", total_steps, " steps)...");

    for (int i = 0; i < total_steps; ++i) {
        step();
    }

    LOG_INFO("Simulation complete. Final time: ", current_time_, " ms");
}

void SimulationEngine::apply_sensory_baseline() {
    // Sensory neurons have spontaneous activity even in featureless environment
    // This represents continuous environmental sampling (thermal noise, baseline chem levels)
    // The current flows through the connectome: sensory → interneurons → command → motor
    // REF: Bargmann 2006, Ward 1973 - sensory neuron baseline activity
    int n = static_cast<int>(neurons_.size());
    for (int id : sensory_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(sensory_baseline_);
        }
    }
}

void SimulationEngine::apply_head_tonic() {
    // Head motor neurons (SMD/RMD) receive tonic excitatory input
    // from upstream interneurons (RIA synapses already in connectome)
    // This small tonic keeps the head circuit near oscillation threshold
    // The actual oscillation emerges from: CCA-1 rebound + cross-inhibition
    // REF: Hendricks 2012, Shen 2016
    int n = static_cast<int>(neurons_.size());
    for (int id : head_motor_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(head_tonic_);
        }
    }
}

void SimulationEngine::apply_proprioceptive_stretch() {
    // Set stretch input on MechanoSensitive channels in motor neurons
    // The channel converts mechanical stretch into ionic current through the membrane equation
    // NO external current injection — the current comes from g_MEC * m * (V - E_cat)
    // REF: Wen et al. 2012, Li et al. 2006 - mechanosensitive channels in motor neurons
    int n = static_cast<int>(neurons_.size());
    for (auto& pm : proprio_mappings_) {
        if (pm.neuron_id < 0 || pm.neuron_id >= n) continue;

        double curv = body_.get_local_curvature(pm.sample_segment);

        // Dorsal motor neurons: excited by ventral bend (negative curvature) of anterior segment
        // Ventral motor neurons: excited by dorsal bend (positive curvature) of anterior segment
        double stretch = pm.is_dorsal ? -curv : curv;
        if (stretch < 0.0) stretch = 0.0; // only positive stretch activates channel

        // Set stretch on the neuron's MEC channel (flows through membrane equation)
        auto* scn = dynamic_cast<SingleCompartmentNeuron*>(neurons_[pm.neuron_id].get());
        if (scn) {
            scn->set_stretch_input(stretch);
        }
    }
}

} // namespace celegans
