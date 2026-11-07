// =============================================================================
//  LpfSection.cpp
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Implementation of the low-pass and high-shelf reference section.
//
//  Reference : LpfSection.h
// =============================================================================

#include "LpfSection.h"

#include <algorithm>

namespace iem500 {

// -----------------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------------

void LpfSection::prepare(double sampleRate) noexcept
{
    fs_ = std::max(8000.0, sampleRate);
    update();
    reset();
}

void LpfSection::setParameters(const LpfParameters& p) noexcept
{
    p_ = p;
    p_.cornerHz = std::clamp(p_.cornerHz, 10.0, 0.45 * fs_);
    p_.q        = std::max(0.05, p_.q);
    update();
}

void LpfSection::reset() noexcept
{
    z1_ = 0.0;
    z2_ = 0.0;
}

// -----------------------------------------------------------------------------
//  Coefficient derivation
//
//  Shelf mode  : RBJ high-shelf formulation.
//  Filter mode : Bilinear transform with frequency prewarping on a
//                second-order low-pass analog prototype.
// -----------------------------------------------------------------------------

void LpfSection::update() noexcept
{
    if (p_.shelf) {
        const double A     = std::pow(10.0, p_.gainDb / 40.0);
        const double w0    = 2.0 * M_PI * p_.cornerHz / fs_;
        const double alpha = std::sin(w0) / (2.0 * p_.q);
        const double beta  = 2.0 * std::sqrt(A) * alpha;
        const double c     = std::cos(w0);

        const double b0 =  A * ((A + 1.0) + (A - 1.0) * c + beta);
        const double b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * c);
        const double b2 =  A * ((A + 1.0) + (A - 1.0) * c - beta);

        const double a0 =       (A + 1.0) - (A - 1.0) * c + beta;
        const double a1 =  2.0 * ((A - 1.0) - (A + 1.0) * c);
        const double a2 =       (A + 1.0) - (A - 1.0) * c - beta;

        b0_ = b0 / a0;
        b1_ = b1 / a0;
        b2_ = b2 / a0;
        a1_ = a1 / a0;
        a2_ = a2 / a0;
        return;
    }

    const double w  = 2.0 * fs_ * std::tan(M_PI * p_.cornerHz / fs_);
    const double K  = 2.0 * fs_;
    const double K2 = K * K;
    const double w2 = w * w;

    const double D = K2 + (w * K) / p_.q + w2;

    b0_ =  w2 / D;
    b1_ =  2.0 * w2 / D;
    b2_ =  w2 / D;

    a1_ = (-2.0 * K2 + 2.0 * w2) / D;
    a2_ = ( K2 - (w * K) / p_.q + w2) / D;
}

// -----------------------------------------------------------------------------
//  Processing
// -----------------------------------------------------------------------------

double LpfSection::process(double x) noexcept
{
    const double y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
}

} // namespace iem500
