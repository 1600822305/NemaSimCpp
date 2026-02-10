#pragma once

#include <string>
#include <vector>
#include <memory>

namespace celegans {

// GPU-compatible flat synapse data (no polymorphism)
struct SynapseGPU {
    int pre_id;
    int post_id;
    float weight;
    float g_max;
    float E_syn;
    float V_thresh;
    float V_slope;
    float weight_mod;
    // STP state
    float vesicle_pool;
    float release_prob;
    float p0;
    float tau_recovery;
    float alpha_d;
    float tau_facil;
    float alpha_f;
};

// GPU-compatible flat neuron state
struct NeuronStateGPU {
    float V;           // membrane potential (mV)
    float I_syn;       // total synaptic current (pA)
};

// Device information
struct ComputeDeviceInfo {
    std::string name;
    std::string vendor;
    std::string version;
    size_t global_mem_bytes;
    size_t local_mem_bytes;
    int max_compute_units;
    int max_work_group_size;
    bool is_gpu;
};

// Abstract compute backend
class ComputeBackend {
public:
    virtual ~ComputeBackend() = default;

    // Initialize the backend (select device, compile kernels)
    virtual bool initialize() = 0;

    // Device info
    virtual ComputeDeviceInfo device_info() const = 0;
    virtual std::string backend_name() const = 0;
    virtual bool is_gpu_backend() const = 0;

    // Upload synapse data to device
    virtual void upload_synapses(const std::vector<SynapseGPU>& synapses) = 0;

    // Compute synaptic currents: given neuron voltages, compute I_syn per neuron
    // V_neurons: membrane potentials [N_neurons]
    // I_out:     accumulated synaptic current per neuron [N_neurons] (zeroed then summed)
    // dt:        timestep in ms
    // synapse_scale: runtime multiplier
    virtual void compute_synaptic_currents(
        const std::vector<float>& V_neurons,
        std::vector<float>& I_out,
        float dt,
        float synapse_scale,
        int num_neurons) = 0;

    // Download updated STP state (vesicle_pool, release_prob) back to host
    virtual void download_stp_state(std::vector<SynapseGPU>& synapses) = 0;

    // Factory: create best available backend
    static std::unique_ptr<ComputeBackend> create_best_available();
    static std::unique_ptr<ComputeBackend> create_cpu();
    static std::unique_ptr<ComputeBackend> create_opencl();

    // Query available backends
    static bool opencl_available();
    static std::vector<ComputeDeviceInfo> enumerate_devices();
};

} // namespace celegans
