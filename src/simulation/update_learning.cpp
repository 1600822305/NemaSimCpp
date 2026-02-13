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
// Salt Chemotaxis Learning (Step 21c → Step 77 fix)
//
// Biology: Starvation + NaCl pairing → ASER output weakens → salt aversion
// Mechanism: INS-1 from AIA → DAF-2 in ASER → PI3K/AKT pathway
//   → modifies ASER synaptic output (cell-autonomous, NOT Hebbian)
//
// Step 77 fix: removed S_post from learning rule.
// OLD: dw = lr × learn_signal × S_pre × S_post (Hebbian, too slow)
// NEW: dw = lr × learn_signal × S_pre (ASER-autonomous, PI3K pathway)
// Biological justification:
//   - DAF-2 acts directly IN ASER (Tomioka 2006 Neuron)
//   - PI3K cascade modifies ASER output independent of post-synaptic activity
//   - INS-1 release is driven by starvation state (learn_signal), not post activity
//
// Step 77: lr increased 0.0001→0.001 for simulation timescale
// Biology: salt learning takes 15-60min; simulation runs 300s
// Timescale compression: ~20× (60min/300s ≈ 12×, conservatively 10×)
//
// REF: Tomioka 2006 Neuron — DAF-2/AGE-1/AKT-1 in ASER for salt learning
//      Oda 2011 J Neurophysiol — ASER calcium response plasticity
//      Ikeda 2008 Genetics — PI3K + Gq/PKC dual pathway
// ================================================================
void SimulationEngine::update_salt_learning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();

    // learn_signal: negative when hungry (starvation → salt aversion)
    //               positive when well-fed (reinforces salt attraction)
    double learn_signal = satiety_ - 0.5;
    // Step 62: Sleep boosts learning rate (Chouhan 2023 Cell)
    double sleep_factor = is_sleeping_ ? sleep_learn_boost_ : 1.0;
    // Step 77: lr 0.0001→0.001 (10× for simulation timescale compression)
    double lr = 0.001 * sleep_factor;

    for (size_t idx : aser_syn_indices_) {
        auto& syn = synapses[idx];
        int pre = syn.pre_id();

        double V_pre = neurons_[pre]->get_membrane_potential();
        // S_pre: ASER activity — gated by salt sensation
        // When ASER active (salt present) AND hungry → w_mod decreases
        // When ASER active (salt present) AND well-fed → w_mod increases
        double S_pre = 1.0 / (1.0 + fast_exp(-(V_pre - (-35.0)) / 5.0));

        // Step 77: ASER-autonomous rule (no S_post)
        // PI3K pathway: DAF-2 in ASER modifies output gain
        // dw sign: hungry(sat<0.5) → negative → weaken ASER synapses → reduce salt attraction
        double dw = lr * learn_signal * S_pre;
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

// ================================================================
// Step 117: Associative Odor-Food Conditioning
//
// Biology: C. elegans forms bidirectional odor-food associations:
//   Positive: butanone + food → enhanced attraction (Kauffman 2010 PNAS)
//   Negative: benzaldehyde + starvation → learned aversion (Lin 2010 JNeurosci)
//
// Mechanism (cell-autonomous in AWC, Cho 2016 Neuron):
//   INS-1 from ASI/AIA → DAF-2 in AWC → AGE-1 (PI3K) pathway
//   Food present + AWC active → PI3K strengthens AWC→AIY output (w_mod↑)
//   Food absent + AWC active → PI3K weakens AWC→AIY output (w_mod↓)
//   INS-1 concentration modulates learning rate (starvation signal)
//
// Implementation:
//   learn_signal = food_at_head - 0.5 (positive=fed, negative=starved)
//   Δw = lr × S_awc × learn_signal × (1 + ins1_conc) × sleep_factor
//   S_awc: AWC activity (sigmoid of V)
//   INS-1 amplifies both positive and negative conditioning
//   Sleep boosts learning (Chouhan 2023 Cell)
//
// REF: Kauffman 2010 PNAS — positive butanone conditioning
//      Lin 2010 JNeurosci — INS-1/DAF-2 in benzaldehyde-starvation plasticity
//      Cho 2016 Neuron — CREB/CRH-1 in AWC for long-term memory
//      Wen 2012 Neuron — AWC→AIY synapse is site of plasticity
// ================================================================
void SimulationEngine::update_odor_conditioning() {
    // Only update every 100ms (not every 0.5ms step)
    if (static_cast<int>(current_time_ / dt_) % 200 != 0) return;

    int n = static_cast<int>(neurons_.size());
    auto& synapses = connectome_.synapses_mut();

    // Food signal at head position: >0.5 = food present, <0.5 = no food
    Vector2d head_pos = body_.get_head_position();
    double food_here = environment_.sample_food_density(head_pos);
    // Sigmoid food detection threshold (>0.1 = food detected)
    double food_signal = 1.0 / (1.0 + fast_exp(-20.0 * (food_here - 0.1)));

    // learn_signal: positive when food present, negative when absent
    double learn_signal = food_signal - 0.5;

    // INS-1 amplifies conditioning (starvation enhances aversive learning,
    // Lin 2010: ins-1 mutants are defective in benzaldehyde-starvation plasticity)
    double ins1_amp = 1.0 + ins1_conc_ * 2.0;

    // Sleep boosts learning (Chouhan 2023)
    double sleep_factor = is_sleeping_ ? sleep_learn_boost_ : 1.0;
    double lr = odor_cond_lr_ * ins1_amp * sleep_factor;

    // Modify AWC→AIY synapses (the site of plasticity, Wen 2012 Neuron)
    for (size_t idx : awc_aiy_syn_indices_) {
        auto& syn = synapses[idx];
        int pre = syn.pre_id();
        if (pre < 0 || pre >= n) continue;

        double V_pre = neurons_[pre]->get_membrane_potential();
        // S_awc: AWC must be active (odor present) for conditioning to occur
        // This gates learning: no odor → no AWC activity → no plasticity
        double S_awc = 1.0 / (1.0 + fast_exp(-(V_pre - (-35.0)) / 5.0));
        if (S_awc < 0.05) continue; // skip if AWC silent

        // Bidirectional plasticity:
        // food + odor → learn_signal > 0 → w_mod↑ (positive conditioning)
        // no food + odor → learn_signal < 0 → w_mod↓ (negative conditioning)
        double dw = lr * learn_signal * S_awc;
        syn.adjust_weight_mod(dw);
    }

    update_awc_pref_cache();
}

} // namespace celegans
