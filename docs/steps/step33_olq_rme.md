# Step 33: OLQ Nose Touch + RME Head Inhibition

## Biological Basis

### RME — GABAergic Head Amplitude Control
RME (Ring Motor E) neurons are GABAergic motor neurons that modulate head bending amplitude via push-pull inhibition with SMD. Killing RME → exaggerated head flexures (Huang 2016 eLife).

**Critical anatomy (contralateral projection):**
- RMED → innervates **VENTRAL** head muscles (name "Dorsal" but projects contralateral!)
- RMEV → innervates **DORSAL** head muscles (name "Ventral" but projects contralateral!)
- RMEL/RMER → no effect on D/V bending (Huang 2016), omitted

**SMD⇌RME is EXTRASYNAPTIC (not direct synapse):**
- SMD ACh → GAR-2 muscarinic receptor on RME (volume transmission, ~10× weaker)
- RME GABA → GBB-1/2 GABAB receptor on SMD (volume transmission)
- Modeled as weak chemical synapses (sections=0.3, g≈0.03 nS)

**REF:** White 1986, Huang 2016 eLife, Jorgensen 2005 WormBook

### OLQ — Nose Touch Mechanosensory
OLQ (Outer Labial Quadrant) neurons are glutamatergic mechanosensory neurons at the nose tip. They detect close-range obstacles and mediate head withdrawal reflex.

- Only 5% of nose touch avoidance (ASH=45%, FLP=29%) — subtle, exploratory
- OLQ → RMD (head withdrawal, Hart 1995), NOT → RME
- OLQ → RIC → AVA (indirect reversal, weak)
- OLQ ↔ CEP (gap junction, dopamine coupling)
- OLQ does NOT directly connect to AVA (nose touch ≠ body touch reversal)

**REF:** Kaplan & Horvitz 1993, Hart 1995, White 1986

## What Was Added

### Neurons (6 new, 93→99 total)
| Neuron | Type | NT | Role |
|--------|------|-----|------|
| RMED | Motor | GABA | Inhibits ventral head muscles |
| RMEV | Motor | GABA | Inhibits dorsal head muscles |
| OLQDL | Sensory | Glutamate | Nose touch (dorsal-left quadrant) |
| OLQDR | Sensory | Glutamate | Nose touch (dorsal-right quadrant) |
| OLQVL | Sensory | Glutamate | Nose touch (ventral-left quadrant) |
| OLQVR | Sensory | Glutamate | Nose touch (ventral-right quadrant) |

### Synaptic Connections
| Synapse | Sections | Mechanism |
|---------|----------|-----------|
| SMDDL/DR →(extrasyn) RMED | 0.3 each | GAR-2 muscarinic, 10× diluted |
| SMDVL/VR →(extrasyn) RMEV | 0.3 each | GAR-2 muscarinic, 10× diluted |
| RMED ⊣(GABAB) SMDVL/VR | 0.3 each | GBB-1/2 negative feedback |
| RMEV ⊣(GABAB) SMDDL/DR | 0.3 each | GBB-1/2 negative feedback |
| RIAL/R → RMED | 1 each | Direct synapse (White 1986) |
| RIAL/R → RMEV | 1 each | Direct synapse (White 1986) |
| OLQDL → RMDDL | 1 | Head withdrawal (Hart 1995) |
| OLQDR → RMDDR | 1 | Head withdrawal |
| OLQVL → RMDVL | 1 | Head withdrawal |
| OLQVR → RMDVR | 1 | Head withdrawal |
| OLQDL/VL → RICL | 1 each | Indirect reversal path |
| OLQDR/VR → RICR | 1 each | Indirect reversal path |
| OLQDL ↔ CEPDL | 1 (gap) | DA system coupling |
| OLQDR ↔ CEPDR | 1 (gap) | DA system coupling |
| OLQVL ↔ CEPVL | 1 (gap) | DA system coupling |
| OLQVR ↔ CEPVR | 1 (gap) | DA system coupling |

### Muscle Mappings
| Neuron | Segments | Side | Inhibitory |
|--------|----------|------|------------|
| RMED | 0-4 | ventral | yes (GABAA) |
| RMEV | 0-4 | dorsal | yes (GABAA) |

### OLQ Transduction
- Trigger: head within 0.3mm of arena wall (vs ALM's 2mm for body touch)
- Current: 30 pA × proximity (vs ALM's 80 pA) — weaker, exploratory
- Also activates during body touch (front_touch) at full 30 pA
- Does NOT trigger reversal directly — only head withdrawal via RMD

## Infrastructure Changes

### SynapseInfo.num_sections: int → double
Changed to support extrasynaptic fractional weights (0.3 sections ≈ 0.03 nS).
Affects: `core/types.h`, `connectome_loader.cpp` (lambda signatures).

### Weathervane SMD Fraction: 100% → 40%
**Problem:** Multi-channel weathervane (chemo + repellent + temperature) could align to produce ~39 pA sustained bias on one SMD side, overwhelming the oscillator.
**Fix:** Apply only 40% of weathervane bias to SMD neurons. The curvature_bias bypass (main turning mechanism) remains at full strength.
**Result:** SMD oscillation stable even under worst-case gradient alignment.

### as_factor: 1.0 → 3.5
RMEV inhibits dorsal muscles → dorsal tone dropped from 0.26 to 0.17. Higher factor needed to maintain omega/reversal gating.

## Results

| Metric | Step 32 | Step 33 | Target |
|--------|---------|---------|--------|
| head curv range | [-0.04, +0.15] | [-0.07, +0.07] | symmetric ✅ |
| D/V ratio | 3.6× | 1.06× | ~1.0 ✅ |
| omega/reversal | 67% | 58% | 60-70% ✅ |
| regtest | 17 pass | 17 pass | 17 pass ✅ |
| Wave | GOOD | GOOD | GOOD ✅ |
| RMED release | N/A | 0.300 | active ✅ |
| RMEV release | N/A | 0.295 | active ✅ |

## Emergent Properties
- **Push-pull oscillation**: SMDD excites dorsal + RMED inhibits ventral → clean sinusoidal D bends
- **AS01 bias correction**: RMEV inhibits dorsal head muscles → counteracts AS01 dorsal bias
- **Amplitude control**: RME gain control keeps head bending within optimal range (Huang 2016)
- **Nose exploration**: OLQ→RMD head withdrawal without triggering full reversal (future: with obstacles)

## Files Changed
- `src/core/types.h` — SynapseInfo/GapJunctionInfo num_sections int→double
- `src/connectome/connectome_loader.cpp` — 6 neurons + synapses + lambda signatures
- `src/motor/motor_controller.cpp` — RMED/RMEV inhibitory muscle mappings
- `src/simulation/simulation_engine.h` — olq_ids_, nose_margin_, as_factor 3.5
- `src/simulation/simulation_engine.cpp` — OLQ transduction, weathervane 40% SMD fraction
- `src/simulation/regression_test.cpp` — baselines updated for RME-modulated dynamics
- `src/simulation/diag_main.cpp` — RME/OLQ diagnostic section (#21)
