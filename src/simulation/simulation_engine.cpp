#include "simulation/simulation_engine.h"
#include "neuron/multi_compartment.h"
#include "core/logger.h"
#include <cstring>
#include <algorithm>

namespace celegans {

SimulationEngine::SimulationEngine() {}

static bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

void SimulationEngine::initialize_default() {
    LOG_INFO("=== C. elegans Simulation Engine ===");
    LOG_INFO("Initializing with default connectome...");

    // 1. Generate default connectome
    std::vector<NeuronInfo> neuron_infos;
    std::vector<SynapseInfo> synapse_infos;
    std::vector<GapJunctionInfo> gj_infos;
    ConnectomeLoader::generate_default_connectome(neuron_infos, synapse_infos, gj_infos);

    // 2. Build connectome
    connectome_.build(neuron_infos, synapse_infos, gj_infos);

    // 3. Create neurons (NeuronFactory now auto-specializes by name)
    neurons_.clear();
    neurons_.reserve(neuron_infos.size());
    for (auto& info : neuron_infos) {
        neurons_.push_back(NeuronFactory::create(info));
    }
    LOG_INFO("Created ", neurons_.size(), " neurons");

    // 4. Initialize body
    body_.initialize(Vector2d{25.0, 25.0}, 0.0);
    LOG_INFO("Body initialized at (25, 25), heading 0 rad");

    // 5. Initialize motor controller
    std::unordered_map<std::string, int> name_to_id;
    for (auto& info : neuron_infos) {
        name_to_id[info.name] = info.id;
    }
    motor_controller_.initialize(name_to_id);

    // 6. Initialize environment
    environment_.initialize(50.0, 50.0);
    environment_.chemical_field().add_point_source(Vector2d{35.0, 35.0}, 1.0);  // food odor (volatile)
    // Step 26b: food source also emits soluble chemicals (salt/amino acids)
    // Same σ²=144mm² as food_odor (same diffusion shape) but strength=0.4 (weaker)
    // ASE gets identical concentration profile shape as before → minimal regression
    // Multi-species key: independent channels, not necessarily different diffusion ranges
    // After learning: food_odor weathervane reverses, soluble stays but weak (0.4×0.3=0.12×)
    environment_.soluble_field().add_point_source(Vector2d{35.0, 35.0}, 0.4);
    LOG_INFO("Environment initialized (50x50 mm), food source at (35, 35)");

    // 7. Classify sensory neurons: chemosensory get transducers, others get baseline
    for (auto& info : neuron_infos) {
        if (info.type != NeuronType::SENSORY) continue;

        // Chemosensory neurons: multi-species chemical sensing
        // Step 26b: split into TWO independent chemical channels
        //   food_odor (volatile, bacteria-specific) → AWC/AWA → chem_field_
        //   soluble (salt/amino acids, environmental) → ASE → soluble_field_
        // REF: Bargmann 2006 — AWC detects volatile odors, ASE detects ions
        if (starts_with(info.name, "ASEL")) {
            // ASEL: ON-cell — excited by NaCl concentration INCREASE
            // Suzuki 2008 Nature: ASEL produces TRANSIENT calcium response
            //   "fast calcium response to upstep, immediately decayed to steady state"
            // → slow_tau=3000ms matches ~2-3s transient decay from calcium traces
            // Promotes runs via ASEL⊣AIA(Cl⁻)→AIB released + ASEL⊣AIB(GLC-3 direct)
            // REF: Suzuki 2008 Nature, Kuramochi 2018, Miller 2005 JNeurosci
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 100.0, 5.0, 100.0, 3000.0)});
        } else if (starts_with(info.name, "ASER")) {
            // ASER: OFF-cell — excited by NaCl concentration DECREASE
            // Suzuki 2008 Nature: ASER produces SUSTAINED calcium response
            //   "large, long-lasting response to downstep, slowly decayed" (>10s)
            // Kuramochi 2018: AIB shows "similar response pattern as ASER"
            // → slow_tau=8000ms matches sustained OFF dynamics (~10-30s persistence)
            // Asymmetry 2.7:1 (ASER 8000 / ASEL 3000) biases toward detecting
            // "wrong direction" movements (critical for klinokinesis)
            // Promotes turns via ASER→AIB(GLR-1 direct) + ASER⊣AIA→AIB(released)
            // REF: Suzuki 2008 Nature, Kuramochi 2018, Miller 2005 JNeurosci
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 100.0, 5.0, 100.0, 8000.0)});
        } else if (starts_with(info.name, "AWC")) {
            // AWC: OFF-cell — excited by odor REMOVAL (Chalasani 2007 Nature)
            // Sustained OFF response: calcium rises and persists for seconds after odor removal
            // AWC→AIB(GLR-1 excit.) + AWC→AIY(GLC-3 inhib.) + AWC⊣AIA(Cl⁻ disinhibit.)
            // REF: Chalasani 2007 Nature, Kakaria 2019 eLife, Tsunozaki 2008
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 80.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "AWA")) {
            // AWA: ON-cell — excited by odor ADDITION (Larsch 2015 Cell Reports)
            // Desensitizes within 10s at high conc. (1.15µM), retains sensitivity to increases
            // AWA→AIA via gap junction (Kakaria 2019) — excitatory half of AND-gate
            // REF: Larsch 2015 Cell Reports, Kakaria 2019 eLife, Bargmann 1993
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 80.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "ASH")) {
            // Step 25: ASH nociceptors sample REPELLENT field (not attractant)
            // TONIC: responds to absolute repellent concentration (not dC/dt)
            // gain=80: strong nociceptive drive to trigger reversal via ASH→AVA(3) + ASH→AIB(3)→AVA
            // REF: Summers 2015 JNeurosci — ASH→AIB→AVA nociceptive circuit
            //      Bargmann & Kaplan 1998 — ASH tonic response to noxious stimuli
            // half_max=0.5: only strong response at high concentration (near source)
            // At C=0.8: I=3+80×0.62=52pA, at C=0.5: I=43pA, at C=0.2: I=22pA
            noci_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 80.0, 3.0, 500.0, 5000.0, 0.5)});
        } else if (starts_with(info.name, "NSM")) {
            // Step 45: NSM is an ENTERIC sensory neuron — detects food INGESTION,
            // not food proximity. Uses ASIC channels (DEL-7/DEL-3) to sense
            // pharyngeal pumping. Drive computed from pump_rate_hz after update_pharynx().
            // REF: Randi 2018 Cell — ASICs mediate food responses in NSM
            //      Flavell 2013 Cell — NSM drives dwelling via serotonin
            //      Flavell 2023 Cell — NSM functional organization
            // NSM drive set in update_pharynx section (via nid("NSML")/nid("NSMR"))
        } else if (starts_with(info.name, "CEP")) {
            // CEP: head mechanosensory, detects bacteria (food presence)
            // TONIC: fires when on food lawn, not responding to changes
            // REF: Sawin 2000 — CEP active on bacterial lawn
            // Step 47b: Modest drive (gain=20) — DA for DVA/DOP-1/NLP-12 priming only.
            // Basal slowing is implemented separately via on_lawn sigmoid (instant,
            // position-dependent) because DA acts via extrasynaptic DOP-3 volume
            // transmission, NOT through CEP's synaptic circuit (CEP↔OLQ gap junctions
            // would cause OLQ→RMD/RIC cascade disrupting navigation).
            // REF: Chase 2004 Nature Neurosci — DOP-3 extrasynaptic on motor neurons
            // Step 85: gain 20→35. DA conc=0.003 (dead) because CEP barely fired.
            // Step 57 ion channel changes + modest gain → insufficient DA for DOP-3 slowing.
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 35.0, 1.0, 500.0, 5000.0, 0.5), true});
        } else if (starts_with(info.name, "ADE")) {
            // Step 60: ADE — anterior deirid mechanosensory, dopaminergic
            // Same modality as CEP: detects bacteria texture on food lawn
            // Slightly lower gain than CEP (ADE has fewer synaptic outputs)
            // REF: Sawin 2000 — ADE contributes to basal slowing; Sulston 1977
            // Step 85: gain 15→25 (same scaling as CEP)
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 25.0, 1.0, 500.0, 5000.0, 0.5), true});
        } else if (starts_with(info.name, "PDE")) {
            // Step 60: PDE — posterior deirid mechanosensory, dopaminergic
            // Mid-body position: senses bacteria along body wall
            // Lower gain (12): posterior, fewer synaptic connections than CEP/ADE
            // REF: Sawin 2000 — PDE contributes to basal slowing; Chase & Koelle 2007
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 12.0, 1.0, 500.0, 5000.0, 0.5), true});
        } else if (starts_with(info.name, "ASI")) {
            // Step 61: ASI — insulin/dauer sensory, food quality sensing
            // TONIC: fires on food (like NSM, but insulin pathway instead of 5-HT)
            // Low gain: ASI is more neuroendocrine than fast signaling
            // REF: Bargmann & Horvitz 1991, Beverly 2011
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 10.0, 1.0, 500.0, 5000.0, 0.5), true});
        } else if (starts_with(info.name, "ADL")) {
            // Step 61: ADL — pheromone/nociceptive amphid sensory
            // ON response to repellent (like ASH but weaker)
            // Detects ascarosides, SDS — activated by noxious chemicals
            // REF: Troemel 1997, Jang 2012
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 15.0, 1.0, 200.0, 1000.0, 0.3), false});
        } else if (starts_with(info.name, "AFD")) {
            // AFD: thermosensory neuron — handled by thermo_mappings, not chemo
            // ThermoTransducer: gain=150, baseline=5pA, Tc_tau=3600s(1hr), fast_tau=200ms
            // Low baseline (5pA): avoid tonic over-activation of AIY that disrupts chemotaxis
            // gain=150: strong modulation when approaching/leaving Tc (ratio AFD/ASE~0.78)
            thermo_mappings_.push_back({info.id, ThermoTransducer(150.0, 5.0, 3600000.0, 200.0)});
        } else if (starts_with(info.name, "PHB") || starts_with(info.name, "PHA")) {
            // Step 81: PHB/PHA — phasmid tail chemosensory
            // PHB: senses repellent at TAIL position (Hilliard 2002)
            // PHA: senses food/pheromone at tail
            // Handled separately in apply_tail_chemosensation() (tail position sampling)
        } else if (!starts_with(info.name, "ALM") && !starts_with(info.name, "PLM")
                   && !starts_with(info.name, "AVM")
                   && !starts_with(info.name, "ADF") && !starts_with(info.name, "AWB")
                   && !starts_with(info.name, "ASJ") && !starts_with(info.name, "ASK")) {
            // Non-touch sensory neurons: low baseline
            other_sensory_ids_.push_back(info.id);
            // ALM/PLM/AVM excluded: zero baseline, only activated by wall collision/tap
            // ADF excluded: driven by sickness_ state (Step 26)
            // ASJ/ASK excluded: Step 55 — driven by light field (LITE-1 photoreceptor)
        }
    }


    // 9. Build proprioceptive mappings (motor neuron → body segment for MEC channel)
    // Step 29: Proprioceptive wave propagation (Wen 2012 Neuron, Boyle 2012)
    // B-class: sequential sensing inside PREVIOUS unit's territory
    //   SMD(0-3) -> DB01 senses seg2 -> DB01(4-9) -> VB02 senses seg7
    //   -> VB02(10-19) -> DB03 senses seg15 -> DB03(20-29)
    // D/V alternation relay: DB01(+curv) -> VB02(-curv) -> DB03(+curv) = S-wave
    // Proprioceptive wave propagation setup
    auto add_pm = [&](const char* name, int seg, int start, int end, bool dorsal) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0) proprio_mappings_.push_back({id, seg, start, end, dorsal});
    };
    // Step 29/39: B-class FORWARD wave (Wen 2012, Boyle 2012)
    // Each B-neuron senses curvature in ANTERIOR neighbor's territory → HEAD→TAIL wave
    // Step 39: expanded from 3 to 7 units for continuous coverage
    add_pm("DB01", 2,  0,  4,  true);   // senses SMD territory (seg 0-3)
    add_pm("DB02", 6,  4,  9,  true);   // senses DB01 territory
    add_pm("DB03", 11, 9,  14, true);   // senses DB02 territory
    add_pm("DB04", 16, 14, 19, true);   // senses DB03 territory
    add_pm("DB05", 21, 19, 24, true);   // senses DB04 territory
    add_pm("DB06", 26, 24, 29, true);   // senses DB05 territory
    add_pm("DB07", 32, 29, 35, true);   // senses DB06 territory
    add_pm("VB01", 2,  0,  4,  false);
    add_pm("VB02", 6,  4,  9,  false);
    add_pm("VB03", 11, 9,  14, false);
    add_pm("VB04", 16, 14, 19, false);
    add_pm("VB05", 21, 19, 24, false);
    add_pm("VB06", 26, 24, 29, false);
    add_pm("VB07", 32, 29, 35, false);
    // Step 94: VB08-11 forward proprioception (added Step 87, mappings were missing)
    add_pm("VB08", 27, 25, 29, false);  // senses VB07 territory
    add_pm("VB09", 30, 29, 32, false);  // senses VB08 territory
    add_pm("VB10", 34, 32, 36, false);  // senses VB09 territory
    add_pm("VB11", 37, 36, 39, false);  // senses VB10 territory

    // Step 94: A-class BACKWARD wave — REVERSED proprioceptive direction
    // Each A-neuron senses curvature in POSTERIOR neighbor's territory → TAIL→HEAD wave
    // REF: Kawano 2011 JNeurosci — backward wave propagates tail to head
    //      Wen 2012 — A-class MNs use same stretch-receptor mechanism as B-class
    //      Gao 2018 eLife — A-class proprioceptive coupling for backward locomotion
    // DA: 9 dorsal A-class, each senses POSTERIOR neighbor
    add_pm("DA01", 10, 4,  8,  true);   // senses DA02 territory (8-12)
    add_pm("DA02", 14, 8,  12, true);   // senses DA03 territory (12-16)
    add_pm("DA03", 18, 12, 16, true);   // senses DA04 territory (16-20)
    add_pm("DA04", 22, 16, 20, true);   // senses DA05 territory (20-25)
    add_pm("DA05", 27, 20, 25, true);   // senses DA06 territory (25-29)
    add_pm("DA06", 31, 25, 29, true);   // senses DA07 territory (29-33)
    add_pm("DA07", 35, 29, 33, true);   // senses DA08 territory (33-38)
    add_pm("DA08", 40, 33, 38, true);   // senses DA09 territory (38-42)
    add_pm("DA09", 44, 38, 42, true);   // senses tail stretch (initiates wave)
    // VA: 12 ventral A-class, each senses POSTERIOR neighbor
    add_pm("VA01", 8,  4,  7,  false);  // senses VA02 territory (7-10)
    add_pm("VA02", 11, 7,  10, false);  // senses VA03 territory (10-13)
    add_pm("VA03", 14, 10, 13, false);  // senses VA04 territory (13-16)
    add_pm("VA04", 17, 13, 16, false);  // senses VA05 territory (16-19)
    add_pm("VA05", 20, 16, 19, false);  // senses VA06 territory (19-22)
    add_pm("VA06", 23, 19, 22, false);  // senses VA07 territory (22-25)
    add_pm("VA07", 27, 22, 25, false);  // senses VA08 territory (25-29)
    add_pm("VA08", 30, 25, 29, false);  // senses VA09 territory (29-32)
    add_pm("VA09", 33, 29, 32, false);  // senses VA10 territory (32-35)
    add_pm("VA10", 37, 32, 35, false);  // senses VA11 territory (35-39)
    add_pm("VA11", 40, 35, 39, false);  // senses VA12 territory (39-42)
    add_pm("VA12", 44, 39, 42, false);  // senses tail stretch (initiates wave)


    // Initialize transducers with current concentration at head
    // Step 41: use correct signal source per transducer type
    // NSM/CEP use food_density (narrow sigma), others use volatile chemical field
    double init_vol = environment_.sample_chemical(body_.get_head_position());
    double init_food = environment_.sample_food_density(body_.get_head_position());
    for (auto& cm : chemo_mappings_) {
        cm.transducer.reset(cm.uses_food_density ? init_food : init_vol);
    }

    // 10b. Setup temperature field and thermosensory transducers (Step 23)
    // Gradient: warm on LEFT, cold on RIGHT → opposes food direction (right/up)
    // T(x) = 20 + (-0.5)*(x-25) → x=0: 32.5°C, x=25: 20°C, x=50: 7.5°C
    // Tc = 22.5°C → target temperature is at x=20 (LEFT of start)
    // This creates a conflict: food pulls RIGHT, Tc pulls LEFT
    environment_.set_temperature_gradient(20.0, {-1.0, 0.0}, 0.5);
    cultivation_temp_ = 22.5;  // worm "raised" at 22.5°C → wants to go left
    double init_temp = environment_.sample_temperature(body_.get_head_position());
    for (auto& tm : thermo_mappings_) {
        tm.transducer.reset(init_temp);
        tm.transducer.set_cultivation_temp(cultivation_temp_);
    }

    // 11. Setup neuromodulation (Step 20, Layer 6)
    setup_neuromodulation();

    // 12. Setup short-term plasticity per circuit (Step 21)
    setup_stp_params();

    // 13. Setup GPU compute backend (Step 22)
    setup_gpu_backend();

    LOG_INFO("Chemosensory: ", chemo_mappings_.size(), " neurons with gradient transduction");
    LOG_INFO("Other sensory: ", other_sensory_ids_.size(), " neurons, baseline ", sensory_baseline_, " pA");
    LOG_INFO("Proprioceptive MEC: ", proprio_mappings_.size(), " motor neuron stretch mappings");
    LOG_INFO("Neuromodulators: ", neuromod_.modulators().size(), " species configured");

    // Performance: one-time cache of neuron IDs, typed pointers, synapse indices
    cache_neuron_ids_and_synapses();

    // Step 41: Warmup — let network equilibrate, then reset neuromodulators
    // Initial neuron transients cause brief spurious release that inflates 5-HT/DA/OA
    // 50 steps × 10ms = 500ms is enough for membrane potentials to settle
    for (int i = 0; i < 50; ++i) step();
    neuromod_.reset_concentrations();
    current_time_ = 0.0;  // reset clock so simulation starts at t=0
    body_.initialize(Vector2d{25.0, 25.0}, 0.0);  // reset body to start position

    LOG_INFO("Initialization complete. dt = ", dt_, " ms");
}

void SimulationEngine::initialize(const Config& config) {
    dt_ = config.get_double("dt", 0.5);

    std::string neurons_file = config.get_string("neurons_file", "");
    if (neurons_file.empty()) {
        initialize_default();
        return;
    }

    // Load from CSV files
    auto neuron_infos = ConnectomeLoader::load_neurons(neurons_file);
    std::unordered_map<std::string, int> name_to_id;
    for (auto& ni : neuron_infos) name_to_id[ni.name] = ni.id;

    std::string synapses_file = config.get_string("synapses_file", "");
    auto synapse_infos = ConnectomeLoader::load_synapses(synapses_file, name_to_id);

    std::string gj_file = config.get_string("gap_junctions_file", "");
    auto gj_infos = ConnectomeLoader::load_gap_junctions(gj_file, name_to_id);

    connectome_.build(neuron_infos, synapse_infos, gj_infos);

    neurons_.clear();
    for (auto& info : neuron_infos) {
        neurons_.push_back(NeuronFactory::create(info));
    }

    body_.initialize(Vector2d{25.0, 25.0}, 0.0);
    motor_controller_.initialize(name_to_id);

    double arena_w = config.get_double("arena_width", 50.0);
    double arena_h = config.get_double("arena_height", 50.0);
    environment_.initialize(arena_w, arena_h);
}

// === Performance: one-time cache of neuron IDs, typed pointers, synapse indices ===
void SimulationEngine::cache_neuron_ids_and_synapses() {
    const auto& ninfos = connectome_.neuron_infos();
    int nn = static_cast<int>(neurons_.size());

    // 1. Auto-register ALL neuron IDs by exact name (Step 52)
    nid_.clear();
    for (auto& ni : ninfos) nid_[ni.name] = ni.id;

    // 2. Auto-register prefix-based neuron groups
    nids_.clear();
    static const char* prefixes[] = {
        "AIB", "ADF", "AIY", "AWB", "AIZ",
        "ALM", "PLM", "OLQ", "CEP", "URX", "AUA", "BAG", "PVD",
        "HSN", "VC", "RIC", "MC", "M3", "I1", "PVC", "RMG", "ASH", "FLP",
        "PHB", "PHA",
    };
    for (auto prefix : prefixes) {
        auto& group = nids_[prefix];
        for (auto& ni : ninfos)
            if (starts_with(ni.name, prefix)) group.push_back(ni.id);
    }
    // Composite group: head_motor = SMD + RMD
    {
        auto& hm = nids_["head_motor"];
        for (auto& ni : ninfos)
            if (starts_with(ni.name, "SMD") || starts_with(ni.name, "RMD"))
                hm.push_back(ni.id);
    }

    // 3. Cached typed pointers (eliminate per-step dynamic_cast)
    auto sc = [&](int id) -> SingleCompartmentNeuron* {
        return (id >= 0 && id < nn) ? dynamic_cast<SingleCompartmentNeuron*>(neurons_[id].get()) : nullptr;
    };
    smd_scn_[0] = sc(nid("SMDDL"));
    smd_scn_[1] = sc(nid("SMDVL"));
    smd_scn_[2] = sc(nid("SMDDR"));
    smd_scn_[3] = sc(nid("SMDVR"));
    int rial = nid("RIAL"), riar = nid("RIAR");
    ria_mcn_[0] = (rial >= 0 && rial < nn)
        ? dynamic_cast<MultiCompartmentNeuron*>(neurons_[rial].get()) : nullptr;
    ria_mcn_[1] = (riar >= 0 && riar < nn)
        ? dynamic_cast<MultiCompartmentNeuron*>(neurons_[riar].get()) : nullptr;

    // 4. Pre-index learning synapses (eliminate per-update full scan)
    const auto& synapses = connectome_.synapses();
    awc_aiy_syn_indices_.clear();
    aser_syn_indices_.clear();
    awc_syn_indices_.clear();
    for (size_t i = 0; i < synapses.size(); ++i) {
        int pre = synapses[i].pre_id(), post = synapses[i].post_id();
        if (pre < 0 || pre >= nn || post < 0 || post >= nn) continue;
        const auto& pname = ninfos[pre].name;
        if (pname.compare(0, 3, "AWC") == 0) {
            awc_syn_indices_.push_back(i);
            if (ninfos[post].name.compare(0, 3, "AIY") == 0)
                awc_aiy_syn_indices_.push_back(i);
        }
        if (pname.compare(0, 4, "ASER") == 0)
            aser_syn_indices_.push_back(i);
    }

    update_awc_pref_cache();
    LOG_INFO("Neuron ID cache: ", nid_.size(), " names, ", nids_.size(), " groups; ",
             awc_aiy_syn_indices_.size(), " AWC→AIY, ",
             aser_syn_indices_.size(), " ASER→*, ", awc_syn_indices_.size(), " AWC→* synapses");
}

void SimulationEngine::update_awc_pref_cache() {
    const auto& synapses = connectome_.synapses();
    double sum_wmod = 0.0; int count = 0;
    for (size_t idx : awc_aiy_syn_indices_) {
        sum_wmod += synapses[idx].weight_mod();
        count++;
    }
    if (count > 0) {
        awc_pref_cached_ = (sum_wmod / count - 0.55) * 3.0;
        if (awc_pref_cached_ > 1.0) awc_pref_cached_ = 1.0;
        if (awc_pref_cached_ < -2.0) awc_pref_cached_ = -2.0;
    } else {
        awc_pref_cached_ = 1.0;
    }
}

void SimulationEngine::step() {
    // 0. Sync tuning params to subsystems
    connectome_.set_synapse_scale(static_cast<double>(params.synapse_scale));

    // 1. Environment update
    environment_.step(dt_ * 0.001);

    // 2. Sensory input: chemosensory neurons detect gradient, others get baseline
    // These use set_external_current() → writes to I_ext_ → survives I_syn_ reset
    // REF: Bargmann 2006, Suzuki 2008
    apply_sensory_input();

    // 2c. Touch stimulus: wall collision → ALM/PLM activation (Step 18)
    // Uses set_external_current() → I_ext_ → survives reset
    apply_touch_stimulus();
    apply_sensitization();  // Step 79: nociceptive sensitization → touch pool boost

    // 3. Head motor tonic: upstream interneuron drive (RIA→SMD already in connectome)
    // Uses set_external_current() → I_ext_ → survives reset
    apply_head_tonic();

    // 4. Proprioceptive stretch: set MEC channel stretch from body curvature
    // The stretch goes through the ion channel → membrane equation (no current injection)
    // REF: Wen et al. 2012 - proprioceptive coupling within motor neurons
    apply_proprioceptive_stretch();

    // 5. Compute synaptic currents (chemical + electrical)
    // NOTE: This RESETS I_syn_ for all neurons, then adds connectome synaptic currents.
    // ALL add_synaptic_current() calls MUST come AFTER this point!
    if (use_gpu_ && gpu_backend_) {
        // GPU path: upload voltages, compute on GPU, download currents
        int nn = static_cast<int>(neurons_.size());
        for (int i = 0; i < nn; ++i) {
            gpu_V_[i] = static_cast<float>(neurons_[i]->get_membrane_potential());
        }
        gpu_backend_->compute_synaptic_currents(
            gpu_V_, gpu_I_, static_cast<float>(dt_),
            static_cast<float>(connectome_.get_synapse_scale()), nn);

        // Apply GPU-computed synaptic currents to neurons
        for (auto& n : neurons_) n->reset_synaptic_current();
        for (int i = 0; i < nn; ++i) {
            if (gpu_I_[i] != 0.0f) {
                neurons_[i]->add_synaptic_current(static_cast<double>(gpu_I_[i]));
            }
        }
        // Gap junctions still on CPU (few, bidirectional)
        connectome_.compute_gap_junction_currents(neurons_);
    } else {
        connectome_.compute_synaptic_currents(neurons_, dt_);
    }

    // === All add_synaptic_current() calls below — safe from I_syn_ reset ===

    // Step 96: NPR-1 tonic inhibition on AUA and RMG (moved from apply_sensory_systems)
    // MUST be after I_syn_ reset — add_synaptic_current before compute_synaptic_currents is lost!
    {
        int nn = static_cast<int>(neurons_.size());
        // AUA: NPR-1 dampens O₂ relay (Laurent 2015 eLife)
        for (int id : nids("AUA")) {
            if (id >= 0 && id < nn)
                neurons_[id]->add_synaptic_current(npr1_aua_);
        }
        // RMG: NPR-1 dampens social hub (Macosko 2009 Nature)
        // N2 (npr1_rmg_=-20): RMG suppressed → solitary
        // Hawaiian (npr1_rmg_=0): RMG active → social aggregation
        for (int id : nids("RMG")) {
            if (id >= 0 && id < nn)
                neurons_[id]->add_synaptic_current(npr1_rmg_);
        }
    }

    // Step 43: ADF sickness 5-HT → MOD-1 ⊣ AIY/AIZ (direct current injection)
    // Biological mechanism: sickness → ADF TPH-1 upregulation → LOCAL 5-HT release
    // → MOD-1 (Cl⁻ channel) on AIY/AIZ → hyperpolarization → approach suppressed
    // NOT as synapse: ADF baseline V=-40mV → release=0.27 → disrupts healthy chemotaxis
    // NOT as neuromod: ADF removed as 5-HT source in Step 41 (tonic off-food inflation)
    // Direct injection: zero baseline effect, scales linearly with sickness
    // REF: Zhang 2005 Nature, Ha 2010 Neuron, Frontiers Immunol 2024
    if (sickness_ > 0.01) {
        int nn = static_cast<int>(neurons_.size());
        double I_mod1 = mod1_aiy_gain_ * sickness_;  // up to -12pA at sickness=1
        for (int aiy_id : nids("AIY")) {
            if (aiy_id >= 0 && aiy_id < nn)
                neurons_[aiy_id]->add_synaptic_current(I_mod1);
        }
        double I_mod1_aiz = mod1_aiz_gain_ * sickness_;
        for (int aiz_id : nids("AIZ")) {
            if (aiz_id >= 0 && aiz_id < nn)
                neurons_[aiz_id]->add_synaptic_current(I_mod1_aiz);
        }
    }

    // 2b. Thermosensory input: AFD samples temperature field (Step 23)
    // Uses add_synaptic_current() → MUST be after reset
    apply_thermo_input();

    // 2c. Step 81: Tail chemosensation — PHB/PHA sample at tail position
    apply_tail_chemosensation();

    // Step 15/19: Weathervane — gradient ⊥ heading → SMD bias (Iino & Yoshida 2009)
    apply_weathervane();

    // Step 19: RIA → SMD neuromodulation via CCA-1 threshold shift
    apply_ria_smd_modulation();

    // 5a2. Step 24: Pharyngeal CPG — MC/M3 drive pump, 5-HT/OA modulate
    apply_pharyngeal_modulation();  // 5-HT→MC excitation, OA→MC inhibition
    update_pharynx();               // pharyngeal muscle AP + food ingestion → satiety

    // 5a3. Step 45: NSM enteric drive — pump_rate → ASIC → NSM activation → 5-HT
    // NSM is a pharyngeal neuron that detects food INGESTION via ASIC channels
    // (DEL-7/DEL-3), not food proximity. pump_rate_hz is the correct input signal.
    // REF: Randi 2018 Cell — ASICs mediate food responses in NSM
    //      Flavell 2013 Cell — NSM tonically active on food (= while pumping)
    {
        double pr = pharynx_.pump_rate_hz();
        // I = gain × (rate / (rate + half_max)) + baseline
        // Step 85: gain 30→50pA. Step 57 ion channel changes pushed NSM resting
        // more negative → S(release) barely above threshold → 5-HT≈0. Need stronger drive.
        // At 4Hz (on food): I = 50×(4/6)+1 = 34 pA → S(V)≈0.9 → strong 5-HT
        // At 2Hz (edge):    I = 50×(2/4)+1 = 26 pA → S(V)≈0.7 → moderate
        // At 0Hz (off food): I = 1 pA → S(V)≈0.1 → no 5-HT release
        double nsm_drive = 50.0 * (pr / (pr + 2.0)) + 1.0;
        int n = static_cast<int>(neurons_.size());
        if (nid("NSML") >= 0 && nid("NSML") < n) neurons_[nid("NSML")]->set_external_current(nsm_drive);
        if (nid("NSMR") >= 0 && nid("NSMR") < n) neurons_[nid("NSMR")]->set_external_current(nsm_drive);
    }

    // 5b. Update satiety effects (RIC tonic drive, NSM suppression, chemotaxis)
    update_satiety();

    // 5b2. Update food memory / ARS (Step 20d)
    update_food_memory();

    // 5b3. Gradient-dependent klinokinesis (Step 21d)
    // No gradient → high pirouette rate → local search (Calhoun 2014 eLife)
    apply_gradient_klinokinesis();

    // 5b4. Salt chemotaxis learning (Step 21c)
    update_salt_learning();

    // 5b5. Step 26: Pathogen avoidance learning (Zhang 2005 Nature)
    update_sickness();           // accumulate sickness from toxic food
    update_pathogen_learning();  // AWC synapse plasticity flip
    apply_synaptic_forgetting(); // Step 62: slow w_mod→1.0 drift (sleep suppresses)

    // 5b5b. Step 63: INS-1 insulin signaling (Lin 2010 JNeurosci)
    update_ins1();               // compute INS-1 from satiety + sickness
    apply_ins1_modulation();     // INS-1 → DAF-2 ⊣ AWC/AIA/AIY

    // 5b6. Step 27: Sleep / Quiescence (Lethargus)
    update_fatigue();            // fatigue accumulation → RIS activation
    apply_sleep_effects();       // FLP-11 → global motor inhibition

    // 5b7. Step 56: Defecation Motor Program (DMP)
    update_defecation();         // 45s intestinal pacemaker → AVL/DVB activation

    // 5c. Neuromodulation update (Step 20, Layer 6)
    // Slow timescale: 5-HT/DA/OA concentrations rise/fall over seconds
    // Effects: tonic currents on target neurons, speed modulation
    neuromod_.update(neurons_, dt_);

    // Step 76: Enhanced Slowing Response — hunger-dependent MOD-1 amplification
    // Must be AFTER neuromod_.update() (needs 5-HT concentration)
    // Must be BEFORE neuron step() (applies current to AIY/PVC/RIC)
    apply_esr_modulation();

    // Neuromodulator effects on muscle force (replaces old SPEED_SCALE bypass)
    // 5-HT(-0.60), OA(+0.35), PDF(+0.25), FLP-11(-0.95) now modulate
    // muscle force output via MuscleSystem::neuromod_gain_, not speed directly.
    // Speed emerges from: muscle_force × locomotion_efficiency / drag
    double muscle_gain = neuromod_.get_muscle_gain();
    if (muscle_gain > 3.0) muscle_gain = 3.0;
    if (muscle_gain < 0.05) muscle_gain = 0.05;
    body_.muscles().set_neuromod_gain(muscle_gain);

    // 6. Update all neuron membrane potentials
    for (auto& neuron : neurons_) {
        neuron->step(dt_);
    }

    // 7. Motor output: motor neurons → muscle activations
    body_.muscles().reset_inputs();  // clear all channels before any motor input
    motor_controller_.update(neurons_, body_);

    // 7b. Specialized motor inputs via boost channel (AFTER motor_controller)
    // These add on top of normal MN drive, not cleared by reset_inputs
    apply_riv_omega();      // Step 117: RIV burst → head muscle boost (NMJ 40x)
    apply_smb_neck_bias();  // Step 117: klinotaxis → head muscle boost

    // 8. Command neuron balance → locomotion direction (Step 66: SOLE mechanism)
    // AVA dominant → reverse, AVB dominant → forward
    // Step 66: Removed set_locomotion_state(0,1) override — AVA/AVB balance is now
    // the ONLY source of locomotion direction. Reversals emerge from:
    //   - Spontaneous: ion channel noise (3pA) → stochastic AVA activation (Roberts 2016)
    //   - Sensory: ASE→AIB→AVA pathway (dC/dt modulation, Piggott 2011)
    //   - Nociceptive: ASH→AVA direct (GLR-1, Piggott 2011)
    //   - Food edge: AVA current injection (CEP→DA→AIB→AVA, eLife 2024)
    // REF: Roberts 2016 eLife — neuronal flip-flop with reciprocal inhibition
    {
        double ava_rel = 0.0, avb_rel = 0.0;
        int n = static_cast<int>(neurons_.size());
        if (nid("AVAL") >= 0 && nid("AVAL") < n) ava_rel += neurons_[nid("AVAL")]->get_transmitter_release_rate();
        if (nid("AVAR") >= 0 && nid("AVAR") < n) ava_rel += neurons_[nid("AVAR")]->get_transmitter_release_rate();
        if (nid("AVBL") >= 0 && nid("AVBL") < n) avb_rel += neurons_[nid("AVBL")]->get_transmitter_release_rate();
        if (nid("AVBR") >= 0 && nid("AVBR") < n) avb_rel += neurons_[nid("AVBR")]->get_transmitter_release_rate();
        ava_rel *= 0.5; avb_rel *= 0.5; // average L/R
        body_.set_locomotion_state(avb_rel, ava_rel);
        body_.set_omega_active(riv_omega_active_);

        // Step 66: Detect reversal state from AVA activity (for RIV omega tracking)
        // Schmitt trigger with hysteresis (Roberts 2016: AVA bistable -17/-32 mV)
        // Enter reversal: ava_rel > 0.35 AND ava_rel > avb_rel AND not in refractory
        // Exit reversal: ava_rel < 0.15 AND min duration 300ms elapsed
        // Refractory: 2000ms after reversal end (prevents rapid re-triggering)
        bool was_reversing = is_reversing_;
        if (!is_reversing_) {
            bool past_refractory = (current_time_ > reversal_refractory_end_);
            is_reversing_ = past_refractory && (ava_rel > 0.35) && (ava_rel > avb_rel);
        } else {
            double rev_elapsed = current_time_ - reversal_start_time_;
            // Exit: AVA drops below low threshold after minimum duration
            // Also force-exit after 3000ms (max reversal, prevents stuck state)
            if ((rev_elapsed > 300.0 && ava_rel < 0.15) || rev_elapsed > 3000.0) {
                is_reversing_ = false;
            }
        }

        // Reversal state transitions → RIV omega pulse machinery
        if (is_reversing_ && !was_reversing) {
            reversal_start_time_ = current_time_;
            // Snapshot head muscle D/V balance at reversal start
            // force_diff reflects SMD oscillation phase (random at reversal onset)
            // Positive = dorsal bend, negative = ventral bend
            double fd_snap = 0.0;
            for (int i = 0; i < 6; ++i) {
                fd_snap += body_.muscles().get_force_differential(i);
            }
            pre_rev_dorsal_tone_ = fd_snap / 6.0;
        }
        if (!is_reversing_ && was_reversing) {
            reversal_duration_ = current_time_ - reversal_start_time_;
            reversal_refractory_end_ = current_time_ + 2000.0;  // 2s refractory
            // Only fire RIV pulse if reversal was at least 200ms (filter out transients)
            if (reversal_duration_ > 200.0) {
                riv_post_rev_time_ = current_time_;
                double ta_conc = neuromod_.get_concentration("TA");
                double base_amp = static_cast<double>(params.pulse_amp) * ta_conc;
                double heading = body_.get_head_angle();
                Vector2d grad = environment_.chemical_field().gradient(body_.get_head_position());
                double grad_perp = -std::sin(heading) * grad.x + std::cos(heading) * grad.y;
                double grad_lr = std::tanh(grad_perp * 50.0);
                double posture_lr = -pre_rev_dorsal_tone_ * 10.0;  // force_diff centered at 0
                posture_lr = std::tanh(posture_lr);
                double lr_grad = 0.3 * grad_lr;
                double lr_posture = 0.3 * posture_lr;
                riv_post_rev_amp_l_ = base_amp * (1.0 + lr_grad + lr_posture);
                riv_post_rev_amp_r_ = base_amp * (1.0 - lr_grad - lr_posture);
            }
        }
    }

    // 9. Body physics update
    body_.update_physics(dt_ * 0.001);

    // 9. Callback
    if (step_callback_) {
        step_callback_(*this, step_count_);
    }

    current_time_ += dt_;
    step_count_++;
}

void SimulationEngine::run(double duration_ms) {
    int total_steps = static_cast<int>(duration_ms / dt_);
    LOG_INFO("Running simulation for ", duration_ms, " ms (", total_steps, " steps)...");

    for (int i = 0; i < total_steps; ++i) {
        step();
    }

    LOG_INFO("Simulation complete. Final time: ", current_time_, " ms");
}

// apply_sensory_input(), apply_thermo_input(), apply_tail_chemosensation(),
// apply_touch_stimulus(), apply_sensitization()
//   → moved to apply_sensory_systems.cpp (Step 92)

// apply_head_tonic(), apply_weathervane(), apply_smb_neck_bias(),
// apply_ria_smd_modulation(), apply_proprioceptive_stretch(), apply_riv_omega()
//   → moved to apply_motor_control.cpp (Step 92)

// setup_stp_params(), setup_gpu_backend(), sync_synapses_to_gpu()
//   → moved to setup_gpu_stp.cpp (Step 92)

// setup_neuromodulation() → moved to setup_neuromodulation.cpp (Step 50a)

// update_satiety(), update_food_memory(), apply_gradient_klinokinesis()
//   → moved to update_internal_states.cpp (Step 50)

// update_salt_learning(), update_sickness(), update_pathogen_learning()
//   → moved to update_learning.cpp (Step 50)

// apply_pharyngeal_modulation(), update_pharynx()
//   → moved to update_pharynx_system.cpp (Step 50)

// update_fatigue(), apply_sleep_effects()
//   → moved to update_internal_states.cpp (Step 50)

} // namespace celegans
