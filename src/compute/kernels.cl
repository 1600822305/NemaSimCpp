// ================================================================
// C. elegans Neural Simulation — OpenCL Compute Kernels
// ================================================================
// GPU-accelerated synaptic current computation with STP dynamics
// Supports AMD (RDNA2+), NVIDIA, and Intel GPUs via OpenCL 1.2+
//
// REF: Tsodyks & Markram 1997 (STP model)
//      Liu 2009 PNAS (graded synapse adaptation)
// ================================================================

// Synapse data layout (must match SynapseGPU struct in C++)
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

// ----------------------------------------------------------------
// Kernel: Compute synaptic currents with STP update
// One work-item per synapse. Atomic add to accumulate I_out.
// ----------------------------------------------------------------
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

    // STD: vesicle pool recovery and depletion
    float dn = (1.0f - syn->vesicle_pool) / syn->tau_recovery - syn->alpha_d * S * syn->vesicle_pool;
    syn->vesicle_pool += dn * dt;
    syn->vesicle_pool = clamp(syn->vesicle_pool, 0.01f, 1.0f);

    // STF: release probability facilitation and decay
    float dp = (syn->p0 - syn->release_prob) / syn->tau_facil + syn->alpha_f * S * (1.0f - syn->release_prob);
    syn->release_prob += dp * dt;
    syn->release_prob = clamp(syn->release_prob, syn->p0 * 0.1f, 1.0f);

    // Effective synaptic strength: n * (p/p0)
    float stp_factor = syn->vesicle_pool * (syn->release_prob / syn->p0);

    // Synaptic current: I = -w * w_mod * g_max * stp * S * (V_post - E_syn)
    float I = -syn->weight * syn->weight_mod * syn->g_max * stp_factor * S * (V_post - syn->E_syn);
    I *= synapse_scale;

    // Atomic float add to post-synaptic neuron
    // OpenCL 1.x doesn't have native atomic float add, use CAS loop
    __global volatile int* addr = (__global volatile int*)&I_out[post];
    union { float f; int i; } old_val, new_val;
    do {
        old_val.i = *addr;
        new_val.f = old_val.f + I;
    } while (atomic_cmpxchg((__global volatile int*)addr, old_val.i, new_val.i) != old_val.i);
}
