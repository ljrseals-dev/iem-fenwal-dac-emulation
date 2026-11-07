// =============================================================================
//  FilterChain.cpp
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Implementation of the corrected top-level signal chain.
//
//  Reference : FilterChain.h
//
//  Signal order
//  ------------
//      in --> HPF (ALWAYS ACTIVE) --> [ bypass ? pass : EQ bands ] -->
//             --> coupling transformer (ALWAYS IN CIRCUIT, OVERSAMPLED) --> out
//
//  The high-pass stage sits outside the bypass conditional, and the coupling
//  transformer runs inside the oversampled domain. See FilterChain.h for the
//  rationale.
// =============================================================================

#include "FilterChain.h"

namespace iem500 {

// -----------------------------------------------------------------------------
//  Lifecycle
// -----------------------------------------------------------------------------

void FilterChain::prepare(double sampleRate, std::size_t blockSize) noexcept
{
    sampleRate_ = (sampleRate > 0.0) ? sampleRate : 48000.0;

    // Linear stages run at the host rate.
    hpf_.prepare(sampleRate_);
    lpf_.prepare(sampleRate_);

    // The oversampled path is prepared first so that the transformer can be
    // readied at the correct high rate.
    oversampler_.prepare(sampleRate_, blockSize);

    // The transformer model must run at the oversampled rate. It is a
    // quasi-static model with no rate-dependent configuration, so only its
    // state is reset here.
    transformer_.reset();

    reset();
    updateLatency();
}

void FilterChain::reset() noexcept
{
    hpf_.reset();
    lpf_.reset();
    transformer_.reset();
    oversampler_.reset();
}

// -----------------------------------------------------------------------------
//  Reconfiguration
// -----------------------------------------------------------------------------

void FilterChain::setOversamplingFactor(int factor)
{
    if (factor < 1 || factor == oversampler_.factor()) {
        return;
    }

    oversampler_.setFactor(factor);

    // Re-derive latency after the oversampled path changes.
    updateLatency();
}

// -----------------------------------------------------------------------------
//  Processing
// -----------------------------------------------------------------------------

double FilterChain::process(double x) noexcept
{
    // Stage 1 -- high-pass. Always active, outside the bypass test.
    x = hpf_.process(x);

    // Stage 2 -- EQ bands. Engaged only when bypass is false.
    if (!bypass_) {
        x = lpf_.process(x);
    }

    // Stage 3 -- coupling transformer. Nonlinear, always in circuit, driven
    // at the oversampled rate to prevent aliasing.
    x = oversampler_.processSample(
            x,
            [this](double s) noexcept { return transformer_.process(s); });

    return x;
}

// -----------------------------------------------------------------------------
//  Latency reporting
//
//  The linear sections are pure biquads and contribute no algorithmic
//  latency. Only the oversampled path introduces delay, and it is reported
//  in base-rate samples.
// -----------------------------------------------------------------------------

void FilterChain::updateLatency() noexcept
{
    latency_ = hpf_.latencySamples()
             + lpf_.latencySamples()
             + oversampler_.latencySamples();
}

} // namespace iem500
