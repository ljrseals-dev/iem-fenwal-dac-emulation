// =============================================================================
//  HpfSection.h
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Always-active high-pass reference section.
//
//  Description
//  -----------
//  Second-order digital high-pass derived from an analog prototype using the
//  bilinear transform with frequency prewarping.
//
//  This module models the high-pass behavior of the reference inductor EQ
//  architecture. In that architecture the high-pass section is permanently
//  in the signal path: it is not removed when the equalizer bands are
//  bypassed. `FilterChain` therefore instantiates this section outside its
//  bypass conditional.
//
//  This is a linear reference implementation, not a passive LC wave digital
//  filter reconstruction. Frequency-response agreement with hardware is
//  UNVERIFIED and must be established by measurement before any accuracy
//  claim is made.
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

struct HpfParameters {
    double cornerHz{80.0};                   // High-pass corner frequency.
    double q{0.7071067811865476};            // Butterworth-aligned Q.
};

// -----------------------------------------------------------------------------
//  HpfSection
// -----------------------------------------------------------------------------

class HpfSection {
public:
    HpfSection() noexcept = default;
    ~HpfSection() noexcept = default;

    HpfSection(const HpfSection&)            = default;
    HpfSection& operator=(const HpfSection&) = default;
    HpfSection(HpfSection&&) noexcept        = default;
    HpfSection& operator=(HpfSection&&) noexcept = default;

    // Prepares the section for the given sample rate. Resets internal state.
    // `sampleRate` is clamped to a minimum of 8000 Hz.
    void prepare(double sampleRate) noexcept;

    // Applies a new parameter set. Coefficients are recomputed immediately.
    // The corner frequency is clamped to [1 Hz, 0.45 * sampleRate].
    void setParameters(const HpfParameters& p) noexcept;

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
    HpfParameters p_{};

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
