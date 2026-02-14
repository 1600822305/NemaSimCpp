#include "motor/motor_controller.h"
#include "core/logger.h"

namespace celegans {

void MotorController::add_mapping(const std::unordered_map<std::string, int>& name_to_id,
                                   const std::string& neuron_name,
                                   int seg_start, int seg_end, bool dorsal,
                                   bool inhibitory, double nmj_gain,
                                   bool use_boost) {
    auto it = name_to_id.find(neuron_name);
    if (it != name_to_id.end()) {
        mappings_.push_back({it->second, seg_start, seg_end, dorsal, inhibitory, nmj_gain, use_boost});
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

    // Head motor neurons: use boost channel (sum semantics)
    // Sum semantics preserves SMD antiphase D/V oscillation signal.
    // Max semantics would be masked by SAA/SIA/SIB symmetric baseline (~0.5).
    add_mapping(name_to_id, "SMDDL", 0, 4, true,  false, 1.0, true);
    add_mapping(name_to_id, "SMDDR", 0, 4, true,  false, 1.0, true);
    add_mapping(name_to_id, "SMDVL", 0, 4, false, false, 1.0, true);
    add_mapping(name_to_id, "SMDVR", 0, 4, false, false, 1.0, true);

    // Step 105: URA — inner labial motor neurons (nose positioning)
    add_mapping(name_to_id, "URADL", 0, 3, true,  false, 1.0, true);
    add_mapping(name_to_id, "URADR", 0, 3, true,  false, 1.0, true);
    add_mapping(name_to_id, "URAVL", 0, 3, false, false, 1.0, true);
    add_mapping(name_to_id, "URAVR", 0, 3, false, false, 1.0, true);
    // Step 103: SAA — sublateral with NMJs
    add_mapping(name_to_id, "SAADL", 0, 5, true,  false, 1.0, true);
    add_mapping(name_to_id, "SAADR", 0, 5, true,  false, 1.0, true);
    add_mapping(name_to_id, "SAAVL", 0, 5, false, false, 1.0, true);
    add_mapping(name_to_id, "SAAVR", 0, 5, false, false, 1.0, true);
    // Step 102: SIA — head motor neurons (sublateral)
    add_mapping(name_to_id, "SIADL", 0, 5, true,  false, 1.0, true);
    add_mapping(name_to_id, "SIADR", 0, 5, true,  false, 1.0, true);
    add_mapping(name_to_id, "SIAVL", 0, 5, false, false, 1.0, true);
    add_mapping(name_to_id, "SIAVR", 0, 5, false, false, 1.0, true);
    // Step 102: SIB — head motor neurons (sublateral)
    add_mapping(name_to_id, "SIBDL", 0, 5, true,  false, 1.0, true);
    add_mapping(name_to_id, "SIBDR", 0, 5, true,  false, 1.0, true);
    add_mapping(name_to_id, "SIBVL", 0, 5, false, false, 1.0, true);
    add_mapping(name_to_id, "SIBVR", 0, 5, false, false, 1.0, true);

    // Step 88: AS motor neurons expanded 7→11 (complete complement)
    // AS provides tonic dorsal bias; active during both fwd and bwd locomotion
    // ~3-4 segments each for finer dorsal control
    // REF: White 1986, Haspel 2010, Tolstenkov 2018 eLife
    add_mapping(name_to_id, "AS01", 4, 8, true);    // Step 93: 2→4 start, avoid head seg 0-3 (SMD domain → D/V symmetry)
    add_mapping(name_to_id, "AS02", 6, 10, true);   // anterior body
    add_mapping(name_to_id, "AS03", 10, 14, true);  // anterior-mid
    add_mapping(name_to_id, "AS04", 14, 18, true);  // mid-anterior
    add_mapping(name_to_id, "AS05", 18, 22, true);  // mid-body
    add_mapping(name_to_id, "AS06", 22, 26, true);  // mid-body
    add_mapping(name_to_id, "AS07", 26, 30, true);  // mid-posterior
    add_mapping(name_to_id, "AS08", 30, 33, true);  // posterior
    add_mapping(name_to_id, "AS09", 33, 36, true);  // posterior
    add_mapping(name_to_id, "AS10", 36, 39, true);  // near-tail
    add_mapping(name_to_id, "AS11", 39, 42, true);  // tail

    // Step 33: RME head motor neurons — GABAergic amplitude control (boost channel)
    add_mapping(name_to_id, "RMED", 0, 4, false, true, 1.0, true);  // RMED ⊣ ventral
    add_mapping(name_to_id, "RMEV", 0, 4, true, true, 1.0, true);   // RMEV ⊣ dorsal

    // Step 117: RIV — omega via muscles.add_boost() in apply_riv_omega
    // Step 117: SMB — klinotaxis via muscles.add_boost() in apply_smb_neck_bias
    // Both use additive boost channel (sum semantics) to stack on normal MN drive,
    // avoiding max semantics competition with SAA/SIA/SIB baseline.

    LOG_INFO("Motor controller initialized with ", mappings_.size(), " mappings");
}

void MotorController::update(const std::vector<std::unique_ptr<Neuron>>& neurons, BodyModel& body) {
    // NOTE: muscles.reset_inputs() is called in SimulationEngine::step()
    // BEFORE motor_controller::update(). This allows apply_riv_omega/apply_smb_neck_bias
    // to add boost inputs AFTER motor_controller without being cleared.
    auto& muscles = body.muscles();

    int n_size = static_cast<int>(neurons.size());

    // Single pass: excitatory neurons add cholinergic input, inhibitory add GABAergic
    // Multiple neurons innervating the same muscle SUM their contributions
    // (replaces old max-semantics + two-pass subtract)
    for (auto& map : mappings_) {
        if (map.neuron_id < 0 || map.neuron_id >= n_size) continue;

        double release = neurons[map.neuron_id]->get_transmitter_release_rate();

        double weighted = release * map.nmj_gain;

        if (map.is_inhibitory) {
            double inhibition = weighted * 0.8; // GABA inhibition strength
            for (int seg = map.segment_start; seg < map.segment_end && seg < NUM_BODY_SEGMENTS; ++seg) {
                if (map.use_boost)
                    muscles.add_boost(seg, map.is_dorsal, -inhibition); // negative boost
                else
                    muscles.add_inhibitory(seg, map.is_dorsal, inhibition);
            }
        } else {
            for (int seg = map.segment_start; seg < map.segment_end && seg < NUM_BODY_SEGMENTS; ++seg) {
                if (map.use_boost)
                    muscles.add_boost(seg, map.is_dorsal, weighted);
                else
                    muscles.add_excitatory(seg, map.is_dorsal, weighted);
            }
        }
    }
}

} // namespace celegans
