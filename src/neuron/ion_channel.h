#pragma once

#include <string>
#include <cmath>
#include "core/fast_math.h"

namespace celegans {

class IonChannel {
public:
    virtual ~IonChannel() = default;

    virtual void step(double V, double Ca, double dt) = 0;

    virtual double get_current(double V) const = 0;

    virtual std::string name() const = 0;

    double get_conductance() const { return g_max_ * get_open_fraction(); }
    double get_reversal_potential() const { return E_rev_; }

protected:
    double g_max_ = 0.0;   // max conductance (nS)
    double E_rev_ = 0.0;   // reversal potential (mV)

    virtual double get_open_fraction() const = 0;

    // Utility: steady-state Boltzmann sigmoid
    static double boltzmann(double V, double V_half, double k) {
        return 1.0 / (1.0 + fast_exp(-(V - V_half) / k));
    }

    // Utility: exponential relaxation toward steady-state
    static double relax(double var, double var_inf, double tau, double dt) {
        if (tau < 1e-6) return var_inf;
        return var_inf + (var - var_inf) * fast_exp(-dt / tau);
    }
};

// Leak channel: constant conductance, no gating
class LeakChannel : public IonChannel {
public:
    LeakChannel(double g_leak, double E_leak) {
        g_max_ = g_leak;
        E_rev_ = E_leak;
    }

    void step(double /*V*/, double /*Ca*/, double /*dt*/) override {}

    double get_current(double V) const override {
        return g_max_ * (V - E_rev_);
    }

    std::string name() const override { return "Leak"; }

protected:
    double get_open_fraction() const override { return 1.0; }
};

// EGL-19: L-type voltage-gated calcium channel
// Key channel for sustained calcium current, widely expressed
// Model based on Nicoletti et al. 2019
class EGL19Channel : public IonChannel {
public:
    EGL19Channel(double g_max = 1.5, double E_Ca = 60.0) {
        g_max_ = g_max;
        E_rev_ = E_Ca;
    }

    void step(double V, double /*Ca*/, double dt) override {
        double m_inf = boltzmann(V, -4.4, 7.5);
        double h_inf = boltzmann(V, -24.0, -5.0) * 0.35 + 0.65;
        double tau_m = 2.5;   // ms
        double tau_h = 50.0;  // ms (slow inactivation)
        m_ = relax(m_, m_inf, tau_m, dt);
        h_ = relax(h_, h_inf, tau_h, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * h_ * (V - E_rev_);
    }

    std::string name() const override { return "EGL-19"; }

protected:
    double get_open_fraction() const override { return m_ * m_ * h_; }

private:
    double m_ = 0.0;
    double h_ = 1.0;
};

// SHL-1: Shaker-like voltage-gated potassium channel (A-type, fast inactivating)
// Model based on Nicoletti et al. 2019
class SHL1Channel : public IonChannel {
public:
    SHL1Channel(double g_max = 2.0, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double /*Ca*/, double dt) override {
        double m_inf = boltzmann(V, -12.0, 14.0);
        double h_inf = boltzmann(V, -52.0, -7.0);
        double tau_m = 5.0;    // ms
        double tau_h = 100.0;  // ms
        m_ = relax(m_, m_inf, tau_m, dt);
        h_ = relax(h_, h_inf, tau_h, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * m_ * h_ * (V - E_rev_);
    }

    std::string name() const override { return "SHL-1"; }

protected:
    double get_open_fraction() const override { return m_ * m_ * m_ * h_; }

private:
    double m_ = 0.0;
    double h_ = 1.0;
};

// KQT-3: KCNQ-type potassium channel (M-current, slow)
// Contributes to resting potential and slow excitability modulation
class KQT3Channel : public IonChannel {
public:
    KQT3Channel(double g_max = 0.5, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double /*Ca*/, double dt) override {
        double m_inf = boltzmann(V, -43.0, 5.0);
        double tau_m = 50.0 + 200.0 / (1.0 + fast_exp((V + 35.0) / 15.0)); // ms
        m_ = relax(m_, m_inf, tau_m, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * (V - E_rev_);
    }

    std::string name() const override { return "KQT-3"; }

protected:
    double get_open_fraction() const override { return m_ * m_; }

private:
    double m_ = 0.0;
};

// UNC-2: N/P/Q-type calcium channel (presynaptic calcium entry)
class UNC2Channel : public IonChannel {
public:
    UNC2Channel(double g_max = 1.0, double E_Ca = 60.0) {
        g_max_ = g_max;
        E_rev_ = E_Ca;
    }

    void step(double V, double /*Ca*/, double dt) override {
        double m_inf = boltzmann(V, -12.0, 4.5);
        double h_inf = boltzmann(V, -52.0, -5.0);
        double tau_m = 1.0;
        double tau_h = 30.0;
        m_ = relax(m_, m_inf, tau_m, dt);
        h_ = relax(h_, h_inf, tau_h, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * h_ * (V - E_rev_);
    }

    std::string name() const override { return "UNC-2"; }

protected:
    double get_open_fraction() const override { return m_ * m_ * h_; }

private:
    double m_ = 0.0;
    double h_ = 1.0;
};

// CCA-1: T-type calcium channel (low threshold, critical for RMD oscillation)
class CCA1Channel : public IonChannel {
public:
    CCA1Channel(double g_max = 4.0, double E_Ca = 60.0) {
        g_max_ = g_max;
        E_rev_ = E_Ca;
    }

    // Step 19: Neuromodulation of activation threshold
    // RIA input shifts V_half_m → lower threshold → easier burst → longer duty cycle
    // This is how the klinotaxis signal (ASE→AIY→RIA) modulates SMD oscillation
    void set_activation_shift(double dV) { v_half_m_shift_ = dV; }
    double get_activation_shift() const { return v_half_m_shift_; }

    void step(double V, double /*Ca*/, double dt) override {
        // T-type Ca: h_half at -55mV matches loser's voltage in half-center oscillator
        // Winner at ~-35mV: h≈0.02 (inactivated) → burst ends
        // Loser at ~-55mV: h→0.5 (de-inactivated) → ready for rebound
        // REF: Steger 2005, Bhatt 2014 - CCA-1 in C. elegans head motor neurons
        double m_inf = boltzmann(V, -48.0 + v_half_m_shift_, 5.0);
        double h_inf = boltzmann(V, -55.0, -5.0);
        double tau_m = 3.0;
        double tau_h = 80.0;  // slow recovery → ~0.5-1 Hz oscillation
        m_ = relax(m_, m_inf, tau_m, dt);
        h_ = relax(h_, h_inf, tau_h, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * h_ * (V - E_rev_);
    }

    std::string name() const override { return "CCA-1"; }

protected:
    double get_open_fraction() const override { return m_ * m_ * h_; }

private:
    double m_ = 0.0;
    double h_ = 1.0;
    double v_half_m_shift_ = 0.0;  // neuromodulatory shift of activation V_half (mV)
};

// SLO-1: BK (big conductance calcium-activated potassium channel)
class SLO1Channel : public IonChannel {
public:
    SLO1Channel(double g_max = 3.0, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double Ca, double dt) override {
        // BK channel: essentially closed without Ca, opens with high Ca
        // REF: Bhatt 2014 - at low Ca (<0.1μM) V_half > +50mV, at high Ca (>1μM) V_half ≈ -30mV
        double Ca_shift = 120.0 * Ca / (Ca + 1.0); // large shift at high Ca
        double m_inf = boltzmann(V, 50.0 - Ca_shift, 15.0);
        double tau_m = 5.0;
        m_ = relax(m_, m_inf, tau_m, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * (V - E_rev_);
    }

    std::string name() const override { return "SLO-1"; }

protected:
    double get_open_fraction() const override { return m_; }

private:
    double m_ = 0.0;
};

// NCA: NALCN leak sodium channel (maintains excitability)
class NCAChannel : public IonChannel {
public:
    NCAChannel(double g_max = 0.1, double E_Na = 30.0) {
        g_max_ = g_max;
        E_rev_ = E_Na;
    }

    void step(double /*V*/, double /*Ca*/, double /*dt*/) override {}

    double get_current(double V) const override {
        return g_max_ * (V - E_rev_);
    }

    std::string name() const override { return "NCA"; }

protected:
    double get_open_fraction() const override { return 1.0; }
};

// MechanoSensitive: stretch-activated cation channel for proprioceptive feedback
// B-class motor neurons have stretch-sensitive conductances that allow
// body curvature to propagate the locomotion wave (Wen et al. 2012)
// The channel opens proportionally to local mechanical stretch
class MechanoSensitiveChannel : public IonChannel {
public:
    MechanoSensitiveChannel(double g_max = 2.0, double E_cat = -10.0) {
        g_max_ = g_max;
        E_rev_ = E_cat; // cation (mixed Na/Ca), near 0 mV → excitatory
    }

    void step(double /*V*/, double /*Ca*/, double dt) override {
        // Stretch gating: m relaxes toward stretch-driven steady state
        double m_inf = std::abs(stretch_input_) / (std::abs(stretch_input_) + 0.1);
        double tau_m = 10.0; // ms, mechanical transduction time constant
        m_ = relax(m_, m_inf, tau_m, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * (V - E_rev_);
    }

    std::string name() const override { return "MEC"; }

    // Set by simulation engine based on local body curvature
    void set_stretch(double stretch) { stretch_input_ = stretch; }
    double get_stretch() const { return stretch_input_; }

protected:
    double get_open_fraction() const override { return m_; }

private:
    double m_ = 0.0;
    double stretch_input_ = 0.0; // set externally from body curvature
};

// ================================================================
// Step 57: Additional ion channels (8→14)
// ================================================================

// EGL-36: Shaw-type (Kv3) delayed rectifier potassium channel
// Non-inactivating sustained K⁺ current for spike repolarization
// Widely expressed: ASE, AWC, motor neurons, interneurons
// Higher activation threshold than SHL-1 → activates during strong depolarization
// REF: Johnstone 1997, Elkes 1997, Santi 2003 JBC
class EGL36Channel : public IonChannel {
public:
    EGL36Channel(double g_max = 1.0, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double /*Ca*/, double dt) override {
        // Kv3 activates at more depolarized potentials than Shaker (SHL-1)
        // V_half = -8 mV (vs SHL-1's -12), no inactivation (sustained)
        double m_inf = boltzmann(V, -8.0, 12.0);
        double tau_m = 5.0 + 15.0 / (1.0 + fast_exp((V + 20.0) / 10.0)); // ms
        m_ = relax(m_, m_inf, tau_m, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * (V - E_rev_);
    }

    std::string name() const override { return "EGL-36"; }

protected:
    double get_open_fraction() const override { return m_ * m_; }

private:
    double m_ = 0.0;
};

// IRK: Inward rectifier potassium channel (Kir family)
// Conducts K⁺ preferentially at hyperpolarized potentials → stabilizes resting potential
// IRK-1: pharyngeal muscle; IRK-2: broadly in neurons; IRK-3: body wall muscle
// Characteristic: strong inward rectification below E_K, weak outward above
// REF: Döring 2002 Mol Biol Cell — IRK-1/2/3 cloning and expression
//      Shtonda & Bhatt 2004 — IRK contributes to neuronal resting potential
class IRKChannel : public IonChannel {
public:
    IRKChannel(double g_max = 0.3, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double /*Ca*/, double /*dt*/) override {
        // Inward rectification: Mg²⁺/polyamine block at depolarized potentials
        // At V < E_K: full conductance; at V > E_K: blocked
        // Modeled as instantaneous Boltzmann rectification
        rect_ = 1.0 / (1.0 + fast_exp((V - E_rev_ + 10.0) / 8.0));
    }

    double get_current(double V) const override {
        return g_max_ * rect_ * (V - E_rev_);
    }

    std::string name() const override { return "IRK"; }

protected:
    double get_open_fraction() const override { return rect_; }

private:
    double rect_ = 1.0;  // rectification factor
};

// TWK: Two-pore domain potassium channel (background leak K⁺)
// C. elegans has ~49 TWK genes (largest K₂P family in metazoa)
// TWK-18: temperature-sensitive (Bhatt 2014), TWK-7: locomotion speed
// Voltage-independent leak, modulated by pH, stretch, temperature, anesthetics
// Sets baseline resting potential and excitability threshold
// REF: Salkoff 2001 — "49 potassium channels in C. elegans"
//      Bhatt 2014 — TWK-18 temperature sensitivity
class TWKChannel : public IonChannel {
public:
    TWKChannel(double g_max = 0.15, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double /*V*/, double /*Ca*/, double /*dt*/) override {
        // Background leak: voltage-independent, always open
        // Temperature modulation applied externally if needed
    }

    double get_current(double V) const override {
        return g_max_ * open_ * (V - E_rev_);
    }

    std::string name() const override { return "TWK"; }

    // External modulation (temperature, pH, anesthetics)
    void set_modulation(double mod) { open_ = std::max(0.0, std::min(2.0, mod)); }

protected:
    double get_open_fraction() const override { return open_; }

private:
    double open_ = 1.0;  // baseline fully open, modulated externally
};

// SLO-2: Na⁺-activated potassium channel (Slo family member 2)
// Activated by intracellular Na⁺ and Cl⁻, NOT by Ca²⁺ (contrast with SLO-1)
// Contributes to resting potential in many C. elegans neurons
// Prevents over-excitation during sustained depolarization (Na⁺ influx → K⁺ efflux)
// Also has weak voltage-dependent activation
// REF: Yuan 2000 Neuron — SLO-2 cloning
//      Yuan 2003 JBC — Na⁺/Cl⁻ activation mechanism
//      Salkoff 2006 — SLO family review
class SLO2Channel : public IonChannel {
public:
    SLO2Channel(double g_max = 1.5, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double /*Ca*/, double dt) override {
        // Dual activation: voltage + [Na⁺]i proxy
        // During depolarization: NCA/leak Na⁺ influx raises local [Na⁺]i
        // Approximate: use V as proxy for Na⁺ influx (more depolarized → more Na⁺)
        // V_half = +5 mV (high threshold, only activates during strong depolarization)
        double na_proxy = boltzmann(V, -20.0, 15.0); // Na⁺ accumulation proxy
        double m_inf = boltzmann(V, 5.0, 15.0) * (0.3 + 0.7 * na_proxy);
        double tau_m = 10.0;  // ms, moderate kinetics
        m_ = relax(m_, m_inf, tau_m, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * (V - E_rev_);
    }

    std::string name() const override { return "SLO-2"; }

protected:
    double get_open_fraction() const override { return m_; }

private:
    double m_ = 0.0;
};

// OSM-9: TRPV (Transient Receptor Potential Vanilloid) cation channel
// Forms heteromer with OCR-2 in ASH, OLQ, AWA sensory neurons
// Polymodal: osmolarity, nociception, nose touch, olfaction (diacetyl in AWA)
// Non-selective cation: permeable to Ca²⁺, Na⁺ → E_rev ≈ 0 mV
// Gated by stimulus (not voltage), but modulated by PIP₂ and Ca²⁺
// REF: Colbert 1997 J Neurosci — OSM-9 cloning, osmosensation
//      Tobin 2002 Neuron — OSM-9/OCR-2 heteromer in polymodal nociception
//      Kahn-Kirby 2004 Cell — PUFAs gate C. elegans TRPV channels
class OSM9Channel : public IonChannel {
public:
    OSM9Channel(double g_max = 2.0, double E_cat = 0.0) {
        g_max_ = g_max;
        E_rev_ = E_cat; // non-selective cation
    }

    void step(double /*V*/, double /*Ca*/, double dt) override {
        // Stimulus-gated: opens proportionally to stimulus_input_ (set externally)
        // Slow kinetics: mechanical/chemical transduction timescale
        double m_inf = stimulus_ / (stimulus_ + 0.5); // half-max at stimulus=0.5
        double tau_m = 20.0;  // ms, sensory transduction delay
        m_ = relax(m_, m_inf, tau_m, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * (V - E_rev_);
    }

    std::string name() const override { return "OSM-9"; }

    // Set by simulation engine from sensory stimulus
    void set_stimulus(double s) { stimulus_ = std::max(0.0, s); }
    double get_stimulus() const { return stimulus_; }

protected:
    double get_open_fraction() const override { return m_; }

private:
    double m_ = 0.0;
    double stimulus_ = 0.0;  // external stimulus intensity
};

// EXP-2: Kv-type potassium channel critical for enteric muscle/AVL
// Despite Kv structure, shows inward rectifier-like properties
// Essential for AVL action potential repolarization during DMP
// Also expressed in body wall muscle
// Unique: fast activation at depolarized potentials + slow recovery
// REF: Davis 1999 Science — EXP-2 cloning, enteric function
//      Shtonda 2005 — EXP-2 in body wall muscle
//      Jiang 2022 Nat Commun — EXP-2 repolarizes AVL compound APs
class EXP2Channel : public IonChannel {
public:
    EXP2Channel(double g_max = 2.0, double E_K = -80.0) {
        g_max_ = g_max;
        E_rev_ = E_K;
    }

    void step(double V, double /*Ca*/, double dt) override {
        // Fast activation at depolarized potentials
        double m_inf = boltzmann(V, -10.0, 8.0);
        // Slow inactivation (partial) — allows sustained repolarizing current
        double h_inf = boltzmann(V, -30.0, -10.0) * 0.4 + 0.6;
        double tau_m = 2.0;   // ms, fast activation
        double tau_h = 40.0;  // ms, slow partial inactivation
        m_ = relax(m_, m_inf, tau_m, dt);
        h_ = relax(h_, h_inf, tau_h, dt);
    }

    double get_current(double V) const override {
        return g_max_ * m_ * m_ * h_ * (V - E_rev_);
    }

    std::string name() const override { return "EXP-2"; }

protected:
    double get_open_fraction() const override { return m_ * m_ * h_; }

private:
    double m_ = 0.0;
    double h_ = 1.0;
};

} // namespace celegans
