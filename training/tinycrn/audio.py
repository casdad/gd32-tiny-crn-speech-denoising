import math
from pathlib import Path
from typing import Tuple

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly


def load_audio_mono(path: str, sample_rate: int, min_len: int = 0) -> np.ndarray:
    """Load audio as mono float32 in [-1, 1], resampled to sample_rate."""
    wav, sr = sf.read(str(path), always_2d=True)
    wav = wav.astype(np.float32).mean(axis=1)
    if sr != sample_rate:
        g = math.gcd(sr, sample_rate)
        wav = resample_poly(wav, sample_rate // g, sr // g).astype(np.float32)
    if min_len > 0 and len(wav) < min_len:
        reps = int(np.ceil(min_len / max(1, len(wav))))
        wav = np.tile(wav, reps)
    return wav.astype(np.float32)


def random_segment(wav: np.ndarray, length: int, rng: np.random.Generator) -> np.ndarray:
    if len(wav) <= 0:
        return np.zeros(length, dtype=np.float32)
    if len(wav) < length:
        reps = int(np.ceil(length / len(wav)))
        wav = np.tile(wav, reps)
    if len(wav) == length:
        return wav.astype(np.float32)
    start = int(rng.integers(0, len(wav) - length + 1))
    return wav[start:start + length].astype(np.float32)


def rms(x: np.ndarray, eps: float = 1e-8) -> float:
    return float(np.sqrt(np.mean(np.square(x), dtype=np.float64) + eps))


def mix_at_snr(clean: np.ndarray, noise: np.ndarray, snr_db: float, rng: np.random.Generator,
               speech_gain_db: float = 0.0) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Return noisy, clean_scaled, noise_scaled."""
    clean = clean.astype(np.float32)
    noise = noise.astype(np.float32)

    # Random speech gain first.
    clean_scaled = clean * (10.0 ** (speech_gain_db / 20.0))
    clean_r = rms(clean_scaled)
    noise_r = rms(noise)
    target_noise_r = clean_r / (10.0 ** (snr_db / 20.0))
    noise_scaled = noise * (target_noise_r / max(noise_r, 1e-8))
    noisy = clean_scaled + noise_scaled

    peak = max(float(np.max(np.abs(noisy))), float(np.max(np.abs(clean_scaled))), 1e-6)
    if peak > 0.95:
        scale = 0.95 / peak
        noisy *= scale
        clean_scaled *= scale
        noise_scaled *= scale
    return noisy.astype(np.float32), clean_scaled.astype(np.float32), noise_scaled.astype(np.float32)


def read_list(path: str):
    p = Path(path)
    if not p.exists():
        raise FileNotFoundError(f"File list not found: {path}")
    return [line.strip() for line in p.read_text(encoding="utf-8").splitlines() if line.strip()]
