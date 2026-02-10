# Step 28: Multi-Compartment Neuron Model (RIA)

## Motivation

Single-compartment neurons cannot reproduce subcellular calcium signals observed in RIA interneurons (Hendricks 2012 Nature). RIA's axon loop has two independent domains (nrV, nrD) that receive motor feedback from SMD neurons, enabling hardware-level multiplicative gating for klinotaxis.

## Architecture

### MultiCompartmentNeuron class
- Extends `Neuron` base class (polymorphic with SingleCompartmentNeuron)
- Each compartment: independent V, ion channels, calcium dynamics, I_syn
- Axial coupling: `I_axial = g_axial * (V_a - V_b)`
- Backward compatible: `get_membrane_potential()` returns soma V

### RIA 3-compartment layout
| Compartment | Index | Function |
|-------------|-------|----------|
| soma | 0 | Receives global sensory glutamate (AWC/ASE -> AIY -> RIA) |
| nrV | 1 | Receives SMDVL ACh -> GAR-3 -> IP3 -> local Ca2+ (ventral) |
| nrD | 2 | Receives SMDDL ACh -> GAR-3 -> IP3 -> local Ca2+ (dorsal) |

### IP3-mediated Ca2+ store release
- GAR-3 muscarinic receptor pathway (NOT voltage-gated)
- `dCa_store = store_release_rate * max(0, I_syn) * dt`
- Converts depolarizing synaptic current directly into local Ca2+
- REF: Hendricks 2012 -- ACh -> GAR-3 -> Gq -> PLC -> IP3 -> ER Ca2+ release

### Axial coupling (weak, preserves independence)
- soma <-> nrV: 0.15 nS
- soma <-> nrD: 0.15 nS
- nrV <-> nrD: 0.01 nS (nearly independent, Hendricks 2012)

## Klinotaxis mechanism (replaces Step 19 AC/DC approximation)

### Old (Step 19)
```
sensory_ac = ASE_ON - ASE_OFF - DC_baseline
ria_product = sensory_ac * head_curvature
curvature_bias = gain * filter(ria_product)
```

### New (Step 28)
```
SMD fires during head bend -> ACh -> RIA nrV/nrD -> IP3 -> local Ca2+
Sensory input -> RIA soma -> axial spread -> both nrV/nrD
Ca_diff = nrV_Ca - nrD_Ca (with DC removal, 2s tau)
curvature_bias = gain * filter(Ca_diff_AC)
```

The multiplication happens physically: sensory + motor signals are additive in Ca2+ space, but only the phase-locked oscillatory component (AC) carries perpendicular gradient information.

## Parameters

| Parameter | Value | Rationale |
|-----------|-------|-----------|
| store_release_rate | 0.0003 uM/ms/pA | IP3 Ca2+ store sensitivity |
| Ca tau (nrV/nrD) | 80 ms | Fast local dynamics |
| Ca buffer_ratio | 0.15 | Sensitive to small currents |
| klinotaxis_gain | 3000 /mm/uM | Ca2+ AC diff -> curvature |
| max_bias | 0.5 /mm | Reduced from 2.0 (cleaner signal) |
| mod_gain (CCA-1) | 5.0 mV/unit | Reduced from 15 (feedback compensation) |
| DC removal tau | 2000 ms | Remove tonic Ca2+ bias |
| SMD->RIA synapses | 1 section | Weak (minimize soma voltage leakage) |

## New connectome connections
- SMDDL -> RIAL nrD (comp 2), 1 section, ACh
- SMDDR -> RIAR nrD (comp 2), 1 section, ACh
- SMDVL -> RIAL nrV (comp 1), 1 section, ACh
- SMDVR -> RIAR nrV (comp 1), 1 section, ACh

## Results

### Regtest: 14 pass, 0 FAIL
- Heading rate: 16.9 deg/s (baseline 15.0, +13%)
- SMD oscillation preserved (V swing 58-66 mV)
- Speed, curvature, fatigue all nominal

### Diag (5 runs avg)
- CI: +0.20 (positive chemotaxis)
- Sleep: 20% (1 episode/300s)
- near_food: 18-38%

### RIA compartment Ca2+ dynamics
- Baseline: 0.05 uM
- Active: 0.06-0.12 uM (IP3 store release)
- nrV-nrD difference: +/-0.03 uM (raw), +/-0.004 uM (filtered AC)

## References
- Hendricks et al. 2012 Nature -- compartmentalized Ca2+ in RIA axon
- Ouellette et al. 2018 eNeuro -- RIA subcellular domains for navigation
- Iino & Yoshida 2009 -- curving rate proportional to perpendicular gradient
