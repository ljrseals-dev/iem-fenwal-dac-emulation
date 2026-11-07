# Paper Update — IEM-500 Filter Correction Study

## Editorial replacement text

The original paper should distinguish three separate propositions: signal-path architecture, linear filter topology, and magnetic/nonlinear behavior.

### 1. High-pass bypass behavior

The software model shall treat the high-pass section as an independent signal-path element. `moduleBypass` controls the equalizer bands only. Therefore, an EQ bypass test must not be implemented as a bypass of the complete filter chain.

**Status: IMPLEMENTED at the software-architecture level.**

This statement does not by itself prove the complete hardware topology; that conclusion requires a schematic, service documentation, or measurement of the reference unit.

### 2. LC topology versus biquad implementation

The paper should not state that biquad filters are inherently incapable of reproducing an LC transfer function. A sufficiently identified digital transfer function can reproduce a linear frequency response under defined conditions.

The stronger and defensible research question is whether independently parameterized cookbook biquads reproduce the reference network's loading, phase, component interactions, nonlinear elements, and behavior under operating-level changes.

**Status: RESEARCH HYPOTHESIS / NOT HARDWARE-VERIFIED.**

The current repository therefore uses digital reference sections as a baseline rather than labeling them a completed WDF reconstruction.

### 3. Magnetic hysteresis

A Jiles–Atherton family model is appropriate as an experimental framework for studying magnetic hysteresis, but the presence of the model does not establish that its parameters represent the reference transformer.

The repository's current implementation is a bounded DSP-oriented Jiles–Atherton-style approximation. It is not a published-parameter identification of the reference transformer and should not be described as such.

**Status: IMPLEMENTED EXPERIMENTAL MODEL / PARAMETER IDENTIFICATION UNVERIFIED.**

### 4. Oversampling and alias control

Nonlinear magnetic models can generate harmonics above the host Nyquist frequency. Oversampling is consequently a sensible mitigation strategy. The paper should report actual measured alias rejection rather than claiming a fixed rejection figure merely because an oversampling module exists.

**Status: BASELINE IMPLEMENTATION / MEASUREMENT REQUIRED.**

### 5. Prewarping

Bilinear-transform frequency prewarping is used where a digital reference section is derived from an analog prototype. At low corner frequencies relative to the sample rate, the numerical correction can be small; it should therefore be reported as a transfer-function accuracy technique, not as the primary explanation for large audible discrepancies.

**Status: IMPLEMENTED IN REFERENCE FILTER SECTIONS.**

## Recommended paper claim table

| Claim | Classification | Evidence required |
|---|---|---|
| HPF remains independent of EQ-band bypass | IMPLEMENTED architecture | State-matrix test; hardware measurement for physical confirmation |
| Cookbook biquads may miss topology/loading interactions | RESEARCH HYPOTHESIS | Circuit model and measured frequency/phase response |
| WDF can represent the passive network topology | DESIGN METHOD | Complete WDF network plus independent nodal validation |
| Jiles–Atherton can model hysteresis | ESTABLISHED MODELING METHOD | Parameter identification for the specific magnetic component |
| The reference transformer is reproduced by current J-A parameters | NOT CLAIMED | Measured hysteresis loops / null testing |
| 8x oversampling removes aliasing | NOT CLAIMED | Quantified alias-rejection measurement |
| Complete model matches hardware | NOT CLAIMED | Level/phase alignment and null-test evidence |

## Rewrite rule

Replace categorical language such as “cannot reproduce” with experimentally testable language such as “does not reproduce the measured topology-dependent response under the tested conditions.” This preserves the technical argument while preventing the paper from claiming evidence that the repository does not yet contain.
