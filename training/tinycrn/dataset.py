from pathlib import Path
from typing import List

import numpy as np
import torch
from torch.utils.data import Dataset

from .audio import load_audio_mono, random_segment, mix_at_snr, read_list
from .features import wav_to_training_tensors


class OnTheFlyNoisyDataset(Dataset):
    """Infinite-style dataset: random clean + random noise + random SNR."""

    def __init__(self, clean_list: str, noise_list: str, cfg: dict, steps: int, seed: int = 0):
        self.clean_files = read_list(clean_list)
        self.noise_files = read_list(noise_list)
        if not self.clean_files:
            raise ValueError("clean file list is empty")
        if not self.noise_files:
            raise ValueError("noise file list is empty")
        self.cfg = cfg
        self.steps = int(steps)
        self.base_seed = int(seed)
        self.sr = int(cfg["sample_rate"])
        self.seg_len = int(round(float(cfg["segment_seconds"]) * self.sr))
        self.context = int(cfg["context_frames"])

    def __len__(self):
        return self.steps

    def __getitem__(self, idx):
        worker = torch.utils.data.get_worker_info()
        wid = 0 if worker is None else worker.id
        rng = np.random.default_rng(self.base_seed + idx * 1009 + wid * 9176)

        clean_path = self.clean_files[int(rng.integers(0, len(self.clean_files)))]
        noise_path = self.noise_files[int(rng.integers(0, len(self.noise_files)))]

        clean_full = load_audio_mono(clean_path, self.sr, min_len=self.seg_len)
        noise_full = load_audio_mono(noise_path, self.sr, min_len=self.seg_len)
        clean = random_segment(clean_full, self.seg_len, rng)
        noise = random_segment(noise_full, self.seg_len, rng)

        snr = float(rng.uniform(float(self.cfg["snr_min_db"]), float(self.cfg["snr_max_db"])))
        gain = float(rng.uniform(float(self.cfg.get("speech_gain_min_db", -6.0)),
                                 float(self.cfg.get("speech_gain_max_db", 3.0))))
        noisy, clean_scaled, _ = mix_at_snr(clean, noise, snr, rng, speech_gain_db=gain)

        noisy_t = torch.from_numpy(noisy).float().unsqueeze(0)
        clean_t = torch.from_numpy(clean_scaled).float().unsqueeze(0)
        feat, mask, noisy_band, clean_band = wav_to_training_tensors(
            noisy_t, clean_t,
            sample_rate=self.sr,
            n_fft=int(self.cfg["n_fft"]),
            win_length=int(self.cfg["win_length"]),
            hop_length=int(self.cfg["hop_length"]),
            n_bands=int(self.cfg["n_bands"]),
        )
        feat = feat.squeeze(0)        # [frames,bands]
        mask = mask.squeeze(0)
        noisy_band = noisy_band.squeeze(0)
        clean_band = clean_band.squeeze(0)

        frames = feat.shape[0]
        if frames <= self.context:
            # Pad if very short, should rarely happen with 2s segments.
            pad = self.context + 1 - frames
            feat = torch.cat([feat[:1].repeat(pad, 1), feat], dim=0)
            mask = torch.cat([mask[:1].repeat(pad, 1), mask], dim=0)
            noisy_band = torch.cat([noisy_band[:1].repeat(pad, 1), noisy_band], dim=0)
            clean_band = torch.cat([clean_band[:1].repeat(pad, 1), clean_band], dim=0)
            frames = feat.shape[0]
        cur = int(rng.integers(self.context - 1, frames))
        x = feat[cur - self.context + 1:cur + 1]         # [T,F]
        y_mask = mask[cur]                               # [F]
        y_clean = clean_band[cur]
        x_noisy_band = noisy_band[cur]

        # NHWC model input: [T,F,1]
        # noisy_wav / clean_wav 只给训练过程可视化用；原训练 loss 不使用它们。
        return {
            "x": x.unsqueeze(-1).float(),
            "mask": y_mask.float(),
            "clean_band": y_clean.float(),
            "noisy_band": x_noisy_band.float(),
            "noisy_wav": noisy_t.squeeze(0).float(),
            "clean_wav": clean_t.squeeze(0).float(),
            "snr_db": torch.tensor(snr, dtype=torch.float32),
        }
