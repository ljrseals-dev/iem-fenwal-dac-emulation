// =============================================================================
//  JilesAtherton.cpp
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Implementation of the quasi-static hysteresis model.
//
//  Reference : JilesAtherton.h
//
//  Notes
//  -----
//  The update here is a bounded, direction-aware Euler approximation. It is
//  not a Newton solution of the implicit Jiles-Atherton constitutive
//  equation. It is intended for DSP experimentation and for exercising the
//  oversampling and anti-aliasing path. It is not a validated representation
//  of any specific transformer.
// =============================================================================

#include "JilesAtherton.h"

namespace iem500 {

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------

void JilesAtherton::setParameters(const JilesAthertonParameters& p) noexcept
{
    p_ = p;

    // Clamp to physically realizable ranges.
    p_.Ms     = std::max(1.0e-12, p_.Ms);
    p_.a      = std::max(1.0e-9,  p_.a);
    p_.k      = std::max(1.0e-9,  p_.k);
    p_.c      = std::clamp(p_.c, 0.0, 1.0);
    p_.alpha  = std::clamp(p_.alpha, -1.0, 1.0);
    p_.scaleH = std::max(1.0e-12, p_.scaleH);
    p_.scaleM = std::max(1.0e-12, p_.scaleM);
}

void JilesAtherton::reset() noexcept
{
    M_     = 0.0;
    Hprev_ = 0.0;
}

// -----------------------------------------------------------------------------
//  Langevin function
//
//  L(x) = coth(x) - 1/x
//
//  Near x = 0 the two terms cancel catastrophically in floating point, so a
//  truncated series expansion is used below a small threshold.
// -----------------------------------------------------------------------------

double JilesAtherton::safeLangevin(double x) noexcept
{
    const double ax = std::abs(x);

    if (ax < 1.0e-4) {
        const double x2 = x * x;
        const double x3 = x2 * x;
        const double x5 = x3 * x2;
        return x / 3.0 - x3 / 45.0 + (2.0 * x5) / 945.0;
    }

    const double coth = 1.0 / std::tanh(x);
    return coth - 1.0 / x;
}

// -----------------------------------------------------------------------------
//  Processing
// -----------------------------------------------------------------------------

double JilesAtherton::process(double H) noexcept
{
    // Map the input field into the normalized model domain.
    const double h  = H / p_.scaleH;
    const double he = h + p_.alpha * M_;
    const double x  = he / p_.a;

    // Anhysteretic magnetization.
    const double Man = p_.Ms * safeLangevin(x);

    const double dH = h - Hprev_;

    // Direction of field change. Zero step leaves the state untouched.
    const double direction = (dH > 0.0) ? 1.0 : (dH < 0.0 ? -1.0 : 0.0);

    // Irreversible and reversible contributions.
    const double irreversible = direction * (std::abs(Man - M_) / p_.k);
    const double reversible   = p_.c * (Man - M_);

    // Bounded Euler-like step. Clamping the step magnitude keeps the model
    // stable for large input excursions at the oversampled rate.
    const double step = std::clamp(std::abs(dH), 0.0, 1.0);
    M_ += step * (irreversible + reversible);

    // Guard against divergence.
    M_ = std::clamp(M_, -2.0 * p_.Ms, 2.0 * p_.Ms);

    Hprev_ = h;
    return M_ * p_.scaleM;
}

} // namespace iem500
