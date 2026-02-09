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
    enum class ResponseType { ON, OFF };

    ChemoTransducer(ResponseType type = ResponseType::ON,
                    double gain = 100.0,         // pA per relative change unit
                    double baseline = 5.0,        // pA spontaneous activity
                    double fast_tau = 500.0,      // ms, fast concentration tracker
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
        double response;
        if (type_ == ResponseType::ON) {
            response = signal;
        } else {
            response = -signal;
        }

        // Saturating nonlinearity
        double sat_response = response / (1.0 + std::abs(response) * 2.0);

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

} // namespace celegans
