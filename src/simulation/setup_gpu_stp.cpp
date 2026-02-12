// setup_gpu_stp.cpp — Split from simulation_engine.cpp (Step 92)
// Contains: setup_stp_params, setup_gpu_backend, sync_synapses_to_gpu
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <cstring>

namespace celegans {

// ================================================================
// Short-Term Plasticity Parameter Setup (Step 21a/b)
//
// Different circuits need different recovery time constants:
// - Motor CPG (SMD/DD/VD cross-inhibition): fast recovery (300-500ms)
//   to maintain stable oscillation despite continuous activity
// - Touch (ALM/PLM→AVD/AVA): slow recovery (3-5s)
//   enables tap habituation (Rankin 1990, Wicks & Rankin 1997)
// - Sensory (ASE→AIA, AWC→AIB): medium recovery (1-2s)
//   enables sensory adaptation
// - Default: moderate recovery (2s)
//
// REF: Liu 2009 PNAS (C. elegans graded synaptic depression)
//      Tsodyks & Markram 1997 (vesicle depletion model)
// ================================================================
void SimulationEngine::setup_stp_params() {
    const auto& ninfos = connectome_.neuron_infos();
    auto& synapses = connectome_.synapses_mut();
    int n = static_cast<int>(ninfos.size());

    auto starts_with_any = [](const std::string& name, std::initializer_list<const char*> prefixes) {
        for (auto p : prefixes) {
            if (name.compare(0, std::strlen(p), p) == 0) return true;
        }
        return false;
    };

    int cpg_count = 0, touch_count = 0, sensory_count = 0, default_count = 0;

    for (auto& syn : synapses) {
        int pre = syn.pre_id();
        int post = syn.post_id();
        if (pre < 0 || pre >= n || post < 0 || post >= n) continue;

        const std::string& pre_name = ninfos[pre].name;
        const std::string& post_name = ninfos[post].name;

        // Motor CPG synapses: fast recovery to preserve oscillation
        // SMD↔SMD, DD↔VD cross-inhibition, RMD connections
        // n_ss(S=0.5) = 1/(1+0.0003*0.5*400) = 0.94 — CPG stable
        if (starts_with_any(pre_name, {"SMD", "RMD", "DD", "VD"}) &&
            starts_with_any(post_name, {"SMD", "RMD", "DD", "VD"})) {
            //                    tau_rec  alpha_d   tau_f   alpha_f  p0
            syn.set_stp_params(   400.0,   0.0003,   100.0,  0.001,   0.6);
            cpg_count++;
        }
        // Touch circuit: slow recovery for habituation (Step 78 fix)
        // OLD: tau_rec=4000ms → 91% recovery in 10s ISI → no habituation
        // NEW: tau_rec=15000ms → 52% recovery in 10s ISI → observable depletion
        // Biology: Rankin 1990 — tap habituation recovery ~30-60s
        //          Wicks & Rankin 1997 — ISI determines habituation rate
        // ALM/PLM at rest: S≈0.003 → n≈1.0 (full pool)
        // During tap (200ms, S≈0.8): n drops ~15-20% per tap
        // Across 30 taps at 10s ISI: n converges to ~0.55 (45% reduction)
        else if (starts_with_any(pre_name, {"ALM", "PLM", "ASH"}) &&
                 starts_with_any(post_name, {"AVD", "AVA", "AVB", "PVC", "AIB", "RIM"})) {
            syn.set_stp_params( 15000.0,   0.001,    300.0,  0.003,   0.5);
            touch_count++;
        }
        // Sensory → interneuron: medium recovery for adaptation
        // n_ss(S=0.35) = 1/(1+0.0003*0.35*1500) = 0.86 (mild tonic depression)
        // n_ss(S=0.7)  = 1/(1+0.0003*0.7*1500)  = 0.76 (visible adaptation)
        else if (starts_with_any(pre_name, {"ASE", "AWC", "AWA"}) &&
                 starts_with_any(post_name, {"AIA", "AIB", "AIY", "AIZ"})) {
            syn.set_stp_params(  1500.0,   0.0003,   200.0,  0.003,   0.5);
            sensory_count++;
        }
        // Default: moderate parameters
        // n_ss(S=0.3) = 1/(1+0.0003*0.3*2000) = 0.85
        else {
            syn.set_stp_params(  2000.0,   0.0003,   200.0,  0.001,   0.5);
            default_count++;
        }
    }

    LOG_INFO("STP setup: CPG=", cpg_count, " touch=", touch_count,
             " sensory=", sensory_count, " default=", default_count);
}

// ================================================================
// GPU Compute Backend Setup (Step 22)
//
// Initializes OpenCL GPU backend for synaptic current computation.
// Falls back to CPU if no GPU available.
// Converts ChemicalSynapse objects to flat SynapseGPU structs for GPU.
// ================================================================
void SimulationEngine::setup_gpu_backend() {
    // GPU acceleration is only beneficial at scale (>500 synapses).
    // At 72 neurons / ~110 synapses, kernel launch overhead dominates.
    // Auto-enable when synapse count exceeds threshold.
    size_t num_syn = connectome_.num_synapses();
    bool should_use_gpu = (num_syn >= 500);

    if (should_use_gpu && ComputeBackend::opencl_available()) {
        gpu_backend_ = ComputeBackend::create_opencl();
        if (gpu_backend_) {
            use_gpu_ = true;
            auto info = gpu_backend_->device_info();
            LOG_INFO("GPU backend ACTIVE: ", info.name, " (", info.max_compute_units,
                     " CUs, ", num_syn, " synapses)");

            sync_synapses_to_gpu();
            int nn = static_cast<int>(neurons_.size());
            gpu_V_.resize(nn, 0.0f);
            gpu_I_.resize(nn, 0.0f);
            return;
        }
    }

    use_gpu_ = false;
    if (num_syn < 500) {
        LOG_INFO("GPU: skipped (", num_syn, " synapses < 500 threshold, CPU faster)");
    } else {
        LOG_INFO("GPU: not available, using CPU");
    }
}

void SimulationEngine::sync_synapses_to_gpu() {
    if (!gpu_backend_ || !use_gpu_) return;

    const auto& synapses = connectome_.synapses();
    gpu_synapses_.clear();
    gpu_synapses_.reserve(synapses.size());

    for (const auto& syn : synapses) {
        SynapseGPU gs;
        gs.pre_id = syn.pre_id();
        gs.post_id = syn.post_id();
        gs.weight = static_cast<float>(syn.weight());
        gs.g_max = 0.5f;       // default g_max
        gs.E_syn = static_cast<float>(syn.reversal_potential());
        gs.V_thresh = -35.0f;  // default
        gs.V_slope = 5.0f;     // default
        gs.weight_mod = static_cast<float>(syn.weight_mod());
        gs.vesicle_pool = static_cast<float>(syn.vesicle_pool());
        gs.release_prob = static_cast<float>(syn.release_prob());
        gs.p0 = 0.5f;          // default baseline release probability
        gs.tau_recovery = 2000.0f;
        gs.alpha_d = 0.0003f;
        gs.tau_facil = 200.0f;
        gs.alpha_f = 0.001f;
        gpu_synapses_.push_back(gs);
    }

    gpu_backend_->upload_synapses(gpu_synapses_);
    LOG_INFO("GPU: uploaded ", gpu_synapses_.size(), " synapses");
}

} // namespace celegans
