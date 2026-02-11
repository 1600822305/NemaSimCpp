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
    for (int rid : nids("RIC")) {
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
    if (nid("AVAL") >= 0 && nid("AVAL") < n) {
        double ars_current = 1.5 * food_memory_;
        neurons_[nid("AVAL")]->add_synaptic_current(ars_current);
    }
    if (nid("DVA") >= 0 && nid("DVA") < n) {
        double ars_dva_current = 5.0 * food_memory_;
        neurons_[nid("DVA")]->add_synaptic_current(ars_dva_current);
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
    if (nid("AVAL") >= 0 && nid("AVAL") < n) neurons_[nid("AVAL")]->add_synaptic_current(kk_current);
    if (nid("AVAR") >= 0 && nid("AVAR") < n) neurons_[nid("AVAR")]->add_synaptic_current(kk_current);
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
    if (nid("RIS") >= 0 && nid("RIS") < n) {
        double fatigue_drive = 40.0 / (1.0 + fast_exp(-12.0 * (fatigue_ - fatigue_threshold_)));
        double sleep_maintenance = is_sleeping_ ? 25.0 : 0.0;
        double ris_drive = 2.0 + fatigue_drive + sleep_maintenance;
        double ris_V = neurons_[nid("RIS")]->get_membrane_potential();
        double ris_release = 1.0 / (1.0 + fast_exp(-(ris_V - (-35.0)) / 5.0));
        double self_inhibition = -3.0 * ris_release;
        neurons_[nid("RIS")]->set_external_current(ris_drive + self_inhibition);
    }
}

void SimulationEngine::apply_sleep_effects() {
    int n = static_cast<int>(neurons_.size());
    if (nid("RIS") < 0 || nid("RIS") >= n) return;

    double ris_V = neurons_[nid("RIS")]->get_membrane_potential();
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
    for (int id : nids("MC")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(mc_inhibition);
        }
    }

    double head_inhibition = -20.0 * flp11;
    for (int id : nids("head_motor")) {
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

// ================================================================
// Step 56: Defecation Motor Program (DMP)
// ================================================================
// Intestinal Ca²⁺ oscillator (IP3/ITR-1) → ~45s rhythm → AVL/DVB activation
// Three motor steps executed sequentially:
//   pBoc (0-1s):   posterior body contraction — non-neural (Ca²⁺ wave direct)
//   aBoc (1.5-2.5s): anterior body contraction — requires AVL (non-GABA)
//   Exp  (2.5-3.5s): enteric muscle contraction — AVL+DVB GABA → EXP-1
// Modulation: off food → no DMP; sleep → suppressed; 5-HT → slightly longer period
// Touch/reversal resets timer (Thomas 1990, Liu & Thomas 1994)
// REF: Thomas 1990 Genetics, Dal Santo 1999, Jiang 2022 Nat Commun
void SimulationEngine::update_defecation() {
    int n = static_cast<int>(neurons_.size());
    int avl_id = nid("AVL");
    int dvb_id = nid("DVB");
    if (avl_id < 0 || dvb_id < 0) return;

    // DMP only active on food (intestinal Ca²⁺ oscillator requires feeding)
    // REF: Liu & Thomas 1994 — off lawn, DMP not expressed
    Vector2d head_pos = body_.get_head_position();
    double food = environment_.sample_food_density(head_pos);
    bool on_food = (food > 0.2);

    // During sleep: suppress DMP (RIS global inhibition already handles AVL)
    if (is_sleeping_) {
        // Timer still advances slowly (biological: clock maintains phase)
        dmp_timer_ += dt_ * 0.3;  // 30% rate during sleep
        return;
    }

    // NOTE: biological touch reset (ALM/PLM gentle touch → timer=0) not modeled here
    // Our reversals are chemotaxis/nociceptive-driven, not touch-specific
    // REF: Thomas 1990 — gentle touch resets defecation phase (separate from reversal)

    // Advance intestinal pacemaker timer (autonomous oscillator, always runs)
    // REF: Liu & Thomas 1994 — clock phase maintained even off food
    dmp_timer_ += dt_;

    // 5-HT modulation: higher serotonin → slightly longer period
    // REF: Ségalat 1995 — exogenous 5-HT inhibits EMCs
    double sht = neuromod_.get_concentration("5-HT");
    double effective_period = dmp_period_ * (1.0 + 0.15 * sht);  // up to ~15% longer

    // Trigger new DMP cycle when timer exceeds period (only on food)
    // Off food: timer resets but motor program not expressed (Liu & Thomas 1994)
    if (dmp_timer_ >= effective_period) {
        dmp_timer_ = 0.0;
        if (!dmp_active_ && on_food) {
            dmp_active_ = true;
            dmp_phase_timer_ = 0.0;
        }
    }

    // Execute 3-phase DMP motor program
    if (dmp_active_) {
        dmp_phase_timer_ += dt_;

        // Phase 1: pBoc (0–1000ms) — posterior body contraction
        // Non-neural: intestinal Ca²⁺ wave directly contracts posterior body wall
        // Modeled as brief speed reduction (body shortens posteriorly)
        if (dmp_phase_timer_ < 1000.0) {
            // Mild posterior contraction → speed reduction
            dmp_speed_factor_ = 0.6;  // 40% speed reduction
        }
        // Phase 2: aBoc (1500–2500ms) — anterior body contraction
        // Requires AVL (non-redundant, non-GABA mechanism)
        // AVL receives intestinal signal (AEX-5 peptide → AEX-2 GPCR → Gsα)
        else if (dmp_phase_timer_ >= 1500.0 && dmp_phase_timer_ < 2500.0) {
            double aboc_drive = 50.0;  // Strong activation for AP firing
            if (avl_id < n) neurons_[avl_id]->set_external_current(aboc_drive);
            // Mild anterior contraction
            dmp_speed_factor_ = 0.7;
        }
        // Phase 3: Exp/EMC (2500–3500ms) — enteric muscle contraction
        // AVL + DVB fire synchronized GABA APs → EXP-1 on enteric muscles
        // REF: Jiang 2022 — compound APs (UNC-2 Ca²⁺ + EXP-2 K⁺)
        else if (dmp_phase_timer_ >= 2500.0 && dmp_phase_timer_ < 3500.0) {
            double exp_drive = 70.0;  // Maximal drive for AP burst
            if (avl_id < n) neurons_[avl_id]->set_external_current(exp_drive);
            if (dvb_id < n) neurons_[dvb_id]->set_external_current(exp_drive);
            // Brief pause during expulsion
            dmp_speed_factor_ = 0.5;
        }
        // Inter-phase and post-DMP: baseline drive
        else {
            if (avl_id < n) neurons_[avl_id]->set_external_current(1.0);
            if (dvb_id < n) neurons_[dvb_id]->set_external_current(1.0);
            dmp_speed_factor_ = 1.0;
        }

        // DMP complete after 4s
        if (dmp_phase_timer_ >= 4000.0) {
            dmp_active_ = false;
            dmp_phase_timer_ = -1.0;
            dmp_count_++;
            // Reset baseline currents
            if (avl_id < n) neurons_[avl_id]->set_external_current(1.0);
            if (dvb_id < n) neurons_[dvb_id]->set_external_current(1.0);
        }
    } else {
        // Between DMP cycles: low baseline (AVL/DVB are quiet)
        if (avl_id < n) neurons_[avl_id]->set_external_current(1.0);
        if (dvb_id < n) neurons_[dvb_id]->set_external_current(1.0);
        dmp_speed_factor_ = 1.0;
    }
}

} // namespace celegans
