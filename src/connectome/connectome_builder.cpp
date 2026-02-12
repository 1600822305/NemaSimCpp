// ================================================================
// Connectome Builder — default connectome organized by circuit
// Split from generate_default_connectome() (Step 51)
//
// 12 build functions, one per circuit/subsystem.
// Every connection is hand-written C++, zero external dependencies.
// ================================================================
#include "connectome/connectome_builder.h"
#include "core/logger.h"
#include <unordered_map>
#include <string>
#include <cstring>

namespace celegans {
namespace {

// ----------------------------------------------------------------
// ConnectomeBuilder — helper struct with short method names
// ----------------------------------------------------------------
struct CB {
    std::vector<NeuronInfo>& neurons;
    std::vector<SynapseInfo>& synapses;
    std::vector<GapJunctionInfo>& gap_junctions;
    std::unordered_map<std::string, int> name_to_id;

    // --- Neuron registration ---
    void neuron(const char* name, NeuronType type, NeurotransmitterType nt) {
        NeuronInfo info;
        info.id = static_cast<int>(neurons.size());
        info.name = name;
        info.type = type;
        info.neurotransmitter = nt;
        neurons.push_back(info);
    }

    void finalize_ids() {
        for (auto& n : neurons) name_to_id[n.name] = n.id;
    }

    // --- Synapse helpers ---
    void syn(const char* pre, const char* post, double sections) {
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
    }

    void inh(const char* pre, const char* post, double sections) {
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
    }

    // Step 28: compartment-targeted synapse (for multi-compartment neurons)
    void comp(const char* pre, const char* post, double sections, int post_comp) {
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
    }

    void gj(const char* a, const char* b, double sections) {
        auto a_it = name_to_id.find(a);
        auto b_it = name_to_id.find(b);
        if (a_it != name_to_id.end() && b_it != name_to_id.end()) {
            GapJunctionInfo g;
            g.neuron_a_id = a_it->second;
            g.neuron_b_id = b_it->second;
            g.num_sections = sections;
            gap_junctions.push_back(g);
        }
    }
};

// ================================================================
// 1. Neuron Definitions
// ================================================================
void build_neurons(CB& b) {
    using NT = NeuronType;
    using NTT = NeurotransmitterType;

    // --- Sensory neurons ---
    b.neuron("ASEL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ASER", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("AWCL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("AWCR", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("AWAL", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("AWAR", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("ASHL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ASHR", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ALML", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ALMR", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("PLML", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("PLMR", NT::SENSORY, NTT::GLUTAMATE);
    // Neuromodulatory sensory neurons (Step 20, Layer 6)
    // NSM: pharyngeal neuron, detects food → releases 5-HT → dwelling
    // REF: Flavell 2013 Cell — NSM drives dwelling via serotonin
    b.neuron("NSML", NT::SENSORY, NTT::SEROTONIN);
    b.neuron("NSMR", NT::SENSORY, NTT::SEROTONIN);
    // CEP: head mechanosensory, detects bacteria → releases DA → basal slowing
    // REF: Sawin 2000 — dopamine basal slowing response
    b.neuron("CEPDL", NT::SENSORY, NTT::DOPAMINE);
    b.neuron("CEPDR", NT::SENSORY, NTT::DOPAMINE);
    b.neuron("CEPVL", NT::SENSORY, NTT::DOPAMINE);
    b.neuron("CEPVR", NT::SENSORY, NTT::DOPAMINE);
    // Step 60: ADE — anterior deirid mechanosensory, dopaminergic
    // Ciliated endings in lateral body near pharynx; senses bacteria texture
    // Together with CEP, drives basal slowing response on food
    // REF: Sawin 2000 — cat-2 in ADE/PDE rescues BSR; Sulston 1977 — deirid anatomy
    b.neuron("ADEL", NT::SENSORY, NTT::DOPAMINE);
    b.neuron("ADER", NT::SENSORY, NTT::DOPAMINE);
    // Step 60: PDE — posterior deirid mechanosensory, dopaminergic
    // Located mid-body; senses bacteria along body wall
    // Unique among DA neurons: projects posteriorly, gap junctions with PVD
    // REF: Sawin 2000, Sulston 1977, Chase & Koelle 2007 review
    b.neuron("PDEL", NT::SENSORY, NTT::DOPAMINE);
    b.neuron("PDER", NT::SENSORY, NTT::DOPAMINE);
    // AFD: thermosensory neuron — senses temperature, drives thermotaxis
    // REF: Mori & Ohshima 1995, Luo 2014 PNAS — AFD→AIY core thermotaxis circuit
    b.neuron("AFDL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("AFDR", NT::SENSORY, NTT::GLUTAMATE);
    // ADF: chemosensory neuron, serotonin source for learned pathogen avoidance
    // REF: Zhang 2005 Nature — PA14 exposure → ADF TPH-1 ↑ → 5-HT ↑ → MOD-1 on AIY/AIZ
    //      Ha 2010 Neuron — ADF essential for aversive olfactory learning
    b.neuron("ADFL", NT::SENSORY, NTT::SEROTONIN);
    b.neuron("ADFR", NT::SENSORY, NTT::SEROTONIN);
    // Step 43: AWB repulsive olfactory neurons — sense pathogen volatiles
    // AWB detects 1-undecene (Pseudomonas), serrawettin (Serratia), etc.
    // After aversive learning: AWB→AUA/RMG→AVA drives reflexive backward locomotion
    // REF: Troemel 1997 Cell, Ha 2010 Neuron, BMC Biology 2022
    b.neuron("AWBL", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("AWBR", NT::SENSORY, NTT::ACETYLCHOLINE);
    // Step 33: OLQ nose touch mechanosensory neurons (labial cilia)
    // 4 quadrant neurons: sense close-range obstacles (dist < 0.3mm)
    // OLQ mediates head withdrawal reflex via RMD (Hart 1995)
    // Only 5% of nose touch avoidance (ASH=45%, FLP=29%) — subtle, exploratory
    // REF: Kaplan & Horvitz 1993, Hart 1995, White 1986
    b.neuron("OLQDL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("OLQDR", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("OLQVL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("OLQVR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 34: O₂ sensing neurons
    // URX: primary O₂ sensor, gcy-35/gcy-36 soluble guanylate cyclase
    // Activated by HIGH O₂ (>14%), drives hyperoxia avoidance
    // NPR-1 215V (N2) tonically inhibits URX → mild O₂ response on food
    // REF: Gray 2004 Nature, Cheung 2005 Neuron, Chang 2006 PLoS Biology
    b.neuron("URXL", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("URXR", NT::SENSORY, NTT::ACETYLCHOLINE);
    // AQR: anterior body cavity O₂ sensor (single, unpaired)
    // Exposed to pseudocoelomic fluid, expresses gcy-35
    // REF: Chang 2006 — AQR+PQR+URX form distributed O₂ circuit
    b.neuron("AQR",  NT::SENSORY, NTT::GLUTAMATE);
    // PQR: posterior body cavity O₂ sensor (single, unpaired)
    // Tail position → high O₂ at tail → accelerate forward (Busch 2012)
    b.neuron("PQR",  NT::SENSORY, NTT::GLUTAMATE);
    // Step 35: BAG — CO₂ sensor (gcy-9 receptor guanylate cyclase)
    // Activated by CO₂ > 0.5%, drives CO₂ avoidance (turning + speed change)
    // N2: NPR-1 suppresses URX → URX doesn't inhibit CO₂ circuit → avoids CO₂
    // Phasic response: sensitive to CO₂ changes, OFF rebound on CO₂ decrease
    // REF: Hallem & Sternberg 2008 PNAS, Bretscher 2011 Neuron, Carrillo 2013
    b.neuron("BAGL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("BAGR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 36: PVD — harsh touch + proprioception (multi-dendritic)
    // Dendrites tile entire body wall; dual-mode: harsh touch + body bend sensing
    // Glutamatergic (GLR-1 mediated harsh touch response, Hart 1995)
    // REF: Way & Chalfie 1989, Albeg 2011, Tao 2019
    b.neuron("PVDL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("PVDR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 55: ASJ — PRIMARY photoreceptor + dauer pheromone sensor
    // Expresses LITE-1 gustatory receptor homolog → UV/blue light detection
    // Signal: LITE-1 → Gα (GOA-1/GPA-3) → guanylate cyclase → cGMP → TAX-2/TAX-4 CNG
    // Also senses dauer pheromone (daf-7/TGF-β) and DAF-28/insulin
    // REF: Ward 2008 Nat Neurosci — ASJ is photoreceptor cell
    //      Liu 2010 — LITE-1 phototaxis via multiple sensory neurons
    //      Bargmann & Horvitz 1991 — ASJ chemosensory function
    b.neuron("ASJL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ASJR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 55: ASK — secondary photoreceptor + pheromone sensor
    // Expresses LITE-1, responds to UV/blue light (Liu 2010)
    // Also senses osas#9 dispersal pheromone (avoidance) and lysine (attraction)
    // REF: Liu 2010, eLife 2025 (LITE-1 chemoreceptor)
    //      Bargmann & Horvitz 1991 — ASK amphid sensory neuron
    b.neuron("ASKL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ASKR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 61: AVM — anterior gentle touch neuron (single, unpaired)
    // Completes mechanosensory circuit: AVM + ALM = anterior touch, PLM = posterior
    // AVM born post-embryonically (L1), migrates to mid-body ventral
    // REF: Chalfie 1985, Way & Chalfie 1989 — AVM gentle touch
    b.neuron("AVM",  NT::SENSORY, NTT::ACETYLCHOLINE);
    // Step 61: ASI — amphid sensory, insulin/dauer pathway
    // Secretes DAF-7 (TGF-β) and INS-1 (insulin-like) → food quality signaling
    // Key node for developmental decision (dauer vs reproductive)
    // REF: Bargmann & Horvitz 1991, Beverly 2011, Cornils 2011
    b.neuron("ASIL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ASIR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 61: ADL — pheromone + nociceptive amphid sensory
    // Detects ascaroside pheromones (ascr#3 avoidance), SDS, Cu2+
    // Minor role in chemical avoidance (revealed when ASH ablated)
    // REF: Troemel 1997 Cell, Jang 2012, Serrano-Saiz 2013
    b.neuron("ADLL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("ADLR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 73: FLP — multidendritic head nociceptor
    // Dendrites cover entire head skin; polymodal: harsh touch + thermal nociception
    // 29% of nose touch avoidance (ASH=45%, OLQ=5%)
    // MEC-10 (DEG/ENaC) for cell-autonomous harsh touch response
    // Gentle nose touch requires facilitation from OLQ/CEP via RIH gap junction hub
    // Presynaptic to AVA, AVD, AVE, AIB (reversal-promoting interneurons)
    // REF: Kaplan & Horvitz 1993, Chatzigeorgiou & Schafer 2011 Neuron
    b.neuron("FLPL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("FLPR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 81: PHB — phasmid tail chemosensory (repellent at tail)
    // Polymodal: SDS, IAA, harsh touch, osmotic stimuli (Zou 2017 Sci Rep)
    // Key function: NEGATIVELY modulates reversals to repellents (Hilliard 2002 Curr Biol)
    // Head ASH detects repellent → reversal; tail PHB → suppresses reversal
    // → directional escape: repellent ahead=reverse, repellent behind=continue forward
    // REF: Hilliard 2002 Curr Biol, Zou 2017 Sci Rep, Cook 2019
    b.neuron("PHBL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("PHBR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 81: PHA — phasmid tail chemosensory (dauer pheromone + food quality)
    // Similar structure to PHB but different downstream connections
    // Also polymodal (Zou 2017); senses ascarosides for dauer decision
    // REF: Hilliard 2002, Zou 2017, Bargmann & Horvitz 1991
    b.neuron("PHAL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("PHAR", NT::SENSORY, NTT::GLUTAMATE);
    // Step 73: IL1 — inner labial sensory neurons (4 quadrant)
    // Ciliated sensory endings at nose tip; sense directional nose touch
    // Mediate head withdrawal reflex with OLQ via RMD motor neurons
    // Ablation of OLQ+IL1 → abnormally slow foraging + exaggerated nose turns
    // Community 2 (Emmons 2024): foraging/nose positioning
    // REF: Hart et al. 1995, White 1986, Emmons 2024 PLOS Biology
    b.neuron("IL1DL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("IL1DR", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("IL1VL", NT::SENSORY, NTT::GLUTAMATE);
    b.neuron("IL1VR", NT::SENSORY, NTT::GLUTAMATE);

    // Step 96: IL2 — inner labial type 2 sensory neurons (6 total, adding 4 quadrant)
    // Ciliated sensory endings at nose tip; detect environmental conditions
    // Part of RMG hub-and-spoke gap junction network (Macosko 2009 Fig 3a)
    // Involved in dauer nictation and social aggregation signaling
    // REF: White 1986, Macosko 2009 Nature, WormAtlas
    b.neuron("IL2DL", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("IL2DR", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("IL2VL", NT::SENSORY, NTT::ACETYLCHOLINE);
    b.neuron("IL2VR", NT::SENSORY, NTT::ACETYLCHOLINE);

    // Step 105: URA — inner labial motor neurons (4 quadrant)
    // Community 2 (Foraging): same module as IL1/IL2
    // Motor neuron making NMJs in nerve ring → head body wall muscles
    // Dendritic extensions towards nose → possible sensory function ("unknown receptor")
    // IL1/IL2 → URA → head muscles: nose positioning during foraging
    // Cholinergic (Pereira 2015 eLife)
    // REF: White 1986, Emmons 2024 PLOS Biology (PMC10983851)
    b.neuron("URADL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("URADR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("URAVL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("URAVR", NT::MOTOR, NTT::ACETYLCHOLINE);

    // --- Interneurons ---
    b.neuron("AIAL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AIAR", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AIBL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AIBR", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AIYL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AIYR", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AIZL", NT::INTER, NTT::UNKNOWN);
    b.neuron("AIZR", NT::INTER, NTT::UNKNOWN);
    b.neuron("RIAL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("RIAR", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("RIBL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("RIBR", NT::INTER, NTT::ACETYLCHOLINE);
    // Step 34: AUA — O₂ signal relay/integration interneuron
    // Receives from URX (O₂) + ADF (5-HT) → outputs to AVA/AVB
    // Key integration point: O₂ and serotonin converge here
    // REF: Chang 2006 PLoS Biology, WormWiring (Cook 2019)
    b.neuron("AUAL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AUAR", NT::INTER, NTT::GLUTAMATE);
    // Step 36: DVA — whole-body proprioceptive interneuron (single, unpaired)
    // Axon spans entire body; TRP-4 TRPN stretch receptor channel
    // Senses body curvature → modulates motor neuron gain
    // trp-4 mutant: abnormal body bending (Li 2006 Nature)
    // REF: Li 2006 Nature, Hu 2011, Yeon 2018 PLoS Biology
    b.neuron("DVA",  NT::INTER, NTT::GLUTAMATE);
    // Command interneurons
    b.neuron("AVAL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AVAR", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AVBL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AVBR", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AVDL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AVDR", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AVEL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AVER", NT::INTER, NTT::ACETYLCHOLINE);
    // Step 53: PVC — forward command interneuron (5th command pair)
    // Receives PLM (posterior touch), AIY (chemotaxis), DVA (proprioception)
    // Outputs to AVB (main forward drive). 5-HT MOD-1 inhibits on food.
    // REF: Chalfie 1985, White 1986, Kawano 2011, Zheng 1999
    b.neuron("PVCL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("PVCR", NT::INTER, NTT::GLUTAMATE);
    // Step 83: AVF — second forward command interneuron (ventral cord)
    // Receives PHA (tail chemosensory) + PVC (forward command)
    // Outputs to AVB (forward drive) — parallel to PVC→AVB
    // Emmons 2024: "AVF collects input... directs output to... AVB"
    // Community 9; cholinergic
    // REF: Emmons 2024 PLOS Biology, Cook 2019
    b.neuron("AVFL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("AVFR", NT::INTER, NTT::ACETYLCHOLINE);
    // Step 83: LUA — tail sensory relay interneuron
    // Receives PHB (tail repellent) + PLM (posterior touch)
    // Outputs to AVD/PVC (backward/forward command relay)
    // Integrates tail sensory modalities for command neuron selection
    // Glutamatergic; Cook 2019: preanal ganglion
    // REF: White 1986, Cook 2019, Emmons 2024
    b.neuron("LUAL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("LUAR", NT::INTER, NTT::GLUTAMATE);
    // Step 82: AIN — ring interneuron, parallel chemotaxis relay
    // Receives ASE/AWC chemosensory input, outputs to AIY/RIA
    // Creates parallel pathway ASE→AIN→AIY alongside direct ASE→AIY
    // Glutamatergic; loss reduces chemotaxis efficiency
    // REF: White 1986, Cook 2019, Emmons 2024 PLOS Biology
    b.neuron("AINL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AINR", NT::INTER, NTT::GLUTAMATE);
    // Step 82: RIG — single unpaired ring interneuron, ventral cord→navigation relay
    // Receives DVC/PVT (ventral cord integrators) → outputs to AIY/AIZ/RIA/AVK
    // Community 4 (Emmons 2024): navigation/head motor
    // Bridges ventral cord information processing to head navigation circuit
    // REF: Emmons 2024 PLOS Biology, Cook 2019
    b.neuron("RIG",  NT::INTER, NTT::GLUTAMATE);
    // Step 75: RMG — social/pathogen hub interneuron (reclassified from motor, Cook 2019)
    // Hub of hub-and-spoke gap junction network for aggregation (NPR-1 modulated)
    // Pathogen aversion: AWB→RMG→AVA/AVD drives reflexive backward locomotion
    // eat-4+ (glutamatergic), also expresses FLP-21 neuropeptide (NPR-1 ligand)
    // REF: de Bono 2002 Nature, Macosko 2009 Nature, Filipowicz 2022 BMC Biology
    b.neuron("RMGL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("RMGR", NT::INTER, NTT::GLUTAMATE);
    // Step 103: SAA — sublateral interneurons with motor-like properties (4 quadrant)
    // Classified as interneuron (White 1986) but makes NMJs like sublateral motor neurons
    // Expresses stretch receptor genes → nose proprioceptive function
    // "RIV, SAA, and SMB are part of a turn circuit that inhibits reversals" (Emmons 2024)
    // "SAA neurons are a major source of input to AVA" — unique among sublateral MNs
    // Community 3 (chemosensation/navigation): alongside AIA/AIB/AIY/AIZ/RIM
    // Cholinergic; ALN/PLN contribute 20% of chemical input
    // REF: White 1986, Cook 2019, Emmons 2024 PLOS Biology (PMC10983851)
    b.neuron("SAADL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("SAADR", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("SAAVL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("SAAVR", NT::INTER, NTT::ACETYLCHOLINE);
    // Step 73: RIH — hub interneuron for nose touch coincidence detection
    // Single unpaired neuron in nerve ring
    // Hub of hub-and-spoke gap junction network: FLP, OLQ, CEP, ADF all connect
    // Function: coincidence detector — active spokes amplify, inactive suppress via shunting
    // REF: Chatzigeorgiou & Schafer 2011 Neuron, Rabinowitch et al. 2013 Curr Biol
    b.neuron("RIH",  NT::INTER, NTT::GLUTAMATE);
    // RIM: reversal-active interneuron, stabilizes forward/reverse states
    // REF: Ouellette 2022 eLife — RIM gap junctions create behavioral inertia
    b.neuron("RIML", NT::INTER, NTT::GLUTAMATE);
    b.neuron("RIMR", NT::INTER, NTT::GLUTAMATE);
    // RIC: octopamine/tyramine source — promotes roaming when off food
    // REF: Alkema 2005 — RIC produces OA, antagonizes 5-HT dwelling
    b.neuron("RICL", NT::INTER, NTT::OCTOPAMINE);
    b.neuron("RICR", NT::INTER, NTT::OCTOPAMINE);
    // Step 61: Ventral cord interneurons — "integration hub" (Emmons 2024)
    // Community 9: 13 classes of non-command VNC interneurons synapse onto
    // 59% of all neurons in 1 step, 98% in 2 steps (chemical)
    // DVC — stretch receptor interneuron (single, unpaired)
    // Stretch → AVA (backward locomotion), gap junctions to PVT
    // REF: Li 2006, Emmons 2024 PLOS Biology
    b.neuron("DVC",  NT::INTER, NTT::GLUTAMATE);
    // PVT — neuropeptide network hub (single, unpaired)
    // Connected to DVC by gap junctions; similar connectivity
    // Hub of neuropeptide connectome (Ripoll-Sánchez 2023)
    // REF: Emmons 2024 — PVT is hub of neuropeptide communication
    b.neuron("PVT",  NT::INTER, NTT::GLUTAMATE);
    // AVK — PDE target, turn circuit integrator
    // Primary target of PDE (50% of PDE output by weight!)
    // Outputs to RIM, RIV (turn circuit), sublateral motors
    // Expresses FLP-1 neuropeptide; gap junctions to RIC, DVA
    // REF: Emmons 2024, Li 1999 — FLP-1 locomotion
    b.neuron("AVKL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AVKR", NT::INTER, NTT::GLUTAMATE);
    // AVJ — O₂/aversive integrator
    // Inputs: ADL, AQR, PQR, URX (all O₂/aversive)
    // Gap junctions to RIS (5 sections!) — sleep connection
    // REF: Emmons 2024
    b.neuron("AVJL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AVJR", NT::INTER, NTT::GLUTAMATE);
    // AVH — sensory bridge interneuron
    // Gap junctions to ASK, PHB; output to SMB sublateral motor
    // Creates ASK→AVH→RIR→AIZ/RIA pathway
    // REF: Emmons 2024
    b.neuron("AVHL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("AVHR", NT::INTER, NTT::GLUTAMATE);
    // PVP — highest gap junction degree in entire nervous system
    // Gap junctions: AQR(102!), PQR(26), DVC(54), PVT(31)
    // Chemical output to AVA, AVB, PVC
    // Involved in roaming/dwelling regulation
    // REF: Emmons 2024, Flavell 2020
    b.neuron("PVPL", NT::INTER, NTT::GLUTAMATE);
    b.neuron("PVPR", NT::INTER, NTT::GLUTAMATE);
    // PVR — proprioceptive hub (single, unpaired)
    // Mechanosensory, extension into tail whip
    // Hub of bodywide sensory network with DVA
    // Gap junctions to DVA; output to RIP (pharyngeal regulation)
    // REF: Emmons 2024 — "significance previously unrecognized"
    b.neuron("PVR",  NT::INTER, NTT::GLUTAMATE);

    // --- Motor neurons ---
    // Head motor neurons
    b.neuron("SMDVL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SMDVR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SMDDL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SMDDR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("RMDVL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("RMDVR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("RMDDL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("RMDDR", NT::MOTOR, NTT::ACETYLCHOLINE);
    // Step 102: SIA — head motor neurons (4 quadrant, sublateral)
    // Receive RIB sublateral input + RMG social hub gap junctions
    // RMG↔SIA gap junctions: social feeding modulation (Macosko 2009)
    // Cholinergic; innervate head/neck muscles similar to SMD
    // REF: White 1986, Cook 2019, Gray 2005 J Neurosci (navigation circuit)
    b.neuron("SIADL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SIADR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SIAVL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SIAVR", NT::MOTOR, NTT::ACETYLCHOLINE);
    // Step 102: SIB — head motor neurons (4 quadrant, sublateral)
    // Receive RIB + AIZ input; modulate head oscillation amplitude
    // Cholinergic; parallel to SMB for neck/head movement
    // REF: White 1986, Cook 2019, Gray 2005 J Neurosci
    b.neuron("SIBDL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SIBDR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SIBVL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SIBVR", NT::MOTOR, NTT::ACETYLCHOLINE);
    // Neck motor neurons — klinotaxis effectors (Izquierdo 2015, Yamazaki 2022)
    // SMB controls neck curvature DC bias, independent of SMD head oscillation
    b.neuron("SMBDL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SMBDR", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SMBVL", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("SMBVR", NT::MOTOR, NTT::ACETYLCHOLINE);
    // Ventral cord motor neurons (Step 39: expanded from 3→5-7 per class)
    // REF: White 1986, Haspel 2010 (body segment mapping)
    // DA: dorsal A-class, backward locomotion (DA1-9, all 9)
    // Step 84: expanded from 5→9 (Haspel 2011, Gao 2018 eLife)
    // A-class motor neurons are intrinsic oscillators for backward movement
    // AVA provides descending input via mixed gap junction + chemical synapse
    b.neuron("DA01", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA02", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA03", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA04", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA05", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA06", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA07", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA08", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DA09", NT::MOTOR, NTT::ACETYLCHOLINE);
    // DB: dorsal B-class, forward locomotion (real: DB1-7, all 7)
    b.neuron("DB01", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DB02", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DB03", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DB04", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DB05", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DB06", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("DB07", NT::MOTOR, NTT::ACETYLCHOLINE);
    // VA: ventral A-class, backward locomotion (VA1-12, all 12)
    // Step 84: expanded from 5→12 (Haspel 2011, Gao 2018 eLife)
    b.neuron("VA01", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA02", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA03", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA04", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA05", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA06", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA07", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA08", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA09", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA10", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA11", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VA12", NT::MOTOR, NTT::ACETYLCHOLINE);
    // VB: ventral B-class, forward locomotion (VB1-11, all 11)
    // Step 87: expanded from 7→11 (complete complement)
    // VB transduces proprioceptive signal for forward wave propagation
    // REF: Wen 2012 Neuron — B-type MNs drive forward locomotion via proprioception
    //      Chalfie 1985 — B-type required for forward movement
    b.neuron("VB01", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB02", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB03", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB04", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB05", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB06", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB07", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB08", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB09", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB10", NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VB11", NT::MOTOR, NTT::ACETYLCHOLINE);
    // DD: dorsal D-class, GABAergic cross-inhibition (DD1-6, all 6)
    // Step 86: expanded from 5→6 (complete complement)
    // DD receives cholinergic input via LGC-46, inhibits ventral muscles
    // REF: Shan 2005, White 1986
    b.neuron("DD01", NT::MOTOR, NTT::GABA);
    b.neuron("DD02", NT::MOTOR, NTT::GABA);
    b.neuron("DD03", NT::MOTOR, NTT::GABA);
    b.neuron("DD04", NT::MOTOR, NTT::GABA);
    b.neuron("DD05", NT::MOTOR, NTT::GABA);
    b.neuron("DD06", NT::MOTOR, NTT::GABA);
    // VD: ventral D-class, GABAergic cross-inhibition (VD1-13, all 13)
    // Step 86: expanded from 5→13 (complete complement)
    // VD receives cholinergic input via LGC-46, inhibits dorsal muscles
    // VD→AVA retrograde inhibition via UNC-49 biases toward reward
    // REF: Gao 2015 Nat Commun, White 1986
    b.neuron("VD01", NT::MOTOR, NTT::GABA);
    b.neuron("VD02", NT::MOTOR, NTT::GABA);
    b.neuron("VD03", NT::MOTOR, NTT::GABA);
    b.neuron("VD04", NT::MOTOR, NTT::GABA);
    b.neuron("VD05", NT::MOTOR, NTT::GABA);
    b.neuron("VD06", NT::MOTOR, NTT::GABA);
    b.neuron("VD07", NT::MOTOR, NTT::GABA);
    b.neuron("VD08", NT::MOTOR, NTT::GABA);
    b.neuron("VD09", NT::MOTOR, NTT::GABA);
    b.neuron("VD10", NT::MOTOR, NTT::GABA);
    b.neuron("VD11", NT::MOTOR, NTT::GABA);
    b.neuron("VD12", NT::MOTOR, NTT::GABA);
    b.neuron("VD13", NT::MOTOR, NTT::GABA);
    // Step 31: RIV — omega turn motor neurons (GABAergic, ventral head bend)
    // RIV innervates ventral neck muscles; specifies ventral bias of omega turns
    // REF: Gray 2005 PNAS — RIV ablation reduces omega frequency
    //      Donnelly 2013 — RIV triggers omega via ventral head bend
    b.neuron("RIVL", NT::MOTOR, NTT::GABA);
    b.neuron("RIVR", NT::MOTOR, NTT::GABA);
    // Step 32: AS motor neurons — dorsal-only body wall projections
    // AS receives both AVA and AVB → always active → tonic dorsal bias
    // Breaks dorsal-ventral symmetry; provides background against which
    // RIV must compete → graded omega turns emerge from RIV-AS force balance
    // REF: White 1986 (anatomy), Haspel 2010 (dorsal projection),
    //      Chen 2006 (active during both forward and backward)
    // Step 88: expanded from 7→11 (complete complement)
    // Tolstenkov 2018 eLife: AS MNs asymmetrically excite dorsal muscles
    // + VD neurons, active during both forward and backward locomotion
    // AS→dorsal BWM (excitatory) + AS→VD (ventral inhibition via VD)
    // AS↔AVA gap junctions: electrical feedback to backward PIN (UNC-7)
    b.neuron("AS01", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS02", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS03", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS04", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS05", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS06", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS07", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS08", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS09", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS10", NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("AS11", NT::MOTOR, NTT::GLUTAMATE);
    // Step 33: RME head motor neurons — GABAergic amplitude control
    // RMED/RMEV modulate head bending amplitude via push-pull with SMD
    // RMED innervates VENTRAL head muscles (contralateral!)
    // RMEV innervates DORSAL head muscles (contralateral!)
    // RMEL/RMER omitted: no effect on D/V bending (Huang 2016 eLife)
    // REF: White 1986, Huang 2016 eLife, Jorgensen 2005 WormBook
    b.neuron("RMED", NT::MOTOR, NTT::GABA);
    b.neuron("RMEV", NT::MOTOR, NTT::GABA);
    // Step 24: Pharyngeal nervous system (independent CPG)
    // 20 neurons total, 14 types; we implement the 5 essential types (9 neurons)
    // REF: Albertson & Thomson 1976, Avery (WormBook 2012)
    // MC: excitatory motor neuron, ACh pacemaker → controls pump rate
    // REF: Raizen & Avery 1994 — MC necessary and sufficient for rapid pumping
    //      Song & Avery 2012 eLife — 5-HT activates MC via SER-7
    b.neuron("MCL",  NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("MCR",  NT::MOTOR, NTT::ACETYLCHOLINE);
    // M3: inhibitory motor neuron, Glu → controls relaxation timing
    // REF: Avery 1993 — M3 proprioceptive loop, triggers repolarization
    //      Dent et al — M3 uses glutamate via AVR-15 Cl⁻ channel
    b.neuron("M3L",  NT::MOTOR, NTT::GLUTAMATE);
    b.neuron("M3R",  NT::MOTOR, NTT::GLUTAMATE);
    // M4: motor neuron, controls isthmus peristalsis (food transport)
    // REF: Avery & Horvitz 1987 — M4 essential for growth
    b.neuron("M4",   NT::MOTOR, NTT::ACETYLCHOLINE);
    // I1: pharyngeal interneuron, receives RIP gap junction (somatic↔pharyngeal bridge)
    // REF: Albertson & Thomson 1976 — I1 connects via RIP to extrapharyngeal NS
    b.neuron("I1L",  NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("I1R",  NT::INTER, NTT::ACETYLCHOLINE);
    // RIP: extrapharyngeal neuron, sole bridge to pharyngeal NS via gap junction to I1
    // REF: Albertson & Thomson 1976 — bilateral pair, gap junction to I1
    b.neuron("RIPL", NT::INTER, NTT::ACETYLCHOLINE);
    b.neuron("RIPR", NT::INTER, NTT::ACETYLCHOLINE);
    // Step 27: RIS — sleep-active neuron, single (unpaired) GABAergic + peptidergic
    // REF: Turek 2016 eLife — RIS releases FLP-11 (major sleep inducer, not GABA)
    //      Konietzka 2020 Nat Commun — RIS also functions as locomotion stop neuron
    //      Maluck 2023 PLOS Genetics — RIS promotes survival independently of sleep
    b.neuron("RIS",  NT::INTER, NTT::GABA);
    // Step 56: AVL — enteric motor neuron, GABAergic (single, unpaired)
    // Cell body in head, axon runs full ventral cord to tail
    // Fires compound action potentials: UNC-2 (CaV2) Ca²⁺ spike + EXP-2 K⁺ repolarization
    // Drives aBoc (non-GABA) + Exp/EMC (GABA → EXP-1 excitatory receptor on enteric muscles)
    // Partially redundant with DVB for EMC, non-redundant for aBoc
    // REF: McIntire 1993 — AVL+DVB control EMC; Thomas 1990 — DMP genetics
    //      Jiang 2022 Nat Commun — AVL fires action potentials
    b.neuron("AVL",  NT::MOTOR, NTT::GABA);
    // Step 56: DVB — enteric motor neuron, GABAergic (single, unpaired)
    // Cell body in dorsorectal ganglion (tail), NMJ to anal depressor
    // Fires synchronized APs with AVL via INX-1 gap junction
    // Also contains FLRFamide neuropeptide (second transmitter for residual EMC)
    // REF: McIntire 1993, Jiang 2022 Nat Commun
    b.neuron("DVB",  NT::MOTOR, NTT::GABA);
    // Step 38: Egg-laying circuit (Collins 2016 eLife, Schafer 2006)
    // HSN: serotonergic command motor neuron, drives vulval muscle contraction
    // Releases 5-HT + NLP-3 → initiates ~2min active egg-laying state
    // Tyramine feedback via LGC-55 terminates active state (uv1 cells)
    // REF: Waggoner 1998 Neuron, Brewer 2019 PLoS Genetics
    b.neuron("HSNL", NT::MOTOR, NTT::SEROTONIN);
    b.neuron("HSNR", NT::MOTOR, NTT::SEROTONIN);
    // VC4/VC5: cholinergic motor neurons, most proximal to vulva
    // Mechanically activated by vulval muscle contraction → positive feedback
    // REF: Collins 2016 eLife, 2021 J Neurosci — VC facilitates egg release
    b.neuron("VC4",  NT::MOTOR, NTT::ACETYLCHOLINE);
    b.neuron("VC5",  NT::MOTOR, NTT::ACETYLCHOLINE);
}

// ================================================================
// 2. Chemotaxis Circuit — ASE/AWC/AWA/AFD → AIA/AIB/AIY/AIZ
// ================================================================
void build_chemotaxis(CB& b) {
    // Sensory → Interneuron
    // Step 72: ASEL→AIA INHIBITORY (Kakaria 2019 eLife)
    // Glutamate from ASEL activates GLC-3/AVR-14 Cl⁻ channels on AIA → shunting inhibition
    // AIA AND-gate: requires AWA gap junction excitation AND glutamatergic disinhibition
    // ASEL inhibition provides gain control, prevents AIA over-activation
    // REF: Kakaria 2019 eLife, Cook 2019
    b.inh("ASEL", "AIAL", 3); b.syn("ASEL", "AIYL", 3);
    // ASER→AIA/AIY: INHIBITORY (eLife 2024, Matsumoto et al.)
    // ASER releases glutamate → GLC-3 (Cl⁻ channel) on AIY → inhibitory
    // Fixes pirouette modulation: C↓ → ASER↑ → AIA↓ → AIB↑(disinhibited) → more pirouettes
    b.inh("ASER", "AIAR", 2); b.inh("ASER", "AIYR", 2);
    // Step 72: ASE→AIB DIRECT klinokinesis pathway (Kuramochi 2018)
    // ASER→AIB: EXCITATORY (GLR-1 AMPA + mGluR, proximal to AIB soma)
    // C↓ → ASER active → AIB directly excited → reversals (FAST, direct)
    // Cook 2019: ASER→AIB ~7 EM sections; scaled to 3 for model balance
    // ASEL→AIB: INHIBITORY (GLC-3 Cl⁻ channel, distal on AIB neurite)
    // C↑ → ASEL active → AIB directly inhibited → fewer reversals
    // Cook 2019: ASEL→AIB ~3 EM sections; scaled to 2
    // REF: Kuramochi 2018 Front Mol Neurosci, Suzuki 2008 Nature, Cook 2019
    b.syn("ASER", "AIBL", 1); b.syn("ASER", "AIBR", 1);
    b.inh("ASEL", "AIBL", 1); b.inh("ASEL", "AIBR", 1);
    b.syn("AWCL", "AIBL", 4); b.syn("AWCL", "AIYL", 6);
    b.syn("AWCR", "AIBR", 4); b.syn("AWCR", "AIYR", 6);
    // Step 72: AWA→AIA via GAP JUNCTIONS (Kakaria 2019 eLife)
    // AWA::TeTx (blocks vesicle release): AIA response UNCHANGED → not chemical synapse
    // unc-7/unc-9 innexin mutants: AIA response diminished → gap junction mediated
    // Gap junctions preferentially mediate anterograde flow (AWA→AIA > AIA→AWA)
    // AIA is bistable (-80mV / -20mV, threshold 2-3pA): AWA gj current flips AIA state
    // REF: Kakaria 2019 eLife, Cook 2019
    b.gj("AWAL", "AIAL", 3); b.gj("AWAR", "AIAR", 3);
    // Step 72: AWC→AIA INHIBITORY — disinhibition pathway (Kakaria 2019 eLife)
    // AWC is OFF cell: food present → AWC silent → less glutamate → AIA Cl⁻ channels close
    // → AIA membrane resistance increases → AWA gap junction current sufficient to flip AIA
    // Food absent → AWC active → glutamate → AIA inhibited → AWA gj current shunted
    // This is the disinhibition half of the AIA AND-gate
    // Removing glutamate from AWC+ASE (eat-4 excision) → AIA responds like unc-18 mutants
    // REF: Kakaria 2019 eLife Figure 4F-G
    b.inh("AWCL", "AIAL", 2); b.inh("AWCR", "AIAR", 2);
    // Step 82: AIN — parallel chemotaxis relay (White 1986, Cook 2019)
    // Creates ASE→AIN→AIY pathway parallel to direct ASE→AIY
    // Strengthens chemotaxis signal; AIN loss reduces CI efficiency
    // ASE→AIN: chemosensory input (excitatory, glutamatergic)
    b.syn("ASEL", "AINL", 2); b.syn("ASER", "AINR", 2);
    // AWC→AIN: olfactory input (excitatory)
    b.syn("AWCL", "AINL", 1); b.syn("AWCR", "AINR", 1);
    // AIN→AIY: forward drive relay (excitatory)
    b.syn("AINL", "AIYL", 2); b.syn("AINR", "AIYR", 2);
    // AIN→RIA: head motor relay (excitatory)
    b.syn("AINL", "RIAL", 1); b.syn("AINR", "RIAR", 1);
    // AIN L↔R: bilateral coupling
    b.gj("AINL", "AINR", 2);
    // ASE↔AIN: electrical coupling (Cook 2019)
    b.gj("ASEL", "AINL", 1); b.gj("ASER", "AINR", 1);

    // Step 23: Thermotaxis circuit — AFD→AIY (Mori & Ohshima 1995)
    // AFD is the primary thermosensory neuron, AIY is the shared integration node
    // AFD→AIY: excitatory, 5 sections (strengthened to compete with chemotaxis)
    // Cook 2019: ~3 EM sections; boosted for functional thermotaxis in MVP
    // This shares the AIY→RIA→SMD downstream pathway with chemotaxis (ASE→AIA→AIY)
    b.syn("AFDL", "AIYL", 5); b.syn("AFDR", "AIYR", 5);
    // AFD→AIZ: weaker connection, contributes to cryophilic behavior
    // REF: Mori 1995 — AIZ ablation → thermophilic (loses cold-seeking)
    b.syn("AFDL", "AIZL", 2); b.syn("AFDR", "AIZR", 2);
}

// ================================================================
// 3. Touch & Nociception Circuit
// ================================================================
void build_touch_nociception(CB& b) {
    // Touch circuit (Chalfie et al. 1985)
    // Principle: touch cells form gap junctions with AGONIST interneurons,
    //           chemical (inhibitory) synapses with ANTAGONIST interneurons.
    // AVD → AVA excitatory relay (signal from touch → backward command)
    // Step 42: Cook 2019 weights: AVDL→AVAL=37, AVDR→AVAR=52
    // Conservative: 1→2 (full Cook scaling overdrives AVA tonic level)
    b.syn("AVDL", "AVAL", 2); b.syn("AVDR", "AVAR", 2);
    // ALM ⊣ AVB: anterior touch inhibits forward (antagonist)
    b.inh("ALML", "AVBL", 3); b.inh("ALMR", "AVBR", 3);
    // PLM ⊣ AVA/AVD: posterior touch inhibits backward (antagonist)
    b.inh("PLML", "AVAL", 3); b.inh("PLMR", "AVAR", 3);
    b.inh("PLML", "AVDL", 2); b.inh("PLMR", "AVDR", 2);
    // ALM → AVD: anterior touch excites backward interneuron (gap junction)
    b.gj("ALML", "AVDL", 4); b.gj("ALMR", "AVDR", 4);

    // Nose touch / nociception → reverse
    // ASH→AVA: 3 sections provides strong avoidance drive when repellent is present
    b.syn("ASHL", "AVAL", 3); b.syn("ASHR", "AVAR", 3);
    b.syn("ASHL", "AVDL", 3); b.syn("ASHR", "AVDR", 3);
    // Step 25: ASH nociceptive avoidance circuit (Cook 2019, Summers 2015)
    // ASH→AIB: glutamatergic excitatory via GLR-1 (AMPA-like)
    b.syn("ASHL", "AIBL", 3); b.syn("ASHR", "AIBR", 3);
    // ASH→RIM: nociceptive activation of RIM (promotes omega turns)
    b.syn("ASHL", "RIML", 1); b.syn("ASHR", "RIMR", 1);

    // Step 73: FLP nose touch avoidance (29% of response, Kaplan 1993)
    // FLP is presynaptic to AVA, AVD, AVE, AIB (reversal-promoting interneurons)
    // Optogenetic activation of FLP is sufficient to trigger reversals
    // MEC-10 (DEG/ENaC) for harsh touch; gentle nose touch via RIH facilitation
    // FLP→AVA: 2 sections (scaled from ASH 3 sections × 29/45 ≈ 2)
    // REF: Kaplan 1993, Chatzigeorgiou 2011, PMC8601619
    b.syn("FLPL", "AVAL", 2); b.syn("FLPR", "AVAR", 2);
    b.syn("FLPL", "AVDL", 2); b.syn("FLPR", "AVDR", 2);
    b.syn("FLPL", "AVEL", 1); b.syn("FLPR", "AVER", 1);
    b.syn("FLPL", "AIBL", 1); b.syn("FLPR", "AIBR", 1);

    // Step 43: AWB repulsive olfactory circuit (learned reflexive aversion)
    // AWB↔AUA: ELECTRICAL synapse (gap junction), not chemical
    // REF: Filipowicz 2022 BMC Biology — "AWB electrically synapses onto AUA and RMG"
    b.gj("AWBL", "AUAL", 2); b.gj("AWBR", "AUAR", 2);

    // Step 75: AWB↔RMG gap junctions — parallel pathogen aversion pathway
    // Filipowicz 2022: "AWB electrically synapses onto AUA AND RMG interneurons"
    // Ablation of RMG eliminates motor neuron oscillations → both AUA+RMG required
    // RMG is also the social feeding hub (NPR-1 modulated, de Bono 2002)
    // In N2 (npr-1 215V), RMG hub is suppressed for aggregation but still functional
    //   for pathogen aversion — different activation threshold
    // REF: Filipowicz 2022 BMC Biology, Macosko 2009 Nature
    b.gj("AWBL", "RMGL", 2); b.gj("AWBR", "RMGR", 2);

    // Step 96: RMG hub-and-spoke gap junction network (Macosko 2009 Nature, Fig 3a)
    // RMG is a hub interneuron connected by gap junctions to 7 classes of sensory neurons.
    // These gap junctions amplify/propagate sensory signals when RMG is active (npr-1 lf).
    // In N2 (npr-1 215V gof), NPR-1 tonically inhibits RMG → hub is dampened → solitary.
    // Hall & Bhatt 2017 Dev Neurobiol: neuronal gj are small/low-conductance innexin channels
    // REF: Macosko 2009 Nature, de Bono 2002, Busch 2012, Fenk & de Bono 2015,
    //      Hall & Bhatt 2017 Dev Neurobiol (gap junction review)
    b.gj("URXL", "RMGL", 2); b.gj("URXR", "RMGR", 2);  // O₂→aggregation
    b.gj("ASKL", "RMGL", 1); b.gj("ASKR", "RMGR", 1);  // pheromone→attraction
    b.gj("ADLL", "RMGL", 1); b.gj("ADLR", "RMGR", 1);  // nociception→aversion
    b.gj("ASHL", "RMGL", 1); b.gj("ASHR", "RMGR", 1);  // polymodal→amplification
    // Completing 7 spoke classes (Macosko 2009 Fig 3a: URX,ASH,ADL,ASK,AWB,IL2,AUA)
    b.gj("IL2DL", "RMGL", 1); b.gj("IL2DR", "RMGR", 1);  // environmental→nictation/aggregation
    b.gj("IL2VL", "RMGL", 1); b.gj("IL2VR", "RMGR", 1);  // IL2 4 quadrant positions
    b.gj("AUAL", "RMGL", 1); b.gj("AUAR", "RMGR", 1);    // O₂ relay interneuron→hub

    // Step 102: RMG↔SIA gap junctions — social hub → head motor output
    // SIA receives RMG activity via gap junctions → modulates head movement
    // When RMG is active (npr-1 lf, high O₂): SIA receives depolarizing current
    // REF: White 1986 (Table 2), Cook 2019, Macosko 2009
    b.gj("RMGL", "SIADL", 1); b.gj("RMGL", "SIAVL", 1);
    b.gj("RMGR", "SIADR", 1); b.gj("RMGR", "SIAVR", 1);

    // Step 102: RMG peptidergic output → AVB/AIY (forward drive + reversal suppression)
    // Laurent 2015 eLife: "stimulating RMG inhibits reversals and induces rapid forward movement"
    // "peptidergic release from RMG is a major output of the URX and RMG couple"
    // "information about O₂ could flow from URX-RMG to AVB and AIY via neuropeptide secretion"
    // Modeled as weak excitatory synapses (approximation of FLP-21 peptidergic volume transmission)
    // In N2: NPR-1 suppresses RMG → these synapses are negligible
    // In npr-1(lf): RMG active at high O₂ → strong forward drive → social speed burst
    // REF: Laurent 2015 eLife, Macosko 2009 Nature
    b.syn("RMGL", "AVBL", 1); b.syn("RMGR", "AVBR", 1);
    b.syn("RMGL", "AIYL", 1); b.syn("RMGR", "AIYR", 1);

    // Step 102: SIA input circuits (White 1986, Cook 2019)
    // RIB→SIA: major sublateral input, locomotion modulation
    b.syn("RIBL", "SIADL", 2); b.syn("RIBL", "SIAVL", 2);
    b.syn("RIBR", "SIADR", 2); b.syn("RIBR", "SIAVR", 2);
    // RIA→SIA: head turn coordination (thermotaxis/chemotaxis output)
    b.syn("RIAL", "SIADL", 1); b.syn("RIAL", "SIAVL", 1);
    b.syn("RIAR", "SIADR", 1); b.syn("RIAR", "SIAVR", 1);
    // SIA→RMD: head muscle modulation (SIA drives RMD for head positioning)
    b.syn("SIADL", "RMDDL", 1); b.syn("SIAVL", "RMDVL", 1);
    b.syn("SIADR", "RMDDR", 1); b.syn("SIAVR", "RMDVR", 1);

    // Step 102: SIB input circuits (White 1986, Cook 2019)
    // RIB→SIB: major sublateral input (parallel to RIB→SIA)
    b.syn("RIBL", "SIBDL", 2); b.syn("RIBL", "SIBVL", 2);
    b.syn("RIBR", "SIBDR", 2); b.syn("RIBR", "SIBVR", 2);
    // AIZ→SIB: turn promotion → head oscillation amplitude modulation
    b.syn("AIZL", "SIBDL", 1); b.syn("AIZL", "SIBVL", 1);
    b.syn("AIZR", "SIBDR", 1); b.syn("AIZR", "SIBVR", 1);
    // SIB→RMD: head muscle modulation (weaker than SIA→RMD)
    b.syn("SIBDL", "RMDDL", 1); b.syn("SIBVL", "RMDVL", 1);
    b.syn("SIBDR", "RMDDR", 1); b.syn("SIBVR", "RMDVR", 1);

    // Step 103: SAA circuit connections (Emmons 2024, Cook 2019)
    // AIB→SAA: turn decision input (AIB pirouette circuit → SAA sublateral)
    // Community 3 internal: AIB evaluates chemosensory, SAA executes head turn
    b.syn("AIBL", "SAADL", 2); b.syn("AIBL", "SAAVL", 2);
    b.syn("AIBR", "SAADR", 2); b.syn("AIBR", "SAAVR", 2);
    // RIB→SAA: sublateral locomotion input (major sublateral pathway)
    b.syn("RIBL", "SAADL", 2); b.syn("RIBL", "SAAVL", 2);
    b.syn("RIBR", "SAADR", 2); b.syn("RIBR", "SAAVR", 2);
    // SAA→AVA: backward command input — UNIQUE among sublateral motor neurons
    // "SAA neurons are a major source of input to AVA" (Emmons 2024)
    // This creates turn→reversal pathway: AIB→SAA→AVA
    b.syn("SAADL", "AVAL", 2); b.syn("SAAVL", "AVAL", 2);
    b.syn("SAADR", "AVAR", 2); b.syn("SAAVR", "AVAR", 2);
    // SAA→RMD: head positioning (ipsilateral quadrant, like SIA→RMD)
    b.syn("SAADL", "RMDDL", 1); b.syn("SAAVL", "RMDVL", 1);
    b.syn("SAADR", "RMDDR", 1); b.syn("SAAVR", "RMDVR", 1);
    // SAA↔SMB: gap junctions — turn circuit coordination
    // "RIV, SAA, and SMB are part of a turn circuit that inhibits reversals" (Emmons 2024)
    b.gj("SAADL", "SMBDL", 1); b.gj("SAADR", "SMBDR", 1);
    b.gj("SAAVL", "SMBVL", 1); b.gj("SAAVR", "SMBVR", 1);

    // Step 75: RMG → command interneurons — drives backward locomotion
    // Filipowicz 2022: "AUA and RMG synapse onto motor command interneurons
    //   to control backward locomotion motor neurons"
    // RMG→AVA: primary backward command (reversal initiation)
    // RMG→AVD: secondary backward command (reversal support)
    // REF: Filipowicz 2022 BMC Biology, Cook 2019 (WormWiring)
    b.syn("RMGL", "AVAL", 1); b.syn("RMGR", "AVAR", 1);
    b.syn("RMGL", "AVDL", 1); b.syn("RMGR", "AVDR", 1);

    // Step 75: AUA → AVD — parallel backward command for pathogen aversion
    // AUA→AVA already exists (0.3 sections, Step 34 O₂ relay)
    // Adding AUA→AVD completes the dual-pathway drive to backward command
    // REF: Filipowicz 2022, Cook 2019
    b.syn("AUAL", "AVDL", 0.5); b.syn("AUAR", "AVDR", 0.5);

    // Step 81: PHB — tail repellent sensing → SUPPRESS reversal (directional escape)
    // Hilliard 2002: PHB "negatively modulate reversals to repellents"
    // Head ASH excites AVA → reversal; tail PHB INHIBITS AVA → suppresses reversal
    // This creates directional escape: repellent ahead = reverse, behind = continue forward
    // PHB glutamate → GLC-3 Cl⁻ channel on AVA (inhibitory, like ASEL→AIA)
    // Cook 2019: PHBL→AVAL ~4 EM sections, PHBR→AVAR ~4 EM sections
    // REF: Hilliard 2002 Curr Biol, Cook 2019
    b.inh("PHBL", "AVAL", 3); b.inh("PHBR", "AVAR", 3);
    // PHB → PVC: tail repellent promotes forward locomotion (escape forward)
    // Cook 2019: PHB→PVC ~2 EM sections
    b.syn("PHBL", "PVCL", 2); b.syn("PHBR", "PVCR", 2);
    // PHB → AVD: weak modulation of backward command relay
    b.syn("PHBL", "AVDL", 1); b.syn("PHBR", "AVDR", 1);
    // PHB bilateral and PHB↔PHA gap junctions
    b.gj("PHBL", "PHBR", 2);
    b.gj("PHBL", "PHAL", 1); b.gj("PHBR", "PHAR", 1);
    // PHB ↔ AVH: gap junction (Cook 2019, Emmons 2024 — sensory bridge)
    b.gj("PHBL", "AVHL", 1); b.gj("PHBR", "AVHR", 1);

    // Step 81: PHA — tail chemosensory (dauer pheromone + food quality)
    // PHA has different downstream targets from PHB
    // PHA → AVD: weak excitation of backward command
    // Cook 2019: PHA→AVD ~1 EM section
    b.syn("PHAL", "AVDL", 1); b.syn("PHAR", "AVDR", 1);
    // PHA → AVH: pheromone → sensory bridge (Cook 2019)
    b.syn("PHAL", "AVHL", 1); b.syn("PHAR", "AVHR", 1);
    // PHA bilateral coupling
    b.gj("PHAL", "PHAR", 2);

    // Step 83: LUA — tail sensory relay interneuron
    // Integrates PHB (repellent) + PLM (touch) → command neurons
    // PHB→LUA: tail repellent relay (Cook 2019: ~3 EM sections)
    b.syn("PHBL", "LUAL", 2); b.syn("PHBR", "LUAR", 2);
    // PLM→LUA: posterior touch relay (Cook 2019: ~2 EM sections)
    b.syn("PLML", "LUAL", 2); b.syn("PLMR", "LUAR", 2);
    // LUA→AVD: backward command relay (tail sensory → reversal)
    b.syn("LUAL", "AVDL", 2); b.syn("LUAR", "AVDR", 2);
    // LUA→PVC: forward command relay (tail sensory → forward escape)
    b.syn("LUAL", "PVCL", 1); b.syn("LUAR", "PVCR", 1);
    // LUA bilateral coupling
    b.gj("LUAL", "LUAR", 2);
    // LUA↔PHB: gap junction (local tail circuit, Cook 2019)
    b.gj("LUAL", "PHBL", 1); b.gj("LUAR", "PHBR", 1);

    // Step 83: AVF — second forward command interneuron
    // Emmons 2024: "AVF collects input... directs output to... AVB"
    // PHA→AVF: tail food/pheromone → forward drive
    b.syn("PHAL", "AVFL", 2); b.syn("PHAR", "AVFR", 2);
    // PVC→AVF: forward command relay (strengthens forward drive)
    b.syn("PVCL", "AVFL", 2); b.syn("PVCR", "AVFR", 2);
    // AVF→AVB: second forward command output (parallel to PVC→AVB)
    b.syn("AVFL", "AVBL", 3); b.syn("AVFR", "AVBR", 3);
    // AVF bilateral coupling
    b.gj("AVFL", "AVFR", 2);
    // AVF↔PVQ: gap junction (Emmons 2024: ventral cord coupling)
    // PVQ not in model yet, skip
}

// ================================================================
// 3b. Phototaxis / Light Avoidance Circuit (Step 55)
// ================================================================
void build_phototaxis(CB& b) {
    // ASJ: primary photoreceptor → downstream interneurons
    // Cook 2019: ASJ→AIA(2), ASJ→AIB(1), ASJ→RIA(2)
    // ASJ detects UV/blue light via LITE-1 → CNG channel depolarization
    // ASJ→AIA: excitatory → AIA ⊣ AIB → suppresses reversal (indirect path)
    // ASJ→AIB: excitatory → AIB → AVA → promotes reversal (direct path)
    // ASJ→RIA: excitatory → RIA → SMD → head motor (steering away from light)
    // Net effect: ASJ light → AIB dominates → reversal + reorientation
    // REF: Ward 2008 Nat Neurosci, Liu 2010, Cook 2019
    b.syn("ASJL", "AIAL", 2); b.syn("ASJR", "AIAR", 2);
    b.syn("ASJL", "AIBL", 1); b.syn("ASJR", "AIBR", 1);
    b.syn("ASJL", "RIAL", 2); b.syn("ASJR", "RIAR", 2);

    // ASK: secondary photoreceptor → interneurons
    // Cook 2019: ASK→AIA(3), ASK→AIB(1), ASK→AIY(2)
    // ASK also senses pheromones (osas#9 avoidance) — multifunctional
    // REF: Liu 2010, eLife 2025 LITE-1 chemoreceptor
    b.syn("ASKL", "AIAL", 2); b.syn("ASKR", "AIAR", 2);
    b.syn("ASKL", "AIBL", 1); b.syn("ASKR", "AIBR", 1);
    b.syn("ASKL", "AIYL", 1); b.syn("ASKR", "AIYR", 1);

    // AWB: volatile repellent + photosensitive (LITE-1 expressed)
    // NEW connections for light circuit — AWB→AIB (reversal), AWB→AIZ
    // Cook 2019: AWB→AIZ(3), AWB↔AIB gap junction exists but chemical→AIB too
    // REF: Liu 2010 — ASJ+ASK+AWB+ASH mediate light avoidance combinatorially
    b.syn("AWBL", "AIZL", 1); b.syn("AWBR", "AIZR", 1);

    // Gap junctions: photosensory neuron coupling
    // ASJ ↔ ASK: co-labeled in amphid, share sensory environment
    // REF: Cook 2019 — ASJ↔ASK gap junction
    b.gj("ASJL", "ASKL", 2); b.gj("ASJR", "ASKR", 2);
    // ASJ L↔R + ASK L↔R: bilateral symmetry coupling
    b.gj("ASJL", "ASJR", 1);
    b.gj("ASKL", "ASKR", 1);
}

// ================================================================
// 4. Interneuron Layer — AIA/AIB/AIY/RIB relay
// ================================================================
void build_interneuron(CB& b) {
    // AIA ⊣ AIB: inhibitory — suppresses pirouettes when ON chemosensory active
    // REF: Chalasani 2007 — AIA inhibits AIB, critical for pirouette suppression
    b.inh("AIAL", "AIBL", 5); b.inh("AIAR", "AIBR", 5);
    // Step 42: Cook 2019 weights: AIBL→AVAL=5, AIBR→AVAR=2
    b.syn("AIBL", "AVAL", 2); b.syn("AIBR", "AVAR", 1);
    // Step 42: Cook 2019 weights: AIYL→RIAL=51, AIYR→RIAR=50 (scale ÷10)
    b.syn("AIYL", "RIAR", 5); b.syn("AIYR", "RIAL", 5);
    // Step 42: Cook 2019 weights: AIYL→AIZL=67, AIYR→AIZR=70
    b.syn("AIYL", "AIZL", 3); b.syn("AIYR", "AIZR", 3);
    // AIY → AVB: promotes forward locomotion
    // REF: Gray 2005 — AIY ablation reduces forward runs
    b.syn("AIYL", "AVBL", 3); b.syn("AIYR", "AVBR", 3);
    // AVB drive pathway: AIY → AVB (TD-01, additional excitatory interneuron drive)
    // REF: White 1986, WormAtlas
    b.syn("AIYL", "AVBL", 3); b.syn("AIYR", "AVBR", 3);
    // AIB → RIM: activates RIM during reversals (reversal signal relay)
    b.syn("AIBL", "RIML", 3); b.syn("AIBR", "RIMR", 3);
    // RIB → AVB (additional forward drive)
    b.syn("RIBL", "AVBL", 2); b.syn("RIBR", "AVBR", 2);
}

// ================================================================
// 5. Head Motor Oscillator — SMD/RMD/RME/SMB
// ================================================================
void build_head_motor(CB& b) {
    // RIA → SMD
    b.syn("RIAL", "SMDVL", 4); b.syn("RIAR", "SMDVR", 4);
    b.syn("RIAL", "SMDDL", 3); b.syn("RIAR", "SMDDR", 3);

    // Step 28: SMD → RIA feedback (ACh via GAR-3 muscarinic receptor)
    // REF: Hendricks 2012 Nature — motor-correlated compartmentalized Ca²⁺
    b.comp("SMDDL", "RIAL", 1, 2);  // SMDDL → RIAL nrD
    b.comp("SMDDR", "RIAR", 1, 2);  // SMDDR → RIAR nrD
    b.comp("SMDVL", "RIAL", 1, 1);  // SMDVL → RIAL nrV
    b.comp("SMDVR", "RIAR", 1, 1);  // SMDVR → RIAR nrV

    // Dorsal-ventral cross-inhibition → half-center oscillator
    // Step 19: reduced from 8→3 sections so oscillator is sensitive to weathervane bias
    b.inh("SMDDL", "SMDVL", 3); b.inh("SMDDR", "SMDVR", 3);
    b.inh("SMDVL", "SMDDL", 3); b.inh("SMDVR", "SMDDR", 3);
    // RMD dorsal-ventral cross-inhibition
    b.inh("RMDDL", "RMDVL", 6); b.inh("RMDDR", "RMDVR", 6);
    b.inh("RMDVL", "RMDDL", 6); b.inh("RMDVR", "RMDDR", 6);
    // SMD → RMD excitatory (same side, co-activate dorsal or ventral)
    b.syn("SMDDL", "RMDDL", 3); b.syn("SMDDR", "RMDDR", 3);
    b.syn("SMDVL", "RMDVL", 3); b.syn("SMDVR", "RMDVR", 3);

    // Step 19 Phase 2: Klinotaxis pathway — AIZ → SMB (Izquierdo 2015, Yamazaki 2022)
    b.syn("AIZL", "SMBDL", 4);
    b.syn("AIZR", "SMBVR", 4);
    // SMB dorsal-ventral cross-inhibition
    b.inh("SMBDL", "SMBVL", 3); b.inh("SMBDR", "SMBVR", 3);
    b.inh("SMBVL", "SMBDL", 3); b.inh("SMBVR", "SMBDR", 3);

    // Step 33: RME + OLQ
    // SMD →(extrasynaptic) RME: cholinergic volume transmission via GAR-2
    // REF: Huang 2016 eLife — SMD ACh → GAR-2 muscarinic on RME
    b.syn("SMDDL", "RMED", 0.3); b.syn("SMDDR", "RMED", 0.3);
    b.syn("SMDVL", "RMEV", 0.3); b.syn("SMDVR", "RMEV", 0.3);
    // RME ⊣(GABAB) SMD: extrasynaptic GABA → GBB-1/2 on SMD
    // REF: Huang 2016 eLife — GABAB GBB-1/2 on SMD restrains head bending
    b.inh("RMED", "SMDVL", 0.3); b.inh("RMED", "SMDVR", 0.3);
    b.inh("RMEV", "SMDDL", 0.3); b.inh("RMEV", "SMDDR", 0.3);
    // RIA → RME: direct chemical synapse (White 1986 connectome)
    b.syn("RIAL", "RMED", 1); b.syn("RIAL", "RMEV", 1);
    b.syn("RIAR", "RMED", 1); b.syn("RIAR", "RMEV", 1);
    // OLQ → RMD: head withdrawal reflex (Hart 1995)
    b.syn("OLQDL", "RMDDL", 1); b.syn("OLQDR", "RMDDR", 1);
    b.syn("OLQVL", "RMDVL", 1); b.syn("OLQVR", "RMDVR", 1);
    // OLQ → RIC: indirect path to AVA
    b.syn("OLQDL", "RICL", 1); b.syn("OLQDR", "RICR", 1);
    b.syn("OLQVL", "RICL", 1); b.syn("OLQVR", "RICR", 1);
    // OLQ ↔ CEP: gap junction coupling with dopaminergic mechanosensory
    b.gj("OLQDL", "CEPDL", 1); b.gj("OLQDR", "CEPDR", 1);
    b.gj("OLQVL", "CEPVL", 1); b.gj("OLQVR", "CEPVR", 1);

    // Step 73: Hub-and-spoke gap junction network for nose touch coincidence detection
    // Hub: RIH (single unpaired interneuron)
    // Spokes: FLP, OLQ, CEP — all connected to RIH via gap junctions
    // Function: active spokes facilitate FLP nose touch response;
    //   inactive spokes suppress via shunting (Rabinowitch 2013 Curr Biol)
    // FLP harsh touch is cell-autonomous (MEC-10); gentle nose touch needs facilitation
    // REF: Chatzigeorgiou & Schafer 2011 Neuron, Rabinowitch 2013 Curr Biol
    b.gj("FLPL", "RIH", 2); b.gj("FLPR", "RIH", 2);
    b.gj("OLQDL", "RIH", 1); b.gj("OLQDR", "RIH", 1);
    b.gj("OLQVL", "RIH", 1); b.gj("OLQVR", "RIH", 1);
    b.gj("CEPDL", "RIH", 1); b.gj("CEPDR", "RIH", 1);
    b.gj("CEPVL", "RIH", 1); b.gj("CEPVR", "RIH", 1);

    // Step 73: IL1 → RMD — head withdrawal reflex (ipsilateral)
    // IL1 + OLQ together mediate head withdrawal (Hart 1995)
    // OLQ ablation: majority of response lost; IL1: remainder
    // Ablation of OLQ+IL1 → abnormally slow foraging + exaggerated nose turns
    // IL1→RMD: glutamatergic via GLR-1 (glr-1 mutants defective for head withdrawal)
    // REF: Hart et al. 1995, White 1986, Emmons 2024 PLOS Biology
    b.syn("IL1DL", "RMDDL", 2); b.syn("IL1DR", "RMDDR", 2);
    b.syn("IL1VL", "RMDVL", 2); b.syn("IL1VR", "RMDVR", 2);
    // IL1 ↔ RIH: gap junction integration for foraging regulation
    b.gj("IL1DL", "RIH", 1); b.gj("IL1DR", "RIH", 1);
    b.gj("IL1VL", "RIH", 1); b.gj("IL1VR", "RIH", 1);

    // Step 105: IL1/IL2 → URA — inner labial motor pathway for foraging
    // "IL1 and IL2 target body wall muscles in the head via URA" (Emmons 2024)
    // IL1→URA: nose touch/tactile → head positioning (ipsilateral quadrant)
    b.syn("IL1DL", "URADL", 2); b.syn("IL1DR", "URADR", 2);
    b.syn("IL1VL", "URAVL", 2); b.syn("IL1VR", "URAVR", 2);
    // IL2→URA: inner labial chemical/environmental → head positioning
    b.syn("IL2DL", "URADL", 1); b.syn("IL2DR", "URADR", 1);
    b.syn("IL2VL", "URAVL", 1); b.syn("IL2VR", "URAVR", 1);
    // URA→RMD: coordinate head muscle contraction (nose positioning)
    b.syn("URADL", "RMDDL", 1); b.syn("URADR", "RMDDR", 1);
    b.syn("URAVL", "RMDVL", 1); b.syn("URAVR", "RMDVR", 1);

    // Step 60: Dopaminergic mechanosensory circuit (ADE + PDE)
    // ADE → RIC: DA from head deirid → octopaminergic interneuron
    // ADE senses bacteria alongside CEP; projects to nerve ring
    // REF: Cook 2019, White 1986 — ADE synapses in nerve ring
    b.syn("ADEL", "RICL", 1); b.syn("ADER", "RICR", 1);
    // ADE L↔R: bilateral gap junction coupling (coordinate bilateral DA release)
    b.gj("ADEL", "ADER", 1);
    // PDE → DVA: posterior mechanosensory DA → proprioceptive interneuron
    // Complements CEP→DOP-1→DVA pathway for NLP-12 priming
    // REF: White 1986, Cook 2019 — PDE chemical synapses
    b.syn("PDEL", "DVA", 1); b.syn("PDER", "DVA", 1);
    // PDE → PVC: posterior DA → forward command (food-contact forward drive)
    b.syn("PDEL", "PVCL", 1); b.syn("PDER", "PVCR", 1);
    // PDE ↔ PVD: gap junction — mechanosensory DA + harsh touch proprioception
    // PDE and PVD both innervate body wall; shared mechanosensory context
    // REF: White 1986, Cook 2019 — PDE-PVD gap junctions
    b.gj("PDEL", "PVDL", 2); b.gj("PDER", "PVDR", 2);
    // PDE L↔R: bilateral coupling
    b.gj("PDEL", "PDER", 1);
}

// ================================================================
// 6. Command Interneurons & Ventral Cord Motor
// ================================================================
void build_command_ventral(CB& b) {
    // Step 37: AVE backward command — reversal grading + omega gating
    // AIB → AVE: chemosensory relay → backward command (weaker than AIB→AVA)
    b.syn("AIBL", "AVEL", 1); b.syn("AIBR", "AVER", 1);
    // ASH → AVE: nociception direct → committed reversal
    b.syn("ASHL", "AVEL", 2); b.syn("ASHR", "AVER", 2);
    // AVE → DA: backward motor neuron drive (Step 84: expanded to all 9 DA neurons)
    b.syn("AVEL", "DA01", 1); b.syn("AVER", "DA02", 1); b.syn("AVEL", "DA03", 1);
    b.syn("AVER", "DA04", 1); b.syn("AVEL", "DA05", 1);
    b.syn("AVER", "DA06", 1); b.syn("AVEL", "DA07", 1);
    b.syn("AVER", "DA08", 1); b.syn("AVEL", "DA09", 1);
    // AVE → RIM: additional reversal input
    b.syn("AVEL", "RIML", 2); b.syn("AVER", "RIMR", 2);

    // Command → Motor (Step 84: expanded to full A-class complement)
    // AVA → A-class (backward): anterior stronger, posterior weaker (gradient)
    // Gao 2018 eLife: AVA provides descending input via mixed gj + chemical synapse
    // Haspel 2011: DA innervated by AVA, AVD, AVE; activated during backward
    b.syn("AVAL", "DA01", 5); b.syn("AVAL", "DA02", 4); b.syn("AVAL", "DA03", 3);
    b.syn("AVAL", "DA04", 2); b.syn("AVAL", "DA05", 2);
    b.syn("AVAL", "DA06", 2); b.syn("AVAL", "DA07", 1);
    b.syn("AVAL", "DA08", 1); b.syn("AVAL", "DA09", 1);
    b.syn("AVAL", "VA01", 4); b.syn("AVAL", "VA02", 3); b.syn("AVAL", "VA03", 3);
    b.syn("AVAL", "VA04", 2); b.syn("AVAL", "VA05", 2);
    b.syn("AVAL", "VA06", 2); b.syn("AVAL", "VA07", 1); b.syn("AVAL", "VA08", 1);
    b.syn("AVAL", "VA09", 1); b.syn("AVAL", "VA10", 1);
    b.syn("AVAL", "VA11", 1); b.syn("AVAL", "VA12", 1);
    b.syn("AVAR", "DA01", 5); b.syn("AVAR", "DA02", 4); b.syn("AVAR", "DA03", 3);
    b.syn("AVAR", "DA04", 2); b.syn("AVAR", "DA05", 2);
    b.syn("AVAR", "DA06", 2); b.syn("AVAR", "DA07", 1);
    b.syn("AVAR", "DA08", 1); b.syn("AVAR", "DA09", 1);
    b.syn("AVAR", "VA01", 4); b.syn("AVAR", "VA02", 3); b.syn("AVAR", "VA03", 3);
    b.syn("AVAR", "VA04", 2); b.syn("AVAR", "VA05", 2);
    b.syn("AVAR", "VA06", 2); b.syn("AVAR", "VA07", 1); b.syn("AVAR", "VA08", 1);
    b.syn("AVAR", "VA09", 1); b.syn("AVAR", "VA10", 1);
    b.syn("AVAR", "VA11", 1); b.syn("AVAR", "VA12", 1);
    // AVB → B-class (forward): anterior stronger, posterior weaker
    b.syn("AVBL", "DB01", 5); b.syn("AVBL", "DB02", 4); b.syn("AVBL", "DB03", 3);
    b.syn("AVBL", "DB04", 3); b.syn("AVBL", "DB05", 2); b.syn("AVBL", "DB06", 2);
    b.syn("AVBL", "DB07", 1);
    b.syn("AVBL", "VB01", 4); b.syn("AVBL", "VB02", 3); b.syn("AVBL", "VB03", 3);
    b.syn("AVBL", "VB04", 2); b.syn("AVBL", "VB05", 2); b.syn("AVBL", "VB06", 2);
    b.syn("AVBL", "VB07", 1);
    // Step 87: VB08-11 posterior B-class (gradient continues)
    b.syn("AVBL", "VB08", 1); b.syn("AVBL", "VB09", 1);
    b.syn("AVBL", "VB10", 1); b.syn("AVBL", "VB11", 1);
    b.syn("AVBR", "DB01", 5); b.syn("AVBR", "DB02", 4); b.syn("AVBR", "DB03", 3);
    b.syn("AVBR", "DB04", 3); b.syn("AVBR", "DB05", 2); b.syn("AVBR", "DB06", 2);
    b.syn("AVBR", "DB07", 1);
    // D-type cross inhibition (Step 86: expanded to full DD6 ↔ VD13)
    // DD and VD form reciprocal inhibitory pairs based on segment overlap
    // Each DD overlaps ~2 VDs; cross-inhibition preserves D/V phase
    // REF: White 1986, Shan 2005 Dev Biol
    b.syn("DD01", "VD01", 3); b.syn("DD01", "VD02", 2);
    b.syn("DD02", "VD03", 3); b.syn("DD02", "VD04", 2);
    b.syn("DD03", "VD05", 3); b.syn("DD03", "VD06", 2);
    b.syn("DD04", "VD07", 3); b.syn("DD04", "VD08", 2);
    b.syn("DD05", "VD09", 3); b.syn("DD05", "VD10", 2);
    b.syn("DD06", "VD11", 3); b.syn("DD06", "VD12", 2); b.syn("DD06", "VD13", 2);
    b.syn("VD01", "DD01", 3); b.syn("VD02", "DD01", 2);
    b.syn("VD03", "DD02", 3); b.syn("VD04", "DD02", 2);
    b.syn("VD05", "DD03", 3); b.syn("VD06", "DD03", 2);
    b.syn("VD07", "DD04", 3); b.syn("VD08", "DD04", 2);
    b.syn("VD09", "DD05", 3); b.syn("VD10", "DD05", 2);
    b.syn("VD11", "DD06", 3); b.syn("VD12", "DD06", 2); b.syn("VD13", "DD06", 2);
    // Step 86: VD → AVA retrograde inhibition via UNC-49 GABA receptor
    // D-MNs bias threat-reward decision toward reward by inhibiting AVA
    // REF: Gao 2015 Nat Commun — VD5/6→AVAL(1), VD5/11/13→AVAR
    b.inh("VD05", "AVAL", 1); b.inh("VD06", "AVAL", 1);
    b.inh("VD05", "AVAR", 1); b.inh("VD11", "AVAR", 2); b.inh("VD13", "AVAR", 2);

    // Step 32: AS motor neuron circuit (White 1986, Haspel 2010, Chen 2006)
    // AVA → AS (Tolstenkov 2018: AS receives from both forward and backward PINs)
    b.syn("AVAL", "AS01", 1); b.syn("AVAR", "AS02", 1);
    b.syn("AVAL", "AS03", 1); b.syn("AVAR", "AS04", 1); b.syn("AVAL", "AS05", 1);
    b.syn("AVAR", "AS06", 1); b.syn("AVAL", "AS07", 1);
    // Step 88: AS08-11 (alternating L/R pattern continues)
    b.syn("AVAR", "AS08", 1); b.syn("AVAL", "AS09", 1);
    b.syn("AVAR", "AS10", 1); b.syn("AVAL", "AS11", 1);
    // AVB → AS
    b.syn("AVBL", "AS01", 1); b.syn("AVBR", "AS02", 1);
    b.syn("AVBL", "AS03", 1); b.syn("AVBR", "AS04", 1); b.syn("AVBL", "AS05", 1);
    b.syn("AVBR", "AS06", 1); b.syn("AVBL", "AS07", 1);
    // Step 88: AVB→AS08-11
    b.syn("AVBR", "AS08", 1); b.syn("AVBL", "AS09", 1);
    b.syn("AVBR", "AS10", 1); b.syn("AVBL", "AS11", 1);
    // DD ⊣ AS: GABAergic cross-inhibition during ventral phase
    b.inh("DD01", "AS01", 1); b.inh("DD01", "AS02", 1);
    b.inh("DD02", "AS03", 1); b.inh("DD02", "AS04", 1);
    b.inh("DD03", "AS04", 1); b.inh("DD03", "AS05", 1);
    b.inh("DD04", "AS05", 1); b.inh("DD04", "AS06", 1);
    b.inh("DD05", "AS06", 1); b.inh("DD05", "AS07", 1);
    b.inh("DD06", "AS07", 1); // Step 86: DD06 posterior overlap
    // Step 88: DD⊣AS08-11 (DD05/DD06 cover posterior segments)
    b.inh("DD05", "AS08", 1); // DD05 seg 30-36, AS08 ~30-33
    b.inh("DD06", "AS09", 1); // DD06 seg 36-42, AS09 ~33-36
    b.inh("DD06", "AS10", 1); // DD06 seg 36-42, AS10 ~36-39
    b.inh("DD06", "AS11", 1); // DD06 seg 36-42, AS11 ~39-42
    // DB ↔ AS: gap junction coupling (synchronize dorsal activation)
    b.gj("DB01", "AS01", 1); b.gj("DB01", "AS02", 1);
    b.gj("DB02", "AS03", 1); b.gj("DB02", "AS04", 1);
    b.gj("DB03", "AS04", 1); b.gj("DB03", "AS05", 1);
    b.gj("DB04", "AS05", 1); b.gj("DB05", "AS06", 1);
    b.gj("DB06", "AS06", 1); b.gj("DB07", "AS07", 1);
    // Step 88: DB↔AS08-11 (DB07 covers posterior, overlaps AS08-11)
    b.gj("DB07", "AS08", 1); b.gj("DB07", "AS09", 1);
    b.gj("DB07", "AS10", 1); b.gj("DB07", "AS11", 1);

    // Step 89: AS → VD excitatory connections — dorsal bias mechanism
    // Tolstenkov 2018 eLife: AS depolarization → excites overlapping VD neurons
    // → VD GABAergic output inhibits ventral BWMs → net dorsal bias
    // Each AS excites VD(s) in its segment region (White 1986, Cook 2019)
    b.syn("AS01", "VD01", 1);
    b.syn("AS02", "VD02", 1); b.syn("AS02", "VD03", 1);
    b.syn("AS03", "VD03", 1); b.syn("AS03", "VD04", 1);
    b.syn("AS04", "VD04", 1); b.syn("AS04", "VD05", 1);
    b.syn("AS05", "VD06", 1); b.syn("AS05", "VD07", 1);
    b.syn("AS06", "VD07", 1); b.syn("AS06", "VD08", 1);
    b.syn("AS07", "VD08", 1); b.syn("AS07", "VD09", 1);
    b.syn("AS08", "VD09", 1); b.syn("AS08", "VD10", 1);
    b.syn("AS09", "VD10", 1); b.syn("AS09", "VD11", 1);
    b.syn("AS10", "VD12", 1);
    b.syn("AS11", "VD13", 1);

    // Step 90: VA → DD chemical synapses — backward ventral wave propagation
    // White 1986: VA excites DD → DD inhibits ventral BWM in next segment
    // Creates traveling wave: ventral contraction → DD → ventral relaxation ahead
    b.syn("VA01", "DD01", 1);
    b.syn("VA02", "DD01", 1); b.syn("VA03", "DD02", 1);
    b.syn("VA04", "DD02", 1); b.syn("VA05", "DD03", 1);
    b.syn("VA06", "DD03", 1); b.syn("VA07", "DD04", 1);
    b.syn("VA08", "DD04", 1); b.syn("VA09", "DD05", 1);
    b.syn("VA10", "DD05", 1); b.syn("VA11", "DD06", 1);
    b.syn("VA12", "DD06", 1);

    // Step 91: Complete excitatory→inhibitory MN cross-inhibition pathways
    // White 1986: Each excitatory MN class synapses onto D-class inhibitory MNs
    // This creates contralateral muscle relaxation for coordinated undulation
    // Four pathways total: VB→VD, DB→DD (forward) + VA→DD, DA→VD (backward)

    // VB → VD: forward ventral phase → dorsal relaxation (primary overlap only)
    // VB contracts ventral BWM + excites VD → VD inhibits dorsal BWM
    b.syn("VB01", "VD01", 1); b.syn("VB02", "VD02", 1);
    b.syn("VB03", "VD03", 1); b.syn("VB04", "VD04", 1);
    b.syn("VB05", "VD06", 1); b.syn("VB06", "VD07", 1);
    b.syn("VB07", "VD08", 1); b.syn("VB08", "VD09", 1);
    b.syn("VB09", "VD10", 1); b.syn("VB10", "VD12", 1);
    b.syn("VB11", "VD13", 1);

    // DB → DD: forward dorsal phase → ventral relaxation (primary overlap only)
    // DB contracts dorsal BWM + excites DD → DD inhibits ventral BWM
    b.syn("DB01", "DD01", 1); b.syn("DB02", "DD02", 1);
    b.syn("DB03", "DD02", 1); b.syn("DB04", "DD03", 1);
    b.syn("DB05", "DD04", 1); b.syn("DB06", "DD05", 1);
    b.syn("DB07", "DD06", 1);

    // DA → VD: backward dorsal phase → wave propagation (primary overlap only)
    // DA contracts dorsal BWM + excites VD → VD inhibits dorsal BWM ahead
    b.syn("DA01", "VD01", 1); b.syn("DA02", "VD02", 1);
    b.syn("DA03", "VD04", 1); b.syn("DA04", "VD05", 1);
    b.syn("DA05", "VD06", 1); b.syn("DA06", "VD07", 1);
    b.syn("DA07", "VD09", 1); b.syn("DA08", "VD11", 1);
    b.syn("DA09", "VD13", 1);

    // Step 89: AS ↔ AVA gap junctions — electrical feedback to backward PIN
    // Tolstenkov 2018 eLife: UNC-7 innexin mediates AS→AVA retrograde signaling
    // AS photostimulation → 18% ΔR/R Ca²⁺ in AVA, abolished in unc-7(e5)
    // AS→AVB signaling not significant (smaller number of gap junctions)
    // Pattern: odd AS→AVAL, even AS→AVAR (alternating, matches chemical synapses)
    b.gj("AS01", "AVAL", 1); b.gj("AS02", "AVAR", 1);
    b.gj("AS03", "AVAL", 1); b.gj("AS04", "AVAR", 1);
    b.gj("AS05", "AVAL", 1); b.gj("AS06", "AVAR", 1);
    b.gj("AS07", "AVAL", 1); b.gj("AS08", "AVAR", 1);
    b.gj("AS09", "AVAL", 1); b.gj("AS10", "AVAR", 1);
    b.gj("AS11", "AVAL", 1);

    // Step 53: PVC forward command interneuron circuit
    // --- Inputs to PVC ---
    // PLM → PVC: posterior gentle touch → accelerate forward (Chalfie 1985)
    b.syn("PLML", "PVCL", 2); b.syn("PLMR", "PVCR", 2);
    // AIY → PVC: chemotaxis forward drive (White 1986, Kawano 2011)
    b.syn("AIYL", "PVCL", 1); b.syn("AIYR", "PVCR", 1);
    // DVA → PVC: proprioceptive input (Li 2006, Cook 2019)
    b.syn("DVA",  "PVCL", 1); b.syn("DVA",  "PVCR", 1);
    // AVD → PVC: touch integration relay (White 1986)
    b.syn("AVDL", "PVCL", 1); b.syn("AVDR", "PVCR", 1);
    // --- Outputs from PVC ---
    // PVC → AVB: main forward command drive (Zheng 1999, Kawano 2011)
    b.syn("PVCL", "AVBL", 3); b.syn("PVCR", "AVBR", 3);
}

// ================================================================
// 7. Omega Turn Circuit — RIA↔RIV
// ================================================================
void build_omega(CB& b) {
    // Step 31: RIV omega turn circuit (Gray 2005, Donnelly 2013)
    // Step 42C: Complete RIA↔RIV negative feedback loop (Cook 2019 anatomy)
    b.syn("RIAL", "RIVR", 1); b.syn("RIAL", "RIVL", 1);
    b.syn("RIAR", "RIVL", 1); b.syn("RIAR", "RIVR", 1);
    // RIV→RIA inhibitory feedback (GABA via UNC-49)
    b.inh("RIVL", "RIAL", 1);   b.inh("RIVR", "RIAL", 1);
    b.inh("RIVL", "RIAR", 0.5); b.inh("RIVR", "RIAR", 0.5);
    // RIV ⊣ RMD dorsal: suppress dorsal muscles during omega → deepen ventral bend
    b.inh("RIVL", "RMDDL", 1); b.inh("RIVR", "RMDDR", 1);
}

// ================================================================
// 8. Gas Sensing — O₂ (URX/AUA/AQR/PQR) + CO₂ (BAG)
// ================================================================
void build_gas_sensing(CB& b) {
    // Step 34: O₂ sensing circuit (Gray 2004, Chang 2006, Laurent 2015)
    // URX → AUA: primary O₂ relay
    b.syn("URXL", "AUAL", 2); b.syn("URXR", "AUAR", 2);
    // AUA → AVA: O₂ relay → backward command (NPR-1 reduces effective release)
    b.syn("AUAL", "AVAL", 0.3); b.syn("AUAL", "AVAR", 0.3);
    b.syn("AUAR", "AVAL", 0.3); b.syn("AUAR", "AVAR", 0.3);
    // URX → AVB: direct speed modulation
    b.syn("URXL", "AVBL", 1);
    // AQR → AVA: anterior body cavity O₂ → backward command
    b.syn("AQR", "AVAL", 1); b.syn("AQR", "AVAR", 1);
    // PQR → AVA: posterior body cavity O₂ → backward command
    b.syn("PQR", "AVAL", 1); b.syn("PQR", "AVAR", 1);

    // Step 35: CO₂ sensing circuit (Hallem 2008, Bretscher 2011, Carrillo 2013)
    // BAG ⊣ AIY: CO₂ high → suppress forward drive (via RIG/GluCl, inhibitory)
    b.inh("BAGL", "AIYL", 1); b.inh("BAGR", "AIYR", 1);
    // BAG → AIB: CO₂ high → promote turning/reversal (excitatory)
    b.syn("BAGL", "AIBL", 1); b.syn("BAGR", "AIBR", 1);
    // BAG → RIA: head turning modulation
    b.syn("BAGL", "RIAL", 1); b.syn("BAGR", "RIAR", 1);
}

// ================================================================
// 9. Proprioception — DVA + PVD
// ================================================================
void build_proprioception(CB& b) {
    // Step 36: DVA→DB/VB: modulate forward wave amplitude (Step 39: expanded)
    b.syn("DVA", "DB01", 1); b.syn("DVA", "DB02", 1); b.syn("DVA", "DB03", 1);
    b.syn("DVA", "DB04", 1); b.syn("DVA", "DB05", 1); b.syn("DVA", "DB06", 1);
    b.syn("DVA", "DB07", 1);
    b.syn("DVA", "VB01", 1); b.syn("DVA", "VB02", 1); b.syn("DVA", "VB03", 1);
    b.syn("DVA", "VB04", 1); b.syn("DVA", "VB05", 1); b.syn("DVA", "VB06", 1);
    b.syn("DVA", "VB07", 1);
    // Step 87: DVA→VB08-11 proprioceptive modulation (complete VB set)
    b.syn("DVA", "VB08", 1); b.syn("DVA", "VB09", 1);
    b.syn("DVA", "VB10", 1); b.syn("DVA", "VB11", 1);
    // DVA → AVA: extreme bending → protective reversal (weak, 0.5 section)
    b.syn("DVA", "AVAL", 0.5);
    // PVD → AVA: harsh touch → backward movement (2 sections, strong)
    b.syn("PVDL", "AVAL", 2); b.syn("PVDR", "AVAR", 2);
    // PVD ↔ DVA: gap junction — proprioceptive signal integration
    b.gj("PVDL", "DVA", 1); b.gj("PVDR", "DVA", 1);
}

// ================================================================
// 10. Pharyngeal Nervous System
// ================================================================
void build_pharynx(CB& b) {
    // Step 24: REF: Albertson & Thomson 1976, Cook 2020 (pharyngeal connectome)
    // I1 → MC: excitatory, relays extrapharyngeal signals to pacemaker
    b.syn("I1L", "MCL", 3); b.syn("I1R", "MCR", 3);
    // MC → M3: MC activity → muscle contraction → M3 proprioceptive firing
    b.syn("MCL", "M3L", 2); b.syn("MCR", "M3R", 2);
    // M3 → MC: weak inhibitory feedback (glutamate → Cl⁻)
    b.syn("M3L", "MCL", 1); b.syn("M3R", "MCR", 1);
    // MC → M4: MC pumping activates M4 for isthmus peristalsis
    b.syn("MCL", "M4", 2); b.syn("MCR", "M4", 2);
    // Pharyngeal gap junctions
    b.gj("I1L", "I1R", 2);
    b.gj("MCL", "MCR", 3);
    b.gj("M3L", "M3R", 2);
    // RIP ↔ I1: the SOLE bridge between somatic and pharyngeal nervous systems
    b.gj("RIPL", "I1L", 2); b.gj("RIPR", "I1R", 2);
}

// ================================================================
// 11. Egg-Laying Circuit
// ================================================================
void build_egg_laying(CB& b) {
    // Step 38: Egg-laying circuit (Collins 2016 eLife, Schafer 2006)
    // PLM ⊣ HSN: gentle touch inhibits egg laying (safety mechanism)
    b.inh("PLML", "HSNL", 1); b.inh("PLMR", "HSNR", 1);
    // VC → VB: egg-laying slows locomotion (weak inhibition)
    b.inh("VC4", "VB01", 0.5); b.inh("VC5", "VB02", 0.5);
    // HSN ↔ VC gap junction — synchronize egg-laying motor output
    b.gj("HSNL", "VC4", 2); b.gj("HSNR", "VC5", 2);
}

// ================================================================
// 12. Sleep Circuit + Core Gap Junctions
// ================================================================
void build_sleep_and_gaps(CB& b) {
    // Step 27: RIS sleep neuron connections
    // REF: White 1986, Cook 2019 — RIS synaptic outputs
    //      Turek 2016 eLife — FLP-11 is the major sleep transmitter (volume)
    // RIS ⊣ AVA: GABA inhibition of backward command (stop reversals during sleep)
    b.inh("RIS", "AVAL", 2); b.inh("RIS", "AVAR", 2);
    // RIS ⊣ AVB: GABA inhibition of forward command
    b.inh("RIS", "AVBL", 1); b.inh("RIS", "AVBR", 1);
    // RIS ⊣ AIB: GABA inhibition of reversal initiation
    b.inh("RIS", "AIBL", 1); b.inh("RIS", "AIBR", 1);
    // RIS gap junctions: AIB (5 sections in connectome, community 4)
    b.gj("RIS", "AIBL", 2); b.gj("RIS", "AIBR", 2);

    // RIS ⊣ PVC: sleep should also suppress forward command (Step 53)
    b.inh("RIS", "PVCL", 1); b.inh("RIS", "PVCR", 1);

    // Core gap junctions — command interneuron L-R coupling
    b.gj("AVAL", "AVAR", 10);
    b.gj("AVBL", "AVBR", 12);
    b.gj("AVDL", "AVDR", 5);
    b.gj("AVEL", "AVER", 4);
    b.gj("PVCL", "PVCR", 4);  // Step 53: PVC L-R coupling
    // Step 37: AVA ↔ AVE gap junction
    b.gj("AVAL", "AVEL", 3); b.gj("AVAR", "AVER", 3);
    b.gj("ASEL", "ASER", 2);
    b.gj("AIBL", "AIBR", 3);
    // AVA ↔ PVC gap junctions: forward/backward mutual coupling (White 1986)
    // During reversal: AVA active → depolarizes PVC → but chemical inhibition dominates
    b.gj("AVAL", "PVCL", 2); b.gj("AVAR", "PVCR", 2);
    // RIM ↔ AVA gap junctions: CRITICAL for forward run stabilization
    // REF: Ouellette 2022 eLife — RIM gap junctions propagate hyperpolarization
    b.gj("RIML", "AVAL", 2); b.gj("RIMR", "AVAR", 2);
    b.gj("RIML", "RIMR", 3);
    // Step 42: RIV L-R coupling (Cook 2019: RIVL↔RIVR=28)
    b.gj("RIVL", "RIVR", 4);

    // Step 87: VB↔VB adjacent gap junctions — proprioceptive wave propagation
    // Wen 2012 Neuron: B-type MNs propagate bending waves via proprioceptive
    // coupling between adjacent neurons. Gap junctions between neighboring VBs
    // provide the anatomic platform for propagating bending signals.
    // ConnectomeToolbox/Cook 2019: VB(n)↔VB(n+1) = 3-5 sections
    b.gj("VB01", "VB02", 2); b.gj("VB02", "VB03", 2);
    b.gj("VB03", "VB04", 2); b.gj("VB04", "VB05", 2);
    b.gj("VB05", "VB06", 2); b.gj("VB06", "VB07", 2);
    b.gj("VB07", "VB08", 2); b.gj("VB08", "VB09", 2);
    b.gj("VB09", "VB10", 2); b.gj("VB10", "VB11", 2);

    // Step 89: DB↔DB adjacent gap junctions — dorsal proprioceptive wave
    // Symmetric to VB↔VB (Wen 2012): dorsal B-type MNs also propagate
    // bending waves via proprioceptive coupling between adjacent neurons
    // Cook 2019: DB(n)↔DB(n+1) gap junctions present
    b.gj("DB01", "DB02", 2); b.gj("DB02", "DB03", 2);
    b.gj("DB03", "DB04", 2); b.gj("DB04", "DB05", 2);
    b.gj("DB05", "DB06", 2); b.gj("DB06", "DB07", 2);

    // Step 90: A-class backward proprioceptive wave + motor interconnect
    // Gao 2018 eLife: A-class MNs are intrinsic oscillators, proprioception
    // entrains their activities and phase-couples adjacent members
    // Liu 2017 Nat Commun: AVA↔A-type gap junctions are antidromic-rectifying

    // VA↔VA adjacent gap junctions — ventral backward wave propagation
    // Symmetric to VB↔VB: proprioceptive coupling for backward undulation
    b.gj("VA01", "VA02", 2); b.gj("VA02", "VA03", 2);
    b.gj("VA03", "VA04", 2); b.gj("VA04", "VA05", 2);
    b.gj("VA05", "VA06", 2); b.gj("VA06", "VA07", 2);
    b.gj("VA07", "VA08", 2); b.gj("VA08", "VA09", 2);
    b.gj("VA09", "VA10", 2); b.gj("VA10", "VA11", 2);
    b.gj("VA11", "VA12", 2);

    // DA↔DA adjacent gap junctions — dorsal backward wave propagation
    // Symmetric to DB↔DB: dorsal A-class proprioceptive coupling
    b.gj("DA01", "DA02", 2); b.gj("DA02", "DA03", 2);
    b.gj("DA03", "DA04", 2); b.gj("DA04", "DA05", 2);
    b.gj("DA05", "DA06", 2); b.gj("DA06", "DA07", 2);
    b.gj("DA07", "DA08", 2); b.gj("DA08", "DA09", 2);

    // DA↔AS gap junctions — synchronize dorsal activation during backward movement
    // White 1986, Cook 2019: DA and AS share gap junctions in overlapping segments
    // Both innervate dorsal muscles; coupling ensures coordinated dorsal contraction
    b.gj("DA01", "AS01", 1); b.gj("DA02", "AS02", 1);
    b.gj("DA03", "AS03", 1); b.gj("DA04", "AS04", 1);
    b.gj("DA05", "AS05", 1); b.gj("DA06", "AS06", 1);
    b.gj("DA07", "AS07", 1); b.gj("DA08", "AS08", 1);
    b.gj("DA09", "AS09", 1);
}

// ================================================================
// 12. Defecation Motor Program (Step 56)
// ================================================================
void build_defecation(CB& b) {
    // AVL ↔ DVB: INX-1 gap junction — synchronizes action potential firing
    // AVL fires compound APs (UNC-2 Ca²⁺ + EXP-2 K⁺), propagates to DVB
    // Both release GABA onto enteric muscles via EXP-1 excitatory receptor
    // REF: Jiang 2022 Nat Commun — INX-1 required for AVL→DVB AP propagation
    b.gj("AVL", "DVB", 3);
    // AVL ↔ DD05: gap junction in posterior ventral cord
    // AVL axon runs through ventral cord, gap junctions to posterior D-type neurons
    // During DMP, may coordinate body wall relaxation for posterior contraction
    // REF: White 1986 — AVL gap junctions to D-type neurons
    b.gj("AVL", "DD05", 2);
    b.gj("AVL", "DD06", 1); // Step 86: AVL also contacts DD06 (posterior)
    // Step 71: AVL/DVB GABA → B-class motor neuron inhibition during DMP
    // AVL axon runs full ventral cord → GABA release contacts posterior MNs
    // During DMP (50-70pA drive): AVL fires → GABA inhibits VB/DB → speed reduction
    // Replaces direct dmp_speed_factor_ multiplication (P0-5 fix)
    // REF: Alkema 2015 Sci Rep — DMP coupled to locomotion pause
    //      Jiang 2022 Nat Commun — AVL fires compound APs during DMP
    //      White 1986 — AVL axon contacts in ventral cord
    b.inh("AVL", "VB05", 1);  // posterior ventral B-class
    b.inh("AVL", "DB05", 1);  // posterior dorsal B-class
    b.inh("DVB", "VB06", 1);  // DVB in tail → posterior MNs
    b.inh("DVB", "VB07", 1);
    b.inh("DVB", "VB08", 1);  // Step 87: DVB contacts posterior VB08
    // RIS ⊣ AVL: sleep neuron inhibits defecation during quiescence
    // DMP suppressed during sleep — consistent with global RIS inhibition
    b.inh("RIS", "AVL", 1);
}

// ================================================================
// 13. Ventral Cord Integrators + New Sensory (Step 61)
// ================================================================
void build_ventral_cord_integrators(CB& b) {
    // Step 61: AVM — anterior gentle touch (completes ALM/PLM circuit)
    // AVM → AVD: anterior touch excites backward command (like ALM)
    // AVM ⊣ AVB: anterior touch inhibits forward (like ALM)
    // REF: Chalfie 1985, Way & Chalfie 1989
    b.gj("AVM", "AVDL", 2); b.gj("AVM", "AVDR", 2);
    b.inh("AVM", "AVBL", 2); b.inh("AVM", "AVBR", 2);
    // AVM → PVC: Cook 2019 — AVM has chemical output to PVC
    b.syn("AVM", "PVCL", 1); b.syn("AVM", "PVCR", 1);

    // Step 61: ASI — insulin/dauer sensory circuit
    // ASI → AIA: chemosensory relay (like ASE, AWC)
    // ASI → AIY: dauer/food quality → forward drive modulation
    // ASI → AIB: weak, aversive component
    // REF: Bargmann & Horvitz 1991, Beverly 2011
    b.syn("ASIL", "AIAL", 2); b.syn("ASIR", "AIAR", 2);
    b.syn("ASIL", "AIYL", 1); b.syn("ASIR", "AIYR", 1);
    b.syn("ASIL", "AIBL", 1); b.syn("ASIR", "AIBR", 1);
    b.gj("ASIL", "ASIR", 1);  // bilateral coupling

    // Step 61: ADL — pheromone/nociceptive circuit
    // ADL → AVA: aversive sensing → reversal (like ASH but weaker)
    // ADL → AVJ: O₂/aversive integration (Emmons 2024)
    // ADL → AIA: chemosensory relay
    // REF: Troemel 1997, Jang 2012
    b.syn("ADLL", "AVAL", 1); b.syn("ADLR", "AVAR", 1);
    b.syn("ADLL", "AVJL", 1); b.syn("ADLR", "AVJR", 1);
    b.syn("ADLL", "AIAL", 1); b.syn("ADLR", "AIAR", 1);
    b.gj("ADLL", "ADLR", 1);

    // Step 61: DVC — stretch receptor → AVA backward locomotion
    // Documented mechanosensory function (Li 2006)
    // Chemical output to AVA; gap junctions to PVT
    // REF: Emmons 2024, Li 2006
    b.syn("DVC", "AVAL", 2); b.syn("DVC", "AVAR", 2);
    b.gj("DVC", "PVT", 3);

    // Step 61: PVT — neuropeptide hub
    // Gap junctions to DVC (see above), PVP
    // Weak chemical output to navigational interneurons
    // REF: Emmons 2024 — PVT is neuropeptide connectome hub
    b.gj("PVT", "PVPL", 2); b.gj("PVT", "PVPR", 2);

    // Step 61: AVK — PDE target, turn circuit integrator
    // PDE → AVK: MAJOR connection (50% of PDE output!)
    // AVK → RIM: turn circuit modulation
    // AVK → RIV: omega turn circuit
    // AVK ↔ RIC: gap junction (octopaminergic coupling)
    // AVK ↔ DVA: gap junction (proprioceptive integration)
    // REF: Emmons 2024 — AVK aggregates PDE + sensory → turn circuit
    b.syn("PDEL", "AVKL", 3); b.syn("PDER", "AVKR", 3); // PDE→AVK (major!)
    b.syn("AVKL", "RIML", 1); b.syn("AVKR", "RIMR", 1);
    b.syn("AVKL", "RIVL", 1); b.syn("AVKR", "RIVR", 1);
    b.syn("AVKL", "SMBDL", 1); b.syn("AVKR", "SMBDR", 1); // sublateral motor
    b.gj("AVKL", "RICL", 2); b.gj("AVKR", "RICR", 2);
    b.gj("AVKL", "DVA", 1); b.gj("AVKR", "DVA", 1);
    b.gj("AVKL", "AVKR", 2);  // bilateral coupling

    // Step 61: AVJ — O₂/aversive integrator
    // Inputs: ADL (above), AQR, PQR, URX (O₂ sensors)
    // AVJ ↔ RIS: 5 gap junction sections! — sleep coupling
    // REF: Emmons 2024 — AVJ integrates O₂/aversive → RIS
    b.syn("AQR", "AVJL", 1); b.syn("AQR", "AVJR", 1);
    b.syn("PQR", "AVJL", 1); b.syn("PQR", "AVJR", 1);
    b.syn("URXL", "AVJL", 1); b.syn("URXR", "AVJR", 1);
    b.gj("AVJL", "RIS", 3); b.gj("AVJR", "RIS", 2);  // 5 total sections
    b.gj("AVJL", "AVJR", 2);

    // Step 61: AVH — sensory bridge interneuron
    // Gap junctions to ASK; chemical output to SMB
    // Creates path: ASK → AVH → SMB (sublateral motor modulation)
    // REF: Emmons 2024 — bridges pheromone sensing to motor output
    b.gj("AVHL", "ASKL", 2); b.gj("AVHR", "ASKR", 2);
    b.syn("AVHL", "SMBVL", 1); b.syn("AVHR", "SMBVR", 1);
    b.gj("AVHL", "AVHR", 1);

    // Step 61: PVP — highest gap junction degree neuron
    // Gap junctions: AQR, PQR (O₂ sensors), DVC, PVT (above)
    // Chemical output to AVA, AVB, PVC (command modulation)
    // REF: Emmons 2024 — PVP roaming/dwelling regulation
    b.gj("PVPL", "AQR", 4); b.gj("PVPR", "AQR", 4);   // 102 sections total
    b.gj("PVPL", "PQR", 2); b.gj("PVPR", "PQR", 2);   // 26 sections
    b.gj("PVPL", "DVC", 3); b.gj("PVPR", "DVC", 3);   // 54 sections
    b.syn("PVPL", "AVAL", 1); b.syn("PVPR", "AVAR", 1);
    b.syn("PVPL", "AVBL", 1); b.syn("PVPR", "AVBR", 1);
    b.syn("PVPL", "PVCL", 1); b.syn("PVPR", "PVCR", 1);
    b.gj("PVPL", "PVPR", 3);

    // Step 61: PVR — proprioceptive hub
    // Gap junctions to DVA (bodywide sensory network)
    // Chemical output to AVJ (sensory → aversive integration)
    // Output to RIP (pharyngeal regulation — bodywide state sensing)
    // REF: Emmons 2024 — PVR + DVA = bodywide proprioceptive network
    b.gj("PVR", "DVA", 2);
    b.syn("PVR", "AVJL", 1); b.syn("PVR", "AVJR", 1);
    b.syn("PVR", "RIPL", 1); b.syn("PVR", "RIPR", 1);

    // Step 82: RIG — ventral cord → navigation relay (single unpaired neuron)
    // Emmons 2024: "DVC and PVT share chemical output to... RIG"
    // RIG bridges ventral cord information processing to head navigation circuit
    // DVC→RIG: stretch receptor/proprioceptive integrator (Emmons 2024)
    b.syn("DVC", "RIG", 2);
    // PVT→RIG: neuropeptide hub integrator (Emmons 2024)
    b.syn("PVT", "RIG", 2);
    // RIG→AIY: modulates forward drive (navigation relay)
    b.syn("RIG", "AIYL", 1); b.syn("RIG", "AIYR", 1);
    // RIG→AIZ: modulates turning behavior
    b.syn("RIG", "AIZL", 1); b.syn("RIG", "AIZR", 1);
    // RIG→RIA: head motor modulation
    b.syn("RIG", "RIAL", 1); b.syn("RIG", "RIAR", 1);
    // RIG→AVK: turn circuit integrator (Emmons 2024: "AVK receives... from RIG")
    b.syn("RIG", "AVKL", 1); b.syn("RIG", "AVKR", 1);
    // AVH→RIG: sensory bridge pathway (Emmons 2024 community analysis)
    // Creates ASK→AVH→RIG→AIZ/RIA pathway
    b.syn("AVHL", "RIG", 1); b.syn("AVHR", "RIG", 1);
}

} // anonymous namespace

// ================================================================
// Public entry point — orchestrates all build functions
// ================================================================
void build_default_connectome(
    std::vector<NeuronInfo>& neurons,
    std::vector<SynapseInfo>& synapses,
    std::vector<GapJunctionInfo>& gap_junctions) {

    neurons.clear();
    synapses.clear();
    gap_junctions.clear();

    CB b{neurons, synapses, gap_junctions, {}};

    // 1. Register all neurons
    build_neurons(b);
    b.finalize_ids();

    // 2. Wire circuits (order doesn't matter — all neurons already registered)
    build_chemotaxis(b);         // ASE/AWC/AWA/AFD → AIA/AIB/AIY/AIZ
    build_touch_nociception(b);  // ALM/PLM/ASH/OLQ/AWB touch & pain
    build_phototaxis(b);         // Step 55: ASJ/ASK/AWB light avoidance
    build_interneuron(b);        // AIA/AIB/AIY/RIB → AVA/AVB relay
    build_head_motor(b);         // SMD/RMD/RME/SMB oscillator + klinotaxis
    build_command_ventral(b);    // AVA/AVB/AVE → motor neurons, DD↔VD, AS
    build_omega(b);              // RIA↔RIV omega turn circuit
    build_gas_sensing(b);        // O₂ (URX/AUA/AQR/PQR) + CO₂ (BAG)
    build_proprioception(b);     // DVA + PVD body sensing
    build_pharynx(b);            // MC/M3/M4/I1/RIP pharyngeal CPG
    build_egg_laying(b);         // HSN/VC egg-laying
    build_defecation(b);         // Step 56: AVL/DVB defecation motor program
    build_ventral_cord_integrators(b); // Step 61: AVM/ASI/ADL/DVC/PVT/AVK/AVJ/AVH/PVP/PVR
    build_sleep_and_gaps(b);     // RIS sleep + core L-R gap junctions

    LOG_INFO("Generated default connectome: ", neurons.size(), " neurons, ",
             synapses.size(), " synapses, ", gap_junctions.size(), " gap junctions");
}

} // namespace celegans
