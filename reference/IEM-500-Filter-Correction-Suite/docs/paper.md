# High-Pass and Low-Pass Modeling Failures in Inductor EQ Emulations:
# A Reference Implementation for Corrected Bypass Semantics,
# Filter Topology, and Alias Control

**Maurice McInnis, Adonna Seals, Claudia Anderson, Angela Seals**

---

## Abstract

Software emulations of inductor-based equalizer architectures commonly exhibit
two classes of defect that are independent of the accuracy of their frequency
response curves. The first is architectural: the high-pass section is placed
inside the equalizer bypass conditional, so disengaging the equalizer bands
also removes the high-pass filter from the signal path. In the reference
hardware architecture, the high-pass section is permanently in circuit. The
second is topological: the filter sections are realized as independently
parameterized second-order IIR (biquad) filters, which cannot reproduce the
loading, phase, and operating-level interactions of a shared passive LC
network.

This paper formalizes both defects, proposes a corrected signal-chain
architecture, and presents a reference C++ implementation. The implementation
places the high-pass section outside the bypass conditional, applies bilinear
frequency prewarping to the reference filter sections, models the coupling
transformer as a bounded Jiles-Atherton hysteresis stage, and drives that
nonlinear stage inside a polyphase-FIR oversampled domain. A seven-case
regression suite exercises the architectural invariant and the numerical
stability of the nonlinear path.

We are explicit about the limits of this work. The implementation is a
research scaffold. It does not contain a topology-complete wave digital filter
reconstruction of the passive network, it does not carry a validated
transformer parameter set, and it has not been null-tested against hardware.
Every quantitative accuracy claim is therefore marked UNVERIFIED. The
contribution is the corrected architecture and a testable, measurable
baseline, not a demonstrated clone.

---

## 1. Introduction

The equalizer architecture under study combines custom-wound inductors,
discrete Class A gain blocks, and transformer-balanced input and output. Its
characteristic low-end control derives in part from a fixed high-pass section
that operates alongside the low-frequency equalizer band rather than as a
separately switchable module.

Two properties of that architecture are frequently misrepresented in software:

1. **The high-pass section is always active.** It sits before the equalizer
   engage switch in the signal path. Disengaging the equalizer bands does not
   remove the high-pass filter.
2. **The filter sections are not independent.** The low, mid, and high bands
   share an inductor network. Adjusting one band alters the loading seen by
   the others. This coupling is a topological property, not a curve.

Most plugin implementations invert property (1) by construction and cannot
represent property (2) at all. The result is a plugin that measures
approximately correct on a static frequency sweep and behaves incorrectly
under bypass, under level change, and in the transition between the two.

This paper addresses both. Section 2 formalizes the bypass defect. Section 3
examines the topology question and states it as a testable hypothesis rather
than an absolute claim. Section 4 addresses the nonlinear magnetic stage and
the aliasing it introduces. Section 5 describes the reference implementation.
Section 6 reports the regression results. Section 7 states the limitations.
Section 8 concludes.

---

## 2. The Bypass Architectural Defect

### 2.1 Hardware behavior

In the reference architecture, the high-pass section is positioned before the
equalizer engage switch. The practical consequence is that the unit is never
truly out of the signal path. Even with the equalizer disengaged, the
high-pass section continues to shape the low-frequency response.

This is not an oversight in the hardware. It is a design decision: the
high-pass section is intended to work in conjunction with the low-frequency
band, attenuating content below the band's operating range so that the band
itself does not have to work against subsonic energy.

### 2.2 Software behavior

A conventional plugin implementation places all filter sections inside a
single bypass conditional:

```cpp
if (eqEnabled) {
    applyHighPass(buffer);
    applyLowShelf(buffer);
    applyMidPeak(buffer);
    applyHighShelf(buffer);
}
```

Under this structure, toggling the equalizer engage control removes the
high-pass section from the signal path. The plugin's low-frequency response
under bypass therefore differs from the hardware's by the entire high-pass
transfer function.

2.3 Corrected structure

The high-pass section must be instantiated outside the bypass conditional:

```cpp
applyHighPass(buffer);          // always active

if (eqEnabled) {
    applyLowShelf(buffer);
    applyMidPeak(buffer);
    applyHighShelf(buffer);
}
```

A second requirement follows from the first. If the high-pass section is
outside the bypass conditional, its state must survive a bypass transition.
Resetting the filter state on bypass would introduce a transient — an audible
click — at exactly the moment the user expects the change to be seamless. The
corrected implementation therefore preserves the high-pass state across bypass
transitions and does not touch it in setBypass.

2.4 Testable invariant

The correction yields a directly testable invariant:

With the equalizer bands disengaged, a low-frequency test tone must still be
attenuated relative to an in-band reference tone, by an amount consistent
with the high-pass transfer function.

The regression suite implements this as Test 4. It measures the root-mean-
square level of a 20 Hz tone against a 1 kHz tone with bypass engaged, and
asserts that the 20 Hz level is below 30 percent of the 1 kHz level.

---

3. The Filter Topology Question

3.1 A claim that must be stated carefully

It is tempting, and incorrect, to assert that biquad filters are inherently
incapable of reproducing an LC transfer function. A sufficiently identified
digital transfer function can reproduce a linear frequency response to
arbitrary precision under defined conditions. The bilinear transform with
frequency prewarping, applied to an accurate analog prototype, will produce a
digital filter whose magnitude response matches the prototype within a
specified tolerance across the audio band.

The defensible claim is narrower and more useful:

Independently parameterized cookbook biquads do not reproduce the reference
network's inter-band loading, phase interaction, component tolerance
behavior, or operating-level dependence.

3.2 Why the distinction matters

Three concrete consequences follow from the shared-network topology.

Inter-band loading. In the hardware, the low, mid, and high bands are not
isolated. Changing the low band's corner frequency alters the impedance
presented to the mid band. A cascade of independent biquads has no mechanism
to express this. The measured response of the cascade and the hardware will
therefore agree at one band setting and diverge as soon as a second band is
moved.

Phase interaction. Because the bands share reactive components, their
phase responses combine in a way that is not the sum of the individual band
phase responses. This is audible as a difference in transient behavior even
when the magnitude responses match.

Operating-level dependence. The inductors in the reference architecture
are nonlinear. Their effective inductance and series resistance vary with
signal level. A linear biquad has no level dependence at all. This is the
subject of Section 4.

3.3 Proposed method

The topology-correct approach is a wave digital filter (WDF) realization of
the passive LC network. In a WDF, each inductor, capacitor, and resistor is
represented as a port with incident and reflected waves, and the network is
solved as an actual circuit. This preserves the loading and phase interaction
by construction, and it provides a natural place to introduce a nonlinear
inductor model.

A WDF realization requires the network topology and the component values.
Neither is present in this repository. The current implementation therefore
uses digital reference sections as a linear baseline:

· HpfSection — a second-order high-pass derived from an analog prototype
  via the bilinear transform with frequency prewarping.
· LpfSection — a second-order low-pass on the same basis, with an optional
  RBJ high-shelf mode.

These sections are labeled reference implementations, not reconstructions.

3.4 On prewarping

Bilinear-transform frequency prewarping is applied via the substitution

```
w = 2 * fs * tan(pi * f0 / fs)
```

so that the digital corner frequency matches the requested analog corner. We
note explicitly that at low corner frequencies relative to the sample rate,
the numerical correction is small. Prewarping should be reported as a
transfer-function accuracy technique, not as the primary explanation for
audible discrepancies. A corollary: a plugin that omits prewarping will not
sound dramatically wrong, but it will not measure correctly either.

---

4. The Nonlinear Magnetic Stage and Aliasing

4.1 Source of the problem

The coupling transformers in the reference architecture are magnetic
components with hysteretic behavior. Under signal, they generate harmonics
that extend above the audio band. When such a model is evaluated at the host
sample rate, those harmonics fold back into the audible spectrum as
inharmonic distortion.

A plugin that models transformer saturation with a memoryless waveshaper
running at the host rate will alias. This is not a tuning issue; it is a
structural one.

4.2 Oversampling requirement

The nonlinear magnetic stage must run at a rate substantially above the host
rate. Common practice is 4x to 8x oversampling, with a steep decimation filter
to remove the harmonics above the original Nyquist frequency before
downsampling.

A critical and frequently violated constraint: the decimation filter must be a
proper anti-aliasing filter with high stopband rejection. Substituting a
gentle analog-style low-pass filter — the kind used inside an equalizer —
reintroduces exactly the aliasing the oversampler exists to remove.

4.3 Reference implementation

The repository provides a streaming polyphase-FIR oversampler. A single
Blackman-windowed sinc prototype is designed at a cutoff just below the
oversampled Nyquist frequency, normalized to unity DC gain, and decomposed
into factor polyphase branches for interpolation with the standard gain
compensation for zero stuffing. Decimation runs the prototype directly over
the high-rate history.

The per-sample entry point invokes the supplied nonlinear callable exactly
factor times per base-rate sample, which keeps the transformer model
entirely inside the oversampled domain without requiring block-based
plumbing:

```cpp
x = oversampler_.processSample(
        x,
        [this](double s) noexcept { return transformer_.process(s); });
```

4.4 Hysteresis model

The transformer is modeled with a bounded, direction-aware Jiles-Atherton-style
formulation. The anhysteretic magnetization is computed from the Langevin
function

```
L(x) = coth(x) - 1/x
```

evaluated with a truncated series expansion below |x| < 1e-4 to avoid
catastrophic cancellation near the origin. The irreversible and reversible
contributions are combined in a bounded Euler step, and the magnetization is
clamped to [-2*Ms, 2*Ms] to prevent divergence under large input
excursions.

We state plainly: this is not a Newton solution of the implicit Jiles-Atherton
constitutive equation. It is a DSP-oriented approximation intended to exercise
the oversampling path and to provide a working nonlinear stage. The default
parameter set is generic and has not been identified from measurements of any
specific transformer.

4.5 Measured rejection is the only valid claim

The presence of an oversampling module does not by itself establish that
aliasing has been eliminated. Alias rejection is finite and depends on the
prototype length, the oversampling factor, and the spectral content of the
nonlinearity. The correct claim is a measured rejection figure for a specified
configuration and a specified excitation, and that measurement has not been
performed here.

---

5. Reference Implementation

5.1 Module structure

Module Responsibility
HpfSection Always-active high-pass reference section
LpfSection Low-pass and high-shelf reference section
JilesAtherton Bounded hysteresis model for the coupling stage
Oversampler Streaming polyphase-FIR oversampler
FilterChain Corrected top-level signal chain

5.2 Signal order

```
in --> HPF (ALWAYS ACTIVE) --> [ bypass ? pass : EQ bands ] -->
       --> coupling transformer (ALWAYS IN CIRCUIT, OVERSAMPLED) --> out
```

The high-pass section executes unconditionally. The equalizer bands are gated
by the bypass flag. The coupling transformer executes unconditionally, inside
the oversampled domain.

5.3 Latency reporting

The linear sections are pure biquads and contribute no algorithmic latency.
Only the oversampled path introduces delay, and the chain reports that delay
in base-rate samples so that host delay compensation remains stable across
bypass transitions. Latency is constant regardless of bypass state.

5.4 Build and dependencies

C++17, CMake 3.16 or later, standard library only. No external DSP framework
is required.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

---

6. Regression Results

The suite comprises seven cases. All are internal-consistency and
architectural tests. None constitutes a hardware null test.

# Test Assertion
1 HPF attenuates DC Steady-state output for a unit DC input is below 1e-3
2 LPF passes DC Steady-state output for a unit DC input is within 1e-3 of unity
3 Chain stability Output is finite over 48000 samples; peak below 4.0
4 Bypass invariant With EQ bypassed, 20 Hz RMS is below 0.30 times 1 kHz RMS
5 Engaged low-frequency roll-off With EQ engaged, 20 Hz RMS is below 0.30
6 Hysteresis boundedness Magnetization finite and peak below 10.0 over 20000 samples
7 Oversampler passband In-band sine preserved with peak within 0.15 of unity

Test 4 is the central architectural assertion of the paper. It fails on a
conventional implementation, where bypassing the EQ removes the high-pass
section and the 20 Hz tone passes at approximately unity.

Test 7 uses a tolerance of 0.15, which assumes a Blackman-windowed prototype
with 31 taps per phase. Changing tapsPerPhase_ alters the group delay and
passband ripple, and the tolerance must be re-derived accordingly.

---

7. Limitations

We enumerate the limitations of this work without qualification.

1. No topology-complete WDF network. The passive LC network has not been
   reconstructed. The filter sections are linear digital references.
2. No validated transformer parameters. The hysteresis model uses a
   generic parameter set. It has not been identified from measured major and
   minor loops of any specific magnetic component.
3. No implicit Newton solver. The hysteresis update is a bounded Euler
   approximation, not a solution of the implicit constitutive equation.
4. No measured alias rejection. The oversampler is implemented but its
   stopband behavior has not been characterized for a specified excitation.
5. No hardware null test. The implementation has not been compared against
   a physical unit with aligned level, phase, and null testing.
6. No host validation. The implementation has not been exercised inside a
   plugin host under real-time constraints.
7. No claim of hardware equivalence. Nothing in this paper should be read
   as asserting that the implementation reproduces the sound of any physical
   device.

---

8. Conclusion

The gap between an inductor EQ plugin and its hardware counterpart is not
purely a matter of curve accuracy. Two structural defects account for a
significant portion of the discrepancy, and both are correctable at the
architecture level rather than at the tuning level.

The first defect is the placement of the high-pass section inside the bypass
conditional. The correction is to instantiate it outside, and to preserve its
state across bypass transitions. This yields a directly testable invariant,
which the regression suite enforces.

The second defect is the use of independently parameterized biquads to stand in
for a shared passive LC network. The honest formulation of this problem is a
hypothesis about inter-band loading, phase interaction, and operating-level
dependence, not an assertion that biquads are incapable of matching a
magnitude response. The proposed method is a wave digital filter realization,
which remains future work.

Underlying both is the nonlinear magnetic stage. Modeling it requires
oversampling with a proper decimation filter, and the only meaningful claim
about aliasing is a measured rejection figure rather than the presence of an
oversampler in the signal graph.

The implementation presented here is deliberately labeled a research
scaffold. Its value is that it makes the architectural correction explicit,
testable, and measurable, and that it states plainly what has not been
verified. A code module is not proof of hardware equivalence, and this paper
does not present it as such.

---

Appendix A — Neutral Terminology

Term Meaning
IEM-500 Neutral identifier for the reference inductor EQ architecture
kHpfCorner Fixed high-pass corner used by the reference architecture
kLpfCorner Selectable low-pass or filter corner
moduleBypass EQ band bypass control
couplingTransformer Generic input and output transformer stage
inductorNetwork Generic passive LC network under investigation

No manufacturer or model attribution is required by this work. All trademarks
referenced implicitly by the underlying subject matter remain the property of
their respective owners and are deliberately not reproduced here.

---

Appendix B — Claim Classification

Claim Classification Evidence required
HPF remains independent of EQ-band bypass IMPLEMENTED architecture State-matrix test; hardware measurement for physical confirmation
Cookbook biquads may miss topology and loading interactions RESEARCH HYPOTHESIS Circuit model and measured frequency and phase response
WDF can represent the passive network topology DESIGN METHOD Complete WDF network plus independent nodal validation
Jiles-Atherton can model hysteresis ESTABLISHED MODELING METHOD Parameter identification for the specific component
The reference transformer is reproduced by current parameters NOT CLAIMED Measured hysteresis loops and null testing
8x oversampling removes aliasing NOT CLAIMED Quantified alias-rejection measurement
Complete model matches hardware NOT CLAIMED Level and phase alignment plus null-test evidence

---

Appendix C — Rewrite Rule for Categorical Language

Replace categorical statements such as "cannot reproduce" with experimentally
testable statements such as "does not reproduce the measured
topology-dependent response under the tested conditions." This preserves the
technical argument while preventing the work from claiming evidence it does
not contain.

---

References

1. Wave digital filter port resistance definitions and adaptor formulation,
   standard DSP literature.
2. Bilinear transform with frequency prewarping, standard filter design
   literature.
3. RBJ audio EQ cookbook, high-shelf formulation.
4. Jiles, D. C. and Atherton, D. L., theory of ferromagnetic hysteresis.
5. Blackman window, standard window function literature.
6. Polyphase decomposition and FIR interpolation, standard multirate signal
   processing literature.
