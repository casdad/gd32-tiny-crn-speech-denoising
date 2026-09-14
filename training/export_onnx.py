import argparse
from pathlib import Path

import torch
import torch.nn as nn

from tinycrn.model import build_model_from_config


PROJECT_DIR = Path(__file__).resolve().parent
DEFAULT_RUN_NAME = "train"
DEFAULT_CKPT = PROJECT_DIR / "runs" / DEFAULT_RUN_NAME / "best.pt"
DEFAULT_OUT = PROJECT_DIR / "exports" / f"{DEFAULT_RUN_NAME}_gd32.onnx"


def project_path(path_like):
    p = Path(path_like)
    return p if p.is_absolute() else PROJECT_DIR / p


class GD32ExportWrapper(nn.Module):
    """Export wrapper for GD32 Embedded AI.

    ONNX input: [B, 1, T, F]  (NCHW)
    Expected TFLite input after GD32 conversion: [B, T, F, 1] (NHWC)

    The original TinyCRN model uses [B, T, F, 1], so the wrapper converts
    NCHW -> NHWC before calling the model.
    """

    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, x_nchw):
        # x_nchw: [B,1,T,F]
        x_nhwc = x_nchw.permute(0, 2, 3, 1).contiguous()
        return self.model(x_nhwc)


def export_onnx(ckpt_path=DEFAULT_CKPT, out_path=DEFAULT_OUT):
    ckpt_path = project_path(ckpt_path)
    out_path = project_path(out_path)
    if not ckpt_path.exists():
        raise FileNotFoundError(f"checkpoint not found: {ckpt_path}")

    ckpt = torch.load(str(ckpt_path), map_location="cpu")
    cfg = ckpt["cfg"]
    model = build_model_from_config(cfg)
    model.load_state_dict(ckpt["model"], strict=True)
    model.eval()

    wrapper = GD32ExportWrapper(model)
    wrapper.eval()

    t = int(cfg["context_frames"])
    f = int(cfg["n_bands"])
    dummy = torch.randn(1, 1, t, f, dtype=torch.float32)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        wrapper,
        dummy,
        str(out_path),
        input_names=["input"],
        output_names=["mask"],
        opset_version=13,
        do_constant_folding=True,
        dynamic_axes=None,
    )

    print("exported:", out_path)
    print(f"ONNX input shape: [1, 1, {t}, {f}]  # NCHW")
    print(f"Expected GD32/TFLite input shape: [1, {t}, {f}, 1]  # NHWC")
    print(f"Output shape: [1, {f}]")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=str(DEFAULT_CKPT), help="best.pt 路径；不填时使用代码里的 DEFAULT_CKPT")
    ap.add_argument("--out", default=str(DEFAULT_OUT), help="ONNX 输出路径；不填时使用代码里的 DEFAULT_OUT")
    args = ap.parse_args()
    export_onnx(args.ckpt, args.out)


if __name__ == "__main__":
    main()
