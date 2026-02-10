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
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 100.0, 5.0)});
        } else if (starts_with(info.name, "ASER")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 100.0, 5.0)});
        } else if (starts_with(info.name, "AWC")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::OFF, 80.0, 5.0)});
        } else if (starts_with(info.name, "AWA")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 80.0, 5.0)});
        } else if (starts_with(info.name, "ASH")) {
            chemo_mappings_.push_back({info.id, ChemoTransducer(ChemoTransducer::ResponseType::ON, 60.0, 3.0)});
        } else if (!starts_with(info.name, "ALM") && !starts_with(info.name, "PLM")) {
            // Non-touch sensory neurons: low baseline
            other_sensory_ids_.push_back(info.id);
            // ALM/PLM excluded: zero baseline, only activated by wall collision
        }
    }

    // 8. Collect head motor neuron IDs (SMD/RMD receive tonic from upstream)
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "SMD") || starts_with(info.name, "RMD")) {
            head_motor_ids_.push_back(info.id);
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

    // 10. Collect touch neuron IDs (Step 18)
    for (auto& info : neuron_infos) {
        if (starts_with(info.name, "ALM")) alm_ids_.push_back(info.id);
        if (starts_with(info.name, "PLM")) plm_ids_.push_back(info.id);
    }

    // Initialize transducers with current concentration at head
    double init_conc = environment_.sample_chemical(body_.get_head_position());
    for (auto& cm : chemo_mappings_) {
        cm.transducer.reset(init_conc);
    }

    LOG_INFO("Chemosensory: ", chemo_mappings_.size(), " neurons with gradient transduction");
    LOG_INFO("Other sensory: ", other_sensory_ids_.size(), " neurons, baseline ", sensory_baseline_, " pA");
    LOG_INFO("Head tonic: ", head_motor_ids_.size(), " head motor neurons, ", head_tonic_, " pA");
    LOG_INFO("Proprioceptive MEC: ", proprio_mappings_.size(), " motor neuron stretch mappings");
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
    // Chemosensory: concentration temporal derivative → graded input current
    // Touch: low baseline (no stimulus in current environment)
    // REF: Bargmann 2006, Suzuki 2008
    apply_sensory_input();

    // 2b. Weathervane: gradient ⊥ heading → differential SMD bias
    apply_weathervane();

    // 2c. Touch stimulus: wall collision → ALM/PLM activation (Step 18)
    apply_touch_stimulus();

    // 2d. Omega turn: post-reversal deep ventral bend (Step 18)
    apply_omega_turn();

    // 3. Head motor tonic: upstream interneuron drive (RIA→SMD already in connectome)
    // Small tonic represents the net excitatory input from the head circuit
    apply_head_tonic();

    // 4. Proprioceptive stretch: set MEC channel stretch from body curvature
    // The stretch goes through the ion channel → membrane equation (no current injection)
    // REF: Wen et al. 2012 - proprioceptive coupling within motor neurons
    apply_proprioceptive_stretch();

    // 5. Compute synaptic currents (chemical + electrical)
    connectome_.compute_synaptic_currents(neurons_);

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

    // Chemosensory neurons: sample concentration at head position, compute dC/dt
    Vector2d head_pos = body_.get_head_position();
    double concentration = environment_.sample_chemical(head_pos);

    for (auto& cm : chemo_mappings_) {
        if (cm.neuron_id < 0 || cm.neuron_id >= n) continue;
        double I_sensory = cm.transducer.update(concentration, dt_);
        I_sensory *= static_cast<double>(params.sensory_gain);
        neurons_[cm.neuron_id]->set_external_current(I_sensory);
    }

    // Touch/other sensory: low baseline (no active stimulus)
    for (int id : other_sensory_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->set_external_current(sensory_baseline_);
        }
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
    double bias_current = weathervane_gain * grad_normal;

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

    // Dorsal SMD gets positive bias when gradient is to the left
    apply_bias("SMDDL", bias_current);
    apply_bias("SMDDR", bias_current);
    // Ventral SMD gets negative bias (opposite)
    apply_bias("SMDVL", -bias_current);
    apply_bias("SMDVR", -bias_current);

    // Direct curvature bias: bypass neural dynamics bottleneck
    // Maps gradient normal → head curvature offset (1/mm)
    // REF: Iino & Yoshida 2009 — curving rate 12.7 °/mm × ∇C_⊥
    // At speed 0.2 mm/s, to get 5°/s curving: need κ_bias = (5°/s) / (57.3 × 0.2) ≈ 0.44 /mm
    // With gradient ~0.01, need gain ~44. Use weathervane_gain/10 as curvature gain.
    // curv_gain calibration: at gradient 0.01, need ~0.44 /mm bias for 5°/s at 0.2 mm/s
    // → curv_gain ≈ 44. Use weathervane_gain * 0.15 as scaling factor.
    double curv_gain = static_cast<double>(params.weathervane_gain) * 0.15;
    double curv_bias = curv_gain * grad_normal;
    // Clamp to ±2.0 /mm (physiological limit ~3 /mm)
    if (curv_bias > 2.0) curv_bias = 2.0;
    if (curv_bias < -2.0) curv_bias = -2.0;
    body_.set_curvature_bias(curv_bias);
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

    // Track reversal state for omega turn decision
    // AVA release rate > threshold → reversing
    int ava_l = connectome_.get_neuron_id("AVAL");
    int ava_r = connectome_.get_neuron_id("AVAR");
    double ava_rel = 0.0;
    if (ava_l >= 0 && ava_l < n) ava_rel += neurons_[ava_l]->get_transmitter_release_rate();
    if (ava_r >= 0 && ava_r < n) ava_rel += neurons_[ava_r]->get_transmitter_release_rate();
    ava_rel *= 0.5;

    bool was_reversing = is_reversing_;
    is_reversing_ = (ava_rel > 0.6);  // threshold for "actively reversing"

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
            omega_end_time_ = current_time_ + 500.0;  // 500ms deep bend
            // Mostly ventral (C. elegans omega turns are ventrally biased)
            omega_direction_ = (dist(touch_rng_) < 0.8) ? 1.0 : -1.0;
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
        body_.set_curvature_bias(0.0);
        body_.set_omega_mode(false);
        return;
    }

    // Omega turn: very strong curvature bias → deep bend >140°
    // At speed 0.2 mm/s with omega max_dtheta = 300°/s:
    // 300°/s × 0.5s = 150° heading change ✓
    body_.set_omega_mode(true);
    double omega_curv = 8.0 * omega_direction_;
    body_.set_curvature_bias(omega_curv);
}

} // namespace celegans
