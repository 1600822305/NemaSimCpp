#pragma once

#include "core/types.h"
#include <string>
#include <cmath>

namespace celegans {

// Chemosensory transduction: converts chemical concentration to neural input current
// REF: Bargmann 2006, Suzuki 2008 - C. elegans chemotaxis via temporal derivatives
// ASEL: ON response (excited by concentration increase)
// ASER: OFF response (excited by concentration decrease)
// AWC:  OFF response (excited by odor removal)
// AWA:  ON response (excited by odor addition)
class ChemoTransducer {
public:
    enum class ResponseType { ON, OFF, TONIC };

    ChemoTransducer(ResponseType type = ResponseType::ON,
                    double gain = 100.0,         // pA per relative change unit
                    double baseline = 5.0,        // pA spontaneous activity
                    double fast_tau = 100.0,      // ms, fast tracker (Step 19: 500→100ms)
                                                  // Must track 2Hz head oscillation for klinotaxis
                                                  // REF: Suzuki 2008 — ASE response ~50-200ms
                    double slow_tau = 5000.0,     // ms, slow adaptation (seconds)
                    double half_max = 0.1)        // TONIC half-max concentration
        : type_(type), gain_(gain), baseline_(baseline),
          fast_tau_(fast_tau), slow_tau_(slow_tau), half_max_(half_max) {}

    // Update with new concentration sample, returns input current (pA)
    // Uses fast-slow dual filter: detects RELATIVE concentration changes
    // REF: Suzuki 2008, Clark 2006 - Weber-Fechner law in C. elegans chemosensation
    double update(double concentration, double dt) {
        // Fast tracker: follows concentration with ~500ms lag
        fast_ += (concentration - fast_) * dt / fast_tau_;
        // Slow adaptation: follows concentration with ~5s lag
        slow_ += (concentration - slow_) * dt / slow_tau_;

        // Relative change signal: (fast - slow) / slow
        // When worm moves toward food: fast > slow → positive signal
        // When worm moves away: fast < slow → negative signal
        double signal = (fast_ - slow_) / (slow_ + 1e-4);

        // ON neurons respond to increases, OFF neurons respond to decreases
        // TONIC neurons respond to absolute concentration level
        double response;
        if (type_ == ResponseType::TONIC) {
            // Tonic: output proportional to absolute concentration
            // REF: NSM detects food via bacterial ingestion (tonic when on food)
            //      CEP detects bacteria mechanically (tonic on food lawn)
            response = fast_;  // use fast-filtered concentration directly
        } else if (type_ == ResponseType::ON) {
            response = signal;
        } else {
            response = -signal;
        }

        // Saturating nonlinearity
        double sat_response = (type_ == ResponseType::TONIC)
            ? response / (response + half_max_)  // half-max configurable
            : response / (1.0 + std::abs(response) * 2.0);

        // Total current = baseline + modulation (clamped)
        double I_out = baseline_ + gain_ * sat_response;
        if (I_out < 0.0) I_out = 0.0;
        if (I_out > 50.0) I_out = 50.0;

        return I_out;
    }

    void reset(double initial_concentration) {
        fast_ = initial_concentration;
        slow_ = initial_concentration;
    }

private:
    ResponseType type_;
    double gain_;
    double baseline_;
    double fast_tau_;
    double slow_tau_;
    double half_max_;

    double fast_ = 0.0;     // fast-tracking filter
    double slow_ = 0.0;     // slow-adapting filter
};

// Mechanosensory transduction: rapid adaptation touch response
// REF: Chalfie 1985 - gentle body touch neurons ALM, PLM
class MechanoTransducer {
public:
    MechanoTransducer(double gain = 40.0,     // pA peak response
                      double adapt_tau = 50.0) // ms, rapid adaptation
        : gain_(gain), adapt_tau_(adapt_tau) {}

    // Apply a touch stimulus (0.0 = no touch, 1.0 = max touch)
    double update(double stimulus, double dt) {
        // Rapid adaptation: respond to ONSET of touch, not sustained pressure
        double delta = stimulus - adapted_state_;
        adapted_state_ += (stimulus - adapted_state_) * dt / adapt_tau_;

        double response = (delta > 0.0) ? delta * gain_ : 0.0;
        return response;
    }

private:
    double gain_;
    double adapt_tau_;
    double adapted_state_ = 0.0;
};

// Thermosensory transduction: AFD temperature sensing with cultivation memory
// REF: Mori & Ohshima 1995 — AFD senses temperature relative to cultivation temperature (Tc)
//      Clark 2006 — AFD responds to warming above Tc, cooling below Tc
//      Luo 2014 PNAS — bidirectional thermotaxis: negative=klinokinesis, positive=turning bias
//      eLife 2021 Hawk — starvation disrupts thermotaxis via AWC-AIA (not AFD)
//
// Key mechanism (analogous to chemotaxis klinokinesis):
//   AFD tracks deviation |T - Tc| with fast/slow dual filters (Weber-Fechner).
//   OFF response to deviation:
//     - Approaching Tc (deviation decreasing) → AFD fires → AIY active → forward runs
//     - Moving away from Tc (deviation increasing) → AFD silent → more pirouettes
//   This is the SAME klinokinesis mechanism as chemotaxis, just on temperature.
//   Satiety modulation: AWC→AIA pathway (already in model, eLife 2021 Hawk).
class ThermoTransducer {
public:
    ThermoTransducer(double gain = 100.0,         // pA per unit signal
                     double baseline = 5.0,        // pA spontaneous (low: avoid AIY over-activation)
                     double tc_adapt_tau = 3600000.0, // ms (~1 hour, Mori 1995)
                     double fast_tau = 200.0)       // ms, fast deviation tracker
        : gain_(gain), baseline_(baseline),
          tc_adapt_tau_(tc_adapt_tau), fast_tau_(fast_tau) {}

    // Update with current temperature, returns input current (pA)
    // Uses dual-filter OFF response on |T - Tc| deviation
    double update(double temperature, double dt) {
        // Track raw temperature for Tc adaptation
        raw_ += (temperature - raw_) * dt / fast_tau_;

        // Cultivation temperature memory: very slow adaptation (hours)
        tc_ += (temperature - tc_) * dt / tc_adapt_tau_;

        // Compute deviation from Tc (always positive)
        double deviation = std::abs(raw_ - tc_);

        // Dual-filter Weber-Fechner on deviation (same as ChemoTransducer)
        dev_fast_ += (deviation - dev_fast_) * dt / 500.0;   // 500ms fast tracker
        dev_slow_ += (deviation - dev_slow_) * dt / 5000.0;  // 5s slow adaptation

        // OFF response: fires when deviation DECREASES (approaching Tc)
        // dev_fast < dev_slow → signal > 0 → AFD active → AIY → forward runs
        // dev_fast > dev_slow → signal < 0 → AFD quiet → more pirouettes
        double signal = -(dev_fast_ - dev_slow_) / (dev_slow_ + 0.5);

        // Saturating nonlinearity + baseline
        double sat_signal = signal / (1.0 + std::abs(signal) * 2.0);
        double I_out = baseline_ + gain_ * sat_signal;
        if (I_out < 0.0) I_out = 0.0;
        if (I_out > 80.0) I_out = 80.0;

        return I_out;
    }

    void reset(double initial_temperature) {
        raw_ = initial_temperature;
        tc_ = initial_temperature;
        dev_fast_ = 0.0;
        dev_slow_ = 0.0;
    }

    double cultivation_temp() const { return tc_; }
    void set_cultivation_temp(double tc) { tc_ = tc; }

private:
    double gain_;
    double baseline_;
    double tc_adapt_tau_;   // cultivation temperature adaptation tau (ms)
    double fast_tau_;        // raw temperature tracker tau (ms)

    double raw_ = 20.0;      // fast-tracking temperature
    double tc_ = 20.0;       // cultivation temperature memory (Tc)
    double dev_fast_ = 0.0;  // fast-tracking |T - Tc| deviation
    double dev_slow_ = 0.0;  // slow-adapting |T - Tc| deviation
};

} // namespace celegans
