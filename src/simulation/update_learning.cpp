// ================================================================
// Learning Systems — Split from simulation_engine.cpp (Step 50)
//
// Contains: update_salt_learning, update_sickness, update_pathogen_learning,
//           apply_synaptic_forgetting (Step 62)
// ================================================================
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include "core/fast_math.h"
#include <cmath>

namespace celegans {

// ================================================================
// Salt Chemotaxis Learning (Step 21c)
// ================================================================
void SimulationEngine::update_salt_learning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();
    const auto& ninfos = connectome_.neuron_infos();

    double learn_signal = satiety_ - 0.5;
    // Step 62: Sleep boosts learning rate (Chouhan 2023 Cell)
    double sleep_factor = is_sleeping_ ? sleep_learn_boost_ : 1.0;
    double lr = 0.0001 * sleep_factor;

    for (size_t idx : aser_syn_indices_) {
        auto& syn = synapses[idx];
        int pre = syn.pre_id();
        int post = syn.post_id();

        double V_pre = neurons_[pre]->get_membrane_potential();
        double V_post = neurons_[post]->get_membrane_potential();
        double S_pre = 1.0 / (1.0 + fast_exp(-(V_pre - (-35.0)) / 5.0));
        double S_post = 1.0 / (1.0 + fast_exp(-(V_post - (-35.0)) / 5.0));

        double dw = lr * learn_signal * S_pre * S_post;
        syn.adjust_weight_mod(dw);
    }
}

// ================================================================
// Step 26: Learned Pathogen Avoidance (Zhang 2005 Nature)
// ================================================================
void SimulationEngine::update_sickness() {
    Vector2d head_pos = body_.get_head_position();
    double food_here = environment_.sample_food_density(head_pos);
    double toxin_here = environment_.sample_repellent(head_pos);

    bool eating_toxic = (food_here > 0.1 && toxin_here > 0.1 && pharynx_.pump_rate_hz() > 0.5);

    if (eating_toxic) {
        double toxicity = toxin_here / (toxin_here + 0.3);
        double d_sick = toxicity * dt_ / sickness_tau_rise_;
        sickness_ += d_sick;
        if (sickness_ > 1.0) sickness_ = 1.0;
        // Step 62: Learning generates sleep pressure (Chouhan 2023 Cell)
        // Aversive experience → ALA-dependent sleep induction
        learning_sleep_drive_ += toxicity * dt_ / 60000.0; // accumulates over ~60s
        if (learning_sleep_drive_ > 0.5) learning_sleep_drive_ = 0.5;
    } else {
        // Step 62: Sleep protects sickness memory (consolidation)
        double decay_mult = is_sleeping_ ? sleep_sickness_protect_ : 1.0;
        double d_decay = sickness_ * dt_ / sickness_tau_decay_ * decay_mult;
        sickness_ -= d_decay;
        if (sickness_ < 0.0) sickness_ = 0.0;
    }
    // Decay learning-induced sleep drive
    learning_sleep_drive_ -= learning_sleep_drive_ * dt_ / learning_sleep_tau_;
    if (learning_sleep_drive_ < 0.0) learning_sleep_drive_ = 0.0;
}

void SimulationEngine::update_pathogen_learning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;
    if (sickness_ < 0.05) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();
    const auto& ninfos = connectome_.neuron_infos();

    // Step 62: Sleep boosts pathogen learning rate (consolidation)
    double sleep_factor = is_sleeping_ ? sleep_learn_boost_ : 1.0;
    double lr = 0.003 * sickness_ * sleep_factor;

    for (size_t idx : awc_syn_indices_) {
        auto& syn = synapses[idx];
        int pre = syn.pre_id();
        int post = syn.post_id();
        const std::string& post_name = ninfos[post].name;

        double V_pre = neurons_[pre]->get_membrane_potential();
        double S_pre = 1.0 / (1.0 + fast_exp(-(V_pre - (-35.0)) / 5.0));
        if (S_pre < 0.05) continue;

        if (post_name.compare(0, 3, "AIY") == 0) {
            syn.adjust_weight_mod(-lr * S_pre);
        }
        if (post_name.compare(0, 3, "AIB") == 0) {
            syn.adjust_weight_mod(+lr * S_pre);
        }
    }

    update_awc_pref_cache();
}

// ================================================================
// Step 62: Synaptic forgetting — slow w_mod drift toward 1.0
// ================================================================
void SimulationEngine::apply_synaptic_forgetting() {
    // Only update every 500ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 1000 != 0) return;

    auto& synapses = connectome_.synapses_mut();

    // Step 62: Sleep suppresses forgetting (Chouhan 2023 Cell)
    // "Sleep is required to consolidate odor memory"
    // During sleep: forgetting rate ×0.3 → learned weights preserved
    // During wake: forgetting rate ×1.0 → weights slowly return to 1.0
    double forget_mult = is_sleeping_ ? sleep_forget_suppress_ : 1.0;
    double effective_rate = w_mod_forget_rate_ * forget_mult * 500.0; // ×500ms interval

    // Apply forgetting to AWC synapses (pathogen learning)
    for (size_t idx : awc_syn_indices_) {
        auto& syn = synapses[idx];
        double wm = syn.weight_mod();
        if (std::abs(wm - 1.0) < 0.001) continue; // skip if near baseline
        double drift = (1.0 - wm) * effective_rate;
        syn.set_weight_mod(wm + drift);
    }

    // Apply forgetting to ASER synapses (salt learning)
    for (size_t idx : aser_syn_indices_) {
        auto& syn = synapses[idx];
        double wm = syn.weight_mod();
        if (std::abs(wm - 1.0) < 0.001) continue;
        double drift = (1.0 - wm) * effective_rate;
        syn.set_weight_mod(wm + drift);
    }

    update_awc_pref_cache();
}

} // namespace celegans
