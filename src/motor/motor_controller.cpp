#include "motor/motor_controller.h"
#include "core/logger.h"

namespace celegans {

void MotorController::add_mapping(const std::unordered_map<std::string, int>& name_to_id,
                                   const std::string& neuron_name,
                                   int seg_start, int seg_end, bool dorsal,
                                   bool inhibitory) {
    auto it = name_to_id.find(neuron_name);
    if (it != name_to_id.end()) {
        mappings_.push_back({it->second, seg_start, seg_end, dorsal, inhibitory});
    }
}

void MotorController::initialize(const std::unordered_map<std::string, int>& name_to_id) {
    mappings_.clear();

    // B-class (forward): dorsal DB, ventral VB
    // Each motor neuron innervates a few body segments
    // Segments 0-5: head, 6-42: body, 43-47: tail
    add_mapping(name_to_id, "DB01", 4, 10, true);
    add_mapping(name_to_id, "DB02", 10, 20, true);
    add_mapping(name_to_id, "DB03", 20, 30, true);
    add_mapping(name_to_id, "VB01", 4, 10, false);
    add_mapping(name_to_id, "VB02", 10, 20, false);
    add_mapping(name_to_id, "VB03", 20, 30, false);

    // A-class (reverse): dorsal DA, ventral VA
    add_mapping(name_to_id, "DA01", 4, 10, true);
    add_mapping(name_to_id, "DA02", 10, 20, true);
    add_mapping(name_to_id, "DA03", 20, 30, true);
    add_mapping(name_to_id, "VA01", 4, 10, false);
    add_mapping(name_to_id, "VA02", 10, 20, false);
    add_mapping(name_to_id, "VA03", 20, 30, false);

    // D-class (cross-inhibition): GABAergic, inhibit contralateral muscles
    // DD: receives dorsal input, inhibits VENTRAL muscles
    // VD: receives ventral input, inhibits DORSAL muscles
    add_mapping(name_to_id, "DD01", 4, 10, false, true);  // DD inhibits ventral
    add_mapping(name_to_id, "DD02", 10, 20, false, true);
    add_mapping(name_to_id, "DD03", 20, 30, false, true);
    add_mapping(name_to_id, "VD01", 4, 10, true, true);   // VD inhibits dorsal
    add_mapping(name_to_id, "VD02", 10, 20, true, true);
    add_mapping(name_to_id, "VD03", 20, 30, true, true);

    // Head motor neurons: SMD controls head segments
    add_mapping(name_to_id, "SMDDL", 0, 4, true);
    add_mapping(name_to_id, "SMDDR", 0, 4, true);
    add_mapping(name_to_id, "SMDVL", 0, 4, false);
    add_mapping(name_to_id, "SMDVR", 0, 4, false);

    LOG_INFO("Motor controller initialized with ", mappings_.size(), " mappings");
}

void MotorController::update(const std::vector<std::unique_ptr<Neuron>>& neurons, BodyModel& body) {
    body.reset_activations();

    int n_size = static_cast<int>(neurons.size());

    // First pass: excitatory motor neurons (A/B class, SMD)
    for (auto& map : mappings_) {
        if (map.is_inhibitory) continue;
        if (map.neuron_id < 0 || map.neuron_id >= n_size) continue;

        double release = neurons[map.neuron_id]->get_transmitter_release_rate();

        for (int seg = map.segment_start; seg < map.segment_end && seg < NUM_BODY_SEGMENTS; ++seg) {
            body.set_muscle_activation(seg, map.is_dorsal, release);
        }
    }

    // Second pass: inhibitory D-class neurons reduce contralateral activation
    // DD inhibits ventral, VD inhibits dorsal
    for (auto& map : mappings_) {
        if (!map.is_inhibitory) continue;
        if (map.neuron_id < 0 || map.neuron_id >= n_size) continue;

        double release = neurons[map.neuron_id]->get_transmitter_release_rate();
        double inhibition = release * 0.8; // GABA inhibition strength

        for (int seg = map.segment_start; seg < map.segment_end && seg < NUM_BODY_SEGMENTS; ++seg) {
            auto& s = body.segments()[seg];
            double current = map.is_dorsal ?
                s.dorsal_activation : s.ventral_activation;
            double reduced = std::max(0.0, current - inhibition);
            // Use direct access since we need to reduce
            body.set_muscle_activation_direct(seg, map.is_dorsal, reduced);
        }
    }
}

} // namespace celegans
