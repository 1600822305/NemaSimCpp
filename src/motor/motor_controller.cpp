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
    // Step 39: expanded to 7 units, continuous coverage seg 4-40
    // Segments 0-5: head, 6-42: body, 43-47: tail
    add_mapping(name_to_id, "DB01", 4, 9, true);
    add_mapping(name_to_id, "DB02", 9, 14, true);
    add_mapping(name_to_id, "DB03", 14, 19, true);
    add_mapping(name_to_id, "DB04", 19, 24, true);
    add_mapping(name_to_id, "DB05", 24, 29, true);
    add_mapping(name_to_id, "DB06", 29, 35, true);
    add_mapping(name_to_id, "DB07", 35, 42, true);
    add_mapping(name_to_id, "VB01", 4, 9, false);
    add_mapping(name_to_id, "VB02", 9, 14, false);
    add_mapping(name_to_id, "VB03", 14, 19, false);
    add_mapping(name_to_id, "VB04", 19, 24, false);
    add_mapping(name_to_id, "VB05", 24, 29, false);
    add_mapping(name_to_id, "VB06", 29, 35, false);
    add_mapping(name_to_id, "VB07", 35, 42, false);

    // A-class (reverse): dorsal DA, ventral VA
    // Step 39: expanded to 5 units
    add_mapping(name_to_id, "DA01", 4, 12, true);
    add_mapping(name_to_id, "DA02", 12, 20, true);
    add_mapping(name_to_id, "DA03", 20, 28, true);
    add_mapping(name_to_id, "DA04", 28, 35, true);
    add_mapping(name_to_id, "DA05", 35, 42, true);
    add_mapping(name_to_id, "VA01", 4, 12, false);
    add_mapping(name_to_id, "VA02", 12, 20, false);
    add_mapping(name_to_id, "VA03", 20, 28, false);
    add_mapping(name_to_id, "VA04", 28, 35, false);
    add_mapping(name_to_id, "VA05", 35, 42, false);

    // D-class (cross-inhibition): GABAergic, inhibit contralateral muscles
    // DD: receives dorsal input, inhibits VENTRAL muscles
    // VD: receives ventral input, inhibits DORSAL muscles
    // Step 39: expanded to 5 units
    add_mapping(name_to_id, "DD01", 4, 12, false, true);  // DD inhibits ventral
    add_mapping(name_to_id, "DD02", 12, 20, false, true);
    add_mapping(name_to_id, "DD03", 20, 28, false, true);
    add_mapping(name_to_id, "DD04", 28, 35, false, true);
    add_mapping(name_to_id, "DD05", 35, 42, false, true);
    add_mapping(name_to_id, "VD01", 4, 12, true, true);   // VD inhibits dorsal
    add_mapping(name_to_id, "VD02", 12, 20, true, true);
    add_mapping(name_to_id, "VD03", 20, 28, true, true);
    add_mapping(name_to_id, "VD04", 28, 35, true, true);
    add_mapping(name_to_id, "VD05", 35, 42, true, true);

    // Head motor neurons: SMD controls head segments
    add_mapping(name_to_id, "SMDDL", 0, 4, true);
    add_mapping(name_to_id, "SMDDR", 0, 4, true);
    add_mapping(name_to_id, "SMDVL", 0, 4, false);
    add_mapping(name_to_id, "SMDVR", 0, 4, false);

    // Step 32/39: AS motor neurons — dorsal-only body wall projection
    // AS provides tonic dorsal bias; RIV must overcome this for omega turns
    // REF: White 1986 (anatomy), Haspel 2010 (dorsal projection)
    add_mapping(name_to_id, "AS01", 2, 6, true);    // head-neck transition
    add_mapping(name_to_id, "AS02", 6, 12, true);   // anterior body
    add_mapping(name_to_id, "AS03", 12, 18, true);  // mid-anterior
    add_mapping(name_to_id, "AS04", 18, 24, true);  // mid-body
    add_mapping(name_to_id, "AS05", 24, 30, true);  // mid-posterior
    add_mapping(name_to_id, "AS06", 30, 36, true);  // posterior
    add_mapping(name_to_id, "AS07", 36, 42, true);  // tail

    // Step 33: RME head motor neurons — GABAergic amplitude control
    // RMED innervates VENTRAL head muscles (contralateral! name="Dorsal" but projects ventral)
    // RMEV innervates DORSAL head muscles (contralateral! name="Ventral" but projects dorsal)
    // Push-pull with SMD: SMDD excites dorsal + RMED inhibits ventral → clean dorsal bend
    // RMEV inhibits dorsal → counteracts AS01 dorsal bias → restores D/V symmetry
    // REF: White 1986, Huang 2016 eLife, Jorgensen 2005 WormBook
    add_mapping(name_to_id, "RMED", 0, 4, false, true);  // RMED ⊣ ventral seg 0-4
    add_mapping(name_to_id, "RMEV", 0, 4, true, true);   // RMEV ⊣ dorsal seg 0-4

    // Step 31: RIV — omega turn driven via curvature_bias bypass (apply_riv_omega)
    // NOT mapped to motor controller: muscle_gain (0.3) too weak for omega curvature,
    // and even low RIV tonic activity (release ~0.1) creates persistent ventral bias
    // that disrupts SMD head oscillation. RIV drives omega through curvature_bias
    // (same bypass pattern as weathervane and klinotaxis).

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
