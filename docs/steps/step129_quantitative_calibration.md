# Step 129: Quantitative Calibration — Ablation-Guided CI Improvement

## Problem
Baseline chemotaxis index was CI = 0.010 ± 0.009 (8 seeds, 300s).
This is far below biological values (~0.3-0.5, Bargmann 1993).

## Diagnostic Approach: Virtual Laser Ablation
Built `ablation_analyzer` tool — systematically ablates 20 neuron targets and measures CI change.
This identified the root causes rather than guessing parameters.

### Key Ablation Findings (pre-fix baseline)

| Ablation | ΔCI | Phenotype | Interpretation |
|----------|-----|-----------|----------------|
| AWC | 0.000 | neutral | **Sensory signal disconnected from behavior!** |
| ASE | +0.009 | neutral | Taste doesn't influence decisions |
| AIY | +0.014 | CI UP (wrong!) | AIY over-suppressing reversals |
| RIA | +0.004 | neutral | Klinotaxis pathway non-functional |
| AVA | -0.073 | Rev=0% ✓ | Classic phenotype correct |
| RIV | -0.059 | Omega=0.1% ✓ | Classic phenotype correct |
| NSM | +0.006 | HURTING CI | 5-HT over-suppressing speed/reversals |

**Root cause**: CI came entirely from random omega reorientation bias, not from neural chemotaxis.

## Bugs Found

### Bug 1: `reversal_rate_scale_` was dead code
5-HT REVERSAL_RATE modulation (-50%) was computed in neuromodulation but **never consumed**.
The Schmitt trigger used a fixed threshold of 0.35 regardless of neuromodulatory state.

**Fix**: Wire `reversal_rate_scale_` into the Schmitt trigger entry threshold:
```cpp
double entry_thresh = 0.45 / std::max(rev_scale, 0.3);
```
On food (5-HT ~0.65): entry threshold rises from 0.45 to ~0.69 → fewer reversals.

### Bug 2: Missing SER-4 → AVA direct inhibition
No 5-HT receptor directly on AVA command interneurons. The only reversal suppression
was through the dead-code REVERSAL_RATE mechanism.

**Fix**: Added SER-4 (Gαi/o inhibitory) → AVA at -3pA.
REF: Harris 2009 J Neurosci, Flavell 2013 Cell, Dag & Flavell 2023 Cell.

### Bug 3: 5-HT muscle_gain too aggressive
MUSCLE_GAIN = -0.60 cut speed by 60% on food (0.069 vs 0.092 mm/s off food).
Biological slowing is ~30% (Sawin 2000).

**Fix**: -0.60 → -0.30.

## Parameter Changes

| Parameter | Before | After | Rationale |
|-----------|--------|-------|-----------|
| Reversal entry thresh | 0.35 (fixed) | 0.45 / rev_scale | Wire 5-HT modulation |
| Reversal exit thresh | 0.15 | 0.25 | Faster reversal exit |
| Max reversal duration | 2000 ms | 1200 ms | Bio: ~1s (Wakabayashi 2004) |
| Refractory period | 2000 ms | 1500 ms | Natural reversal rhythm |
| SER-4 → AVA | (none) | -3 pA | Missing pathway |
| 5-HT MUSCLE_GAIN | -0.60 | -0.30 | Bio: ~30% slowing |
| Klinokinesis dC/dt gain | 300 | 800 | Ablation: AWC had zero CI effect |
| Klinokinesis dC/dt clamp | 3 pA | 10 pA | Signal too weak for AVA |
| Klinokinesis tonic (no-grad) | 1.0 pA | 0.3 pA | Reduce tonic reversal drive |
| Sat weathervane suppression | 0.85 | 0.75 | Marginal (SMD linear limit) |

## Key Discovery: SMD Oscillator Linear Regime
The weathervane bias must stay below ~60 effective pA/(conc/mm) or the SMD oscillation
gets captured and WV_slope flips negative (anti-chemotaxis). This severely limits
klinotaxis contribution. Future work should widen the SMD linear regime.

## Results (8 seeds, 300s)

| Metric | Before | After | Target |
|--------|--------|-------|--------|
| CI | 0.010 ± 0.009 | 0.036 ± 0.003 | ~0.3-0.5 |
| Omega toward% | 71.2% | 93.5% | >55% |
| Klinokinesis | 0.076 | passes | >0.1 |
| WV slope | -0.29 | -0.72* | >0 |
| Speed | 0.175 | 0.189 | 0.1-0.3 |
| Converging | 3/8 | 4-5/8 | 8/8 |
| Bottlenecks | 5 | 4-5 | 0 |

*WV_slope is negative due to SMD nonlinear regime; weathervane contribution limited.

## Post-fix Ablation Phenotypes

| Ablation | ΔCI | Status |
|----------|-----|--------|
| AVA → Rev=0% | ✓ | Chalfie 1985 |
| RIV → Omega=0.1% | ✓ | Gray 2005 |
| AIY → CI drops | ✓ | Tsalik 2003 (was wrong before) |
| NSM → neutral | ✓ | No longer hurting CI |
| RIM → CI drops | ✓ | TA/omega pathway critical |

## Remaining Bottlenecks
1. **Heading bias** still slightly negative — SMD linear range limits klinotaxis
2. **Run ratio < 1** — klinokinesis modulation of run length still weak
3. **Time near food = 0%** — worm doesn't reach food in 300s at CI=0.036

## New Tool
- `ablation_analyzer` — 20 neuron targets × parallel execution, phenotype validation

## Files Modified
- `simulation_engine.cpp` — Schmitt trigger reversal_rate_scale wiring, entry/exit thresholds
- `setup_neuromodulation.cpp` — SER-4→AVA, muscle_gain reduction
- `update_internal_states.cpp` — klinokinesis gain/clamp
- `apply_motor_control.cpp` — satiety weathervane modulation, 5-HT SMD scale docs
- `simulation_engine.h` — weathervane_gain docs

## Files Created
- `src/diagnostics/ablation_analyzer_main.cpp`
- `docs/steps/step129_quantitative_calibration.md`
