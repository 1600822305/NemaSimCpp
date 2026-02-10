# Step 27: Sleep / Quiescence (Lethargus)

## Overview

Implements sleep-like behavioral quiescence driven by the RIS neuron and FLP-11 neuropeptide.
This is the first "stop behavior" — previously the worm was always active.

**Real biology**: activity → fatigue → quiescence (lethargus) → recovery → activity

## Biological Background

### RIS Neuron
- Single (unpaired) GABAergic + peptidergic interneuron
- Sleep-active: depolarizes at sleep onset, silent during wakefulness
- Optogenetic activation → immediate locomotion stop + pump cessation
- Functions as both sleep inducer AND locomotion stop neuron

### FLP-11 Neuropeptide
- **Major sleep transmitter** of RIS (not GABA) — Turek 2016 eLife
- Released upon RIS depolarization via dense-core vesicles
- Acts through multiple redundant GPCRs: FRPR-3 (~30 neurons), NPR-4 (~5 neurons), NPR-22 (neurons + pharynx muscle + head muscle)
- **Volume transmission**: targets include non-synaptic partners → systemic effect
- **Self-inhibition**: FLP-11 feeds back on RIS negatively → spontaneous awakening (2025 Current Biology)

### Sleep Homeostasis (Nagy 2014 eLife)
- **Micro-homeostasis**: activity bout → longer subsequent quiescence (NPR-1/NPY dependent)
- **Macro-homeostasis**: strong disruption → elevated baseline quiescence (DAF-16/FOXO dependent)
- **Arousal threshold**: weak stimuli → no response; strong stimuli → awakening

## Implementation

### New Components

| Component | Details |
|-----------|---------|
| **RIS neuron** | 1 neuron (84th, GABA) — `connectome_loader.cpp` |
| **Chemical synapses** | RIS⊣AVA(2), RIS⊣AVB(1), RIS⊣AIB(1) — GABAergic inhibition |
| **Gap junctions** | RIS↔AIBL(2), RIS↔AIBR(2) |
| **fatigue_** | [0,1] homeostatic sleep drive — `simulation_engine.h` |
| **FLP-11 effects** | Volume transmission: speed, commands, pharynx, head motors |

### Fatigue Dynamics

```cpp
// Awake: fatigue rises proportional to locomotion speed
fatigue_ += activity * dt / tau_rise;   // tau_rise = 120s

// Sleeping: fatigue decays (restorative)
fatigue_ -= fatigue_ * dt / tau_decay;  // tau_decay = 60s

// Sleep onset: fatigue > 0.7 → is_sleeping_ = true
// Wake threshold: fatigue < 0.15 → is_sleeping_ = false (hysteresis)
```

### RIS Activation

```cpp
// Sigmoid of fatigue around threshold
fatigue_drive = 40 / (1 + exp(-12 × (fatigue - 0.7)));

// Sleep maintenance: flip-flop stable state (Saper 2005)
sleep_maintenance = is_sleeping ? 25.0 : 0.0;

// Self-inhibition (2025 Current Biology)
self_inhibition = -3.0 × RIS_release_rate;

// Total: RIS I_ext = 2 + fatigue_drive + sleep_maintenance + self_inhibition
// Awake: ~2 pA (silent)
// Sleeping: ~60 pA (strong activation → FLP-11 release)
```

### FLP-11 Effects (Volume Transmission)

```cpp
flp11 = sigmoid(RIS_V);  // 0-1, transmitter release rate

// 1. Speed: × (1 - 0.97 × flp11) → up to 97% reduction (near-atonia)
// 2. Command interneurons: AVA/AVB -= 15 × flp11 pA
// 3. Pharyngeal MC: -= 12 × flp11 pA (pump cessation)
// 4. Head motors (SMD/RMD): -= 20 × flp11 pA (stop CCA-1 oscillation)
// 5. Ventral cord motors (DA/DB/VA/VB/DD/VD): -= 30 × flp11 pA (deep atonia)
// 6. Head tonic suppression: head_tonic × (1 - 0.95 × flp11) (upstream drive off)
```

### Arousal Threshold (Emergent)

No explicit arousal code — it emerges from the competition:
- **Wall touch**: ALM injects 80 pA into AVD → AVA
- **FLP-11 inhibition**: ~15 pA on AVA
- **Result**: 80 pA >> 15 pA → touch wins → worm reverses even during sleep
- Weak stimuli (e.g., small gradient change) < 15 pA → sleep maintained

## Diag Results

```
t=20:   fatigue=0.16, slp=0, speed=0.27 → active foraging
t=60:   fatigue=0.47, slp=0, speed=0.23 → still active
t=80:   fatigue=0.63, slp=0, speed=0.19 → nearing threshold
t=100:  fatigue=0.65, slp=1, speed=0.012 → SLEEP ONSET ✅ (near-zero!)
t=140:  fatigue=0.33, slp=1, speed=0.019 → deep sleep
t=180:  fatigue=0.17, slp=1, speed=0.026 → fatigue nearly cleared
t=200:  fatigue=0.25, slp=0, speed=0.24  → WAKE UP ✅
t=260:  fatigue=0.69, slp=0, speed=0.06  → fatigue building again
t=280:  fatigue=0.55, slp=1, speed=0.016 → SECOND SLEEP ✅

RIS: V=-20mV, I_ext=25pA, FLP-11=0.95 (during sleep)
Sleep episodes: 2
Total sleep: ~120s (40% of 300s)
Fatigue range: [0.000, 0.700]
Awake speed: 0.19-0.27 mm/s | Sleep speed: 0.01-0.03 mm/s (~10:1 ratio)
```

### Sleep-Wake Cycle Timeline

```
0─────120s──────200s──────280s──────300s
 AWAKE          SLEEP      AWAKE     SLEEP
 foraging       quiescent  foraging  quiescent
 fatigue↑       fatigue↓   fatigue↑  fatigue↓
 speed~0.2      speed~0.02 speed~0.2  speed~0.02
```

## Key References

- **Turek et al. 2016 eLife** — FLP-11 is major sleep inducer, not GABA
- **Konietzka et al. 2020 Nat Commun** — RIS as locomotion stop neuron
- **Maluck et al. 2023 PLOS Genetics** — RIS survival independent of sleep behavior
- **Nagy et al. 2014 eLife** — Sleep homeostasis dual mechanisms
- **Saper et al. 2005** — Flip-flop switch model of sleep/wake
- **2025 Current Biology** — FLP-11 self-inhibition → spontaneous awakening
- **Emmons 2024 PLOS Biology** — RIS in community 4 connectome

## Modified Files

- `src/connectome/connectome_loader.cpp` — RIS neuron + synapses + gap junctions
- `src/simulation/simulation_engine.h` — fatigue_, ris_id_, is_sleeping_, accessors
- `src/simulation/simulation_engine.cpp` — update_fatigue(), apply_sleep_effects(), step() integration
- `src/simulation/diag_main.cpp` — fatigue/sleep time series (actual speed) + Section 18 diagnostic

## Step 27b: Sleep Speed Fix

Initial implementation had residual speed ~0.04-0.07 mm/s during sleep. Root cause:
1. **Ventral cord motor neurons** (DA/DB/VA/VB/DD/VD) not inhibited by FLP-11
2. **head_tonic = 3pA** always on, kept head oscillator alive
3. **Speed factor 0.9** (10% residual) too generous

Fix: added Effect 5 (ventral cord -30pA), head_tonic suppression, speed factor 0.97.
Result: sleep speed 0.01-0.03 mm/s (near-zero).

## Regression

- **regtest**: 12 pass, 0 FAIL (sleep doesn't trigger in 30s test)
- **Neuron count**: 83 → 84 (1 new: RIS)
- **Synapse count**: +6 chemical (RIS⊣AVA×2, RIS⊣AVB×2, RIS⊣AIB×2)
- **Gap junctions**: +4 (RIS↔AIB×4)
