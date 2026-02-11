# Step 34: O₂ Sensing — URX/AQR/PQR + AUA Relay

## Biological Basis

### O₂ as Food Proxy
Bacteria consume O₂ → food areas have lower O₂ (7-12% vs 21% air).
O₂ concentration is a **proxy signal for food presence**, complementing olfactory cues.

### O₂ Sensing Neurons
- **URX L/R**: Primary O₂ sensors in the head, exposed to pseudocoelomic fluid
  - gcy-35/gcy-36 soluble guanylate cyclase → cGMP → TAX-2/TAX-4 channel
  - Activated by HIGH O₂ (>14%) — hyperoxia avoidance
  - **Cholinergic** (ACh), NOT glutamatergic (WormAtlas: cha-1+/unc-17+)
- **AQR**: Anterior body cavity O₂ sensor (single, unpaired, glutamatergic)
- **PQR**: Posterior body cavity O₂ sensor (single, unpaired, glutamatergic)
  - PQR at tail: high O₂ → accelerate forward without turning (Busch 2012)

### AUA — Integration Hub
- Receives from URX (O₂) + ADF (serotonin)
- Outputs to AVA (reversal) + AVB (speed)
- Key integration: O₂ and food signals converge here
- **Glutamatergic** (eat-4+)

### NPR-1 — The "Personality" Switch
- NPR-1 215V (N2 Bristol): HIGH activity → tonically inhibits URX
  - On food: virtually indifferent to O₂ (Chang 2006)
  - Off food: some hyperoxia avoidance remains
- NPR-1 215F (Hawaiian): LOW activity → strong O₂ response → aggregation
- Modeled as tonic inhibition: -15pA × satiety (fed = strong suppression)

**REF:** Gray 2004 Nature, Cheung 2005 Neuron, Chang 2006 PLoS Biology, Laurent 2015 eLife

## What Was Added

### Neurons (6 new, 99→105 total)
| Neuron | Type | NT | Role |
|--------|------|-----|------|
| URXL | Sensory | ACh | Head O₂ sensor (high O₂ activated) |
| URXR | Sensory | ACh | Head O₂ sensor |
| AQR | Sensory | Glutamate | Anterior body cavity O₂ |
| PQR | Sensory | Glutamate | Posterior body cavity O₂ |
| AUAL | Inter | Glutamate | O₂ relay/integration |
| AUAR | Inter | Glutamate | O₂ relay/integration |

### Synaptic Connections (11 new)
| Synapse | Sections | Function |
|---------|----------|----------|
| URXL → AUAL | 2 | Primary O₂ relay |
| URXR → AUAR | 2 | Primary O₂ relay |
| AUAL → AVAL | 2 | O₂ → backward command |
| AUAL → AVAR | 1 | O₂ → backward command |
| AUAR → AVAL | 1 | O₂ → backward command |
| AUAR → AVAR | 2 | O₂ → backward command |
| URXL → AVBL | 1 | Direct speed modulation |
| AQR → AVAL | 1 | Head O₂ → reversal |
| AQR → AVAR | 1 | Head O₂ → reversal |
| PQR → AVAL | 1 | Tail O₂ → reversal |
| PQR → AVAR | 1 | Tail O₂ → reversal |

### O₂ Field (no new class needed)
```
O₂(x) = 21% - 13% × min(food_density(x), 1.0)
At peak food: O₂ ≈ 8%
No food: O₂ = 21% (air)
Hyperoxia threshold: 14%
```

### O₂ Transduction
```
O₂ field: O₂(x) = 21% - 13% × food_density(x)  (σ≈3mm, NOT volatile odor σ=12mm)
URX: I_ext = 30pA × (O₂ - 14%) / 7%   when O₂ > 14%
     I_ext = 0                           when O₂ ≤ 14%
NPR-1: I_inh = -25pA (constant, N2 constitutively active)
  At 21% O₂: 30 - 25 = 5pA net (barely active) ✔
Net: urx_net = max(O₂_drive + NPR-1_inh, 0)

AQR/PQR: 50% of URX gain, 50% NPR-1 inhibition
```

### Research Corrections Applied
1. **URX → AVA does NOT exist** (WormWiring) → URX → AUA → AVA
2. **AQR → AIY not supported** → AQR → AVA (Chang 2006 Fig 8A)
3. **URX is cholinergic**, not glutamatergic (WormAtlas: cha-1+)
4. **AUA counted in neuron total** (6, not 4)
5. **NPR-1 acts primarily on RMG** output (Laurent 2015), modeled as URX tonic inhibition for simplicity

## Results

| Metric | Step 33 | Step 34 | Notes |
|--------|---------|---------|-------|
| Neurons | 99 | 105 (+6) | 30 sensory, 29 inter, 46 motor |
| regtest | 17 pass | 17 pass | ✅ |
| D/V ratio | 1.06 | 1.10 | ✅ maintained |
| O₂ mean | N/A | 19.3% | Mostly away from food center (σ=3mm) |
| URX release | N/A | 0.241 | Weak — NPR-1 -25pA suppression working |
| AUA release | N/A | 0.357 | Moderate — integration hub |
| CI | variable | -0.1 to 0.4 | Variable (trajectory-dependent) |
| Wave | GOOD | GOOD | ✅ |

### Emergent Behavior
- **Everywhere (N2)**: NPR-1 -25pA constitutive → URX max 5pA at 21% O₂ → near-silent
- **Future Hawaiian**: Remove NPR-1 → URX 30pA at 21% → strong hyperoxia avoidance + aggregation
- **Near food edge**: O₂ rises as worm leaves food zone → mild URX activation → subtle course correction
- **No hard-coded speed modulation**: O₂ → URX → AUA → AVA/AVB path handles it via synaptic drive

## Not Implemented (Future)
- **RMG hub** (social behavior, aggregation — Hawaiian strain)
- **BAG neurons** (low O₂ sensors, counterbalance URX)
- **SDQ/ALN/PLN** (redundant O₂ sensors)
- **URX ↔ RMG gap junction** (needs RMG neuron first)

## Files Changed
- `src/connectome/connectome_loader.cpp` — 6 neurons + 11 synapses
- `src/simulation/simulation_engine.h` — urx_ids_, aqr_id_, pqr_id_, o2_gain_, npr1_tonic_
- `src/simulation/simulation_engine.cpp` — O₂ transduction in apply_touch_stimulus()
- `src/simulation/diag_main.cpp` — O₂ diagnostic section (#22)
- `docs/steps/step34_oxygen.md` — this file
