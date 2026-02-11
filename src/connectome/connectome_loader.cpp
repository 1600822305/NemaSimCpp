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
        // Neuromodulatory sensory neurons (Step 20, Layer 6)
        // NSM: pharyngeal neuron, detects food → releases 5-HT → dwelling
        // REF: Flavell 2013 Cell — NSM drives dwelling via serotonin
        {"NSML", NeuronType::SENSORY, NeurotransmitterType::SEROTONIN},
        {"NSMR", NeuronType::SENSORY, NeurotransmitterType::SEROTONIN},
        // CEP: head mechanosensory, detects bacteria → releases DA → basal slowing
        // REF: Sawin 2000 — dopamine basal slowing response
        {"CEPDL", NeuronType::SENSORY, NeurotransmitterType::DOPAMINE},
        {"CEPDR", NeuronType::SENSORY, NeurotransmitterType::DOPAMINE},
        {"CEPVL", NeuronType::SENSORY, NeurotransmitterType::DOPAMINE},
        {"CEPVR", NeuronType::SENSORY, NeurotransmitterType::DOPAMINE},
        // AFD: thermosensory neuron — senses temperature, drives thermotaxis
        // REF: Mori & Ohshima 1995, Luo 2014 PNAS — AFD→AIY core thermotaxis circuit
        {"AFDL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"AFDR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        // ADF: chemosensory neuron, serotonin source for learned pathogen avoidance
        // REF: Zhang 2005 Nature — PA14 exposure → ADF TPH-1 ↑ → 5-HT ↑ → MOD-1 on AIY/AIZ
        //      Ha 2010 Neuron — ADF essential for aversive olfactory learning
        {"ADFL", NeuronType::SENSORY, NeurotransmitterType::SEROTONIN},
        {"ADFR", NeuronType::SENSORY, NeurotransmitterType::SEROTONIN},
        // Step 33: OLQ nose touch mechanosensory neurons (labial cilia)
        // 4 quadrant neurons: sense close-range obstacles (dist < 0.3mm)
        // OLQ mediates head withdrawal reflex via RMD (Hart 1995)
        // Only 5% of nose touch avoidance (ASH=45%, FLP=29%) — subtle, exploratory
        // REF: Kaplan & Horvitz 1993, Hart 1995, White 1986
        {"OLQDL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"OLQDR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"OLQVL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"OLQVR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        // Step 34: O₂ sensing neurons
        // URX: primary O₂ sensor, gcy-35/gcy-36 soluble guanylate cyclase
        // Activated by HIGH O₂ (>14%), drives hyperoxia avoidance
        // NPR-1 215V (N2) tonically inhibits URX → mild O₂ response on food
        // REF: Gray 2004 Nature, Cheung 2005 Neuron, Chang 2006 PLoS Biology
        {"URXL", NeuronType::SENSORY, NeurotransmitterType::ACETYLCHOLINE},
        {"URXR", NeuronType::SENSORY, NeurotransmitterType::ACETYLCHOLINE},
        // AQR: anterior body cavity O₂ sensor (single, unpaired)
        // Exposed to pseudocoelomic fluid, expresses gcy-35
        // REF: Chang 2006 — AQR+PQR+URX form distributed O₂ circuit
        {"AQR",  NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        // PQR: posterior body cavity O₂ sensor (single, unpaired)
        // Tail position → high O₂ at tail → accelerate forward (Busch 2012)
        {"PQR",  NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        // Step 35: BAG — CO₂ sensor (gcy-9 receptor guanylate cyclase)
        // Activated by CO₂ > 0.5%, drives CO₂ avoidance (turning + speed change)
        // N2: NPR-1 suppresses URX → URX doesn't inhibit CO₂ circuit → avoids CO₂
        // Phasic response: sensitive to CO₂ changes, OFF rebound on CO₂ decrease
        // REF: Hallem & Sternberg 2008 PNAS, Bretscher 2011 Neuron, Carrillo 2013
        {"BAGL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"BAGR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        // Step 36: PVD — harsh touch + proprioception (multi-dendritic)
        // Dendrites tile entire body wall; dual-mode: harsh touch + body bend sensing
        // Glutamatergic (GLR-1 mediated harsh touch response, Hart 1995)
        // REF: Way & Chalfie 1989, Albeg 2011, Tao 2019
        {"PVDL", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
        {"PVDR", NeuronType::SENSORY, NeurotransmitterType::GLUTAMATE},
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
        // Step 34: AUA — O₂ signal relay/integration interneuron
        // Receives from URX (O₂) + ADF (5-HT) → outputs to AVA/AVB
        // Key integration point: O₂ and serotonin converge here
        // REF: Chang 2006 PLoS Biology, WormWiring (Cook 2019)
        {"AUAL", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AUAR", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        // Step 36: DVA — whole-body proprioceptive interneuron (single, unpaired)
        // Axon spans entire body; TRP-4 TRPN stretch receptor channel
        // Senses body curvature → modulates motor neuron gain
        // trp-4 mutant: abnormal body bending (Li 2006 Nature)
        // REF: Li 2006 Nature, Hu 2011, Yeon 2018 PLoS Biology
        {"DVA",  NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        // Command interneurons
        {"AVAL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVAR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVBL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVBR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVDL", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AVDR", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"AVEL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"AVER", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        // RIM: reversal-active interneuron, stabilizes forward/reverse states
        // REF: Ouellette 2022 eLife — RIM gap junctions create behavioral inertia
        {"RIML", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        {"RIMR", NeuronType::INTER, NeurotransmitterType::GLUTAMATE},
        // RIC: octopamine/tyramine source — promotes roaming when off food
        // REF: Alkema 2005 — RIC produces OA, antagonizes 5-HT dwelling
        {"RICL", NeuronType::INTER, NeurotransmitterType::OCTOPAMINE},
        {"RICR", NeuronType::INTER, NeurotransmitterType::OCTOPAMINE},
        // Head motor neurons
        {"SMDVL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMDVR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMDDL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMDDR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDVL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDVR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDDL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"RMDDR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // Neck motor neurons — klinotaxis effectors (Izquierdo 2015, Yamazaki 2022)
        // SMB controls neck curvature DC bias, independent of SMD head oscillation
        {"SMBDL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMBDR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMBVL", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"SMBVR", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // Ventral cord motor neurons (Step 39: expanded from 3→5-7 per class)
        // REF: White 1986, Haspel 2010 (body segment mapping)
        // DA: dorsal A-class, backward locomotion (real: DA1-9, we use DA1-5)
        {"DA01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DA02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DA03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DA04", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DA05", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // DB: dorsal B-class, forward locomotion (real: DB1-7, all 7)
        {"DB01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB04", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB05", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB06", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"DB07", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // VA: ventral A-class, backward locomotion (real: VA1-12, we use VA1-5)
        {"VA01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA04", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VA05", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // VB: ventral B-class, forward locomotion (real: VB1-11, we use VB1-7)
        {"VB01", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB02", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB03", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB04", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB05", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB06", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VB07", NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // DD: dorsal D-class, GABAergic cross-inhibition (real: DD1-6, we use DD1-5)
        {"DD01", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"DD02", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"DD03", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"DD04", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"DD05", NeuronType::MOTOR, NeurotransmitterType::GABA},
        // VD: ventral D-class, GABAergic cross-inhibition (real: VD1-13, we use VD1-5)
        {"VD01", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD02", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD03", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD04", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"VD05", NeuronType::MOTOR, NeurotransmitterType::GABA},
        // Step 31: RIV — omega turn motor neurons (GABAergic, ventral head bend)
        // RIV innervates ventral neck muscles; specifies ventral bias of omega turns
        // REF: Gray 2005 PNAS — RIV ablation reduces omega frequency
        //      Donnelly 2013 — RIV triggers omega via ventral head bend
        {"RIVL", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"RIVR", NeuronType::MOTOR, NeurotransmitterType::GABA},
        // Step 32: AS motor neurons — dorsal-only body wall projections
        // AS receives both AVA and AVB → always active → tonic dorsal bias
        // Breaks dorsal-ventral symmetry; provides background against which
        // RIV must compete → graded omega turns emerge from RIV-AS force balance
        // REF: White 1986 (anatomy), Haspel 2010 (dorsal projection),
        //      Chen 2006 (active during both forward and backward)
        {"AS01", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"AS02", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"AS03", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"AS04", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"AS05", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"AS06", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"AS07", NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        // Step 33: RME head motor neurons — GABAergic amplitude control
        // RMED/RMEV modulate head bending amplitude via push-pull with SMD
        // RMED innervates VENTRAL head muscles (contralateral!)
        // RMEV innervates DORSAL head muscles (contralateral!)
        // RMEL/RMER omitted: no effect on D/V bending (Huang 2016 eLife)
        // REF: White 1986, Huang 2016 eLife, Jorgensen 2005 WormBook
        {"RMED", NeuronType::MOTOR, NeurotransmitterType::GABA},
        {"RMEV", NeuronType::MOTOR, NeurotransmitterType::GABA},
        // Step 24: Pharyngeal nervous system (independent CPG)
        // 20 neurons total, 14 types; we implement the 5 essential types (9 neurons)
        // REF: Albertson & Thomson 1976, Avery (WormBook 2012)
        // MC: excitatory motor neuron, ACh pacemaker → controls pump rate
        // REF: Raizen & Avery 1994 — MC necessary and sufficient for rapid pumping
        //      Song & Avery 2012 eLife — 5-HT activates MC via SER-7
        {"MCL",  NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"MCR",  NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // M3: inhibitory motor neuron, Glu → controls relaxation timing
        // REF: Avery 1993 — M3 proprioceptive loop, triggers repolarization
        //      Dent et al — M3 uses glutamate via AVR-15 Cl⁻ channel
        {"M3L",  NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        {"M3R",  NeuronType::MOTOR, NeurotransmitterType::GLUTAMATE},
        // M4: motor neuron, controls isthmus peristalsis (food transport)
        // REF: Avery & Horvitz 1987 — M4 essential for growth
        {"M4",   NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        // I1: pharyngeal interneuron, receives RIP gap junction (somatic↔pharyngeal bridge)
        // REF: Albertson & Thomson 1976 — I1 connects via RIP to extrapharyngeal NS
        {"I1L",  NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"I1R",  NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        // RIP: extrapharyngeal neuron, sole bridge to pharyngeal NS via gap junction to I1
        // REF: Albertson & Thomson 1976 — bilateral pair, gap junction to I1
        {"RIPL", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        {"RIPR", NeuronType::INTER, NeurotransmitterType::ACETYLCHOLINE},
        // Step 27: RIS — sleep-active neuron, single (unpaired) GABAergic + peptidergic
        // REF: Turek 2016 eLife — RIS releases FLP-11 (major sleep inducer, not GABA)
        //      Konietzka 2020 Nat Commun — RIS also functions as locomotion stop neuron
        //      Maluck 2023 PLOS Genetics — RIS promotes survival independently of sleep
        {"RIS",  NeuronType::INTER, NeurotransmitterType::GABA},
        // Step 38: Egg-laying circuit (Collins 2016 eLife, Schafer 2006)
        // HSN: serotonergic command motor neuron, drives vulval muscle contraction
        // Releases 5-HT + NLP-3 → initiates ~2min active egg-laying state
        // Tyramine feedback via LGC-55 terminates active state (uv1 cells)
        // REF: Waggoner 1998 Neuron, Brewer 2019 PLoS Genetics
        {"HSNL", NeuronType::MOTOR, NeurotransmitterType::SEROTONIN},
        {"HSNR", NeuronType::MOTOR, NeurotransmitterType::SEROTONIN},
        // VC4/VC5: cholinergic motor neurons, most proximal to vulva
        // Mechanically activated by vulval muscle contraction → positive feedback
        // REF: Collins 2016 eLife, 2021 J Neurosci — VC facilitates egg release
        {"VC4",  NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
        {"VC5",  NeuronType::MOTOR, NeurotransmitterType::ACETYLCHOLINE},
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

    auto add_syn = [&](const char* pre, const char* post, double sections) {
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

    // Step 28: compartment-targeted synapse (for multi-compartment neurons)
    auto add_syn_comp = [&](const char* pre, const char* post, double sections, int post_comp) {
        auto pre_it = name_to_id.find(pre);
        auto post_it = name_to_id.find(post);
        if (pre_it != name_to_id.end() && post_it != name_to_id.end()) {
            SynapseInfo s;
            s.pre_neuron_id = pre_it->second;
            s.post_neuron_id = post_it->second;
            s.num_sections = sections;
            s.neurotransmitter = neurons[pre_it->second].neurotransmitter;
            s.post_compartment = post_comp;
            synapses.push_back(s);
        }
    };

    auto add_gj = [&](const char* a, const char* b, double sections) {
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
    // ASER→AIA/AIY: INHIBITORY — moved to after add_syn_inh definition (Step 19)
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
    // Step 42: Cook 2019 weights: AVDL→AVAL=37, AVDR→AVAR=52
    // Conservative: 1→2 (full Cook scaling overdrives AVA tonic level)
    add_syn("AVDL", "AVAL", 2); add_syn("AVDR", "AVAR", 2);
    // Interneuron → Command interneuron
    // AIA ⊣ AIB: inhibitory (suppresses pirouettes when ON chemosensory active)
    // REF: Chalasani 2007 — AIA inhibits AIB via inhibitory ACh receptors
    // NOTE: uses add_syn_inh defined below for inhibitory synapse
    // (moved to after add_syn_inh lambda definition)
    // Step 42: Cook 2019 weights: AIBL→AVAL=5, AIBR→AVAR=2
    // Preserve L/R asymmetry but keep functional balance
    add_syn("AIBL", "AVAL", 2); add_syn("AIBR", "AVAR", 1);
    // Step 42: Cook 2019 weights: AIYL→RIAL=51, AIYR→RIAR=50 (scale ÷10)
    add_syn("AIYL", "RIAR", 5); add_syn("AIYR", "RIAL", 5);
    // Step 42: Cook 2019 weights: AIYL→AIZL=67, AIYR→AIZR=70
    // Keep at 3 for now (increasing destabilizes downstream TA/omega dynamics)
    add_syn("AIYL", "AIZL", 3); add_syn("AIYR", "AIZR", 3);
    // AIY → AVB: promotes forward locomotion
    // REF: Gray 2005 — AIY ablation reduces forward runs
    add_syn("AIYL", "AVBL", 3); add_syn("AIYR", "AVBR", 3);
    // Command → Motor (Step 39: expanded to full complement)
    // AVA → A-class (backward): anterior stronger, posterior weaker (gradient)
    add_syn("AVAL", "DA01", 5); add_syn("AVAL", "DA02", 4); add_syn("AVAL", "DA03", 3);
    add_syn("AVAL", "DA04", 2); add_syn("AVAL", "DA05", 2);
    add_syn("AVAL", "VA01", 4); add_syn("AVAL", "VA02", 3); add_syn("AVAL", "VA03", 3);
    add_syn("AVAL", "VA04", 2); add_syn("AVAL", "VA05", 2);
    add_syn("AVAR", "DA01", 5); add_syn("AVAR", "DA02", 4); add_syn("AVAR", "DA03", 3);
    add_syn("AVAR", "DA04", 2); add_syn("AVAR", "DA05", 2);
    // AVB → B-class (forward): anterior stronger, posterior weaker
    add_syn("AVBL", "DB01", 5); add_syn("AVBL", "DB02", 4); add_syn("AVBL", "DB03", 3);
    add_syn("AVBL", "DB04", 3); add_syn("AVBL", "DB05", 2); add_syn("AVBL", "DB06", 2);
    add_syn("AVBL", "DB07", 1);
    add_syn("AVBL", "VB01", 4); add_syn("AVBL", "VB02", 3); add_syn("AVBL", "VB03", 3);
    add_syn("AVBL", "VB04", 2); add_syn("AVBL", "VB05", 2); add_syn("AVBL", "VB06", 2);
    add_syn("AVBL", "VB07", 1);
    add_syn("AVBR", "DB01", 5); add_syn("AVBR", "DB02", 4); add_syn("AVBR", "DB03", 3);
    add_syn("AVBR", "DB04", 3); add_syn("AVBR", "DB05", 2); add_syn("AVBR", "DB06", 2);
    add_syn("AVBR", "DB07", 1);
    // D-type cross inhibition (Step 39: expanded to 5 pairs)
    add_syn("DD01", "VD01", 3); add_syn("DD02", "VD02", 3); add_syn("DD03", "VD03", 3);
    add_syn("DD04", "VD04", 3); add_syn("DD05", "VD05", 3);
    add_syn("VD01", "DD01", 3); add_syn("VD02", "DD02", 3); add_syn("VD03", "DD03", 3);
    add_syn("VD04", "DD04", 3); add_syn("VD05", "DD05", 3);
    // Head motor → SMD/RMD circuits
    add_syn("RIAL", "SMDVL", 4); add_syn("RIAR", "SMDVR", 4);
    add_syn("RIAL", "SMDDL", 3); add_syn("RIAR", "SMDDR", 3);

    // Step 28: SMD → RIA feedback (ACh via GAR-3 muscarinic receptor)
    // REF: Hendricks 2012 Nature — motor-correlated compartmentalized Ca²⁺
    // Dorsal SMD → RIA nrD (compartment 2): dorsal head bending feedback
    // Ventral SMD → RIA nrV (compartment 1): ventral head bending feedback
    // These are the KEY synapses enabling subcellular computation in RIA
    add_syn_comp("SMDDL", "RIAL", 1, 2);  // SMDDL → RIAL nrD (weak: minimize soma leakage)
    add_syn_comp("SMDDR", "RIAR", 1, 2);  // SMDDR → RIAR nrD
    add_syn_comp("SMDVL", "RIAL", 1, 1);  // SMDVL → RIAL nrV
    add_syn_comp("SMDVR", "RIAR", 1, 1);  // SMDVR → RIAR nrV
    // Nose touch / nociception → reverse
    // ASH→AVA: restored to 3 sections (was 4→2→3)
    // Original reduction: ASH sampled attractant, had tonic drive pushing AVA near threshold
    // Now ASH only samples repellent field (Step 25): silent at 3pA baseline without repellent
    // 3 sections provides strong avoidance drive when repellent is present
    add_syn("ASHL", "AVAL", 3); add_syn("ASHR", "AVAR", 3);
    add_syn("ASHL", "AVDL", 3); add_syn("ASHR", "AVDR", 3);
    // Step 25: ASH nociceptive avoidance circuit (Cook 2019, Summers 2015)
    // ASH→AIB: glutamatergic excitatory via GLR-1 (AMPA-like)
    // This is the KEY pathway for nociceptive decision-making at AIB hub
    add_syn("ASHL", "AIBL", 3); add_syn("ASHR", "AIBR", 3);
    // ASH→RIM: nociceptive activation of RIM (promotes omega turns)
    add_syn("ASHL", "RIML", 1); add_syn("ASHR", "RIMR", 1);

    // Step 26: ADF serotonergic modulation of learned avoidance (Zhang 2005 Nature)
    // ADF→AIY: serotonin via MOD-1 (Cl⁻) → inhibits approach pathway when sick
    // ADF→AIZ: serotonin via MOD-1 → inhibits AIZ (part of approach circuit)
    // NOTE: ADF 5-HT also acts via volume transmission (neuromodulation system)
    add_syn("ADFL", "AIYL", 2); add_syn("ADFR", "AIYR", 2);
    add_syn("ADFL", "AIZL", 1); add_syn("ADFR", "AIZR", 1);

    // Head oscillator: dorsal-ventral cross-inhibition (TD-02)
    // SMD dorsal → RMD ventral and vice versa (reciprocal inhibition circuit)
    // In C. elegans some ACh synapses act inhibitory via ACC chloride channels
    // REF: Pereira 2015 (ACh receptor diversity), Hendricks 2012
    // These use GABA-like reversal to model functional inhibition
    auto add_syn_inh = [&](const char* pre, const char* post, double sections) {
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
    // Step 19: ASER→AIA/AIY INHIBITORY (eLife 2024, Matsumoto et al.)
    // ASER releases glutamate → GLC-3 (Cl⁻ channel) on AIY → inhibitory
    // Fixes pirouette modulation: C↓ → ASER↑ → AIA↓ → AIB↑(disinhibited) → more pirouettes
    add_syn_inh("ASER", "AIAR", 2); add_syn_inh("ASER", "AIYR", 2);
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
    // Step 19: reduced from 8→3 sections so oscillator is sensitive to weathervane bias
    // Strong (8) cross-inhibition made oscillator too robust → 3.7 pA bias produced only
    // 0.007/mm curvature shift. Weaker inhibition allows bias to shift duty cycle.
    add_syn_inh("SMDDL", "SMDVL", 3); add_syn_inh("SMDDR", "SMDVR", 3);
    add_syn_inh("SMDVL", "SMDDL", 3); add_syn_inh("SMDVR", "SMDDR", 3);
    // RMD dorsal-ventral cross-inhibition
    add_syn_inh("RMDDL", "RMDVL", 6); add_syn_inh("RMDDR", "RMDVR", 6);
    add_syn_inh("RMDVL", "RMDDL", 6); add_syn_inh("RMDVR", "RMDDR", 6);
    // SMD → RMD excitatory (same side, co-activate dorsal or ventral)
    add_syn("SMDDL", "RMDDL", 3); add_syn("SMDDR", "RMDDR", 3);
    add_syn("SMDVL", "RMDVL", 3); add_syn("SMDVR", "RMDVR", 3);

    // Step 19 Phase 2: Klinotaxis pathway — AIZ → SMB (Izquierdo 2015, Yamazaki 2022)
    // SMB controls neck curvature DC bias, independent of SMD head oscillation CPG.
    // Ipsilateral wiring: AIZL → dorsal SMB, AIZR → ventral SMB
    // When AIZL > AIZR: dorsal bias → curvature offset
    // Cross-inhibition (SMBDL ⊣ SMBVL) amplifies D-V difference
    add_syn("AIZL", "SMBDL", 4);
    add_syn("AIZR", "SMBVR", 4);
    // SMB dorsal-ventral cross-inhibition: push-pull amplification of D-V asymmetry
    add_syn_inh("SMBDL", "SMBVL", 3); add_syn_inh("SMBDR", "SMBVR", 3);
    add_syn_inh("SMBVL", "SMBDL", 3); add_syn_inh("SMBVR", "SMBDR", 3);

    // AVB drive pathway: AIY → AVB (TD-01, excitatory interneuron drive)
    // REF: White 1986, WormAtlas
    add_syn("AIYL", "AVBL", 3); add_syn("AIYR", "AVBR", 3);
    // RIB → AVB (additional forward drive)
    add_syn("RIBL", "AVBL", 2); add_syn("RIBR", "AVBR", 2);

    // ================================================================
    // Step 37: AVE backward command — reversal grading + omega gating
    // AVE is the second backward command interneuron pair (with AVA)
    // AVA: lower threshold → short exploratory reversals
    // AVE: higher threshold → long committed reversals → omega
    // REF: Chalfie 1985, Piggott 2011, Kawano 2011
    // ================================================================

    // AIB → AVE: chemosensory relay → backward command (weaker than AIB→AVA)
    // AVE has higher activation threshold → only fires on strong AIB drive
    // REF: White 1986 — AIB makes synapses onto AVE
    add_syn("AIBL", "AVEL", 1); add_syn("AIBR", "AVER", 1);

    // ASH → AVE: nociception direct → committed reversal
    // REF: White 1986 — ASH synapses onto AVE (2 sections)
    add_syn("ASHL", "AVEL", 2); add_syn("ASHR", "AVER", 2);

    // AVE → DA: backward motor neuron drive (same targets as AVA)
    // REF: Chalfie 1985 — AVE commands DA/VA motor neurons
    // Step 39: expanded to all 5 DA neurons
    add_syn("AVEL", "DA01", 1); add_syn("AVER", "DA02", 1); add_syn("AVEL", "DA03", 1);
    add_syn("AVER", "DA04", 1); add_syn("AVEL", "DA05", 1);

    // RIM connections (Step 19b — Ouellette 2022 eLife)
    // AIB → RIM: activates RIM during reversals (reversal signal relay)
    add_syn("AIBL", "RIML", 3); add_syn("AIBR", "RIMR", 3);
    // AVE → RIM: additional reversal input
    add_syn("AVEL", "RIML", 2); add_syn("AVER", "RIMR", 2);

    // Step 31: RIV omega turn circuit (Gray 2005, Donnelly 2013)
    // RIV omega burst via POST-INHIBITORY REBOUND (not direct AVA excitation):
    //   Reversal: TA→LGC-55 holds RIV at -80mV → CCA-1 T-type Ca²⁺ deinactivates
    //   Reversal ends: TA decays → inhibition released → CCA-1 rebound burst → omega
    //   (AVA→RIV removed: AVA has 0.24 tonic release → 23pA continuous drive
    //    → RIV fires during forward locomotion → destroys SMD head oscillation)
    // Step 42C: Complete RIA↔RIV negative feedback loop (Cook 2019 anatomy)
    // RIA→RIV excitatory (ACh): Cook 2019 RIAL→RIVR=12, RIAL→RIVL=8, RIAR→RIVL=3, RIAR→RIVR=3
    // RIV→RIA inhibitory (GABA): Cook 2019 RIVL→RIAL=5, RIVR→RIAL=6, RIVL→RIAR=2, RIVR→RIAR=2
    // This creates a self-limiting oscillatory loop:
    //   RIA excites RIV → RIV inhibits RIA → RIA drive drops → RIV quiets → RIA recovers → cycle
    // Without feedback: RIA tonic drive keeps RIV depolarized → CCA-1 h inactivated → no omega
    // With feedback: RIV self-limits → oscillates around low activity → h can deinactivate
    // During reversal: TA deeply suppresses RIV → h fully deinactivates
    // Reversal ends: TA decays → RIA-RIV loop resumes → first burst = omega trigger
    // NOTE: AVE→RIV removed (does NOT exist in Cook 2019)
    add_syn("RIAL", "RIVR", 1); add_syn("RIAL", "RIVL", 1);
    add_syn("RIAR", "RIVL", 1); add_syn("RIAR", "RIVR", 1);
    // RIV→RIA inhibitory feedback (GABA via UNC-49)
    add_syn_inh("RIVL", "RIAL", 1);   add_syn_inh("RIVR", "RIAL", 1);
    add_syn_inh("RIVL", "RIAR", 0.5); add_syn_inh("RIVR", "RIAR", 0.5);
    // RIV ⊣ RMD dorsal: suppress dorsal muscles during omega → deepen ventral bend
    // REF: Donnelly 2013 — asymmetric D/V muscle drive for deep omega
    add_syn_inh("RIVL", "RMDDL", 1); add_syn_inh("RIVR", "RMDDR", 1);

    // Step 32: AS motor neuron circuit (White 1986, Haspel 2010, Chen 2006)
    // AS is unique: receives BOTH AVA and AVB → always active regardless of direction
    // This provides tonic dorsal bias that RIV must overcome for omega turns
    //
    // AVA → AS: backward command drives AS during reversals (1 section each)
    // AS are unpaired neurons; alternate L/R innervation along body
    // REF: White 1986 — AS1 receives from AVAL, AS2 from AVAR, etc.
    add_syn("AVAL", "AS01", 1); add_syn("AVAR", "AS02", 1);
    add_syn("AVAL", "AS03", 1); add_syn("AVAR", "AS04", 1); add_syn("AVAL", "AS05", 1);
    add_syn("AVAR", "AS06", 1); add_syn("AVAL", "AS07", 1);
    // AVB → AS: forward command also drives AS (1 section each)
    // REF: Chen 2006 — AS calcium imaging shows activity in both fwd/rev
    add_syn("AVBL", "AS01", 1); add_syn("AVBR", "AS02", 1);
    add_syn("AVBL", "AS03", 1); add_syn("AVBR", "AS04", 1); add_syn("AVBL", "AS05", 1);
    add_syn("AVBR", "AS06", 1); add_syn("AVBL", "AS07", 1);
    // DD ⊣ AS: GABAergic cross-inhibition during ventral phase
    // When DD fires (dorsal input → ventral inhibition), also suppress AS dorsal drive
    // This allows ventral-phase bending without AS fighting it
    add_syn_inh("DD01", "AS01", 1); add_syn_inh("DD01", "AS02", 1);
    add_syn_inh("DD02", "AS03", 1); add_syn_inh("DD02", "AS04", 1);
    add_syn_inh("DD03", "AS04", 1); add_syn_inh("DD03", "AS05", 1);
    add_syn_inh("DD04", "AS05", 1); add_syn_inh("DD04", "AS06", 1);
    add_syn_inh("DD05", "AS06", 1); add_syn_inh("DD05", "AS07", 1);

    // DB ↔ AS: gap junction coupling (synchronize dorsal activation)
    // REF: White 1986 — DB and AS co-innervate dorsal muscles
    add_gj("DB01", "AS01", 1); add_gj("DB01", "AS02", 1);
    add_gj("DB02", "AS03", 1); add_gj("DB02", "AS04", 1);
    add_gj("DB03", "AS04", 1); add_gj("DB03", "AS05", 1);
    add_gj("DB04", "AS05", 1); add_gj("DB05", "AS06", 1);
    add_gj("DB06", "AS06", 1); add_gj("DB07", "AS07", 1);

    // ================================================================
    // Step 33: RME + OLQ synaptic connections
    // ================================================================

    // SMD →(extrasynaptic) RME: cholinergic volume transmission via GAR-2
    // SMD does NOT directly synapse onto RME (White 1986 connectome)
    // Modeled as weak chemical synapse (g ~0.03 nS, 10× weaker than normal)
    // REF: Huang 2016 eLife — SMD ACh → GAR-2 muscarinic on RME
    // SMDD active during dorsal bend → activates RMED (same phase)
    // SMDV active during ventral bend → activates RMEV (same phase)
    // sections=0.3 approximates extrasynaptic 10× dilution (0.3 × 0.1nS ≈ 0.03nS)
    add_syn("SMDDL", "RMED", 0.3); add_syn("SMDDR", "RMED", 0.3);
    add_syn("SMDVL", "RMEV", 0.3); add_syn("SMDVR", "RMEV", 0.3);

    // RME ⊣(GABAB) SMD: extrasynaptic GABA → GBB-1/2 on SMD
    // Negative feedback: RME inhibits SMD to limit head bending amplitude
    // Also modeled as weak inhibitory synapse (extrasynaptic)
    // RMED active during dorsal bend → inhibits SMDV (contralateral)
    // RMEV active during ventral bend → inhibits SMDD (contralateral)
    // REF: Huang 2016 eLife — GABAB GBB-1/2 on SMD restrains head bending
    add_syn_inh("RMED", "SMDVL", 0.3); add_syn_inh("RMED", "SMDVR", 0.3);
    add_syn_inh("RMEV", "SMDDL", 0.3); add_syn_inh("RMEV", "SMDDR", 0.3);

    // RIA → RME: direct chemical synapse (White 1986 connectome)
    // RIA provides head oscillation signal to RME
    add_syn("RIAL", "RMED", 1); add_syn("RIAL", "RMEV", 1);
    add_syn("RIAR", "RMED", 1); add_syn("RIAR", "RMEV", 1);

    // OLQ → RMD: head withdrawal reflex (Hart 1995)
    // Touch on one side → RMD activation → head moves away
    // REF: Hart 1995 — OLQ+IL1 required for head withdrawal
    add_syn("OLQDL", "RMDDL", 1); add_syn("OLQDR", "RMDDR", 1);
    add_syn("OLQVL", "RMDVL", 1); add_syn("OLQVR", "RMDVR", 1);

    // OLQ → RIC: indirect path to AVA (not direct!)
    // REF: White 1986 — OLQ synapses onto RIC, which connects to command INs
    add_syn("OLQDL", "RICL", 1); add_syn("OLQDR", "RICR", 1);
    add_syn("OLQVL", "RICL", 1); add_syn("OLQVR", "RICR", 1);

    // OLQ ↔ CEP: gap junction coupling with dopaminergic mechanosensory
    // REF: White 1986 — OLQ and CEP share gap junctions at lip
    add_gj("OLQDL", "CEPDL", 1); add_gj("OLQDR", "CEPDR", 1);
    add_gj("OLQVL", "CEPVL", 1); add_gj("OLQVR", "CEPVR", 1);

    // ================================================================
    // Step 34: O₂ sensing circuit (Gray 2004, Chang 2006, Laurent 2015)
    // URX → AUA → AVA: hyperoxia avoidance pathway
    // AQR/PQR → AVA: body cavity O₂ → reversal
    // ================================================================

    // URX → AUA: primary O₂ relay (WormWiring: 2 sections each)
    // AUA integrates O₂ (URX) + serotonin (ADF) signals
    add_syn("URXL", "AUAL", 2); add_syn("URXR", "AUAR", 2);

    // AUA → AVA: O₂ relay → backward command (hyperoxia → reversal)
    // REF: Chang 2006 — "AUA receives from URX and synapses onto AVA"
    // NPR-1 215V suppresses AUA neurosecretion (Laurent 2015: acts downstream of Ca2+)
    // Use 0.3 sections to model NPR-1 presynaptic inhibition in N2
    // (WormWiring anatomical: 2+1 sections, but NPR-1 reduces effective release ~10x)
    add_syn("AUAL", "AVAL", 0.3); add_syn("AUAL", "AVAR", 0.3);
    add_syn("AUAR", "AVAL", 0.3); add_syn("AUAR", "AVAR", 0.3);

    // URX → AVB: direct speed modulation (WormWiring: URXL→AVBL 1 section)
    // High O₂ → URX active → AVB excited → faster forward (escape high O₂)
    add_syn("URXL", "AVBL", 1);

    // AQR → AVA: anterior body cavity O₂ → backward command
    // REF: Chang 2006 Fig 8A — AQR converges on AVA
    add_syn("AQR", "AVAL", 1); add_syn("AQR", "AVAR", 1);

    // PQR → AVA: posterior body cavity O₂ → backward command
    // REF: Chang 2006 Fig 8A — PQR converges on AVA
    add_syn("PQR", "AVAL", 1); add_syn("PQR", "AVAR", 1);

    // ================================================================
    // Step 35: CO₂ sensing circuit (Hallem 2008, Bretscher 2011, Carrillo 2013)
    // BAG detects CO₂ via gcy-9 → cGMP → TAX-2/TAX-4
    // Downstream: BAG → RIG → AIY/AIB (simplified as direct BAG→AIY/AIB)
    // N2: NPR-1 suppresses URX → URX doesn't inhibit CO₂ circuit → avoids CO₂
    // ================================================================

    // BAG ⊣ AIY: CO₂ high → suppress forward drive (via RIG/GluCl, inhibitory)
    // REF: White 1986 — BAG→RIG→AIY pathway; GluCl-mediated inhibition
    add_syn_inh("BAGL", "AIYL", 1); add_syn_inh("BAGR", "AIYR", 1);

    // BAG → AIB: CO₂ high → promote turning/reversal (excitatory)
    // REF: White 1986 — BAG→AIB (1 section each)
    add_syn("BAGL", "AIBL", 1); add_syn("BAGR", "AIBR", 1);

    // BAG → RIA: head turning modulation (1 section each)
    // REF: White 1986 — BAG makes synapses onto RIA
    add_syn("BAGL", "RIAL", 1); add_syn("BAGR", "RIAR", 1);

    // ================================================================
    // Step 36: Proprioception circuit (Li 2006, Way 1989, Yeon 2018)
    // DVA: whole-body stretch receptor → motor neuron gain modulation
    // PVD: harsh touch + posterior body proprioception → AVA
    // ================================================================

    // DVA → DB: modulate forward wave amplitude (White 1986: DVA→DB ~2 sections)
    // DVA senses body curvature via TRP-4 → adjusts B-class MN drive
    // Step 39: expanded to all 7 DB neurons
    add_syn("DVA", "DB01", 1); add_syn("DVA", "DB02", 1); add_syn("DVA", "DB03", 1);
    add_syn("DVA", "DB04", 1); add_syn("DVA", "DB05", 1); add_syn("DVA", "DB06", 1);
    add_syn("DVA", "DB07", 1);

    // DVA → VB: same modulation for ventral B-class (1 section each)
    // Step 39: expanded to all 7 VB neurons
    add_syn("DVA", "VB01", 1); add_syn("DVA", "VB02", 1); add_syn("DVA", "VB03", 1);
    add_syn("DVA", "VB04", 1); add_syn("DVA", "VB05", 1); add_syn("DVA", "VB06", 1);
    add_syn("DVA", "VB07", 1);

    // DVA → AVA: extreme bending → protective reversal (weak, 0.5 section)
    // REF: White 1986 — DVA makes few synapses onto AVA
    add_syn("DVA", "AVAL", 0.5);

    // PVD → AVA: harsh touch → backward movement (2 sections, strong)
    // REF: Way & Chalfie 1989, Hart 1995 — PVD is harsh touch sensor
    // GLR-1 glutamate receptors on AVA mediate this response
    add_syn("PVDL", "AVAL", 2); add_syn("PVDR", "AVAR", 2);

    // PVD ↔ DVA: gap junction — proprioceptive signal integration
    // PVD dendrites tile body wall → local curvature → DVA integrates globally
    add_gj("PVDL", "DVA", 1); add_gj("PVDR", "DVA", 1);

    // ================================================================
    // Step 38: Egg-laying circuit (Collins 2016, Schafer 2006)
    // HSN: 5-HT command motor neuron → vulval muscle contraction
    // VC4/VC5: ACh motor neurons → facilitate egg release
    // ================================================================

    // PLM ⊣ HSN: gentle touch inhibits egg laying (safety mechanism)
    // REF: Zhang 2008 — PLM inhibits HSN activity during locomotion
    add_syn_inh("PLML", "HSNL", 1); add_syn_inh("PLMR", "HSNR", 1);

    // VC → VB: egg-laying slows locomotion (weak inhibition)
    // REF: Collins 2016 — VC synapses onto locomotion motor neurons
    add_syn_inh("VC4", "VB01", 0.5); add_syn_inh("VC5", "VB02", 0.5);

    // Step 23: Thermotaxis circuit — AFD→AIY (Mori & Ohshima 1995)
    // AFD is the primary thermosensory neuron, AIY is the shared integration node
    // AFD→AIY: excitatory, 5 sections (strengthened to compete with chemotaxis)
    // Cook 2019: ~3 EM sections; boosted for functional thermotaxis in MVP
    // This shares the AIY→RIA→SMD downstream pathway with chemotaxis (ASE→AIA→AIY)
    add_syn("AFDL", "AIYL", 5); add_syn("AFDR", "AIYR", 5);
    // AFD→AIZ: weaker connection, contributes to cryophilic behavior
    // REF: Mori 1995 — AIZ ablation → thermophilic (loses cold-seeking)
    add_syn("AFDL", "AIZL", 2); add_syn("AFDR", "AIZR", 2);

    // Key gap junctions
    add_gj("AVAL", "AVAR", 10);  // left-right coupling of command interneurons
    add_gj("AVBL", "AVBR", 12);
    add_gj("AVDL", "AVDR", 5);
    add_gj("AVEL", "AVER", 4);
    // Step 37: AVA ↔ AVE gap junction — tight coupling of backward command pair
    // REF: White 1986, Kawano 2011 — AVA/AVE calcium tightly coupled
    // 2023 Frontiers: RIM promotes reversal via gap junctions to AVA/AVE
    add_gj("AVAL", "AVEL", 3); add_gj("AVAR", "AVER", 3);
    add_gj("ASEL", "ASER", 2);
    add_gj("AIBL", "AIBR", 3);
    // RIM ↔ AVA gap junctions: CRITICAL for forward run stabilization
    // REF: Ouellette 2022 eLife — RIM gap junctions propagate hyperpolarization
    //   Forward state: RIM at rest (-60mV) pulls AVA down via gap junction
    //   → prevents spontaneous reversal initiation (behavioral inertia)
    //   Reversal state: AIB activates both AVA and RIM → cooperative switch
    // Step 42: Cook 2019 weights: RIMR↔AVAL=11, RIML↔AVAR=8 (cross-wired)
    // Keep at 2: increasing destabilizes TA dynamics (TA baseline rises → speed drop)
    add_gj("RIML", "AVAL", 2); add_gj("RIMR", "AVAR", 2);
    // RIM L-R coupling
    add_gj("RIML", "RIMR", 3);
    // Step 42: RIV L-R coupling (Cook 2019: RIVL↔RIVR=28)
    // Conservative: SMDV↔RIV gap OMITTED — SMD oscillation prevents CCA-1 h deinactivation
    add_gj("RIVL", "RIVR", 4);
    // Touch circuit gap junctions (Chalfie 1985: touch cells → agonist interneurons)
    // ALM → AVD: anterior touch excites backward interneuron
    add_gj("ALML", "AVDL", 4); add_gj("ALMR", "AVDR", 4);

    // ================================================================
    // Step 24: Pharyngeal nervous system synapses
    // REF: Albertson & Thomson 1976, Cook 2020 (pharyngeal connectome)
    // ================================================================

    // I1 → MC: excitatory, relays extrapharyngeal signals to pacemaker
    // REF: Raizen et al 1995 — I1 affects pump rate in absence of bacteria
    add_syn("I1L", "MCL", 3); add_syn("I1R", "MCR", 3);

    // MC → M3: MC activity → muscle contraction → M3 proprioceptive firing
    // Modeled as direct excitatory connection (shortcut for muscle-mediated)
    // REF: Raizen & Avery 1994 — M3 fires during MC-driven contraction
    add_syn("MCL", "M3L", 2); add_syn("MCR", "M3R", 2);

    // M3 → MC: weak inhibitory feedback (glutamate → Cl⁻)
    // M3 relaxation signal slightly delays next MC firing
    add_syn("M3L", "MCL", 1); add_syn("M3R", "MCR", 1);

    // MC → M4: MC pumping activates M4 for isthmus peristalsis
    // REF: Song et al 2013 — M4 activated by anterior pumping + 5-HT
    add_syn("MCL", "M4", 2); add_syn("MCR", "M4", 2);

    // I1 → I1: left-right coupling within pharynx
    add_gj("I1L", "I1R", 2);
    // MC ↔ MC: left-right synchronization of pacemaker
    add_gj("MCL", "MCR", 3);
    // M3 ↔ M3: left-right synchronization of relaxation
    add_gj("M3L", "M3R", 2);

    // RIP ↔ I1: the SOLE bridge between somatic and pharyngeal nervous systems
    // REF: Albertson & Thomson 1976 — bilateral gap junction pair
    add_gj("RIPL", "I1L", 2); add_gj("RIPR", "I1R", 2);

    // Step 38: HSN ↔ VC gap junction — synchronize egg-laying motor output
    // REF: White 1986, Collins 2016 — HSN and VC electrically coupled
    add_gj("HSNL", "VC4", 2); add_gj("HSNR", "VC5", 2);

    // ================================================================
    // Step 27: RIS sleep neuron connections
    // REF: White 1986, Cook 2019 — RIS synaptic outputs
    //      Turek 2016 eLife — FLP-11 is the major sleep transmitter (volume)
    //      Konietzka 2020 — RIS as locomotion stop neuron
    // NOTE: RIS sleep induction is primarily via FLP-11 volume transmission
    //       (handled in neuromodulation system), NOT wired synapses.
    //       The chemical synapses below provide fast GABA inhibition
    //       to command interneurons for acute locomotion stop.
    // ================================================================
    // RIS ⊣ AVA: GABA inhibition of backward command (stop reversals during sleep)
    add_syn_inh("RIS", "AVAL", 2); add_syn_inh("RIS", "AVAR", 2);
    // RIS ⊣ AVB: GABA inhibition of forward command (stop forward during sleep)
    add_syn_inh("RIS", "AVBL", 1); add_syn_inh("RIS", "AVBR", 1);
    // RIS ⊣ AIB: GABA inhibition of reversal initiation
    add_syn_inh("RIS", "AIBL", 1); add_syn_inh("RIS", "AIBR", 1);

    // RIS gap junctions: AIB (5 sections in connectome, community 4)
    // REF: Emmons 2024 PLOS Biology — RIS has 5 gap junctions to AIB
    add_gj("RIS", "AIBL", 2); add_gj("RIS", "AIBR", 2);

    LOG_INFO("Generated default connectome: ", neurons.size(), " neurons, ",
             synapses.size(), " synapses, ", gap_junctions.size(), " gap junctions");
}

} // namespace celegans
