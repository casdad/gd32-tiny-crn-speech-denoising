import argparse
from pathlib import Path

import numpy as np
import soundfile as sf
import torch

from tinycrn.audio import load_audio_mono
from tinycrn.features import band_matrix, feature_from_band, band_mask_to_bin_mask
from tinycrn.model import build_model_from_config


PROJECT_DIR = Path(__file__).resolve().parent
DEFAULT_RUN_NAME = "tiny_crn_48k_smoke"
DEFAULT_CKPT = PROJECT_DIR / "runs" / DEFAULT_RUN_NAME / "best.pt"
DEFAULT_WAV = PROJECT_DIR / "examples" / "noisy.wav"
DEFAULT_OUT = PROJECT_DIR / "examples" / "enhanced.wav"


def project_path(path_like):
    p = Path(path_like)
    return p if p.is_absolute() else PROJECT_DIR / p


def load_model(ckpt_path, device):
    ckpt_path = project_path(ckpt_path)
    ckpt = torch.load(str(ckpt_path), map_location=device)
    cfg = ckpt["cfg"]
    model = build_model_from_config(cfg).to(device)
    model.load_state_dict(ckpt["model"], strict=True)
    model.eval()
    return model, cfg


def enhance(wav, model, cfg, device):
    sr = int(cfg["sample_rate"])
    n_fft = int(cfg["n_fft"])
    win_length = int(cfg["win_length"])
    hop = int(cfg["hop_length"])
    n_bands = int(cfg["n_bands"])
    context = int(cfg["context_frames"])

    x = torch.from_numpy(wav).float().to(device).unsqueeze(0)
    window = torch.hann_window(win_length, device=device)
    spec = torch.stft(x, n_fft=n_fft, hop_length=hop, win_length=win_length,
                      window=window, center=True, return_complex=True)
    mag = spec.abs().transpose(1, 2).contiguous()  # [1,frames,bins]
    fb = band_matrix(sr, n_fft, n_bands, device=device, dtype=mag.dtype)
    band = torch.matmul(mag, fb.t())
    feat = feature_from_band(band).squeeze(0)  # [frames,bands]

    frames = feat.shape[0]
    masks = []
    hist = torch.zeros(context, n_bands, 1, device=device)
    with torch.no_grad():
        for t in range(frames):
            hist = torch.roll(hist, shifts=-1, dims=0)
            hist[-1, :, 0] = feat[t]
            if t < context - 1:
                hist[:context - 1 - t, :, 0] = feat[0]
            pred = model(hist.unsqueeze(0)).squeeze(0).detach().cpu().numpy()
            masks.append(pred)
    masks = np.stack(masks, axis=0).astype(np.float32)  # [frames,bands]

    bin_masks = []
    for t in range(frames):
        bin_masks.append(band_mask_to_bin_mask(masks[t], sr, n_fft, n_bands))
    bin_masks = torch.from_numpy(np.stack(bin_masks, axis=0)).float().to(device)  # [frames,bins]
    enhanced_spec = spec * bin_masks.t().unsqueeze(0)
    y = torch.istft(enhanced_spec, n_fft=n_fft, hop_length=hop, win_length=win_length,
                    window=window, center=True, length=len(wav))
    return y.squeeze(0).detach().cpu().numpy().astype(np.float32)


def eval_file(ckpt=DEFAULT_CKPT, wav_path=DEFAULT_WAV, out_path=DEFAULT_OUT):
    ckpt = project_path(ckpt)
    wav_path = project_path(wav_path)
    out_path = project_path(out_path)
    if not ckpt.exists():
        raise FileNotFoundError(f"checkpoint not found: {ckpt}")
    if not wav_path.exists():
        raise FileNotFoundError(f"wav not found: {wav_path}")

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model, cfg = load_model(ckpt, device)
    sr = int(cfg["sample_rate"])
    wav = load_audio_mono(str(wav_path), sr)
    y = enhance(wav, model, cfg, device)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(out_path), y, sr)
    print(f"wrote {out_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=str(DEFAULT_CKPT))
    ap.add_argument("--wav", default=str(DEFAULT_WAV))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    args = ap.parse_args()
    eval_file(args.ckpt, args.wav, args.out)


if __name__ == "__main__":
    main()
