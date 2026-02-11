# Step 35: BAG CO₂ Sensing

## Biological Background

BAG neurons (2, glutamatergic) are the primary CO₂ sensors in C. elegans.
They detect CO₂ via the receptor guanylate cyclase GCY-9, which activates
TAX-2/TAX-4 cGMP-gated channels (Hallem & Sternberg 2008 PNAS).

### Key Biology
- CO₂ is produced by bacteria → high CO₂ in food zone, low in open air
- BAG responds phasically: sensitive to CO₂ changes (dCO₂/dt)
- OFF rebound: CO₂ decrease → transient burst (escape acceleration)
- N2: NPR-1 suppresses URX → URX doesn't inhibit CO₂ circuit → **N2 avoids CO₂**
- npr-1(lf): URX active → suppresses CO₂ avoidance (Carrillo 2013 J Neurosci)

### O₂-CO₂ Antagonism
```
Food zone:
  O₂ low  → URX silent → "stay" (comfort)
  CO₂ high → BAG active → "leave" (aversion)
  
Balance depends on state:
  Hungry: O₂ comfort + hunger > CO₂ aversion → stay and eat
  Fed+sick: CO₂ aversion + sickness > O₂ comfort → leave
```

## Implementation

### Neurons Added (105 → 107)
| Neuron | Type | NT | Function |
|--------|------|-----|----------|
| BAGL | Sensory | Glu | Left CO₂ sensor |
| BAGR | Sensory | Glu | Right CO₂ sensor |

### Synapses Added (6 new)
| Pre | Post | Type | Sections | Function |
|-----|------|------|----------|----------|
| BAGL | AIYL | Inh | 1 | CO₂ → suppress forward |
| BAGR | AIYR | Inh | 1 | CO₂ → suppress forward |
| BAGL | AIBL | Exc | 1 | CO₂ → promote turning |
| BAGR | AIBR | Exc | 1 | CO₂ → promote turning |
| BAGL | RIAL | Exc | 1 | Head turning modulation |
| BAGR | RIAR | Exc | 1 | Head turning modulation |

### CO₂ Field
```
CO₂(x) = 0.04% + 3% × food_density(x)
Range: [0.04%, 3.04%]  (ambient to bacterial peak)
Source: sample_food_density() (σ≈3mm, same as O₂)
```

### BAG Transduction
```
Tonic: I = 40pA × (CO₂ - 0.5%) / 3%   when CO₂ > 0.5%
Phasic (rising):  I += 20 pA × dCO₂/dt  (entering food zone)
Phasic (falling): I += -10 pA × dCO₂/dt (OFF rebound, escape)
Clamp: max 60 pA

URX cross-inhibition: I_inh = 30 × sum(URX_release)
  N2: URX S≈0.15 → 4.5pA inhibition (weak, BAG works)
  npr-1(lf): URX S≈0.5+ → 15+pA inhibition (BAG suppressed)
```

### Step 34 Fixes (concurrent)
- AUA→AVA: 2+1 sections → **0.3 sections** (NPR-1 presynaptic inhibition)
- URX NPR-1: -25 → **-28 pA** (stronger suppression)
- AUA NPR-1: **-12 pA** (proxy for missing RMG suppression)
- REF: Laurent 2015 eLife — NPR-1 inhibits RMG output downstream of Ca2+

## Results

### Regression Test
17 pass, 0 FAIL ✅

### Diagnostic (300s)
| Metric | Before (Step 34) | After (Step 35) | Notes |
|--------|-----------------|-----------------|-------|
| CI (sickness=1) | +0.57 | **+0.08** | CO₂ counteracts O₂ attraction |
| Reversals | 12 | **21** | BAG→AIB→AVA increases turning |
| near_food | 23.7% | **13.2%** | Less time trapped near food |
| CO₂ mean | N/A | 0.84% | Field working |
| BAGL S | N/A | 0.207 | Active, moderate |
| O₂ mean | 17.4% | 17.5% | Unchanged |
| URXL S | 0.158 | 0.151 | Still suppressed by NPR-1 |
| Wave | GOOD | GOOD | No regression |

### Emergent Behavior
- **Hungry worm**: arrives at food → CO₂ rises but hunger dominates → stays
- **Fed+sick worm**: CO₂ aversion + pathogen learning → pushes away from food
- **O₂ vs CO₂**: antagonistic signals balanced by internal state (satiety, sickness)
- **Reversal rate**: 0.04→0.07/s — BAG→AIB pathway increases exploratory turning

## References
- Hallem & Sternberg 2008 PNAS — BAG CO₂ sensing, acute avoidance
- Bretscher et al. 2011 Neuron — BAG multimodal (CO₂, O₂, salt, temp)
- Carrillo et al. 2013 J Neurosci — URX controls CO₂ response via NPR-1
- Laurent et al. 2015 eLife — NPR-1 inhibits RMG downstream of Ca2+
- Chang et al. 2006 PLoS Biology — Distributed O₂/CO₂ circuit

## Files Modified
- `src/connectome/connectome_loader.cpp` — 2 neurons + 6 synapses
- `src/simulation/simulation_engine.h` — bag_ids_, co2_gain_, co2_threshold_
- `src/simulation/simulation_engine.cpp` — CO₂ transduction block
- `src/simulation/diag_main.cpp` — CO₂ diagnostic section #23
