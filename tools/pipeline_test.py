from pathlib import Path
import sys

import numpy as np
import onnx
import onnxruntime as ort
import soundfile as sf
import torch
import yaml


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "training"
if len(sys.argv) != 2:
    raise SystemExit("Usage: python tools/pipeline_test.py NEW_OUTPUT_DIRECTORY")
OUT = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(SOURCE))

from tools.build_filelists import build_filelists
from train import train_from_config
from export_onnx import export_onnx
from make_npz_for_gd32 import make_npz
from eval_denoise import eval_file
from tinycrn.model import build_model_from_config


def main():
    if OUT.exists():
        raise RuntimeError("Validation output already exists; refusing to overwrite")
    torch.set_num_threads(2)
    rng = np.random.default_rng(2026)
    for name in ("clean", "noise"):
        (OUT / name).mkdir(parents=True)
    t = np.arange(48000, dtype=np.float32) / 48000
    for i in range(20):
        clean = 0.15 * np.sin(2 * np.pi * (220 + 17 * i) * t)
        sf.write(OUT / "clean" / f"{i:02}.wav", clean, 48000)
        sf.write(OUT / "noise" / f"{i:02}.wav", rng.normal(0, 0.03, t.size), 48000)
    build_filelists(OUT / "clean", OUT / "noise", OUT / "filelists")
    cfg = yaml.safe_load((SOURCE / "configs/train.yaml").read_text(encoding="utf-8"))
    cfg["run_name"] = "pipeline_test"
    cfg["segment_seconds"] = 0.2
    cfg["train"].update(epochs=1, steps_per_epoch=4, val_steps=2, batch_size=2, num_workers=0, amp=False)
    cfg["visualize"]["enabled"] = False
    cfg["paths"] = {
        key: str(OUT / "filelists" / filename) for key, filename in [
            ("train_clean_list", "train_clean.txt"), ("train_noise_list", "train_noise.txt"),
            ("val_clean_list", "val_clean.txt"), ("val_noise_list", "val_noise.txt"),
        ]
    }
    cfg["paths"]["out_dir"] = str(OUT / "runs")
    config = OUT / "test.yaml"
    config.write_text(yaml.safe_dump(cfg), encoding="utf-8")
    train_from_config(config)
    checkpoint = OUT / "runs/pipeline_test/best.pt"
    export_onnx(checkpoint, OUT / "test.onnx")
    onnx.checker.check_model(onnx.load(str(OUT / "test.onnx")))
    make_npz(config, OUT / "filelists/val_clean.txt", OUT / "filelists/val_noise.txt", OUT / "test.npz", 4)
    assert np.load(OUT / "test.npz")["data"].shape == (4, 8, 32, 1)
    eval_file(checkpoint, OUT / "noise/00.wav", OUT / "enhanced.wav")
    output, sr = sf.read(OUT / "enhanced.wav")
    assert sr == 48000 and output.shape == (48000,) and np.isfinite(output).all()

    original = torch.load(SOURCE / "runs/train/best.pt", map_location="cpu", weights_only=True)
    model = build_model_from_config(original["cfg"]).eval()
    model.load_state_dict(original["model"])
    session = ort.InferenceSession(str(SOURCE / "exports/train_gd32.onnx"), providers=["CPUExecutionProvider"])
    maximum = 0.0
    for _ in range(10):
        x = rng.uniform(0, 8, (1, 8, 32, 1)).astype(np.float32)
        with torch.no_grad():
            expected = model(torch.from_numpy(x)).numpy()
        actual = session.run(None, {session.get_inputs()[0].name: x.transpose(0, 3, 1, 2)})[0]
        maximum = max(maximum, float(np.max(np.abs(expected - actual))))
        np.testing.assert_allclose(actual, expected, atol=1e-5, rtol=1e-4)
    print(f"PASS: synthetic data -> filelists -> training -> checkpoint -> ONNX -> NPZ -> WAV; original ONNX/PyTorch max_abs_error={maximum:.9g}")
    print("This does not validate MCU quantization or real denoising quality.")


if __name__ == "__main__":
    main()
