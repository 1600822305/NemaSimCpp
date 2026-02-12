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
    // Step 39: DB 7 units, continuous coverage seg 4-42
    // Segments 0-5: head, 6-42: body, 43-47: tail
    add_mapping(name_to_id, "DB01", 4, 9, true);
    add_mapping(name_to_id, "DB02", 9, 14, true);
    add_mapping(name_to_id, "DB03", 14, 19, true);
    add_mapping(name_to_id, "DB04", 19, 24, true);
    add_mapping(name_to_id, "DB05", 24, 29, true);
    add_mapping(name_to_id, "DB06", 29, 35, true);
    add_mapping(name_to_id, "DB07", 35, 42, true);
    // Step 87: VB expanded 7→11 (complete complement)
    // ~3-4 segments each for finer ventral forward control
    // REF: Wen 2012 Neuron, White 1986
    add_mapping(name_to_id, "VB01", 4, 8, false);
    add_mapping(name_to_id, "VB02", 8, 11, false);
    add_mapping(name_to_id, "VB03", 11, 15, false);
    add_mapping(name_to_id, "VB04", 15, 18, false);
    add_mapping(name_to_id, "VB05", 18, 22, false);
    add_mapping(name_to_id, "VB06", 22, 25, false);
    add_mapping(name_to_id, "VB07", 25, 29, false);
    add_mapping(name_to_id, "VB08", 29, 32, false);
    add_mapping(name_to_id, "VB09", 32, 36, false);
    add_mapping(name_to_id, "VB10", 36, 39, false);
    add_mapping(name_to_id, "VB11", 39, 42, false);

    // A-class (reverse): dorsal DA, ventral VA
    // Step 84: expanded DA 5→9, VA 5→12 (Haspel 2011, Gao 2018 eLife)
    // DA: 9 dorsal A-class, each ~4 segments
    add_mapping(name_to_id, "DA01", 4, 8, true);
    add_mapping(name_to_id, "DA02", 8, 12, true);
    add_mapping(name_to_id, "DA03", 12, 16, true);
    add_mapping(name_to_id, "DA04", 16, 20, true);
    add_mapping(name_to_id, "DA05", 20, 25, true);
    add_mapping(name_to_id, "DA06", 25, 29, true);
    add_mapping(name_to_id, "DA07", 29, 33, true);
    add_mapping(name_to_id, "DA08", 33, 38, true);
    add_mapping(name_to_id, "DA09", 38, 42, true);
    // VA: 12 ventral A-class, each ~3 segments
    add_mapping(name_to_id, "VA01", 4, 7, false);
    add_mapping(name_to_id, "VA02", 7, 10, false);
    add_mapping(name_to_id, "VA03", 10, 13, false);
    add_mapping(name_to_id, "VA04", 13, 16, false);
    add_mapping(name_to_id, "VA05", 16, 19, false);
    add_mapping(name_to_id, "VA06", 19, 22, false);
    add_mapping(name_to_id, "VA07", 22, 25, false);
    add_mapping(name_to_id, "VA08", 25, 29, false);
    add_mapping(name_to_id, "VA09", 29, 32, false);
    add_mapping(name_to_id, "VA10", 32, 35, false);
    add_mapping(name_to_id, "VA11", 35, 39, false);
    add_mapping(name_to_id, "VA12", 39, 42, false);

    // D-class (cross-inhibition): GABAergic, inhibit contralateral muscles
    // DD: receives dorsal input, inhibits VENTRAL muscles
    // VD: receives ventral input, inhibits DORSAL muscles
    // Step 86: expanded DD 5→6, VD 5→13 (complete complement)
    // DD: 6 units, each ~6-7 segments
    add_mapping(name_to_id, "DD01", 4, 11, false, true);  // DD inhibits ventral
    add_mapping(name_to_id, "DD02", 11, 17, false, true);
    add_mapping(name_to_id, "DD03", 17, 24, false, true);
    add_mapping(name_to_id, "DD04", 24, 30, false, true);
    add_mapping(name_to_id, "DD05", 30, 36, false, true);
    add_mapping(name_to_id, "DD06", 36, 42, false, true);
    // VD: 13 units, each ~3 segments
    add_mapping(name_to_id, "VD01", 4, 7, true, true);    // VD inhibits dorsal
    add_mapping(name_to_id, "VD02", 7, 10, true, true);
    add_mapping(name_to_id, "VD03", 10, 13, true, true);
    add_mapping(name_to_id, "VD04", 13, 16, true, true);
    add_mapping(name_to_id, "VD05", 16, 19, true, true);
    add_mapping(name_to_id, "VD06", 19, 22, true, true);
    add_mapping(name_to_id, "VD07", 22, 25, true, true);
    add_mapping(name_to_id, "VD08", 25, 28, true, true);
    add_mapping(name_to_id, "VD09", 28, 31, true, true);
    add_mapping(name_to_id, "VD10", 31, 34, true, true);
    add_mapping(name_to_id, "VD11", 34, 37, true, true);
    add_mapping(name_to_id, "VD12", 37, 40, true, true);
    add_mapping(name_to_id, "VD13", 40, 42, true, true);

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

    // Step 31: RIV — omega turn driven via curvature_bias (apply_riv_omega)
    // NOT mapped to motor controller: muscle_gain (0.3) too weak for omega curvature,
    // and even low RIV tonic activity (release ~0.1) creates persistent ventral bias
    // that disrupts SMD head oscillation. RIV drives omega through curvature_bias.
    // Step 65: weathervane curvature_bias bypass removed; only RIV omega uses it now.

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
