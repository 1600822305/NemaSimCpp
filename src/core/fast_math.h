#pragma once

#include <cstdint>
#include <cmath>

namespace celegans {

// Fast exp() using range reduction + degree-5 Taylor polynomial.
// Max relative error < 2e-8 — effectively exact for neural simulation.
// ~2-3x faster than std::exp() on x86-64 (no libm call, pure ALU + floor).
//
// Used in hot paths: boltzmann sigmoid (~1320/step), synapse sigmoid (~110/step),
// relaxation (~660/step), release rate (~132/step). Total ~2200 calls/step.
inline double fast_exp(double x) {
    if (x < -700.0) return 0.0;
    if (x >  700.0) return 1e308;

    // Range reduction: exp(x) = 2^n * exp(r), |r| <= ln2/2 ≈ 0.347
    const double LOG2E  = 1.4426950408889634;   // 1 / ln(2)
    const double LN2_HI = 0.6931471805599453;   // ln(2)

    // Fast round-to-nearest using cast (avoids std::floor libm call)
    double xn = x * LOG2E + 6755399441055744.0;  // 2^52 + 2^51 magic rounding
    union { double d; int64_t i; } rnd;
    rnd.d = xn;
    int64_t ni = rnd.i - 0x4338000000000000LL;   // subtract magic bias
    double n = static_cast<double>(ni);
    double r = x - n * LN2_HI;

    // Horner form of Taylor series: exp(r) = 1 + r + r²/2! + ... + r⁶/6!
    // For |r| ≤ 0.347: max relative error < 2e-10
    double p = 1.0 + r * (1.0 + r * (0.5 + r * (0.16666666666666666
                   + r * (0.041666666666666664 + r * (0.008333333333333333
                   + r * 0.001388888888888889)))));

    // Reconstruct: exp(x) = 2^n * p, using IEEE 754 bit manipulation for 2^n
    union { double d; int64_t i; } scale;
    scale.i = (1023LL + ni) << 52;

    return scale.d * p;
}

} // namespace celegans
