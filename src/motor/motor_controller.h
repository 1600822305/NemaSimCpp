#pragma once

#include "core/types.h"
#include "neuron/single_compartment.h"
#include "body/body_model.h"
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>

namespace celegans {

class MotorController {
public:
    MotorController() = default;

    // Register motor neuron name -> body segment mapping
    void initialize(const std::unordered_map<std::string, int>& name_to_id);

    // Map motor neuron outputs to muscle activations
    void update(const std::vector<std::unique_ptr<Neuron>>& neurons, BodyModel& body);

private:
    struct MotorMapping {
        int neuron_id;
        int segment_start;
        int segment_end;
        bool is_dorsal;
        bool is_inhibitory = false; // D-class: inhibit contralateral side
    };

    std::vector<MotorMapping> mappings_;

    void add_mapping(const std::unordered_map<std::string, int>& name_to_id,
                     const std::string& neuron_name,
                     int seg_start, int seg_end, bool dorsal,
                     bool inhibitory = false);
};

} // namespace celegans
