# Step 38: HSN/VC — Egg-Laying System

## Biological Background

### Egg-Laying Behavior (Collins 2016 eLife, Schafer 2006)
- Two behavioral states: **inactive** (~20 min) and **active** (~2 min)
- Inactive: HSN Ca2+ transients every ~41s, vulval muscles quiet
- Active: HSN burst → 5-HT release → vulval muscle rhythmic contraction
- Eggs accumulate ~1 per 10 min per gonad arm; 2-3 eggs trigger active state
- Active state: multiple egg-laying events (~10s intervals)

### HSN Command Motor Neuron (Waggoner 1998, Brewer 2019)
- Serotonergic (5-HT + NLP-3 neuropeptide)
- Drives vulval muscle contraction via 5-HT → G protein-coupled receptors
- Input: PLM inhibition (gentle touch → stop egg laying)
- Input: BAG→FLP-10/17→EGL-6→HSN inhibition (CO2 sensing)
- Feedback: uv1 tyramine → LGC-55 → HSN hyperpolarization (terminates active state)

### VC Motor Neurons (2021 J Neurosci)
- Cholinergic (ACh), 6 total (VC1-6), VC4/VC5 most proximal to vulva
- Mechanically activated by vulval muscle contraction → positive feedback
- VC → vm2 vulval muscles (ACh excitation)
- VC ⊣ HSN (ACh via GAR-2 muscarinic inhibition)
- VC → VA/VB/VD locomotion motor neurons (slowing during egg laying)

## Implementation

### Neurons Added (110 → 114)
| Neuron | Type | NT | Function |
|--------|------|-----|----------|
| HSNL | Motor | 5-HT | Left egg-laying command motor neuron |
| HSNR | Motor | 5-HT | Right egg-laying command motor neuron |
| VC4 | Motor | ACh | Vulval motor neuron (proximal) |
| VC5 | Motor | ACh | Vulval motor neuron (proximal) |

### Synapses Added (4 chemical + 2 gap)
| Pre | Post | Type | Sections | Function |
|-----|------|------|----------|----------|
| PLML | HSNL | Inh | 1 | Gentle touch inhibits egg laying |
| PLMR | HSNR | Inh | 1 | Same, right side |
| VC4 | VB01 | Inh | 0.5 | Egg-laying slows locomotion |
| VC5 | VB02 | Inh | 0.5 | Same |
| HSNL ↔ VC4 | Gap | 2 | Synchronize egg-laying motor output |
| HSNR ↔ VC5 | Gap | 2 | Same, right side |

### HSN as 5-HT Source
HSN registered as serotonin source (alongside NSM and ADF).
5-HT sources: 4 → 6 (NSM L/R + ADF L/R + HSN L/R)

### Egg-Laying Transduction
```
egg_pressure: ramps toward 1.0 (tau_fill = 120s)
  Simulates egg accumulation in uterus

HSN activation: sigmoid(egg_pressure - 0.7) × 30 pA
  + tyramine inhibition: -20 pA × TA concentration (LGC-55)
  → HSN only fires when egg_pressure high AND TA low

Active state: 2s duration (scaled from real 2min)
  HSN active → VC gets 15 pA excitation
  End of active state → egg_laid event → egg_pressure reset to 0.1

Cycle: ~120-150s between egg-laying events
```

### Parameters
| Parameter | Value | Unit | Description |
|-----------|-------|------|-------------|
| egg_tau_fill | 120000 | ms | Time constant for egg accumulation |
| egg_threshold | 0.7 | - | HSN activation threshold |
| hsn_egg_gain | 30 | pA | Max HSN drive from egg pressure |
| egg_active_duration | 2000 | ms | Active state duration |
| TA inhibition | -20 | pA | LGC-55 tyramine → HSN |

## Bug Fix
- **wall_dist negative when outside arena**: `std::max(0.0, ...)` clamp added
  - Without clamp: worm outside 50mm → wall_dist < 0 → PVD proximity > 1.0 → saturation

## Results

### Regression Test
17 pass, 0 FAIL ✅

### Diagnostic (300s)
| Metric | Step 37 | Step 38 | Notes |
|--------|---------|---------|-------|
| **eggs_laid** | N/A | **2** | ~150s interval |
| **egg_pressure** | N/A | **0.001→0.705** | Correct sawtooth |
| **HSNL S** | N/A | **0.210** | Moderate activity |
| **VC4 S** | N/A | **0.424** | Active during HSN burst |
| **5-HT sources** | 4 | **6** | +HSN L/R |
| **omega/reversal** | 0.85 | **0.90** | Normal variation |
| **CI** | -0.303 | **-0.238** | Pathogen avoidance |
| **PVDL S** | 0.169 | **0.169** | wall_dist fix OK |
| **Wave** | GOOD | **GOOD** | |

### Emergent Behaviors
1. **Egg-laying slows locomotion**: HSN burst → 5-HT↑ → speed_scale↓
2. **Touch inhibits egg laying**: PLM⊣HSN → no eggs during reversal
3. **Tyramine terminates active state**: TA→LGC-55→HSN hyperpolarization
4. **Egg-laying on food preferred**: Near food → satiety↑ → 5-HT↑ → HSN potentiated

## References
- Collins et al. 2016 eLife — Egg-laying circuit activity and behavioral states
- Collins et al. 2021 J Neurosci — VC mechanically activated motor neurons
- Waggoner et al. 1998 Neuron — HSN serotonin controls egg laying
- Brewer et al. 2019 PLoS Genetics — NLP-3 neuropeptide co-transmission
- Schafer 2006 — Egg-laying circuit review
- Zhang et al. 2008 — PLM inhibits HSN, VC timing

## Files Modified
- `src/connectome/connectome_loader.cpp` — 4 neurons + 4 synapses + 2 gap junctions
- `src/simulation/simulation_engine.h` — hsn_ids_, vc_ids_, egg_pressure_, egg params, accessors
- `src/simulation/simulation_engine.cpp` — HSN/VC transduction, HSN as 5-HT source, wall_dist fix
- `src/simulation/diag_main.cpp` — Egg-laying diagnostic section #25
