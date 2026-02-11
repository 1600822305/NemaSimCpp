// ================================================================
// Internal States — Split from simulation_engine.cpp (Step 50)
//
// Contains: update_satiety, update_food_memory,
//           update_fatigue, apply_sleep_effects
// ================================================================
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include "core/fast_math.h"

namespace celegans {

static bool starts_with(const std::string& s, const char* prefix) {
    return s.compare(0, std::strlen(prefix), prefix) == 0;
}

// ================================================================
// Satiety internal state (Step 20c)
// ================================================================
void SimulationEngine::update_satiety() {
    double food_conc = environment_.sample_food_density(body_.get_head_position());
    double on_food = food_conc * food_conc / (food_conc * food_conc + 0.09);

    double depletion_rate = (on_food < 0.3) ? 1.0 : 0.5;
    satiety_ -= satiety_ * dt_ * depletion_rate / satiety_tau_deplete_;
    if (satiety_ < 0.0) satiety_ = 0.0;
    if (satiety_ > 1.0) satiety_ = 1.0;

    int n = static_cast<int>(neurons_.size());

    // Effect 2: Satiety excites RIC
    double ric_baseline = 5.0;
    double ric_satiety = 10.0 * satiety_;
    for (int rid : ric_ids_) {
        if (rid >= 0 && rid < n) {
            neurons_[rid]->add_synaptic_current(ric_baseline + ric_satiety);
        }
    }

    // Effect 3: Satiety suppresses chemotaxis (ASE/AWC)
    if (satiety_ > 0.3) {
        double suppress = -5.0 * (satiety_ - 0.3) / 0.7;
        const auto& ninfos = connectome_.neuron_infos();
        for (size_t i = 0; i < chemo_mappings_.size(); ++i) {
            int nid = chemo_mappings_[i].neuron_id;
            if (nid < 0 || nid >= n) continue;
            if (starts_with(ninfos[nid].name, "ASE") || starts_with(ninfos[nid].name, "AWC")) {
                neurons_[nid]->add_synaptic_current(suppress);
            }
        }
    }
}

// ================================================================
// Area-Restricted Search (Step 20d)
// ================================================================
void SimulationEngine::update_food_memory() {
    double food_conc = environment_.sample_food_density(body_.get_head_position());
    double on_food = food_conc / (food_conc + 0.1);

    double effective_decay_tau = food_memory_tau_decay_;
    if (sickness_ > 0.3) {
        effective_decay_tau = 5000.0;
    }

    if (on_food > food_memory_ && sickness_ < 0.3) {
        food_memory_ += (on_food - food_memory_) * dt_ / food_memory_tau_rise_;
    } else {
        food_memory_ -= food_memory_ * dt_ / effective_decay_tau;
    }
    if (food_memory_ < 0.0) food_memory_ = 0.0;
    if (food_memory_ > 1.0) food_memory_ = 1.0;

    int n = static_cast<int>(neurons_.size());
    if (aval_id_ >= 0 && aval_id_ < n) {
        double ars_current = 1.5 * food_memory_;
        neurons_[aval_id_]->add_synaptic_current(ars_current);
    }
    if (dva_id_ >= 0 && dva_id_ < n) {
        double ars_dva_current = 5.0 * food_memory_;
        neurons_[dva_id_]->add_synaptic_current(ars_dva_current);
    }
}

// ================================================================
// Gradient-Dependent Klinokinesis (Step 21d)
// ================================================================
void SimulationEngine::apply_gradient_klinokinesis() {
    Vector2d head_pos = body_.get_head_position();
    Vector2d grad = environment_.chemical_field().gradient(head_pos);
    double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

    double no_signal_factor = fast_exp(-grad_mag / 0.002);

    int n = static_cast<int>(neurons_.size());
    double kk_current = 1.0 * no_signal_factor;
    if (aval_id_ >= 0 && aval_id_ < n) neurons_[aval_id_]->add_synaptic_current(kk_current);
    if (avar_id_ >= 0 && avar_id_ < n) neurons_[avar_id_]->add_synaptic_current(kk_current);
}

// ================================================================
// Step 27: Sleep / Quiescence (Lethargus)
// ================================================================
void SimulationEngine::update_fatigue() {
    double speed = body_.get_speed();
    double activity = std::min(speed / 0.2, 1.0);

    if (!is_sleeping_) {
        fatigue_ += activity * dt_ / fatigue_tau_rise_;
    } else {
        fatigue_ -= fatigue_ * dt_ / fatigue_tau_decay_;
    }
    if (fatigue_ < 0.0) fatigue_ = 0.0;
    if (fatigue_ > 1.0) fatigue_ = 1.0;

    if (!is_sleeping_ && fatigue_ > fatigue_threshold_) {
        is_sleeping_ = true;
    } else if (is_sleeping_ && fatigue_ < 0.15) {
        is_sleeping_ = false;
    }

    int n = static_cast<int>(neurons_.size());
    if (ris_id_ >= 0 && ris_id_ < n) {
        double fatigue_drive = 40.0 / (1.0 + fast_exp(-12.0 * (fatigue_ - fatigue_threshold_)));
        double sleep_maintenance = is_sleeping_ ? 25.0 : 0.0;
        double ris_drive = 2.0 + fatigue_drive + sleep_maintenance;
        double ris_V = neurons_[ris_id_]->get_membrane_potential();
        double ris_release = 1.0 / (1.0 + fast_exp(-(ris_V - (-35.0)) / 5.0));
        double self_inhibition = -3.0 * ris_release;
        neurons_[ris_id_]->set_external_current(ris_drive + self_inhibition);
    }
}

void SimulationEngine::apply_sleep_effects() {
    int n = static_cast<int>(neurons_.size());
    if (ris_id_ < 0 || ris_id_ >= n) return;

    double ris_V = neurons_[ris_id_]->get_membrane_potential();
    double flp11 = 1.0 / (1.0 + fast_exp(-(ris_V - (-35.0)) / 5.0));

    if (flp11 < 0.1) return;

    double cmd_inhibition = -15.0 * flp11;
    const char* cmd_names[] = {"AVAL", "AVAR", "AVBL", "AVBR"};
    for (auto name : cmd_names) {
        int id = connectome_.get_neuron_id(name);
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(cmd_inhibition);
        }
    }

    double mc_inhibition = -12.0 * flp11;
    for (int id : mc_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(mc_inhibition);
        }
    }

    double head_inhibition = -20.0 * flp11;
    for (int id : head_motor_ids_) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(head_inhibition);
        }
    }

    double motor_inhibition = -30.0 * flp11;
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
