#pragma once

#include <string>
#include <cmath>

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
        return 1.0 / (1.0 + std::exp(-(V - V_half) / k));
    }

    // Utility: exponential relaxation toward steady-state
    static double relax(double var, double var_inf, double tau, double dt) {
        if (tau < 1e-6) return var_inf;
        return var_inf + (var - var_inf) * std::exp(-dt / tau);
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
        double tau_m = 50.0 + 200.0 / (1.0 + std::exp((V + 35.0) / 15.0)); // ms
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

    void step(double V, double /*Ca*/, double dt) override {
        // T-type Ca: h_half at -55mV matches loser's voltage in half-center oscillator
        // Winner at ~-35mV: h≈0.02 (inactivated) → burst ends
        // Loser at ~-55mV: h→0.5 (de-inactivated) → ready for rebound
        // REF: Steger 2005, Bhatt 2014 - CCA-1 in C. elegans head motor neurons
        double m_inf = boltzmann(V, -48.0, 5.0);
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

} // namespace celegans
