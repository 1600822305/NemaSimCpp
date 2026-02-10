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
                    double slow_tau = 5000.0)     // ms, slow adaptation (seconds)
        : type_(type), gain_(gain), baseline_(baseline),
          fast_tau_(fast_tau), slow_tau_(slow_tau) {}

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
            ? response / (response + 0.1)  // half-max at C=0.1
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
//      Luo 2014 PNAS — bidirectional thermotaxis via AFD
//      eLife 2021 Hawk — starvation disrupts thermotaxis via AWC-AIA (not AFD)
//
// Key biology:
//   - AFD fires when T > Tc (warming response threshold)
//   - Tc slowly adapts to ambient temperature (tau ~minutes)
//   - Fed worms → positive thermotaxis toward Tc (via AIY activation)
//   - Starved worms → thermotaxis disrupted (AWC→AIA pathway, already in model)
//   - AFD response is INDEPENDENT of feeding state (eLife 2021)
class ThermoTransducer {
public:
    ThermoTransducer(double gain = 60.0,          // pA per °C above Tc
                     double baseline = 5.0,        // pA spontaneous activity
                     double tc_adapt_tau = 120000.0, // ms (~2 min, cultivation memory)
                     double fast_tau = 200.0)       // ms, fast temperature tracker
        : gain_(gain), baseline_(baseline),
          tc_adapt_tau_(tc_adapt_tau), fast_tau_(fast_tau) {}

    // Update with current temperature, returns input current (pA)
    // AFD responds when T rises above Tc (warming detection)
    double update(double temperature, double dt) {
        // Fast tracker: follows temperature with ~200ms lag
        fast_ += (temperature - fast_) * dt / fast_tau_;

        // Cultivation temperature memory: very slow adaptation (minutes)
        // Tc tracks ambient temperature over long timescales
        tc_ += (temperature - tc_) * dt / tc_adapt_tau_;

        // Temperature deviation from Tc
        double dT = fast_ - tc_;

        // AFD warming response: sigmoidal activation above Tc
        // REF: Clark 2006 — AFD Ca²⁺ response threshold at Tc
        // Positive dT → strong response, negative dT → weak/no response
        double response = dT / (1.0 + std::abs(dT) * 2.0);

        // Total current = baseline + gain × response
        double I_out = baseline_ + gain_ * response;
        if (I_out < 0.0) I_out = 0.0;
        if (I_out > 50.0) I_out = 50.0;

        return I_out;
    }

    void reset(double initial_temperature) {
        fast_ = initial_temperature;
        tc_ = initial_temperature;
    }

    double cultivation_temp() const { return tc_; }
    void set_cultivation_temp(double tc) { tc_ = tc; }

private:
    double gain_;
    double baseline_;
    double tc_adapt_tau_;   // cultivation temperature adaptation tau (ms)
    double fast_tau_;        // fast temperature tracker tau (ms)

    double fast_ = 20.0;    // fast-tracking temperature filter
    double tc_ = 20.0;      // cultivation temperature memory (Tc)
};

} // namespace celegans
