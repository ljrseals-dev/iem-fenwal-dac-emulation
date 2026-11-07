// =============================================================================
//  HpfSection.cpp
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Implementation of the always-active high-pass section.
//
//  Reference : HpfSection.h
// =============================================================================

#include "HpfSection.h"

#include <algorithm>

namespace iem500 {

// -----------------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------------

void HpfSection::prepare(double sampleRate) noexcept
{
    fs_ = std::max(8000.0, sampleRate);
    update();
    reset();
}

void HpfSection::setParameters(const HpfParameters& p) noexcept
{
    p_ = p;
    p_.cornerHz = std::clamp(p_.cornerHz, 1.0, 0.45 * fs_);
    p_.q        = std::max(0.05, p_.q);
    update();
}

void HpfSection::reset() noexcept
{
    z1_ = 0.0;
    z2_ = 0.0;
}

// -----------------------------------------------------------------------------
//  Coefficient derivation
//
//  Bilinear transform with frequency prewarping. The analog prototype is a
//  second-order high-pass in the standard K * (s^2) / (s^2 + (w/Q)s + w^2)
//  form. Prewarping is applied via the substitution
//      w = 2 * fs * tan(pi * f0 / fs)
//  so that the digital corner matches the requested analog corner.
// -----------------------------------------------------------------------------

void HpfSection::update() noexcept
{
    const double w  = 2.0 * fs_ * std::tan(M_PI * p_.cornerHz / fs_);
    const double K  = 2.0 * fs_;
    const double K2 = K * K;
    const double w2 = w * w;

    const double D = K2 + (w * K) / p_.q + w2;

    b0_ =  K2 / D;
    b1_ = -2.0 * K2 / D;
    b2_ =  K2 / D;

    a1_ = (-2.0 * K2 + 2.0 * w2) / D;
    a2_ = ( K2 - (w * K) / p_.q + w2) / D;
}

// -----------------------------------------------------------------------------
//  Processing
// -----------------------------------------------------------------------------

double HpfSection::process(double x) noexcept
{
    // Direct Form II transposed.
    const double y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
}

} // namespace iem500
