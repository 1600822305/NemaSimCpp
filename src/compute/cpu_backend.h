#pragma once

#include "compute/compute_backend.h"
#include <cmath>
#include <algorithm>

namespace celegans {

class CpuBackend : public ComputeBackend {
public:
    bool initialize() override { return true; }

    ComputeDeviceInfo device_info() const override {
        ComputeDeviceInfo info;
        info.name = "CPU (reference)";
        info.vendor = "Host";
        info.version = "C++20";
        info.global_mem_bytes = 0;
        info.local_mem_bytes = 0;
        info.max_compute_units = 1;
        info.max_work_group_size = 1;
        info.is_gpu = false;
        return info;
    }

    std::string backend_name() const override { return "CPU"; }
    bool is_gpu_backend() const override { return false; }

    void upload_synapses(const std::vector<SynapseGPU>& synapses) override {
        synapses_ = synapses;
    }

    void compute_synaptic_currents(
        const std::vector<float>& V_neurons,
        std::vector<float>& I_out,
        float dt,
        float synapse_scale,
        int num_neurons) override
    {
        // Zero output
        std::fill(I_out.begin(), I_out.end(), 0.0f);

        for (auto& syn : synapses_) {
            int pre = syn.pre_id;
            int post = syn.post_id;
            if (pre < 0 || pre >= num_neurons || post < 0 || post >= num_neurons) continue;

            float V_pre = V_neurons[pre];
            float V_post = V_neurons[post];

            // Graded transmitter release sigmoid
            float S = 1.0f / (1.0f + std::exp(-(V_pre - syn.V_thresh) / syn.V_slope));

            // STD: vesicle pool dynamics
            float dn = (1.0f - syn.vesicle_pool) / syn.tau_recovery - syn.alpha_d * S * syn.vesicle_pool;
            syn.vesicle_pool += dn * dt;
            syn.vesicle_pool = std::clamp(syn.vesicle_pool, 0.01f, 1.0f);

            // STF: release probability dynamics
            float dp = (syn.p0 - syn.release_prob) / syn.tau_facil + syn.alpha_f * S * (1.0f - syn.release_prob);
            syn.release_prob += dp * dt;
            syn.release_prob = std::clamp(syn.release_prob, syn.p0 * 0.1f, 1.0f);

            // Effective synaptic strength
            float stp_factor = syn.vesicle_pool * (syn.release_prob / syn.p0);

            // Synaptic current
            float I = -syn.weight * syn.weight_mod * syn.g_max * stp_factor * S * (V_post - syn.E_syn);
            I *= synapse_scale;

            I_out[post] += I;
        }
    }

    void download_stp_state(std::vector<SynapseGPU>& synapses) override {
        synapses = synapses_;
    }

private:
    std::vector<SynapseGPU> synapses_;
};

} // namespace celegans
