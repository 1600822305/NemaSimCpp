# Step 24: Pharyngeal Pumping System

## Overview

Replaced the placeholder satiety mechanism (`dist < 3mm → satiety += dt/τ`) with a biologically
realistic pharyngeal pumping system. The pharynx is an independent neuromuscular pump that ingests
bacteria through rhythmic contractions driven by a dedicated nervous system.

## Biological Background

### Pharyngeal Nervous System
- **20 neurons, 14 types** — independent from the somatic NS (282 neurons)
- Connected to somatic NS only via **RIP ↔ I1 gap junctions** (bilateral pair)
- Pharyngeal muscle can pump without nervous system (intrinsic pacemaker ~1 Hz)
- With MC pacemaker: ~4 Hz on food, regulated by 5-HT/OA

### Three Essential Motor Neurons (Avery & Horvitz 1987; Raizen & Avery 1994)
| Neuron | Count | Transmitter | Function |
|--------|-------|-------------|----------|
| **MC** | 2 (L/R) | ACh (nicotinic, EAT-2) | Pacemaker — controls pump rate |
| **M3** | 2 (L/R) | Glu (AVR-15 Cl⁻) | Proprioceptive — controls relaxation timing |
| **M4** | 1 | ACh + peptides | Isthmus peristalsis — food transport |

### NSM (already in model, Step 20)
- Detects bacteria in pharynx → releases 5-HT → slowing + pump stimulation
- Redundant with MC for pump rate control

### 5-HT / OA Modulation
```
5-HT pathway (promotes pumping):
  ADF/NSM → 5-HT → SER-7 receptor on MC → ↑ACh release → ↑pump rate
  REF: Song & Avery 2012 eLife, Hobson 2006 Genetics

OA pathway (inhibits pumping):
  RIC → OA → MC inhibition → ↓pump rate
  REF: Niacaris & Bhatt 2003

Pump rates:
  On food + 5-HT: 200-300 pumps/min (3-5 Hz)
  Off food / no MC: ~60 pumps/min (~1 Hz intrinsic)
```

### Pharyngeal Muscle Action Potential
- Similar to vertebrate cardiac muscle
- Phases: E (excitation, CCA-1) → P (plateau, EGL-19) → R (repolarization, AVR-15 + K⁺)
- Resting: -45 mV, Peak: +30 mV, Plateau: +20 mV, Undershoot: -55 mV

## Implementation

### New Neurons (9 total, 83 neurons total in model)
```
MCL, MCR   — pharyngeal motor, ACh (pacemaker)
M3L, M3R   — pharyngeal motor, Glu (relaxation)
M4         — pharyngeal motor, ACh (isthmus peristalsis)
I1L, I1R   — pharyngeal inter (somatic bridge)
RIPL, RIPR — extrapharyngeal inter (bridge to I1)
```

### New Synapses
```
I1 → MC  (3 sections): relay extrapharyngeal signals to pacemaker
MC → M3  (2 sections): MC activity drives M3 proprioceptive firing
M3 → MC  (1 section):  weak feedback (simplified — real path is via muscle)
MC → M4  (2 sections): pumping activates isthmus peristalsis
```

### New Gap Junctions
```
I1L ↔ I1R  (2): left-right coupling
MCL ↔ MCR  (3): pacemaker synchronization
M3L ↔ M3R  (2): relaxation synchronization
RIPL ↔ I1L (2): somatic ↔ pharyngeal bridge (sole connection!)
RIPR ↔ I1R (2): somatic ↔ pharyngeal bridge
```

### PharyngealPump Model (`src/pharynx/pharyngeal_pump.h`)

4-phase state machine modeling pharyngeal muscle action potential:

```
RESTING → EXCITATION → PLATEAU → REPOLARIZATION → RESTING
  ↑ MC fires    ↑ CCA-1 Ca²⁺   ↑ EGL-19 Ca²⁺    ↑ AVR-15 Cl⁻ (M3)
  |              5 ms            20-300 ms          15 ms
  └──────── refractory period (200-800 ms) ────────┘
```

**Key parameters:**
- `refractory_base_ms_ = 800`: no MC → ~1.2 Hz (intrinsic)
- `refractory_min_ms_ = 200`: max MC → ~4 Hz (on food + 5-HT)
- `food_per_pump_ = 0.006`: satiety increment per pump at conc=1.0
- MC-modulated refractory: `effective_refractory = base - (base - min) × mc_release`

### Neuromodulation Integration
```
MC tonic drive = 3.0 (baseline)
               + 8.0 × food/(food+0.1)     (mechanosensory: bacteria in pharynx)
               + 15.0 × [5-HT]              (SER-7 excitation)
               - 10.0 × [OA]                (inhibition)

M3 proprioceptive = 12 pA during PLATEAU, 5 pA during EXCITATION
M4 = 2.0 + 8.0 × [5-HT]  (5-HT also activates M4)
```

### Real Satiety (replaces placeholder)
```
OLD: satiety += (on_food - satiety) × dt / tau_fill    ← PLACEHOLDER
NEW: if pump_event: satiety += food_per_pump × food_conc  ← REAL INGESTION

Depletion: satiety -= satiety × dt × rate / tau_deplete
  rate = 1.0 off food, 0.5 on food (metabolic)
  tau_deplete = 40 s
```

### 5-HT Positive Feedback Loop
```
Food → NSM 5-HT → MC excitation → ↑pump rate → ↑food intake
                                                     ↓
                ↑satiety ← ← ← ← ← ← ← ← ← ← ← ←┘
                     ↓
              NSM suppression → ↓5-HT → ↓pump rate → ↓intake
              (insulin/DAF-2)
```

## Results

### Pharyngeal Diagnostics (300s simulation)
```
Pump rate: ~2-3 Hz (food dependent)
Total pumps: ~800 in 300s
MC: -30 to -40 mV (oscillating)
M3: -40 mV (fires during pump plateau)
M4: -30 to -40 mV (activates after pumping)
```

### Satiety Dynamics
- Satiety oscillates around 0.4-0.55 (crosses threshold reliably)
- FOOD → TEMP mode switching occurs at sat=0.5
- CI ≈ 0.4-0.5 (comparable to Step 23c results)
- X displacement: ~4-5 mm (food↔Tc oscillation preserved)

### Known Simplifications
1. M3→MC synapse uses default excitatory E_syn (-10mV) instead of AVR-15 Cl⁻ (-70mV)
   - Real M3 inhibition modeled through PharyngealPump state machine instead
2. Pharyngeal muscle AP simplified to 4-phase state machine (not full HH model)
3. M4 isthmus peristalsis modeled as every-4th-pump (not full state machine)
4. 10 of 14 pharyngeal neuron types omitted (I2-I6, M1-M2, M5, MI)

## References
- Albertson & Thomson (1976) — pharyngeal anatomy and connectome
- Avery (WormBook 2012) — comprehensive pharyngeal feeding review
- Avery & Horvitz (1987) — M4 essential for growth
- Raizen & Avery (1994) — MC/M3 electrophysiology
- Song & Avery (2012) eLife — 5-HT/SER-7 → MC → pump rate
- Hobson (2006) Genetics — SER-7 necessary for 5-HT stimulation
- Niacaris & Bhatt (2003) — OA suppresses pumping
- Cook et al. (2020) — pharyngeal connectome revision
- You et al. (2008) — satiety quiescence
