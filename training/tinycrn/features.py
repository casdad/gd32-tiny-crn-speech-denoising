from functools import lru_cache
from typing import Tuple

import numpy as np
import torch


@lru_cache(maxsize=32)
def _band_matrix_np(sample_rate: int, n_fft: int, n_bands: int) -> np.ndarray:
    """Create a simple triangular filterbank over linear frequency bins.

    This avoids a librosa dependency. For MCU implementation, this matrix can be
    exported as constants or replaced by fixed Bark/Mel bands.
    """
    n_bins = n_fft // 2 + 1
    # Use a slightly denser low-frequency allocation by sqrt spacing.
    edges = np.linspace(0.0, 1.0, n_bands + 2) ** 1.4
    edges = np.round(edges * (n_bins - 1)).astype(np.int64)
    edges[0] = 0
    edges[-1] = n_bins - 1
    for i in range(1, len(edges)):
        if edges[i] <= edges[i - 1]:
            edges[i] = min(n_bins - 1, edges[i - 1] + 1)
    fb = np.zeros((n_bands, n_bins), dtype=np.float32)
    for b in range(n_bands):
        left, center, right = edges[b], edges[b + 1], edges[b + 2]
        if center == left:
            center += 1
        if right == center:
            right += 1
        center = min(center, n_bins - 1)
        right = min(right, n_bins - 1)
        if center > left:
            fb[b, left:center + 1] = np.linspace(0.0, 1.0, center - left + 1)
        if right > center:
            fb[b, center:right + 1] = np.linspace(1.0, 0.0, right - center + 1)
        s = fb[b].sum()
        if s > 0:
            fb[b] /= s
    return fb


def band_matrix(sample_rate: int, n_fft: int, n_bands: int, device=None, dtype=torch.float32) -> torch.Tensor:
    fb = _band_matrix_np(sample_rate, n_fft, n_bands)
    return torch.tensor(fb, device=device, dtype=dtype)


def stft_mag(wav: torch.Tensor, n_fft: int, win_length: int, hop_length: int) -> torch.Tensor:
    """wav: [B, N] -> magnitude [B, frames, bins]"""
    if wav.dim() == 1:
        wav = wav.unsqueeze(0)
    window = torch.hann_window(win_length, device=wav.device, dtype=wav.dtype)
    spec = torch.stft(
        wav,
        n_fft=n_fft,
        hop_length=hop_length,
        win_length=win_length,
        window=window,
        center=True,
        return_complex=True,
    )
    mag = spec.abs().transpose(1, 2).contiguous()
    return mag


def band_magnitude(mag: torch.Tensor, sample_rate: int, n_fft: int, n_bands: int) -> torch.Tensor:
    """mag: [B, frames, bins] -> [B, frames, n_bands]"""
    fb = band_matrix(sample_rate, n_fft, n_bands, device=mag.device, dtype=mag.dtype)
    return torch.matmul(mag, fb.t())


def feature_from_band(band_mag: torch.Tensor) -> torch.Tensor:
    """Log-compressed features."""
    return torch.log1p(10.0 * band_mag)


def ideal_band_mask(clean_band: torch.Tensor, noisy_band: torch.Tensor) -> torch.Tensor:
    mask = clean_band / (noisy_band + 1e-8)
    return torch.clamp(mask, 0.0, 1.0)


def wav_to_training_tensors(noisy: torch.Tensor, clean: torch.Tensor, *, sample_rate: int,
                            n_fft: int, win_length: int, hop_length: int, n_bands: int):
    noisy_mag = stft_mag(noisy, n_fft, win_length, hop_length)
    clean_mag = stft_mag(clean, n_fft, win_length, hop_length)
    noisy_band = band_magnitude(noisy_mag, sample_rate, n_fft, n_bands)
    clean_band = band_magnitude(clean_mag, sample_rate, n_fft, n_bands)
    feat = feature_from_band(noisy_band)
    mask = ideal_band_mask(clean_band, noisy_band)
    return feat, mask, noisy_band, clean_band


def band_mask_to_bin_mask(mask_band: np.ndarray, sample_rate: int, n_fft: int, n_bands: int) -> np.ndarray:
    """Approximate inverse mapping: band mask -> FFT-bin mask."""
    fb = _band_matrix_np(sample_rate, n_fft, n_bands)  # [bands,bins]
    weights = fb.T  # [bins,bands]
    denom = np.maximum(weights.sum(axis=1, keepdims=True), 1e-8)
    weights = weights / denom
    return weights @ mask_band.astype(np.float32)
