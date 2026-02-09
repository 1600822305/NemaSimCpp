#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace celegans {

class ConnectomeLoader {
public:
    // Load neuron list from CSV: id, name, type, neurotransmitter
    static std::vector<NeuronInfo> load_neurons(const std::string& path);

    // Load chemical synapses from CSV: pre_name, post_name, sections, nt_type
    static std::vector<SynapseInfo> load_synapses(const std::string& path,
        const std::unordered_map<std::string, int>& name_to_id);

    // Load gap junctions from CSV: neuron_a_name, neuron_b_name, sections
    static std::vector<GapJunctionInfo> load_gap_junctions(const std::string& path,
        const std::unordered_map<std::string, int>& name_to_id);

    // Generate a minimal default connectome for testing (without CSV files)
    static void generate_default_connectome(
        std::vector<NeuronInfo>& neurons,
        std::vector<SynapseInfo>& synapses,
        std::vector<GapJunctionInfo>& gap_junctions);

private:
    static NeuronType parse_neuron_type(const std::string& s);
    static NeurotransmitterType parse_nt_type(const std::string& s);
    static std::vector<std::string> split_csv_line(const std::string& line);
    static std::string trim(const std::string& s);
};

} // namespace celegans
