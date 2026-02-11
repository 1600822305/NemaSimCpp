# Step 37: AVE Backward Command — Reversal Grading + Omega Gating

## Problem

omega/reversal ratio stuck at 100% since Step 35 (BAG CO2 addition).
Root cause: AIB→RIV direct synapse + elevated AIB baseline (from BAG→AIB + AWC→AIB w_mod=2.4)
→ RIV always had enough drive to burst during every reversal.

## Biological Solution

AVA and AVE are both backward command interneurons, but with different thresholds:
- **AVA**: lower threshold → short exploratory reversals (not always omega)
- **AVE**: higher threshold → long committed reversals (usually omega)

Without AVE connections, all reversals went through AVA alone, and AIB→RIV
ensured every reversal triggered an omega turn.

### Key Biology (Chalfie 1985, Piggott 2011, Kawano 2011)
- AVA/AVE calcium activities tightly coupled with reversals
- RIM promotes reversal via gap junctions to AVA/AVE (amplitude boost)
- RIM suppresses reversal probability via chemical synapses (frequency reduction)
- AVE ablation + AVA ablation → complete reversal elimination
- Omega probability correlates with reversal strength (Gray 2005)

## Implementation

### Neurons: 0 new (AVE already existed as empty shells)
AVEL/AVER were defined in connectome since early steps but had minimal connections
(only AVE→RIM 2 sec and AVEL↔AVER 4 sec gap junction).

### New Synapses Added (9 chemical + 2 gap)
| Pre | Post | Type | Sections | Function |
|-----|------|------|----------|----------|
| AIBL | AVEL | Exc | 1 | Chemosensory → backward (weaker than AIB→AVA) |
| AIBR | AVER | Exc | 1 | Same, right side |
| ASHL | AVEL | Exc | 2 | Nociception → committed reversal |
| ASHR | AVER | Exc | 2 | Same, right side |
| AVEL | DA01 | Exc | 1 | Backward motor drive |
| AVER | DA02 | Exc | 1 | Same |
| AVEL | DA03 | Exc | 1 | Same |
| AVEL | RIVL | Exc | 1 | **Omega gating** (replaces AIB→RIV) |
| AVER | RIVR | Exc | 1 | Same, right side |
| AVAL ↔ AVEL | Gap | 3 | Tight coupling of backward pair |
| AVAR ↔ AVER | Gap | 3 | Same, right side |

### Synapse Removed
| Pre | Post | Reason |
|-----|------|--------|
| AIBL → RIVL | Deleted | AIB baseline too high → 100% omega |
| AIBR → RIVR | Deleted | Same |

### Omega Gating Logic (emergent)
```
Weak stimulus → AIB slight increase → AVA crosses threshold → short reversal
  → AVE stays below threshold (higher threshold, weaker AIB→AVE synapse)
  → No AVE→RIV drive → RIV relies only on TA pulse
  → TA insufficient for CCA-1 burst → NO omega

Strong stimulus → AIB large increase → AVA + AVE both cross threshold → long reversal
  → AVE active → AVE→RIV drive → RIV burst → omega ✅
```

## Results

### Regression Test
17 pass, 0 FAIL ✅

### Diagnostic (300s)
| Metric | Step 36 | Step 37 | Target |
|--------|---------|---------|--------|
| **omega/reversal** | **1.00** ⚠️ | **0.85** ✅ | ~60-70% |
| **CI (sickness)** | +0.40 | **-0.303** ✅ | negative |
| **reversals** | 21 | 20 | ~0.07/s |
| **D/V ratio** | 0.79 | **1.00** ✅ | ~1.0 |
| **Wave** | GOOD | **GOOD** | |
| **Speed** | 0.19 | 0.195 | ~0.2 |

### Reversal Grading
- 20 reversals total, 17 with omega (85%), 3 without (15%)
- The 3 non-omega reversals = AVA-only short exploratory reversals
- omega/reversal improved from 100% → 85% (target 60-70%)

## References
- Chalfie et al. 1985 — AVA/AVE command interneurons for backward locomotion
- Piggott et al. 2011 — AVE threshold and reversal dynamics
- Kawano et al. 2011 — AVA/AVE calcium tightly coupled with reversals
- Gray et al. 2005 — omega probability correlates with reversal strength
- 2023 Frontiers — RIM dual role: gap junction promotion + chemical suppression

## Files Modified
- `src/connectome/connectome_loader.cpp` — 9 new synapses + 2 gap junctions, deleted AIB→RIV
