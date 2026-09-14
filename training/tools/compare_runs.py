"""Compare Tiny-CRN experiment outputs.

Examples:
    python tools/compare_runs.py --runs runs/tiny_crn_48k_smoke runs/exp2
    python tools/compare_runs.py --runs runs/exp1 runs/exp2 --plot_spectrum
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np


def _init_matplotlib():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    return plt


def read_train_log(run_dir: Path) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    path = run_dir / "train_log.csv"
    if not path.exists():
        raise FileNotFoundError(f"train_log.csv not found: {path}")

    epochs: List[int] = []
    train_losses: List[float] = []
    val_losses: List[float] = []
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            epochs.append(int(row["epoch"]))
            train_losses.append(float(row["train_loss"]))
            val_losses.append(float(row["val_loss"]))
    return np.asarray(epochs), np.asarray(train_losses), np.asarray(val_losses)


def latest_band_csv(run_dir: Path) -> Path:
    data_dir = run_dir / "figures" / "data"
    files = sorted(data_dir.glob("epoch_*_band_data.csv"))
    if not files:
        raise FileNotFoundError(f"No epoch_*_band_data.csv found in: {data_dir}")
    return files[-1]


def read_band_csv(path: Path) -> Dict[str, np.ndarray]:
    out: Dict[str, List[float]] = {}
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            for k, v in row.items():
                out.setdefault(k, []).append(float(v))
    return {k: np.asarray(v, dtype=np.float32) for k, v in out.items()}


def plot_loss_compare(run_dirs: List[Path], out_path: Path) -> None:
    plt = _init_matplotlib()
    fig, ax = plt.subplots(figsize=(9, 5), dpi=150)
    for run_dir in run_dirs:
        epochs, train_loss, val_loss = read_train_log(run_dir)
        label = run_dir.name
        ax.plot(epochs, train_loss, linewidth=1.4, linestyle="--", label=f"{label} train")
        ax.plot(epochs, val_loss, linewidth=1.8, label=f"{label} val")

    ax.set_title("Experiment loss comparison")
    ax.set_xlabel("Epoch")
    ax.set_ylabel("Loss")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path)
    plt.close(fig)
    print("wrote", out_path)


def plot_spectrum_compare(run_dirs: List[Path], out_path: Path) -> None:
    plt = _init_matplotlib()
    fig, ax = plt.subplots(figsize=(9, 5), dpi=150)

    first = True
    for run_dir in run_dirs:
        data = read_band_csv(latest_band_csv(run_dir))
        band = data["band"]
        if first:
            ax.plot(band, data["noisy_db"], linewidth=1.2, linestyle=":", label="noisy reference")
            ax.plot(band, data["clean_db"], linewidth=1.6, label="clean reference")
            first = False
        ax.plot(band, data["enhanced_db"], linewidth=1.8, label=f"{run_dir.name} enhanced")

    ax.set_title("Latest frequency-band spectrum comparison")
    ax.set_xlabel("Frequency band index, low to high")
    ax.set_ylabel("Magnitude, dB")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path)
    plt.close(fig)
    print("wrote", out_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", nargs="+", required=True, help="Run dirs, for example: runs/exp1 runs/exp2")
    ap.add_argument("--out", default="runs/compare_loss.png", help="Output PNG for loss comparison")
    ap.add_argument("--plot_spectrum", action="store_true", help="Also compare latest enhanced band spectra")
    ap.add_argument("--spectrum_out", default="runs/compare_spectrum.png", help="Output PNG for spectrum comparison")
    args = ap.parse_args()

    run_dirs = [Path(x) for x in args.runs]
    plot_loss_compare(run_dirs, Path(args.out))
    if args.plot_spectrum:
        plot_spectrum_compare(run_dirs, Path(args.spectrum_out))


if __name__ == "__main__":
    main()
