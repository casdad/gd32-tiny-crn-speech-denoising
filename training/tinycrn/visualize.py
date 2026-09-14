"""Training-time visualization helpers for Tiny-CRN.

This file is intentionally standalone: it does not change the model, loss,
optimizer, checkpoint format, ONNX export, or GD32 NPZ generation. It only
saves figures/CSV/TXT artifacts under the current run directory.
"""
from __future__ import annotations

import csv
import math
import warnings
from pathlib import Path
from typing import Dict, List, Optional, Tuple

import numpy as np
import torch

from tinycrn.features import band_matrix, band_mask_to_bin_mask, feature_from_band


class TrainingVisualizer:
    """Save training curves, validation figures, waveform comparison and metrics.

    Default output directory:
        runs/<run_name>/figures/

    Files generated:
        loss_curve.png
        loss_curve_epoch_0001.png
        epoch_0001_mask.png
        epoch_0001_band_spectrum.png
        epoch_0001_band_error.png
        epoch_0001_waveform.png
        epoch_0001_wav_spectrum.png
        metrics_epoch_0001.txt
        metrics_latest.txt
        metrics_summary.csv
        data/epoch_0001_band_data.csv
        data/epoch_0001_waveform_data.csv

    Optional YAML block:
        visualize:
          enabled: true
          every_n_epochs: 1
          sample_index: 0
          fig_dir: figures
          save_waveform: true
          save_wav_spectrum: true
          save_wav_audio: false
          metrics_txt: true
    """

    def __init__(self, cfg: dict, out_dir: Path, device: Optional[torch.device] = None):
        self.cfg = cfg
        self.out_dir = Path(out_dir)
        self.device = device

        vis_cfg = cfg.get("visualize", {}) or {}
        self.enabled = bool(vis_cfg.get("enabled", True))
        self.every_n_epochs = max(1, int(vis_cfg.get("every_n_epochs", 1)))
        self.sample_index = max(0, int(vis_cfg.get("sample_index", 0)))
        self.save_waveform = bool(vis_cfg.get("save_waveform", True))
        self.save_wav_spectrum = bool(vis_cfg.get("save_wav_spectrum", True))
        self.save_wav_audio = bool(vis_cfg.get("save_wav_audio", False))
        self.metrics_txt = bool(vis_cfg.get("metrics_txt", True))

        fig_dir = Path(str(vis_cfg.get("fig_dir", "figures")))
        self.fig_dir = fig_dir if fig_dir.is_absolute() else self.out_dir / fig_dir
        self.data_dir = self.fig_dir / "data"
        self.audio_dir = self.fig_dir / "audio"

        self.train_losses: List[float] = []
        self.val_losses: List[float] = []

        self._plt = None
        if self.enabled:
            self.fig_dir.mkdir(parents=True, exist_ok=True)
            self.data_dir.mkdir(parents=True, exist_ok=True)
            if self.save_wav_audio:
                self.audio_dir.mkdir(parents=True, exist_ok=True)
            try:
                import matplotlib

                matplotlib.use("Agg")
                import matplotlib.pyplot as plt

                self._plt = plt
            except Exception as exc:  # pragma: no cover
                self.enabled = False
                warnings.warn(
                    "Training visualization disabled because matplotlib could not be imported. "
                    "Install it with: pip install matplotlib. Original training continues. "
                    f"Import error: {exc}"
                )

    def update(self, epoch: int, train_loss: float, val_loss: float, model: torch.nn.Module, val_dataset) -> None:
        """Call once after each epoch. Visualization errors are ignored."""
        if not self.enabled or self._plt is None:
            return

        self.train_losses.append(float(train_loss))
        self.val_losses.append(float(val_loss))

        try:
            self._save_loss_curve(epoch)
            if epoch % self.every_n_epochs == 0:
                self._save_one_sample_comparison(epoch, model, val_dataset)
        except Exception as exc:  # pragma: no cover - keep training safe
            warnings.warn(f"Training visualization failed at epoch {epoch}, ignored. Error: {exc}")

    def _save_loss_curve(self, epoch: int) -> None:
        epochs = np.arange(1, len(self.train_losses) + 1)
        plt = self._plt

        fig, ax = plt.subplots(figsize=(8, 4.5), dpi=140)
        ax.plot(epochs, self.train_losses, marker="o", linewidth=1.8, label="train_loss")
        ax.plot(epochs, self.val_losses, marker="o", linewidth=1.8, label="val_loss")
        ax.set_title("Training loss curve")
        ax.set_xlabel("Epoch")
        ax.set_ylabel("Loss")
        ax.grid(True, alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(self.fig_dir / "loss_curve.png")
        fig.savefig(self.fig_dir / f"loss_curve_epoch_{epoch:04d}.png")
        plt.close(fig)

    @torch.no_grad()
    def _save_one_sample_comparison(self, epoch: int, model: torch.nn.Module, val_dataset) -> None:
        if len(val_dataset) <= 0:
            return

        idx = self.sample_index % len(val_dataset)
        sample = val_dataset[idx]
        device = self.device or next(model.parameters()).device

        x = sample["x"].unsqueeze(0).to(device)  # [1,T,F,1]
        target_mask = sample["mask"].detach().cpu().numpy().astype(np.float32)
        clean_band = sample["clean_band"].detach().cpu().numpy().astype(np.float32)
        noisy_band = sample["noisy_band"].detach().cpu().numpy().astype(np.float32)

        was_training = model.training
        model.eval()
        pred_mask = model(x).squeeze(0).detach().cpu().numpy().astype(np.float32)
        if was_training:
            model.train(True)

        pred_mask = np.clip(pred_mask, 0.0, 1.0)
        enhanced_band = pred_mask * noisy_band
        band = np.arange(len(target_mask), dtype=np.int32)

        eps = 1e-8
        clean_db = 20.0 * np.log10(np.maximum(clean_band, eps))
        noisy_db = 20.0 * np.log10(np.maximum(noisy_band, eps))
        enhanced_db = 20.0 * np.log10(np.maximum(enhanced_band, eps))
        abs_error_db = np.abs(enhanced_db - clean_db)

        self._save_band_csv(
            epoch=epoch,
            band=band,
            target_mask=target_mask,
            pred_mask=pred_mask,
            clean_band=clean_band,
            noisy_band=noisy_band,
            enhanced_band=enhanced_band,
            clean_db=clean_db,
            noisy_db=noisy_db,
            enhanced_db=enhanced_db,
            abs_error_db=abs_error_db,
        )
        self._plot_mask(epoch, band, target_mask, pred_mask)
        self._plot_band_spectrum(epoch, band, clean_db, noisy_db, enhanced_db)
        self._plot_band_error(epoch, band, abs_error_db)

        metrics = self._compute_band_metrics(
            epoch=epoch,
            train_loss=self.train_losses[-1],
            val_loss=self.val_losses[-1],
            target_mask=target_mask,
            pred_mask=pred_mask,
            clean_db=clean_db,
            noisy_db=noisy_db,
            enhanced_db=enhanced_db,
        )

        if self.save_waveform and "noisy_wav" in sample and "clean_wav" in sample:
            wav_metrics = self._save_waveform_outputs(epoch, model, sample, device)
            metrics.update(wav_metrics)

        self._append_metrics_csv(metrics)
        if self.metrics_txt:
            self._save_metrics_txt(epoch, metrics)

    def _compute_band_metrics(
        self,
        *,
        epoch: int,
        train_loss: float,
        val_loss: float,
        target_mask: np.ndarray,
        pred_mask: np.ndarray,
        clean_db: np.ndarray,
        noisy_db: np.ndarray,
        enhanced_db: np.ndarray,
    ) -> Dict[str, float]:
        mask_mae = float(np.mean(np.abs(pred_mask - target_mask)))
        mask_rmse = float(np.sqrt(np.mean((pred_mask - target_mask) ** 2)))
        band_mae_db = float(np.mean(np.abs(enhanced_db - clean_db)))
        noisy_band_mae_db = float(np.mean(np.abs(noisy_db - clean_db)))
        band_mae_improvement_db = float(noisy_band_mae_db - band_mae_db)
        band_rmse_db = float(np.sqrt(np.mean((enhanced_db - clean_db) ** 2)))
        spectral_convergence = float(
            np.linalg.norm(enhanced_db - clean_db) / max(np.linalg.norm(clean_db), 1e-8)
        )

        return {
            "epoch": float(epoch),
            "train_loss": float(train_loss),
            "val_loss": float(val_loss),
            "mask_mae": mask_mae,
            "mask_rmse": mask_rmse,
            "band_mae_db": band_mae_db,
            "noisy_band_mae_db": noisy_band_mae_db,
            "band_mae_improvement_db": band_mae_improvement_db,
            "band_rmse_db": band_rmse_db,
            "spectral_convergence": spectral_convergence,
        }

    @torch.no_grad()
    def _save_waveform_outputs(
        self,
        epoch: int,
        model: torch.nn.Module,
        sample: Dict[str, torch.Tensor],
        device: torch.device,
    ) -> Dict[str, float]:
        clean = sample["clean_wav"].detach().cpu().numpy().astype(np.float32)
        noisy = sample["noisy_wav"].detach().cpu().numpy().astype(np.float32)
        enhanced = self._enhance_wav(noisy, model, device)

        clean, noisy, enhanced = self._same_length(clean, noisy, enhanced)
        sr = int(self.cfg["sample_rate"])

        self._save_waveform_csv(epoch, sr, clean, noisy, enhanced)
        self._plot_waveform(epoch, sr, clean, noisy, enhanced)
        if self.save_wav_spectrum:
            self._plot_wav_spectrum(epoch, sr, clean, noisy, enhanced)
        if self.save_wav_audio:
            self._save_audio(epoch, sr, clean, noisy, enhanced)

        noisy_snr = _snr_db(clean, noisy)
        enhanced_snr = _snr_db(clean, enhanced)
        noisy_si_sdr = _si_sdr_db(clean, noisy)
        enhanced_si_sdr = _si_sdr_db(clean, enhanced)

        return {
            "noisy_snr_db": noisy_snr,
            "enhanced_snr_db": enhanced_snr,
            "snr_improvement_db": enhanced_snr - noisy_snr,
            "noisy_si_sdr_db": noisy_si_sdr,
            "enhanced_si_sdr_db": enhanced_si_sdr,
            "si_sdr_improvement_db": enhanced_si_sdr - noisy_si_sdr,
            "waveform_l1": float(np.mean(np.abs(enhanced - clean))),
            "waveform_rmse": float(np.sqrt(np.mean((enhanced - clean) ** 2))),
        }

    @torch.no_grad()
    def _enhance_wav(self, wav: np.ndarray, model: torch.nn.Module, device: torch.device) -> np.ndarray:
        """Frame-by-frame inference for full waveform visualization."""
        sr = int(self.cfg["sample_rate"])
        n_fft = int(self.cfg["n_fft"])
        win_length = int(self.cfg["win_length"])
        hop = int(self.cfg["hop_length"])
        n_bands = int(self.cfg["n_bands"])
        context = int(self.cfg["context_frames"])

        x = torch.from_numpy(wav).float().to(device).unsqueeze(0)
        window = torch.hann_window(win_length, device=device)
        spec = torch.stft(
            x,
            n_fft=n_fft,
            hop_length=hop,
            win_length=win_length,
            window=window,
            center=True,
            return_complex=True,
        )
        mag = spec.abs().transpose(1, 2).contiguous()  # [1,frames,bins]
        fb = band_matrix(sr, n_fft, n_bands, device=device, dtype=mag.dtype)
        band = torch.matmul(mag, fb.t())
        feat = feature_from_band(band).squeeze(0)  # [frames,bands]

        frames = feat.shape[0]
        masks = []
        hist = torch.zeros(context, n_bands, 1, device=device)

        was_training = model.training
        model.eval()
        for t in range(frames):
            hist = torch.roll(hist, shifts=-1, dims=0)
            hist[-1, :, 0] = feat[t]
            if t < context - 1:
                hist[: context - 1 - t, :, 0] = feat[0]
            pred = model(hist.unsqueeze(0)).squeeze(0).detach().cpu().numpy()
            masks.append(np.clip(pred, 0.0, 1.0))
        if was_training:
            model.train(True)

        masks_np = np.stack(masks, axis=0).astype(np.float32)  # [frames,bands]
        bin_masks = []
        for t in range(frames):
            bin_masks.append(band_mask_to_bin_mask(masks_np[t], sr, n_fft, n_bands))
        bin_masks_t = torch.from_numpy(np.stack(bin_masks, axis=0)).float().to(device)  # [frames,bins]

        enhanced_spec = spec * bin_masks_t.t().unsqueeze(0)
        y = torch.istft(
            enhanced_spec,
            n_fft=n_fft,
            hop_length=hop,
            win_length=win_length,
            window=window,
            center=True,
            length=len(wav),
        )
        return y.squeeze(0).detach().cpu().numpy().astype(np.float32)

    @staticmethod
    def _same_length(*arrays: np.ndarray) -> Tuple[np.ndarray, ...]:
        n = min(len(a) for a in arrays)
        return tuple(a[:n] for a in arrays)

    def _save_band_csv(self, epoch: int, **arrays: Dict[str, np.ndarray]) -> None:
        path = self.data_dir / f"epoch_{epoch:04d}_band_data.csv"
        keys = [
            "band",
            "target_mask",
            "pred_mask",
            "clean_band",
            "noisy_band",
            "enhanced_band",
            "clean_db",
            "noisy_db",
            "enhanced_db",
            "abs_error_db",
        ]
        rows = zip(*(arrays[k] for k in keys))
        with path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(keys)
            writer.writerows(rows)

    def _save_waveform_csv(
        self,
        epoch: int,
        sr: int,
        clean: np.ndarray,
        noisy: np.ndarray,
        enhanced: np.ndarray,
    ) -> None:
        path = self.data_dir / f"epoch_{epoch:04d}_waveform_data.csv"
        n = len(clean)
        t = np.arange(n, dtype=np.float32) / float(sr)
        with path.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["time_sec", "clean", "noisy", "enhanced"])
            writer.writerows(zip(t, clean, noisy, enhanced))

    def _append_metrics_csv(self, metrics: Dict[str, float]) -> None:
        path = self.fig_dir / "metrics_summary.csv"
        keys = list(metrics.keys())
        write_header = not path.exists()
        if not write_header:
            # Keep stable header when optional waveform metrics appear from the first epoch.
            with path.open("r", encoding="utf-8") as f:
                first = f.readline().strip()
            if first:
                keys = first.split(",")

        with path.open("a", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=keys)
            if write_header:
                writer.writeheader()
            writer.writerow({k: metrics.get(k, "") for k in keys})

    def _save_metrics_txt(self, epoch: int, metrics: Dict[str, float]) -> None:
        lines = [
            "Tiny-CRN validation visualization metrics",
            f"Epoch: {int(metrics.get('epoch', epoch))}",
            "",
            "Loss",
            f"  train_loss: {metrics.get('train_loss', float('nan')):.8f}",
            f"  val_loss:   {metrics.get('val_loss', float('nan')):.8f}",
            "",
            "Mask / band spectrum metrics on one fixed validation sample",
            f"  mask_mae:                    {metrics.get('mask_mae', float('nan')):.8f}",
            f"  mask_rmse:                   {metrics.get('mask_rmse', float('nan')):.8f}",
            f"  noisy_band_mae_db:           {metrics.get('noisy_band_mae_db', float('nan')):.8f}",
            f"  enhanced_band_mae_db:        {metrics.get('band_mae_db', float('nan')):.8f}",
            f"  band_mae_improvement_db:     {metrics.get('band_mae_improvement_db', float('nan')):.8f}",
            f"  band_rmse_db:                {metrics.get('band_rmse_db', float('nan')):.8f}",
            f"  spectral_convergence:        {metrics.get('spectral_convergence', float('nan')):.8f}",
        ]

        if "enhanced_snr_db" in metrics:
            lines += [
                "",
                "Waveform metrics on the same fixed validation sample",
                f"  noisy_snr_db:                {metrics.get('noisy_snr_db', float('nan')):.8f}",
                f"  enhanced_snr_db:             {metrics.get('enhanced_snr_db', float('nan')):.8f}",
                f"  snr_improvement_db:          {metrics.get('snr_improvement_db', float('nan')):.8f}",
                f"  noisy_si_sdr_db:             {metrics.get('noisy_si_sdr_db', float('nan')):.8f}",
                f"  enhanced_si_sdr_db:          {metrics.get('enhanced_si_sdr_db', float('nan')):.8f}",
                f"  si_sdr_improvement_db:       {metrics.get('si_sdr_improvement_db', float('nan')):.8f}",
                f"  waveform_l1:                 {metrics.get('waveform_l1', float('nan')):.8f}",
                f"  waveform_rmse:               {metrics.get('waveform_rmse', float('nan')):.8f}",
            ]

        lines += [
            "",
            "Note",
            "  These metrics are for training-process visualization on one fixed validation sample.",
            "  For final paper tables, run a full test-set evaluation separately.",
        ]

        text = "\n".join(lines) + "\n"
        (self.fig_dir / f"metrics_epoch_{epoch:04d}.txt").write_text(text, encoding="utf-8")
        (self.fig_dir / "metrics_latest.txt").write_text(text, encoding="utf-8")

    def _plot_mask(self, epoch: int, band: np.ndarray, target_mask: np.ndarray, pred_mask: np.ndarray) -> None:
        plt = self._plt
        fig, ax = plt.subplots(figsize=(8, 4.5), dpi=140)
        ax.plot(band, target_mask, linewidth=1.8, label="ideal_mask")
        ax.plot(band, pred_mask, linewidth=1.8, label="pred_mask")
        ax.set_title(f"Mask comparison - epoch {epoch}")
        ax.set_xlabel("Frequency band index, low to high")
        ax.set_ylabel("Mask")
        ax.set_ylim(-0.05, 1.05)
        ax.grid(True, alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(self.fig_dir / f"epoch_{epoch:04d}_mask.png")
        plt.close(fig)

    def _plot_band_spectrum(
        self,
        epoch: int,
        band: np.ndarray,
        clean_db: np.ndarray,
        noisy_db: np.ndarray,
        enhanced_db: np.ndarray,
    ) -> None:
        plt = self._plt
        fig, ax = plt.subplots(figsize=(8, 4.5), dpi=140)
        ax.plot(band, noisy_db, linewidth=1.6, label="noisy_band")
        ax.plot(band, clean_db, linewidth=1.8, label="clean_band")
        ax.plot(band, enhanced_db, linewidth=1.8, label="enhanced_band")
        ax.set_title(f"Frequency-band spectrum comparison - epoch {epoch}")
        ax.set_xlabel("Frequency band index, low to high")
        ax.set_ylabel("Magnitude, dB")
        ax.grid(True, alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(self.fig_dir / f"epoch_{epoch:04d}_band_spectrum.png")
        plt.close(fig)

    def _plot_band_error(self, epoch: int, band: np.ndarray, abs_error_db: np.ndarray) -> None:
        plt = self._plt
        fig, ax = plt.subplots(figsize=(8, 4.5), dpi=140)
        ax.bar(band, abs_error_db)
        ax.set_title(f"Enhanced vs clean absolute band error - epoch {epoch}")
        ax.set_xlabel("Frequency band index, low to high")
        ax.set_ylabel("Absolute error, dB")
        ax.grid(True, axis="y", alpha=0.3)
        fig.tight_layout()
        fig.savefig(self.fig_dir / f"epoch_{epoch:04d}_band_error.png")
        plt.close(fig)

    def _plot_waveform(self, epoch: int, sr: int, clean: np.ndarray, noisy: np.ndarray, enhanced: np.ndarray) -> None:
        plt = self._plt
        t = np.arange(len(clean), dtype=np.float32) / float(sr)
        fig, ax = plt.subplots(figsize=(10, 4.8), dpi=140)
        ax.plot(t, noisy, linewidth=0.8, alpha=0.75, label="noisy")
        ax.plot(t, clean, linewidth=0.9, alpha=0.9, label="clean")
        ax.plot(t, enhanced, linewidth=0.9, alpha=0.9, label="enhanced")
        ax.set_title(f"Waveform comparison - epoch {epoch}")
        ax.set_xlabel("Time, s")
        ax.set_ylabel("Amplitude")
        ax.grid(True, alpha=0.25)
        ax.legend(loc="upper right")
        fig.tight_layout()
        fig.savefig(self.fig_dir / f"epoch_{epoch:04d}_waveform.png")
        plt.close(fig)

    def _plot_wav_spectrum(self, epoch: int, sr: int, clean: np.ndarray, noisy: np.ndarray, enhanced: np.ndarray) -> None:
        plt = self._plt
        freq, clean_db = _rfft_db(clean, sr)
        _, noisy_db = _rfft_db(noisy, sr)
        _, enhanced_db = _rfft_db(enhanced, sr)

        fig, ax = plt.subplots(figsize=(9, 4.8), dpi=140)
        ax.plot(freq, noisy_db, linewidth=1.0, label="noisy")
        ax.plot(freq, clean_db, linewidth=1.1, label="clean")
        ax.plot(freq, enhanced_db, linewidth=1.1, label="enhanced")
        ax.set_title(f"Waveform spectrum comparison - epoch {epoch}")
        ax.set_xlabel("Frequency, Hz")
        ax.set_ylabel("Magnitude, dB")
        ax.grid(True, alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(self.fig_dir / f"epoch_{epoch:04d}_wav_spectrum.png")
        plt.close(fig)

    def _save_audio(self, epoch: int, sr: int, clean: np.ndarray, noisy: np.ndarray, enhanced: np.ndarray) -> None:
        try:
            import soundfile as sf
        except Exception as exc:  # pragma: no cover
            warnings.warn(f"save_wav_audio=True but soundfile import failed: {exc}")
            return
        sf.write(str(self.audio_dir / f"epoch_{epoch:04d}_clean.wav"), clean, sr)
        sf.write(str(self.audio_dir / f"epoch_{epoch:04d}_noisy.wav"), noisy, sr)
        sf.write(str(self.audio_dir / f"epoch_{epoch:04d}_enhanced.wav"), enhanced, sr)


def _rfft_db(wav: np.ndarray, sr: int) -> Tuple[np.ndarray, np.ndarray]:
    wav = np.asarray(wav, dtype=np.float32)
    if wav.size == 0:
        return np.zeros(1, dtype=np.float32), np.zeros(1, dtype=np.float32)
    window = np.hanning(len(wav)).astype(np.float32)
    spec = np.fft.rfft(wav * window)
    mag = np.abs(spec).astype(np.float32)
    db = 20.0 * np.log10(np.maximum(mag, 1e-8))
    freq = np.fft.rfftfreq(len(wav), d=1.0 / float(sr)).astype(np.float32)
    return freq, db


def _snr_db(clean: np.ndarray, test: np.ndarray) -> float:
    clean = np.asarray(clean, dtype=np.float64)
    test = np.asarray(test, dtype=np.float64)
    noise = test - clean
    signal_power = float(np.sum(clean ** 2))
    noise_power = float(np.sum(noise ** 2))
    return 10.0 * math.log10((signal_power + 1e-12) / (noise_power + 1e-12))


def _si_sdr_db(clean: np.ndarray, test: np.ndarray) -> float:
    clean = np.asarray(clean, dtype=np.float64)
    test = np.asarray(test, dtype=np.float64)
    clean = clean - np.mean(clean)
    test = test - np.mean(test)
    target = np.dot(test, clean) * clean / (np.dot(clean, clean) + 1e-12)
    noise = test - target
    ratio = (np.sum(target ** 2) + 1e-12) / (np.sum(noise ** 2) + 1e-12)
    return 10.0 * math.log10(ratio)
