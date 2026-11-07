// =============================================================================
//  JilesAtherton.h
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Quasi-static magnetic hysteresis model for the coupling stage.
//
//  Description
//  -----------
//  Development Jiles-Atherton-style quasi-static magnetic hysteresis model.
//
//  The reference architecture couples its input and output through magnetic
//  transformer stages. Those stages introduce frequency-dependent saturation
//  and harmonic generation that a linear filter chain cannot reproduce. This
//  module provides an experimental hysteresis primitive for studying that
//  behavior.
//
//  IMPORTANT LIMITATIONS
//  ---------------------
//  - This is a bounded, DSP-oriented approximation. It is NOT a Newton
//    solution of the implicit Jiles-Atherton constitutive equation.
//  - The default parameter set is generic. It has NOT been identified from
//    measurements of any specific transformer.
//  - The model must be driven at an oversampled rate. Calling `process` at
//    the host sample rate will produce aliasing.
//
//  Any accuracy claim regarding a specific hardware transformer requires
//  measured major and minor hysteresis loops and a proper parameter
//  identification procedure.
//
//  Thread safety
//  -------------
//  Not thread-safe. Drive from a single audio thread.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>

namespace iem500 {

// -----------------------------------------------------------------------------
//  Configuration
//
//  Field : Ms, a, k, c, alpha follow the standard Jiles-Atherton naming.
//          scaleH and scaleM are input and output scaling factors that map
//          the normalized model domain onto the DSP signal domain.
// -----------------------------------------------------------------------------

struct JilesAthertonParameters {
    double Ms{1.0};                          // Saturation magnetization.
    double a{0.25};                          // Thermal energy parameter.
    double k{0.40};                          // Pinning site energy.
    double c{0.15};                          // Reversibility coefficient [0, 1].
    double alpha{0.02};                      // Mean field parameter.
    double scaleH{1.0};                      // Input field scaling.
    double scaleM{1.0};                      // Output magnetization scaling.
};

// -----------------------------------------------------------------------------
//  JilesAtherton
// -----------------------------------------------------------------------------

class JilesAtherton {
public:
    JilesAtherton() noexcept = default;
    ~JilesAtherton() noexcept = default;

    JilesAtherton(const JilesAtherton&)            = default;
    JilesAtherton& operator=(const JilesAtherton&) = default;
    JilesAtherton(JilesAtherton&&) noexcept        = default;
    JilesAtherton& operator=(JilesAtherton&&) noexcept = default;

    // Applies a new parameter set. Values are clamped to physically
    // realizable ranges.
    void setParameters(const JilesAthertonParameters& p) noexcept;

    // Clears magnetization and field history.
    void reset() noexcept;

    // Advances the model by one sample. `H` is the applied field.
    // Returns the resulting magnetization, scaled by `scaleM`.
    // Intended to be called at an oversampled rate.
    double process(double H) noexcept;

    // -------------------------------------------------------------------------
    //  Introspection
    // -------------------------------------------------------------------------

    double magnetization()  const noexcept { return M_; }
    double previousField()  const noexcept { return Hprev_; }

private:
    // Numerically stable evaluation of the Langevin function.
    static double safeLangevin(double x) noexcept;

    JilesAthertonParameters p_{};
    double M_{0.0};                          // Current magnetization.
    double Hprev_{0.0};                      // Previous applied field.
};

} // namespace iem500
