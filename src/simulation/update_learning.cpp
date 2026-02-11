// ================================================================
// Learning Systems — Split from simulation_engine.cpp (Step 50)
//
// Contains: update_salt_learning, update_sickness, update_pathogen_learning
// ================================================================
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include "core/fast_math.h"

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
    double lr = 0.0001;

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
    } else {
        double d_decay = sickness_ * dt_ / sickness_tau_decay_;
        sickness_ -= d_decay;
        if (sickness_ < 0.0) sickness_ = 0.0;
    }
}

void SimulationEngine::update_pathogen_learning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;
    if (sickness_ < 0.05) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();
    const auto& ninfos = connectome_.neuron_infos();

    double lr = 0.003 * sickness_;

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

} // namespace celegans
