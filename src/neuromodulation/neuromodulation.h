#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace celegans {

// Forward declarations
class Neuron;
class Connectome;

// ============================================================================
// Neuromodulation Layer (Layer 6) — "Wireless Connectome"
//
// Unlike synapses (point-to-point, fast, ms), neuromodulators act via
// volume transmission (diffuse, slow, seconds-minutes):
//   Source neuron releases modulator → diffuses in extracellular space →
//   binds receptors on distant target neurons → modulates excitability,
//   synaptic gain, or ion channel properties.
//
// REF: Ripoll-Sánchez 2023 Neuron — neuropeptidergic connectome
//      Flavell 2013 Cell — 5-HT/PDF roaming/dwelling states
//      Sawin 2000 — dopamine basal slowing response
//      Chase & Koelle 2007 — monoamine signaling in C. elegans
// ============================================================================

// Types of neuromodulatory effects on target neurons
enum class ModulationEffect {
    EXCITABILITY,    // shift leak reversal or add tonic current (pA)
    SYNAPSE_GAIN,    // multiply outgoing synapse weights
    SPEED_SCALE,     // modify locomotion speed (basal slowing)
    REVERSAL_RATE,   // modify pirouette/reversal probability
};

// A single modulation target: one receptor on one neuron
struct ModulationTarget {
    int neuron_id;           // target neuron
    std::string receptor;    // receptor name (e.g. "MOD-1", "DOP-3")
    ModulationEffect effect; // what the receptor does
    double strength;         // effect magnitude (sign matters: + excite, - inhibit)
};

// A neuromodulator species (e.g. serotonin, dopamine)
struct Neuromodulator {
    std::string name;                  // "5-HT", "DA", "TA", "OA"
    std::vector<int> source_neuron_ids; // neurons that release this modulator
    std::vector<ModulationTarget> targets; // receptor-mediated effects

    // Dynamics
    double concentration = 0.0;   // extracellular concentration [0, 1]
    double tau_rise = 2000.0;     // release time constant (ms) — slow!
    double tau_decay = 5000.0;    // degradation time constant (ms)

    // Release threshold: source neuron release rate must exceed this
    // to contribute to modulator release (prevents tonic baseline release)
    double release_threshold = 0.3;
};

// ============================================================================
// NeuromodulationManager: updates all modulators each timestep
// ============================================================================
class NeuromodulationManager {
public:
    NeuromodulationManager() = default;

    // Add a neuromodulator to the system
    void add_modulator(Neuromodulator mod);

    // Resolve source neuron names to IDs (call after connectome is built)
    void resolve_neuron_ids(const Connectome& connectome);

    // Main update: compute release, decay, and apply effects
    // Called each simulation timestep
    void update(std::vector<std::unique_ptr<Neuron>>& neurons, double dt_ms);

    // Query current concentration of a modulator
    double get_concentration(const std::string& name) const;

    // Get all modulators (for diagnostics/visualization)
    const std::vector<Neuromodulator>& modulators() const { return modulators_; }

    // Accumulated modulation effects per neuron (reset each step)
    // Maps neuron_id → tonic current shift (pA)
    double get_tonic_current(int neuron_id) const;

    // Maps neuron_id → synapse gain multiplier
    double get_synapse_gain(int neuron_id) const;

    // Step 41: Reset all neuromodulator concentrations to 0
    // Call after network warmup to clear initial transients
    void reset_concentrations() {
        for (auto& mod : modulators_) mod.concentration = 0.0;
        speed_scale_ = 1.0;
        reversal_rate_scale_ = 1.0;
    }

    // Global speed modulation from neuromodulators
    double get_speed_scale() const { return speed_scale_; }

    // Global reversal rate modulation
    double get_reversal_rate_scale() const { return reversal_rate_scale_; }

private:
    std::vector<Neuromodulator> modulators_;

    // Per-neuron accumulated effects (cleared each update)
    std::unordered_map<int, double> tonic_currents_;   // neuron_id → pA
    std::unordered_map<int, double> synapse_gains_;    // neuron_id → multiplier

    // Global effects
    double speed_scale_ = 1.0;
    double reversal_rate_scale_ = 1.0;

    // Deferred name→id resolution
    struct DeferredSource {
        int modulator_idx;
        std::string neuron_name;
    };
    struct DeferredTarget {
        int modulator_idx;
        int target_idx;
        std::string neuron_name;
    };
    std::vector<DeferredSource> deferred_sources_;
    std::vector<DeferredTarget> deferred_targets_;
    bool resolved_ = false;
};

} // namespace celegans
