#pragma once

#include <cmath>
#include <algorithm>

namespace celegans {

// ================================================================
// PharyngealPump: Models the pharyngeal muscle action potential cycle
//
// The C. elegans pharynx is a tubular pump that ingests bacteria.
// Pharyngeal muscle generates cardiac-like action potentials:
//   E (excitation): CCA-1 T-type Ca²⁺ → rapid depolarization
//   P (plateau):    EGL-19 L-type Ca²⁺ → sustained contraction
//   R (repolarization): AVR-15 Glu-Cl⁻ (M3) + K⁺ channels → relaxation
//
// Pump cycle: MC fires → E → P → M3 fires (proprioceptive) → R → rest
// Rate: ~4 Hz on food (with 5-HT), ~1 Hz without MC
//
// REF: Avery (WormBook 2012), Raizen & Avery 1994,
//      Steger & Avery 2004, Franks 2002
// ================================================================
class PharyngealPump {
public:
    enum class Phase { RESTING, EXCITATION, PLATEAU, REPOLARIZATION };

    PharyngealPump() = default;

    // Main update: call each simulation step
    // mc_output: MC motor neuron release probability (0-1), triggers pumping
    // m3_output: M3 motor neuron release probability (0-1), triggers relaxation
    // Returns: true if a pump event completed this step
    bool update(double mc_output, double m3_output, double dt_ms) {
        bool pump_completed = false;
        phase_timer_ += dt_ms;

        switch (phase_) {
        case Phase::RESTING:
            // MC fires → triggers action potential
            // Refractory period: muscle cannot fire for refractory_ms_ after last pump
            // MC modulates refractory: higher MC release → shorter refractory → faster rate
            // REF: Avery 2012 — on food ~4 Hz (250ms), off food ~1 Hz (1000ms)
            {
                // MC-modulated refractory: 800ms baseline, down to 200ms with strong MC
                double effective_refractory = refractory_base_ms_ - 
                    (refractory_base_ms_ - refractory_min_ms_) * mc_output;
                if (effective_refractory < refractory_min_ms_) 
                    effective_refractory = refractory_min_ms_;

                if (phase_timer_ >= effective_refractory) {
                    if (mc_output > mc_threshold_) {
                        enter_phase(Phase::EXCITATION);
                    } else if (phase_timer_ > intrinsic_period_ms_) {
                        // Intrinsic muscle pacemaker (no MC needed, slow)
                        enter_phase(Phase::EXCITATION);
                    }
                }
            }
            // Resting potential
            V_muscle_ += (V_rest_ - V_muscle_) * dt_ms / 5.0;  // 5ms tau
            break;

        case Phase::EXCITATION:
            // CCA-1 T-type Ca²⁺ → rapid depolarization to +30 mV
            // Duration: ~5 ms
            V_muscle_ += (V_peak_ - V_muscle_) * dt_ms / 2.0;  // 2ms tau (fast)
            if (phase_timer_ > 5.0) {
                enter_phase(Phase::PLATEAU);
            }
            break;

        case Phase::PLATEAU:
            // EGL-19 L-type Ca²⁺ sustains depolarization
            // Normal duration: ~150 ms
            // M3 fires → shortens plateau (triggers repolarization)
            V_muscle_ += (V_plateau_ - V_muscle_) * dt_ms / 10.0;
            if (m3_output > m3_threshold_ && phase_timer_ > 20.0) {
                // M3-triggered relaxation (normal)
                enter_phase(Phase::REPOLARIZATION);
            } else if (phase_timer_ > max_plateau_ms_) {
                // Max plateau duration reached (safety, no M3)
                enter_phase(Phase::REPOLARIZATION);
            }
            break;

        case Phase::REPOLARIZATION:
            // AVR-15 Glu-Cl⁻ (from M3) + K⁺ channels → rapid repolarization
            // Overshoots to ~-55 mV then rebounds
            V_muscle_ += (V_undershoot_ - V_muscle_) * dt_ms / 3.0;  // 3ms tau
            if (phase_timer_ > 15.0) {
                // One pump cycle complete
                pump_completed = true;
                total_pumps_++;
                enter_phase(Phase::RESTING);
            }
            break;
        }

        // Update pump rate (exponential moving average)
        if (pump_completed) {
            // Instantaneous rate from inter-pump interval
            double interval_s = (current_time_ - last_pump_time_) / 1000.0;
            if (interval_s > 0.01) {
                double inst_rate = 1.0 / interval_s;
                pump_rate_hz_ += (inst_rate - pump_rate_hz_) * 0.3;  // fast tracking
            }
            last_pump_time_ = current_time_;
        } else {
            // Decay pump rate if no pump for a while
            double since_last = (current_time_ - last_pump_time_) / 1000.0;
            if (since_last > 0.5) {  // 500ms without pump → rate decaying
                pump_rate_hz_ *= (1.0 - dt_ms / 2000.0);  // 2s decay tau
            }
        }

        current_time_ += dt_ms;
        return pump_completed;
    }

    // Food ingestion: pump near food → real satiety
    // Returns food_ingested this step (arbitrary units, 0-1 scale)
    double compute_food_intake(double food_concentration, bool pump_event) const {
        if (!pump_event) return 0.0;
        // Each pump ingests bacteria proportional to local concentration
        // Efficiency factor: how much food per pump (~0.001 satiety units per pump at conc=1.0)
        return food_concentration * food_per_pump_;
    }

    // Isthmus peristalsis: M4 drives posterior transport
    // Occurs after approximately every 4th pump
    bool should_peristalse() const {
        return (total_pumps_ % peristalsis_ratio_) == 0 && total_pumps_ > 0;
    }

    // Accessors
    Phase phase() const { return phase_; }
    double V_muscle() const { return V_muscle_; }
    double pump_rate_hz() const { return pump_rate_hz_; }
    int total_pumps() const { return total_pumps_; }
    double food_per_pump() const { return food_per_pump_; }

    // Set intrinsic period (modulated by 5-HT on muscle directly)
    void set_intrinsic_period(double period_ms) { intrinsic_period_ms_ = period_ms; }

    // Parameters
    double mc_threshold_ = 0.3;       // MC release threshold for triggering pump
    double m3_threshold_ = 0.2;       // M3 release threshold for triggering relaxation
    double max_plateau_ms_ = 300.0;   // max plateau without M3 (longer = less efficient)
    double food_per_pump_ = 0.006;    // satiety increment per pump at conc=1.0
    int peristalsis_ratio_ = 4;       // isthmus peristalsis every N pumps
    double refractory_base_ms_ = 800.0;  // base refractory (no MC): ~1.2 Hz
    double refractory_min_ms_ = 200.0;   // min refractory (max MC): ~4 Hz

private:
    void enter_phase(Phase p) {
        phase_ = p;
        phase_timer_ = 0.0;
    }

    Phase phase_ = Phase::RESTING;
    double phase_timer_ = 0.0;
    double current_time_ = 0.0;

    // Pharyngeal muscle membrane potential
    double V_muscle_ = -45.0;         // mV, current membrane potential
    static constexpr double V_rest_ = -45.0;      // mV, resting
    static constexpr double V_peak_ = 30.0;       // mV, E phase peak
    static constexpr double V_plateau_ = 20.0;    // mV, P phase sustained
    static constexpr double V_undershoot_ = -55.0; // mV, post-R undershoot

    // Intrinsic pacemaker (without MC)
    double intrinsic_timer_ = 0.0;
    double intrinsic_period_ms_ = 1000.0;  // 1 Hz without MC (slow, irregular)

    // Pump counting and rate
    int total_pumps_ = 0;
    double pump_rate_hz_ = 0.0;
    double last_pump_time_ = 0.0;
};

} // namespace celegans
