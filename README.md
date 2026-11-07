# Fenwal-AJ Research Scaffold

**Research and reference implementation combining:**

1. **IEM-500 Filter Correction Suite** – corrected bypass semantics, linear filter sections, and bounded Jiles-Atherton magnetic hysteresis for inductor-based EQ emulation.
2. **Fenwal-class analog processing chain** – discrete-transistor wideband model with even-order harmonic preference, thermal drift, and ultra-low noise floor.
3. **DAC-style residual vector quantizer** – educational Python 3.8+ skeleton of the Descript Audio Codec (RVQ) architecture.

This repository is a **research scaffold**. It does not claim sonic equivalence to any commercial hardware or to the proprietary models used by ElevenLabs / Descript.

---

## Layout

```
fenwal-aj-research/
├── README.md
├── LICENSE
├── src/
│   ├── jiles_atherton.py      # Bounded JA hysteresis (Python port of IEM-500)
│   ├── dac_style_codec.py     # DAC / EnCodec-style RVQ scaffold
│   └── generate_fenwal_stem.py
├── audio/                     # Example 45 s stems (24-bit / 48 kHz)
├── docs/
│   └── aj_phase_noise.png
└── reference/
    └── IEM-500-Filter-Correction-Suite/   # Original C++ research scaffold
```

---

## Quick start

```bash
# Python 3.8+
pip install numpy scipy

# Generate a 45-second Fenwal + JA stem
cd src
python generate_fenwal_stem.py --out ../audio/fenwal_ja_stem.wav

# Smoke-test the DAC-style codec
python -c "
from dac_style_codec import DACStyleCodec
import numpy as np
c = DACStyleCodec(num_codebooks=4, codebook_size=256)
x = 0.5 * np.sin(2 * np.pi * 440 * np.linspace(0, 1, 48000))
recon, codes = c.encode_decode(x)
print(codes.shape, recon.shape)
"
```

---

## Key components

### Jiles-Atherton (magnetic hysteresis)

Port of the bounded, direction-aware Euler approximation from the IEM-500 suite.  
Intended for the coupling-transformer stage of an analog EQ.  
**Must be oversampled** in production to avoid aliasing.

### Fenwal-class stem generator

- 1 kHz carrier with 0.3 Hz thermal AM and micro-frequency drift  
- Soft mechanical relay events  
- Even-order harmonic dominance (quadratic residual)  
- Light JA contribution for magnetic memory  
- 0.2 ms Haas stereo, 1/f noise floor, tape-style fades  
- Target: 48 kHz / 24-bit, true-peak ≤ –1 dBTP

### DAC-style codec

Educational residual vector quantizer + simple convolutional encoder/decoder.  
Mirrors the three-stage architecture of Descript Audio Codec / EnCodec.  
No trained weights – replace the encoder/decoder with a real neural model for production use.

---

## Reference

The original IEM-500 C++ research scaffold (authors: Maurice McInnis, Adonna Seals, Claudia Anderson, Angela Seals) is included under `reference/`.  
See its `README.md`, `docs/paper.md` and `docs/Technical-Manual.md` for the architectural claims, limitations, and test suite.

---

## Status & limitations

- Research scaffold only.  
- JA parameters are generic; they have **not** been identified from measured B-H loops of a specific transformer.  
- The Python JA stage in the stem generator currently runs at base rate for speed; full oversampling is recommended for final rendering.  
- DAC-style codec is a structural skeleton – not a drop-in replacement for the official `descript-audio-codec` weights.  
- No claim of hardware null-test equivalence or commercial product parity.

---

## License

- Python research code in `src/`: MIT  
- IEM-500 reference material: see `reference/IEM-500-Filter-Correction-Suite/LICENSE`
