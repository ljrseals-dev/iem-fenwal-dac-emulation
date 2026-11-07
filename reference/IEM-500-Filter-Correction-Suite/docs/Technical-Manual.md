# IEM-500 Filter Correction Suite — Technical Manual

**Document version:** 0.1.0
**Library version:** 0.1.0
**Language:** C++17
**Build system:** CMake 3.16+
**Status:** Research scaffold — see §13

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Scope and Audience](#2-scope-and-audience)
3. [Nomenclature](#3-nomenclature)
4. [System Requirements](#4-system-requirements)
5. [Architecture Overview](#5-architecture-overview)
6. [Build and Installation](#6-build-and-installation)
7. [Module Reference](#7-module-reference)
8. [Integration Guide](#8-integration-guide)
9. [Numerical Considerations](#9-numerical-considerations)
10. [Testing](#10-testing)
11. [Performance](#11-performance)
12. [Troubleshooting](#12-troubleshooting)
13. [Verification Status and Limitations](#13-verification-status-and-limitations)
14. [Appendix A — Parameter Quick Reference](#appendix-a--parameter-quick-reference)
15. [Appendix B — File Manifest](#appendix-b--file-manifest)
16. [Appendix C — Glossary](#appendix-c--glossary)

---

## 1. Introduction

The IEM-500 Filter Correction Suite is a C++17 reference implementation of a
corrected signal chain for inductor-based equalizer emulation. It addresses two
structural defects that are independent of frequency-response accuracy:

1. **Bypass architectural defect.** In conventional plugin implementations, the
   high-pass section is placed inside the equalizer bypass conditional. When
   the equalizer bands are disengaged, the high-pass filter is also removed
   from the signal path. The reference hardware architecture keeps the
   high-pass section permanently in circuit.

2. **Nonlinear magnetic stage defect.** The coupling transformer is a
   hysteretic magnetic component that generates harmonics above the audio band.
   Evaluating such a model at the host sample rate folds those harmonics back
   into the audible spectrum as inharmonic distortion.

The suite provides corrected bypass semantics, prewarped reference filter
sections, a bounded hysteresis model, and a streaming polyphase-FIR
oversampler to drive the nonlinear stage without aliasing.

This manual documents the public API, the internal architecture, the build
procedure, the test suite, and the limits of what has been verified.

---

## 2. Scope and Audience

**Intended audience**

- DSP engineers integrating the library into a plugin or offline processing tool.
- Researchers reproducing or extending the correction described in the
  accompanying paper.
- Reviewers auditing the implementation's claims.

**In scope**

- Public API of all five modules.
- Signal flow, threading model, and latency model.
- Build, test, and integration procedures.
- Numerical behavior and stability constraints.

**Out of scope**

- Host wrapper, GUI, preset management, and installer construction.
- Parameter automation and preset serialization formats.
- Hardware measurement procedures (referenced, not specified).
- Any claim of acoustic equivalence to a physical device.

---

## 3. Nomenclature

The library uses neutral terminology throughout. No manufacturer or model
attribution appears in identifiers, comments, filenames, or test data.

| Term | Meaning |
|---|---|
| `IEM-500` | Neutral identifier for the reference inductor EQ architecture |
| `kHpfCorner` | Fixed high-pass corner used by the reference architecture |
| `kLpfCorner` | Selectable low-pass or filter corner |
| `moduleBypass` | EQ band bypass control |
| `couplingTransformer` | Generic input and output transformer stage |
| `inductorNetwork` | Generic passive LC network under investigation |
| `reference section` | A linear digital filter derived from an analog prototype |
| `reconstruction` | A topology-complete model of the passive network |
| `UNVERIFIED` | A claim that has not been established by measurement |

Throughout this manual, *reference section* and *reconstruction* are used
precisely. The current implementation provides reference sections only.

---

## 4. System Requirements

### 4.1 Build requirements

| Requirement | Minimum | Notes |
|---|---|---|
| C++ standard | C++17 | `cxx_std_17` is set as a public compile feature |
| CMake | 3.16 | Required for `target_include_directories` generator expressions |
| Compiler | MSVC 2019, GCC 9, Clang 10 | Any conforming C++17 compiler should work |
| External DSP framework | None | Standard library only |

### 4.2 Runtime requirements

| Requirement | Notes |
|---|---|
| Floating point | IEEE 754 double precision required for internal DSP |
| Denormal support | Recommended; flush-to-zero is advisable (see §9.2) |
| Heap allocation | Not permitted on the audio thread (see §5.3) |

### 4.3 Tested configurations

The test suite is known to build and pass under Release configurations of
MSVC, GCC, and Clang on x86-64. Other architectures have not been exercised.

---

## 5. Architecture Overview

### 5.1 Signal flow

```
                 ┌──────────────────────────────────────┐
  in ───────────►│  HpfSection   (ALWAYS ACTIVE)        │
                 └──────────────────┬───────────────────┘
                                    │
                          ┌─────────▼─────────┐
                          │  bypass_ ?        │
                          └────┬─────────┬────┘
                        true   │         │  false
                               ▼         ▼
                    ┌──────────────┐   (pass through)
                    │  LpfSection  │
                    │  EQ bands    │
                    └──────┬───────┘
                           │
                           ▼
                 ┌──────────────────────┐
                 │  Oversampler         │
                 │   ├─ upsample        │
                 │   ├─ JilesAtherton   │  ← runs at factor × host rate
                 │   └─ decimate        │
                 └──────────┬───────────┘
                            ▼
                           out
```

The high-pass section executes unconditionally. The equalizer band section is
gated by the bypass flag. The coupling transformer executes unconditionally,
inside the oversampled domain.

### 5.2 Module map

| Module | Header | Responsibility |
|---|---|---|
| `HpfSection` | `include/HpfSection.h` | Always-active high-pass reference section |
| `LpfSection` | `include/LpfSection.h` | Low-pass and high-shelf reference section |
| `JilesAtherton` | `include/JilesAtherton.h` | Bounded hysteresis model for the coupling stage |
| `Oversampler` | `include/Oversampler.h` | Streaming polyphase-FIR oversampler |
| `FilterChain` | `include/FilterChain.h` | Corrected top-level signal chain |

Dependency direction is strictly downward: `FilterChain` depends on the other
four; no other module depends on `FilterChain`. `HpfSection` and `LpfSection`
are independent of each other.

### 5.3 Threading model

| Method class | Thread | Real-time safe |
|---|---|---|
| `prepare()` | Main / setup thread | No — allocates |
| `reset()` | Main thread, or audio thread if no allocation | Yes |
| `setParameters()` | Main thread, or audio thread | Yes — arithmetic only |
| `setBypass()` | Main thread, or audio thread | Yes — flag assignment |
| `setOversamplingFactor()` | Main thread | No — allocates |
| `process()` | Audio thread | Yes — no allocation, no locks |

**Invariant.** `process()` performs no heap allocation, no locking, and no
system calls. All buffers are sized during `prepare()`.

**Constraint.** `prepare()` and `setOversamplingFactor()` must not be called
concurrently with `process()`. In a plugin host, these belong in
`prepareToPlay()` and a suspended state respectively.

No module in the suite is internally synchronized. Concurrent access from
multiple threads is undefined behavior.

### 5.4 Latency model

| Stage | Algorithmic latency |
|---|---|
| `HpfSection` | 0 samples |
| `LpfSection` | 0 samples |
| `Oversampler` | `(downHistSize - 1) / (2 × factor)` base-rate samples |
| `FilterChain` | Sum of the above |

With the default configuration (8× oversampling, 31 taps per phase),
`downHistSize` is 248, giving an oversampler latency of approximately 15.44
base-rate samples. The chain reports latency in base-rate samples via
`FilterChain::latencySamples()`.

**Latency is constant across bypass states.** Because the high-pass section and
the oversampled transformer path are always active, toggling bypass does not
change the reported latency. Host delay compensation therefore does not need
to be recalculated on bypass transitions.

---

## 6. Build and Installation

### 6.1 Standard build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

### 6.2 Build artifacts

| Artifact | Type | Description |
|---|---|---|
| `iem500_filters` | Static library | The five DSP modules |
| `iem500_tests` | Executable | Seven-case regression suite |

### 6.3 Consuming the library

```cmake
add_subdirectory(path/to/iem500)
target_link_libraries(your_target PRIVATE iem500_filters)
```

The public include directory is exported via `target_include_directories`, so
no manual include path configuration is required.

### 6.4 Compiler flags applied

The build enables strict warnings by default:

- MSVC: `/W4 /permissive-`
- GCC / Clang: `-Wall -Wextra -Wpedantic`

If you vendor these sources into a project with warnings-as-errors enabled,
expect clean compilation. Report any warning as a defect.

---

## 7. Module Reference

### 7.1 `HpfSection`

**Purpose.** Always-active high-pass reference section.

**Design.** Second-order high-pass derived from an analog prototype using the
bilinear transform with frequency prewarping, implemented in Direct Form II
transposed.

**Header.** `include/HpfSection.h`

#### Configuration

```cpp
struct HpfParameters {
    double cornerHz{80.0};                   // High-pass corner frequency
    double q{0.7071067811865476};            // Butterworth-aligned Q
};
```

#### API

| Method | Description |
|---|---|
| `void prepare(double sampleRate) noexcept` | Configures for a sample rate; clamps to ≥ 8000 Hz; resets state |
| `void setParameters(const HpfParameters& p) noexcept` | Applies parameters; recomputes coefficients immediately |
| `void reset() noexcept` | Clears filter state; coefficients unchanged |
| `double process(double x) noexcept` | Processes one sample |
| `double sampleRate() const noexcept` | Current sample rate |
| `double cornerHz() const noexcept` | Current corner frequency |
| `double latencySamples() const noexcept` | Always 0.0 |

#### Parameter clamping

| Parameter | Clamp |
|---|---|
| `cornerHz` | `[1.0, 0.45 × sampleRate]` |
| `q` | `≥ 0.05` |

#### Behavior notes

- The corner frequency is prewarped via `w = 2·fs·tan(π·f0/fs)`, so the
  digital corner matches the requested analog corner.
- Because this section sits outside the bypass conditional in `FilterChain`,
  it must never be reset by a bypass transition. Resetting it would inject a
  transient at the moment the user expects a seamless change.
- This is a linear reference section. It is not a passive LC reconstruction
  and does not model inductor saturation.

#### Example

```cpp
iem500::HpfSection hpf;
hpf.prepare(48000.0);
hpf.setParameters({80.0, 0.7071067811865476});

for (int i = 0; i < numSamples; ++i) {
    buffer[i] = hpf.process(buffer[i]);
}
```

---

### 7.2 `LpfSection`

**Purpose.** Low-pass and high-shelf reference section standing in for the
equalizer's high-frequency shaping stage.

**Design.** When `shelf` is false, a second-order low-pass from an analog
prototype with bilinear prewarping. When `shelf` is true, an RBJ cookbook
high-shelf.

**Header.** `include/LpfSection.h`

#### Configuration

```cpp
struct LpfParameters {
    double cornerHz{12000.0};                // Corner frequency
    double q{0.7071067811865476};            // Butterworth-aligned Q
    double gainDb{0.0};                      // Shelf gain; ignored when shelf=false
    bool   shelf{false};                     // false: low-pass, true: high-shelf
};
```

#### API

| Method | Description |
|---|---|
| `void prepare(double sampleRate) noexcept` | Configures for a sample rate; clamps to ≥ 8000 Hz; resets state |
| `void setParameters(const LpfParameters& p) noexcept` | Applies parameters; recomputes coefficients immediately |
| `void reset() noexcept` | Clears filter state; coefficients unchanged |
| `double process(double x) noexcept` | Processes one sample |
| `double sampleRate() const noexcept` | Current sample rate |
| `double cornerHz() const noexcept` | Current corner frequency |
| `double latencySamples() const noexcept` | Always 0.0 |

#### Parameter clamping

| Parameter | Clamp |
|---|---|
| `cornerHz` | `[10.0, 0.45 × sampleRate]` |
| `q` | `≥ 0.05` |
| `gainDb` | Unclamped; used only in shelf mode |

#### Behavior notes

- In filter mode, the analog prototype is a second-order low-pass; in shelf
  mode, the RBJ high-shelf formulation is used with `A = 10^(gainDb/40)`.
- This section is gated by `FilterChain`'s bypass conditional. Disengaging
  bypass removes it from the signal path.
- This is a linear reference section. It does not model the shared inductor
  network's inter-band loading.

---

### 7.3 `JilesAtherton`

**Purpose.** Bounded, quasi-static magnetic hysteresis model for the coupling
transformer stage.

**Header.** `include/JilesAtherton.h`

#### Configuration

```cpp
struct JilesAthertonParameters {
    double Ms{1.0};                          // Saturation magnetization
    double a{0.25};                          // Thermal energy parameter
    double k{0.40};                          // Pinning site energy
    double c{0.15};                          // Reversibility coefficient [0, 1]
    double alpha{0.02};                      // Mean field parameter
    double scaleH{1.0};                      // Input field scaling
    double scaleM{1.0};                      // Output magnetization scaling
};
```

#### API

| Method | Description |
|---|---|
| `void setParameters(const JilesAthertonParameters& p) noexcept` | Applies and clamps parameters |
| `void reset() noexcept` | Clears magnetization and field history |
| `double process(double H) noexcept` | Advances one sample; returns scaled magnetization |
| `double magnetization() const noexcept` | Current magnetization state |
| `double previousField() const noexcept` | Previous applied field |

#### Parameter clamping

| Parameter | Clamp |
|---|---|
| `Ms` | `≥ 1e-12` |
| `a` | `≥ 1e-9` |
| `k` | `≥ 1e-9` |
| `c` | `[0.0, 1.0]` |
| `alpha` | `[-1.0, 1.0]` |
| `scaleH`, `scaleM` | `≥ 1e-12` |

#### Algorithm

The anhysteretic magnetization is computed from the Langevin function

```
L(x) = coth(x) - 1/x
```

evaluated with a truncated series expansion for `|x| < 1e-4` to avoid
catastrophic cancellation near the origin:

```
L(x) ≈ x/3 - x³/45 + 2x⁵/945
```

The irreversible and reversible contributions are combined in a bounded
Euler step:

```
Man  = Ms · L((h + α·M) / a)
dir  = sign(Δh)
M   += clamp(|Δh|, 0, 1) · ( dir · |Man − M| / k  +  c · (Man − M) )
M    = clamp(M, −2·Ms, 2·Ms)
```

#### Critical constraints

1. **Drive at the oversampled rate.** Calling `process()` at the host sample
   rate will produce aliasing. `FilterChain` enforces this by construction.
2. **Not a Newton solver.** This is a bounded Euler approximation, not a
   solution of the implicit constitutive equation.
3. **Generic parameters.** The defaults are not identified from measurements
   of any specific transformer. Do not present them as such.

#### Example

```cpp
iem500::JilesAtherton ja;
ja.setParameters({});        // generic defaults

// Call at the oversampled rate, not the host rate.
const double y = ja.process(h);
```

---

### 7.4 `Oversampler`

**Purpose.** Streaming polyphase-FIR oversampler for the nonlinear magnetic
stage.

**Header.** `include/Oversampler.h`

#### API

| Method | Description |
|---|---|
| `explicit Oversampler(int factor = 8)` | Constructs and builds the prototype; factor < 1 clamps to 1 |
| `void prepare(double baseSampleRate, std::size_t blockSize = 512)` | Allocates state; computes latency |
| `void reset() noexcept` | Clears history; coefficients unchanged |
| `void setFactor(int factor)` | Rebuilds prototype and reallocates state |
| `int factor() const noexcept` | Current oversampling factor |
| `double latencySamples() const noexcept` | Round-trip group delay in base-rate samples |
| `double baseSampleRate() const noexcept` | Configured base sample rate |
| `template<typename Fn> double processSample(double x, Fn&& fn) noexcept` | Streaming entry point |

#### Internal constants

| Constant | Value | Notes |
|---|---|---|
| `tapsPerPhase_` | 31 | Compile-time constant; changing it alters latency and ripple |
| `factor_` | 8 | Default; configurable at construction or via `setFactor` |

#### Prototype design

A single Blackman-windowed sinc prototype is designed with cutoff at
`fc = 0.5 / factor`, normalized to unity DC gain, then decomposed into
`factor` polyphase branches for interpolation with the standard `factor` gain
compensation for zero stuffing. Decimation runs the prototype directly over
the high-rate history.

The Blackman window is:

```
w(n) = 0.42 − 0.50·cos(2πn/M) + 0.08·cos(4πn/M)
```

#### `processSample` contract

```cpp
template <typename Fn>
double processSample(double x, Fn&& fn) noexcept;
```

- `fn` is invoked exactly `factor()` times per call.
- `fn` must accept and return `double`.
- `fn` must be real-time safe: no allocation, no locks, no I/O.
- When `factor() <= 1`, `fn` is invoked once and the input is passed through
  unchanged, bypassing the FIR path entirely.

#### Critical constraints

1. **Do not substitute a gentle analog-style low-pass for the decimation
   filter.** The prototype must have high stopband rejection. A gentle filter
   reintroduces exactly the aliasing this module exists to remove.
2. **Alias rejection is finite and unmeasured.** The rejection depends on
   prototype length, factor, and the spectral content of the nonlinearity.
   Report a measured figure for a specified configuration; do not assume one.
3. **`setFactor()` reallocates.** It is not real-time safe. Call it from a
   suspended state.

#### Example

```cpp
iem500::Oversampler os(8);
os.prepare(48000.0);

const double y = os.processSample(
    x,
    [](double s) noexcept { return std::tanh(s); });
```

---

### 7.5 `FilterChain`

**Purpose.** Corrected top-level signal chain. Owns all four stages and
enforces the ordering described in §5.1.

**Header.** `include/FilterChain.h`

**Copy semantics.** Copy construction and copy assignment are deleted. Move
construction and move assignment are defaulted.

#### API

| Method | Description |
|---|---|
| `void prepare(double sampleRate, std::size_t blockSize = 512) noexcept` | Prepares all stages; resets everything |
| `void reset() noexcept` | Clears all state; configuration unchanged |
| `void setBypass(bool bypass) noexcept` | Gates the EQ bands only |
| `bool bypass() const noexcept` | Current bypass state |
| `double process(double x) noexcept` | Processes one sample through the full chain |
| `void setOversamplingFactor(int factor)` | Reconfigures the oversampled path |
| `int oversamplingFactor() const noexcept` | Current oversampling factor |
| `double latencySamples() const noexcept` | Total chain latency in base-rate samples |
| `double sampleRate() const noexcept` | Configured sample rate |
| `HpfSection& hpf() noexcept` | Direct access to the high-pass stage |
| `LpfSection& lpf() noexcept` | Direct access to the EQ band stage |
| `JilesAtherton& transformer() noexcept` | Direct access to the hysteresis model |
| `Oversampler& oversampler() noexcept` | Direct access to the oversampler |

#### Static constant

```cpp
static constexpr int kDefaultOversampling = 8;
```

#### Bypass semantics

`setBypass(true)` disengages the EQ bands only.

| Stage | Bypass = false | Bypass = true |
|---|---|---|
| `HpfSection` | Active | **Active** |
| `LpfSection` | Active | Inactive |
| `JilesAtherton` | Active | **Active** |

The high-pass section and the coupling transformer remain in circuit in both
states. This is the central architectural correction of the suite.

Bypass transitions do **not** reset the high-pass state, so no click is
generated by the toggle.

#### `setOversamplingFactor` behavior

```cpp
void FilterChain::setOversamplingFactor(int factor)
{
    if (factor < 1 || factor == oversampler_.factor()) {
        return;
    }

    oversampler_.setFactor(factor);
    updateLatency();
}
```

This method reallocates inside `Oversampler`. It is not real-time safe and
must be called from a suspended state.

---

## 8. Integration Guide

### 8.1 Minimal host integration

```cpp
#include "FilterChain.h"

class MyProcessor
{
public:
    void prepareToPlay(double sampleRate, int /*maxBlockSize*/)
    {
        chain_.prepare(sampleRate, 512);
        chain_.hpf().setParameters({80.0, 0.7071067811865476});
        chain_.lpf().setParameters({12000.0, 0.7071067811865476, 0.0, false});
    }

    void releaseResources() { /* nothing owned by the chain */ }

    void processBlock(double* buffer, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i) {
            buffer[i] = chain_.process(buffer[i]);
        }
    }

    void setEqEngaged(bool engaged) { chain_.setBypass(!engaged); }

private:
    iem500::FilterChain chain_;
};
```

### 8.2 Bypass handling in a host

Do **not** map the host's bypass mechanism to `FilterChain::setBypass()`. The
host bypass is expected to remove all processing; `setBypass()` removes only
the EQ bands. Keep the two concepts distinct:

- **Host bypass** — stop calling `process()`, or feed silence. The host owns
  the wet/dry mix.
- **Module bypass** — `FilterChain::setBypass()`. This is the front-panel EQ
  engage control of the reference architecture.

### 8.3 Parameter changes from the audio thread

`setParameters()` on `HpfSection` and `LpfSection` recomputes coefficients
arithmetically and performs no allocation, so it is safe to call from the
audio thread. However:

- **Do not smooth coefficient changes by smoothing the parameters.** Coefficients
  are non-linear functions of the parameters. Smoothing `cornerHz` between two
  values does not produce an interpolation of the two frequency responses.
- **Prefer crossfading or state re-initialization for large parameter jumps.**
  A large corner-frequency jump with retained state can produce a transient.
- **For automated sweeps**, either run the coefficient update at a slower
  control rate with a smoothing filter on the output, or accept the artifact.

`JilesAtherton::setParameters()` is likewise arithmetic-only and audio-thread
safe, but changing hysteresis parameters mid-signal alters the loop shape and
may produce a DC step. Reset the model if the parameter change is large.

### 8.4 Sample rate changes

Call `prepare()` with the new rate. This reallocates inside `Oversampler` and
recomputes all coefficients. Latency changes with the oversampler's state size
(which is factor-dependent, not rate-dependent), so host delay compensation
usually does not need to change on a rate switch, but you should re-query
`latencySamples()` and report it again.

### 8.5 Block size

`Oversampler::prepare()` accepts a `blockSize` argument that is currently
unused; the streaming path allocates fixed state sized by `factor` and
`tapsPerPhase_`. There is no block-size dependency in the current
implementation. The parameter is reserved for a future block-based path.

### 8.6 Building a stereo instance

The chain is mono. Construct one `FilterChain` per channel. Sharing a single
instance across channels is incorrect: the filter state and hysteresis state
are per-channel.

```cpp
iem500::FilterChain left, right;
left.prepare(sampleRate);
right.prepare(sampleRate);
```

For matched behavior, prepare both with identical parameters. The two
instances will remain sample-aligned because they have identical latency.

---

## 9. Numerical Considerations

### 9.1 Precision

All internal DSP is performed in `double`. The public audio boundary is
`double` as well. If your host operates in `float`, convert at the boundary:

```cpp
x = chain_.process(static_cast<double>(floatSample));
```

Do not narrow the internal path to `float`. The hysteresis model's bounded
Euler step and the polyphase accumulator both benefit from the additional
mantissa bits.

### 9.2 Denormals

The suite does not contain explicit denormal protection. In silence, the
biquad states of `HpfSection` and `LpfSection` can decay into the denormal
range, where some processors incur a significant performance penalty.

**Recommended mitigation** (host side, not library side):

- Enable flush-to-zero and denormals-are-zero modes in your audio thread:
  - x86: set MXCSR bits FTZ (15) and DAZ (6).
  - ARM: set the FZ bit in FPCR.
- Alternatively, inject a DC offset well below the noise floor, or apply a
  tiny dither, at the chain input.

The library deliberately does not set these modes itself, because doing so
would alter the host's floating-point environment globally.

### 9.3 Stability

| Risk | Mitigation present |
|---|---|
| Divergence in the hysteresis model | Magnetization clamped to `±2·Ms`; Euler step magnitude clamped to `[0, 1]` |
| Catastrophic cancellation in the Langevin function | Truncated series expansion for `|x| < 1e-4` |
| Coefficient blow-up at extreme parameters | `cornerHz` clamped to `0.45 × sampleRate`; `q` clamped to `≥ 0.05` |
| Filter instability | Direct Form II transposed with poles guaranteed inside the unit circle by the bilinear transform |

Known residual risks:

- The bounded Euler step in `JilesAtherton` is not unconditionally stable for
  arbitrary parameter sets. If you supply parameters far outside the default
  range, verify stability empirically with a swept excitation.
- There is no DC blocker after the hysteresis stage. A persistently asymmetric
  input can accumulate a DC offset in the magnetization state.

### 9.4 Denormal and NaN propagation

`HpfSection` and `LpfSection` do not contain NaN guards. A NaN entering the
chain will propagate indefinitely. If your host can deliver NaN (for example,
from an uninitialized buffer), sanitize at the boundary.

`JilesAtherton::process()` clamps its output state, which bounds the effect of
an out-of-range input, but does not remove a NaN. Adding a `std::isfinite`
check at the chain input is a reasonable defensive measure in development
builds.

---

## 10. Testing

### 10.1 Running the suite

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

A passing run prints:

```
iem500_tests: PASS
```

CTest applies a 120-second timeout to the test executable.

### 10.2 Test descriptions

| # | Name | Assertion | Purpose |
|---|---|---|---|
| 1 | HPF attenuates DC | Steady-state output for a unit DC input is below `1e-3` | Confirms the high-pass section is functional |
| 2 | LPF passes DC | Steady-state output is within `1e-3` of unity | Confirms the low-pass section is functional |
| 3 | Chain stability | Output finite over 48000 samples; peak below 4.0 | Confirms no divergence with the default configuration |
| 4 | **Bypass invariant** | With EQ bypassed, 20 Hz RMS < 0.30 × 1 kHz RMS | **Central architectural assertion** |
| 5 | Engaged roll-off | With EQ engaged, 20 Hz RMS < 0.30 | Confirms the HPF operates in both states |
| 6 | Hysteresis boundedness | Finite output; peak below 10.0 over 20000 samples | Confirms the nonlinear stage does not diverge |
| 7 | Oversampler passband | In-band sine preserved; peak within 0.15 of unity | Confirms the oversampler has approximately unity passband gain |

### 10.3 Interpreting Test 4

Test 4 is the reason this suite exists. On a conventional implementation —
where the high-pass section is inside the bypass conditional — bypassing the
EQ removes the high-pass filter and the 20 Hz tone passes at approximately
unity, failing the assertion.

If you are porting the correction to another codebase, this is the test to
carry over. It is a direct, falsifiable check of the architectural invariant.

### 10.4 Interpreting Test 7

Test 7 uses a tolerance of `0.15`, which assumes a Blackman-windowed prototype
with 31 taps per phase. If you change `tapsPerPhase_`, the group delay and
passband ripple change, and the tolerance must be re-derived. A tighter
tolerance with a shorter prototype will fail.

### 10.5 Adding tests

When adding a test:

1. Append it to `test/test_suite.cpp` as a new scoped block with a numbered
   comment header.
2. Use `assert` for hard invariants; use `std::cout` for informational output.
3. Skip an initial settling region in RMS measurements (the existing tests
   skip 4800 samples of a 48000-sample run).
4. Never claim hardware accuracy in a test name or comment. The suite tests
   internal consistency and architecture, not equivalence.

### 10.6 Test suite limitations

The suite does **not** test:

- Alias rejection magnitude.
- Frequency response accuracy against an independently solved analog prototype.
- Hardware null-test agreement.
- Denormal behavior or NaN propagation.
- Multi-channel alignment.
- Real-time performance under load.

These are open work items, not oversights.

---

## 11. Performance

### 11.1 CPU cost model

The dominant cost is the oversampled path. Per base-rate sample, the chain
performs:

| Operation | Count |
|---|---|
| High-pass biquad | 1 |
| EQ band biquad (when engaged) | 1 |
| Polyphase interpolation | `factor` × `tapsPerPhase` MACs |
| Nonlinear callable invocations | `factor` |
| Decimation FIR | `factor` × `tapsPerPhase` MACs |

With the defaults (`factor = 8`, `tapsPerPhase = 31`), the FIR work alone is
approximately 496 multiply-accumulate operations per base-rate sample.

### 11.2 Oversampling factor tradeoff

| Factor | FIR MACs / sample | Relative cost | Notes |
|---|---|---|---|
| 1 | 0 | 1× | Nonlinear stage runs at host rate; aliasing expected |
| 2 | 124 | ~2× | Marginal improvement; low-order harmonics only |
| 4 | 248 | ~4× | Common minimum for nonlinear stages |
| 8 | 496 | ~8× | Default; conservative |
| 16 | 992 | ~16× | Diminishing returns for this model class |

The cost scales linearly with `factor`, but the reduction in aliasing scales
with the stopband rejection of the prototype, which is a function of
`tapsPerPhase_`, not `factor`. Increasing the factor without a sufficiently
long prototype yields limited benefit.

### 11.3 Memory

State sizes are fixed at construction and do not grow.

| Buffer | Size (doubles) |
|---|---|
| `upCoeffs_` | `factor × tapsPerPhase` |
| `downCoeffs_` | `factor × tapsPerPhase` |
| `upHistory_` | `tapsPerPhase` |
| `downHistory_` | `factor × tapsPerPhase` |

With the defaults, that is 248 + 248 + 31 + 248 = 775 doubles, approximately
6.2 KB per `Oversampler` instance. A stereo pair uses roughly 12.4 KB.

### 11.4 Reducing cost

- Set `factor = 4` if you can tolerate a higher alias floor.
- Reduce `tapsPerPhase_` if your aliasing requirement is modest. This also
  reduces latency.
- Disengage the EQ bands when not in use. This does not remove the high-pass
  section or the transformer stage — by design.

There is no bypass path that removes the transformer stage. If you need a
truly idle path, do not call `process()`.

---

## 12. Troubleshooting

### 12.1 The plugin sounds thin even with the EQ bypassed

**Expected.** The high-pass section is always active. This is the correction,
not a defect. If you need a flat bypass, you are requesting behavior the
reference architecture does not provide.

### 12.2 There is a click when toggling the EQ

**Check:** Are you resetting the chain on bypass? `FilterChain::setBypass()`
does not reset any state. If your host wrapper calls `reset()` on a bypass
change, remove that call. Resetting the high-pass state mid-signal produces a
transient.

### 12.3 Output is NaN after a few seconds

**Likely causes:**

1. An uninitialized buffer entering the chain. `HpfSection` and `LpfSection`
   do not contain NaN guards.
2. Hysteresis parameters far outside the default range. The bounded Euler step
   is not unconditionally stable for arbitrary parameter sets.

**Mitigation:** Sanitize at the input boundary in development builds, and
verify parameter ranges empirically.

### 12.4 CPU usage is higher than expected in silence

**Likely cause:** Denormal values in the biquad states.

**Mitigation:** Enable flush-to-zero and denormals-are-zero in your audio
thread. See §9.2.

### 12.5 Test 7 fails after changing `tapsPerPhase_`

**Expected.** The tolerance of `0.15` assumes 31 taps per phase. Re-derive the
tolerance or restore the constant.

### 12.6 `setOversamplingFactor()` produces a dropout

**Expected.** The method reallocates inside `Oversampler`. It is not
real-time safe. Call it from a suspended state, or from
`prepareToPlay()`.

### 12.7 Latency reported by the host does not match `latencySamples()`

`latencySamples()` reports only the algorithmic latency of this chain. It does
not include host buffering, the plugin's own lookahead, or any latency added
by your wrapper. Add it to your own reported latency.

### 12.8 Stereo image drifts or channels decorrelate

**Likely cause:** A single `FilterChain` instance shared across channels, or
two instances prepared with different parameters.

**Mitigation:** Construct one instance per channel and prepare both
identically.

---

## 13. Verification Status and Limitations

This section restates the limitations from the accompanying paper. They are
not caveats; they are the current scope of the work.

### 13.1 Not implemented

| Item | Status |
|---|---|
| Topology-complete passive LC wave digital filter network | Not implemented |
| Transformer-specific Jiles-Atherton parameter set | Not implemented |
| Full implicit Newton Jiles-Atherton solver | Not implemented |
| Verified alias rejection characterization | Not performed |
| Host and plugin validation | Not performed |

### 13.2 Not verified

| Claim | Status |
|---|---|
| HPF remains independent of EQ-band bypass | Implemented; architecturally tested; not hardware-confirmed |
| Cookbook biquads miss topology and loading interactions | Research hypothesis |
| WDF can represent the passive network topology | Design method; not demonstrated here |
| Jiles-Atherton can model hysteresis | Established modeling method |
| The reference transformer is reproduced by current parameters | **NOT CLAIMED** |
| 8× oversampling removes aliasing | **NOT CLAIMED** — measurement required |
| Complete model matches hardware | **NOT CLAIMED** |

### 13.3 What this library does establish

- The bypass architectural correction is implemented and enforced by a
  falsifiable test.
- The reference filter sections are prewarped and numerically stable.
- The hysteresis model is bounded and does not diverge under the default
  configuration.
- The oversampler has approximately unity passband gain.
- The chain is real-time safe in `process()` and reports constant latency
  across bypass states.

### 13.4 What this library does not establish

- That the output matches any physical device.
- That the aliasing introduced by the nonlinear stage has been eliminated.
- That the filter sections reproduce the reference network's inter-band
  loading or phase interaction.

Do not describe this library as a clone, an emulation, or a reproduction of
any specific hardware. Describe it as a corrected architecture and a
measurable baseline.

---

## Appendix A — Parameter Quick Reference

### A.1 `HpfParameters`

| Field | Type | Default | Clamp | Unit |
|---|---|---|---|---|
| `cornerHz` | `double` | 80.0 | `[1.0, 0.45·fs]` | Hz |
| `q` | `double` | 0.7071067811865476 | `≥ 0.05` | — |

### A.2 `LpfParameters`

| Field | Type | Default | Clamp | Unit |
|---|---|---|---|---|
| `cornerHz` | `double` | 12000.0 | `[10.0, 0.45·fs]` | Hz |
| `q` | `double` | 0.7071067811865476 | `≥ 0.05` | — |
| `gainDb` | `double` | 0.0 | none | dB |
| `shelf` | `bool` | false | — | — |

### A.3 `JilesAthertonParameters`

| Field | Type | Default | Clamp |
|---|---|---|---|
| `Ms` | `double` | 1.0 | `≥ 1e-12` |
| `a` | `double` | 0.25 | `≥ 1e-9` |
| `k` | `double` | 0.40 | `≥ 1e-9` |
| `c` | `double` | 0.15 | `[0, 1]` |
| `alpha` | `double` | 0.02 | `[-1, 1]` |
| `scaleH` | `double` | 1.0 | `≥ 1e-12` |
| `scaleM` | `double` | 1.0 | `≥ 1e-12` |

### A.4 `Oversampler` and `FilterChain` constants

| Constant | Value | Scope |
|---|---|---|
| `kDefaultOversampling` | 8 | `FilterChain` |
| `tapsPerPhase_` | 31 | `Oversampler` (private, compile-time) |
| Default `factor` | 8 | `Oversampler` constructor |

---

## Appendix B — File Manifest

```
IEM-500-filter-corrections/
├── README.md                      Project overview and build instructions
├── TECHNICAL_MANUAL.md            This document
├── CMakeLists.txt                 Build configuration
├── include/
│   ├── HpfSection.h               High-pass reference section
│   ├── LpfSection.h               Low-pass and high-shelf reference section
│   ├── JilesAtherton.h            Hysteresis model
│   ├── Oversampler.h              Polyphase-FIR oversampler
│   └── FilterChain.h              Top-level corrected chain
├── src/
│   ├── HpfSection.cpp
│   ├── LpfSection.cpp
│   ├── JilesAtherton.cpp
│   ├── Oversampler.cpp
│   └── FilterChain.cpp
├── test/
│   └── test_suite.cpp             Seven-case regression suite
└── docs/
    ├── paper-update.md            Editorial notes for the paper
    ├── signal-flow.md             Signal-flow diagram and state rules
    └── paper.md                   Full paper
```

---

## Appendix C — Glossary

| Term | Definition |
|---|---|
| **Aliasing** | The folding of energy above the Nyquist frequency back into the audible band, producing inharmonic distortion |
| **Anhysteretic magnetization** | The magnetization a material would reach in the absence of pinning, given by the Langevin function in the Jiles-Atherton model |
| **Biquad** | A second-order IIR filter section; the standard building block of cookbook digital equalizers |
| **Bilinear transform** | The mapping from the analog s-plane to the digital z-plane used to derive digital filters from analog prototypes |
| **Blackman window** | A three-term cosine window function used to taper FIR prototypes and reduce stopband ripple |
| **Decimation** | Downsampling with a preceding anti-aliasing low-pass filter |
| **Flush-to-zero (FTZ)** | A processor mode that treats denormal results as zero, avoiding denormal performance penalties |
| **Hysteresis** | The lag of magnetization behind the applied field; the source of the transformer stage's nonlinear harmonics |
| **Interpolation** | Upsampling with a following anti-imaging low-pass filter |
| **Jiles-Atherton model** | A physics-based magnetic hysteresis model parameterized by `Ms`, `a`, `k`, `c`, and `alpha` |
| **Langevin function** | `L(x) = coth(x) − 1/x`; the basis of the anhysteretic magnetization curve |
| **Latency** | The algorithmic delay introduced by a processing stage, expressed in samples |
| **Oversampling** | Running a processing stage at a rate above the host rate to move generated harmonics above the audible band before downsampling |
| **Polyphase decomposition** | A factorization of a FIR filter into `factor` sub-filters, allowing efficient interpolation and decimation |
| **Prewarping** | Adjusting the analog prototype frequency so that the bilinear transform places the digital corner at the requested frequency |
| **Real-time safe** | Guaranteed to perform no allocation, no locking, and no system calls |
| **Reference section** | A linear digital filter derived from an analog prototype |
| **Reconstruction** | A topology-complete model of a passive network |
| **Stopband rejection** | The attenuation a filter provides in its stopband, in dB |
| **Wave digital filter (WDF)** | A filter structure that models each circuit element as a port with incident and reflected waves, solving the network as an actual circuit |

---

*End of technical manual.*
