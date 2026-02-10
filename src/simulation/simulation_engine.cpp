#include "simulation/simulation_engine.h"
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
    environment_.chemical_field().add_point_source(Vector2d{35.0, 35.0}, 1.0);
    LOG_INFO("Environment initialized (50x50 mm), food source at (35, 35)");

    // 7. Classify sensory neurons: chemosensory get transducers, others get baseline
    for (auto& info : neuron_infos) {
        if (info.type != NeuronType::SENSORY) continue;

        // Chemosensory neurons: detect concentration temporal derivative
        // ASEL: ON (excited by [NaCl] increase)
        // ASER: OFF (excited by [NaCl] decrease)
        // AWC:  OFF (excited by odor removal)
        // AWA:  ON (excited by odor addition)
        // ASH:  nociceptive, also responds to high osmolarity (ON-like)
        if (starts_with(info.name, "ASEL")) {
            // fast_tau=100ms: captures 2Hz head oscillation for klinotaxis
            // Downstream separation: klinokinesis pathway has 5s adaptation (pirouette)
            //                        klinotaxis pathway (SMB) responds to oscillatory component
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 100.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "ASER")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 100.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "AWC")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 80.0, 5.0, 100.0)});
        } else if (starts_with(info.name, "AWA")) {
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
            // NSM: pharyngeal neuron, detects food (absolute concentration)
            // TONIC: fires proportionally to food concentration, not dC/dt
            // REF: Flavell 2013 — NSM tonically active on food
            // uses_food_density=true: bacteria are localized (σ=3mm), not diffuse
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 30.0, 1.0, 500.0), true});
        } else if (starts_with(info.name, "CEP")) {
            // CEP: head mechanosensory, detects bacteria (food presence)
            // TONIC: fires when on food lawn, not responding to changes
            // REF: Sawin 2000 — CEP active on bacterial lawn
            // uses_food_density=true: detects physical bacteria contact
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::TONIC, 20.0, 1.0, 500.0), true});
        } else if (starts_with(info.name, "AFD")) {
            // AFD: thermosensory neuron — handled by thermo_mappings, not chemo
            // ThermoTransducer: gain=150, baseline=5pA, Tc_tau=3600s(1hr), fast_tau=200ms
            // Low baseline (5pA): avoid tonic over-activation of AIY that disrupts chemotaxis
            // gain=150: strong modulation when approaching/leaving Tc (ratio AFD/ASE~0.78)
            thermo_mappings_.push_back({info.id, ThermoTransducer(150.0, 5.0, 3600000.0, 200.0)});
        } else if (!starts_with(info.name, "ALM") && !starts_with(info.name, "PLM")
                   && !starts_with(info.name, "ADF")) {
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
    }

    // 9. Build proprioceptive mappings (motor neuron → body segment for MEC channel)
    // Dorsal B-class: sense anterior curvature, negative curv (ventral bend) excites
    auto add_pm = [&](const char* name, int seg, bool dorsal) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0) proprio_mappings_.push_back({id, seg, dorsal});
    };
    add_pm("DB01", 0, true);  add_pm("DB02", 5, true);  add_pm("DB03", 15, true);
    add_pm("VB01", 0, false); add_pm("VB02", 5, false); add_pm("VB03", 15, false);
    add_pm("DA01", 0, true);  add_pm("DA02", 5, true);  add_pm("DA03", 15, true);
    add_pm("VA01", 0, false); add_pm("VA02", 5, false); add_pm("VA03", 15, false);

    // 10. Collect touch neuron IDs (Step 18) + pharyngeal neuron IDs (Step 24)
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "ALM")) alm_ids_.push_back(info.id);
        if (starts_with(info.name, "PLM")) plm_ids_.push_back(info.id);
        if (starts_with(info.name, "RIC")) ric_ids_.push_back(info.id);
        // Step 24: Pharyngeal neurons
        if (starts_with(info.name, "MC") && info.name.size() <= 3) mc_ids_.push_back(info.id);
        if (starts_with(info.name, "M3")) m3_ids_.push_back(info.id);
        if (info.name == "M4") m4_id_ = info.id;
        if (starts_with(info.name, "I1")) i1_ids_.push_back(info.id);
    }

    // Initialize transducers with current concentration at head
    double init_conc = environment_.sample_chemical(body_.get_head_position());
    for (auto& cm : chemo_mappings_) {
        cm.transducer.reset(init_conc);
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

    // 2b. Thermosensory input: AFD samples temperature field (Step 23)
    // Uses add_synaptic_current() → MUST be after reset
    apply_thermo_input();

    // 2d. Omega turn: post-reversal deep ventral bend (Step 18)
    // Uses add_synaptic_current() on SMD → MUST be after reset
    apply_omega_turn();

    // Step 15/19: Weathervane — gradient ⊥ heading → SMD bias (Iino & Yoshida 2009)
    apply_weathervane();

    // Step 19: RIA → SMD neuromodulation via CCA-1 threshold shift
    apply_ria_smd_modulation();

    // Step 19 Phase 2: SMB neck curvature bias (klinotaxis effector)
    apply_smb_neck_bias();

    // 5a2. Step 24: Pharyngeal CPG — MC/M3 drive pump, 5-HT/OA modulate
    apply_pharyngeal_modulation();  // 5-HT→MC excitation, OA→MC inhibition
    update_pharynx();               // pharyngeal muscle AP + food ingestion → satiety

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

    // 5c. Neuromodulation update (Step 20, Layer 6)
    // Slow timescale: 5-HT/DA/OA concentrations rise/fall over seconds
    // Effects: tonic currents on target neurons, speed modulation
    neuromod_.update(neurons_, dt_);

    // Apply neuromodulation speed scaling to body
    body_.set_speed_scale(params.speed_scale * neuromod_.get_speed_scale());

    // 6. Update all neuron membrane potentials
    for (auto& neuron : neurons_) {
        neuron->step(dt_);
    }

    // 7. Motor output: motor neurons → muscle activations
    motor_controller_.update(neurons_, body_);

    // 8. Command neuron balance → locomotion direction
    // AVA dominant → reverse, AVB dominant → forward
    {
        int ava_l = connectome_.get_neuron_id("AVAL");
        int ava_r = connectome_.get_neuron_id("AVAR");
        int avb_l = connectome_.get_neuron_id("AVBL");
        int avb_r = connectome_.get_neuron_id("AVBR");
        double ava_rel = 0.0, avb_rel = 0.0;
        int n = static_cast<int>(neurons_.size());
        if (ava_l >= 0 && ava_l < n) ava_rel += neurons_[ava_l]->get_transmitter_release_rate();
        if (ava_r >= 0 && ava_r < n) ava_rel += neurons_[ava_r]->get_transmitter_release_rate();
        if (avb_l >= 0 && avb_l < n) avb_rel += neurons_[avb_l]->get_transmitter_release_rate();
        if (avb_r >= 0 && avb_r < n) avb_rel += neurons_[avb_r]->get_transmitter_release_rate();
        ava_rel *= 0.5; avb_rel *= 0.5; // average L/R
        body_.set_locomotion_state(avb_rel, ava_rel);
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
    double sat_switch = 1.0 / (1.0 + std::exp(-10.0 * (satiety_ - 0.5)));
    double chemo_sat_gain = 1.0 - 0.85 * sat_switch;  // hungry: 1.0, fed: 0.15

    // Food density at head (narrow σ=3mm for NSM/CEP food detectors)
    double food_density = environment_.sample_food_density(head_pos);

    for (auto& cm : chemo_mappings_) {
        if (cm.neuron_id < 0 || cm.neuron_id >= n) continue;
        // NSM/CEP: use narrow food density; ASE/AWC/etc: use wide navigation gradient
        double input_conc = cm.uses_food_density ? food_density : concentration;
        double I_sensory = cm.transducer.update(input_conc, dt_);
        I_sensory *= static_cast<double>(params.sensory_gain) * chemo_sat_gain;
        neurons_[cm.neuron_id]->set_external_current(I_sensory);
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
            double I_adf = 2.0 + 30.0 * sickness_;  // 2pA baseline, up to 32pA when sick
            neurons_[adf_id]->set_external_current(I_adf);
        }
    }

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
    double sat_switch_t = 1.0 / (1.0 + std::exp(-10.0 * (satiety_ - 0.5)));
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
    int n = static_cast<int>(neurons_.size());
    for (int id : head_motor_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(head_tonic_);
        }
    }
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
    Vector2d grad = environment_.chemical_field().gradient(head_pos);

    // Decompose gradient into tangential (along heading) and normal (perpendicular) components
    double heading = body_.get_head_angle();
    double cos_h = std::cos(heading);
    double sin_h = std::sin(heading);

    // Normal component: gradient projected onto the direction 90° left of heading
    // Positive = gradient points to the left → curve left (dorsal activation)
    // In C. elegans body coordinates: dorsal turn = positive curvature
    double grad_normal = -sin_h * grad.x + cos_h * grad.y;

    // Convert to differential current bias
    // 12.7 °/mm per mM/mm gradient (Iino 2009), but our gradient is in concentration/mm
    // Scale factor calibrated for our chemical field (Gaussian, peak=1.0, sigma²=25)
    // At 14mm from source: gradient ~0.011 conc/mm → bias ~0.14 °/mm → small but cumulative
    double weathervane_gain = static_cast<double>(params.weathervane_gain);

    // Step 23c: Satiety modulates chemotaxis weathervane gain
    double sat_switch_wv = 1.0 / (1.0 + std::exp(-10.0 * (satiety_ - 0.5)));
    double chemo_wv_gain = 1.0 - 0.85 * sat_switch_wv;  // fed: 0.15, hungry: 1.0
    double bias_current = weathervane_gain * grad_normal * chemo_wv_gain;

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

    // Apply: positive bias → more dorsal drive (curve toward gradient)
    //        negative bias → more ventral drive
    // Use add_synaptic_current to accumulate (external current is set by other stages)
    auto apply_bias = [&](const char* name, double current) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(current);
        }
    };

    // Step 19: Apply bias to shift half-center duty cycle
    // Sign: positive grad_normal → dorsal bias → positive curvature → heading increases
    apply_bias("SMDDL", bias_current);
    apply_bias("SMDDR", bias_current);
    apply_bias("SMDVL", -bias_current);
    apply_bias("SMDVR", -bias_current);

    // Direct curvature bias: bypass SMD oscillator bottleneck (110mV amplitude drowns ±24pA bias)
    // REF: diagnosed in Step 15 — SMD bias alone gives CI=0.07, with curv_bias CI=0.76
    // Recalibrated for σ²=144 gradient (4.5x stronger than old σ²=25):
    //   0.15 → 0.035 to maintain same turning radius (~2mm at 14mm from food)
    double curv_gain = weathervane_gain * 0.06;
    double curv_bias = curv_gain * grad_normal * chemo_wv_gain;
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
    // Don't override omega turn's ±8.0 curvature_bias (set by apply_omega_turn)
    if (!omega_pending_) {
        body_.set_curvature_bias(curv_bias);
    }
}

void SimulationEngine::apply_smb_neck_bias() {
    // Step 19 Phase 2: Simplified RIA gate-and-switch → SMB neck curvature bias
    //
    // Biological mechanism (Ouellette 2018, eNeuro):
    //   RIA has subcellular nrV/nrD domains that receive motor feedback from SMD.
    //   Sensory input is gated by head position → RIA output is the PRODUCT of
    //   sensory signal × motor state → extracts perpendicular gradient component.
    //
    // Mathematical essence:
    //   sensory(t) = ASE_ON - ASE_OFF ∝ dC/dt ∝ grad_normal × sin(ωt)
    //   curvature(t) ∝ sin(ωt)
    //   <sensory × curvature> = grad_normal × <sin²(ωt)> = grad_normal / 2 ≠ 0
    //   → DC component proportional to perpendicular gradient!
    //
    // This is NOT a bypass: it reads actual neural activity (ASE release rates)
    // and actual body state (head curvature). The signal passes through the
    // transducer → neuron → release rate before being used.
    // Same principle as AVA/AVB → forward/reverse: neural output → motor state.
    //
    // REF: Iino & Yoshida 2009 — curving rate ∝ ∇C_⊥
    //      Izquierdo 2015 — klinotaxis through SMB motor neurons

    int n = static_cast<int>(neurons_.size());
    auto get_rel = [&](const char* name) -> double {
        int id = connectome_.get_neuron_id(name);
        return (id >= 0 && id < n) ? neurons_[id]->get_transmitter_release_rate() : 0.5;
    };

    // Sensory signal: ASE ON-OFF differential (from neural activity, not raw gradient)
    double asel_rel = 0.5 * (get_rel("ASEL") + get_rel("AWCL")); // average ON-type
    double aser_rel = 0.5 * (get_rel("ASER") + get_rel("AWCR")); // average OFF-type
    double sensory_diff = asel_rel - aser_rel;  // positive = C increasing

    // Extract oscillatory component: remove DC baseline with 2s time constant
    // The DC component (trend over seconds) drives klinokinesis (pirouettes)
    // The AC component (phase-locked to head oscillation) drives klinotaxis
    // Without this separation, DC × curvature produces huge AC noise that
    // swamps the true direction signal (AC × curvature → DC)
    sensory_diff_mean_ += (sensory_diff - sensory_diff_mean_) * dt_ / 2000.0;
    double sensory_ac = sensory_diff - sensory_diff_mean_;  // oscillatory only

    // Motor state: head curvature (proprioceptive feedback to RIA)
    double head_curv = body_.get_local_curvature(0);

    // Gate-and-switch: multiply oscillatory sensory × motor to extract direction
    // <sensory_ac(t) × curvature(t)> = gradient_normal × amplitude² / 2 ≠ 0
    // This DC component is proportional to the perpendicular gradient!
    double ria_product = sensory_ac * head_curv;

    // Smooth with ~300ms (just over half an oscillation cycle to clean up 2f ripple)
    ria_curv_filtered_ += (ria_product - ria_curv_filtered_) * dt_ / 300.0;

    // Convert to curvature bias
    // With clean AC signal: sensory_ac ≈ ±0.04 release, curvature ≈ ±0.07/mm
    //   DC component ≈ 0.04 × 0.07 / 2 = 0.0014
    //   × gain 5000 → bias ≈ 7/mm → clamped to 2/mm → dθ/dt ≈ 0.2×2×57 ≈ 23°/s
    double klinotaxis_gain = 6000.0;  // /mm per unit (release × curvature)
    double curvature_offset = klinotaxis_gain * ria_curv_filtered_;

    // Clamp
    double max_bias = 2.0;
    if (curvature_offset > max_bias) curvature_offset = max_bias;
    if (curvature_offset < -max_bias) curvature_offset = -max_bias;

    // Don't override omega turn's ±8.0 curvature_bias
    if (!omega_pending_) {
        body_.set_curvature_bias(curvature_offset);
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
    int rial_id = connectome_.get_neuron_id("RIAL");
    int riar_id = connectome_.get_neuron_id("RIAR");

    double ria_release_L = 0.0, ria_release_R = 0.0;
    if (rial_id >= 0 && rial_id < n)
        ria_release_L = neurons_[rial_id]->get_transmitter_release_rate();
    if (riar_id >= 0 && riar_id < n)
        ria_release_R = neurons_[riar_id]->get_transmitter_release_rate();

    // Modulation gain: how much RIA release shifts CCA-1 V_half (mV)
    // At release=0.5 (baseline): shift=0 (symmetric)
    // At release=0.7: shift = +3mV → easier burst → longer duty cycle
    // At release=0.3: shift = -3mV → harder burst → shorter duty cycle
    // 15 mV/unit: calibrated so ±0.1 release diff → ±1.5mV CCA-1 shift
    // CCA-1 V_half is -48mV, slope=5mV, so 1.5mV shift changes m_inf significantly
    double mod_gain = 15.0;  // mV per unit release rate deviation from 0.5
    double shift_L = mod_gain * (ria_release_L - 0.5);
    double shift_R = mod_gain * (ria_release_R - 0.5);

    // Apply to SMD neurons: RIAL drives SMDDL/SMDVL, RIAR drives SMDDR/SMDVR
    auto modulate = [&](const char* name, double shift) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0 && id < n) {
            auto* scn = dynamic_cast<SingleCompartmentNeuron*>(neurons_[id].get());
            if (scn) scn->set_cca1_activation_shift(shift);
        }
    };

    modulate("SMDDL", shift_L);
    modulate("SMDVL", shift_L);
    modulate("SMDDR", shift_R);
    modulate("SMDVR", shift_R);
}

void SimulationEngine::apply_proprioceptive_stretch() {
    // Set stretch input on MechanoSensitive channels in motor neurons
    // The channel converts mechanical stretch into ionic current through the membrane equation
    // NO external current injection — the current comes from g_MEC * m * (V - E_cat)
    // REF: Wen et al. 2012, Li et al. 2006 - mechanosensitive channels in motor neurons
    int n = static_cast<int>(neurons_.size());
    for (auto& pm : proprio_mappings_) {
        if (pm.neuron_id < 0 || pm.neuron_id >= n) continue;

        double curv = body_.get_local_curvature(pm.sample_segment);

        // Dorsal motor neurons: excited by ventral bend (negative curvature) of anterior segment
        // Ventral motor neurons: excited by dorsal bend (positive curvature) of anterior segment
        double stretch = pm.is_dorsal ? -curv : curv;
        if (stretch < 0.0) stretch = 0.0; // only positive stretch activates channel

        // Set stretch on the neuron's MEC channel (flows through membrane equation)
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

    if (front_touch) {
        // Strong current pulse to ALM neurons → triggers reversal via ALM→AVD→AVA
        for (int id : alm_ids_) {
            if (id >= 0 && id < n) {
                neurons_[id]->set_external_current(touch_current_);
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
    double sat_switch = 1.0 / (1.0 + std::exp(-10.0 * (satiety_ - 0.5)));
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
    //    x = 0 (flat field) → r = r_mid ≈ 0.035/s (spontaneous)
    bool was_reversing = is_reversing_;
    if (!is_reversing_) {
        double r_min = 0.01;   // /s, strongly suppressed when heading up gradient
        double r_max = 0.16;   // /s, elevated rate when heading down gradient
        // Asymmetry ratio 16:1 ensures long runs toward food, short runs away
        // REF: Pierce-Shimomura 1999 — pirouette rate heavily suppressed during approach
        double pir_rate = r_min + (r_max - r_min) /
                          (1.0 + std::exp(-combined));

        double p_pir = pir_rate * (dt_ / 1000.0);
        std::uniform_real_distribution<double> rdist(0.0, 1.0);
        if (current_time_ > reversal_refractory_end_ && rdist(touch_rng_) < p_pir) {
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
    }
    if (!is_reversing_ && was_reversing) {
        // Reversal just ended — decide omega turn
        reversal_duration_ = current_time_ - reversal_start_time_;
        // Longer reversals → higher omega probability (Wang et al. 2020)
        // P(omega) = 1 - exp(-duration/tau), tau ~ 1000ms
        double p_omega = 1.0 - std::exp(-reversal_duration_ / 1000.0);
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        if (dist(touch_rng_) < p_omega) {
            omega_pending_ = true;
            omega_heading_before_ = body_.get_head_angle();
            Vector2d hp2 = body_.get_head_position();
            Vector2d fp2 = {35.0, 35.0};
            omega_dist_before_ = std::sqrt((hp2.x-fp2.x)*(hp2.x-fp2.x)+(hp2.y-fp2.y)*(hp2.y-fp2.y));

            // Gradient-biased omega direction (Pierce-Shimomura 1999 Fig 9)
            Vector2d head_pos = body_.get_head_position();
            double heading = body_.get_head_angle();
            double cos_h = std::cos(heading);
            double sin_h = std::sin(heading);

            // Select target gradient based on satiety mode
            double omega_sat_switch = 1.0 / (1.0 + std::exp(-10.0 * (satiety_ - 0.5)));
            Vector2d chem_grad = environment_.chemical_field().gradient(head_pos);
            Vector2d tgrad = environment_.temperature_gradient(head_pos);
            double temp_here = environment_.sample_temperature(head_pos);
            double tsign = (temp_here > cultivation_temp_) ? -1.0 : 1.0;
            Vector2d temp_target_grad = {tsign * tgrad.x, tsign * tgrad.y};

            // Blend: hungry → chem_grad, fed → temp_target_grad
            Vector2d grad;
            grad.x = (1.0 - omega_sat_switch) * chem_grad.x + omega_sat_switch * temp_target_grad.x;
            grad.y = (1.0 - omega_sat_switch) * chem_grad.y + omega_sat_switch * temp_target_grad.y;

            double grad_along = cos_h * grad.x + sin_h * grad.y;
            double grad_perp  = -sin_h * grad.x + cos_h * grad.y;
            double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

            double angle_to_target = 0.0;
            if (grad_mag > 0.001 && dist(touch_rng_) < 0.70) {
                angle_to_target = std::atan2(grad_perp, grad_along);
                omega_direction_ = (angle_to_target > 0) ? -1.0 : 1.0;
            } else {
                omega_direction_ = (dist(touch_rng_) < 0.8) ? 1.0 : -1.0;
                angle_to_target = omega_direction_ * 1.57;  // default ~90deg for random
            }

            // Omega duration proportional to |angle_to_target|
            // dtheta/dt = speed × curv_bias ≈ 0.21 × 8.0 = 1.68 rad/s
            // duration = |angle| / 1.68, range 300-2000ms
            // REF: Gray 2005 — omega turn duration 0.5-3s, mean ~1.5s
            double omega_rate = 1.68;  // rad/s (speed × curv_bias, approximate)
            double omega_dur_ms = std::abs(angle_to_target) / omega_rate * 1000.0;
            if (omega_dur_ms < 300.0) omega_dur_ms = 300.0;
            if (omega_dur_ms > 2000.0) omega_dur_ms = 2000.0;
            omega_end_time_ = current_time_ + omega_dur_ms;

        }
    }
}

void SimulationEngine::apply_omega_turn() {
    // Step 18: Deep ventral bend after reversal (Gray 2005, Wang 2020)
    // SMD neurons drive the omega turn amplitude.
    // Inject strong asymmetric current to SMD ventral (or dorsal) to create >140° bend.
    if (!omega_pending_) return;

    if (current_time_ > omega_end_time_) {
        omega_pending_ = false;
        body_.set_omega_mode(false);
        return;
    }

    // Omega turn: deep bend >140° (Gray 2005)
    // The muscle_gain (0.3) limits SMD→curvature to ~0.3/mm, far too weak for omega.
    // Real omega turns involve extreme body wall contraction (curvature ~10-15/mm).
    // Solution: direct curvature_bias_ bypass (same principle as weathervane bypass).
    // SMD injection kept for biological authenticity; curvature_bias_ does the actual turn.
    body_.set_omega_mode(true);

    // Direct curvature bias: ±8.0/mm during omega → ~150° turn in 500ms
    // omega_direction: -1.0 = LEFT (SMDD/dorsal), +1.0 = RIGHT (SMDV/ventral)
    // curvature_bias > 0 → positive curvature → LEFT turn (matches omega_direction=-1.0)
    body_.set_curvature_bias(-omega_direction_ * 8.0);
    // NOTE: No SMD current injection. The 200pA injection drove SMD to ±100mV,
    // destroying the half-center oscillator (222mV amplitude vs normal 110mV).
    // curvature_bias bypass handles the actual heading change directly.
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
        serotonin.release_threshold = 0.3;

        // Source neurons: NSM (pharyngeal, food detection) + ADF (pathogen learning)
        int nsml = connectome_.get_neuron_id("NSML");
        int nsmr = connectome_.get_neuron_id("NSMR");
        if (nsml >= 0) serotonin.source_neuron_ids.push_back(nsml);
        if (nsmr >= 0) serotonin.source_neuron_ids.push_back(nsmr);
        // Step 26: ADF as 5-HT source (activated by sickness/pathogen exposure)
        // REF: Zhang 2005 Nature — ADF TPH-1 upregulated during PA14 infection
        for (int adf_id : adf_ids_) {
            if (adf_id >= 0) serotonin.source_neuron_ids.push_back(adf_id);
        }

        // Target: AIY via MOD-1 (inhibitory Cl- channel)
        // 5-HT → MOD-1 on AIY → hyperpolarize → reduce forward drive
        // REF: Flavell 2013 — NSM 5-HT inhibits AIY
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) serotonin.targets.push_back(
            {aiyl, "MOD-1", ModulationEffect::EXCITABILITY, -5.0}); // -5 pA inhibitory
        if (aiyr >= 0) serotonin.targets.push_back(
            {aiyr, "MOD-1", ModulationEffect::EXCITABILITY, -5.0});

        // Step 25: Target: AIB inhibition (suppress avoidance while feeding)
        // REF: Summers 2015 JNeurosci — 5-HT via MOD-1 (5-HT-gated Cl⁻) inhibits AIB
        // On food: 5-HT↑ → AIB↓ → animals continue forward despite repellent
        // Off food: 5-HT↓ → AIB active → full avoidance response
        for (int aib_id : aib_ids_) {
            if (aib_id >= 0) serotonin.targets.push_back(
                {aib_id, "MOD-1", ModulationEffect::EXCITABILITY, -6.0}); // -6 pA inhibitory
        }

        // Target: speed reduction (enhanced slowing on food)
        // REF: Sawin 2000 — serotonin reduces locomotion speed
        serotonin.targets.push_back(
            {-1, "SER-7", ModulationEffect::SPEED_SCALE, -0.15}); // 15% slower at max

        // Target: RIC inhibition (cross-inhibit OA source during dwelling)
        // 5-HT → SER-4 on RIC → inhibit → no OA during active dwelling
        // REF: Chase & Koelle 2007 — 5-HT/OA antagonism
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) serotonin.targets.push_back(
            {ricl, "SER-4", ModulationEffect::EXCITABILITY, -8.0}); // -8 pA inhibitory
        if (ricr >= 0) serotonin.targets.push_back(
            {ricr, "SER-4", ModulationEffect::EXCITABILITY, -8.0});

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

        // Target: global speed reduction (basal slowing response)
        // REF: Sawin 2000 — cat-2 mutants (no DA) fail to slow on food
        dopamine.targets.push_back(
            {-1, "DOP-3", ModulationEffect::SPEED_SCALE, -0.25}); // 25% slower at max

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
            {-1, "SER-3", ModulationEffect::SPEED_SCALE, 0.30}); // +30% speed at max

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

    LOG_INFO("Neuromodulation setup: 5-HT (dwelling), DA (basal slowing), OA (roaming)");
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

    // --- Effect 1: Satiety reduces NSM gain ---
    // High satiety → insulin → NSM less responsive to food
    // Implemented: inject hyperpolarizing current into NSM proportional to satiety
    // At satiety=1.0: -15 pA → NSM release drops below threshold → 5-HT decays
    int n = static_cast<int>(neurons_.size());
    double nsm_suppression = -15.0 * satiety_;  // pA, inhibitory
    int nsml = connectome_.get_neuron_id("NSML");
    int nsmr = connectome_.get_neuron_id("NSMR");
    if (nsml >= 0 && nsml < n) neurons_[nsml]->add_synaptic_current(nsm_suppression);
    if (nsmr >= 0 && nsmr < n) neurons_[nsmr]->add_synaptic_current(nsm_suppression);

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
    if (on_food > food_memory_) {
        // Rising: fast phosphorylation (on food)
        food_memory_ += (on_food - food_memory_) * dt_ / food_memory_tau_rise_;
    } else {
        // Decaying: slow dephosphorylation (off food)
        food_memory_ -= food_memory_ * dt_ / food_memory_tau_decay_;
    }
    if (food_memory_ < 0.0) food_memory_ = 0.0;
    if (food_memory_ > 1.0) food_memory_ = 1.0;

    // Effect: food_memory → excite AVA (via enhanced GLR-1)
    // High food_memory → AVA more excitable → more reversals → LOCAL SEARCH
    // This keeps the worm near the food patch after leaving
    int n = static_cast<int>(neurons_.size());
    int aval = connectome_.get_neuron_id("AVAL");
    if (aval >= 0 && aval < n) {
        // Scale: 0→0 pA at no memory, up to +2.5 pA at full memory
        // 2.5 pA gently biases AVA toward reversal without constant triggering
        double ars_current = 2.5 * food_memory_;
        neurons_[aval]->add_synaptic_current(ars_current);
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
    double no_signal_factor = std::exp(-grad_mag / 0.002);

    // Excite AVA: more reversal when no gradient signal
    // 1.0 pA max — very gentle; avoid disrupting weathervane approach
    int n = static_cast<int>(neurons_.size());
    int aval = connectome_.get_neuron_id("AVAL");
    int avar = connectome_.get_neuron_id("AVAR");
    double kk_current = 1.0 * no_signal_factor;
    if (aval >= 0 && aval < n) neurons_[aval]->add_synaptic_current(kk_current);
    if (avar >= 0 && avar < n) neurons_[avar]->add_synaptic_current(kk_current);
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

    for (auto& syn : synapses) {
        int pre = syn.pre_id();
        int post = syn.post_id();
        if (pre < 0 || pre >= n || post < 0 || post >= n) continue;

        const std::string& pre_name = ninfos[pre].name;

        // Only modulate ASER output synapses (salt learning locus)
        if (pre_name != "ASERL" && pre_name != "ASERR" &&
            pre_name != "ASER" && pre_name.compare(0, 4, "ASER") != 0) continue;

        // Pre and post activity (sigmoid release)
        double V_pre = neurons_[pre]->get_membrane_potential();
        double V_post = neurons_[post]->get_membrane_potential();
        double S_pre = 1.0 / (1.0 + std::exp(-(V_pre - (-35.0)) / 5.0));
        double S_post = 1.0 / (1.0 + std::exp(-(V_post - (-35.0)) / 5.0));

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

    for (auto& syn : synapses) {
        int pre = syn.pre_id();
        int post = syn.post_id();
        if (pre < 0 || pre >= n || post < 0 || post >= n) continue;

        const std::string& pre_name = ninfos[pre].name;
        const std::string& post_name = ninfos[post].name;

        // Only modulate AWC output synapses (olfactory learning locus)
        if (pre_name.compare(0, 3, "AWC") != 0) continue;

        // Pre activity (AWC release rate)
        double V_pre = neurons_[pre]->get_membrane_potential();
        double S_pre = 1.0 / (1.0 + std::exp(-(V_pre - (-35.0)) / 5.0));
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
            double s = 1.0 / (1.0 + std::exp(-(v - (-35.0)) / 5.0));
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
            double s = 1.0 / (1.0 + std::exp(-(v - (-35.0)) / 5.0));
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

} // namespace celegans
