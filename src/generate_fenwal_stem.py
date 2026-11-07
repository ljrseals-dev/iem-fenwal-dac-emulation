# -*- coding: utf-8 -*-
"""
Generate a 45-second Fenwal-class analog stem with
bounded Jiles-Atherton hysteresis (IEM-500 style).

Python 3.8+  |  requires: numpy, scipy
"""

from __future__ import annotations

import argparse
import time

import numpy as np
from scipy import signal, fft
from scipy.io import wavfile

from jiles_atherton import JilesAtherton, JilesAthertonParameters


def soft_click(t, fs, center, strength=0.09):
    # type: (np.ndarray, int, float, float) -> np.ndarray
    n = len(t)
    c = np.zeros(n)
    idx = int(center * fs)
    for d, a in ((0.0, 1.0), (0.001, 0.30), (0.0022, 0.09)):
        i = idx + int(d * fs)
        if 0 <= i < n:
            lt = np.arange(min(int(0.014 * fs), n - i)) / float(fs)
            p = np.exp(-lt / 0.0007) - np.exp(-lt / 0.0024)
            p /= np.max(np.abs(p)) + 1e-12
            c[i : i + len(p)] += a * strength * p
    b, a = signal.butter(3, 1200, fs=fs)
    return signal.lfilter(b, a, c)


def generate_stem(
    duration=45.0,
    fs=48000,
    f0=1000.0,
    seed=1139,
    out_path="fenwal_ja_stem.wav",
):
    # type: (float, int, float, int, str) -> str
    rng = np.random.default_rng(seed)
    n = int(fs * duration)
    t = np.arange(n) / float(fs)

    # Carrier + slow thermal AM + micro-drift
    am = 1.0 + 0.007 * np.sin(2 * np.pi * 0.3 * t)
    drift = np.cumsum(rng.normal(0, 3.5e-8, n))
    x = np.sin(2 * np.pi * (f0 + drift) * t) * am
    x += soft_click(t, fs, 12.0) + soft_click(t, fs, 31.0)

    # Linear chain (HPF always in circuit)
    b, a = signal.butter(2, 16000, btype="low", fs=fs)
    x = signal.lfilter(b, a, x)
    b, a = signal.butter(2, 38, btype="high", fs=fs)
    x = signal.lfilter(b, a, x)

    # Bounded JA hysteresis (light contribution for magnetic memory)
    params = JilesAthertonParameters(
        Ms=0.8, a=0.20, k=0.38, c=0.28, alpha=0.008, scaleH=0.8, scaleM=0.35
    )
    ja = JilesAtherton(params)
    y_ja = ja.process_block(x)

    # Blend + explicit even-order residual (Fenwal even-harmonic preference)
    y = 0.94 * x + 0.06 * y_ja
    y = y + 0.055 * (y ** 2) + 0.0005 * (y ** 3)

    # Very low 1/f floor
    white = rng.standard_normal(n)
    f = fft.rfftfreq(n, 1.0 / fs)
    f[0] = 1.0
    shape = np.sqrt(1.0 + (40.0 / f))
    noise = fft.irfft(fft.rfft(white) * shape, n)
    noise /= np.std(noise) + 1e-12
    y += noise * (10 ** (-150 / 20.0))

    # Haas stereo (0.2 ms)
    delay = int(0.0002 * fs)
    left = y.copy()
    right = np.zeros(n)
    right[delay:] = y[:-delay]
    mid = 0.5 * (left + right)
    side = 0.5 * (left - right)
    left = mid + 0.05 * side
    right = mid - 0.05 * side

    # Envelope
    env = np.ones(n)
    env[: 2 * fs] = np.linspace(0, 1, 2 * fs) ** 1.35
    env[40 * fs :] = np.linspace(1, 0, n - 40 * fs) ** 1.65
    left *= env
    right *= env

    stereo = np.stack([left, right], axis=1)
    stereo /= np.max(np.abs(stereo)) * 1.122  # ~ -1 dBTP

    int_data = np.clip(stereo * (2 ** 23 - 1), -2 ** 23, 2 ** 23 - 1).astype(np.int32)
    wavfile.write(out_path, fs, int_data)
    return out_path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate Fenwal + JA stem")
    parser.add_argument("--duration", type=float, default=45.0)
    parser.add_argument("--fs", type=int, default=48000)
    parser.add_argument("--out", type=str, default="fenwal_ja_stem.wav")
    args = parser.parse_args()

    t0 = time.time()
    path = generate_stem(duration=args.duration, fs=args.fs, out_path=args.out)
    print("Wrote {}  ({:.1f}s)".format(path, time.time() - t0))
