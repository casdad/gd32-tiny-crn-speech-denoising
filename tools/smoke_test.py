"""CPU-only model, feature, reconstruction and ONNX structural checks."""

from pathlib import Path
import sys

import numpy as np
import onnx
import torch
import yaml


ROOT = Path(__file__).resolve().parents[1]
TRAINING = ROOT / "training"
sys.path.insert(0, str(TRAINING))

from eval_denoise import enhance
from export_onnx import GD32ExportWrapper
from tinycrn.features import wav_to_training_tensors
from tinycrn.model import build_model_from_config


def main():
    torch.manual_seed(1234)
    torch.set_num_threads(2)
    checkpoint = torch.load(TRAINING / "runs/train/best.pt", map_location="cpu", weights_only=True)
    cfg = checkpoint["cfg"]
    model = build_model_from_config(cfg)
    model.load_state_dict(checkpoint["model"], strict=True)
    model.eval()
    assert sum(p.numel() for p in model.parameters()) == 71248
    x = torch.randn(2, 8, 32, 1)
    with torch.no_grad():
        masks = model(x)
        wrapped = GD32ExportWrapper(model)(x.permute(0, 3, 1, 2).contiguous())
    assert masks.shape == (2, 32)
    assert torch.isfinite(masks).all() and ((masks >= 0) & (masks <= 1)).all()
    torch.testing.assert_close(masks, wrapped)

    sr = cfg["sample_rate"]
    t = np.arange(sr // 5, dtype=np.float32) / sr
    clean = (0.15 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
    noise = np.random.default_rng(1234).normal(0, 0.025, len(t)).astype(np.float32)
    noisy = clean + noise
    features, target, noisy_band, clean_band = wav_to_training_tensors(
        torch.from_numpy(noisy)[None], torch.from_numpy(clean)[None],
        sample_rate=sr, n_fft=cfg["n_fft"], win_length=cfg["win_length"],
        hop_length=cfg["hop_length"], n_bands=cfg["n_bands"],
    )
    assert features.shape[-1] == 32 and torch.isfinite(features).all()
    enhanced = enhance(noisy, model, cfg, torch.device("cpu"))
    assert enhanced.shape == noisy.shape and np.isfinite(enhanced).all()

    model.train()
    optimizer = torch.optim.AdamW(model.parameters(), lr=0.001)
    prediction = model(features[:, :8, :, None])
    loss = torch.nn.functional.l1_loss(prediction, target[:, 7])
    loss += 0.5 * torch.nn.functional.l1_loss(
        torch.log1p(10 * prediction * noisy_band[:, 7]),
        torch.log1p(10 * clean_band[:, 7]),
    )
    optimizer.zero_grad()
    loss.backward()
    assert torch.isfinite(loss) and all(p.grad is None or torch.isfinite(p.grad).all() for p in model.parameters())
    optimizer.step()

    graph = onnx.load(str(TRAINING / "exports/train_gd32.onnx"))
    onnx.checker.check_model(graph)
    shape = lambda value: [dim.dim_value for dim in value.type.tensor_type.shape.dim]
    assert shape(graph.graph.input[0]) == [1, 1, 8, 32]
    assert shape(graph.graph.output[0]) == [1, 32]
    assert any(op.domain == "" and op.version == 13 for op in graph.opset_import)
    for path in (TRAINING / "configs").glob("*.yaml"):
        candidate = yaml.safe_load(path.read_text(encoding="utf-8"))
        variant = build_model_from_config(candidate).eval()
        with torch.no_grad():
            result = variant(torch.zeros(1, candidate["context_frames"], candidate["n_bands"], 1))
        assert result.shape == (1, candidate["n_bands"])
    print(f"PASS: 71,248 parameters; checkpoint epoch={checkpoint['epoch']}; "
          "forward, wrapper, features, audio reconstruction, backward/step, "
          "ONNX structure and all configuration shapes")
    print("No files were modified; synthetic audio does not measure denoising quality.")


if __name__ == "__main__":
    main()
