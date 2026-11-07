# -*- coding: utf-8 -*-
"""
Bounded Jiles-Atherton hysteresis model
=======================================
Python 3.8+ port of the IEM-500 Filter Correction Suite
(include/JilesAtherton.h + src/JilesAtherton.cpp).

Quasi-static magnetic hysteresis for coupling-transformer stages.
This is a DSP-oriented Euler approximation, NOT a full Newton solution
of the implicit Jiles-Atherton constitutive equation.

Must be driven at an oversampled rate for production use to avoid aliasing.
"""

from __future__ import annotations

import math
from typing import Optional

import numpy as np


class JilesAthertonParameters(object):
    """Standard Jiles-Atherton parameter set."""

    def __init__(
        self,
        Ms=1.0,          # Saturation magnetization
        a=0.25,          # Thermal energy / shape parameter
        k=0.40,          # Pinning site energy
        c=0.15,          # Reversibility coefficient [0, 1]
        alpha=0.02,      # Mean-field coupling
        scaleH=1.0,      # Input field scaling
        scaleM=1.0,      # Output magnetization scaling
    ):
        # type: (float, float, float, float, float, float, float) -> None
        self.Ms = max(1e-12, float(Ms))
        self.a = max(1e-9, float(a))
        self.k = max(1e-9, float(k))
        self.c = float(np.clip(c, 0.0, 1.0))
        self.alpha = float(np.clip(alpha, -1.0, 1.0))
        self.scaleH = max(1e-12, float(scaleH))
        self.scaleM = max(1e-12, float(scaleM))


class JilesAtherton(object):
    """
    Bounded, direction-aware Jiles-Atherton hysteresis.

    Call process() sample-by-sample (preferably at an oversampled rate).
    """

    def __init__(self, params=None):
        # type: (Optional[JilesAthertonParameters]) -> None
        self.p = params if params is not None else JilesAthertonParameters()
        self.M = 0.0          # current magnetization
        self.Hprev = 0.0      # previous applied field (normalized)

    def reset(self):
        # type: () -> None
        self.M = 0.0
        self.Hprev = 0.0

    def set_parameters(self, params):
        # type: (JilesAthertonParameters) -> None
        self.p = params

    @staticmethod
    def _langevin(x):
        # type: (float) -> float
        """Numerically stable Langevin function L(x) = coth(x) - 1/x."""
        ax = abs(x)
        if ax < 1.0e-4:
            x2 = x * x
            x3 = x2 * x
            x5 = x3 * x2
            return x / 3.0 - x3 / 45.0 + (2.0 * x5) / 945.0
        return 1.0 / math.tanh(x) - 1.0 / x

    def process(self, H):
        # type: (float) -> float
        """
        Advance the model by one sample.
        H : applied field (DSP domain)
        Returns scaled magnetization.
        """
        p = self.p
        h = H / p.scaleH
        he = h + p.alpha * self.M
        x = he / p.a

        Man = p.Ms * self._langevin(x)          # anhysteretic

        dH = h - self.Hprev
        if dH > 0.0:
            direction = 1.0
        elif dH < 0.0:
            direction = -1.0
        else:
            direction = 0.0

        irreversible = direction * (abs(Man - self.M) / p.k)
        reversible = p.c * (Man - self.M)

        step = min(abs(dH), 1.0)
        self.M += step * (irreversible + reversible)
        self.M = max(min(self.M, 2.0 * p.Ms), -2.0 * p.Ms)

        self.Hprev = h
        return self.M * p.scaleM

    def process_block(self, x):
        # type: (np.ndarray) -> np.ndarray
        """Process a 1-D array sample-by-sample (keeps internal state)."""
        out = np.empty_like(x, dtype=np.float64)
        for i in range(len(x)):
            out[i] = self.process(float(x[i]))
        return out
