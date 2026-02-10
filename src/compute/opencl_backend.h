#pragma once

#include "compute/compute_backend.h"

#ifdef CELEGANS_HAS_OPENCL
#define CL_HPP_TARGET_OPENCL_VERSION 200
#define CL_HPP_MINIMUM_OPENCL_VERSION 120
#include <CL/opencl.hpp>
#endif

namespace celegans {

#ifdef CELEGANS_HAS_OPENCL

class OpenCLBackend : public ComputeBackend {
public:
    bool initialize() override;

    ComputeDeviceInfo device_info() const override;
    std::string backend_name() const override { return "OpenCL"; }
    bool is_gpu_backend() const override { return true; }

    void upload_synapses(const std::vector<SynapseGPU>& synapses) override;

    void compute_synaptic_currents(
        const std::vector<float>& V_neurons,
        std::vector<float>& I_out,
        float dt,
        float synapse_scale,
        int num_neurons) override;

    void download_stp_state(std::vector<SynapseGPU>& synapses) override;

private:
    cl::Platform platform_;
    cl::Device device_;
    cl::Context context_;
    cl::CommandQueue queue_;
    cl::Program program_;
    cl::Kernel kernel_syn_;

    // Device buffers
    cl::Buffer buf_synapses_;
    cl::Buffer buf_V_;
    cl::Buffer buf_I_out_;

    int num_synapses_ = 0;
    bool initialized_ = false;

    std::string load_kernel_source();
};

#else

// Stub when OpenCL not available
class OpenCLBackend : public ComputeBackend {
public:
    bool initialize() override { return false; }
    ComputeDeviceInfo device_info() const override { return {}; }
    std::string backend_name() const override { return "OpenCL (unavailable)"; }
    bool is_gpu_backend() const override { return false; }
    void upload_synapses(const std::vector<SynapseGPU>&) override {}
    void compute_synaptic_currents(const std::vector<float>&, std::vector<float>&,
        float, float, int) override {}
    void download_stp_state(std::vector<SynapseGPU>&) override {}
};

#endif

} // namespace celegans
