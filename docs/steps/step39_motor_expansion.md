# Step 39: Motor Neuron Expansion — Complete B/A/D Coverage

## Biological Background

### Motor Neuron Classes (White 1986, Haspel 2010)
- **B-class** (DB/VB): Forward locomotion, cholinergic excitatory
  - Real: DB1-7 (dorsal), VB1-11 (ventral)
  - B-MNs are proprioceptive relay oscillators (Wen 2012)
- **A-class** (DA/VA): Backward locomotion, cholinergic excitatory
  - Real: DA1-9 (dorsal), VA1-12 (ventral)
  - A-MNs are intrinsic oscillators (Gao & Zhen 2018 eLife)
- **D-class** (DD/VD): Cross-inhibition, GABAergic
  - Real: DD1-6 (dorsal input → ventral inhibition), VD1-13 (ventral → dorsal)
  - Reciprocal inhibition ensures alternating D/V contraction
- **AS-class**: Dorsal-only, always active (both fwd/rev)
  - Real: AS1-11, glutamatergic

### Key Principle: Tiled NMJs
Each motor neuron innervates a contiguous stretch of body wall muscle.
Adjacent MNs overlap slightly, creating smooth wave propagation.
B-MNs sense curvature in the PREVIOUS unit's territory → sequential activation.

## Implementation

### Neurons Added (114 → 132, +18)
| Class | Before | After | Change |
|-------|--------|-------|--------|
| DB | 3 (DB01-03) | 7 (DB01-07) | +4 |
| VB | 3 (VB01-03) | 7 (VB01-07) | +4 |
| DA | 3 (DA01-03) | 5 (DA01-05) | +2 |
| VA | 3 (VA01-03) | 5 (VA01-05) | +2 |
| DD | 3 (DD01-03) | 5 (DD01-05) | +2 |
| VD | 3 (VD01-03) | 5 (VD01-05) | +2 |
| AS | 5 (AS01-05) | 7 (AS01-07) | +2 |

### Motor Controller Mapping (body segment coverage)
```
B-class (forward, 7 units, continuous seg 4-42):
  DB01: seg 4-9    VB01: seg 4-9
  DB02: seg 9-14   VB02: seg 9-14
  DB03: seg 14-19  VB03: seg 14-19
  DB04: seg 19-24  VB04: seg 19-24
  DB05: seg 24-29  VB05: seg 24-29
  DB06: seg 29-35  VB06: seg 29-35
  DB07: seg 35-42  VB07: seg 35-42

A-class (backward, 5 units):
  DA01: seg 4-12   VA01: seg 4-12
  DA02: seg 12-20  VA02: seg 12-20
  DA03: seg 20-28  VA03: seg 20-28
  DA04: seg 28-35  VA04: seg 28-35
  DA05: seg 35-42  VA05: seg 35-42

D-class (cross-inhibition, 5 units):
  DD01: seg 4-12 (inh ventral)   VD01: seg 4-12 (inh dorsal)
  DD02: seg 12-20                VD02: seg 12-20
  DD03: seg 20-28                VD03: seg 20-28
  DD04: seg 28-35                VD04: seg 28-35
  DD05: seg 35-42                VD05: seg 35-42

AS-class (dorsal only, 7 units):
  AS01: seg 2-6, AS02: seg 6-12, AS03: seg 12-18,
  AS04: seg 18-24, AS05: seg 24-30, AS06: seg 30-36, AS07: seg 36-42
```

### Proprioceptive Mapping (B-class sequential relay)
```
DB01 senses seg 0-4  (SMD territory)
DB02 senses seg 4-9  (DB01 territory)
DB03 senses seg 9-14 (DB02 territory)
DB04 senses seg 14-19 (DB03 territory)
DB05 senses seg 19-24 (DB04 territory)
DB06 senses seg 24-29 (DB05 territory)
DB07 senses seg 29-35 (DB06 territory)
```

### Synapses Expanded
- **AVA→DA/VA**: 3→5 targets each (gradient: anterior 5 sec → posterior 2 sec)
- **AVB→DB/VB**: 3→7 targets each (gradient: anterior 5 sec → posterior 1 sec)
- **DD↔VD**: 3→5 pairs cross-inhibition
- **DD⊣AS**: expanded to DD04/DD05 → AS05-07
- **DB↔AS gap**: expanded to DB04-07 ↔ AS05-07
- **DVA→DB/VB**: 3→7 targets each
- **AVE→DA**: 3→5 targets

## Results

### Regression Test
17 pass, 0 FAIL ✅ (3 consecutive runs all pass)

### Diagnostic (300s)
| Metric | Step 38 | Step 39 | Notes |
|--------|---------|---------|-------|
| **Wave** | GOOD | **GOOD** | Propagates to tail |
| **Speed** | 0.185 | **0.176** | Slight decrease, normal |
| **Muscle work** | 0.316 | **0.338** | Improved! More MNs |
| **Curv stability** | 2.0 Hz | **0.6 Hz** | More stable |
| **Seg 7 amp** | 0.290 | **0.291** | Consistent |
| **Seg 15 amp** | 0.384 | **0.246** | More uniform distribution |
| **D/V ratio** | 1.02 | **0.97** | Perfect symmetry |
| **eggs_laid** | 2 | **2** | Unaffected |
| **omega/reversal** | 0.95 | **0.95** | Consistent |

### Key Improvements
1. **Continuous body coverage**: 7 B-class units tile seg 4-42 without gaps
2. **More uniform wave**: Seg 15 amplitude decreased from 0.384 to 0.246 (less peaky)
3. **Better stability**: Curvature sign-change rate 2.0→0.6 Hz (smoother oscillation)
4. **Higher muscle work**: 0.316→0.338 (more MNs contributing)

## References
- White et al. 1986 — Complete motor neuron anatomy
- Haspel & O'Donovan 2010 — Motor neuron body segment mapping
- Wen et al. 2012 Neuron — B-class sequential activation
- Gao & Zhen 2018 eLife — A-MNs are local oscillators for backward locomotion
- Boyle et al. 2012 — Proprioceptive coupling in locomotion

## Files Modified
- `src/connectome/connectome_loader.cpp` — 18 new neurons + expanded synapses/gap junctions
- `src/motor/motor_controller.cpp` — Updated segment mappings for all expanded classes
- `src/simulation/simulation_engine.cpp` — Updated proprioceptive mappings (B×7, A×5)
