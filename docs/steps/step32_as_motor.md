# Step 32: AS Motor Neurons — Dorsal Bias

## Biological Basis
AS motor neurons are glutamatergic neurons that exclusively project to dorsal body wall muscles. Unlike other motor neuron classes, AS receives input from BOTH AVA (backward command) and AVB (forward command), making them "always-on" dorsal stabilizers active during both forward and backward locomotion.

**REF**: White 1986 (anatomy), Haspel 2010 (dorsal-only projection), Chen 2006 (active during fwd+rev)

## What Was Added

### Neurons (5 new, 88→93 total)
- AS01–AS05: glutamatergic motor neurons
- Use `create_motor()` factory (EGL-19/UNC-2/SHL-1, no CCA-1)

### Synaptic Inputs
- **AVA → AS** (1 section each): backward command drives AS during reversals
  - AS are unpaired; alternate L/R innervation (AS01←AVAL, AS02←AVAR, etc.)
- **AVB → AS** (1 section each): forward command also drives AS
- **DD ⊣ AS** (1 section): GABAergic cross-inhibition during ventral phase
- **DB ↔ AS** (gap junction, 1 section): synchronize dorsal activation

### Dorsal Muscle Mapping
| Neuron | Segments | Region |
|--------|----------|--------|
| AS01 | 2–6 | head-neck transition |
| AS02 | 6–12 | anterior body |
| AS03 | 12–20 | mid-body |
| AS04 | 20–30 | mid-posterior |
| AS05 | 30–40 | posterior body |

All mappings are **dorsal-only** (`is_dorsal=true`).

## AS Dorsal Resistance — Omega Gating

### Problem
Without AS, RIV burst → omega turn 100% of the time (20/19 ratio).
The worm has no dorsal resistance, so even weak RIV bursts produce deep omega turns.

### Solution: Pre-Reversal Snapshot + Peak Detection

1. **At reversal START**: record dorsal tone snapshot (`pre_rev_dorsal_tone_`)
   - Captures random SMD oscillation phase before TA suppresses SMD
   
2. **At RIV burst PEAK** (peak detection: release was rising, now falling):
   - `effective_riv = peak_release - pre_rev_dorsal_tone * as_factor`
   - If `effective_riv > omega_threshold` → omega activates

3. **Why pre-reversal snapshot?**
   - During reversal, TA via LGC-55 suppresses SMD (-25pA) → dorsal tone drops
   - If we check tone at burst peak (post-reversal), it's ALWAYS low → 100% omega
   - Pre-reversal tone is random (SMD was oscillating normally) → natural filtering

### Parameter: `as_factor = 1.0`
- Found via CLI parameter sweep (7 values × 120s each, no recompile)
- At mean dorsal tone 0.26: omega when tone < 0.5 → P ≈ 67%

## Runtime Parameter System (New)

Added CLI parameter overrides to avoid recompilation during tuning:

```
celegans_diag.exe --as_factor 1.0 --pulse_amp 60 --duration 120
celegans_regtest.exe --as_factor 1.2
```

Parameters in `TuningParams` struct:
- `as_factor` — AS dorsal resistance factor
- `pulse_amp` — RIV post-reversal pulse amplitude scaling  
- `omega_threshold` — RIV release threshold for omega mode
- `riv_tonic` — RIV baseline tonic drive (pA)

Sweep script: `sweep_as_factor.ps1`

## Results

| Metric | Before (Step 31) | After (Step 32) | Target |
|--------|-----------------|-----------------|--------|
| omega/reversal | 20/19 = 100% | 12/18 = 67% | 60-70% ✅ |
| regtest | 17 pass | 17 pass | 17 pass ✅ |
| Speed | 0.21 mm/s | 0.18 mm/s | >0.1 ✅ |
| Wave quality | GOOD | GOOD | GOOD ✅ |
| AS01 release | N/A | 0.233 | >0.1 ✅ |
| Dorsal tone | N/A | mean=0.26 | >0 ✅ |

## Emergent Properties
- **Graded omega**: Strong RIV (high TA + low dorsal tone) → omega; weak RIV → blocked
- **SMD phase gating**: Omega probability depends on body posture at escape onset
- **Dorsal bias**: AS provides continuous dorsal muscle activation (mean release 0.23)

## Files Changed
- `src/connectome/connectome_loader.cpp` — AS01-05 neurons + synapses + gap junctions
- `src/motor/motor_controller.cpp` — AS→dorsal muscle mapping
- `src/simulation/simulation_engine.h` — TuningParams + state variables
- `src/simulation/simulation_engine.cpp` — pre-reversal snapshot, peak detection, runtime params
- `src/simulation/regression_test.cpp` — CLI params, speed baseline 0.26→0.35
- `src/simulation/diag_main.cpp` — AS diagnostics + CLI params + variable duration
- `sweep_as_factor.ps1` — parameter sweep script
