#include "neuromodulation/neuromodulation.h"
#include "neuron/single_compartment.h"
#include "connectome/connectome.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>

namespace celegans {

void NeuromodulationManager::add_modulator(Neuromodulator mod) {
    modulators_.push_back(std::move(mod));
}

void NeuromodulationManager::resolve_neuron_ids(const Connectome& connectome) {
    for (auto& ds : deferred_sources_) {
        int id = connectome.get_neuron_id(ds.neuron_name.c_str());
        if (id >= 0) {
            modulators_[ds.modulator_idx].source_neuron_ids.push_back(id);
        } else {
            LOG_WARN("Neuromodulation: source neuron '", ds.neuron_name, "' not found");
        }
    }
    for (auto& dt : deferred_targets_) {
        int id = connectome.get_neuron_id(dt.neuron_name.c_str());
        if (id >= 0) {
            modulators_[dt.modulator_idx].targets[dt.target_idx].neuron_id = id;
        } else {
            LOG_WARN("Neuromodulation: target neuron '", dt.neuron_name, "' not found");
        }
    }
    deferred_sources_.clear();
    deferred_targets_.clear();
    resolved_ = true;
    LOG_INFO("NeuromodulationManager: resolved ", modulators_.size(), " modulators");
}

void NeuromodulationManager::update(
    std::vector<std::unique_ptr<Neuron>>& neurons, double dt_ms)
{
    int n = static_cast<int>(neurons.size());

    // Clear per-step accumulated effects
    tonic_currents_.clear();
    synapse_gains_.clear();
    speed_scale_ = 1.0;
    reversal_rate_scale_ = 1.0;

    for (auto& mod : modulators_) {
        // --- 1. Compute release from source neurons ---
        // Average release rate of source neurons above threshold
        double total_release = 0.0;
        int active_sources = 0;
        for (int src_id : mod.source_neuron_ids) {
            if (src_id >= 0 && src_id < n) {
                double rel = neurons[src_id]->get_transmitter_release_rate();
                if (rel > mod.release_threshold) {
                    total_release += (rel - mod.release_threshold);
                    active_sources++;
                }
            }
        }
        // Normalize: max release when active sources are fully active
        // Step 40: use active_sources instead of total sources in denominator
        // Prevents inactive sources (e.g. HSN when not egg-laying) from
        // diluting concentration driven by active sources (e.g. NSM on food)
        double max_possible = 1.0 - mod.release_threshold;
        double release_drive = (active_sources > 0 && max_possible > 0)
            ? total_release / (active_sources * max_possible)
            : 0.0;
        if (release_drive > 1.0) release_drive = 1.0;

        // --- 2. Update concentration with first-order dynamics ---
        // Rise toward release_drive, decay toward 0
        // dC/dt = (drive - C) / tau_rise  when drive > C  (release)
        //       = -C / tau_decay           when drive < C  (degradation)
        if (release_drive > mod.concentration) {
            mod.concentration += (release_drive - mod.concentration) * dt_ms / mod.tau_rise;
        } else {
            mod.concentration -= mod.concentration * dt_ms / mod.tau_decay;
        }
        // Clamp
        if (mod.concentration < 0.0) mod.concentration = 0.0;
        if (mod.concentration > 1.0) mod.concentration = 1.0;

        // --- 3. Apply receptor-mediated effects ---
        double conc = mod.concentration;
        if (conc < 0.001) continue;  // skip if negligible

        for (const auto& target : mod.targets) {
            int tid = target.neuron_id;
            double effect = target.strength * conc;

            switch (target.effect) {
            case ModulationEffect::EXCITABILITY:
                // Add tonic current shift to target neuron (requires valid neuron_id)
                if (tid >= 0 && tid < n)
                    tonic_currents_[tid] += effect;
                break;

            case ModulationEffect::SYNAPSE_GAIN:
                // Multiply synapse gain (requires valid neuron_id)
                if (tid >= 0 && tid < n) {
                    if (synapse_gains_.count(tid) == 0)
                        synapse_gains_[tid] = 1.0;
                    synapse_gains_[tid] *= (1.0 + effect);
                }
                break;

            case ModulationEffect::SPEED_SCALE:
                // Step 136: additive accumulation (was multiplicative → compounding collapse)
                // Final speed_scale_ = 1.0 + sum(effects), applied after loop
                speed_scale_ += effect;
                break;

            case ModulationEffect::REVERSAL_RATE:
                // Global reversal rate modulation (neuron_id=-1 for global)
                reversal_rate_scale_ *= (1.0 + effect);
                break;
            }
        }
    }

    // --- 4. Apply tonic currents to neurons ---
    for (auto& [nid, current] : tonic_currents_) {
        if (nid >= 0 && nid < n) {
            neurons[nid]->add_synaptic_current(current);
        }
    }
}

double NeuromodulationManager::get_concentration(const std::string& name) const {
    for (const auto& mod : modulators_) {
        if (mod.name == name) return mod.concentration;
    }
    return 0.0;
}

double NeuromodulationManager::get_tonic_current(int neuron_id) const {
    auto it = tonic_currents_.find(neuron_id);
    return (it != tonic_currents_.end()) ? it->second : 0.0;
}

double NeuromodulationManager::get_synapse_gain(int neuron_id) const {
    auto it = synapse_gains_.find(neuron_id);
    return (it != synapse_gains_.end()) ? it->second : 1.0;
}

} // namespace celegans
