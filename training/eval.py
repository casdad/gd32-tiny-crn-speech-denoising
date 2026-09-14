"""
6.2 降噪效果测试脚本（PyCharm 直接运行版）

使用方法：
1. 把本文件放到 Tiny_crn_gd32_ 工程根目录下，也就是和 train.py、eval_denoise.py 同级。
2. 在 PyCharm 中打开本文件，检查下面“用户配置区”的路径。
3. 直接点击 Run 运行。
4. 结果会输出到 eval_62_results 文件夹：
   - detail_metrics.csv：每条样本的详细指标
   - summary_metrics.csv：按噪声类型和 SNR 汇总后的指标
   - table_6_1_for_paper.csv：可直接填论文表 6-1 的结果
   - audio/：测试用 clean/noisy/traditional/crn 音频
   - figures/：降噪前后波形与谱图对比图

说明：
- 本脚本可以自动计算 Noisy、传统谱减法、CRN 的 SNR、Delta SNR、STOI、PESQ、频带 MAE。
- STOI 需要安装 pystoi：pip install pystoi
- PESQ 需要安装 pesq：pip install pesq
- 如果没有安装 pystoi 或 pesq，脚本仍可运行，对应指标会显示为空。
- RNN 示例指标无法从训练代码自动得到；若要填 RNN 示例指标，需要先由板端或 RNN 程序生成对应 wav，再另行计算。
"""

from __future__ import annotations

import csv
import math
import re
import sys
from collections import defaultdict
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
import soundfile as sf
import torch
from scipy.signal import resample_poly

# 避免服务器/无显示环境报错；PyCharm 本地也可正常保存图像
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


plt.rcParams['font.sans-serif'] = ['SimHei']  # 黑体
plt.rcParams['axes.unicode_minus'] = False    # 解决负号显示问题

# ======== 用户配置区：主要改这里 ========
PROJECT_DIR = Path(__file__).resolve().parent

CKPT_PATH = PROJECT_DIR / "runs" / "train" / "best.pt"
CLEAN_LIST = PROJECT_DIR / "filelists" / "test_clean.txt"
NOISE_LIST = PROJECT_DIR / "filelists" / "test_noise.txt"
OUT_DIR = PROJECT_DIR / "eval_results"

# 每个 clean/noise 文件截取的测试片段长度。论文测试建议 2~5 s，数据量不宜过大。
SEG_SECONDS = 2.0

# 每个 SNR 条件下测试多少条干净语音、多少条噪声。先小规模验证，后续可以加大。
NUM_CLEAN = 4
NUM_NOISE = 4

# 表 6-1 的四个典型场景。noise_index 表示使用 test_noise.txt 里的第几个噪声文件。
# 你可以根据噪声文件实际内容修改 noise_type 和 noise_index。
TEST_CASES = [
    {"noise_type": "风扇/空调噪声", "input_snr_db": -5, "noise_index": 0},
    {"noise_type": "键盘敲击噪声", "input_snr_db": 0, "noise_index": 1},
    {"noise_type": "室内人声背景", "input_snr_db": 5, "noise_index": 2},
    {"noise_type": "环境混响/室内噪声", "input_snr_db": 10, "noise_index": 3},
]

RANDOM_SEED = 2026
SAVE_AUDIO_EXAMPLES = True
SAVE_FIGURE_EXAMPLES = True
# ======================================


# 导入你工程里的函数和模型
try:
    from tinycrn.audio import load_audio_mono, random_segment, mix_at_snr, read_list
    from tinycrn.features import band_matrix, feature_from_band, band_mask_to_bin_mask
    from tinycrn.model import build_model_from_config
except Exception as e:
    raise RuntimeError(
        "请把本脚本放到 Tiny_crn_gd32_ 工程根目录下运行，确保能看到 tinycrn 文件夹。\n"
        f"当前 PROJECT_DIR={PROJECT_DIR}\n原始错误：{e}"
    )


# 可选指标库：没有安装也不影响运行
try:
    from pystoi import stoi as stoi_func
except Exception:
    stoi_func = None

try:
    from pesq import pesq as pesq_func
except Exception:
    pesq_func = None


def safe_float(x, ndigits: int = 4):
    """把数值转成适合写 CSV 的格式。"""
    if x is None:
        return ""
    try:
        x = float(x)
    except Exception:
        return ""
    if math.isnan(x) or math.isinf(x):
        return ""
    return round(x, ndigits)


def resolve_path(p: str | Path) -> Path:
    """支持绝对路径和相对工程路径。"""
    p = Path(str(p).strip().strip('"'))
    if p.is_absolute():
        return p
    return PROJECT_DIR / p


def read_audio_list(list_path: Path) -> List[Path]:
    items = []
    for line in read_list(str(list_path)):
        p = resolve_path(line)
        items.append(p)
    if not items:
        raise RuntimeError(f"文件列表为空：{list_path}")
    return items


def load_model(ckpt_path: Path, device: torch.device):
    if not ckpt_path.exists():
        raise FileNotFoundError(f"找不到模型文件：{ckpt_path}")
    ckpt = torch.load(str(ckpt_path), map_location=device)
    cfg = ckpt["cfg"]
    model = build_model_from_config(cfg).to(device)
    model.load_state_dict(ckpt["model"], strict=True)
    model.eval()
    return model, cfg


def crn_enhance(wav: np.ndarray, model, cfg: dict, device: torch.device) -> np.ndarray:
    """使用训练好的 TinyCRN 对单段语音做离线降噪。"""
    sr = int(cfg["sample_rate"])
    n_fft = int(cfg["n_fft"])
    win_length = int(cfg["win_length"])
    hop = int(cfg["hop_length"])
    n_bands = int(cfg["n_bands"])
    context = int(cfg["context_frames"])

    x = torch.from_numpy(wav.astype(np.float32)).to(device).unsqueeze(0)
    window = torch.hann_window(win_length, device=device)

    spec = torch.stft(
        x, n_fft=n_fft, hop_length=hop, win_length=win_length,
        window=window, center=True, return_complex=True
    )
    mag = spec.abs().transpose(1, 2).contiguous()  # [1, frames, bins]

    fb = band_matrix(sr, n_fft, n_bands, device=device, dtype=mag.dtype)
    band = torch.matmul(mag, fb.t())
    feat = feature_from_band(band).squeeze(0)  # [frames, bands]

    frames = int(feat.shape[0])
    masks = []
    hist = torch.zeros(context, n_bands, 1, device=device)

    with torch.no_grad():
        for t in range(frames):
            hist = torch.roll(hist, shifts=-1, dims=0)
            hist[-1, :, 0] = feat[t]
            if t < context - 1:
                hist[: context - 1 - t, :, 0] = feat[0]
            pred = model(hist.unsqueeze(0)).squeeze(0).detach().cpu().numpy()
            masks.append(pred.astype(np.float32))

    masks = np.stack(masks, axis=0)
    bin_masks = [band_mask_to_bin_mask(m, sr, n_fft, n_bands) for m in masks]
    bin_masks = torch.from_numpy(np.stack(bin_masks, axis=0)).float().to(device)  # [frames,bins]

    enhanced_spec = spec * bin_masks.t().unsqueeze(0)
    y = torch.istft(
        enhanced_spec, n_fft=n_fft, hop_length=hop, win_length=win_length,
        window=window, center=True, length=len(wav)
    )
    return y.squeeze(0).detach().cpu().numpy().astype(np.float32)


def spectral_subtract(noisy: np.ndarray, sr: int, noise_seconds: float = 0.25) -> np.ndarray:
    """传统谱减法基线：仅作为论文对比里的传统方法参考。"""
    n_fft = 1024
    hop = 480 if sr == 48000 else int(0.01 * sr)
    win_length = 960 if sr == 48000 else int(0.02 * sr)
    win_length = min(win_length, n_fft)

    device = torch.device("cpu")
    x = torch.from_numpy(noisy.astype(np.float32)).unsqueeze(0)
    window = torch.hann_window(win_length, device=device)
    spec = torch.stft(
        x, n_fft=n_fft, hop_length=hop, win_length=win_length,
        window=window, center=True, return_complex=True
    )
    mag = spec.abs()
    phase = torch.angle(spec)

    noise_frames = max(1, int(noise_seconds * sr / hop))
    noise_mag = mag[:, :, :noise_frames].mean(dim=2, keepdim=True)
    enhanced_mag = torch.clamp(mag - 1.2 * noise_mag, min=0.02 * mag)
    enhanced_spec = enhanced_mag * torch.exp(1j * phase)

    y = torch.istft(
        enhanced_spec, n_fft=n_fft, hop_length=hop, win_length=win_length,
        window=window, center=True, length=len(noisy)
    )
    return y.squeeze(0).numpy().astype(np.float32)


def snr_db(clean: np.ndarray, test: np.ndarray, eps: float = 1e-10) -> float:
    clean, test = align_pair(clean, test)
    noise = test - clean
    return 10.0 * math.log10((float(np.sum(clean ** 2)) + eps) / (float(np.sum(noise ** 2)) + eps))


def si_snr_db(clean: np.ndarray, test: np.ndarray, eps: float = 1e-10) -> float:
    clean, test = align_pair(clean, test)
    clean = clean.astype(np.float64) - np.mean(clean)
    test = test.astype(np.float64) - np.mean(test)
    target = np.sum(test * clean) * clean / (np.sum(clean ** 2) + eps)
    noise = test - target
    return 10.0 * math.log10((np.sum(target ** 2) + eps) / (np.sum(noise ** 2) + eps))


def align_pair(a: np.ndarray, b: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
    n = min(len(a), len(b))
    return a[:n].astype(np.float32), b[:n].astype(np.float32)


def resample_to(x: np.ndarray, sr_from: int, sr_to: int) -> np.ndarray:
    if sr_from == sr_to:
        return x.astype(np.float32)
    g = math.gcd(sr_from, sr_to)
    return resample_poly(x, sr_to // g, sr_from // g).astype(np.float32)


def stoi_score(clean: np.ndarray, test: np.ndarray, sr: int):
    if stoi_func is None:
        return None
    clean, test = align_pair(clean, test)
    # STOI 常用 10 kHz 或 16 kHz，这里统一重采样到 16 kHz。
    clean16 = resample_to(clean, sr, 16000)
    test16 = resample_to(test, sr, 16000)
    try:
        return float(stoi_func(clean16, test16, 16000, extended=False))
    except Exception:
        return None


def pesq_score(clean: np.ndarray, test: np.ndarray, sr: int):
    if pesq_func is None:
        return None
    clean, test = align_pair(clean, test)
    # pesq 包通常只支持 8000/16000，这里统一到 16 kHz 宽带。
    clean16 = resample_to(clean, sr, 16000)
    test16 = resample_to(test, sr, 16000)
    try:
        return float(pesq_func(16000, clean16, test16, "wb"))
    except Exception:
        return None


def band_mae_db(clean: np.ndarray, test: np.ndarray, sr: int, n_fft: int, win_length: int, hop: int, n_bands: int) -> float:
    """频带平均绝对误差，单位 dB，越小越好。"""
    clean, test = align_pair(clean, test)
    device = torch.device("cpu")
    with torch.no_grad():
        window = torch.hann_window(win_length, device=device)
        c = torch.from_numpy(clean).float().unsqueeze(0)
        t = torch.from_numpy(test).float().unsqueeze(0)
        cs = torch.stft(c, n_fft=n_fft, hop_length=hop, win_length=win_length, window=window, center=True, return_complex=True)
        ts = torch.stft(t, n_fft=n_fft, hop_length=hop, win_length=win_length, window=window, center=True, return_complex=True)
        cmag = cs.abs().transpose(1, 2).contiguous()
        tmag = ts.abs().transpose(1, 2).contiguous()
        fb = band_matrix(sr, n_fft, n_bands, device=device, dtype=torch.float32)
        cb = torch.matmul(cmag, fb.t()).squeeze(0).numpy()
        tb = torch.matmul(tmag, fb.t()).squeeze(0).numpy()
    cb_db = 20.0 * np.log10(np.maximum(cb, 1e-8))
    tb_db = 20.0 * np.log10(np.maximum(tb, 1e-8))
    return float(np.mean(np.abs(tb_db - cb_db)))


def calculate_metrics(clean: np.ndarray, noisy: np.ndarray, test: np.ndarray, sr: int, cfg: dict) -> Dict[str, float | None]:
    base_snr = snr_db(clean, noisy)
    test_snr = snr_db(clean, test)
    return {
        "snr_db": test_snr,
        "delta_snr_db": test_snr - base_snr,
        "si_snr_db": si_snr_db(clean, test),
        "stoi": stoi_score(clean, test, sr),
        "pesq": pesq_score(clean, test, sr),
        "band_mae_db": band_mae_db(
            clean, test, sr,
            n_fft=int(cfg["n_fft"]),
            win_length=int(cfg["win_length"]),
            hop=int(cfg["hop_length"]),
            n_bands=int(cfg["n_bands"]),
        ),
    }


def normalize_audio(x: np.ndarray) -> np.ndarray:
    x = x.astype(np.float32)
    peak = float(np.max(np.abs(x))) if len(x) else 0.0
    if peak > 0.98:
        x = x * (0.98 / peak)
    return x.astype(np.float32)


def safe_stem(path: Path) -> str:
    stem = path.stem
    stem = re.sub(r"[^0-9A-Za-z_\-\u4e00-\u9fff]+", "_", stem)
    return stem[:40]


def save_wav(path: Path, wav: np.ndarray, sr: int):
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), normalize_audio(wav), sr)


def save_compare_figure(path: Path, clean: np.ndarray, noisy: np.ndarray, traditional: np.ndarray, crn: np.ndarray, sr: int, title: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    signals = [
        ("Clean", clean),
        ("Noisy", noisy),
        ("Traditional", traditional),
        ("CRN", crn),
    ]
    max_seconds = min(1.0, len(clean) / sr)
    n_show = int(max_seconds * sr)
    t = np.arange(n_show) / sr

    fig, axes = plt.subplots(len(signals), 2, figsize=(12, 8))
    fig.suptitle(title)
    for i, (name, sig) in enumerate(signals):
        sig = sig[:len(clean)]
        axes[i, 0].plot(t, sig[:n_show])
        axes[i, 0].set_title(f"{name} waveform")
        axes[i, 0].set_xlabel("Time/s")
        axes[i, 0].set_ylabel("Amplitude")

        axes[i, 1].specgram(sig, NFFT=1024, Fs=sr, noverlap=768)
        axes[i, 1].set_title(f"{name} spectrogram")
        axes[i, 1].set_xlabel("Time/s")
        axes[i, 1].set_ylabel("Frequency/Hz")
    plt.tight_layout()
    fig.savefig(str(path), dpi=200)
    plt.close(fig)


def write_csv(path: Path, rows: List[Dict], fieldnames: List[str]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8-sig", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def summarize(rows: List[Dict]) -> List[Dict]:
    keys = ["noise_type", "input_snr_db", "method"]
    metric_names = ["snr_db", "delta_snr_db", "si_snr_db", "stoi", "pesq", "band_mae_db"]
    groups = defaultdict(list)
    for r in rows:
        groups[tuple(r[k] for k in keys)].append(r)

    summary = []
    for group_key, items in sorted(groups.items(), key=lambda x: (str(x[0][0]), float(x[0][1]), str(x[0][2]))):
        out = dict(zip(keys, group_key))
        out["n"] = len(items)
        for m in metric_names:
            vals = []
            for item in items:
                v = item.get(m, "")
                if v == "" or v is None:
                    continue
                vals.append(float(v))
            out[m + "_mean"] = safe_float(np.mean(vals)) if vals else ""
            out[m + "_std"] = safe_float(np.std(vals, ddof=1)) if len(vals) > 1 else ""
        summary.append(out)
    return summary


def make_table_6_1(summary_rows: List[Dict]) -> List[Dict]:
    """生成适合论文表 6-1 的宽表。"""
    index = {}
    for r in summary_rows:
        key = (r["noise_type"], r["input_snr_db"])
        index[(key, r["method"])] = r

    all_keys = sorted({key for key, method in index.keys()}, key=lambda x: (str(x[0]), float(x[1])))
    table = []
    for noise_type, snr in all_keys:
        noisy = index.get(((noise_type, snr), "Noisy"), {})
        trad = index.get(((noise_type, snr), "Traditional"), {})
        crn = index.get(((noise_type, snr), "CRN"), {})
        table.append({
            "噪声类型": noise_type,
            "输入SNR/dB": snr,
            "Noisy_SNR": noisy.get("snr_db_mean", ""),
            "传统方法_SNR": trad.get("snr_db_mean", ""),
            "CRN_SNR": crn.get("snr_db_mean", ""),
            "CRN提升/dB": crn.get("delta_snr_db_mean", ""),
            "Noisy_STOI": noisy.get("stoi_mean", ""),
            "CRN_STOI": crn.get("stoi_mean", ""),
            "Noisy_PESQ": noisy.get("pesq_mean", ""),
            "CRN_PESQ": crn.get("pesq_mean", ""),
            "CRN频带MAE/dB": crn.get("band_mae_db_mean", ""),
            "备注": "同一测试语音，结果为平均值",
        })
    return table


def main():
    print("===== 6.2 降噪效果测试开始 =====")
    print(f"工程目录：{PROJECT_DIR}")
    print(f"模型路径：{CKPT_PATH}")
    print(f"输出目录：{OUT_DIR}")
    if stoi_func is None:
        print("提示：未安装 pystoi，STOI 指标将为空。安装命令：pip install pystoi")
    if pesq_func is None:
        print("提示：未安装 pesq，PESQ 指标将为空。安装命令：pip install pesq")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(RANDOM_SEED)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"推理设备：{device}")

    model, cfg = load_model(CKPT_PATH, device)
    sr = int(cfg["sample_rate"])
    seg_len = int(round(SEG_SECONDS * sr))

    clean_files = read_audio_list(CLEAN_LIST)
    noise_files = read_audio_list(NOISE_LIST)
    print(f"clean 文件数：{len(clean_files)}")
    print(f"noise 文件数：{len(noise_files)}")

    # 选择前 NUM_CLEAN 条干净语音，保证结果可复现。
    chosen_clean = clean_files[: min(NUM_CLEAN, len(clean_files))]

    detail_rows = []
    sample_id = 0

    for case_i, case in enumerate(TEST_CASES, start=1):
        noise_type = str(case["noise_type"])
        snr_value = float(case["input_snr_db"])
        noise_index = int(case.get("noise_index", case_i - 1)) % len(noise_files)
        noise_path = noise_files[noise_index]

        print(f"\n--- 测试场景 {case_i}: {noise_type}, 输入 SNR={snr_value} dB, noise={noise_path.name} ---")

        for clean_i, clean_path in enumerate(chosen_clean, start=1):
            sample_id += 1
            print(f"处理样本 {sample_id}: clean={clean_path.name}")

            clean_full = load_audio_mono(str(clean_path), sr, min_len=seg_len)
            noise_full = load_audio_mono(str(noise_path), sr, min_len=seg_len)
            clean = random_segment(clean_full, seg_len, rng)
            noise = random_segment(noise_full, seg_len, rng)
            noisy, clean_scaled, _ = mix_at_snr(clean, noise, snr_value, rng, speech_gain_db=0.0)

            traditional = spectral_subtract(noisy, sr)
            crn = crn_enhance(noisy, model, cfg, device)

            methods = {
                "Noisy": noisy,
                "Traditional": traditional,
                "CRN": crn,
            }

            base_name = f"case{case_i:02d}_snr{int(snr_value):+d}_sample{clean_i:02d}_{safe_stem(clean_path)}"

            if SAVE_AUDIO_EXAMPLES and clean_i == 1:
                save_wav(OUT_DIR / "audio" / f"{base_name}_clean.wav", clean_scaled, sr)
                save_wav(OUT_DIR / "audio" / f"{base_name}_noisy.wav", noisy, sr)
                save_wav(OUT_DIR / "audio" / f"{base_name}_traditional.wav", traditional, sr)
                save_wav(OUT_DIR / "audio" / f"{base_name}_crn.wav", crn, sr)

            if SAVE_FIGURE_EXAMPLES and clean_i == 1:
                save_compare_figure(
                    OUT_DIR / "figures" / f"{base_name}_wave_spectrum.png",
                    clean_scaled, noisy, traditional, crn, sr,
                    title=f"{noise_type}, input SNR={snr_value} dB"
                )

            for method_name, wav in methods.items():
                metric = calculate_metrics(clean_scaled, noisy, wav, sr, cfg)
                row = {
                    "sample_id": sample_id,
                    "noise_type": noise_type,
                    "input_snr_db": snr_value,
                    "method": method_name,
                    "clean_file": str(clean_path),
                    "noise_file": str(noise_path),
                    "snr_db": safe_float(metric["snr_db"]),
                    "delta_snr_db": safe_float(metric["delta_snr_db"]),
                    "si_snr_db": safe_float(metric["si_snr_db"]),
                    "stoi": safe_float(metric["stoi"]),
                    "pesq": safe_float(metric["pesq"]),
                    "band_mae_db": safe_float(metric["band_mae_db"]),
                }
                detail_rows.append(row)

    detail_fields = [
        "sample_id", "noise_type", "input_snr_db", "method", "clean_file", "noise_file",
        "snr_db", "delta_snr_db", "si_snr_db", "stoi", "pesq", "band_mae_db"
    ]
    write_csv(OUT_DIR / "detail_metrics.csv", detail_rows, detail_fields)

    summary_rows = summarize(detail_rows)
    summary_fields = list(summary_rows[0].keys()) if summary_rows else []
    write_csv(OUT_DIR / "summary_metrics.csv", summary_rows, summary_fields)

    table_rows = make_table_6_1(summary_rows)
    table_fields = list(table_rows[0].keys()) if table_rows else []
    write_csv(OUT_DIR / "table_6_1_for_paper.csv", table_rows, table_fields)

    print("\n===== 测试完成 =====")
    print(f"详细结果：{OUT_DIR / 'detail_metrics.csv'}")
    print(f"汇总结果：{OUT_DIR / 'summary_metrics.csv'}")
    print(f"论文表 6-1：{OUT_DIR / 'table_6_1_for_paper.csv'}")
    print(f"音频示例：{OUT_DIR / 'audio'}")
    print(f"对比图：{OUT_DIR / 'figures'}")
    print("\n注意：RNN 示例指标需要你先生成对应 RNN 输出 wav，本脚本无法仅凭训练代码自动得到 RNN 示例结果。")


if __name__ == "__main__":
    main()
