#include "compute/opencl_backend.h"
#include "compute/cpu_backend.h"
#include "core/logger.h"
#include <fstream>
#include <sstream>

namespace celegans {

#ifdef CELEGANS_HAS_OPENCL

bool OpenCLBackend::initialize() {
    try {
        // Get all platforms
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        if (platforms.empty()) {
            LOG_WARN("OpenCL: No platforms found");
            return false;
        }

        // Find a GPU device (prefer discrete GPU)
        bool found = false;
        for (auto& plat : platforms) {
            std::vector<cl::Device> devices;
            plat.getDevices(CL_DEVICE_TYPE_GPU, &devices);
            for (auto& dev : devices) {
                std::string name = dev.getInfo<CL_DEVICE_NAME>();
                // Prefer discrete GPU (skip "Graphics" integrated if discrete available)
                size_t mem = dev.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
                if (!found || mem > device_.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>()) {
                    platform_ = plat;
                    device_ = dev;
                    found = true;
                }
            }
        }

        if (!found) {
            // Fallback to any device (CPU OpenCL)
            for (auto& plat : platforms) {
                std::vector<cl::Device> devices;
                plat.getDevices(CL_DEVICE_TYPE_ALL, &devices);
                if (!devices.empty()) {
                    platform_ = plat;
                    device_ = devices[0];
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            LOG_WARN("OpenCL: No devices found");
            return false;
        }

        // Create context and command queue
        context_ = cl::Context(device_);
        queue_ = cl::CommandQueue(context_, device_);

        // Load and compile kernel
        std::string src = load_kernel_source();
        if (src.empty()) {
            LOG_WARN("OpenCL: Failed to load kernel source");
            return false;
        }

        program_ = cl::Program(context_, src);
        try {
            program_.build({device_}, "-cl-fast-relaxed-math");
        } catch (...) {
            std::string log = program_.getBuildInfo<CL_PROGRAM_BUILD_LOG>(device_);
            LOG_WARN("OpenCL kernel build failed: ", log);
            return false;
        }

        kernel_syn_ = cl::Kernel(program_, "compute_synaptic_currents");

        auto info = device_info();
        LOG_INFO("OpenCL initialized: ", info.name,
                 " (", info.max_compute_units, " CUs, ",
                 info.global_mem_bytes / (1024*1024), " MB)");

        initialized_ = true;
        return true;

    } catch (std::exception& e) {
        LOG_WARN("OpenCL init error: ", e.what());
        return false;
    }
}

ComputeDeviceInfo OpenCLBackend::device_info() const {
    ComputeDeviceInfo info;
    if (!initialized_ && num_synapses_ == 0) {
        // Before init, try to return basic info
        info.name = "OpenCL (not initialized)";
        return info;
    }
    try {
        info.name = device_.getInfo<CL_DEVICE_NAME>();
        info.vendor = device_.getInfo<CL_DEVICE_VENDOR>();
        info.version = device_.getInfo<CL_DEVICE_VERSION>();
        info.global_mem_bytes = device_.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
        info.local_mem_bytes = device_.getInfo<CL_DEVICE_LOCAL_MEM_SIZE>();
        info.max_compute_units = device_.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
        info.max_work_group_size = static_cast<int>(device_.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>());
        info.is_gpu = (device_.getInfo<CL_DEVICE_TYPE>() & CL_DEVICE_TYPE_GPU) != 0;
    } catch (...) {}
    return info;
}

void OpenCLBackend::upload_synapses(const std::vector<SynapseGPU>& synapses) {
    if (!initialized_) return;
    num_synapses_ = static_cast<int>(synapses.size());
    if (num_synapses_ == 0) return;

    buf_synapses_ = cl::Buffer(context_, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        sizeof(SynapseGPU) * num_synapses_, (void*)synapses.data());
}

void OpenCLBackend::compute_synaptic_currents(
    const std::vector<float>& V_neurons,
    std::vector<float>& I_out,
    float dt,
    float synapse_scale,
    int num_neurons)
{
    if (!initialized_ || num_synapses_ == 0) return;

    // Upload voltages
    buf_V_ = cl::Buffer(context_, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * num_neurons, (void*)V_neurons.data());

    // Create output buffer (zeroed)
    std::fill(I_out.begin(), I_out.end(), 0.0f);
    buf_I_out_ = cl::Buffer(context_, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * num_neurons, (void*)I_out.data());

    // Set kernel args
    kernel_syn_.setArg(0, buf_synapses_);
    kernel_syn_.setArg(1, buf_V_);
    kernel_syn_.setArg(2, buf_I_out_);
    kernel_syn_.setArg(3, num_synapses_);
    kernel_syn_.setArg(4, num_neurons);
    kernel_syn_.setArg(5, dt);
    kernel_syn_.setArg(6, synapse_scale);

    // Execute: one work-item per synapse
    queue_.enqueueNDRangeKernel(kernel_syn_, cl::NullRange,
        cl::NDRange(num_synapses_), cl::NullRange);

    // Read back I_out (with atomic adds, already accumulated)
    queue_.enqueueReadBuffer(buf_I_out_, CL_TRUE, 0,
        sizeof(float) * num_neurons, I_out.data());
}

void OpenCLBackend::download_stp_state(std::vector<SynapseGPU>& synapses) {
    if (!initialized_ || num_synapses_ == 0) return;
    synapses.resize(num_synapses_);
    queue_.enqueueReadBuffer(buf_synapses_, CL_TRUE, 0,
        sizeof(SynapseGPU) * num_synapses_, synapses.data());
}

std::string OpenCLBackend::load_kernel_source() {
    // Try multiple paths for kernel file
    const char* paths[] = {
        "src/compute/kernels.cl",
        "../src/compute/kernels.cl",
        "kernels.cl",
        "compute/kernels.cl"
    };

    for (auto& path : paths) {
        std::ifstream f(path);
        if (f.good()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }

    // Embedded kernel as fallback
    return R"CL(
// Synapse data layout (must match SynapseGPU struct)
typedef struct {
    int pre_id;
    int post_id;
    float weight;
    float g_max;
    float E_syn;
    float V_thresh;
    float V_slope;
    float weight_mod;
    float vesicle_pool;
    float release_prob;
    float p0;
    float tau_recovery;
    float alpha_d;
    float tau_facil;
    float alpha_f;
} SynapseGPU;

__kernel void compute_synaptic_currents(
    __global SynapseGPU* synapses,
    __global const float* V,
    __global float* I_out,
    const int num_synapses,
    const int num_neurons,
    const float dt,
    const float synapse_scale)
{
    int gid = get_global_id(0);
    if (gid >= num_synapses) return;

    __global SynapseGPU* syn = &synapses[gid];
    int pre = syn->pre_id;
    int post = syn->post_id;
    if (pre < 0 || pre >= num_neurons || post < 0 || post >= num_neurons) return;

    float V_pre = V[pre];
    float V_post = V[post];

    // Graded transmitter release sigmoid
    float S = 1.0f / (1.0f + exp(-(V_pre - syn->V_thresh) / syn->V_slope));

    // STD: vesicle pool dynamics
    float dn = (1.0f - syn->vesicle_pool) / syn->tau_recovery - syn->alpha_d * S * syn->vesicle_pool;
    syn->vesicle_pool += dn * dt;
    syn->vesicle_pool = clamp(syn->vesicle_pool, 0.01f, 1.0f);

    // STF: release probability dynamics
    float dp = (syn->p0 - syn->release_prob) / syn->tau_facil + syn->alpha_f * S * (1.0f - syn->release_prob);
    syn->release_prob += dp * dt;
    syn->release_prob = clamp(syn->release_prob, syn->p0 * 0.1f, 1.0f);

    // Effective synaptic strength
    float stp_factor = syn->vesicle_pool * (syn->release_prob / syn->p0);

    // Synaptic current
    float I = -syn->weight * syn->weight_mod * syn->g_max * stp_factor * S * (V_post - syn->E_syn);
    I *= synapse_scale;

    // Atomic add to post-synaptic neuron (multiple synapses converge)
    // Use atomic_add for float via union trick (OpenCL 1.x compatibility)
    union { float f; int i; } old_val, new_val;
    __global volatile int* addr = (__global volatile int*)&I_out[post];
    do {
        old_val.i = *addr;
        new_val.f = old_val.f + I;
    } while (atomic_cmpxchg((__global volatile int*)addr, old_val.i, new_val.i) != old_val.i);
}
)CL";
}

#endif // CELEGANS_HAS_OPENCL

// ================================================================
// Static factory methods (always available)
// ================================================================

bool ComputeBackend::opencl_available() {
#ifdef CELEGANS_HAS_OPENCL
    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        for (auto& plat : platforms) {
            std::vector<cl::Device> devices;
            try { plat.getDevices(CL_DEVICE_TYPE_GPU, &devices); } catch (...) {}
            if (!devices.empty()) return true;
        }
    } catch (...) {}
#endif
    return false;
}

std::vector<ComputeDeviceInfo> ComputeBackend::enumerate_devices() {
    std::vector<ComputeDeviceInfo> result;
#ifdef CELEGANS_HAS_OPENCL
    try {
        std::vector<cl::Platform> platforms;
        cl::Platform::get(&platforms);
        for (auto& plat : platforms) {
            std::vector<cl::Device> devices;
            try { plat.getDevices(CL_DEVICE_TYPE_ALL, &devices); } catch (...) { continue; }
            for (auto& dev : devices) {
                ComputeDeviceInfo info;
                info.name = dev.getInfo<CL_DEVICE_NAME>();
                info.vendor = dev.getInfo<CL_DEVICE_VENDOR>();
                info.version = dev.getInfo<CL_DEVICE_VERSION>();
                info.global_mem_bytes = dev.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
                info.local_mem_bytes = dev.getInfo<CL_DEVICE_LOCAL_MEM_SIZE>();
                info.max_compute_units = dev.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
                info.max_work_group_size = static_cast<int>(dev.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>());
                info.is_gpu = (dev.getInfo<CL_DEVICE_TYPE>() & CL_DEVICE_TYPE_GPU) != 0;
                result.push_back(info);
            }
        }
    } catch (...) {}
#endif
    return result;
}

std::unique_ptr<ComputeBackend> ComputeBackend::create_cpu() {
    auto backend = std::make_unique<CpuBackend>();
    backend->initialize();
    return backend;
}

std::unique_ptr<ComputeBackend> ComputeBackend::create_opencl() {
    auto backend = std::make_unique<OpenCLBackend>();
    if (backend->initialize()) {
        return backend;
    }
    return nullptr;
}

std::unique_ptr<ComputeBackend> ComputeBackend::create_best_available() {
    // Try OpenCL first
    auto ocl = create_opencl();
    if (ocl) return ocl;

    // Fallback to CPU
    return create_cpu();
}

} // namespace celegans
