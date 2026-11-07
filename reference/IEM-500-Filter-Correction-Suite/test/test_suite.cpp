// =============================================================================
//  test_suite.cpp
//
//  Project : IEM-500 Filter Correction Suite
//  Purpose : Regression tests for the corrected filter chain.
//
//  Coverage
//  --------
//    1. HPF attenuates DC.
//    2. LPF passes DC.
//    3. Chain output remains finite over a long run.
//    4. Low-frequency roll-off is present while the EQ is bypassed, proving
//       the high-pass section is not removed by the bypass control.
//    5. Low-frequency roll-off is present with the EQ engaged.
//    6. The hysteresis model remains bounded and finite.
//    7. The oversampler preserves an in-band sine with approximately unity
//       passband gain.
//
//  Notes
//  -----
//  These tests exercise internal consistency and the specific architectural
//  correction documented in the paper. They do not constitute a hardware
//  null test and do not establish equivalence to any physical unit.
//
//  Build
//  -----
//      cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
//      cmake --build build --config Release
//      ctest --test-dir build --output-on-failure
// =============================================================================

#include "FilterChain.h"
#include "HpfSection.h"
#include "LpfSection.h"
#include "JilesAtherton.h"
#include "Oversampler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFs = 48000.0;

// Root-mean-square of a signal, optionally skipping an initial settling
// region to avoid transient energy dominating the measurement.
double rms(const std::vector<double>& v, int skip = 0)
{
    double sum = 0.0;
    int    n   = 0;

    for (int i = skip; i < static_cast<int>(v.size()); ++i) {
        const double s = v[static_cast<std::size_t>(i)];
        sum += s * s;
        ++n;
    }

    return std::sqrt(sum / static_cast<double>(std::max(1, n)));
}

} // namespace

int main()
{
    // -------------------------------------------------------------------------
    //  Test 1 -- HPF attenuates DC.
    // -------------------------------------------------------------------------
    {
        iem500::HpfSection hpf;
        hpf.prepare(kFs);
        hpf.setParameters({80.0, 0.7071067811865476});

        double y = 0.0;
        for (int i = 0; i < 4800; ++i) {
            y = hpf.process(1.0);
        }

        assert(std::abs(y) < 1.0e-3);
    }

    // -------------------------------------------------------------------------
    //  Test 2 -- LPF passes DC.
    // -------------------------------------------------------------------------
    {
        iem500::LpfSection lpf;
        lpf.prepare(kFs);
        lpf.setParameters({12000.0, 0.7071067811865476, 0.0, false});

        double y = 0.0;
        for (int i = 0; i < 4800; ++i) {
            y = lpf.process(1.0);
        }

        assert(std::abs(y - 1.0) < 1.0e-3);
    }

    // -------------------------------------------------------------------------
    //  Test 3 -- Chain output is finite over a long run.
    // -------------------------------------------------------------------------
    {
        iem500::FilterChain c;
        c.prepare(kFs, 512);

        double maxAbs = 0.0;

        for (int i = 0; i < 48000; ++i) {
            const double x = std::sin(2.0 * kPi * 1000.0 * i / kFs);
            const double y = c.process(x);

            assert(std::isfinite(y));
            maxAbs = std::max(maxAbs, std::abs(y));
        }

        assert(maxAbs < 4.0);
    }

    // -------------------------------------------------------------------------
    //  Test 4 -- Bypass does not remove the high-pass section.
    //
    //  With the EQ bands disengaged, a 20 Hz tone must still be attenuated
    //  relative to a 1 kHz tone. This is the central architectural assertion
    //  of the correction.
    // -------------------------------------------------------------------------
    {
        iem500::FilterChain c;
        c.prepare(kFs, 512);
        c.setBypass(true);

        auto measure = [&](double freq) {
            c.reset();

            std::vector<double> out;
            out.reserve(48000);

            for (int i = 0; i < 48000; ++i) {
                const double x = std::sin(2.0 * kPi * freq * i / kFs);
                out.push_back(c.process(x));
            }

            return rms(out, 4800);
        };

        const double r20 = measure(20.0);
        const double r1k = measure(1000.0);

        assert(r1k > 1.0e-6);
        assert(r20 < 0.30 * r1k);
    }

    // -------------------------------------------------------------------------
    //  Test 5 -- Low-frequency roll-off is present with the EQ engaged.
    // -------------------------------------------------------------------------
    {
        iem500::FilterChain c;
        c.prepare(kFs, 512);
        c.setBypass(false);
        c.reset();

        std::vector<double> out;
        out.reserve(48000);

        for (int i = 0; i < 48000; ++i) {
            const double x = std::sin(2.0 * kPi * 20.0 * i / kFs);
            out.push_back(c.process(x));
        }

        const double r20 = rms(out, 4800);
        assert(r20 < 0.30);
    }

    // -------------------------------------------------------------------------
    //  Test 6 -- Hysteresis model remains bounded and finite.
    // -------------------------------------------------------------------------
    {
        iem500::JilesAtherton ja;
        ja.setParameters({});

        double peak = 0.0;

        for (int i = 0; i < 20000; ++i) {
            const double h = std::sin(2.0 * kPi * i / 2000.0);
            const double y = ja.process(h);

            assert(std::isfinite(y));
            peak = std::max(peak, std::abs(y));
        }

        assert(peak < 10.0);
    }

    // -------------------------------------------------------------------------
    //  Test 7 -- Oversampler preserves an in-band sine.
    // -------------------------------------------------------------------------
    {
        iem500::Oversampler os(4);
        os.prepare(kFs, 512);

        double peak = 0.0;

        for (int i = 0; i < 48000; ++i) {
            const double x = std::sin(2.0 * kPi * 1000.0 * i / kFs);
            const double y = os.processSample(
                x,
                [](double s) noexcept { return s; });

            if (i > 5000) {
                peak = std::max(peak, std::abs(y));
            }
        }

        assert(std::abs(peak - 1.0) < 0.15);
    }

    std::cout << "iem500_tests: PASS\n";
    return 0;
}
