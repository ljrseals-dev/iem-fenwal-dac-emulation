// =============================================================================
//  LpfSection.h
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Low-pass and high-shelf reference section.
//
//  Description
//  -----------
//  When `shelf` is false, a second-order low-pass is realized from an analog
//  prototype with bilinear frequency prewarping. When `shelf` is true, a
//  high-shelf is realized in the RBJ cookbook form.
//
//  This module stands in for the equalizer's high-frequency shaping stage.
//  It is a linear reference implementation, not a passive LC wave digital
//  filter reconstruction.
//
//  Thread safety
//  -------------
//  Not thread-safe. All methods must be called from a single audio thread,
//  with `prepare` and `setParameters` occurring outside the audio callback
//  or under a lock.
// =============================================================================

#pragma once

#include <cmath>

namespace iem500 {

// -----------------------------------------------------------------------------
//  Configuration
// -----------------------------------------------------------------------------

struct LpfParameters {
    double cornerHz{12000.0};                // Corner frequency.
    double q{0.7071067811865476};            // Butterworth-aligned Q.
    double gainDb{0.0};                      // Shelf gain. Ignored when shelf=false.
    bool   shelf{false};                     // False: low-pass. True: high-shelf.
};

// -----------------------------------------------------------------------------
//  LpfSection
// -----------------------------------------------------------------------------

class LpfSection {
public:
    LpfSection() noexcept = default;
    ~LpfSection() noexcept = default;

    LpfSection(const LpfSection&)            = default;
    LpfSection& operator=(const LpfSection&) = default;
    LpfSection(LpfSection&&) noexcept        = default;
    LpfSection& operator=(LpfSection&&) noexcept = default;

    // Prepares the section for the given sample rate. Resets internal state.
    // `sampleRate` is clamped to a minimum of 8000 Hz.
    void prepare(double sampleRate) noexcept;

    // Applies a new parameter set. Coefficients are recomputed immediately.
    // The corner frequency is clamped to [10 Hz, 0.45 * sampleRate].
    void setParameters(const LpfParameters& p) noexcept;

    // Clears the filter state. Does not alter coefficients.
    void reset() noexcept;

    // Processes one sample. Returns the filtered value.
    double process(double x) noexcept;

    // -------------------------------------------------------------------------
    //  Introspection
    // -------------------------------------------------------------------------

    double sampleRate() const noexcept { return fs_; }
    double cornerHz()   const noexcept { return p_.cornerHz; }

    // This section is a pure biquad and introduces no algorithmic latency.
    double latencySamples() const noexcept { return 0.0; }

private:
    void update() noexcept;

    double        fs_{48000.0};
    LpfParameters p_{};

    // Direct Form II transposed coefficients.
    double b0_{1.0};
    double b1_{0.0};
    double b2_{0.0};
    double a1_{0.0};
    double a2_{0.0};

    // Transposed-form state.
    double z1_{0.0};
    double z2_{0.0};
};

} // namespace iem500
