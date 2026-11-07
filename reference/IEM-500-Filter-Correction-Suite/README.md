# IEM-500 Filter Correction Suite

**Reference implementation of corrected bypass semantics, linear filter sections, and oversampled nonlinear magnetic modeling for inductor-based equalizer research.**

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![CMake](https://img.shields.io/badge/CMake-3.16%2B-green.svg)](https://cmake.org/)
[![License: Research](https://img.shields.io/badge/License-Research%20%2F%20Educational-lightgrey.svg)](LICENSE)
[![Status: Research Scaffold](https://img.shields.io/badge/Status-Research%20Scaffold-orange.svg)](#status-and-scope)

---

## Authors

**Maurice McInnis · Adonna Seals · Claudia Anderson · Angela Seals**

This repository is the companion reference implementation to the research paper  
*High-Pass and Low-Pass Modeling Failures in Inductor EQ Emulations: A Reference Implementation for Corrected Bypass Semantics, Filter Topology, and Alias Control*.

All scientific claims, experimental design, and architectural analysis originate with the authors named above. This codebase implements the software architecture described in that work and does not introduce new primary research claims beyond the executable tests included herein.

---

## In Plain English (Layman’s Summary)

Imagine an old-school hardware equalizer that uses real inductors and transformers. Even when you “bypass” the EQ bands, a high-pass filter stays in the circuit and the transformers keep doing their magnetic thing. Many software plugins get this wrong in two important ways:

1. **They turn the high-pass filter off when you bypass the EQ.**  
   On the real hardware the high-pass never leaves the signal path. Turning it off in software changes the low-end behaviour and can introduce clicks or state jumps.

2. **They model the magnetic transformers too simply (or at the wrong sample rate).**  
   Real transformers produce harmonics. If those harmonics are generated at the normal audio sample rate they fold back into the audible range as unwanted “aliasing” distortion. The fix is proper oversampling with a good anti-aliasing filter.

This repository is a clean, minimal C++ research scaffold that demonstrates the *correct* software architecture for those two problems. It is deliberately labelled a research scaffold — not a finished commercial plugin and not a claim that the code sounds identical to any particular hardware unit. The tests prove the architectural invariants; they do not prove hardware equivalence.

---

## Status and Scope

**Research scaffold (v0.1.0).**  

The project cleanly separates:

- What is **implemented and tested** today,  
- What is a **research hypothesis** still requiring measurement, and  
- What is **explicitly not claimed**.

A code module is never treated as proof of hardware equivalence.

### Implemented and regression-tested

| Component | Description | Evidence |
|-----------|-------------|----------|
| Independent HPF | High-pass section lives outside the bypass conditional; state survives bypass toggles | Test 4 (bypass invariant) passes |
| Bilinear HPF / LPF sections | Second-order digital references with frequency prewarping; optional high-shelf | Unit tests + chain stability |
| Bounded Jiles–Atherton-style hysteresis | Direction-aware magnetic model with numerical safeguards | Boundedness test passes |
| Streaming polyphase-FIR oversampler | Single-sample API, constant reported latency | Passband test + latency reporting |
| Corrected FilterChain | HPF always active → optional EQ bands → oversampled transformer | Full chain tests |

### Explicitly not claimed / not yet measured

- Topology-complete wave-digital-filter (WDF) reconstruction of the passive LC network  
- Transformer parameters identified from measured major/minor B-H loops of a named component  
- Implicit Newton solution of the full Jiles–Atherton constitutive equation  
- Quantified alias-rejection figures (dB) for a stated excitation  
- Plugin-host real-time validation under load  
- Level- and phase-aligned hardware null test  
- Any claim of sonic equivalence to a physical unit  

These distinctions are intentional and documented in the paper’s claim table (`docs/paper-update.md`) and Section 7 Limitations.

---

## Signal-Flow Invariant (the architectural core)

```text
input
  │
  ▼
┌─────────────────────────────┐
│  HPF                        │  ← ALWAYS in circuit, state preserved
│  (independent of bypass)    │
└─────────────┬───────────────┘
              │
              ▼
       ┌──────────────┐
       │ moduleBypass │
       └──────┬───────┘
              │
        ┌─────┴─────┐
        │           │
      true        false
        │           │
        │     ┌─────▼──────┐
        │     │ EQ / LPF   │
        │     │ bands      │
        │     └─────┬──────┘
        │           │
        └─────┬─────┘
              │
              ▼
┌─────────────────────────────┐
│ Coupling transformer        │  ← ALWAYS in circuit
│ (oversampled nonlinear)     │
└─────────────┬───────────────┘
              │
              ▼
            output
```

**Critical software rule:** Toggling `moduleBypass` must neither remove the high-pass filter from the signal path nor reset its internal state. Latency reported by the chain remains constant across bypass transitions.

---

## Repository Layout

```text
IEM-500-Filter-Correction-Suite/
├── README.md                 ← this file
├── CMakeLists.txt
├── LICENSE
├── .gitignore
├── CITATION.cff
├── include/
│   ├── HpfSection.h
│   ├── LpfSection.h
│   ├── JilesAtherton.h
│   ├── Oversampler.h
│   └── FilterChain.h
├── src/
│   ├── HpfSection.cpp
│   ├── LpfSection.cpp
│   ├── JilesAtherton.cpp
│   ├── Oversampler.cpp
│   └── FilterChain.cpp
├── test/
│   └── test_suite.cpp        ← seven architectural / consistency tests
├── docs/
│   ├── paper.md              ← full research paper
│   ├── paper-update.md       ← claim classification & rewrite rules
│   ├── signal-flow.md
│   └── Technical-Manual.md
└── Build/
    └── IEM-500-filter-corrections-complete.zip   (archived snapshot)
```

---

## Build & Test

Requirements: CMake ≥ 3.16, a C++17 compiler. No external DSP frameworks.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The regression suite currently contains seven tests. All are internal-consistency and architectural checks. None is a hardware null test.

| # | Assertion | Purpose |
|---|-----------|---------|
| 1 | HPF attenuates DC | Steady-state output for unit DC < 1e-3 |
| 2 | LPF passes DC | Steady-state output for unit DC within 1e-3 of unity |
| 3 | Chain stability | Output finite over 48 000 samples; peak < 4.0 |
| 4 | **Bypass invariant** | With EQ bypassed, 20 Hz RMS < 0.30 × 1 kHz RMS |
| 5 | Engaged low-frequency roll-off | With EQ engaged, 20 Hz RMS < 0.30 |
| 6 | Hysteresis boundedness | Magnetization finite and peak < 10.0 |
| 7 | Oversampler passband | In-band sine preserved (peak within 0.15 of unity) |

**Test 4 is the central architectural assertion of the paper.** A conventional design that places the HPF inside the bypass conditional fails this test.

---

## Roadmap (mapped to paper limitations)

Each item maps to a numbered limitation in Section 7 of `docs/paper.md`. An item is considered **done** only when its exit criterion has been *executed and recorded*. Code that merely exists does not close an item. Until then the claim table in `docs/paper-update.md` remains at NOT CLAIMED where appropriate.

| # | Work item | Paper limitation | Exit criterion (measured, not asserted) | Status |
|---|-----------|------------------|-----------------------------------------|--------|
| 0 | Corrected bypass semantics: HPF outside `moduleBypass`, state preserved, constant latency | — | Test 4 passes; state-matrix test shows HPF state unchanged across bypass toggle | **Implemented** (software architecture) |
| 1 | Alias-rejection measurement for the oversampler | 4 | Swept high-level sine through nonlinear stage; report rejection in dB for stated factor, tap count and excitation | Not started |
| 2 | Hardened oversampler (stopband specification, block API) | 4 | Stopband attenuation from item 1 meets a pre-stated target | Not started |
| 3 | Implicit Newton solver for the hysteresis model | 3 | Converges on major & minor loops; agrees with reference solver; stays bounded | Not started |
| 4 | Transformer parameter identification | 2 | Parameters fitted to measured major/minor B-H loops of a *named* component; residual reported | Needs measured data |
| 5 | Topology-complete passive LC network (wave digital filter) | 1 | WDF response agrees with independent nodal analysis of same netlist; inter-band loading demonstrated | Needs schematic / component values |
| 6 | Biquad versus network comparison | 1 | Magnitude, phase and level-dependence deltas under stated conditions | Blocked by 5 |
| 7 | Plugin host validation | 6 | Runs under real-time load; no allocation in audio path; latency constant across bypass | Not started |
| 8 | Hardware null test | 5 | Level- and phase-aligned null against a physical unit; residual reported | Needs hardware measurements |

**Not on the roadmap:** any claim of hardware equivalence (paper Limitation 7). That claim can only follow from the successful completion of items 4, 5 and 8 together.

**Suggested order of attack:** items 1 and 2 first (no external data required), then 3, then 7. Items 4, 5 and 8 wait on measured data or a schematic and should not be started from guesses.

---

## Validation Policy

The paper and this repository report only measurements that have actually been executed.

Recommended future validation experiments:

1. Compare the digital HPF against an independently solved analog prototype.  
2. Measure the bypass-state transfer function with the HPF held constant.  
3. Identify transformer hysteresis parameters from measured major and minor loops.  
4. Measure alias rejection using a high-level swept nonlinear excitation.  
5. Compare the complete chain against hardware using aligned level, phase, and null tests.

Until those experiments exist, treat every accuracy claim as **UNVERIFIED**.

---

## Limitations (paper Section 7, restated)

We enumerate the limitations of this work without qualification:

1. **No topology-complete WDF network.** The passive LC network has not been reconstructed. The filter sections are linear digital references.  
2. **No validated transformer parameters.** The hysteresis model uses a generic parameter set that has not been identified from measured loops of any specific magnetic component.  
3. **No implicit Newton solver.** The hysteresis update is a bounded Euler approximation, not a solution of the implicit constitutive equation.  
4. **No measured alias rejection.** The oversampler is implemented but its stopband behaviour has not been characterised for a specified excitation.  
5. **No hardware null test.** The implementation has not been compared against a physical unit with aligned level, phase, and null testing.  
6. **No host validation.** The implementation has not been exercised inside a plugin host under real-time constraints.  
7. **No claim of hardware equivalence.** Nothing in this work should be read as asserting that the implementation reproduces the sound of any physical device.

---

## Neutral Terminology

| Term | Meaning |
|------|---------|
| `IEM-500` | Neutral identifier for the reference inductor EQ architecture studied in the paper |
| `kHpfCorner` | Fixed high-pass corner used by the reference architecture |
| `kLpfCorner` | Selectable low-pass / filter corner |
| `moduleBypass` | EQ-band bypass control |
| `couplingTransformer` | Generic input / output transformer stage |
| `inductorNetwork` | Generic passive LC network under investigation |

No manufacturer or model attribution is required by this work. All trademarks remain the property of their respective owners and are deliberately not reproduced here.

---

## Citation

If you use this repository or the accompanying paper in academic work, please credit the authors:

```
McInnis, M., Seals, A., Anderson, C., & Seals, A. (2026).
High-Pass and Low-Pass Modeling Failures in Inductor EQ Emulations:
A Reference Implementation for Corrected Bypass Semantics, Filter Topology,
and Alias Control. Research scaffold and companion code.
```

A formal citation file (`CITATION.cff`) is included for automated tooling.

---

## License & Notice

Research and educational implementation. See `LICENSE` for the full text.

No affiliation, endorsement, or sponsorship by any hardware manufacturer is claimed. All trademarks referenced implicitly by the underlying subject matter remain the property of their respective owners and are deliberately not reproduced here.

---

## Contributing

This is a research scaffold. Contributions that advance the measured roadmap items (especially alias-rejection measurements, parameter identification from real B-H data, or a validated WDF network) are welcome. Please open an issue first to discuss scope so that new code does not outrun the evidence standards of the paper.
