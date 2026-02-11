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
            // ASE: detects salt/amino acids (Bargmann 2006)
            // Currently samples same field as AWC for regression safety;
            // soluble_field_ infrastructure ready for future multi-odor routing
            // fast_tau=100ms: captures 2Hz head oscillation for klinotaxis
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 100.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "ASER")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 100.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "AWC")) {
            // AWC: volatile odor channel → samples chem_field_ (food odor)
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 80.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "AWA")) {
            // AWA: volatile odor channel → samples chem_field_ (food odor)
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
            // NSM IDs cached as nsml_id_/nsmr_id_ — drive set in update_pharynx section
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
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 20.0, 1.0, 500.0, 5000.0, 0.5), true});
        } else if (starts_with(info.name, "AFD")) {
            // AFD: thermosensory neuron — handled by thermo_mappings, not chemo
            // ThermoTransducer: gain=150, baseline=5pA, Tc_tau=3600s(1hr), fast_tau=200ms
            // Low baseline (5pA): avoid tonic over-activation of AIY that disrupts chemotaxis
            // gain=150: strong modulation when approaching/leaving Tc (ratio AFD/ASE~0.78)
            thermo_mappings_.push_back({info.id, ThermoTransducer(150.0, 5.0, 3600000.0, 200.0)});
        } else if (!starts_with(info.name, "ALM") && !starts_with(info.name, "PLM")
                   && !starts_with(info.name, "ADF") && !starts_with(info.name, "AWB")) {
            // Non-touch sensory neurons: low baseline
            other_sensory_ids_.push_back(info.id);
            // ALM/PLM excluded: zero baseline, only activated by wall collision
            // ADF excluded: driven by sickness_ state (Step 26)
        }
    }

    // 8. Collect head motor neuron IDs (SMD/RMD receive tonic from upstream)
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "SMD") || starts_with(info.name, "RMD")) {
            head_motor_ids_.push_back(info.id);
        }
    }

    // Step 25: Collect AIB IDs for 5-HT→MOD-1 inhibition
    // Step 26: Collect ADF and AIY IDs for pathogen learning
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "AIB")) {
            aib_ids_.push_back(info.id);
        }
        if (starts_with(info.name, "ADF")) {
            adf_ids_.push_back(info.id);
        }
        if (starts_with(info.name, "AIY")) {
            aiy_ids_.push_back(info.id);
        }
        if (starts_with(info.name, "AWB")) {
            awb_ids_.push_back(info.id);
        }
        if (starts_with(info.name, "AIZ")) {
            aiz_ids_.push_back(info.id);
        }
    }

    // 9. Build proprioceptive mappings (motor neuron → body segment for MEC channel)
    // Step 29: Proprioceptive wave propagation (Wen 2012 Neuron, Boyle 2012)
    // B-class: sequential sensing inside PREVIOUS unit's territory
    //   SMD(0-3) -> DB01 senses seg2 -> DB01(4-9) -> VB02 senses seg7
    //   -> VB02(10-19) -> DB03 senses seg15 -> DB03(20-29)
    // D/V alternation relay: DB01(+curv) -> VB02(-curv) -> DB03(+curv) = S-wave
    // A-class: sync mapping (baseline muscle drive, unchanged from Step 28)
    auto add_pm = [&](const char* name, int seg, int start, int end, bool dorsal) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0) proprio_mappings_.push_back({id, seg, start, end, dorsal});
    };
    // Step 29/39: B-class sequential proprioceptive wave (Wen 2012, Boyle 2012)
    // Each B-neuron senses curvature INSIDE the previous unit's territory.
    // D/V alternation relay: DB01(+) -> VB02(-) -> DB03(+) = S-wave
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
    // A-class: sync mapping (baseline muscle drive, Step 29 rule: A-class unchanged)
    // Step 39: expanded from 3 to 5 units
    add_pm("DA01", 0,  0,  6,  true);
    add_pm("DA02", 8,  4,  12, true);
    add_pm("DA03", 16, 12, 20, true);
    add_pm("DA04", 24, 20, 28, true);
    add_pm("DA05", 32, 28, 36, true);
    add_pm("VA01", 0,  0,  6,  false);
    add_pm("VA02", 8,  4,  12, false);
    add_pm("VA03", 16, 12, 20, false);
    add_pm("VA04", 24, 20, 28, false);
    add_pm("VA05", 32, 28, 36, false);

    // 10. Collect touch neuron IDs (Step 18) + pharyngeal neuron IDs (Step 24)
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "ALM")) alm_ids_.push_back(info.id);
        if (starts_with(info.name, "PLM")) plm_ids_.push_back(info.id);
        if (starts_with(info.name, "OLQ")) olq_ids_.push_back(info.id);  // Step 33
        if (starts_with(info.name, "CEP")) cep_ids_.push_back(info.id);  // Step 47b
        if (starts_with(info.name, "URX")) urx_ids_.push_back(info.id);  // Step 34
        if (starts_with(info.name, "AUA")) aua_ids_.push_back(info.id); // Step 34
        if (info.name == "AQR") aqr_id_ = info.id;   // Step 34
        if (info.name == "PQR") pqr_id_ = info.id;   // Step 34
        if (starts_with(info.name, "BAG")) bag_ids_.push_back(info.id); // Step 35
        if (info.name == "DVA") dva_id_ = info.id;                     // Step 36
        if (starts_with(info.name, "PVD")) pvd_ids_.push_back(info.id); // Step 36
        if (starts_with(info.name, "HSN")) hsn_ids_.push_back(info.id); // Step 38
        if (starts_with(info.name, "VC")) vc_ids_.push_back(info.id);   // Step 38
        if (starts_with(info.name, "RIC")) ric_ids_.push_back(info.id);
        // Step 24: Pharyngeal neurons
        if (starts_with(info.name, "MC") && info.name.size() <= 3) mc_ids_.push_back(info.id);
        if (starts_with(info.name, "M3")) m3_ids_.push_back(info.id);
        if (info.name == "M4") m4_id_ = info.id;
        if (starts_with(info.name, "I1")) i1_ids_.push_back(info.id);
        // Step 27: RIS sleep neuron
        if (info.name == "RIS") ris_id_ = info.id;
        // Step 31: RIV omega turn neurons
        if (info.name == "RIVL") rivl_id_ = info.id;
        if (info.name == "RIVR") rivr_id_ = info.id;
        // Step 28: RIA multi-compartment IDs
        if (info.name == "RIAL") rial_id_ = info.id;
        if (info.name == "RIAR") riar_id_ = info.id;
    }

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
    LOG_INFO("Head tonic: ", head_motor_ids_.size(), " head motor neurons, ", head_tonic_, " pA");
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
    // Cached neuron IDs (eliminate per-step hash lookups)
    aval_id_  = connectome_.get_neuron_id("AVAL");
    avar_id_  = connectome_.get_neuron_id("AVAR");
    avbl_id_  = connectome_.get_neuron_id("AVBL");
    avbr_id_  = connectome_.get_neuron_id("AVBR");
    smddl_id_ = connectome_.get_neuron_id("SMDDL");
    smddr_id_ = connectome_.get_neuron_id("SMDDR");
    smdvl_id_ = connectome_.get_neuron_id("SMDVL");
    smdvr_id_ = connectome_.get_neuron_id("SMDVR");
    nsml_id_  = connectome_.get_neuron_id("NSML");
    nsmr_id_  = connectome_.get_neuron_id("NSMR");

    // Cached typed pointers (eliminate per-step dynamic_cast)
    int nn = static_cast<int>(neurons_.size());
    auto sc = [&](int id) -> SingleCompartmentNeuron* {
        return (id >= 0 && id < nn) ? dynamic_cast<SingleCompartmentNeuron*>(neurons_[id].get()) : nullptr;
    };
    smd_scn_[0] = sc(smddl_id_);
    smd_scn_[1] = sc(smdvl_id_);
    smd_scn_[2] = sc(smddr_id_);
    smd_scn_[3] = sc(smdvr_id_);
    ria_mcn_[0] = (rial_id_ >= 0 && rial_id_ < nn)
        ? dynamic_cast<MultiCompartmentNeuron*>(neurons_[rial_id_].get()) : nullptr;
    ria_mcn_[1] = (riar_id_ >= 0 && riar_id_ < nn)
        ? dynamic_cast<MultiCompartmentNeuron*>(neurons_[riar_id_].get()) : nullptr;

    // Pre-index learning synapses (eliminate per-update full scan)
    const auto& synapses = connectome_.synapses();
    const auto& ninfos = connectome_.neuron_infos();
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
    LOG_INFO("Performance cache: ", awc_aiy_syn_indices_.size(), " AWC→AIY, ",
             aser_syn_indices_.size(), " ASER→*, ", awc_syn_indices_.size(), " AWC→* synapses indexed");
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
    body_.set_speed_scale(static_cast<double>(params.speed_scale));

    // 1. Environment update
    environment_.step(dt_ * 0.001);

    // 2. Sensory input: chemosensory neurons detect gradient, others get baseline
    // These use set_external_current() → writes to I_ext_ → survives I_syn_ reset
    // REF: Bargmann 2006, Suzuki 2008
    apply_sensory_input();

    // 2c. Touch stimulus: wall collision → ALM/PLM activation (Step 18)
    // Uses set_external_current() → I_ext_ → survives reset
    apply_touch_stimulus();

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
        for (int aiy_id : aiy_ids_) {
            if (aiy_id >= 0 && aiy_id < nn)
                neurons_[aiy_id]->add_synaptic_current(I_mod1);
        }
        double I_mod1_aiz = mod1_aiz_gain_ * sickness_;
        for (int aiz_id : aiz_ids_) {
            if (aiz_id >= 0 && aiz_id < nn)
                neurons_[aiz_id]->add_synaptic_current(I_mod1_aiz);
        }
    }

    // 2b. Thermosensory input: AFD samples temperature field (Step 23)
    // Uses add_synaptic_current() → MUST be after reset
    apply_thermo_input();

    // 2d. Step 31: RIV-driven omega turn (emergent from TA gating)
    // RIV burst → curvature_bias + omega_mode (replaces hardcoded Step 18)
    apply_riv_omega();

    // Step 15/19: Weathervane — gradient ⊥ heading → SMD bias (Iino & Yoshida 2009)
    apply_weathervane();

    // Step 19: RIA → SMD neuromodulation via CCA-1 threshold shift
    apply_ria_smd_modulation();

    // Step 19 Phase 2: SMB neck curvature bias (klinotaxis effector)
    apply_smb_neck_bias();

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
        // gain=30pA, half_max=2.0Hz, baseline=1.0pA
        // At 4Hz (on food): I = 30×(4/6)+1 = 21 pA → S(V)≈0.8 → strong 5-HT
        // At 2Hz (edge):    I = 30×(2/4)+1 = 16 pA → S(V)≈0.7 → moderate
        // At 0Hz (off food): I = 1 pA → S(V)≈0.1 → no 5-HT release
        double nsm_drive = 30.0 * (pr / (pr + 2.0)) + 1.0;
        int n = static_cast<int>(neurons_.size());
        if (nsml_id_ >= 0 && nsml_id_ < n) neurons_[nsml_id_]->set_external_current(nsm_drive);
        if (nsmr_id_ >= 0 && nsmr_id_ < n) neurons_[nsmr_id_]->set_external_current(nsm_drive);
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

    // 5b6. Step 27: Sleep / Quiescence (Lethargus)
    update_fatigue();            // fatigue accumulation → RIS activation
    apply_sleep_effects();       // FLP-11 → global motor inhibition

    // 5c. Neuromodulation update (Step 20, Layer 6)
    // Slow timescale: 5-HT/DA/OA concentrations rise/fall over seconds
    // Effects: tonic currents on target neurons, speed modulation
    neuromod_.update(neurons_, dt_);

    // Apply neuromodulation + sleep speed scaling to body
    // Step 27: FLP-11 sleep suppression stacks with neuromodulation
    double sleep_speed_factor = 1.0;
    {
        int nn = static_cast<int>(neurons_.size());
        if (ris_id_ >= 0 && ris_id_ < nn) {
            double rv = neurons_[ris_id_]->get_membrane_potential();
            double flp11 = 1.0 / (1.0 + fast_exp(-(rv - (-35.0)) / 5.0));
            sleep_speed_factor = 1.0 - 0.97 * flp11;  // up to 97% speed reduction (near-atonia)
        }
    }
    // Step 44: clamp effective speed_scale to prevent extreme values
    double effective_speed = params.speed_scale * neuromod_.get_speed_scale() * sleep_speed_factor;

    // Step 47b: DA basal slowing — instant, position-dependent speed reduction
    // Biological basis (Sawin 2000 Neuron):
    //   "Basal slowing response is mediated by a dopamine-containing neural circuit
    //    that senses a mechanical attribute of bacteria."
    //   cat-2 mutants (no DA): fail to slow on food (~30% reduction in wild-type)
    //
    // ARCHITECTURE: on_lawn sigmoid directly modulates speed (NOT via DA concentration).
    // DA acts via extrasynaptic DOP-3 volume transmission on motor neurons (Chase 2004),
    // NOT through CEP's connectome synaptic circuit. Driving CEP neurons at high current
    // (40pA) causes OLQ cascade via gap junctions (OLQ→RMD head disruption, OLQ→RIC
    // OA release) that destroys chemotaxis. The on_lawn sigmoid captures the CEP
    // mechanosensory "feet on bacteria" signal directly.
    //
    // on_lawn: 0 off food, 1.0 on food center, instant transition at lawn edge
    // Off food: factor=1.0 (NO effect) → CI preserved
    // On food: factor=0.65 (35% reduction) → matches Sawin 2000 Fig 2
    //
    // REF: Sawin 2000 Neuron — DA basal slowing
    //      Chase 2004 Nature Neurosci — DOP-3 extrasynaptic volume transmission
    {
        double food_at_head = environment_.sample_food_density(body_.get_head_position());
        // Sharp sigmoid: bacterial lawn mechanical edge
        // threshold=0.4 at ~5.4mm from center (food σ=4mm)
        // Symmetric with head poke reversal edge detection (0.4→0.3 transition)
        double on_lawn = 1.0 / (1.0 + fast_exp(-(food_at_head - 0.4) * 20.0));
        double basal_slow = 1.0 - 0.25 * on_lawn;
        if (basal_slow < 0.65) basal_slow = 0.65;  // floor: max 35% reduction
        effective_speed *= basal_slow;
    }

    if (effective_speed > 3.0) effective_speed = 3.0;
    if (effective_speed < 0.1) effective_speed = 0.1;
    body_.set_speed_scale(effective_speed);

    // 6. Update all neuron membrane potentials
    for (auto& neuron : neurons_) {
        neuron->step(dt_);
    }

    // 7. Motor output: motor neurons → muscle activations
    motor_controller_.update(neurons_, body_);

    // 8. Command neuron balance → locomotion direction
    // AVA dominant → reverse, AVB dominant → forward
    {
        double ava_rel = 0.0, avb_rel = 0.0;
        int n = static_cast<int>(neurons_.size());
        if (aval_id_ >= 0 && aval_id_ < n) ava_rel += neurons_[aval_id_]->get_transmitter_release_rate();
        if (avar_id_ >= 0 && avar_id_ < n) ava_rel += neurons_[avar_id_]->get_transmitter_release_rate();
        if (avbl_id_ >= 0 && avbl_id_ < n) avb_rel += neurons_[avbl_id_]->get_transmitter_release_rate();
        if (avbr_id_ >= 0 && avbr_id_ < n) avb_rel += neurons_[avbr_id_]->get_transmitter_release_rate();
        ava_rel *= 0.5; avb_rel *= 0.5; // average L/R
        body_.set_locomotion_state(avb_rel, ava_rel);
    }

    // Step 41: Pirouette reversal overrides locomotion direction
    // The pirouette Poisson process is a decision-layer shortcut (bypasses AVA circuit
    // for WHEN to reverse). Consistently, execution also bypasses command neuron balance
    // to specify backward movement. This avoids AVA injection side effects (AVE→RIV
    // excitation would prevent omega turn CCA-1 h deinactivation).
    // REF: Fang-Yen 2010 — reverse speed ~60% of forward
    if (is_reversing_) {
        body_.set_locomotion_state(0.0, 1.0);  // force backward: fwd=0, rev=1
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

void SimulationEngine::apply_sensory_input() {
    int n = static_cast<int>(neurons_.size());

    // Head sweep sampling: nose position displaced by head curvature
    // This serves BOTH klinokinesis (slow trend) AND klinotaxis (phase-locked).
    // The same ASE neurons carry both signals; downstream circuits separate them:
    //   - Klinokinesis: ASE → AIA → AIB → AVA (slow pirouette modulation, 5s tau)
    //   - Klinotaxis: ASE → AIY → AIZ → SMB (fast neck bias, no adaptation)
    // REF: Izquierdo & Lockery 2010
    Vector2d head_pos = body_.get_head_position();
    double head_angle = body_.get_head_angle();
    double head_curv = body_.get_local_curvature(0);
    double sweep_radius = 1.5;  // mm, curv→displacement gain
    double lateral_offset = head_curv * sweep_radius;
    double nx = -std::sin(head_angle);
    double ny =  std::cos(head_angle);
    Vector2d sample_pos = {head_pos.x + lateral_offset * nx,
                           head_pos.y + lateral_offset * ny};
    double concentration = environment_.sample_chemical(sample_pos);

    // Step 23c: Satiety modulates chemosensory gain (Mori 1995, Tomioka 2006)
    // Sharp sigmoid switch at satiety=0.5:
    //   Hungry (sat<0.3): full chemotaxis (find food!)
    //   Fed (sat>0.7): chemotaxis nearly off (temperature priority)
    double sat_switch = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    double chemo_sat_gain = 1.0 - 0.85 * sat_switch;  // hungry: 1.0, fed: 0.15

    // Step 26b: Sickness suppresses chemosensory gain (illness-induced anorexia)
    // REF: DAF-7 (TGF-β) from ASI reduces food attraction during pathogen exposure
    // Reduces ASE/AWC/AWA neural drive when sick — weathervane unaffected (separate)
    double sick_suppression = 1.0 - 0.85 * sickness_;  // sick: 15% of normal drive

    // Food density at head (narrow σ=3mm for NSM/CEP food detectors)
    double food_density = environment_.sample_food_density(head_pos);

    for (auto& cm : chemo_mappings_) {
        if (cm.neuron_id < 0 || cm.neuron_id >= n) continue;
        // NSM/CEP: use narrow food density, NOT suppressed by sickness OR satiety
        // NSM/CEP detect physical food contact → drive 5-HT/DA unconditionally on food
        // Satiety modulation acts DOWNSTREAM (ASE/AWC/AWA chemotaxis gain), not on NSM
        // BUG FIX: chemo_sat_gain was suppressing NSM to 0.15 when fed → 5-HT=0.019
        double input_conc = cm.uses_food_density ? food_density : concentration;
        double I_sensory = cm.transducer.update(input_conc, dt_);
        double gain_mod = cm.uses_food_density ? 1.0 : (chemo_sat_gain * sick_suppression);
        I_sensory *= static_cast<double>(params.sensory_gain) * gain_mod;
        neurons_[cm.neuron_id]->set_external_current(I_sensory);
    }

    // Step 26b: ASE samples SOLUBLE field (salt/amino acids — independent of food odor)
    // REF: Bargmann 2006 — ASE detects water-soluble ions, not volatile odors
    double soluble_conc = environment_.sample_soluble(sample_pos);
    for (auto& sm : soluble_mappings_) {
        if (sm.neuron_id < 0 || sm.neuron_id >= n) continue;
        double I_sol = sm.transducer.update(soluble_conc, dt_);
        I_sol *= static_cast<double>(params.sensory_gain) * chemo_sat_gain;
        neurons_[sm.neuron_id]->set_external_current(I_sol);
    }

    // Step 25: ASH nociceptors sample repellent field
    // ASH is ON-type: excited by repellent concentration increase
    // REF: Summers 2015 — ASH→AIB→AVA nociceptive avoidance circuit
    double repellent_conc = environment_.sample_repellent(sample_pos);
    for (auto& nm : noci_mappings_) {
        if (nm.neuron_id < 0 || nm.neuron_id >= n) continue;
        double I_noci = nm.transducer.update(repellent_conc, dt_);
        I_noci *= static_cast<double>(params.sensory_gain);
        // No satiety modulation: nociception is not suppressed by feeding state
        // (5-HT suppresses downstream AIB instead — Summers 2015)
        neurons_[nm.neuron_id]->set_external_current(I_noci);
    }

    // Step 26: ADF serotonin neurons — driven by sickness state
    // REF: Zhang 2005 Nature — PA14 exposure → TPH-1 upregulation → ADF 5-HT↑
    // ADF baseline=2pA (low), sickness drives strong depolarization → 5-HT release
    for (int adf_id : adf_ids_) {
        if (adf_id >= 0 && adf_id < n) {
            double I_adf = 0.5 + 30.0 * sickness_;  // 0.5pA baseline (silent), up to 30.5pA when sick
            neurons_[adf_id]->set_external_current(I_adf);
        }
    }

    // Step 43: AWB repulsive olfactory neurons — sense pathogen volatiles
    // AWB detects repulsive odors (1-undecene, serrawettin) at the food/toxin source
    // After aversive learning (sickness > 0), AWB response is amplified
    // AWB→AUA→AVA drives reflexive backward locomotion away from pathogen
    // AWB ⊣ AIY further suppresses approach
    // REF: Troemel 1997 Cell, Ha 2010 Neuron, BMC Biology 2022
    {
        Vector2d head_pos = body_.get_head_position();
        double repellent = environment_.sample_repellent(head_pos);
        for (int awb_id : awb_ids_) {
            if (awb_id >= 0 && awb_id < n) {
                // Base response: low (2pA) — AWB mainly activated after learning
                // Learned amplification: sickness × repellent → strong AWB drive
                double I_awb = 2.0 + awb_pathogen_gain_ * sickness_ * repellent;
                neurons_[awb_id]->set_external_current(I_awb);
            }
        }
    }

    // Step 43: ADF sickness 5-HT → MOD-1 ⊣ AIY/AIZ
    // MOVED to post-reset section in step() — add_synaptic_current() would be
    // wiped by reset_synaptic_current() if called here.

    // Touch/other sensory: low baseline (no active stimulus)
    for (int id : other_sensory_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(sensory_baseline_);
        }
    }
}

void SimulationEngine::apply_thermo_input() {
    // Step 23: AFD thermosensory neurons sample temperature at head position
    // AFD responds to temperature relative to cultivation temperature (Tc)
    // AFD→AIY: excitatory, drives thermotaxis via shared AIY→RIA→SMD pathway
    // REF: Mori & Ohshima 1995, Clark 2006, Luo 2014 PNAS
    int n = static_cast<int>(neurons_.size());
    Vector2d head_pos = body_.get_head_position();
    double temperature = environment_.sample_temperature(head_pos);

    // Step 23c: Satiety modulates thermosensory gain (Mori 1995)
    // Sharp sigmoid switch at satiety=0.5:
    //   Hungry: weak thermotaxis (food priority)
    //   Fed: strong thermotaxis (navigate to cultivation temperature)
    double sat_switch_t = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    double thermo_sat_gain = 0.2 + 1.8 * sat_switch_t;  // hungry: 0.2, fed: 2.0

    for (auto& tm : thermo_mappings_) {
        if (tm.neuron_id < 0 || tm.neuron_id >= n) continue;
        double I_thermo = tm.transducer.update(temperature, dt_);
        I_thermo *= thermo_sat_gain;
        // AFD current adds to (not replaces) any existing external current
        neurons_[tm.neuron_id]->add_synaptic_current(I_thermo);
    }
}

void SimulationEngine::apply_head_tonic() {
    // Head motor neurons (SMD/RMD) receive tonic excitatory input
    // from upstream interneurons (RIA synapses already in connectome)
    // This small tonic keeps the head circuit near oscillation threshold
    // The actual oscillation emerges from: CCA-1 rebound + cross-inhibition
    // REF: Hendricks 2012, Shen 2016
    //
    // Step 27: During sleep, FLP-11 suppresses upstream interneuron drive
    // RIS inhibits RIA/RIB (approach circuit) → tonic drive drops
    // REF: Konietzka 2020 — RIS depolarization → cessation of head movement
    double tonic = head_tonic_;
    if (is_sleeping_ && ris_id_ >= 0 && ris_id_ < static_cast<int>(neurons_.size())) {
        double rv = neurons_[ris_id_]->get_membrane_potential();
        double flp11 = 1.0 / (1.0 + fast_exp(-(rv - (-35.0)) / 5.0));
        tonic *= (1.0 - 0.95 * flp11);  // near-zero tonic during deep sleep
    }
    int n = static_cast<int>(neurons_.size());
    for (int id : head_motor_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(tonic);
        }
    }

    // Step 31: RIV baseline + post-reversal pulse
    //
    // Tonic: 2 pA (below CCA-1 oscillation threshold ~3 pA)
    //   During reversal: TA→-20×[TA] suppresses RIV → CCA-1 h deinactivates
    //   After reversal: TA decays → tonic pushes V toward CCA-1 window
    //
    // Post-reversal pulse: models reversal→forward transition signal
    //   Biological basis (Donnelly 2013, Neural Sequences 2024):
    //   "omega initiated when animal reinitiates forward locomotion"
    //   Pulse amplitude ∝ [TA] at reversal end (set in update_pirouette_state)
    //   Decays with tau=200ms — enough to trigger CCA-1 burst within h recovery window
    //
    double riv_tonic = static_cast<double>(params.riv_tonic);  // pA baseline
    if (is_sleeping_) riv_tonic *= 0.1;

    // Post-reversal pulse: decaying excitation for ~500ms after reversal ends
    // L/R asymmetric pulse → gradient-dependent omega direction
    double riv_pulse_l = 0.0, riv_pulse_r = 0.0;
    double dt_since_rev = current_time_ - riv_post_rev_time_;
    if (dt_since_rev >= 0.0 && dt_since_rev < 600.0) {
        double decay = fast_exp(-dt_since_rev / 400.0);
        if (riv_post_rev_amp_l_ > 1.0) riv_pulse_l = riv_post_rev_amp_l_ * decay;
        if (riv_post_rev_amp_r_ > 1.0) riv_pulse_r = riv_post_rev_amp_r_ * decay;
    }

    if (rivl_id_ >= 0 && rivl_id_ < n) neurons_[rivl_id_]->set_external_current(riv_tonic + riv_pulse_l);
    if (rivr_id_ >= 0 && rivr_id_ < n) neurons_[rivr_id_]->set_external_current(riv_tonic + riv_pulse_r);
}

void SimulationEngine::apply_weathervane() {
    // Weathervane mechanism: gradient ⊥ heading → differential SMD drive
    // REF: Iino & Yoshida 2009 — curving rate bias = 12.7 °/mm × ∇C_normal
    // Implementation: compute gradient perpendicular to heading direction,
    // then apply differential current to dorsal vs ventral SMD neurons.
    // This biases the half-center oscillator, causing gradual curving toward food.
    //
    // Neural basis: head oscillation samples gradient laterally → ASE → AIZ → SMD
    // We approximate this by directly biasing SMD based on the normal gradient component.

    Vector2d head_pos = body_.get_head_position();
    double heading = body_.get_head_angle();
    double cos_h = std::cos(heading);
    double sin_h = std::sin(heading);
    double weathervane_gain = static_cast<double>(params.weathervane_gain);

    // Step 23c: Satiety modulates chemotaxis weathervane gain
    double sat_switch_wv = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    double chemo_wv_gain = 1.0 - 0.85 * sat_switch_wv;  // fed: 0.15, hungry: 1.0

    // Step 26b: DUAL-CHANNEL WEATHERVANE
    // Channel 1: Food odor (volatile, AWC/AWA) — modulated by learned preference
    // Channel 2: Soluble (salt/amino acids, ASE) — NOT affected by pathogen learning
    // REF: Bargmann 2006 — AWC and ASE detect independent chemical modalities

    // --- Channel 1: Food odor weathervane (learnable) ---
    Vector2d grad = environment_.chemical_field().gradient(head_pos);
    double grad_normal = -sin_h * grad.x + cos_h * grad.y;

    // AWC preference: derived from mean AWC→AIY w_mod
    // Asymmetric scaling: avoidance stronger than attraction
    // (missing food = minor cost; eating toxin = sickness = high cost)
    // w_mod=1.0 → pref=+1.0 (naive, attract to food odor)
    // w_mod=0.5 → pref=-0.15 (slight avoidance)
    // w_mod=0.1 → pref=-1.35 (strong repulsion, 1.35× attract gain)
    double awc_pref = awc_pref_cached_;  // updated by update_awc_pref_cache() after learning
    double odor_bias = weathervane_gain * grad_normal * chemo_wv_gain * awc_pref;

    // --- Channel 2: Soluble (ASE) ---
    // ASE drives klinokinesis (pirouette rate), NOT klinotaxis (weathervane)
    // REF: Iino & Yoshida 2009 — weathervane primarily AWC-mediated
    // Soluble gradient computed for curvature bias only (not SMD drive)
    Vector2d sol_grad = environment_.soluble_field().gradient(head_pos);
    double sol_grad_normal = -sin_h * sol_grad.x + cos_h * sol_grad.y;
    double sol_wv_scale = 0.0;  // ASE→pirouettes, not weathervane
    double sol_bias = 0.0;      // no soluble weathervane contribution

    double bias_current = odor_bias + sol_bias;

    // Step 25: Repellent weathervane — turn AWAY from repellent gradient
    // Symmetric to attractant weathervane but with reversed sign
    // Without this: worm bounces back and forth (hit→reverse→attract→hit)
    // With this: worm continuously deflects around repellent zone
    Vector2d rep_grad = environment_.repellent_field().gradient(head_pos);
    double rep_grad_normal = -sin_h * rep_grad.x + cos_h * rep_grad.y;
    // Negative sign: curve AWAY from repellent gradient (opposite to attractant)
    // Gain matches attractant weathervane so forces compete symmetrically
    // Not modulated by satiety: nociceptive avoidance is unconditional
    double rep_bias = -weathervane_gain * rep_grad_normal;
    bias_current += rep_bias;

    // Step 23c: Temperature weathervane — turn toward Tc when fed
    // Navigate to minimize |T - Tc|: bias = -sign(T-Tc) × grad_T_normal
    // This steers toward Tc regardless of which side the worm is on
    Vector2d tgrad = environment_.temperature_gradient(head_pos);
    double temp_grad_normal = -sin_h * tgrad.x + cos_h * tgrad.y;
    double temp_at_head = environment_.sample_temperature(head_pos);
    double tc = cultivation_temp_;  // 22.5°C
    double temp_sign = (temp_at_head > tc) ? -1.0 : 1.0;  // toward Tc
    double thermo_wv_gain = 0.0 + 2.0 * sat_switch_wv;    // hungry: 0, fed: 2.0
    // Temperature weathervane gain: 30 pA per °C/mm
    // At 0.5°C/mm gradient, fed(×2.0): 30×0.25×2.0 = 15 pA (competes with chemo ~5-20 pA)
    double temp_bias = 30.0 * temp_sign * temp_grad_normal * thermo_wv_gain;
    bias_current += temp_bias;

    // Clamp to ±bias_clamp pA (should not overwhelm the half-center oscillator)
    double clamp = static_cast<double>(params.bias_clamp);
    if (bias_current > clamp) bias_current = clamp;
    if (bias_current < -clamp) bias_current = -clamp;

    int n = static_cast<int>(neurons_.size());

    // Step 19: Apply bias to shift half-center duty cycle
    // Sign: positive grad_normal → dorsal bias → positive curvature → heading increases
    // Step 33: Reduce SMD fraction to prevent multi-channel weathervane
    // from overwhelming SMD oscillator. Curvature_bias bypass is the main
    // turning mechanism; SMD just needs gentle duty-cycle modulation.
    // Step 41: Modulate by 5-HT — off-food (5-HT≈0) → full weathervane (1.0)
    //          on-food (5-HT≈0.7) → reduced (0.4) to prevent SMD saturation
    // REF: Iino 2009 — both pirouette + weathervane needed for efficient chemotaxis
    double sht_conc_wv = neuromod_.get_concentration("5-HT");
    double smd_wv_frac = 0.4 + 0.6 * std::max(0.0, 1.0 - sht_conc_wv / 0.7);
    if (smd_wv_frac > 1.0) smd_wv_frac = 1.0;
    if (smddl_id_ >= 0 && smddl_id_ < n) neurons_[smddl_id_]->add_synaptic_current( bias_current * smd_wv_frac);
    if (smddr_id_ >= 0 && smddr_id_ < n) neurons_[smddr_id_]->add_synaptic_current( bias_current * smd_wv_frac);
    if (smdvl_id_ >= 0 && smdvl_id_ < n) neurons_[smdvl_id_]->add_synaptic_current(-bias_current * smd_wv_frac);
    if (smdvr_id_ >= 0 && smdvr_id_ < n) neurons_[smdvr_id_]->add_synaptic_current(-bias_current * smd_wv_frac);

    // Direct curvature bias: bypass SMD oscillator bottleneck (110mV amplitude drowns ±24pA bias)
    // REF: diagnosed in Step 15 — SMD bias alone gives CI=0.07, with curv_bias CI=0.76
    // Recalibrated for σ²=144 gradient (4.5x stronger than old σ²=25):
    //   0.15 → 0.035 to maintain same turning radius (~2mm at 14mm from food)
    double curv_gain = weathervane_gain * 0.06;
    // Step 26b: dual-channel curvature bias (mirrors SMD dual-channel)
    double curv_bias = curv_gain * grad_normal * chemo_wv_gain * awc_pref;  // food odor (learnable)
    curv_bias += curv_gain * sol_grad_normal * chemo_wv_gain * sol_wv_scale; // soluble (fixed, 0.3×)
    // Step 25: Repellent curvature bias (same bypass, opposite sign)
    double rep_curv_bias = -curv_gain * rep_grad_normal;
    curv_bias += rep_curv_bias;
    // Add temperature curvature bias (same bypass for temp weathervane)
    double temp_curv_bias = 30.0 * 0.15 * temp_sign * temp_grad_normal * thermo_wv_gain;
    curv_bias += temp_curv_bias;
    // Clamp curvature bias
    double curv_clamp = clamp * 0.15;
    if (curv_bias > curv_clamp) curv_bias = curv_clamp;
    if (curv_bias < -curv_clamp) curv_bias = -curv_clamp;
    // Step 41: Curvature bias only during forward locomotion (run phase)
    // - Omega: RIV-driven deep bend (set by apply_riv_omega), don't override
    // - Reversal: no sensory steering — worm just backs up
    //   REF: Iino & Yoshida 2009 — klinotaxis (weathervane) is a run-phase behavior
    //   Without this: direction=-1 reverses dθ sign → worm steers AWAY during reversal
    // - Forward: weathervane actively steers toward attractant gradient
    if (riv_omega_active_) {
        // omega curvature set by apply_riv_omega()
    } else if (is_reversing_) {
        body_.set_curvature_bias(0.0);  // no sensory steering during reversal
    } else {
        body_.set_curvature_bias(curv_bias);
    }
}

void SimulationEngine::apply_smb_neck_bias() {
    // Step 28: RIA multi-compartment Ca²⁺ gate-and-switch → SMB neck curvature bias
    //
    // Replaces Step 19 AC/DC approximation with true subcellular computation:
    //   RIA nrV: receives SMDVL ACh → GAR-3 → local Ca²⁺ during ventral bend
    //   RIA nrD: receives SMDDL ACh → GAR-3 → local Ca²⁺ during dorsal bend
    //   RIA soma: receives global sensory glutamate (AWC/ASE → AIY → RIA)
    //
    // The multiplication happens physically:
    //   - Sensory → soma → spreads to nrV and nrD via axial coupling
    //   - Motor feedback → only nrV OR nrD (compartment-specific)
    //   - Both present → high local Ca²⁺ (additive: Hendricks 2012)
    //   - Ca_nrD - Ca_nrV encodes perpendicular gradient component
    //
    // REF: Hendricks 2012 Nature — compartmentalized Ca²⁺ in RIA axon
    //      Ouellette 2018 eNeuro — RIA subcellular domains for navigation
    //      Iino & Yoshida 2009 — curving rate ∝ ∇C_⊥

    int n = static_cast<int>(neurons_.size());

    // Read RIA nrV (comp 1) and nrD (comp 2) calcium from multi-compartment neurons
    double ca_diff = 0.0;
    int count = 0;

    // Uses cached MultiCompartmentNeuron* pointers (avoid per-step dynamic_cast)
    for (int i = 0; i < 2; ++i) {
        auto* mc = ria_mcn_[i];
        if (!mc || mc->num_compartments() < 3) continue;
        double ca_nrV = mc->get_compartment_calcium(1);  // nrV = compartment 1
        double ca_nrD = mc->get_compartment_calcium(2);  // nrD = compartment 2
        ca_diff += (ca_nrV - ca_nrD);  // sign: ventral Ca > dorsal → curve toward food
        count++;
    }

    if (count > 0) ca_diff /= count;  // average L/R

    // DC removal: track slow baseline (2s tau) and subtract
    // Only the oscillatory (AC) component carries perpendicular gradient info:
    //   AC = phase-locked to head oscillation via SMD feedback
    //   DC = tonic level, creates positive feedback loop if not removed
    ria_ca_diff_mean_ += (ca_diff - ria_ca_diff_mean_) * dt_ / 2000.0;
    double ca_diff_ac = ca_diff - ria_ca_diff_mean_;

    // Low-pass filter: ~300ms (half oscillation cycle, removes 2f ripple)
    ria_ca_diff_filtered_ += (ca_diff_ac - ria_ca_diff_filtered_) * dt_ / 300.0;

    // Convert Ca2+ AC difference to curvature bias
    // AC amplitude ~0.01-0.03 uM, gain calibrated for heading ~15 deg/s
    double klinotaxis_gain = 3000.0;  // /mm per uM Ca2+ AC difference
    double curvature_offset = klinotaxis_gain * ria_ca_diff_filtered_;

    // Clamp
    // Step 28: reduced from 2.0 to 0.9 because Ca2+ signal is cleaner
    // than old AC/DC approximation (less noise -> hits clamp more often)
    double max_bias = 0.5;
    if (curvature_offset > max_bias) curvature_offset = max_bias;
    if (curvature_offset < -max_bias) curvature_offset = -max_bias;

    // Don't override RIV-driven omega curvature_bias (Step 31)
    // ADD to existing curvature_bias (set by apply_weathervane), don't replace!
    // Bug fix: apply_smb_neck_bias() was overwriting weathervane's gradient-proportional
    // curvature_bias (up to ±7.5) with RIA Ca²⁺ signal (±0.5 max), destroying chemotaxis.
    // Both should co-exist: weathervane provides gradient steering, RIA provides neural modulation.
    if (!riv_omega_active_) {
        body_.set_curvature_bias(body_.get_curvature_bias() + curvature_offset);
    }
}

void SimulationEngine::apply_ria_smd_modulation() {
    // Step 19: RIA → SMD neuromodulation via CCA-1 threshold shift
    // NOT current injection! This modulates the oscillator's intrinsic property.
    //
    // Connectome: RIAL → SMDDL(3), SMDVL(4) and RIAR → SMDDR(3), SMDVR(4)
    // These synaptic currents are already computed by compute_synaptic_currents().
    // But the DC synaptic current can't shift duty cycle of a 100mV oscillation.
    //
    // Biological mechanism: RIA release → metabotropic receptor → second messenger
    // → modulates CCA-1 (T-type Ca²⁺) activation threshold
    // → lower threshold → burst starts earlier → longer burst → higher duty cycle
    //
    // REF: Hendricks 2012, Mellem 2002 — metabotropic modulation of ion channels
    int n = static_cast<int>(neurons_.size());

    double ria_release_L = 0.0, ria_release_R = 0.0;
    if (rial_id_ >= 0 && rial_id_ < n)
        ria_release_L = neurons_[rial_id_]->get_transmitter_release_rate();
    if (riar_id_ >= 0 && riar_id_ < n)
        ria_release_R = neurons_[riar_id_]->get_transmitter_release_rate();

    // Modulation gain: how much RIA release shifts CCA-1 V_half (mV)
    // At release=0.5 (baseline): shift=0 (symmetric)
    // At release=0.7: shift = +3mV → easier burst → longer duty cycle
    // At release=0.3: shift = -3mV → harder burst → shorter duty cycle
    // 15 mV/unit: calibrated so ±0.1 release diff → ±1.5mV CCA-1 shift
    // CCA-1 V_half is -48mV, slope=5mV, so 1.5mV shift changes m_inf significantly
    // Step 28: reduced from 15 to 8 to compensate for SMD-RIA feedback loop
    double mod_gain = 5.0;  // mV per unit release rate deviation from 0.5
    double shift_L = mod_gain * (ria_release_L - 0.5);
    double shift_R = mod_gain * (ria_release_R - 0.5);

    // Apply to SMD neurons: RIAL drives SMDDL/SMDVL, RIAR drives SMDDR/SMDVR
    // Uses cached SingleCompartmentNeuron* pointers (avoid per-step dynamic_cast)
    if (smd_scn_[0]) smd_scn_[0]->set_cca1_activation_shift(shift_L);  // SMDDL
    if (smd_scn_[1]) smd_scn_[1]->set_cca1_activation_shift(shift_L);  // SMDVL
    if (smd_scn_[2]) smd_scn_[2]->set_cca1_activation_shift(shift_R);  // SMDDR
    if (smd_scn_[3]) smd_scn_[3]->set_cca1_activation_shift(shift_R);  // SMDVR
}

void SimulationEngine::apply_proprioceptive_stretch() {
    // Step 29: Proprioceptive wave propagation (Wen 2012, Boyle 2012)
    // Each B-class motor neuron senses curvature at its sample_segment.
    // Wave propagates via neural relay: head oscillation → DB01 → VB02 → DB03...
    // REF: Wen 2012 Neuron — B-type MNs transduce proprioceptive signal
    int n = static_cast<int>(neurons_.size());
    for (auto& pm : proprio_mappings_) {
        if (pm.neuron_id < 0 || pm.neuron_id >= n) continue;

        double curv = body_.get_local_curvature(pm.sample_segment);
        // Dorsal MN: excited by ventral bend (negative curv)
        // Ventral MN: excited by dorsal bend (positive curv)
        double stretch = pm.is_dorsal ? -curv : curv;
        if (stretch < 0.0) stretch = 0.0;

        auto* scn = dynamic_cast<SingleCompartmentNeuron*>(neurons_[pm.neuron_id].get());
        if (scn) {
            scn->set_stretch_input(stretch);
        }
    }
}

void SimulationEngine::apply_touch_stimulus() {
    // Step 18: Wall collision → touch neuron activation (Chalfie 1985)
    // Arena is 50×50 mm. When head approaches wall → anterior touch (ALM).
    // When tail approaches wall → posterior touch (PLM).
    int n = static_cast<int>(neurons_.size());
    auto head = body_.get_head_position();
    auto tail = body_.get_tail_position();
    double arena_w = 50.0, arena_h = 50.0;

    // Anterior touch: head near wall
    bool front_touch = (head.x < arena_margin_ || head.x > arena_w - arena_margin_ ||
                        head.y < arena_margin_ || head.y > arena_h - arena_margin_);

    // Posterior touch: tail near wall
    bool rear_touch = (tail.x < arena_margin_ || tail.x > arena_w - arena_margin_ ||
                       tail.y < arena_margin_ || tail.y > arena_h - arena_margin_);

    // Step 33: OLQ nose touch — closer range than ALM body touch
    // OLQ detects head proximity to wall (dist < 0.3mm vs ALM's 2mm)
    // 4 quadrant neurons: directional sensitivity based on which wall
    // OLQ→RMD head withdrawal, OLQ→RIC indirect reversal (weak)
    // NOT triggering full reversal — just head withdrawal + direction change
    // REF: Kaplan & Horvitz 1993, Hart 1995
    double nose_current = 30.0;  // pA, weaker than body touch (80pA)
    double dx_left  = head.x;                  // distance to left wall
    double dx_right = arena_w - head.x;        // distance to right wall
    double dy_bottom = head.y;                 // distance to bottom wall
    double dy_top   = arena_h - head.y;        // distance to top wall
    double heading = body_.get_head_angle();
    double cos_h = std::cos(heading);
    double sin_h = std::sin(heading);

    // Check each wall: activate quadrant-specific OLQ based on heading
    // OLQ naming: DL=dorsal-left, DR=dorsal-right, VL=ventral-left, VR=ventral-right
    // Simplified: activate all 4 OLQ when nose is near any wall
    // Direction selectivity emerges from OLQ→RMD ipsilateral mapping
    bool nose_touch = false;
    double min_wall_dist = std::min({dx_left, dx_right, dy_bottom, dy_top});
    if (min_wall_dist < nose_margin_ && !front_touch) {
        // Nose close to wall but not yet body-touch range
        // Scale current by proximity: closer = stronger
        double prox = 1.0 - min_wall_dist / nose_margin_;  // 0→1
        double olq_drive = nose_current * prox;
        for (int id : olq_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(olq_drive);
            }
        }
        nose_touch = true;
    }

    if (front_touch) {
        // Strong current pulse to ALM neurons → triggers reversal via ALM→AVD→AVA
        for (int id : alm_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(touch_current_);
            }
        }
        // Also activate OLQ at full strength during body touch
        for (int id : olq_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(nose_current);
            }
        }
    }

    if (rear_touch) {
        // Strong current pulse to PLM neurons → triggers forward acceleration
        for (int id : plm_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(touch_current_);
            }
        }
    }

    // Step 47b: CEP binary tactile drive REMOVED.
    // CEP↔OLQ gap junctions cause cascade: 40pA CEP → OLQ→RMD (head disruption)
    // + OLQ→RIC (OA release) that destroys chemotaxis. CEP is now driven modestly
    // via chemo_mappings_ (gain=20, for DA→DVA/NLP-12 priming only).
    // Basal slowing uses on_lawn sigmoid directly (see effective_speed section).

    // ======================================================================
    // Step 34: O₂ sensing — URX/AQR/PQR transduction
    // O₂ derived from food field: bacteria consume O₂ → low O₂ at food
    // O₂(x) = 21% - 13% × food_density(x) (Gray 2004)
    // URX: activated by HIGH O₂ (>14%), drives hyperoxia avoidance
    // AQR: head O₂, PQR: tail O₂ (body cavity sensors)
    // NPR-1 215V (N2): tonic inhibition scales with satiety
    // REF: Gray 2004 Nature, Cheung 2005, Chang 2006 PLoS Biology
    // ======================================================================
    {
        // Compute O₂ at head and tail from FOOD DENSITY (bacteria, σ≈3mm)
        // NOT sample_chemical (volatile odor, σ≈12mm) — O₂ depletion is local
        double food_at_head = environment_.sample_food_density(head);
        double food_at_tail = environment_.sample_food_density(tail);
        // Normalize food concentration (peak ~1.0 at source center)
        // O₂ = 21% - 13% × food_density → range [8%, 21%]
        double o2_head = 21.0 - 13.0 * std::min(food_at_head, 1.0);
        double o2_tail = 21.0 - 13.0 * std::min(food_at_tail, 1.0);

        // URX transduction: activated when O₂ > 14% (hyperoxia threshold)
        // Linear ramp: 0 at 14%, max (o2_gain_) at 21%
        // gcy-35/gcy-36 → cGMP → TAX-2/TAX-4 channel opening
        double urx_drive = 0.0;
        if (o2_head > 14.0) {
            urx_drive = o2_gain_ * (o2_head - 14.0) / 7.0;  // 0→30 pA
        }

        // NPR-1 tonic inhibition (N2 215V = constitutively active)
        // N2: NPR-1 is always on → strongly suppresses O₂ circuit
        // At 21% O₂: 30pA drive - 25pA NPR-1 = 5pA net (barely active)
        // REF: Chang 2006 — "N2 is indifferent to high O₂ when food is present"
        //       Laurent 2015 — NPR-1 inhibits RMG output downstream of Ca2+
        double npr1_inh = npr1_tonic_;  // constant for N2 (future: modulate for Hawaiian)

        // Net URX drive = O₂ excitation + NPR-1 inhibition
        double urx_net = std::max(urx_drive + npr1_inh, 0.0);

        for (int id : urx_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(urx_net);
            }
        }

        // AQR: head O₂ sensor (same threshold, weaker gain)
        // AQR is unpaired, same location as URX (head pseudocoelom)
        if (aqr_id_ >= 0 && aqr_id_ < n) {
            double aqr_drive = 0.0;
            if (o2_head > 14.0) {
                aqr_drive = (o2_gain_ * 0.5) * (o2_head - 14.0) / 7.0;  // 50% of URX
            }
            double aqr_net = std::max(aqr_drive + npr1_inh * 0.5, 0.0);
            neurons_[aqr_id_]->set_external_current(aqr_net);
        }

        // PQR: tail O₂ sensor
        // Tail high O₂ → PQR activates → AVA → accelerate forward (escape)
        // REF: Busch 2012 — PQR tail position facilitates forward escape
        if (pqr_id_ >= 0 && pqr_id_ < n) {
            double pqr_drive = 0.0;
            if (o2_tail > 14.0) {
                pqr_drive = (o2_gain_ * 0.5) * (o2_tail - 14.0) / 7.0;
            }
            double pqr_net = std::max(pqr_drive + npr1_inh * 0.5, 0.0);
            neurons_[pqr_id_]->set_external_current(pqr_net);
        }

        // AUA: NPR-1 tonic inhibition (proxy for missing RMG suppression)
        // In N2, NPR-1 suppresses RMG hub → RMG→AUA gap junction weakened
        // Without RMG neuron, we apply inhibitory current directly to AUA
        // This prevents AUA from amplifying weak URX signals into strong AVA drive
        // REF: Laurent 2015 eLife — NPR-1 inhibits RMG Ca2+ responses
        for (int id : aua_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->add_synaptic_current(npr1_aua_);
            }
        }
    }

    // ======================================================================
    // Step 35: CO₂ sensing — BAG transduction
    // CO₂ derived from food field: bacteria produce CO₂
    // CO₂(x) = 0.04% + 3% × food_density(x) (ambient + bacterial)
    // BAG: activated by CO₂ > 0.5%, phasic response (dCO₂/dt sensitive)
    // OFF rebound: CO₂ decrease → transient burst (like AWC OFF)
    // N2: NPR-1 suppresses URX → URX doesn't inhibit CO₂ circuit → avoids CO₂
    // REF: Hallem & Sternberg 2008, Bretscher 2011, Carrillo 2013
    // ======================================================================
    {
        double food_at_head = environment_.sample_food_density(head);
        double co2_head = 0.04 + 3.0 * std::min(food_at_head, 1.0);  // range [0.04%, 3.04%]

        // Phasic component: BAG responds to CO₂ CHANGES more than absolute level
        // dCO₂/dt > 0 (entering food) → strong activation
        // dCO₂/dt < 0 (leaving food) → OFF rebound burst
        double dco2 = (co2_head - prev_co2_head_) / (dt_ * 0.001);  // %/s
        prev_co2_head_ = co2_head;

        // Tonic component: sustained drive when CO₂ > threshold
        double tonic_drive = 0.0;
        if (co2_head > co2_threshold_) {
            tonic_drive = co2_gain_ * (co2_head - co2_threshold_) / 3.0;  // 0→40 pA
        }

        // Phasic component: sensitive to rate of change
        // Rising CO₂ → strong activation; falling CO₂ → OFF rebound
        double phasic_drive = 0.0;
        if (dco2 > 0.0) {
            // Entering high CO₂ zone: strong phasic response
            phasic_drive = 20.0 * dco2;  // 20 pA per %/s
        } else if (dco2 < 0.0) {
            // OFF rebound: leaving CO₂ zone → transient burst (escape acceleration)
            // REF: Bretscher 2011 — BAG OFF response drives escape from CO₂
            phasic_drive = -10.0 * dco2;  // positive current from negative dco2
        }

        // Total BAG drive = tonic + phasic (clamped)
        double bag_drive = std::max(tonic_drive + phasic_drive, 0.0);
        if (bag_drive > 60.0) bag_drive = 60.0;  // clamp

        // URX cross-inhibition: in npr-1(lf), active URX suppresses CO₂ circuit
        // In N2: URX is suppressed by NPR-1 → no cross-inhibition → BAG works
        // Carrillo 2013: "ablating URX in npr-1(lf) restores CO₂ avoidance"
        double urx_inhibition = 0.0;
        for (int id : urx_ids_) {
            if (id >= 0 && id < n) {
                urx_inhibition += neurons_[id]->get_transmitter_release_rate();
            }
        }
        urx_inhibition *= 30.0;  // scale: URX S=0.15 → 4.5pA inhibition (weak in N2)

        double bag_net = std::max(bag_drive - urx_inhibition, 0.0);

        for (int id : bag_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(bag_net);
            }
        }
    }

    // ======================================================================
    // Step 36: Proprioception — DVA + PVD transduction
    // DVA: whole-body stretch receptor (TRP-4 TRPN channel)
    //   Senses mean |curvature| across all segments → modulates B-class MN gain
    //   trp-4 mutant: exaggerated body bends (Li 2006 Nature)
    // PVD: harsh touch (stronger than ALM) + posterior body proprioception
    //   Dendrites tile body wall → dual-mode sensory neuron
    // REF: Li 2006 Nature, Way & Chalfie 1989, Albeg 2011
    // ======================================================================
    {
        // --- DVA: whole-body curvature integration ---
        if (dva_id_ >= 0 && dva_id_ < n) {
            const auto& segs = body_.segments();
            int nseg = static_cast<int>(segs.size());
            double sum_abs_curv = 0.0;
            for (int si = 0; si < nseg; ++si) {
                sum_abs_curv += std::abs(segs[si].curvature);
            }
            double mean_abs_curv = (nseg > 0) ? sum_abs_curv / nseg : 0.0;

            // TRP-4 transduction: stretch → depolarization
            // mean_abs_curv typical range: 0.05-0.3 /mm during normal locomotion
            // DVA drive: proportional to mean |curvature|
            double dva_drive = dva_gain_ * mean_abs_curv;
            if (dva_drive > 30.0) dva_drive = 30.0;  // clamp

            neurons_[dva_id_]->set_external_current(dva_drive);
        }

        // --- PVD: harsh touch + posterior proprioception ---
        Vector2d head = body_.get_head_position();
        double wall_dist = std::max(0.0, std::min({head.x, 50.0 - head.x, head.y, 50.0 - head.y}));

        for (int id : pvd_ids_) {
            if (id < 0 || id >= n) continue;
            double I_pvd = 0.0;

            // Mode 1: Harsh touch — wall collision at closer range than ALM
            // PVD responds to stronger mechanical stimuli (platinum wire vs eyelash)
            // Use wall proximity as proxy: PVD fires when very close to wall
            if (wall_dist < pvd_harsh_thresh_) {
                double proximity = 1.0 - wall_dist / pvd_harsh_thresh_;
                I_pvd += pvd_harsh_current_ * proximity;
            }

            // Mode 2: Posterior body proprioception
            // PVD dendrites cover posterior body → sense posterior curvature
            const auto& segs = body_.segments();
            int nseg = static_cast<int>(segs.size());
            double post_curv = 0.0;
            int post_start = nseg / 2;  // posterior half
            int post_count = 0;
            for (int si = post_start; si < nseg; ++si) {
                post_curv += std::abs(segs[si].curvature);
                post_count++;
            }
            if (post_count > 0) {
                post_curv /= post_count;
                I_pvd += pvd_proprio_gain_ * post_curv;
            }

            neurons_[id]->set_external_current(I_pvd);
        }
    }

    // ======================================================================
    // Step 38: Egg-laying — HSN/VC transduction
    // egg_pressure ramps up slowly (tau=120s), simulating egg accumulation
    // When egg_pressure > threshold → HSN burst → 5-HT release → egg laid
    // Tyramine feedback via LGC-55 inhibits HSN (already in TA system)
    // REF: Collins 2016 eLife, Waggoner 1998 Neuron
    // ======================================================================
    {
        // egg_pressure ramps toward 1.0 (tau_fill = 120s)
        double egg_target = 1.0;
        double alpha_fill = dt_ / egg_tau_fill_;
        egg_pressure_ += alpha_fill * (egg_target - egg_pressure_);
        if (egg_pressure_ > 1.0) egg_pressure_ = 1.0;

        // HSN activation: sigmoid of (egg_pressure - threshold)
        double hsn_sigmoid = 1.0 / (1.0 + fast_exp(-(egg_pressure_ - egg_threshold_) / 0.05));
        double I_hsn = hsn_egg_gain_ * hsn_sigmoid;

        // Tyramine inhibition on HSN via LGC-55 (same receptor as RIV/SMD)
        // REF: Collins 2016 — uv1 tyramine → LGC-55 → HSN hyperpolarization
        double ta_conc = neuromod_.get_concentration("TA");
        double ta_inh = -20.0 * ta_conc;  // -20pA at max TA
        I_hsn = std::max(I_hsn + ta_inh, 0.0);

        for (int id : hsn_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(I_hsn);
            }
        }

        // Egg-laying event: HSN active + egg_pressure high → lay egg
        // Check if we're in active state or should start one
        if (hsn_sigmoid > 0.5 && current_time_ > egg_active_end_) {
            // Start active state
            egg_active_end_ = current_time_ + egg_active_duration_;
        }

        // During active state: VC gets excitation from HSN (via gap junction + 5-HT)
        if (current_time_ < egg_active_end_) {
            for (int id : vc_ids_) {
                if (id >= 0 && id < n) {
                    neurons_[id]->set_external_current(15.0);  // 5-HT potentiation
                }
            }
            // Egg laid at end of active state
            if (current_time_ + dt_ >= egg_active_end_ && egg_pressure_ > egg_threshold_) {
                egg_laid_count_ += 1;
                egg_pressure_ = 0.1;  // reset (not zero — some eggs remain)
            }
        }
    }

    // ======================================================================
    // Pirouette model (Pierce-Shimomura et al. 1999, J Neurosci 19:9557)
    // ======================================================================
    // Biased random walk: pirouette initiation rate r is a sigmoid of dC/dt.
    // dC/dt < 0 (heading down gradient) → elevated r → more pirouettes
    // dC/dt > 0 (heading up gradient)   → suppressed r → fewer pirouettes
    // dC/dt = 0 (flat field)            → spontaneous rate ~0.025/s
    //
    // This bypasses the noisy klinokinesis neural pathway (ASE→AIB→AVA),
    // same principle as the curvature_bias_ bypass for weathervane (Iino 2009).
    // Both mechanisms operate in parallel for efficient chemotaxis (Iino 2009 Fig 3).
    //
    // Post-pirouette bearing is biased toward gradient (course correction,
    // not random reorientation) — implemented via gradient-biased omega turns.
    // ======================================================================

    // 1. Compute sensory derivatives and low-pass filter (tau=4s)
    //    Chemical: dC/dt (Pierce-Shimomura 1999 Fig 7)
    //    Thermal: d|T-Tc|/dt (Ryu & Samuel 2002, Luo 2014)
    Vector2d head_pos = body_.get_head_position();

    // 1a. Chemical derivative
    double conc_now = environment_.sample_chemical(head_pos);
    double raw_dCdt = (conc_now - prev_concentration_) / (dt_ / 1000.0);
    prev_concentration_ = conc_now;
    double alpha_filt = dt_ / 4000.0;  // tau=4s
    dCdt_filtered_ += (raw_dCdt - dCdt_filtered_) * alpha_filt;

    // 1b. Thermal deviation derivative: d|T - Tc|/dt
    //     |T-Tc| increasing → moving AWAY from Tc → more pirouettes
    //     |T-Tc| decreasing → moving TOWARD Tc → fewer pirouettes
    //     REF: Ryu & Samuel 2002 — pirouette rate rises when warming above Tc
    double temp_now = environment_.sample_temperature(head_pos);
    double temp_dev = std::abs(temp_now - cultivation_temp_);
    double raw_dTdev = (temp_dev - prev_temp_dev_) / (dt_ / 1000.0);
    prev_temp_dev_ = temp_dev;
    dTdev_filtered_ += (raw_dTdev - dTdev_filtered_) * alpha_filt;

    // 2. Combined pirouette signal: satiety-weighted blend of chemical and thermal
    //    Hungry → chemical klinokinesis: dC/dt < 0 triggers pirouettes
    //    Fed    → thermal klinokinesis: d|T-Tc|/dt > 0 triggers pirouettes
    //    Both use the same biased random walk strategy (Pierce-Shimomura 1999)
    double sat_switch = 1.0 / (1.0 + fast_exp(-10.0 * (satiety_ - 0.5)));
    // Normalize signals to comparable scales:
    //   dCdt_filtered_: typical range ±0.005 → k_chem=1500 → sigmoid range 0.01-0.06
    //   dTdev_filtered_: typical range ±0.1°C/s → k_therm=30 → similar sigmoid range
    // Combined: negative = "bad direction" → more pirouettes
    double combined = (1.0 - sat_switch) * (-dCdt_filtered_ * 1500.0)  // chemical: dC/dt<0 → positive
                    + sat_switch * (dTdev_filtered_ * 30.0);            // thermal: d|T-Tc|/dt>0 → positive

    // 3. Pirouette initiation rate: sigmoid of combined signal
    //    r(x) = r_min + (r_max - r_min) / (1 + exp(-x))
    //    x > 0 (bad direction) → r → r_max (more pirouettes)
    //    x < 0 (good direction) → r → r_min (fewer pirouettes)
    //    x = 0 (flat field) → r = r_mid ≈ 0.14/s (Step 44: raised for off-food search)
    bool was_reversing = is_reversing_;
    if (!is_reversing_) {
        double r_min = 0.03;   // /s, suppressed when heading up gradient
        double r_max = 0.25;   // /s, elevated rate when heading down gradient
        // Step 44: raised from 0.01/0.16 to 0.03/0.25
        // Off-food r_mid=0.14 → effective ~0.10/s (target 6/min, Campbell 2016)
        // On-food with 5-HT: 0.14×0.65=0.09 → effective ~0.06/s (target 3/min)
        // REF: Pierce-Shimomura 1999 — pirouette rate heavily suppressed during approach
        //      Gray 2005 PNAS — off-food reversal rate 6/min
        double pir_rate = r_min + (r_max - r_min) /
                          (1.0 + fast_exp(-combined));

        // Step 44: Apply neuromodulatory reversal rate scaling
        // 5-HT on food → scale=0.65 (50% suppression at [5-HT]=0.73)
        // Off food → scale=1.0 (no suppression → full exploration)
        // REF: Flavell 2013 Cell — 5-HT promotes dwelling (low reversal state)
        pir_rate *= neuromod_.get_reversal_rate_scale();

        // Step 44→45: ARS food_memory → pirouette rate bonus (reversal-coupled ARS)
        // After leaving food: food_memory high → more reversals → stay near patch
        // As food_memory decays (90s tau): rate drops → transition to dispersal
        // Step 45: reduced 0.08→0.04 — NLP-12→CKR-1→SMD now handles forward
        // reorientations (head swing amplitude); this bonus covers reversal-coupled
        // omega turns only. NLP-12→CKR-2→AVA (+2pA) also contributes to reversal bias.
        // REF: Hills 2004 J Neurosci — DA→DARPP-32→GLR-1 increases turn frequency
        //      Bhattacharya 2014 — nlp-12(lf) reduces forward reorientations, NOT omega turns
        pir_rate += 0.08 * food_memory_;

        // Step 47: Head poke reversal at food boundary (eLife 2024, Flavell lab)
        // When head exits food patch → reversal with state-dependent probability
        // Data: head poke reversal 1.1/min, lawn leaving only 1/95min
        //   → ~98% of food-edge encounters result in staying on food
        // Mechanism: CEP stops detecting bacteria → DA signal drops → AIB→AVA reversal
        // Probability modulated by behavioral state:
        //   Dwelling (high 5-HT, low PDF): ~80% reversal → worm stays on food
        //   Roaming (low 5-HT, high PDF):  ~20% reversal → worm can leave to explore
        //   Matches: "leaving rates 20-fold higher in roaming" (eLife 2024)
        // REF: Flavell 2024 eLife — foraging decisions at food boundary
        //      Gray 2005 PNAS — AIB promotes reversals
        //      Sawin 2000 — CEP mechanosensory detection of bacteria
        double food_at_head = environment_.sample_food_density(body_.get_head_position());
        bool food_edge_exit = (prev_food_at_head_ > 0.4 && food_at_head < 0.3);
        prev_food_at_head_ = food_at_head;

        if (food_edge_exit && current_time_ > reversal_refractory_end_) {
            // State-dependent reversal probability at food edge
            double sht = neuromod_.get_concentration("5-HT");
            double pdf = neuromod_.get_concentration("PDF");
            // Dwelling: 5-HT high → p=0.80, Roaming: PDF high → p→0.20
            double p_edge_rev = 0.50 + 0.30 * sht - 0.30 * pdf;
            if (p_edge_rev < 0.15) p_edge_rev = 0.15;
            if (p_edge_rev > 0.85) p_edge_rev = 0.85;

            std::uniform_real_distribution<double> rdist01(0.0, 1.0);
            if (rdist01(touch_rng_) < p_edge_rev) {
                is_reversing_ = true;
                // Short reversal: head poke reversals are brief (~500ms)
                planned_reversal_end_ = current_time_ + 500.0;
            }
        }

        // Standard gradient-dependent pirouette (unchanged)
        double p_pir = pir_rate * (dt_ / 1000.0);
        std::uniform_real_distribution<double> rdist(0.0, 1.0);
        if (!is_reversing_ && current_time_ > reversal_refractory_end_ && rdist(touch_rng_) < p_pir) {
            is_reversing_ = true;
            // Draw reversal duration: exponential with mean 1000ms
            // REF: Gray 2005 — mean reversal ~1s; Luo 2014 — τ_run=6.2s, τ_pir=7.4s
            std::exponential_distribution<double> dur_dist(1.0 / 1000.0);
            double rev_dur = dur_dist(touch_rng_);
            if (rev_dur < 300.0) rev_dur = 300.0;
            if (rev_dur > 3000.0) rev_dur = 3000.0;
            planned_reversal_end_ = current_time_ + rev_dur;
        }
    } else {
        if (current_time_ >= planned_reversal_end_) {
            is_reversing_ = false;
            reversal_refractory_end_ = current_time_ + 2000.0;  // 2s (adjusted: bio tcrit=6s includes 7.4s pirouette)
        }
    }

    if (is_reversing_ && !was_reversing) {
        // Reversal just started
        reversal_start_time_ = current_time_;
        // Step 32: Snapshot dorsal tone at reversal start (before TA suppresses SMD)
        // This captures random SMD phase → basis for AS resistance evaluation
        double dt_snap = 0.0;
        for (int i = 0; i < 6; ++i) {
            dt_snap += body_.segments()[i].dorsal_activation;
        }
        pre_rev_dorsal_tone_ = dt_snap / 6.0;
    }
    if (!is_reversing_ && was_reversing) {
        // Reversal just ended — record duration for diagnostics
        reversal_duration_ = current_time_ - reversal_start_time_;

        // Step 31: RIV post-reversal pulse — models reversal→forward transition signal
        // Biological basis (Donnelly 2013): "The omega turn is initiated by a steep
        // ventral bend of the head when the animal REINITIATES FORWARD LOCOMOTION"
        // Pulse amplitude ∝ [TA]: longer reversals accumulate more TA → stronger pulse
        // → higher RIV burst probability → more omega turns (emergent correlation)
        riv_post_rev_time_ = current_time_;
        double ta_conc = neuromod_.get_concentration("TA");
        // Scale: 50 pA at [TA]=1.0 → must overcome concurrent TA tonic (-20×[TA])
        // Net at [TA]=0.5: tonic(2) + pulse(25) - TA_tonic(10) = 17 pA → CCA-1 burst
        // Net at [TA]=0.1: tonic(2) + pulse(5) - TA_tonic(2) = 5 pA → no burst
        double base_amp = static_cast<double>(params.pulse_amp) * ta_conc;

        // L/R asymmetry for omega direction: TWO mechanisms
        //
        // 1. GRADIENT signal (ASE→AIA→AIB→RIV sensory relay)
        //    grad_perp > 0 → food to LEFT → bias RIVL → turn LEFT toward food
        double heading = body_.get_head_angle();
        Vector2d grad = environment_.chemical_field().gradient(body_.get_head_position());
        double grad_perp = -std::sin(heading) * grad.x + std::cos(heading) * grad.y;
        double grad_lr = std::tanh(grad_perp * 50.0);  // saturating [-1, 1]
        //
        // 2. BODY POSTURE signal (Step 41: previously computed but never used)
        //    Head SMD oscillation phase at reversal onset determines which side
        //    the head is bent toward → that side initiates the omega turn
        //    REF: Gray 2005 — omega direction correlates with body posture
        //    REF: Donnelly 2013 — "steep ventral bend of the head" initiates omega
        //    pre_rev_dorsal_tone_ > 0.5 → head bent dorsally → omega ventral (RIVR)
        //    pre_rev_dorsal_tone_ < 0.5 → head bent ventrally → omega dorsal (RIVL)
        //    Since reversal timing is stochastic, SMD phase is ~uniform → random omega dir
        double posture_lr = -(pre_rev_dorsal_tone_ - 0.5) * 4.0;  // [-2, 2] range
        posture_lr = std::tanh(posture_lr);  // saturate to [-1, 1]
        //
        // Combined: gradient dominates when present (×0.3), posture fills in (×0.3)
        // Total asymmetry up to ±60% when both agree
        double lr_grad   = 0.3 * grad_lr;
        double lr_posture = 0.3 * posture_lr;
        riv_post_rev_amp_l_ = base_amp * (1.0 + lr_grad + lr_posture);
        riv_post_rev_amp_r_ = base_amp * (1.0 - lr_grad - lr_posture);
    }
}

void SimulationEngine::apply_riv_omega() {
    // Step 31: RIV-driven omega turn (fully emergent from TA gating)
    //
    // Mechanism:
    //   During reversal: AVA active → RIM→TA→LGC-55→RIV(-20pA) = suppressed
    //   Reversal ends:   AVA quiet → TA decays (τ=2s) → RIV released → burst
    //   RIV burst → ventral muscle activation (seg 0-7) → deep head bend → omega
    //   Burst self-terminates via Ca²⁺→SLO-1 adaptation (same as SMD)
    //
    // Direction: RIVL vs RIVR asymmetry from upstream gradient signals
    //   gradient from right → ASER→AIB→RIVR stronger → RIVR burst > RIVL
    //   → curvature_bias direction set by dominant RIV
    //
    // REF: Gray 2005 PNAS — RIV specifies ventral bias of omega turns
    //      Donnelly 2013 — TA gates omega timing via LGC-55 on RIV

    int n = static_cast<int>(neurons_.size());
    if (rivl_id_ < 0 || rivr_id_ < 0 || rivl_id_ >= n || rivr_id_ >= n) return;

    double rivl_rel = neurons_[rivl_id_]->get_transmitter_release_rate();
    double rivr_rel = neurons_[rivr_id_]->get_transmitter_release_rate();
    double riv_max = std::max(rivl_rel, rivr_rel);

    // Step 32: AS dorsal resistance — pre-reversal snapshot gating
    // Biological mechanism: AS provides continuous dorsal body wall tension.
    // RIV ventral force must overcome AS dorsal tone for omega initiation.
    //
    // KEY INSIGHT: Use dorsal tone recorded at REVERSAL START (pre_rev_dorsal_tone_),
    // NOT the tone at RIV burst peak. During reversal, TA via LGC-55 suppresses
    // SMD (-25pA) → dorsal tone drops → burst peak ALWAYS sees low tone → 100% omega.
    // Pre-reversal tone captures random SMD phase (before TA suppression) →
    // P(tone_high) ≈ 30-40% → omega blocked → natural 60-70% omega/reversal.
    //
    // This models: the body's dorsal posture at escape onset determines whether
    // the subsequent omega can override the dorsal muscle tension.

    // --- Omega INITIATION: peak detection + pre-reversal AS resistance ---
    double prev_max = riv_prev_max_;
    riv_prev_max_ = riv_max;

    if (!riv_omega_active_) {
        // Detect RIV burst peak: release was rising, now falling, and crossed threshold
        bool at_peak = (riv_max < prev_max && prev_max > static_cast<double>(params.omega_threshold));
        if (at_peak) {
            // At burst peak: evaluate AS resistance using PRE-REVERSAL dorsal tone
            // Factor 1.5: omega when pre_rev_tone < (peak - 0.5) / 1.5
            //   spike peak ~1.0, tone < 0.33 → omega ✓ (P ≈ 60-70%)
            //   spike peak ~1.0, tone > 0.33 → blocked ✗ (P ≈ 30-40%)
            double effective_riv = prev_max - pre_rev_dorsal_tone_ * static_cast<double>(params.as_factor);
            if (effective_riv > static_cast<double>(params.omega_threshold)) {
                riv_omega_active_ = true;
                riv_omega_start_ = current_time_;
                body_.set_omega_mode(true);
            }
        }
    }

    // --- Omega CONTINUATION: curvature bias while active ---
    if (riv_omega_active_) {
        double omega_curv_gain = 12.0;
        // Direction: dominant RIV determines turn direction
        double bias = (rivl_rel - rivr_rel) * omega_curv_gain;
        // Ensure minimum bias in dominant direction for deep bend
        if (rivl_rel > rivr_rel && bias < omega_curv_gain * 0.3)
            bias = omega_curv_gain * rivl_rel;
        else if (rivr_rel >= rivl_rel && bias > -omega_curv_gain * 0.3)
            bias = -omega_curv_gain * rivr_rel;
        body_.set_curvature_bias(bias);

        // Termination: min 400ms, then end when RIV drops below threshold
        double omega_elapsed = current_time_ - riv_omega_start_;
        if (omega_elapsed > 400.0 && riv_max < static_cast<double>(params.omega_threshold)) {
            riv_omega_active_ = false;
            body_.set_omega_mode(false);
        }
    }
}

void SimulationEngine::setup_neuromodulation() {
    // ================================================================
    // Step 20: Neuromodulation Layer (Layer 6) — "Wireless Connectome"
    //
    // Unlike synapses (point-to-point, ms), neuromodulators act via
    // volume transmission (diffuse, seconds-minutes).
    //
    // Two modulators for MVP:
    //   1. Serotonin (5-HT): food → NSM → dwelling (slow, low reversal)
    //   2. Dopamine (DA): food → CEP → basal slowing response
    //
    // REF: Flavell 2013 Cell — 5-HT/PDF roaming/dwelling
    //      Sawin 2000 — DA basal slowing response
    //      Chase & Koelle 2007 — monoamine signaling review
    // ================================================================

    // --- Serotonin (5-HT) ---
    // Source: NSM pharyngeal neurons (detect food via bacteria ingestion)
    // Effect: promotes dwelling state
    //   - MOD-1 on AIY: inhibitory Cl- channel → reduces AIY activity → less forward
    //   - SER-4 on AIB: inhibitory → reduces AIB → fewer pirouettes
    //   - Global: reduce speed slightly (enhanced slowing response)
    {
        Neuromodulator serotonin;
        serotonin.name = "5-HT";
        serotonin.tau_rise = 3000.0;    // 3s to build up (slow volume transmission)
        serotonin.tau_decay = 8000.0;   // 8s to clear (long-lasting dwelling)
        // Step 41: raised from 0.3 → 0.5 to prevent ADF baseline activity
        // from inflating off-food 5-HT (ADF tonic release ~0.45 exceeds 0.4)
        // Step 43: ADF removed as 5-HT source → high threshold no longer needed
        // Step 45: restored to 0.3 (original value) — with ADF gone, the original
        // concern is moot. NSM off-food S=0.05-0.20, well below 0.3 → no leak.
        // On-food NSM S=0.7-0.85 → drive=(0.7-0.3)/0.7=0.57 → strong dwelling.
        serotonin.release_threshold = 0.3;

        // Source neurons: NSM (pharyngeal, food detection) + ADF (pathogen learning)
        int nsml = connectome_.get_neuron_id("NSML");
        int nsmr = connectome_.get_neuron_id("NSMR");
        if (nsml >= 0) serotonin.source_neuron_ids.push_back(nsml);
        if (nsmr >= 0) serotonin.source_neuron_ids.push_back(nsmr);
        // Step 26→41: ADF removed as 5-HT source. ADF is a chemosensory neuron
        // (detects volatile odors) that fires tonically near food → inflates 5-HT
        // off-food. In real worms, ADF 5-HT release requires TPH-1 upregulation
        // specifically during pathogen exposure (Zhang 2005 Nature), not constitutive.
        // ADF pathogen signaling works via synapses (ADF→AIB) + sickness→weathervane.
        // Step 38: HSN as 5-HT source (egg-laying command motor neuron)
        // REF: Waggoner 1998 — HSN releases 5-HT to initiate egg-laying active state
        for (int hsn_id : hsn_ids_) {
            if (hsn_id >= 0) serotonin.source_neuron_ids.push_back(hsn_id);
        }

        // Target: AIY via MOD-1 (inhibitory Cl- channel)
        // 5-HT → MOD-1 on AIY → hyperpolarize → reduce forward drive
        // REF: Flavell 2013 — NSM 5-HT inhibits AIY
        // Step 43: recalibrated from -5.0 to -2.5 pA after removing buggy excitatory
        // ADF→AIY synapse (was +1.8pA at baseline). Old net: -5×0.73+1.8=-1.85pA.
        // New net: -2.5×0.73=-1.83pA (same baseline effect, no excitatory compensation)
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) serotonin.targets.push_back(
            {aiyl, "MOD-1", ModulationEffect::EXCITABILITY, -2.5});
        if (aiyr >= 0) serotonin.targets.push_back(
            {aiyr, "MOD-1", ModulationEffect::EXCITABILITY, -2.5});

        // Step 25: Target: AIB inhibition (suppress avoidance while feeding)
        // REF: Summers 2015 JNeurosci — 5-HT via MOD-1 (5-HT-gated Cl⁻) inhibits AIB
        // On food: 5-HT↑ → AIB↓ → animals continue forward despite repellent
        // Off food: 5-HT↓ → AIB active → full avoidance response
        for (int aib_id : aib_ids_) {
            if (aib_id >= 0) serotonin.targets.push_back(
                {aib_id, "MOD-1", ModulationEffect::EXCITABILITY, -6.0}); // -6 pA inhibitory
        }

        // Target: reversal rate suppression (dwelling = fewer pirouettes)
        // On food: 5-HT → MOD-1 → fewer reversals → stay on food (dwelling)
        // Off food: no 5-HT → full reversal rate → area-restricted search
        // REF: Gray 2005 PNAS — off-food reversal rate 6/min vs on-food 3/min
        //      Campbell 2016 PLOS Genetics — wild-type 2× reversal rate off food
        //      Flavell 2013 Cell — 5-HT promotes dwelling (low reversal) state
        serotonin.targets.push_back(
            {-1, "MOD-1", ModulationEffect::REVERSAL_RATE, -0.50}); // 50% fewer reversals at peak 5-HT

        // Target: speed reduction (enhanced slowing on food)
        // REF: Sawin 2000 — serotonin reduces locomotion speed
        serotonin.targets.push_back(
            {-1, "SER-7", ModulationEffect::SPEED_SCALE, -0.40}); // behavioral state: dwelling generally slower (stacks with instant food-contact slowing)

        // Target: RIC inhibition (cross-inhibit OA source during dwelling)
        // 5-HT → SER-4 on RIC → inhibit → no OA during active dwelling
        // REF: Chase & Koelle 2007 — 5-HT/OA antagonism
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) serotonin.targets.push_back(
            {ricl, "SER-4", ModulationEffect::EXCITABILITY, -4.0}); // Step 48: -8→-4 pA (allow RIC activation when satiated)
        if (ricr >= 0) serotonin.targets.push_back(
            {ricr, "SER-4", ModulationEffect::EXCITABILITY, -4.0});

        neuromod_.add_modulator(std::move(serotonin));
    }

    // --- Dopamine (DA) ---
    // Source: CEP head neurons (detect bacteria mechanically)
    // Effect: basal slowing response — slow down when encountering food
    //   - DOP-3 on motor neurons: inhibitory → reduces speed
    //   - DOP-1 on RIA: excitatory → enhances head oscillation (foraging)
    // REF: Sawin 2000 — CEP DA drives basal slowing
    //      Chase 2004 — DOP-3 inhibitory on locomotion
    {
        Neuromodulator dopamine;
        dopamine.name = "DA";
        dopamine.tau_rise = 2000.0;     // 2s to build up
        dopamine.tau_decay = 5000.0;    // 5s to clear
        dopamine.release_threshold = 0.3;

        // Source neurons: CEP (4 neurons, head mechanosensory)
        const char* cep_names[] = {"CEPDL", "CEPDR", "CEPVL", "CEPVR"};
        for (auto name : cep_names) {
            int id = connectome_.get_neuron_id(name);
            if (id >= 0) dopamine.source_neuron_ids.push_back(id);
        }

        // Step 47b: DA SPEED_SCALE removed — replaced by instant basal_slow mechanism
        // Old: DOP-3 SPEED_SCALE -0.30 via neuromod (tau_decay=5s → persists off-food → tanks CI)
        // New: basal_slow = 1.0 - 0.35 * da_gate * on_lawn (instant, position-dependent)
        // on_lawn sigmoid drops to 0 immediately when leaving food → no off-food penalty
        // REF: Sawin 2000 — cat-2 mutants fail to slow; DOP-3 inhibitory on motor neurons

        // Step 45: Target: DVA via DOP-1 (D1-like excitatory GPCR)
        // DA from CEP (food detection) → DOP-1 on DVA → stimulates NLP-12 release
        // On food: DA high → DVA excited → NLP-12 primed for ARS upon food departure
        // Off food: DA drops → DVA excitation from DA decreases (proprioception still drives DVA)
        // REF: Bhattacharya 2014 PLOS Genetics — DOP-1 in DVA regulates NLP-12 release
        //      dop-1 mutant: reduced NLP-12-Venus fluorescence change, impaired ARS
        if (dva_id_ >= 0) dopamine.targets.push_back(
            {dva_id_, "DOP-1", ModulationEffect::EXCITABILITY, 4.0}); // +4 pA excitatory (primes DVA, not enough alone for NLP-12 release)

        neuromod_.add_modulator(std::move(dopamine));
    }

    // --- Octopamine (OA) ---
    // Source: RIC interneurons (tonically active, inhibited by 5-HT)
    // Effect: promotes roaming — increase speed, decrease reversal rate
    // OA is the functional antagonist of 5-HT
    // REF: Alkema 2005 — tyramine/octopamine in C. elegans locomotion
    //      Churgin 2017 — OA promotes roaming state
    {
        Neuromodulator octopamine;
        octopamine.name = "OA";
        octopamine.tau_rise = 2000.0;    // 2s to build up
        octopamine.tau_decay = 4000.0;   // 4s to clear (faster than 5-HT)
        octopamine.release_threshold = 0.3;

        // Source neurons: RIC (tonically active when off-food/satiated)
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) octopamine.source_neuron_ids.push_back(ricl);
        if (ricr >= 0) octopamine.source_neuron_ids.push_back(ricr);

        // Target: global speed increase (antagonizes 5-HT/DA slowing)
        // REF: Churgin 2017 — OA mutants have reduced roaming
        octopamine.targets.push_back(
            {-1, "SER-3", ModulationEffect::SPEED_SCALE, 0.35}); // Step 41: +35% speed (compensate stronger 5-HT/DA)

        // Target: AIY excitation (promotes forward runs)
        // SER-6 on AIY: excitatory → more forward → roaming
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) octopamine.targets.push_back(
            {aiyl, "SER-6", ModulationEffect::EXCITABILITY, 4.0}); // +4 pA excitatory
        if (aiyr >= 0) octopamine.targets.push_back(
            {aiyr, "SER-6", ModulationEffect::EXCITABILITY, 4.0});

        neuromod_.add_modulator(std::move(octopamine));
    }

    // --- Tyramine (TA) ---
    // Step 30: RIM tyraminergic signaling for escape response coordination
    // Source: RIM L/R (co-activated with AVA via gap junctions during reversal)
    // TA temporally coordinates reversal phases:
    //   Phase 1 (fast, LGC-55): suppress head oscillation + inhibit forward
    //   Phase 2 (slow, TYRA-3): sensitize nociception for repeated threats
    // REF: Alkema 2005 Neuron — TDC-1 in RIM synthesizes TA
    //      Pirri 2009 Neuron — LGC-55 Cl⁻ channel coordinates escape
    //      Donnelly 2013 PLOS Biology — TA orchestrates motor sequence
    //      Rex 2005 — TYRA-3 GPCR modulates nociception
    {
        Neuromodulator tyramine;
        tyramine.name = "TA";
        tyramine.tau_rise = 500.0;     // 0.5s — fast, escape timescale (Pirri 2009)
        tyramine.tau_decay = 2000.0;   // 2s — behavioral persistence
        tyramine.release_threshold = 0.3;

        // Source neurons: RIM (tyraminergic, TDC-1+, TBH-1-)
        int riml = connectome_.get_neuron_id("RIML");
        int rimr = connectome_.get_neuron_id("RIMR");
        if (riml >= 0) tyramine.source_neuron_ids.push_back(riml);
        if (rimr >= 0) tyramine.source_neuron_ids.push_back(rimr);

        // Target 1: LGC-55 → SMD head motor neurons (Cl⁻ inhibitory)
        // Suppress head oscillation during reversal — "committed reversal"
        // REF: Pirri 2009 — lgc-55 mutants fail to suppress head movements
        const char* smd_names[] = {"SMDDL", "SMDVL", "SMDDR", "SMDVR"};
        for (auto name : smd_names) {
            int id = connectome_.get_neuron_id(name);
            if (id >= 0) tyramine.targets.push_back(
                {id, "LGC-55", ModulationEffect::EXCITABILITY, -25.0});
        }

        // Target 2: LGC-55 → AVB forward command interneurons (Cl⁻ inhibitory)
        // Inhibit forward locomotion → promotes longer reversals
        // REF: Pirri 2009 — LGC-55 expressed in AVB
        int avbl = connectome_.get_neuron_id("AVBL");
        int avbr = connectome_.get_neuron_id("AVBR");
        if (avbl >= 0) tyramine.targets.push_back(
            {avbl, "LGC-55", ModulationEffect::EXCITABILITY, -10.0});
        if (avbr >= 0) tyramine.targets.push_back(
            {avbr, "LGC-55", ModulationEffect::EXCITABILITY, -10.0});

        // Target 3: TYRA-3 → ASH nociceptive neurons (excitatory GPCR)
        // Sensitize nociception — repeated threats → faster escape (emergent)
        // REF: Rex 2005, Donnelly 2013 — TYRA-3 modulates pain-like responses
        int ashl = connectome_.get_neuron_id("ASHL");
        int ashr = connectome_.get_neuron_id("ASHR");
        if (ashl >= 0) tyramine.targets.push_back(
            {ashl, "TYRA-3", ModulationEffect::EXCITABILITY, 5.0});
        if (ashr >= 0) tyramine.targets.push_back(
            {ashr, "TYRA-3", ModulationEffect::EXCITABILITY, 5.0});

        // Target 4: LGC-55 → RIV omega turn neurons (Cl⁻ inhibitory)
        // Gate omega timing: TA suppresses RIV during reversal,
        // RIV bursts only after TA decays → omega follows reversal end
        // REF: Donnelly 2013 — TA coordinates reversal→omega sequence
        int rivl = connectome_.get_neuron_id("RIVL");
        int rivr = connectome_.get_neuron_id("RIVR");
        if (rivl >= 0) tyramine.targets.push_back(
            {rivl, "LGC-55", ModulationEffect::EXCITABILITY, -20.0});
        if (rivr >= 0) tyramine.targets.push_back(
            {rivr, "LGC-55", ModulationEffect::EXCITABILITY, -20.0});

        // Target 5: SER-2 → AIY (Gαi GPCR, inhibitory)
        // Step 43c: RIM TA → SER-2 → AIY imprinted aversive learning
        // During pathogen encounter: RIM fires → TA ↑ → AIY inhibited → approach suppressed
        // SER-2 is required for retrieval of imprinted olfactory memory
        // REF: Jin, Pokala & Bargmann 2016 Cell 164:632-643
        //      Bowitch 2018 G3 — SER-2 on AIY necessary for memory retrieval
        int aiyl_ta = connectome_.get_neuron_id("AIYL");
        int aiyr_ta = connectome_.get_neuron_id("AIYR");
        if (aiyl_ta >= 0) tyramine.targets.push_back(
            {aiyl_ta, "SER-2", ModulationEffect::EXCITABILITY, -10.0}); // -10 pA inhibitory
        if (aiyr_ta >= 0) tyramine.targets.push_back(
            {aiyr_ta, "SER-2", ModulationEffect::EXCITABILITY, -10.0});

        // Target 6: TA→OA biosynthetic coupling via RIC
        // RIM releases TA → diffuses to RIC → TBH-1 converts TA→OA
        // Modeled as weak excitation of RIC proportional to [TA]
        // REF: Alkema 2005 — TDC-1/TBH-1 co-expression in RIC
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) tyramine.targets.push_back(
            {ricl, "TBH-1", ModulationEffect::EXCITABILITY, 2.0}); // +2 pA substrate supply
        if (ricr >= 0) tyramine.targets.push_back(
            {ricr, "TBH-1", ModulationEffect::EXCITABILITY, 2.0});

        neuromod_.add_modulator(std::move(tyramine));
    }

    // --- NLP-12 (CCK homolog) ---
    // Step 45: Neuropeptide from DVA proprioceptive interneuron
    // Source: DVA (single unpaired neuron, senses whole-body curvature via TRP-4)
    // NLP-12 is the key signal for area-restricted search (ARS):
    //   High body curvature (off food, searching) → DVA active → NLP-12 release
    //   Low body curvature (on food, dwelling) → DVA quiet → NLP-12 low
    // Two GPCRs with distinct circuit targets:
    //   CKR-1 → SMD head motor neurons → head swing amplitude ↑ → forward reorientations
    //   CKR-2 → body wall MN / command interneurons → body bend depth + reversal bias
    // REF: Ramachandran 2021 eLife — CKR-1 in SMD necessary and sufficient for ARS
    //      Bhattacharya 2014 PLOS Genetics — DA→DOP-1→DVA→NLP-12 pathway
    //      Hu 2011 Neuron — CKR-2 proprioceptive modulation
    //      Janssen 2008 — NLP-12 structure (CCK-like sulfated peptide)
    {
        Neuromodulator nlp12;
        nlp12.name = "NLP-12";
        nlp12.tau_rise = 3000.0;     // 3s — fast DCV exocytosis (Bhattacharya 2014: rapid ARS onset)
        nlp12.tau_decay = 15000.0;   // 15s — peptide degradation slower than reuptake
        nlp12.release_threshold = 0.5;  // higher than amines: DVA must be strongly active (searching + food_memory)

        // Source neuron: DVA (single, unpaired)
        if (dva_id_ >= 0) nlp12.source_neuron_ids.push_back(dva_id_);

        // Target 1: CKR-1 → SMD head motor neurons (excitatory GPCR)
        // PRIMARY ARS mechanism: NLP-12 → CKR-1 → SMD activation → large head swings
        // → forward reorientations (high-angle turns without reversal)
        // CKR-1 in SMD is SUFFICIENT for local search rescue (Ramachandran 2021 Fig 7A)
        // SMD Ca2+ elevated during ARS (0-5min off food), requires CKR-1 (Fig 8)
        // DVA→SMDVL has 1 synapse; rest is extrasynaptic NLP-12 volume transmission
        // REF: Ramachandran 2021 — ckr-1(lf) reduces reorientations 40-50%
        //      SMD photostimulation recapitulates NLP-12 overexpression effects
        // NOTE: must use get_neuron_id() here — cached IDs not yet populated
        const char* smd_names[] = {"SMDDL", "SMDDR", "SMDVL", "SMDVR"};
        for (auto name : smd_names) {
            int id = connectome_.get_neuron_id(name);
            if (id >= 0) nlp12.targets.push_back(
                {id, "CKR-1", ModulationEffect::EXCITABILITY, 5.0});
        }

        // Target 2: CKR-2 → AVA command interneurons (weak excitatory)
        // Secondary: modest reversal bias during local search
        // CKR-2 has broader expression than CKR-1 (Ramachandran 2021 Fig 5)
        // Replaces hardcoded food_memory→AVA +2.5pA injection (Step 20d)
        // REF: Hu 2011 — CKR-2/egl-30 Gαq coupling → excitatory
        // NOTE: must use get_neuron_id() — cached IDs not yet populated
        int aval = connectome_.get_neuron_id("AVAL");
        int avar = connectome_.get_neuron_id("AVAR");
        if (aval >= 0) nlp12.targets.push_back(
            {aval, "CKR-2", ModulationEffect::EXCITABILITY, 2.0});
        if (avar >= 0) nlp12.targets.push_back(
            {avar, "CKR-2", ModulationEffect::EXCITABILITY, 2.0});

        neuromod_.add_modulator(std::move(nlp12));
    }

    // --- PDF-1 (Pigment Dispersing Factor) ---
    // Step 46: Roaming neuromodulator — opposes 5-HT dwelling (Flavell 2013 Cell)
    // The roaming/dwelling switch is driven by TWO opposing neuromodulators:
    //   5-HT (NSM) → dwelling: slow, low reversal, stay on food
    //   PDF (AVB) → roaming: fast, high exploration, leave food when satiated
    // pdf-1 and pdfr-1 mutants show excessive dwelling, reduced roaming
    // PDFR-1: Gαs-coupled GPCR → cAMP → promotes speed + head movements
    //
    // Source neurons (Flavell 2013 Fig 6): AVB, RIA, ASI, PVP, SIAV, RIF
    //   AVB is the primary source — forward command neuron, active during roaming
    //   We also include RIA (head curvature regulation)
    // Target effects via PDFR-1:
    //   - Speed increase (faster locomotion during roaming)
    //   - Head movement increase (wider exploration)
    //   - Reversal rate increase (more turns → wider coverage)
    //
    // REF: Flavell 2013 Cell — 5-HT/PDF roaming/dwelling bistable switch
    //      Barrios 2012 Nat Neurosci — PDF-1 in exploratory behavior
    //      Janssen 2009 — PDFR-1 Gαs coupling
    {
        Neuromodulator pdf;
        pdf.name = "PDF";
        pdf.tau_rise = 5000.0;     // 5s — neuropeptide DCV release (slower than amines)
        pdf.tau_decay = 20000.0;   // 20s — peptide degradation (very slow, extends roaming)
        pdf.release_threshold = 0.3;

        // Source neurons: AVB (forward command) + RIA (head curvature)
        // AVB is active during forward runs → PDF accumulates during roaming
        // On food with high 5-HT: AVB suppressed (via MOD-1→AIY→less AVB drive) → PDF low
        // Off food: AVB active → PDF high → roaming
        int avbl = connectome_.get_neuron_id("AVBL");
        int avbr = connectome_.get_neuron_id("AVBR");
        int rial = connectome_.get_neuron_id("RIAL");
        int riar = connectome_.get_neuron_id("RIAR");
        if (avbl >= 0) pdf.source_neuron_ids.push_back(avbl);
        if (avbr >= 0) pdf.source_neuron_ids.push_back(avbr);
        if (rial >= 0) pdf.source_neuron_ids.push_back(rial);
        if (riar >= 0) pdf.source_neuron_ids.push_back(riar);

        // Target 1: PDFR-1 → speed increase (roaming = fast locomotion)
        // Opposes 5-HT SER-7 speed reduction (-0.40) and DA DOP-3 (-0.30)
        // Net: roaming (high PDF, low 5-HT) = fast; dwelling (high 5-HT, low PDF) = slow
        // REF: Flavell 2013 — pdf-1 mutants move slower on food
        pdf.targets.push_back(
            {-1, "PDFR-1", ModulationEffect::SPEED_SCALE, 0.25}); // +25% speed at peak PDF

        // Target 2: PDFR-1 → reversal rate increase (roaming = more turns)
        // Opposes 5-HT reversal suppression (-0.50)
        // Net: roaming has moderate reversal rate; dwelling has low reversal rate
        pdf.targets.push_back(
            {-1, "PDFR-1", ModulationEffect::REVERSAL_RATE, 0.30}); // +30% reversals at peak

        // Target 3: PDFR-1 → AIY excitation (promotes forward drive)
        // PDF → PDFR-1 on AIY → cAMP → more forward runs
        // Complements OA→SER-6→AIY but through different receptor/pathway
        // REF: Flavell 2013 — optogenetic cAMP in PDFR-1 cells → prolonged roaming
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) pdf.targets.push_back(
            {aiyl, "PDFR-1", ModulationEffect::EXCITABILITY, 3.0}); // +3 pA
        if (aiyr >= 0) pdf.targets.push_back(
            {aiyr, "PDFR-1", ModulationEffect::EXCITABILITY, 3.0});

        // Step 48: Target 4: PDFR-1 network → NSM inhibition (mutual exclusivity)
        // KEY FEEDBACK completing the roaming/dwelling bistable switch.
        // Without this, NSM keeps firing on food → 5-HT stays high → permanent dwelling.
        // Mechanism: PDF → PDFR-1 expressing neurons → inhibit NSM (indirect)
        // Modeled as PDF EXCITABILITY effect on NSM (simplification of PDFR-1 network).
        // When PDF rises (satiety → OA → AVB active → PDF), NSM is suppressed → 5-HT drops.
        // Positive feedback loop: PDF↑ → NSM↓ → 5-HT↓ → less RIC inhibition → OA↑ → AVB↑ → PDF↑↑
        // REF: Flavell 2020 eLife — "PDF receptor-expressing neurons inhibit NSM"
        //      "optogenetic activation of pdf-1 neurons → acute and robust NSM inhibition"
        //      "PDF signaling necessary and sufficient to keep NSM inactive during roaming"
        // NSM on-food drive ≈ 21pA; at PDF=0.4: inhibition = -8pA → net 13pA (moderate)
        // The bistable positive feedback amplifies small PDF changes to flip the switch.
        if (nsml_id_ >= 0) pdf.targets.push_back(
            {nsml_id_, "PDFR-1", ModulationEffect::EXCITABILITY, -25.0}); // -25 pA at peak PDF
        if (nsmr_id_ >= 0) pdf.targets.push_back(
            {nsmr_id_, "PDFR-1", ModulationEffect::EXCITABILITY, -25.0});

        neuromod_.add_modulator(std::move(pdf));
    }

    LOG_INFO("Neuromodulation setup: 5-HT (dwelling), DA (basal slowing), OA (roaming), TA (escape), NLP-12 (ARS), PDF (roaming)");
}

// ================================================================
// Satiety internal state (Step 20c)
//
// Models the feeding → insulin signaling → behavioral switch:
//   hungry → find food → dwell → eat → satiety↑ → roam → leave → hungry
//
// Mechanism:
//   1. On food: satiety increases (tau_fill ~20s)
//   2. Off food: satiety decreases (tau_deplete ~40s)
//   3. High satiety → reduce NSM sensitivity → 5-HT drops
//   4. High satiety → excite RIC → OA rises → roaming
//
// REF: You 2008 — insulin/DAF-2 modulates foraging
//      Shtonda & Bhatt 2010 — satiety quiescence
// ================================================================
void SimulationEngine::update_satiety() {
    // Step 24: Satiety now driven by REAL pharyngeal pumping
    // pump_rate (Hz) × food_concentration → actual food ingestion → satiety
    // Replaces placeholder: "dist < 3mm → satiety += dt/τ"
    //
    // Satiety accumulation: each pump near food adds food_per_pump × conc
    // Satiety depletion: metabolic consumption tau ~40s
    // The pharynx update_pharynx() already computed the pump event this step.
    // Here we just apply depletion + clamp.

    // Use FOOD DENSITY (narrow σ=3mm) not navigation gradient (wide σ=12mm)
    double food_conc = environment_.sample_food_density(body_.get_head_position());
    double on_food = food_conc * food_conc / (food_conc * food_conc + 0.09);

    // Depletion: always metabolizing (faster when not on food)
    double depletion_rate = (on_food < 0.3) ? 1.0 : 0.5;  // faster off food
    satiety_ -= satiety_ * dt_ * depletion_rate / satiety_tau_deplete_;
    if (satiety_ < 0.0) satiety_ = 0.0;
    if (satiety_ > 1.0) satiety_ = 1.0;

    // --- Effect 1: REMOVED — satiety does NOT suppress NSM ---
    // NSM is an enteric sensory neuron that fires whenever the worm eats
    // (via ASIC DEL-7/DEL-3), regardless of satiety state (Randi 2018 Cell).
    // The previous -15pA suppression created a self-limiting loop:
    //   eat → satiety ↑ → NSM suppressed → 5-HT drops → speed up → leave food
    // This is biologically incorrect. Satiety triggers roaming via a COMPETING
    // pathway (RIC→OA, Effect 2 below), not by suppressing NSM.
    // REF: Flavell 2013 Cell — satiated animals roam via PDF/OA, not NSM suppression
    //      You 2008 Cell — satiety quiescence via ASI→TGF-β/insulin, separate from NSM
    int n = static_cast<int>(neurons_.size());

    // --- Effect 2: Satiety excites RIC ---
    // High satiety → RIC fires → OA released → roaming
    // Also: RIC gets tonic baseline (5 pA) representing hunger drive
    // Net: RIC = baseline + satiety_excitation - 5-HT_inhibition (via neuromod)
    double ric_baseline = 5.0;   // pA tonic (hunger drive)
    double ric_satiety = 10.0 * satiety_;  // pA, satiety excitation
    for (int rid : ric_ids_) {
        if (rid >= 0 && rid < n) {
            neurons_[rid]->add_synaptic_current(ric_baseline + ric_satiety);
        }
    }

    // --- Effect 3: Satiety suppresses chemotaxis (ASE/AWC) ---
    // High satiety → insulin/DAF-2 → reduced chemosensory gain
    // Makes worm less responsive to food gradient → random movement → leaves food
    // REF: Tomioka 2006 — insulin signaling modulates chemotaxis
    //      Chalasani 2010 — neuropeptide modulation of AWC sensitivity
    if (satiety_ > 0.3) {
        double suppress = -5.0 * (satiety_ - 0.3) / 0.7;  // 0 at sat=0.3, -5pA at sat=1.0
        const auto& ninfos = connectome_.neuron_infos();
        for (size_t i = 0; i < chemo_mappings_.size(); ++i) {
            int nid = chemo_mappings_[i].neuron_id;
            if (nid < 0 || nid >= n) continue;
            // Only suppress main chemotaxis neurons (ASE, AWC), not food detectors (NSM, CEP)
            if (starts_with(ninfos[nid].name, "ASE") || starts_with(ninfos[nid].name, "AWC")) {
                neurons_[nid]->add_synaptic_current(suppress);
            }
        }
    }
}

// ================================================================
// Area-Restricted Search (Step 20d)
//
// Models the DA → DARPP-32 phosphorylation → GLR-1 enhancement cascade.
// This is the intracellular memory of recent food exposure:
//   - On food: DARPP-32 rapidly phosphorylated (tau_rise ~5s)
//   - Off food: slowly dephosphorylated (tau_decay ~60s)
//   - High phosphorylation → GLR-1 enhanced → AVA more excitable → more reversals
//   → worm stays near where food was (LOCAL SEARCH)
//   - Phosphorylation decays → fewer reversals → longer runs → GLOBAL SEARCH
//
// REF: Hills 2004 J Neurosci — DA + GLR-1/GLR-2 control ARS
//      Wakabayashi 2004 — pirouette frequency decay after food removal
//      Calhoun 2014 eLife — local→global search transition
// ================================================================
void SimulationEngine::update_food_memory() {
    // Sample food density (narrow σ=3mm bacterial colony)
    double food_conc = environment_.sample_food_density(body_.get_head_position());

    // Update food_memory: fast rise on food, slow decay off food
    // Uses same on_food detection as DA (CEP mechanosensory threshold)
    double on_food = food_conc / (food_conc + 0.1);  // half-max at C=0.1
    // Step 26c: Sickness accelerates food_memory decay
    // REF: Hills 2004 — DA→DARPP-32→GLR-1; sickness suppresses DA release
    // → DARPP-32 dephosphorylation → food_memory clears fast → no local search near toxin
    double effective_decay_tau = food_memory_tau_decay_;
    if (sickness_ > 0.3) {
        effective_decay_tau = 5000.0;  // 5s fast clearance (vs normal 90s)
    }

    if (on_food > food_memory_ && sickness_ < 0.3) {
        // Rising: fast phosphorylation (on food)
        // Step 26c: ONLY when healthy — sick worm doesn't encode toxic food as "good"
        food_memory_ += (on_food - food_memory_) * dt_ / food_memory_tau_rise_;
    } else {
        // Decaying: slow dephosphorylation (off food), fast if sick
        food_memory_ -= food_memory_ * dt_ / effective_decay_tau;
    }
    if (food_memory_ < 0.0) food_memory_ = 0.0;
    if (food_memory_ > 1.0) food_memory_ = 1.0;

    // Step 45: Dual ARS pathway (fast + slow)
    // Two biologically real mechanisms work in parallel:
    //   FAST: food_memory → DARPP-32 → GLR-1 → AVA excitability ↑ (intracellular, ms)
    //   SLOW: food_memory → DVA → NLP-12 → CKR-1 → SMD head swings ↑ (volume, seconds)
    // Step 45: reduced AVA injection from 2.5→1.5 pA since NLP-12→CKR-2→AVA (+2pA)
    // now provides additional reversal bias through the neuropeptide pathway.
    // REF: Hills 2004 J Neurosci — DA→DARPP-32→GLR-1 (fast, intracellular)
    //      Ramachandran 2021 eLife — NLP-12→CKR-1→SMD (slow, volume transmission)
    int n = static_cast<int>(neurons_.size());
    if (aval_id_ >= 0 && aval_id_ < n) {
        double ars_current = 1.5 * food_memory_;
        neurons_[aval_id_]->add_synaptic_current(ars_current);
    }
    // Step 45: food_memory → DVA excitation → NLP-12 release
    // High food_memory (just left food) → DVA extra drive → NLP-12 ↑
    // → CKR-1→SMD: larger head swings → forward reorientations
    // REF: Bhattacharya 2014 — proprioception + DA independently affect DVA/NLP-12
    if (dva_id_ >= 0 && dva_id_ < n) {
        double ars_dva_current = 5.0 * food_memory_;
        neurons_[dva_id_]->add_synaptic_current(ars_dva_current);
    }
}

// ================================================================
// Gradient-Dependent Klinokinesis (Step 21d)
//
// When gradient signal is weak/absent → increase pirouette rate
// → local search behavior → constrains diffusion radius
// → increases probability of re-entering gradient field
//
// Biological basis (Calhoun 2014 eLife, Gray 2005, Hills 2004):
//   AWC detects odorant decrease → AIB/AIZ interneurons
//   → neuromodulatory control of pirouette frequency
//   No signal = high pirouette rate (local search)
//   Strong signal = low pirouette rate (long runs toward food)
//
// Distinct from ARS (food_memory): ARS = PAST food contact memory,
// this = CURRENT gradient signal level.
//
// Gradient magnitude at different distances (σ=5mm):
//   5mm:  0.037 /mm → factor ≈ 0.001 (no effect)
//   10mm: 0.011 /mm → factor ≈ 0.11  (slight)
//   15mm: 0.002 /mm → factor ≈ 0.67  (strong local search)
//   20mm: 0.0003/mm → factor ≈ 0.94  (full local search)
// ================================================================
void SimulationEngine::apply_gradient_klinokinesis() {
    Vector2d head_pos = body_.get_head_position();
    Vector2d grad = environment_.chemical_field().gradient(head_pos);
    double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

    // No-signal factor: high when gradient weak, low when gradient strong
    // Threshold 0.002 /mm — very conservative, only truly lost worms
    //   5mm: 0.000, 10mm: 0.004, 15mm: 0.37, 20mm: 0.86, 25mm: 0.95
    double no_signal_factor = fast_exp(-grad_mag / 0.002);

    // Excite AVA: more reversal when no gradient signal
    // 1.0 pA max — very gentle; avoid disrupting weathervane approach
    int n = static_cast<int>(neurons_.size());
    double kk_current = 1.0 * no_signal_factor;
    if (aval_id_ >= 0 && aval_id_ < n) neurons_[aval_id_]->add_synaptic_current(kk_current);
    if (avar_id_ >= 0 && avar_id_ < n) neurons_[avar_id_]->add_synaptic_current(kk_current);
}

// ================================================================
// Short-Term Plasticity Parameter Setup (Step 21a/b)
//
// Different circuits need different recovery time constants:
// - Motor CPG (SMD/DD/VD cross-inhibition): fast recovery (300-500ms)
//   to maintain stable oscillation despite continuous activity
// - Touch (ALM/PLM→AVD/AVA): slow recovery (3-5s)
//   enables tap habituation (Rankin 1990, Wicks & Rankin 1997)
// - Sensory (ASE→AIA, AWC→AIB): medium recovery (1-2s)
//   enables sensory adaptation
// - Default: moderate recovery (2s)
//
// REF: Liu 2009 PNAS (C. elegans graded synaptic depression)
//      Tsodyks & Markram 1997 (vesicle depletion model)
// ================================================================
void SimulationEngine::setup_stp_params() {
    const auto& ninfos = connectome_.neuron_infos();
    auto& synapses = connectome_.synapses_mut();
    int n = static_cast<int>(ninfos.size());

    auto starts_with_any = [](const std::string& name, std::initializer_list<const char*> prefixes) {
        for (auto p : prefixes) {
            if (name.compare(0, std::strlen(p), p) == 0) return true;
        }
        return false;
    };

    int cpg_count = 0, touch_count = 0, sensory_count = 0, default_count = 0;

    for (auto& syn : synapses) {
        int pre = syn.pre_id();
        int post = syn.post_id();
        if (pre < 0 || pre >= n || post < 0 || post >= n) continue;

        const std::string& pre_name = ninfos[pre].name;
        const std::string& post_name = ninfos[post].name;

        // Motor CPG synapses: fast recovery to preserve oscillation
        // SMD↔SMD, DD↔VD cross-inhibition, RMD connections
        // n_ss(S=0.5) = 1/(1+0.0003*0.5*400) = 0.94 — CPG stable
        if (starts_with_any(pre_name, {"SMD", "RMD", "DD", "VD"}) &&
            starts_with_any(post_name, {"SMD", "RMD", "DD", "VD"})) {
            //                    tau_rec  alpha_d   tau_f   alpha_f  p0
            syn.set_stp_params(   400.0,   0.0003,   100.0,  0.001,   0.6);
            cpg_count++;
        }
        // Touch circuit: slow recovery for habituation
        // ALM/PLM at rest: S≈0.003 → n≈1.0 (full pool)
        // During touch: S≈0.8 → n_ss=1/(1+0.0005*0.8*4000)=0.38 (strong habituation)
        else if (starts_with_any(pre_name, {"ALM", "PLM", "ASH"}) &&
                 starts_with_any(post_name, {"AVD", "AVA", "AVB", "PVC", "AIB", "RIM"})) {
            syn.set_stp_params(  4000.0,   0.0005,   300.0,  0.003,   0.5);
            touch_count++;
        }
        // Sensory → interneuron: medium recovery for adaptation
        // n_ss(S=0.35) = 1/(1+0.0003*0.35*1500) = 0.86 (mild tonic depression)
        // n_ss(S=0.7)  = 1/(1+0.0003*0.7*1500)  = 0.76 (visible adaptation)
        else if (starts_with_any(pre_name, {"ASE", "AWC", "AWA"}) &&
                 starts_with_any(post_name, {"AIA", "AIB", "AIY", "AIZ"})) {
            syn.set_stp_params(  1500.0,   0.0003,   200.0,  0.003,   0.5);
            sensory_count++;
        }
        // Default: moderate parameters
        // n_ss(S=0.3) = 1/(1+0.0003*0.3*2000) = 0.85
        else {
            syn.set_stp_params(  2000.0,   0.0003,   200.0,  0.001,   0.5);
            default_count++;
        }
    }

    LOG_INFO("STP setup: CPG=", cpg_count, " touch=", touch_count,
             " sensory=", sensory_count, " default=", default_count);
}

// ================================================================
// Salt Chemotaxis Learning (Step 21c)
//
// Models the insulin/PI3K pathway that modulates ASER synaptic output:
//   Starvation + NaCl → AIA releases INS-1 → DAF-2 on ASER
//   → PI3K/AGE-1 activation → attenuates ASER→AIA attractive drive
//
// Simplified: Δw ∝ -(satiety - 0.5) × pre_activity × post_activity
//   - satiety > 0.5 (fed): strengthen ASER→AIA (maintain attraction)
//   - satiety < 0.5 (hungry on food): weaken ASER→AIA (learn aversion)
//   - Very slow timescale: weight changes ~0.001 per second
//
// REF: Tomioka 2006 Neuron, Adachi 2010 Nat Commun
// ================================================================
void SimulationEngine::update_salt_learning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();
    const auto& ninfos = connectome_.neuron_infos();

    // Learning signal: how satisfied is the worm?
    // satiety > 0.5 → reinforce current preferences (fed = good association)
    // satiety < 0.5 → weaken current preferences (hungry = bad association)
    double learn_signal = satiety_ - 0.5;  // [-0.5, +0.5]

    // Learning rate: very slow, ~0.001 per second × dt_effective(100ms)
    double lr = 0.0001;

    // Uses pre-indexed ASER synapse indices (avoid full scan + string compare)
    for (size_t idx : aser_syn_indices_) {
        auto& syn = synapses[idx];
        int pre = syn.pre_id();
        int post = syn.post_id();

        // Pre and post activity (sigmoid release)
        double V_pre = neurons_[pre]->get_membrane_potential();
        double V_post = neurons_[post]->get_membrane_potential();
        double S_pre = 1.0 / (1.0 + fast_exp(-(V_pre - (-35.0)) / 5.0));
        double S_post = 1.0 / (1.0 + fast_exp(-(V_post - (-35.0)) / 5.0));

        // Hebbian-like: Δw = lr × learn_signal × pre × post
        // Positive learn_signal (fed) → strengthen active synapses
        // Negative learn_signal (hungry) → weaken active synapses
        double dw = lr * learn_signal * S_pre * S_post;
        syn.adjust_weight_mod(dw);
    }
}

// ================================================================
// Step 26: Learned Pathogen Avoidance (Zhang 2005 Nature)
//
// Mechanism: eating toxic food → sickness_ rises → ADF 5-HT ↑ →
//   1) MOD-1 inhibits AIY (approach suppressed)
//   2) AWC→AIY w_mod ↓ (weaken approach pathway)
//   3) AWC→AIB w_mod ↑ (strengthen avoidance pathway)
// Result: same food odor now drives avoidance instead of approach
//
// REF: Zhang, Lu & Bargmann 2005 Nature 438:179-184
//      Ha et al. 2010 Neuron 68:1173-1186
//      Frontiers Immunol 2024 — three-circuit model
// ================================================================
void SimulationEngine::update_sickness() {
    // Sickness accumulates when the worm is EATING food that overlaps with toxin
    // "Eating" = pharyngeal pump active (pump_rate > 0) AND on food
    // "Toxic food" = food_density > 0.1 AND repellent_conc > 0.1 at same location
    Vector2d head_pos = body_.get_head_position();
    double food_here = environment_.sample_food_density(head_pos);
    double toxin_here = environment_.sample_repellent(head_pos);

    bool eating_toxic = (food_here > 0.1 && toxin_here > 0.1 && pharynx_.pump_rate_hz() > 0.5);

    if (eating_toxic) {
        // Accumulate sickness: food intake × toxicity → malaise
        // Accelerated timescale: real biology ~4-6 hours, we use ~30s
        double toxicity = toxin_here / (toxin_here + 0.3);  // saturating
        double d_sick = toxicity * dt_ / sickness_tau_rise_;
        sickness_ += d_sick;
        if (sickness_ > 1.0) sickness_ = 1.0;
    } else {
        // Slow recovery when not eating toxin
        double d_decay = sickness_ * dt_ / sickness_tau_decay_;
        sickness_ -= d_decay;
        if (sickness_ < 0.0) sickness_ = 0.0;
    }
}

void SimulationEngine::update_pathogen_learning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;
    // Only learn when sick
    if (sickness_ < 0.05) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();
    const auto& ninfos = connectome_.neuron_infos();

    // Learning rate: proportional to sickness level
    // ~0.03 per second at max sickness × dt_effective(100ms)
    // Tuned so w_mod changes ±50% in ~80s of continuous sickness
    // (800 updates × 0.003 × S_pre≈0.2 = 0.48)
    double lr = 0.003 * sickness_;

    // Uses pre-indexed AWC synapse indices (avoid full scan + string compare)
    for (size_t idx : awc_syn_indices_) {
        auto& syn = synapses[idx];
        int pre = syn.pre_id();
        int post = syn.post_id();
        const std::string& post_name = ninfos[post].name;

        // Pre activity (AWC release rate)
        double V_pre = neurons_[pre]->get_membrane_potential();
        double S_pre = 1.0 / (1.0 + fast_exp(-(V_pre - (-35.0)) / 5.0));
        if (S_pre < 0.05) continue;  // skip if AWC not active

        // AWC→AIY: WEAKEN (reduce approach pathway)
        // Sick + AWC active → this odor associated with malaise → reduce attraction
        if (post_name.compare(0, 3, "AIY") == 0) {
            syn.adjust_weight_mod(-lr * S_pre);
        }
        // AWC→AIB: STRENGTHEN (increase avoidance pathway)
        // Sick + AWC active → this odor now drives avoidance
        if (post_name.compare(0, 3, "AIB") == 0) {
            syn.adjust_weight_mod(+lr * S_pre);
        }
    }

    // Update cached awc_pref after learning modifies AWC→AIY weights
    update_awc_pref_cache();
}

// ================================================================
// GPU Compute Backend Setup (Step 22)
//
// Initializes OpenCL GPU backend for synaptic current computation.
// Falls back to CPU if no GPU available.
// Converts ChemicalSynapse objects to flat SynapseGPU structs for GPU.
// ================================================================
void SimulationEngine::setup_gpu_backend() {
    // GPU acceleration is only beneficial at scale (>500 synapses).
    // At 72 neurons / ~110 synapses, kernel launch overhead dominates.
    // Auto-enable when synapse count exceeds threshold.
    size_t num_syn = connectome_.num_synapses();
    bool should_use_gpu = (num_syn >= 500);

    if (should_use_gpu && ComputeBackend::opencl_available()) {
        gpu_backend_ = ComputeBackend::create_opencl();
        if (gpu_backend_) {
            use_gpu_ = true;
            auto info = gpu_backend_->device_info();
            LOG_INFO("GPU backend ACTIVE: ", info.name, " (", info.max_compute_units,
                     " CUs, ", num_syn, " synapses)");

            sync_synapses_to_gpu();
            int nn = static_cast<int>(neurons_.size());
            gpu_V_.resize(nn, 0.0f);
            gpu_I_.resize(nn, 0.0f);
            return;
        }
    }

    use_gpu_ = false;
    if (num_syn < 500) {
        LOG_INFO("GPU: skipped (", num_syn, " synapses < 500 threshold, CPU faster)");
    } else {
        LOG_INFO("GPU: not available, using CPU");
    }
}

void SimulationEngine::sync_synapses_to_gpu() {
    if (!gpu_backend_ || !use_gpu_) return;

    const auto& synapses = connectome_.synapses();
    gpu_synapses_.clear();
    gpu_synapses_.reserve(synapses.size());

    for (const auto& syn : synapses) {
        SynapseGPU gs;
        gs.pre_id = syn.pre_id();
        gs.post_id = syn.post_id();
        gs.weight = static_cast<float>(syn.weight());
        gs.g_max = 0.5f;       // default g_max
        gs.E_syn = static_cast<float>(syn.reversal_potential());
        gs.V_thresh = -35.0f;  // default
        gs.V_slope = 5.0f;     // default
        gs.weight_mod = static_cast<float>(syn.weight_mod());
        gs.vesicle_pool = static_cast<float>(syn.vesicle_pool());
        gs.release_prob = static_cast<float>(syn.release_prob());
        gs.p0 = 0.5f;          // default baseline release probability
        gs.tau_recovery = 2000.0f;
        gs.alpha_d = 0.0003f;
        gs.tau_facil = 200.0f;
        gs.alpha_f = 0.001f;
        gpu_synapses_.push_back(gs);
    }

    gpu_backend_->upload_synapses(gpu_synapses_);
    LOG_INFO("GPU: uploaded ", gpu_synapses_.size(), " synapses");
}

// ================================================================
// Step 24: Pharyngeal Pumping System
//
// The pharynx is an independent neuromuscular pump (20 neurons, 14 types).
// We model the 5 essential types: MC (pacemaker), M3 (relaxation),
// M4 (isthmus peristalsis), I1 (bridge), RIP (extrapharyngeal bridge).
//
// Pump cycle: MC fires → muscle AP (E→P→R) → M3 proprioceptive → relax
// Rate: ~4 Hz on food (5-HT), ~1 Hz off food (intrinsic muscle)
// Food ingestion: pump_rate × food_concentration → satiety
//
// REF: Avery (WormBook 2012), Raizen & Avery 1994, Song & Avery 2012
// ================================================================

void SimulationEngine::apply_pharyngeal_modulation() {
    // 5-HT → MC: SER-7 receptor excitation (increases pump rate)
    // REF: Song & Avery 2012 eLife — 5-HT activates MC via SER-7
    //      Hobson 2006 Genetics — SER-7 necessary for 5-HT stimulation of pumping
    // OA → MC: inhibition (decreases pump rate)
    // REF: Niacaris & Bhatt 2003 — OA suppresses pumping
    int n = static_cast<int>(neurons_.size());

    double sht_conc = neuromod_.get_concentration("5-HT");
    double oa_conc = neuromod_.get_concentration("OA");

    // 5-HT excites MC: +15 pA at full 5-HT → faster firing → higher pump rate
    // OA inhibits MC: -10 pA at full OA → slower firing → lower pump rate
    double mc_5ht_current = 15.0 * sht_conc;   // excitatory
    double mc_oa_current = -10.0 * oa_conc;     // inhibitory

    for (int id : mc_ids_) {
        if (id >= 0 && id < n) {
            // MC tonic drive: baseline + food detection + neuromodulation
            // MC has mechanosensory ending that detects bacteria in pharynx
            double food_conc = environment_.sample_food_density(body_.get_head_position());
            double food_drive = 8.0 * food_conc / (food_conc + 0.1);  // 0-8 pA, half-max at 0.1
            double mc_tonic = 3.0 + food_drive + mc_5ht_current + mc_oa_current;
            neurons_[id]->add_synaptic_current(mc_tonic);
        }
    }

    // M3 gets tonic baseline + proprioceptive drive from pharyngeal muscle
    // M3 fires when muscle is contracted (during plateau phase)
    double m3_drive = 0.0;
    if (pharynx_.phase() == PharyngealPump::Phase::PLATEAU) {
        m3_drive = 12.0;  // proprioceptive: strong during contraction
    } else if (pharynx_.phase() == PharyngealPump::Phase::EXCITATION) {
        m3_drive = 5.0;   // beginning of contraction
    }
    for (int id : m3_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(2.0 + m3_drive);  // 2 pA baseline + proprioceptive
        }
    }

    // M4 tonic: low baseline, activated by MC pumping + 5-HT
    // M4 drives isthmus peristalsis (food transport to terminal bulb)
    if (m4_id_ >= 0 && m4_id_ < n) {
        double m4_5ht = 8.0 * sht_conc;  // 5-HT also activates M4
        neurons_[m4_id_]->add_synaptic_current(2.0 + m4_5ht);
    }

    // I1 gets small baseline — mainly relays RIP signals
    for (int id : i1_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(1.0);
        }
    }
}

void SimulationEngine::update_pharynx() {
    // Read MC and M3 neuron outputs (release probability)
    // MC output → triggers pump (excitatory)
    // M3 output → triggers relaxation (inhibitory on muscle, but we read it as release)
    int n = static_cast<int>(neurons_.size());

    // Average MC release across L/R
    double mc_release = 0.0;
    int mc_count = 0;
    for (int id : mc_ids_) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            // Sigmoid release function: S = 1/(1+exp(-(V-V_half)/slope))
            double s = 1.0 / (1.0 + fast_exp(-(v - (-35.0)) / 5.0));
            mc_release += s;
            mc_count++;
        }
    }
    if (mc_count > 0) mc_release /= mc_count;

    // Average M3 release across L/R
    double m3_release = 0.0;
    int m3_count = 0;
    for (int id : m3_ids_) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            double s = 1.0 / (1.0 + fast_exp(-(v - (-35.0)) / 5.0));
            m3_release += s;
            m3_count++;
        }
    }
    if (m3_count > 0) m3_release /= m3_count;

    // Update pharyngeal muscle state machine
    bool pump_event = pharynx_.update(mc_release, m3_release, dt_);

    // Food ingestion: pump near food → satiety (narrow σ=3mm food zone)
    if (pump_event) {
        double food_conc = environment_.sample_food_density(body_.get_head_position());
        double food_ingested = pharynx_.compute_food_intake(food_conc, true);
        satiety_ += food_ingested;
        if (satiety_ > 1.0) satiety_ = 1.0;
    }
}

// ================================================================
// Step 27: Sleep / Quiescence (Lethargus)
//
// Homeostatic sleep drive: activity → fatigue accumulates → threshold
// → RIS depolarizes → FLP-11 release → systemic quiescence
//
// Wake: fatigue decays → RIS deactivates → locomotion resumes
//
// FLP-11 self-inhibition (2025 Current Biology):
//   High FLP-11 → negative feedback on RIS → spontaneous awakening
//
// Arousal threshold: strong touch stimulus can override sleep
//   ALM 80pA >> FLP-11 inhibition ~20pA → worm wakes
//
// REF: Turek 2016 eLife — FLP-11 is major sleep inducer
//      Konietzka 2020 Nat Commun — RIS as locomotion stop neuron
//      Nagy 2014 eLife — sleep homeostasis
//      Maluck 2023 PLOS Genetics — RIS survival function
// ================================================================

void SimulationEngine::update_fatigue() {
    // Fatigue accumulates proportionally to locomotion activity
    // Models adenosine/somnogens buildup during wakefulness
    double speed = body_.get_speed();
    double activity = std::min(speed / 0.2, 1.0);  // normalize to ~0.2 mm/s typical speed

    if (!is_sleeping_) {
        // Awake: fatigue rises with activity
        fatigue_ += activity * dt_ / fatigue_tau_rise_;
    } else {
        // Sleeping: fatigue decays (restorative process)
        fatigue_ -= fatigue_ * dt_ / fatigue_tau_decay_;
    }
    if (fatigue_ < 0.0) fatigue_ = 0.0;
    if (fatigue_ > 1.0) fatigue_ = 1.0;

    // Sleep state transitions (flip-flop switch with hysteresis)
    // REF: Saper 2005 — mutual inhibition between wake/sleep = flip-flop
    if (!is_sleeping_ && fatigue_ > fatigue_threshold_) {
        is_sleeping_ = true;  // fall asleep
    } else if (is_sleeping_ && fatigue_ < 0.15) {
        is_sleeping_ = false; // wake up (hysteresis: wake threshold << sleep threshold)
    }

    // Drive RIS neuron based on fatigue level + sleep state
    // RIS is tonically silent (2pA) when awake, strongly activated during sleep
    // Uses flip-flop hysteresis: once sleeping, RIS gets strong maintenance drive
    // REF: Saper 2005 — mutual inhibition between wake/sleep = stable states
    int n = static_cast<int>(neurons_.size());
    if (ris_id_ >= 0 && ris_id_ < n) {
        // Base drive: sigmoid of fatigue around threshold
        double fatigue_drive = 40.0 / (1.0 + fast_exp(-12.0 * (fatigue_ - fatigue_threshold_)));
        // Sleep maintenance: once asleep, RIS gets extra sustained drive
        // This models the flip-flop stable state — sleep is self-reinforcing
        double sleep_maintenance = is_sleeping_ ? 25.0 : 0.0;
        double ris_drive = 2.0 + fatigue_drive + sleep_maintenance;
        // FLP-11 self-inhibition: high RIS activity feeds back negatively
        // REF: 2025 Current Biology — FLP-11 induces and self-inhibits sleep
        // Weak feedback: allows RIS to reach high activation during sleep
        // Strong feedback would prevent sleep maintenance
        double ris_V = neurons_[ris_id_]->get_membrane_potential();
        double ris_release = 1.0 / (1.0 + fast_exp(-(ris_V - (-35.0)) / 5.0));
        double self_inhibition = -3.0 * ris_release;  // weak negative feedback
        neurons_[ris_id_]->set_external_current(ris_drive + self_inhibition);
    }
}

void SimulationEngine::apply_sleep_effects() {
    // FLP-11 volume transmission: RIS release rate → global inhibition
    // FLP-11 acts through GPCRs: FRPR-3, NPR-4, NPR-22 (multiple redundant)
    // Targets: motor neurons, command interneurons, pharyngeal muscle
    // REF: Turek 2016 eLife — FLP-11 overexpression → anachronistic quiescence
    //      NPR-22 on pharynx muscle + head muscle → direct inhibition

    int n = static_cast<int>(neurons_.size());
    if (ris_id_ < 0 || ris_id_ >= n) return;

    // Compute FLP-11 concentration from RIS release rate
    double ris_V = neurons_[ris_id_]->get_membrane_potential();
    double flp11 = 1.0 / (1.0 + fast_exp(-(ris_V - (-35.0)) / 5.0));

    // Only apply effects when FLP-11 is significant (>0.3)
    if (flp11 < 0.1) return;

    // Effect 1: Speed suppression handled in step() after neuromodulation
    // (sleep_speed_factor applied to body_.set_speed_scale in step())

    // Effect 2: Inhibit command interneurons (AVA/AVB)
    // FLP-11 → FRPR-3/NPR-4 on command neurons → hyperpolarize
    double cmd_inhibition = -15.0 * flp11;  // up to -15 pA
    const char* cmd_names[] = {"AVAL", "AVAR", "AVBL", "AVBR"};
    for (auto name : cmd_names) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(cmd_inhibition);
        }
    }

    // Effect 3: Suppress pharyngeal pumping
    // FLP-11 → NPR-22 on pharynx muscle → pump rate drops
    // REF: Konietzka 2020 — RIS activation inhibits pharyngeal pumping
    double mc_inhibition = -12.0 * flp11;  // up to -12 pA on MC
    for (int id : mc_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(mc_inhibition);
        }
    }

    // Effect 4: Suppress head motor oscillation
    // FLP-11 → head motor neurons → reduce head swing amplitude
    // Strong inhibition needed to overcome CCA-1 rebound oscillator
    double head_inhibition = -20.0 * flp11;  // up to -20 pA (must exceed tonic 3pA + channel noise)
    for (int id : head_motor_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(head_inhibition);
        }
    }

    // Effect 5: Suppress ventral cord motor neurons (body wall muscle drive)
    // FLP-11 → NPR-22 on body wall muscle + motor neurons → atonia
    // REF: Turek 2016 — FLP-11 receptors include NPR-22 expressed in muscle
    //      Konietzka 2020 — RIS activation stops ALL locomotion, not just head
    // These are the DIRECT drivers of muscle_work → speed
    // Without inhibiting these, residual muscle activity keeps the worm creeping
    // Strong: must push motor neurons well below threshold to stop muscle contraction
    // REF: NPR-22 expressed in body wall muscle → direct hyperpolarization
    double motor_inhibition = -30.0 * flp11;  // up to -30 pA (deep atonia, overcome CCA-1 rebound)
    const char* motor_names[] = {
        "DA01", "DA02", "DA03", "DB01", "DB02", "DB03",
        "VA01", "VA02", "VA03", "VB01", "VB02", "VB03",
        "DD01", "DD02", "DD03", "VD01", "VD02", "VD03"
    };
    for (auto name : motor_names) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(motor_inhibition);
        }
    }
}

} // namespace celegans
