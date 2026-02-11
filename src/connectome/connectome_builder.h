#pragma once
// ================================================================
// Connectome Builder — organizes default connectome by circuit
// Split from generate_default_connectome() (Step 51)
// ================================================================
#include "core/types.h"
#include <vector>

namespace celegans {

void build_default_connectome(
    std::vector<NeuronInfo>& neurons,
    std::vector<SynapseInfo>& synapses,
    std::vector<GapJunctionInfo>& gap_junctions);

} // namespace celegans
