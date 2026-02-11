// ================================================================
// Neuromodulation Setup — Split from simulation_engine.cpp (Step 50a)
//
// Configures all 6 neuromodulators: 5-HT, DA, OA, TA, NLP-12, PDF
// This file is part of the SimulationEngine class (same class, split source).
// ================================================================
#include "simulation/simulation_engine.h"
#include "core/logger.h"

namespace celegans {

void SimulationEngine::setup_neuromodulation() {
    // ================================================================
    // Step 20: Neuromodulation Layer (Layer 6) — "Wireless Connectome"
    //
    // Unlike synapses (point-to-point, ms), neuromodulators act via
    // volume transmission (diffuse, seconds-minutes).
    //
    // Two modulators for MVP:
    //   1. Serotonin (5-HT): food → NSM → dwelling (slow, low reversal)
    //   2. Dopamine (DA): food → CEP → basal slowing response
    //
    // REF: Flavell 2013 Cell — 5-HT/PDF roaming/dwelling
    //      Sawin 2000 — DA basal slowing response
    //      Chase & Koelle 2007 — monoamine signaling review
    // ================================================================

    // --- Serotonin (5-HT) ---
    // Source: NSM pharyngeal neurons (detect food via bacteria ingestion)
    // Effect: promotes dwelling state
    //   - MOD-1 on AIY: inhibitory Cl- channel → reduces AIY activity → less forward
    //   - SER-4 on AIB: inhibitory → reduces AIB → fewer pirouettes
    //   - Global: reduce speed slightly (enhanced slowing response)
    {
        Neuromodulator serotonin;
        serotonin.name = "5-HT";
        serotonin.tau_rise = 3000.0;    // 3s to build up (slow volume transmission)
        serotonin.tau_decay = 8000.0;   // 8s to clear (long-lasting dwelling)
        // Step 41: raised from 0.3 → 0.5 to prevent ADF baseline activity
        // from inflating off-food 5-HT (ADF tonic release ~0.45 exceeds 0.4)
        // Step 43: ADF removed as 5-HT source → high threshold no longer needed
        // Step 45: restored to 0.3 (original value) — with ADF gone, the original
        // concern is moot. NSM off-food S=0.05-0.20, well below 0.3 → no leak.
        // On-food NSM S=0.7-0.85 → drive=(0.7-0.3)/0.7=0.57 → strong dwelling.
        serotonin.release_threshold = 0.3;

        // Source neurons: NSM (pharyngeal, food detection) + ADF (pathogen learning)
        int nsml = connectome_.get_neuron_id("NSML");
        int nsmr = connectome_.get_neuron_id("NSMR");
        if (nsml >= 0) serotonin.source_neuron_ids.push_back(nsml);
        if (nsmr >= 0) serotonin.source_neuron_ids.push_back(nsmr);
        // Step 26→41: ADF removed as 5-HT source. ADF is a chemosensory neuron
        // (detects volatile odors) that fires tonically near food → inflates 5-HT
        // off-food. In real worms, ADF 5-HT release requires TPH-1 upregulation
        // specifically during pathogen exposure (Zhang 2005 Nature), not constitutive.
        // ADF pathogen signaling works via synapses (ADF→AIB) + sickness→weathervane.
        // Step 38: HSN as 5-HT source (egg-laying command motor neuron)
        // REF: Waggoner 1998 — HSN releases 5-HT to initiate egg-laying active state
        for (int hsn_id : nids("HSN")) {
            if (hsn_id >= 0) serotonin.source_neuron_ids.push_back(hsn_id);
        }

        // Target: AIY via MOD-1 (inhibitory Cl- channel)
        // 5-HT → MOD-1 on AIY → hyperpolarize → reduce forward drive
        // REF: Flavell 2013 — NSM 5-HT inhibits AIY
        // Step 43: recalibrated from -5.0 to -2.5 pA after removing buggy excitatory
        // ADF→AIY synapse (was +1.8pA at baseline). Old net: -5×0.73+1.8=-1.85pA.
        // New net: -2.5×0.73=-1.83pA (same baseline effect, no excitatory compensation)
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) serotonin.targets.push_back(
            {aiyl, "MOD-1", ModulationEffect::EXCITABILITY, -2.5});
        if (aiyr >= 0) serotonin.targets.push_back(
            {aiyr, "MOD-1", ModulationEffect::EXCITABILITY, -2.5});

        // Step 25: Target: AIB inhibition (suppress avoidance while feeding)
        // REF: Summers 2015 JNeurosci — 5-HT via MOD-1 (5-HT-gated Cl⁻) inhibits AIB
        // On food: 5-HT↑ → AIB↓ → animals continue forward despite repellent
        // Off food: 5-HT↓ → AIB active → full avoidance response
        for (int aib_id : nids("AIB")) {
            if (aib_id >= 0) serotonin.targets.push_back(
                {aib_id, "MOD-1", ModulationEffect::EXCITABILITY, -6.0}); // -6 pA inhibitory
        }

        // Target: reversal rate suppression (dwelling = fewer pirouettes)
        // On food: 5-HT → MOD-1 → fewer reversals → stay on food (dwelling)
        // Off food: no 5-HT → full reversal rate → area-restricted search
        // REF: Gray 2005 PNAS — off-food reversal rate 6/min vs on-food 3/min
        //      Campbell 2016 PLOS Genetics — wild-type 2× reversal rate off food
        //      Flavell 2013 Cell — 5-HT promotes dwelling (low reversal) state
        serotonin.targets.push_back(
            {-1, "MOD-1", ModulationEffect::REVERSAL_RATE, -0.50}); // 50% fewer reversals at peak 5-HT

        // Target: speed reduction (enhanced slowing on food)
        // Step 49: label fix SER-7→SER-4. SER-7 is pharynx-specific (Song & Avery 2012).
        // Speed reduction is mediated by SER-4 (Gαi/o) on locomotion circuit neurons.
        // Dag & Flavell 2023 Cell: SER-4 is one of three core inhibitory receptors for slowing.
        // REF: Sawin 2000 — serotonin reduces locomotion speed
        //      Dag & Flavell 2023 Cell Fig 2 — SER-4 core slowing receptor
        serotonin.targets.push_back(
            {-1, "SER-4", ModulationEffect::SPEED_SCALE, -0.40}); // behavioral state: dwelling generally slower (stacks with instant food-contact slowing)

        // Target: RIC inhibition (cross-inhibit OA source during dwelling)
        // 5-HT → SER-4 on RIC → inhibit → no OA during active dwelling
        // REF: Chase & Koelle 2007 — 5-HT/OA antagonism
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) serotonin.targets.push_back(
            {ricl, "SER-4", ModulationEffect::EXCITABILITY, -4.0}); // Step 48: -8→-4 pA (allow RIC activation when satiated)
        if (ricr >= 0) serotonin.targets.push_back(
            {ricr, "SER-4", ModulationEffect::EXCITABILITY, -4.0});

        // Step 49: SER-1 → RIA (excitatory Gαq GPCR)
        // SER-1 (5HT2ce) is prominently expressed in RIA head interneurons.
        // On food: 5-HT → SER-1 → RIA enhanced → modulates head curving during dwelling
        // Helps fine-tune klinotaxis for local food patch navigation while dwelling.
        // ser-1 mutant: defective food-induced slowing + changes direction more frequently
        // REF: Dernovici 2007 J Comp Neurol — ser-1::GFP in RIA and RIC
        //      Dag & Flavell 2023 Cell — SER-1 modulates slowing behavior
        int rial = connectome_.get_neuron_id("RIAL");
        int riar = connectome_.get_neuron_id("RIAR");
        if (rial >= 0) serotonin.targets.push_back(
            {rial, "SER-1", ModulationEffect::EXCITABILITY, 3.0}); // +3 pA excitatory (Gαq)
        if (riar >= 0) serotonin.targets.push_back(
            {riar, "SER-1", ModulationEffect::EXCITABILITY, 3.0});

        // Step 49: SER-1 → RIC (excitatory Gαq GPCR)
        // SER-1 also expressed in RIC (Dernovici 2007).
        // Creates push-pull with SER-4 (-4pA): net at peak 5-HT = -4+2 = -2 pA
        // Biological logic: prevents complete OA shutdown during dwelling.
        // Allows faster RIC recovery when 5-HT drops → quicker roaming transition.
        // REF: Dernovici 2007 — ser-1::GFP in RIC
        if (ricl >= 0) serotonin.targets.push_back(
            {ricl, "SER-1", ModulationEffect::EXCITABILITY, 2.0}); // +2 pA (partial SER-4 antagonism)
        if (ricr >= 0) serotonin.targets.push_back(
            {ricr, "SER-1", ModulationEffect::EXCITABILITY, 2.0});

        // Step 49: MOD-1 → AIZ (inhibitory 5-HT-gated Cl⁻ channel)
        // AIZ is in the cold-thermotaxis/avoidance pathway (Mori 1995).
        // 5-HT → MOD-1 ⊣ AIZ → suppress unnecessary thermotaxis exploration during dwelling.
        // Flavell 2013: mod-1 mutants show excessive roaming; MOD-1 inhibits roaming-promoting
        // neurons including AIZ (confirmed by mod-1 promoter expression).
        // REF: Flavell 2013 Cell — MOD-1 on roaming-promoting interneurons
        //      Ranganathan 2000 Nature — MOD-1 5-HT-gated Cl⁻ channel
        for (int aiz_id : nids("AIZ")) {
            if (aiz_id >= 0) serotonin.targets.push_back(
                {aiz_id, "MOD-1", ModulationEffect::EXCITABILITY, -3.0}); // -3 pA inhibitory
        }

        // Step 49: SER-5 → ASH (excitatory GPCR, 5HT6-like)
        // SER-5 in ASH required for 5-HT-dependent enhancement of nociceptive responses.
        // On food: 5-HT → SER-5 → ASH more sensitive to harmful chemicals.
        // Maintains chemical vigilance while dwelling/feeding.
        // Stacks with TYRA-3→ASH (+5pA from TA, escape sensitization) but different mechanism:
        //   SER-5 = tonic food-context sensitization, TYRA-3 = phasic escape sensitization.
        // ser-5 RNAi in ASH abolishes food/5-HT-dependent octanol sensitivity increase.
        // REF: Harris 2009 J Neurosci — SER-5 in ASH for aversive chemosensation
        //      Dag & Flavell 2023 Cell — SER-5 modulates slowing behavior
        int ashl = connectome_.get_neuron_id("ASHL");
        int ashr = connectome_.get_neuron_id("ASHR");
        if (ashl >= 0) serotonin.targets.push_back(
            {ashl, "SER-5", ModulationEffect::EXCITABILITY, 4.0}); // +4 pA excitatory (sensitize nociception)
        if (ashr >= 0) serotonin.targets.push_back(
            {ashr, "SER-5", ModulationEffect::EXCITABILITY, 4.0});

        // Step 49b: LGC-50 → RIA (excitatory 5-HT-gated CATION channel)
        // LGC-50 is the 6th and most recently identified 5-HT receptor (Morud 2021).
        // Unlike MOD-1 (Cl⁻ inhibitory), LGC-50 is a CATION channel (excitatory).
        // One of three CORE slowing receptors (Dag & Flavell 2023 Cell Fig 2).
        // Slowing mechanism is circuit-level: LGC-50 → RIA excited → enhanced head
        // steering → less efficient forward progression → net slowing.
        // LGC-50 in RIA is essential for pathogen avoidance learning:
        //   lgc-50 null mutants fail to learn PA14 avoidance (Morud 2021 Fig 7)
        //   LGC-50 trafficking to synapses modulated by olfactory conditioning
        // Modeled as SYNAPSE_GAIN (not EXCITABILITY) because LGC-50's primary role
        // is synaptic plasticity: receptor trafficking to synapses amplifies RIA output
        // during 5-HT release. This strengthens RIA→SMD klinotaxis signal → circuit-level
        // slowing (more head steering → less efficient forward progression) without
        // destabilizing RIA membrane potential (which SER-1 +3pA already modulates).
        // At 5-HT=0.5: gain = 1 + 0.15×0.5 = 1.075 (7.5% stronger RIA output).
        // REF: Morud 2021 Curr Biol — LGC-50 deorphanization, RIA expression, learning
        //      Dag & Flavell 2023 Cell — LGC-50 core slowing receptor
        // NOTE: rial/riar already resolved above for SER-1
        if (rial >= 0) serotonin.targets.push_back(
            {rial, "LGC-50", ModulationEffect::SYNAPSE_GAIN, 0.15}); // +15% RIA output at peak 5-HT
        if (riar >= 0) serotonin.targets.push_back(
            {riar, "LGC-50", ModulationEffect::SYNAPSE_GAIN, 0.15});

        // LGC-50 → AUA: DEFERRED. AUA is an O₂ relay neuron (Step 34).
        // Expression confirmed (Dag 2023 Fig S5B) but AUA modulation disrupts
        // O₂-dependent navigation. Requires proper O₂ context gating first.

        neuromod_.add_modulator(std::move(serotonin));
    }

    // --- Dopamine (DA) ---
    // Source: CEP head neurons (detect bacteria mechanically)
    // Effect: basal slowing response — slow down when encountering food
    //   - DOP-3 on motor neurons: inhibitory → reduces speed
    //   - DOP-1 on RIA: excitatory → enhances head oscillation (foraging)
    // REF: Sawin 2000 — CEP DA drives basal slowing
    //      Chase 2004 — DOP-3 inhibitory on locomotion
    {
        Neuromodulator dopamine;
        dopamine.name = "DA";
        dopamine.tau_rise = 2000.0;     // 2s to build up
        dopamine.tau_decay = 5000.0;    // 5s to clear
        dopamine.release_threshold = 0.3;

        // Source neurons: CEP (4 neurons, head mechanosensory)
        const char* cep_names[] = {"CEPDL", "CEPDR", "CEPVL", "CEPVR"};
        for (auto name : cep_names) {
            int id = connectome_.get_neuron_id(name);
            if (id >= 0) dopamine.source_neuron_ids.push_back(id);
        }

        // Step 47b: DA SPEED_SCALE removed — replaced by instant basal_slow mechanism
        // Old: DOP-3 SPEED_SCALE -0.30 via neuromod (tau_decay=5s → persists off-food → tanks CI)
        // New: basal_slow = 1.0 - 0.35 * da_gate * on_lawn (instant, position-dependent)
        // on_lawn sigmoid drops to 0 immediately when leaving food → no off-food penalty
        // REF: Sawin 2000 — cat-2 mutants fail to slow; DOP-3 inhibitory on motor neurons

        // Step 45: Target: DVA via DOP-1 (D1-like excitatory GPCR)
        // DA from CEP (food detection) → DOP-1 on DVA → stimulates NLP-12 release
        // On food: DA high → DVA excited → NLP-12 primed for ARS upon food departure
        // Off food: DA drops → DVA excitation from DA decreases (proprioception still drives DVA)
        // REF: Bhattacharya 2014 PLOS Genetics — DOP-1 in DVA regulates NLP-12 release
        //      dop-1 mutant: reduced NLP-12-Venus fluorescence change, impaired ARS
        if (nid("DVA") >= 0) dopamine.targets.push_back(
            {nid("DVA"), "DOP-1", ModulationEffect::EXCITABILITY, 4.0}); // +4 pA excitatory (primes DVA, not enough alone for NLP-12 release)

        neuromod_.add_modulator(std::move(dopamine));
    }

    // --- Octopamine (OA) ---
    // Source: RIC interneurons (tonically active, inhibited by 5-HT)
    // Effect: promotes roaming — increase speed, decrease reversal rate
    // OA is the functional antagonist of 5-HT
    // REF: Alkema 2005 — tyramine/octopamine in C. elegans locomotion
    //      Churgin 2017 — OA promotes roaming state
    {
        Neuromodulator octopamine;
        octopamine.name = "OA";
        octopamine.tau_rise = 2000.0;    // 2s to build up
        octopamine.tau_decay = 4000.0;   // 4s to clear (faster than 5-HT)
        octopamine.release_threshold = 0.3;

        // Source neurons: RIC (tonically active when off-food/satiated)
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) octopamine.source_neuron_ids.push_back(ricl);
        if (ricr >= 0) octopamine.source_neuron_ids.push_back(ricr);

        // Target: global speed increase (antagonizes 5-HT/DA slowing)
        // REF: Churgin 2017 — OA mutants have reduced roaming
        octopamine.targets.push_back(
            {-1, "SER-3", ModulationEffect::SPEED_SCALE, 0.35}); // Step 41: +35% speed (compensate stronger 5-HT/DA)

        // Target: AIY excitation (promotes forward runs)
        // SER-6 on AIY: excitatory → more forward → roaming
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) octopamine.targets.push_back(
            {aiyl, "SER-6", ModulationEffect::EXCITABILITY, 4.0}); // +4 pA excitatory
        if (aiyr >= 0) octopamine.targets.push_back(
            {aiyr, "SER-6", ModulationEffect::EXCITABILITY, 4.0});

        neuromod_.add_modulator(std::move(octopamine));
    }

    // --- Tyramine (TA) ---
    // Step 30: RIM tyraminergic signaling for escape response coordination
    // Source: RIM L/R (co-activated with AVA via gap junctions during reversal)
    // TA temporally coordinates reversal phases:
    //   Phase 1 (fast, LGC-55): suppress head oscillation + inhibit forward
    //   Phase 2 (slow, TYRA-3): sensitize nociception for repeated threats
    // REF: Alkema 2005 Neuron — TDC-1 in RIM synthesizes TA
    //      Pirri 2009 Neuron — LGC-55 Cl⁻ channel coordinates escape
    //      Donnelly 2013 PLOS Biology — TA orchestrates motor sequence
    //      Rex 2005 — TYRA-3 GPCR modulates nociception
    {
        Neuromodulator tyramine;
        tyramine.name = "TA";
        tyramine.tau_rise = 500.0;     // 0.5s — fast, escape timescale (Pirri 2009)
        tyramine.tau_decay = 2000.0;   // 2s — behavioral persistence
        tyramine.release_threshold = 0.3;

        // Source neurons: RIM (tyraminergic, TDC-1+, TBH-1-)
        int riml = connectome_.get_neuron_id("RIML");
        int rimr = connectome_.get_neuron_id("RIMR");
        if (riml >= 0) tyramine.source_neuron_ids.push_back(riml);
        if (rimr >= 0) tyramine.source_neuron_ids.push_back(rimr);

        // Target 1: LGC-55 → SMD head motor neurons (Cl⁻ inhibitory)
        // Suppress head oscillation during reversal — "committed reversal"
        // REF: Pirri 2009 — lgc-55 mutants fail to suppress head movements
        const char* smd_names[] = {"SMDDL", "SMDVL", "SMDDR", "SMDVR"};
        for (auto name : smd_names) {
            int id = connectome_.get_neuron_id(name);
            if (id >= 0) tyramine.targets.push_back(
                {id, "LGC-55", ModulationEffect::EXCITABILITY, -25.0});
        }

        // Target 2: LGC-55 → AVB forward command interneurons (Cl⁻ inhibitory)
        // Inhibit forward locomotion → promotes longer reversals
        // REF: Pirri 2009 — LGC-55 expressed in AVB
        int avbl = connectome_.get_neuron_id("AVBL");
        int avbr = connectome_.get_neuron_id("AVBR");
        if (avbl >= 0) tyramine.targets.push_back(
            {avbl, "LGC-55", ModulationEffect::EXCITABILITY, -10.0});
        if (avbr >= 0) tyramine.targets.push_back(
            {avbr, "LGC-55", ModulationEffect::EXCITABILITY, -10.0});

        // Target 3: TYRA-3 → ASH nociceptive neurons (excitatory GPCR)
        // Sensitize nociception — repeated threats → faster escape (emergent)
        // REF: Rex 2005, Donnelly 2013 — TYRA-3 modulates pain-like responses
        int ashl = connectome_.get_neuron_id("ASHL");
        int ashr = connectome_.get_neuron_id("ASHR");
        if (ashl >= 0) tyramine.targets.push_back(
            {ashl, "TYRA-3", ModulationEffect::EXCITABILITY, 5.0});
        if (ashr >= 0) tyramine.targets.push_back(
            {ashr, "TYRA-3", ModulationEffect::EXCITABILITY, 5.0});

        // Target 4: LGC-55 → RIV omega turn neurons (Cl⁻ inhibitory)
        // Gate omega timing: TA suppresses RIV during reversal,
        // RIV bursts only after TA decays → omega follows reversal end
        // REF: Donnelly 2013 — TA coordinates reversal→omega sequence
        int rivl = connectome_.get_neuron_id("RIVL");
        int rivr = connectome_.get_neuron_id("RIVR");
        if (rivl >= 0) tyramine.targets.push_back(
            {rivl, "LGC-55", ModulationEffect::EXCITABILITY, -20.0});
        if (rivr >= 0) tyramine.targets.push_back(
            {rivr, "LGC-55", ModulationEffect::EXCITABILITY, -20.0});

        // Target 5: SER-2 → AIY (Gαi GPCR, inhibitory)
        // Step 43c: RIM TA → SER-2 → AIY imprinted aversive learning
        // During pathogen encounter: RIM fires → TA ↑ → AIY inhibited → approach suppressed
        // SER-2 is required for retrieval of imprinted olfactory memory
        // REF: Jin, Pokala & Bargmann 2016 Cell 164:632-643
        //      Bowitch 2018 G3 — SER-2 on AIY necessary for memory retrieval
        int aiyl_ta = connectome_.get_neuron_id("AIYL");
        int aiyr_ta = connectome_.get_neuron_id("AIYR");
        if (aiyl_ta >= 0) tyramine.targets.push_back(
            {aiyl_ta, "SER-2", ModulationEffect::EXCITABILITY, -10.0}); // -10 pA inhibitory
        if (aiyr_ta >= 0) tyramine.targets.push_back(
            {aiyr_ta, "SER-2", ModulationEffect::EXCITABILITY, -10.0});

        // Target 6: TA→OA biosynthetic coupling via RIC
        // RIM releases TA → diffuses to RIC → TBH-1 converts TA→OA
        // Modeled as weak excitation of RIC proportional to [TA]
        // REF: Alkema 2005 — TDC-1/TBH-1 co-expression in RIC
        int ricl = connectome_.get_neuron_id("RICL");
        int ricr = connectome_.get_neuron_id("RICR");
        if (ricl >= 0) tyramine.targets.push_back(
            {ricl, "TBH-1", ModulationEffect::EXCITABILITY, 2.0}); // +2 pA substrate supply
        if (ricr >= 0) tyramine.targets.push_back(
            {ricr, "TBH-1", ModulationEffect::EXCITABILITY, 2.0});

        neuromod_.add_modulator(std::move(tyramine));
    }

    // --- NLP-12 (CCK homolog) ---
    // Step 45: Neuropeptide from DVA proprioceptive interneuron
    // Source: DVA (single unpaired neuron, senses whole-body curvature via TRP-4)
    // NLP-12 is the key signal for area-restricted search (ARS):
    //   High body curvature (off food, searching) → DVA active → NLP-12 release
    //   Low body curvature (on food, dwelling) → DVA quiet → NLP-12 low
    // Two GPCRs with distinct circuit targets:
    //   CKR-1 → SMD head motor neurons → head swing amplitude ↑ → forward reorientations
    //   CKR-2 → body wall MN / command interneurons → body bend depth + reversal bias
    // REF: Ramachandran 2021 eLife — CKR-1 in SMD necessary and sufficient for ARS
    //      Bhattacharya 2014 PLOS Genetics — DA→DOP-1→DVA→NLP-12 pathway
    //      Hu 2011 Neuron — CKR-2 proprioceptive modulation
    //      Janssen 2008 — NLP-12 structure (CCK-like sulfated peptide)
    {
        Neuromodulator nlp12;
        nlp12.name = "NLP-12";
        nlp12.tau_rise = 3000.0;     // 3s — fast DCV exocytosis (Bhattacharya 2014: rapid ARS onset)
        nlp12.tau_decay = 15000.0;   // 15s — peptide degradation slower than reuptake
        nlp12.release_threshold = 0.5;  // higher than amines: DVA must be strongly active (searching + food_memory)

        // Source neuron: DVA (single, unpaired)
        if (nid("DVA") >= 0) nlp12.source_neuron_ids.push_back(nid("DVA"));

        // Target 1: CKR-1 → SMD head motor neurons (excitatory GPCR)
        // PRIMARY ARS mechanism: NLP-12 → CKR-1 → SMD activation → large head swings
        // → forward reorientations (high-angle turns without reversal)
        // CKR-1 in SMD is SUFFICIENT for local search rescue (Ramachandran 2021 Fig 7A)
        // SMD Ca2+ elevated during ARS (0-5min off food), requires CKR-1 (Fig 8)
        // DVA→SMDVL has 1 synapse; rest is extrasynaptic NLP-12 volume transmission
        // REF: Ramachandran 2021 — ckr-1(lf) reduces reorientations 40-50%
        //      SMD photostimulation recapitulates NLP-12 overexpression effects
        // NOTE: must use get_neuron_id() here — cached IDs not yet populated
        const char* smd_names[] = {"SMDDL", "SMDDR", "SMDVL", "SMDVR"};
        for (auto name : smd_names) {
            int id = connectome_.get_neuron_id(name);
            if (id >= 0) nlp12.targets.push_back(
                {id, "CKR-1", ModulationEffect::EXCITABILITY, 5.0});
        }

        // Target 2: CKR-2 → AVA command interneurons (weak excitatory)
        // Secondary: modest reversal bias during local search
        // CKR-2 has broader expression than CKR-1 (Ramachandran 2021 Fig 5)
        // Replaces hardcoded food_memory→AVA +2.5pA injection (Step 20d)
        // REF: Hu 2011 — CKR-2/egl-30 Gαq coupling → excitatory
        // NOTE: must use get_neuron_id() — cached IDs not yet populated
        int aval = connectome_.get_neuron_id("AVAL");
        int avar = connectome_.get_neuron_id("AVAR");
        if (aval >= 0) nlp12.targets.push_back(
            {aval, "CKR-2", ModulationEffect::EXCITABILITY, 2.0});
        if (avar >= 0) nlp12.targets.push_back(
            {avar, "CKR-2", ModulationEffect::EXCITABILITY, 2.0});

        neuromod_.add_modulator(std::move(nlp12));
    }

    // --- PDF-1 (Pigment Dispersing Factor) ---
    // Step 46: Roaming neuromodulator — opposes 5-HT dwelling (Flavell 2013 Cell)
    // The roaming/dwelling switch is driven by TWO opposing neuromodulators:
    //   5-HT (NSM) → dwelling: slow, low reversal, stay on food
    //   PDF (AVB) → roaming: fast, high exploration, leave food when satiated
    // pdf-1 and pdfr-1 mutants show excessive dwelling, reduced roaming
    // PDFR-1: Gαs-coupled GPCR → cAMP → promotes speed + head movements
    //
    // Source neurons (Flavell 2013 Fig 6): AVB, RIA, ASI, PVP, SIAV, RIF
    //   AVB is the primary source — forward command neuron, active during roaming
    //   We also include RIA (head curvature regulation)
    // Target effects via PDFR-1:
    //   - Speed increase (faster locomotion during roaming)
    //   - Head movement increase (wider exploration)
    //   - Reversal rate increase (more turns → wider coverage)
    //
    // REF: Flavell 2013 Cell — 5-HT/PDF roaming/dwelling bistable switch
    //      Barrios 2012 Nat Neurosci — PDF-1 in exploratory behavior
    //      Janssen 2009 — PDFR-1 Gαs coupling
    {
        Neuromodulator pdf;
        pdf.name = "PDF";
        pdf.tau_rise = 5000.0;     // 5s — neuropeptide DCV release (slower than amines)
        pdf.tau_decay = 20000.0;   // 20s — peptide degradation (very slow, extends roaming)
        pdf.release_threshold = 0.3;

        // Source neurons: AVB (forward command) + RIA (head curvature)
        // AVB is active during forward runs → PDF accumulates during roaming
        // On food with high 5-HT: AVB suppressed (via MOD-1→AIY→less AVB drive) → PDF low
        // Off food: AVB active → PDF high → roaming
        int avbl = connectome_.get_neuron_id("AVBL");
        int avbr = connectome_.get_neuron_id("AVBR");
        int rial = connectome_.get_neuron_id("RIAL");
        int riar = connectome_.get_neuron_id("RIAR");
        if (avbl >= 0) pdf.source_neuron_ids.push_back(avbl);
        if (avbr >= 0) pdf.source_neuron_ids.push_back(avbr);
        if (rial >= 0) pdf.source_neuron_ids.push_back(rial);
        if (riar >= 0) pdf.source_neuron_ids.push_back(riar);

        // Target 1: PDFR-1 → speed increase (roaming = fast locomotion)
        // Opposes 5-HT SER-7 speed reduction (-0.40) and DA DOP-3 (-0.30)
        // Net: roaming (high PDF, low 5-HT) = fast; dwelling (high 5-HT, low PDF) = slow
        // REF: Flavell 2013 — pdf-1 mutants move slower on food
        pdf.targets.push_back(
            {-1, "PDFR-1", ModulationEffect::SPEED_SCALE, 0.25}); // +25% speed at peak PDF

        // Target 2: PDFR-1 → reversal rate increase (roaming = more turns)
        // Opposes 5-HT reversal suppression (-0.50)
        // Net: roaming has moderate reversal rate; dwelling has low reversal rate
        pdf.targets.push_back(
            {-1, "PDFR-1", ModulationEffect::REVERSAL_RATE, 0.30}); // +30% reversals at peak

        // Target 3: PDFR-1 → AIY excitation (promotes forward drive)
        // PDF → PDFR-1 on AIY → cAMP → more forward runs
        // Complements OA→SER-6→AIY but through different receptor/pathway
        // REF: Flavell 2013 — optogenetic cAMP in PDFR-1 cells → prolonged roaming
        int aiyl = connectome_.get_neuron_id("AIYL");
        int aiyr = connectome_.get_neuron_id("AIYR");
        if (aiyl >= 0) pdf.targets.push_back(
            {aiyl, "PDFR-1", ModulationEffect::EXCITABILITY, 3.0}); // +3 pA
        if (aiyr >= 0) pdf.targets.push_back(
            {aiyr, "PDFR-1", ModulationEffect::EXCITABILITY, 3.0});

        // Step 48: Target 4: PDFR-1 network → NSM inhibition (mutual exclusivity)
        // KEY FEEDBACK completing the roaming/dwelling bistable switch.
        // Without this, NSM keeps firing on food → 5-HT stays high → permanent dwelling.
        // Mechanism: PDF → PDFR-1 expressing neurons → inhibit NSM (indirect)
        // Modeled as PDF EXCITABILITY effect on NSM (simplification of PDFR-1 network).
        // When PDF rises (satiety → OA → AVB active → PDF), NSM is suppressed → 5-HT drops.
        // Positive feedback loop: PDF↑ → NSM↓ → 5-HT↓ → less RIC inhibition → OA↑ → AVB↑ → PDF↑↑
        // REF: Flavell 2020 eLife — "PDF receptor-expressing neurons inhibit NSM"
        //      "optogenetic activation of pdf-1 neurons → acute and robust NSM inhibition"
        //      "PDF signaling necessary and sufficient to keep NSM inactive during roaming"
        // NSM on-food drive ≈ 21pA; at PDF=0.4: inhibition = -8pA → net 13pA (moderate)
        // The bistable positive feedback amplifies small PDF changes to flip the switch.
        if (nid("NSML") >= 0) pdf.targets.push_back(
            {nid("NSML"), "PDFR-1", ModulationEffect::EXCITABILITY, -25.0}); // -25 pA at peak PDF
        if (nid("NSMR") >= 0) pdf.targets.push_back(
            {nid("NSMR"), "PDFR-1", ModulationEffect::EXCITABILITY, -25.0});

        neuromod_.add_modulator(std::move(pdf));
    }

    LOG_INFO("Neuromodulation setup: 5-HT (dwelling), DA (basal slowing), OA (roaming), TA (escape), NLP-12 (ARS), PDF (roaming)");
}

} // namespace celegans
