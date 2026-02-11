# Step 36: DVA/PVD — Whole-Body Proprioception

## Biological Background

### DVA (Li 2006 Nature)
- Single unpaired interneuron, axon spans entire body
- TRP-4 TRPN mechanosensitive channel: stretch receptor
- trp-4 mutant: abnormal body bending (exaggerated amplitude)
- DVA does NOT innervate muscles directly — sends signals to VNC motor neurons
- Laser ablation: mild effect on body bending (Yeon 2018)
- DVA provides global gain modulation, not local CPG

### PVD (Way & Chalfie 1989)
- Paired sensory neurons (L/R), glutamatergic
- Multi-dendritic: dendrites tile entire body wall (menorah structures)
- Dual-mode: harsh touch + proprioception
- Harsh touch threshold higher than ALM (platinum wire vs eyelash)
- GLR-1 glutamate receptors on AVA mediate harsh touch response (Hart 1995)
- Ablation: increased dwelling behavior, reduced locomotion coordination

### SMDD Proprioception (Yeon 2018 PLoS Biology)
- SMDD neurons are the primary head steering proprioceptors
- TRP-1/TRP-2 TRPC channels sense head/neck bending
- Already implemented in our model (SMD oscillator)

## Implementation

### Neurons Added (107 → 110)
| Neuron | Type | NT | Function |
|--------|------|-----|----------|
| DVA | Inter | Glu | Whole-body proprioceptive integration |
| PVDL | Sensory | Glu | Left harsh touch + posterior proprioception |
| PVDR | Sensory | Glu | Right harsh touch + posterior proprioception |

### Synapses Added (10 chemical + 2 gap)
| Pre | Post | Type | Sections | Function |
|-----|------|------|----------|----------|
| DVA | DB01/02/03 | Exc | 1 each | Forward wave amplitude modulation |
| DVA | VB01/02/03 | Exc | 1 each | Ventral B-class modulation |
| DVA | AVAL | Exc | 0.5 | Extreme bending → protective reversal |
| PVDL | AVAL | Exc | 2 | Harsh touch → backward movement |
| PVDR | AVAR | Exc | 2 | Harsh touch → backward movement |
| PVDL ↔ DVA | Gap | 1 | Proprioceptive signal integration |
| PVDR ↔ DVA | Gap | 1 | Proprioceptive signal integration |

### DVA Transduction
```
Input: mean |curvature| across all 48 body segments
Drive: dva_gain (15 pA) × mean_abs_curv
Clamp: max 30 pA
Typical: mean_abs_curv ≈ 0.09 /mm → DVA drive ≈ 1.4 pA
```

### PVD Transduction (dual-mode)
```
Mode 1 - Harsh touch:
  wall_dist < 1.0mm → I = 60pA × (1 - wall_dist/1.0)
  (closer threshold than ALM 2mm = stronger stimulus needed)

Mode 2 - Posterior proprioception:
  posterior half mean |curvature| × 8 pA gain
  Typical: ~0.1 /mm → ~0.8 pA (weak tonic)
```

## Bug Fix History
1. **PVD harsh touch threshold too large**: 3.0mm → 1.0mm
   - At 3mm, PVD fired continuously near walls → PVDL S=1.000 → worm escaped arena
   - At 1mm, PVD only fires on actual wall contact → PVDL S=0.169 (normal)
2. **PVD harsh touch current too strong**: 100pA → 60pA
   - PVD→AVA already has 2 sections (strong synapse)

## Results

### Regression Test
17 pass, 0 FAIL ✅

### Diagnostic (300s)
| Metric | Step 35 | Step 36 | Notes |
|--------|---------|---------|-------|
| DVA S | N/A | 0.327 | Active, moderate |
| PVDL S | N/A | 0.169 | Low baseline (proprioception only) |
| mean |curv| | N/A | 0.091 /mm | Normal locomotion range |
| omega/reversal | 1.00 | 0.94 | Improved from Step 35 |
| Wave | GOOD | GOOD | No regression |
| Speed | 0.20 | 0.19 | Stable |
| near_food | 13.2% | 50.3% | Variable (run-dependent) |

## References
- Li et al. 2006 Nature — DVA TRP-4 stretch receptor
- Hu et al. 2011 — DVA proprioceptive properties
- Yeon et al. 2018 PLoS Biology — SMDD proprioception, TRP-1/TRP-2
- Way & Chalfie 1989 — PVD harsh touch identification
- Hart et al. 1995 — GLR-1 mediates PVD→AVA harsh touch
- Albeg et al. 2011 — PVD dendrite morphology and proprioception

## Files Modified
- `src/connectome/connectome_loader.cpp` — 3 neurons + 10 synapses + 2 gap junctions
- `src/simulation/simulation_engine.h` — dva_id_, pvd_ids_, proprioception params
- `src/simulation/simulation_engine.cpp` — DVA/PVD transduction block
- `src/simulation/diag_main.cpp` — Proprioception diagnostic section #24
