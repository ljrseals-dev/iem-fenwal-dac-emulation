// =============================================================================
//  Oversampler.h
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Streaming polyphase-FIR oversampler for nonlinear stages.
//
//  Description
//  -----------
//  A single windowed-sinc prototype is derived from `factor()` and
//  `tapsPerPhase_`. Interpolation uses the prototype's polyphase
//  decomposition with the standard `factor` gain compensation for zero
//  stuffing. Decimation uses the prototype directly.
//
//  The per-sample entry point `processSample` invokes the supplied callable
//  exactly `factor()` times per call. This keeps the nonlinear stage entirely
//  inside the oversampled domain without requiring block-based plumbing.
//
//  IMPORTANT
//  ---------
//  Aliasing rejection is finite and depends on the prototype length. Measure
//  it for your configuration. Do not assume a fixed rejection figure, and do
//  not substitute a gentle analog-style low-pass for the decimation filter:
//  doing so reintroduces exactly the aliasing this module exists to remove.
//
//  Thread safety
//  -------------
//  Not thread-safe. Drive from a single audio thread. `prepare` and
//  `setFactor` must not be called concurrently with `processSample`.
// =============================================================================

#pragma once

#include <cstddef>
#include <vector>

namespace iem500 {

// -----------------------------------------------------------------------------
//  Oversampler
// -----------------------------------------------------------------------------

class Oversampler {
public:
    // Constructs an oversampler with the requested factor. Factors below 1
    // are clamped to 1, which disables oversampling.
    explicit Oversampler(int factor = 8);

    ~Oversampler() noexcept = default;

    Oversampler(const Oversampler&)            = default;
    Oversampler& operator=(const Oversampler&) = default;
    Oversampler(Oversampler&&) noexcept        = default;
    Oversampler& operator=(Oversampler&&) noexcept = default;

    // Prepares internal state for the given base sample rate.
    // `blockSize` is reserved for future block-based paths and is currently
    // unused; the streaming path allocates fixed state in `allocateState`.
    void prepare(double baseSampleRate, std::size_t blockSize = 512);

    // Clears all history. Does not alter coefficients.
    void reset() noexcept;

    // Rebuilds the prototype for a new factor and reallocates state.
    // No-op when `factor` equals the current factor.
    void setFactor(int factor);

    // -------------------------------------------------------------------------
    //  Introspection
    // -------------------------------------------------------------------------

    int    factor()         const noexcept { return factor_; }
    double latencySamples() const noexcept { return latencyBase_; }
    double baseSampleRate() const noexcept { return baseFs_; }

    // -------------------------------------------------------------------------
    //  Streaming entry point
    //
    //  `fn` is a callable accepting and returning double. It is invoked
    //  `factor()` times per call and must be real-time safe.
    // -------------------------------------------------------------------------

    template <typename Fn>
    double processSample(double x, Fn&& fn) noexcept
    {
        if (factor_ <= 1 || upHistSize_ == 0 || downHistSize_ == 0) {
            return fn(x);
        }

        // Push the newest base-rate input into the interpolation history.
        upHistory_[static_cast<std::size_t>(upWrite_)] = x;
        upWrite_ = (upWrite_ + 1) % upHistSize_;

        // For each interpolation phase, compute one high-rate sample, hand it
        // to the nonlinear stage, and push the result into the decimation
        // history.
        for (int p = 0; p < factor_; ++p) {
            double acc = 0.0;
            int idx = upWrite_ - 1;
            const int base = p * tapsPerPhase_;

            for (int t = 0; t < tapsPerPhase_; ++t) {
                if (idx < 0) {
                    idx += upHistSize_;
                }
                acc += upCoeffs_[static_cast<std::size_t>(base + t)]
                     * upHistory_[static_cast<std::size_t>(idx)];
                --idx;
            }

            const double yHigh = fn(acc);

            downHistory_[static_cast<std::size_t>(downWrite_)] = yHigh;
            downWrite_ = (downWrite_ + 1) % downHistSize_;
        }

        // Full FIR over the decimation history. No additional scaling is
        // required: the prototype is normalized to unity DC gain.
        double acc = 0.0;
        int idx = downWrite_ - 1;

        for (int k = 0; k < downHistSize_; ++k) {
            if (idx < 0) {
                idx += downHistSize_;
            }
            acc += downCoeffs_[static_cast<std::size_t>(k)]
                 * downHistory_[static_cast<std::size_t>(idx)];
            --idx;
        }

        return acc;
    }

private:
    void buildCoefficients();
    void allocateState();

    int factor_{1};
    int tapsPerPhase_{31};

    int upHistSize_{0};
    int downHistSize_{0};
    int upWrite_{0};
    int downWrite_{0};

    std::vector<double> upCoeffs_;           // factor_ * tapsPerPhase_
    std::vector<double> downCoeffs_;         // factor_ * tapsPerPhase_

    std::vector<double> upHistory_;          // tapsPerPhase_ entries
    std::vector<double> downHistory_;        // factor_ * tapsPerPhase_ entries

    double baseFs_{48000.0};
    double latencyBase_{0.0};
};

} // namespace iem500
