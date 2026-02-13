#include "connectome/connectome_loader.h"
#include "connectome/connectome_builder.h"
#include "core/logger.h"
#include <algorithm>

namespace celegans {

std::string ConnectomeLoader::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n\"");
    size_t end = s.find_last_not_of(" \t\r\n\"");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

std::vector<std::string> ConnectomeLoader::split_csv_line(const std::string& line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        tokens.push_back(trim(token));
    }
    return tokens;
}

NeuronType ConnectomeLoader::parse_neuron_type(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "sensory" || lower == "s") return NeuronType::SENSORY;
    if (lower == "inter" || lower == "i" || lower == "interneuron") return NeuronType::INTER;
    if (lower == "motor" || lower == "m") return NeuronType::MOTOR;
    if (lower == "pharyngeal" || lower == "p") return NeuronType::PHARYNGEAL;
    return NeuronType::UNKNOWN;
}

NeurotransmitterType ConnectomeLoader::parse_nt_type(const std::string& s) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "ach" || lower == "acetylcholine") return NeurotransmitterType::ACETYLCHOLINE;
    if (lower == "gaba") return NeurotransmitterType::GABA;
    if (lower == "glu" || lower == "glutamate") return NeurotransmitterType::GLUTAMATE;
    if (lower == "glucl" || lower == "glu_inh" || lower == "glutamate_inhibitory") return NeurotransmitterType::GLUTAMATE_INHIBITORY;
    if (lower == "da" || lower == "dopamine") return NeurotransmitterType::DOPAMINE;
    if (lower == "5ht" || lower == "serotonin" || lower == "5-ht") return NeurotransmitterType::SEROTONIN;
    if (lower == "ta" || lower == "tyramine") return NeurotransmitterType::TYRAMINE;
    if (lower == "oct" || lower == "octopamine") return NeurotransmitterType::OCTOPAMINE;
    return NeurotransmitterType::UNKNOWN;
}

std::vector<NeuronInfo> ConnectomeLoader::load_neurons(const std::string& path) {
    std::vector<NeuronInfo> neurons;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open neuron file: ", path);
        return neurons;
    }

    std::string line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto tokens = split_csv_line(line);
        if (tokens.size() < 3) continue;

        NeuronInfo info;
        info.id = static_cast<int>(neurons.size());
        info.name = tokens[0];
        info.type = parse_neuron_type(tokens[1]);
        if (tokens.size() > 2) info.neurotransmitter = parse_nt_type(tokens[2]);
        neurons.push_back(info);
    }

    LOG_INFO("Loaded ", neurons.size(), " neurons from ", path);
    return neurons;
}

std::vector<SynapseInfo> ConnectomeLoader::load_synapses(const std::string& path,
    const std::unordered_map<std::string, int>& name_to_id) {
    std::vector<SynapseInfo> synapses;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open synapse file: ", path);
        return synapses;
    }

    std::string line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto tokens = split_csv_line(line);
        if (tokens.size() < 3) continue;

        auto pre_it = name_to_id.find(tokens[0]);
        auto post_it = name_to_id.find(tokens[1]);
        if (pre_it == name_to_id.end() || post_it == name_to_id.end()) continue;

        SynapseInfo syn;
        syn.pre_neuron_id = pre_it->second;
        syn.post_neuron_id = post_it->second;
        syn.num_sections = std::stoi(tokens[2]);
        if (tokens.size() > 3) syn.neurotransmitter = parse_nt_type(tokens[3]);
        synapses.push_back(syn);
    }

    LOG_INFO("Loaded ", synapses.size(), " chemical synapses from ", path);
    return synapses;
}

std::vector<GapJunctionInfo> ConnectomeLoader::load_gap_junctions(const std::string& path,
    const std::unordered_map<std::string, int>& name_to_id) {
    std::vector<GapJunctionInfo> gjs;
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("Cannot open gap junction file: ", path);
        return gjs;
    }

    std::string line;
    std::getline(file, line); // skip header

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto tokens = split_csv_line(line);
        if (tokens.size() < 3) continue;

        auto a_it = name_to_id.find(tokens[0]);
        auto b_it = name_to_id.find(tokens[1]);
        if (a_it == name_to_id.end() || b_it == name_to_id.end()) continue;

        GapJunctionInfo gj;
        gj.neuron_a_id = a_it->second;
        gj.neuron_b_id = b_it->second;
        gj.num_sections = std::stoi(tokens[2]);
        gjs.push_back(gj);
    }

    LOG_INFO("Loaded ", gjs.size(), " gap junctions from ", path);
    return gjs;
}

void ConnectomeLoader::generate_default_connectome(
    std::vector<NeuronInfo>& neurons,
    std::vector<SynapseInfo>& synapses,
    std::vector<GapJunctionInfo>& gap_junctions) {
    build_default_connectome(neurons, synapses, gap_junctions);
}

} // namespace celegans
