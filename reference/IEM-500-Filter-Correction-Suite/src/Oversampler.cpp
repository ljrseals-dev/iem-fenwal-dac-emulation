// =============================================================================
//  Oversampler.cpp
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Implementation of the streaming polyphase-FIR oversampler.
//
//  Reference : Oversampler.h
//
//  Notes
//  -----
//  A single Blackman-windowed sinc prototype is designed at a cutoff below
//  the oversampled Nyquist frequency. Interpolation uses the polyphase
//  decomposition of that prototype with the standard `factor` gain
//  compensation for zero stuffing. Decimation runs the prototype directly
//  over the high-rate history.
//
//  The prototype is normalized to unity DC gain, so no additional scaling is
//  applied in either direction.
// =============================================================================

#include "Oversampler.h"

#include <algorithm>
#include <cmath>

namespace iem500 {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------

Oversampler::Oversampler(int factor)
    : factor_(factor < 1 ? 1 : factor)
{
    buildCoefficients();
    allocateState();
    reset();
}

// -----------------------------------------------------------------------------
//  Prototype design
// -----------------------------------------------------------------------------

void Oversampler::buildCoefficients()
{
    const int taps = factor_ * tapsPerPhase_;

    upCoeffs_.assign(static_cast<std::size_t>(taps), 0.0);
    downCoeffs_.assign(static_cast<std::size_t>(taps), 0.0);

    // Cutoff just below the oversampled Nyquist frequency.
    const double fc = 0.5 / static_cast<double>(factor_);
    const int    M  = taps - 1;

    double sum = 0.0;

    for (int n = 0; n < taps; ++n) {
        const double m = static_cast<double>(n) - 0.5 * static_cast<double>(M);

        const double sinc = (std::abs(m) < 1.0e-12)
                          ? 1.0
                          : std::sin(2.0 * kPi * fc * m) / (kPi * m);

        // Blackman window.
        const double w = 0.42
                       - 0.50 * std::cos(2.0 * kPi * n / M)
                       + 0.08 * std::cos(4.0 * kPi * n / M);

        const double c = 2.0 * fc * sinc * w;
        downCoeffs_[static_cast<std::size_t>(n)] = c;
        sum += c;
    }

    // Normalize the prototype to unity DC gain.
    if (std::abs(sum) > 1.0e-12) {
        const double inv = 1.0 / sum;
        for (auto& c : downCoeffs_) {
            c *= inv;
        }
    }

    // Polyphase decomposition for interpolation. Each phase is scaled by
    // `factor` to compensate for the zero stuffing operation.
    for (int p = 0; p < factor_; ++p) {
        for (int t = 0; t < tapsPerPhase_; ++t) {
            const int n = t * factor_ + p;
            upCoeffs_[static_cast<std::size_t>(p * tapsPerPhase_ + t)] =
                downCoeffs_[static_cast<std::size_t>(n)]
              * static_cast<double>(factor_);
        }
    }
}

// -----------------------------------------------------------------------------
//  State allocation
// -----------------------------------------------------------------------------

void Oversampler::allocateState()
{
    upHistSize_   = tapsPerPhase_;
    downHistSize_ = factor_ * tapsPerPhase_;

    upHistory_.assign(static_cast<std::size_t>(upHistSize_), 0.0);
    downHistory_.assign(static_cast<std::size_t>(downHistSize_), 0.0);

    upWrite_   = 0;
    downWrite_ = 0;
}

// -----------------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------------

void Oversampler::prepare(double baseSampleRate, std::size_t /*blockSize*/)
{
    baseFs_ = (baseSampleRate > 0.0) ? baseSampleRate : 48000.0;

    allocateState();

    // Approximate round-trip group delay expressed in base-rate samples.
    latencyBase_ = static_cast<double>(downHistSize_ - 1)
                 / (2.0 * static_cast<double>(factor_));

    reset();
}

void Oversampler::reset() noexcept
{
    std::fill(upHistory_.begin(),   upHistory_.end(),   0.0);
    std::fill(downHistory_.begin(), downHistory_.end(), 0.0);

    upWrite_   = 0;
    downWrite_ = 0;
}

// -----------------------------------------------------------------------------
//  Reconfiguration
// -----------------------------------------------------------------------------

void Oversampler::setFactor(int factor)
{
    const int f = (factor < 1) ? 1 : factor;

    if (f == factor_) {
        return;
    }

    factor_ = f;

    buildCoefficients();
    allocateState();
    reset();
}

} // namespace iem500
