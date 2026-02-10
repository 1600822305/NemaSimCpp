#include "connectome/connectome_loader.h"
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

    neurons.clear();
    synapses.clear();
    gap_junctions.clear();

    // Representative subset of key C. elegans neurons for MVP testing
    // Sensory neurons
    struct NeuronDef { const char* name; NeuronType type; NeurotransmitterType nt; };
    std::vector<NeuronDef> defs = {
        // Sensory
        {"ASEL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"ASER", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"AWCL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"AWCR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"AWAL", NeuronType::SENSORY, NeurotransmitterType::ACETYLCHOLINE},
        {"AWAR", NeuronType::SENSORY, NeurotransmitterType::ACETYLCHOLINE},
        {"ASHL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"ASHR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"ALML", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"ALMR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"PLML", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"PLMR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        // Key interneurons
        {"AIAL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AIAR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AIBL", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AIBR", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AIYL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AIYR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AIZL", NeuronType::INTER, NeurotransmitterType::UNKNOWN},
        {"AIZR", NeuronType::INTER, NeurotransmitterType::UNKNOWN},
        {"RIAL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"RIAR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"RIBL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"RIBR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        // Command interneurons
        {"AVAL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVAR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVBL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVBR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVDL", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AVDR", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AVEL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVER", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        // Head motor neurons
        {"SMDVL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMDVR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMDDL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMDDR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDVL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDVR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDDL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDDR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // Ventral cord motor neurons (representative subset)
        {"DA01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DA02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DA03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DD01", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"DD02", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"DD03", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD01", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD02", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD03", NeuronType::MOTOR, NeurotransmitterType::GABA},
    };

    for (size_t i = 0; i < defs.size(); ++i) {
        NeuronInfo info;
        info.id = static_cast<int>(i);
        info.name = defs[i].name;
        info.type = defs[i].type;
        info.neurotransmitter = defs[i].nt;
        neurons.push_back(info);
    }

    std::unordered_map<std::string, int> name_to_id;
    for (auto& n : neurons) name_to_id[n.name] = n.id;

    auto add_syn = [&](const char* pre, const char* post, int sections) {
        auto pre_it = name_to_id.find(pre);
        auto post_it = name_to_id.find(post);
        if (pre_it != name_to_id.end() && post_it != name_to_id.end()) {
            SynapseInfo s;
            s.pre_neuron_id = pre_it->second;
            s.post_neuron_id = post_it->second;
            s.num_sections = sections;
            s.neurotransmitter = neurons[pre_it->second].neurotransmitter;
            synapses.push_back(s);
        }
    };

    auto add_gj = [&](const char* a, const char* b, int sections) {
        auto a_it = name_to_id.find(a);
        auto b_it = name_to_id.find(b);
        if (a_it != name_to_id.end() && b_it != name_to_id.end()) {
            GapJunctionInfo gj;
            gj.neuron_a_id = a_it->second;
            gj.neuron_b_id = b_it->second;
            gj.num_sections = sections;
            gap_junctions.push_back(gj);
        }
    };

    // Key synaptic connections (chemotaxis circuit)
    // Sensory → Interneuron
    add_syn("ASEL", "AIAL", 5); add_syn("ASEL", "AIYL", 3);
    add_syn("ASER", "AIAR", 5); add_syn("ASER", "AIYR", 3);
    add_syn("AWCL", "AIBL", 4); add_syn("AWCL", "AIYL", 6);
    add_syn("AWCR", "AIBR", 4); add_syn("AWCR", "AIYR", 6);
    add_syn("AWAL", "AIAL", 3); add_syn("AWAR", "AIAR", 3);
    // Touch circuit (Chalfie et al. 1985)
    // Principle: touch cells form gap junctions with AGONIST interneurons,
    //           chemical (inhibitory) synapses with ANTAGONIST interneurons.
    // Anterior touch: ALM → AVD (agonist=backward, gap junction excitatory)
    //                 ALM → AVB (antagonist=forward, inhibitory chemical)
    // Posterior touch: PLM → AVA/AVD (antagonist=backward, inhibitory chemical)
    //                  PLM → PVC (agonist=forward) — PVC not in MVP subset
    // NOTE: ALM→AVD gap junctions moved to gap junction section below
    // NOTE: PLM→AVA inhibitory and ALM→AVB inhibitory moved after add_syn_inh
    // AVD → AVA excitatory relay (signal from touch → backward command)
    // Weak: only effective when AVD is strongly activated by touch (not tonic)
    add_syn("AVDL", "AVAL", 1); add_syn("AVDR", "AVAR", 1);
    // Interneuron → Command interneuron
    // AIA ⊣ AIB: inhibitory (suppresses pirouettes when ON chemosensory active)
    // REF: Chalasani 2007 — AIA inhibits AIB via inhibitory ACh receptors
    // NOTE: uses add_syn_inh defined below for inhibitory synapse
    // (moved to after add_syn_inh lambda definition)
    add_syn("AIBL", "AVAL", 3); add_syn("AIBR", "AVAR", 3);
    add_syn("AIYL", "RIAR", 4); add_syn("AIYR", "RIAL", 4);
    add_syn("AIYL", "AIZL", 3); add_syn("AIYR", "AIZR", 3);
    // AIY → AVB: promotes forward locomotion
    // REF: Gray 2005 — AIY ablation reduces forward runs
    add_syn("AIYL", "AVBL", 3); add_syn("AIYR", "AVBR", 3);
    // Command → Motor
    add_syn("AVAL", "DA01", 5); add_syn("AVAL", "DA02", 4); add_syn("AVAL", "DA03", 3);
    add_syn("AVAL", "VA01", 4); add_syn("AVAL", "VA02", 3); add_syn("AVAL", "VA03", 3);
    add_syn("AVAR", "DA01", 5); add_syn("AVAR", "DA02", 4); add_syn("AVAR", "DA03", 3);
    add_syn("AVBL", "DB01", 5); add_syn("AVBL", "DB02", 4); add_syn("AVBL", "DB03", 3);
    add_syn("AVBL", "VB01", 4); add_syn("AVBL", "VB02", 3); add_syn("AVBL", "VB03", 3);
    add_syn("AVBR", "DB01", 5); add_syn("AVBR", "DB02", 4); add_syn("AVBR", "DB03", 3);
    // D-type cross inhibition
    add_syn("DD01", "VD01", 3); add_syn("DD02", "VD02", 3); add_syn("DD03", "VD03", 3);
    add_syn("VD01", "DD01", 3); add_syn("VD02", "DD02", 3); add_syn("VD03", "DD03", 3);
    // Head motor → SMD/RMD circuits
    add_syn("RIAL", "SMDVL", 4); add_syn("RIAR", "SMDVR", 4);
    add_syn("RIAL", "SMDDL", 3); add_syn("RIAR", "SMDDR", 3);
    // Nose touch → reverse
    add_syn("ASHL", "AVAL", 4); add_syn("ASHR", "AVAR", 4);
    add_syn("ASHL", "AVDL", 3); add_syn("ASHR", "AVDR", 3);

    // Head oscillator: dorsal-ventral cross-inhibition (TD-02)
    // SMD dorsal → RMD ventral and vice versa (reciprocal inhibition circuit)
    // In C. elegans some ACh synapses act inhibitory via ACC chloride channels
    // REF: Pereira 2015 (ACh receptor diversity), Hendricks 2012
    // These use GABA-like reversal to model functional inhibition
    auto add_syn_inh = [&](const char* pre, const char* post, int sections) {
        auto pre_it = name_to_id.find(pre);
        auto post_it = name_to_id.find(post);
        if (pre_it != name_to_id.end() && post_it != name_to_id.end()) {
            SynapseInfo s;
            s.pre_neuron_id = pre_it->second;
            s.post_neuron_id = post_it->second;
            s.num_sections = sections;
            s.neurotransmitter = NeurotransmitterType::GABA; // functional inhibition
            synapses.push_back(s);
        }
    };
    // AIA ⊣ AIB: inhibitory — suppresses pirouettes when ON chemosensory active
    // REF: Chalasani 2007 — AIA inhibits AIB, critical for pirouette suppression
    add_syn_inh("AIAL", "AIBL", 5); add_syn_inh("AIAR", "AIBR", 5);
    // Touch circuit inhibitory connections (Chalfie 1985)
    // ALM ⊣ AVB: anterior touch inhibits forward (antagonist)
    add_syn_inh("ALML", "AVBL", 3); add_syn_inh("ALMR", "AVBR", 3);
    // PLM ⊣ AVA/AVD: posterior touch inhibits backward (antagonist)
    add_syn_inh("PLML", "AVAL", 3); add_syn_inh("PLMR", "AVAR", 3);
    add_syn_inh("PLML", "AVDL", 2); add_syn_inh("PLMR", "AVDR", 2);

    // Dorsal SMD inhibits ventral SMD (and vice versa) → half-center oscillator
    // Strong inhibition needed: must overcome tonic drive to suppress contralateral side
    add_syn_inh("SMDDL", "SMDVL", 8); add_syn_inh("SMDDR", "SMDVR", 8);
    add_syn_inh("SMDVL", "SMDDL", 8); add_syn_inh("SMDVR", "SMDDR", 8);
    // RMD dorsal-ventral cross-inhibition
    add_syn_inh("RMDDL", "RMDVL", 6); add_syn_inh("RMDDR", "RMDVR", 6);
    add_syn_inh("RMDVL", "RMDDL", 6); add_syn_inh("RMDVR", "RMDDR", 6);
    // SMD → RMD excitatory (same side, co-activate dorsal or ventral)
    add_syn("SMDDL", "RMDDL", 3); add_syn("SMDDR", "RMDDR", 3);
    add_syn("SMDVL", "RMDVL", 3); add_syn("SMDVR", "RMDVR", 3);

    // AVB drive pathway: AIY → AVB (TD-01, excitatory interneuron drive)
    // REF: White 1986, WormAtlas
    add_syn("AIYL", "AVBL", 3); add_syn("AIYR", "AVBR", 3);
    // RIB → AVB (additional forward drive)
    add_syn("RIBL", "AVBL", 2); add_syn("RIBR", "AVBR", 2);

    // Key gap junctions
    add_gj("AVAL", "AVAR", 10);  // left-right coupling of command interneurons
    add_gj("AVBL", "AVBR", 12);
    add_gj("AVDL", "AVDR", 5);
    add_gj("AVEL", "AVER", 4);
    add_gj("ASEL", "ASER", 2);
    add_gj("AIBL", "AIBR", 3);
    // Touch circuit gap junctions (Chalfie 1985: touch cells → agonist interneurons)
    // ALM → AVD: anterior touch excites backward interneuron
    add_gj("ALML", "AVDL", 4); add_gj("ALMR", "AVDR", 4);

    LOG_INFO("Generated default connectome: ", neurons.size(), " neurons, ",
             synapses.size(), " synapses, ", gap_junctions.size(), " gap junctions");
}

} // namespace celegans
