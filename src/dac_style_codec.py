# -*- coding: utf-8 -*-
"""
DAC-style Residual Vector Quantization Codec (Python 3.8+)
==========================================================
Minimal educational implementation of the core architecture from
"High-Fidelity Audio Compression with Improved RVQGAN" (Descript Audio Codec).

Structure:
  Encoder  →  Residual Vector Quantizer (RVQ)  →  Decoder

This is a structural / research scaffold.  It does **not** contain
trained weights.  For production quality use the official
`descript-audio-codec` package or a fine-tuned neural tokenizer.

Compatible with Python 3.8 – 3.12.
Requires: numpy
"""

from __future__ import annotations

from typing import List, Optional, Tuple

import numpy as np


class ResidualVectorQuantizer(object):
    """Multi-codebook residual vector quantizer."""

    def __init__(
        self,
        num_codebooks=9,
        codebook_size=1024,
        codebook_dim=8,
        commitment_cost=0.25,
        seed=42,
    ):
        # type: (int, int, int, float, Optional[int]) -> None
        self.num_codebooks = num_codebooks
        self.codebook_size = codebook_size
        self.codebook_dim = codebook_dim
        self.commitment_cost = commitment_cost

        rng = np.random.RandomState(seed)
        self.codebooks = (
            rng.randn(num_codebooks, codebook_size, codebook_dim).astype(np.float32) * 0.02
        )

    def quantize(self, z):
        # type: (np.ndarray) -> Tuple[np.ndarray, np.ndarray, float]
        """
        Quantize latent z of shape [T, D] or [B, T, D].
        Returns (quantized, indices, loss).
        """
        original_shape = z.shape
        if z.ndim == 3:
            B, T, D = z.shape
            z_flat = z.reshape(B * T, D)
        else:
            B = 1
            T, D = z.shape
            z_flat = z

        residual = z_flat.copy()
        quantized = np.zeros_like(z_flat)
        all_indices = []
        commitment_loss = 0.0
        codebook_loss = 0.0

        for k in range(self.num_codebooks):
            r2 = np.sum(residual ** 2, axis=1, keepdims=True)
            c2 = np.sum(self.codebooks[k] ** 2, axis=1)
            rc = residual @ self.codebooks[k].T
            dist = r2 + c2 - 2.0 * rc
            indices = np.argmin(dist, axis=1)
            q = self.codebooks[k][indices]
            commitment_loss += np.mean((residual - q) ** 2)
            codebook_loss += np.mean((q - residual) ** 2)
            quantized += q
            residual -= q
            all_indices.append(indices)

        loss = codebook_loss + self.commitment_cost * commitment_loss
        indices_arr = np.stack(all_indices, axis=0)

        if original_shape[0] != z_flat.shape[0]:
            indices_arr = indices_arr.reshape(self.num_codebooks, B, T).transpose(1, 0, 2)
            quantized = quantized.reshape(B, T, D)
        else:
            quantized = quantized.reshape(T, D)

        return quantized.astype(np.float32), indices_arr, float(loss)

    def dequantize(self, indices):
        # type: (np.ndarray) -> np.ndarray
        if indices.ndim == 3:
            B, K, T = indices.shape
            out = np.zeros((B, T, self.codebook_dim), dtype=np.float32)
            for b in range(B):
                for k in range(K):
                    out[b] += self.codebooks[k][indices[b, k]]
            return out
        else:
            K, T = indices.shape
            out = np.zeros((T, self.codebook_dim), dtype=np.float32)
            for k in range(K):
                out += self.codebooks[k][indices[k]]
            return out


def _conv1d(x, weight, bias=None, stride=1, padding=0):
    # type: (np.ndarray, np.ndarray, Optional[np.ndarray], int, int) -> np.ndarray
    T, C_in = x.shape
    K, C_out, C_in_w = weight.shape
    assert C_in == C_in_w
    if padding:
        x = np.pad(x, ((padding, padding), (0, 0)), mode="reflect")
    out_len = (x.shape[0] - K) // stride + 1
    out = np.zeros((out_len, C_out), dtype=np.float32)
    for i in range(out_len):
        start = i * stride
        window = x[start : start + K]
        out[i] = np.tensordot(window, weight, axes=((0, 1), (0, 2)))
    if bias is not None:
        out += bias
    return out


class SimpleEncoder(object):
    def __init__(self, in_channels=1, latent_dim=8, seed=0):
        # type: (int, int, int) -> None
        rng = np.random.RandomState(seed)
        self.w1 = rng.randn(7, 32, in_channels).astype(np.float32) * 0.02
        self.b1 = np.zeros(32, dtype=np.float32)
        self.w2 = rng.randn(7, 64, 32).astype(np.float32) * 0.02
        self.b2 = np.zeros(64, dtype=np.float32)
        self.w3 = rng.randn(7, latent_dim, 64).astype(np.float32) * 0.02
        self.b3 = np.zeros(latent_dim, dtype=np.float32)

    def __call__(self, x):
        # type: (np.ndarray) -> np.ndarray
        if x.ndim == 1:
            x = x[:, None]
        h = np.tanh(_conv1d(x, self.w1, self.b1, stride=2, padding=3))
        h = np.tanh(_conv1d(h, self.w2, self.b2, stride=2, padding=3))
        z = _conv1d(h, self.w3, self.b3, stride=2, padding=3)
        return z


class SimpleDecoder(object):
    def __init__(self, latent_dim=8, out_channels=1, seed=1):
        # type: (int, int, int) -> None
        rng = np.random.RandomState(seed)
        self.w1 = rng.randn(7, 64, latent_dim).astype(np.float32) * 0.02
        self.b1 = np.zeros(64, dtype=np.float32)
        self.w2 = rng.randn(7, 32, 64).astype(np.float32) * 0.02
        self.b2 = np.zeros(32, dtype=np.float32)
        self.w3 = rng.randn(7, out_channels, 32).astype(np.float32) * 0.02
        self.b3 = np.zeros(out_channels, dtype=np.float32)

    def _upsample(self, x, factor=2):
        # type: (np.ndarray, int) -> np.ndarray
        T, C = x.shape
        out = np.zeros((T * factor, C), dtype=np.float32)
        out[::factor] = x
        for i in range(1, factor):
            out[i::factor] = (1.0 - i / float(factor)) * x + (i / float(factor)) * np.roll(x, -1, axis=0)
        return out

    def __call__(self, z):
        # type: (np.ndarray) -> np.ndarray
        h = self._upsample(z, 2)
        h = np.tanh(_conv1d(h, self.w1, self.b1, stride=1, padding=3))
        h = self._upsample(h, 2)
        h = np.tanh(_conv1d(h, self.w2, self.b2, stride=1, padding=3))
        h = self._upsample(h, 2)
        y = _conv1d(h, self.w3, self.b3, stride=1, padding=3)
        return y[:, 0] if y.shape[1] == 1 else y


class DACStyleCodec(object):
    """End-to-end DAC-style codec scaffold."""

    def __init__(
        self,
        sample_rate=48000,
        num_codebooks=9,
        codebook_size=1024,
        latent_dim=8,
        seed=42,
    ):
        # type: (int, int, int, int, int) -> None
        self.sample_rate = sample_rate
        self.encoder = SimpleEncoder(in_channels=1, latent_dim=latent_dim, seed=seed)
        self.rvq = ResidualVectorQuantizer(
            num_codebooks=num_codebooks,
            codebook_size=codebook_size,
            codebook_dim=latent_dim,
            seed=seed + 1,
        )
        self.decoder = SimpleDecoder(latent_dim=latent_dim, out_channels=1, seed=seed + 2)

    def encode(self, audio):
        # type: (np.ndarray) -> np.ndarray
        audio = np.asarray(audio, dtype=np.float32)
        if audio.ndim > 1:
            audio = audio.mean(axis=-1)
        z = self.encoder(audio)
        _, indices, _ = self.rvq.quantize(z)
        return indices

    def decode(self, indices):
        # type: (np.ndarray) -> np.ndarray
        z_hat = self.rvq.dequantize(indices)
        audio = self.decoder(z_hat)
        return audio.astype(np.float32)

    def encode_decode(self, audio):
        # type: (np.ndarray) -> Tuple[np.ndarray, np.ndarray]
        codes = self.encode(audio)
        recon = self.decode(codes)
        n = min(len(audio), len(recon))
        return recon[:n], codes
