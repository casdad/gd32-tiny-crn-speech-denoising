"""Plot waveform-level spectrum comparison for noisy / clean / enhanced wav files.

Example:
    python tools/plot_wav_spectrum.py --noisy examples/noisy.wav --clean examples/clean.wav --enhanced examples/enhanced.wav --out examples/wav_spectrum.png
"""
from __future__ import annotations

import argparse
from pathlib import Path
from typing import Dict

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly, stft


def _init_matplotlib():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    return plt


def load_mono(path: Path, target_sr: int | None = None):
    wav, sr = sf.read(str(path), always_2d=True)
    wav = wav.astype(np.float32).mean(axis=1)
    if target_sr is not None and sr != target_sr:
        import math

        g = math.gcd(sr, target_sr)
        wav = resample_poly(wav, target_sr // g, sr // g).astype(np.float32)
        sr = target_sr
    return wav, sr


def avg_spectrum_db(wav: np.ndarray, sr: int, n_fft: int = 2048, hop: int | None = None):
    if hop is None:
        hop = n_fft // 2
    freqs, _, z = stft(
        wav,
        fs=sr,
        window="hann",
        nperseg=n_fft,
        noverlap=n_fft - hop,
        nfft=n_fft,
        boundary=None,
        padded=False,
    )
    mag = np.mean(np.abs(z), axis=1)
    db = 20.0 * np.log10(np.maximum(mag, 1e-8))
    return freqs, db


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--noisy", required=True)
    ap.add_argument("--clean", default=None)
    ap.add_argument("--enhanced", required=True)
    ap.add_argument("--out", default="examples/wav_spectrum.png")
    ap.add_argument("--n_fft", type=int, default=2048)
    args = ap.parse_args()

    paths: Dict[str, Path] = {"noisy": Path(args.noisy), "enhanced": Path(args.enhanced)}
    if args.clean:
        paths["clean"] = Path(args.clean)

    noisy_wav, sr = load_mono(paths["noisy"])
    wavs = {"noisy": noisy_wav}
    for name, path in paths.items():
        if name == "noisy":
            continue
        wavs[name] = load_mono(path, target_sr=sr)[0]

    plt = _init_matplotlib()
    fig, ax = plt.subplots(figsize=(9, 5), dpi=150)
    for name, wav in wavs.items():
        freqs, db = avg_spectrum_db(wav, sr=sr, n_fft=args.n_fft)
        ax.plot(freqs, db, linewidth=1.5, label=name)

    ax.set_title("WAV average spectrum comparison")
    ax.set_xlabel("Frequency, Hz")
    ax.set_ylabel("Magnitude, dB")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path)
    plt.close(fig)
    print("wrote", out_path)


if __name__ == "__main__":
    main()
