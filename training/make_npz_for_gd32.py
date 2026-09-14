import argparse
from pathlib import Path

import numpy as np
import torch
import yaml
from tqdm import tqdm

from tinycrn.audio import load_audio_mono, random_segment, mix_at_snr, read_list
from tinycrn.features import wav_to_training_tensors


PROJECT_DIR = Path(__file__).resolve().parent

DEFAULT_CONFIG = PROJECT_DIR / "configs" / "train.yaml"
DEFAULT_CLEAN_LIST = PROJECT_DIR / "filelists" / "val_clean.txt"
DEFAULT_NOISE_LIST = PROJECT_DIR / "filelists" / "val_noise.txt"
DEFAULT_OUT = PROJECT_DIR / "exports" / "train.npz"

DEFAULT_NUM_SAMPLES = 256
DEFAULT_SEED = 2025

# GD32 手册示例使用 data=dataset。
# 如果 data 仍失败，再运行时加：--key input
DEFAULT_NPZ_KEY = "data"


def project_path(path_like):
    p = Path(path_like)
    return p if p.is_absolute() else PROJECT_DIR / p


def make_npz(
    config=DEFAULT_CONFIG,
    clean_list=DEFAULT_CLEAN_LIST,
    noise_list=DEFAULT_NOISE_LIST,
    out=DEFAULT_OUT,
    num_samples=DEFAULT_NUM_SAMPLES,
    seed=DEFAULT_SEED,
    key=DEFAULT_NPZ_KEY,
):
    config = project_path(config)
    clean_list = project_path(clean_list)
    noise_list = project_path(noise_list)
    out = project_path(out)

    cfg = yaml.safe_load(open(config, "r", encoding="utf-8"))

    clean_files = read_list(str(clean_list))
    noise_files = read_list(str(noise_list))

    if len(clean_files) == 0:
        raise RuntimeError(f"clean list is empty: {clean_list}")
    if len(noise_files) == 0:
        raise RuntimeError(f"noise list is empty: {noise_list}")

    rng = np.random.default_rng(int(seed))

    sr = int(cfg["sample_rate"])
    seg_len = int(round(float(cfg["segment_seconds"]) * sr))
    context = int(cfg["context_frames"])
    n_bands = int(cfg["n_bands"])

    print("config:", config)
    print("clean_list:", clean_list)
    print("noise_list:", noise_list)
    print("out:", out)
    print("num_samples:", num_samples)
    print("npz_key:", key)
    print("sample_rate:", sr)
    print("context_frames:", context)
    print("n_bands:", n_bands)

    xs = []

    for _ in tqdm(range(int(num_samples))):
        clean_path = clean_files[int(rng.integers(0, len(clean_files)))]
        noise_path = noise_files[int(rng.integers(0, len(noise_files)))]

        clean_full = load_audio_mono(clean_path, sr, min_len=seg_len)
        noise_full = load_audio_mono(noise_path, sr, min_len=seg_len)

        clean = random_segment(clean_full, seg_len, rng)
        noise = random_segment(noise_full, seg_len, rng)

        snr = float(rng.uniform(float(cfg["snr_min_db"]), float(cfg["snr_max_db"])))
        gain = float(
            rng.uniform(
                float(cfg.get("speech_gain_min_db", -6.0)),
                float(cfg.get("speech_gain_max_db", 3.0)),
            )
        )

        noisy, clean_scaled, _ = mix_at_snr(
            clean,
            noise,
            snr,
            rng,
            speech_gain_db=gain,
        )

        feat, _, _, _ = wav_to_training_tensors(
            torch.from_numpy(noisy).float().unsqueeze(0),
            torch.from_numpy(clean_scaled).float().unsqueeze(0),
            sample_rate=sr,
            n_fft=int(cfg["n_fft"]),
            win_length=int(cfg["win_length"]),
            hop_length=int(cfg["hop_length"]),
            n_bands=n_bands,
        )

        # feat: [1, frames, bands]
        feat = feat.squeeze(0)  # [frames, bands]

        frames = feat.shape[0]

        if frames < context:
            pad = feat[:1].repeat(context - frames, 1)
            window = torch.cat([pad, feat], dim=0)
        else:
            cur = int(rng.integers(context - 1, frames))
            window = feat[cur - context + 1 : cur + 1]

        # window: [8, 32]
        # TFLite / GD32 模型输入单样本：[1, 8, 32, 1]
        sample = window.unsqueeze(-1).numpy().astype(np.float32)  # [8, 32, 1]
        xs.append(sample)

    # 注意：从这里开始必须在 for 循环外
    arr = np.stack(xs, axis=0).astype(np.float32)  # [N, 8, 32, 1]

    assert arr.ndim == 4, arr.shape
    assert arr.shape[0] == int(num_samples), arr.shape
    assert arr.shape[1] == context, arr.shape
    assert arr.shape[2] == n_bands, arr.shape
    assert arr.shape[3] == 1, arr.shape

    out.parent.mkdir(parents=True, exist_ok=True)

    # 单输入模型：只保存一个 key，避免 GD32 工具误判成多输入模型
    np.savez(str(out), **{str(key): arr})

    print(f"wrote {out}: {arr.shape} {arr.dtype}")
    print(f"npz keys: {key}")
    print(f"GD32/TFLite input sample shape should be [1,{context},{n_bands},1]")

    check = np.load(str(out))
    print("check keys:", check.files)
    print("check shape:", check[str(key)].shape)
    print("check dtype:", check[str(key)].dtype)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=str(DEFAULT_CONFIG))
    ap.add_argument("--clean_list", default=str(DEFAULT_CLEAN_LIST))
    ap.add_argument("--noise_list", default=str(DEFAULT_NOISE_LIST))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    ap.add_argument("--num_samples", type=int, default=DEFAULT_NUM_SAMPLES)
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED)
    ap.add_argument("--key", default=DEFAULT_NPZ_KEY, choices=["data", "input", "input_0", "input_1"])
    args = ap.parse_args()

    make_npz(
        config=args.config,
        clean_list=args.clean_list,
        noise_list=args.noise_list,
        out=args.out,
        num_samples=args.num_samples,
        seed=args.seed,
        key=args.key,
    )


if __name__ == "__main__":
    main()