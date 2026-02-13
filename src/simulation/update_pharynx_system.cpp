// ================================================================
// Pharyngeal System — Split from simulation_engine.cpp (Step 50)
//
// Contains: apply_pharyngeal_modulation, update_pharynx
// ================================================================
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include "core/fast_math.h"

namespace celegans {

// ================================================================
// Step 24: Pharyngeal Pumping System
// ================================================================
void SimulationEngine::apply_pharyngeal_modulation() {
    int n = static_cast<int>(neurons_.size());

    double sht_conc = neuromod_.get_concentration("5-HT");
    double oa_conc = neuromod_.get_concentration("OA");

    double mc_5ht_current = 15.0 * sht_conc;
    double mc_oa_current = -10.0 * oa_conc;

    // Step 63: Sickness → MC suppression (anorexia)
    // Sick worms reduce pharyngeal pumping → less food intake → lower satiety
    // REF: You 2008 — pathogen exposure reduces feeding rate
    //      Melo & Ruvkun 2012 — sickness behavior includes feeding suppression
    double mc_sickness = sickness_mc_suppress_ * sickness_;  // 0 to -20 pA

    for (int id : nids("MC")) {
        if (id >= 0 && id < n) {
            double food_conc = cached_food_at_head_;
            double food_drive = 8.0 * food_conc / (food_conc + 0.1);
            double mc_tonic = 3.0 + food_drive + mc_5ht_current + mc_oa_current + mc_sickness;
            neurons_[id]->add_synaptic_current(mc_tonic);
        }
    }

    double m3_drive = 0.0;
    if (pharynx_.phase() == PharyngealPump::Phase::PLATEAU) {
        m3_drive = 12.0;
    } else if (pharynx_.phase() == PharyngealPump::Phase::EXCITATION) {
        m3_drive = 5.0;
    }
    for (int id : nids("M3")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(2.0 + m3_drive);
        }
    }

    if (nid("M4") >= 0 && nid("M4") < n) {
        double m4_5ht = 8.0 * sht_conc;
        neurons_[nid("M4")]->add_synaptic_current(2.0 + m4_5ht);
    }

    for (int id : nids("I1")) {
        if (id >= 0 && id < n) {
            neurons_[id]->add_synaptic_current(1.0);
        }
    }
}

void SimulationEngine::update_pharynx() {
    int n = static_cast<int>(neurons_.size());

    double mc_release = 0.0;
    int mc_count = 0;
    for (int id : nids("MC")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            double s = 1.0 / (1.0 + fast_exp(-(v - (-35.0)) / 5.0));
            mc_release += s;
            mc_count++;
        }
    }
    if (mc_count > 0) mc_release /= mc_count;

    double m3_release = 0.0;
    int m3_count = 0;
    for (int id : nids("M3")) {
        if (id >= 0 && id < n) {
            double v = neurons_[id]->get_membrane_potential();
            double s = 1.0 / (1.0 + fast_exp(-(v - (-35.0)) / 5.0));
            m3_release += s;
            m3_count++;
        }
    }
    if (m3_count > 0) m3_release /= m3_count;

    bool pump_event = pharynx_.update(mc_release, m3_release, dt_);

    if (pump_event) {
        double food_conc = cached_food_at_head_;
        double food_ingested = pharynx_.compute_food_intake(food_conc, true);
        satiety_ += food_ingested;
        if (satiety_ > 1.0) satiety_ = 1.0;
    }
}

} // namespace celegans
