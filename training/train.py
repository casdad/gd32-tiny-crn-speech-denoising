import argparse
import csv
import sys
from pathlib import Path

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader
from torch.cuda.amp import autocast, GradScaler
import yaml
from tqdm import tqdm

from tinycrn.dataset import OnTheFlyNoisyDataset
from tinycrn.model import build_model_from_config
from tinycrn.visualize import TrainingVisualizer


PROJECT_DIR = Path(__file__).resolve().parent
DEFAULT_CONFIG = PROJECT_DIR / "configs" / "train.yaml"


def project_path(path_like):
    p = Path(path_like)
    return p if p.is_absolute() else PROJECT_DIR / p


def load_cfg(path):
    path = project_path(path)
    with open(path, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def save_ckpt(path, model, optimizer, scaler, cfg, epoch, val_loss):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save({
        "model": model.state_dict(),
        "optimizer": optimizer.state_dict(),
        "scaler": None if scaler is None else scaler.state_dict(),
        "cfg": cfg,
        "epoch": epoch,
        "val_loss": val_loss,
    }, path)


def run_epoch(model, loader, optimizer, scaler, device, cfg, train=True):
    model.train(train)

    total = 0.0
    n = 0

    mask_w = float(cfg["train"].get("mask_loss_weight", 1.0))
    mag_w = float(cfg["train"].get("mag_loss_weight", 0.5))
    amp = bool(cfg["train"].get("amp", True)) and device.type == "cuda"
    grad_clip = float(cfg["train"].get("grad_clip", 0.0))

    pbar = tqdm(loader, leave=False)

    for batch in pbar:
        x = batch["x"].to(device, non_blocking=True)
        y_mask = batch["mask"].to(device, non_blocking=True)
        clean_band = batch["clean_band"].to(device, non_blocking=True)
        noisy_band = batch["noisy_band"].to(device, non_blocking=True)

        with torch.set_grad_enabled(train):
            with autocast(enabled=amp):
                pred = model(x)

                loss_mask = F.l1_loss(pred, y_mask)

                enhanced_band = pred * noisy_band
                loss_mag = F.l1_loss(
                    torch.log1p(10.0 * enhanced_band),
                    torch.log1p(10.0 * clean_band)
                )

                loss = mask_w * loss_mask + mag_w * loss_mag

            if train:
                optimizer.zero_grad(set_to_none=True)

                if scaler is not None and amp:
                    scaler.scale(loss).backward()

                    if grad_clip > 0:
                        scaler.unscale_(optimizer)
                        torch.nn.utils.clip_grad_norm_(model.parameters(), grad_clip)

                    scaler.step(optimizer)
                    scaler.update()

                else:
                    loss.backward()

                    if grad_clip > 0:
                        torch.nn.utils.clip_grad_norm_(model.parameters(), grad_clip)

                    optimizer.step()

        bs = x.size(0)
        total += float(loss.detach().cpu()) * bs
        n += bs

        pbar.set_description(f"loss={total / max(1, n):.5f}")

    return total / max(1, n)


def train_from_config(config_path=DEFAULT_CONFIG, run_name_override=None):
    cfg = load_cfg(config_path)

    # ==========================================================
    # 新增：允许 main.py 通过 --run_name 覆盖 yaml 里的 run_name
    # 这样不会再固定保存到 tiny_crn，也不会覆盖旧数据
    # ==========================================================
    if run_name_override is not None and str(run_name_override).strip() != "":
        cfg["run_name"] = str(run_name_override).strip()

    torch.manual_seed(int(cfg.get("seed", 1234)))

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    out_dir = project_path(cfg["paths"].get("out_dir", "runs")) / cfg.get("run_name", "tiny_crn")
    out_dir.mkdir(parents=True, exist_ok=True)

    train_clean_list = project_path(cfg["paths"]["train_clean_list"])
    train_noise_list = project_path(cfg["paths"]["train_noise_list"])
    val_clean_list = project_path(cfg["paths"]["val_clean_list"])
    val_noise_list = project_path(cfg["paths"]["val_noise_list"])

    print("config:", project_path(config_path))
    print("device:", device)
    print("run_name:", cfg.get("run_name", "tiny_crn"))
    print("out_dir:", out_dir)
    print("train_clean_list:", train_clean_list)
    print("train_noise_list:", train_noise_list)

    model = build_model_from_config(cfg).to(device)

    n_params = sum(p.numel() for p in model.parameters())
    print(f"model params: {n_params:,}  float32 bytes≈{n_params * 4:,}  int8 bytes≈{n_params:,}")
    print(f"model input:  [B,{cfg['context_frames']},{cfg['n_bands']},1]")
    print(f"model output: [B,{cfg['n_bands']}]")

    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=float(cfg["train"]["lr"]),
        weight_decay=float(cfg["train"].get("weight_decay", 0.0))
    )

    scaler = GradScaler(
        enabled=bool(cfg["train"].get("amp", True)) and device.type == "cuda"
    )

    train_ds = OnTheFlyNoisyDataset(
        str(train_clean_list),
        str(train_noise_list),
        cfg,
        steps=int(cfg["train"]["steps_per_epoch"]),
        seed=int(cfg.get("seed", 1234))
    )

    val_ds = OnTheFlyNoisyDataset(
        str(val_clean_list),
        str(val_noise_list),
        cfg,
        steps=int(cfg["train"].get("val_steps", 200)),
        seed=int(cfg.get("seed", 1234)) + 999
    )

    train_loader = DataLoader(
        train_ds,
        batch_size=int(cfg["train"]["batch_size"]),
        shuffle=False,
        num_workers=int(cfg["train"].get("num_workers", 4)),
        pin_memory=True,
        drop_last=True
    )

    val_loader = DataLoader(
        val_ds,
        batch_size=int(cfg["train"]["batch_size"]),
        shuffle=False,
        num_workers=int(cfg["train"].get("num_workers", 4)),
        pin_memory=True,
        drop_last=False
    )

    # 你原来的可视化逻辑保留
    visualizer = TrainingVisualizer(cfg=cfg, out_dir=out_dir, device=device)

    best = float("inf")
    log_path = out_dir / "train_log.csv"

    with log_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["epoch", "train_loss", "val_loss"])

        for epoch in range(1, int(cfg["train"]["epochs"]) + 1):
            print(f"Epoch {epoch} / {cfg['train']['epochs']} on {device}")

            train_loss = run_epoch(
                model,
                train_loader,
                optimizer,
                scaler,
                device,
                cfg,
                train=True
            )

            val_loss = run_epoch(
                model,
                val_loader,
                optimizer,
                scaler,
                device,
                cfg,
                train=False
            )

            print(f"train={train_loss:.6f} val={val_loss:.6f}")

            writer.writerow([epoch, train_loss, val_loss])
            f.flush()

            # 你原来的可视化更新逻辑保留
            visualizer.update(epoch, train_loss, val_loss, model, val_ds)

            save_ckpt(
                out_dir / "last.pt",
                model,
                optimizer,
                scaler,
                cfg,
                epoch,
                val_loss
            )

            if val_loss < best:
                best = val_loss

                save_ckpt(
                    out_dir / "best.pt",
                    model,
                    optimizer,
                    scaler,
                    cfg,
                    epoch,
                    val_loss
                )

                print(f"saved best: {best:.6f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--config",
        default=str(DEFAULT_CONFIG),
        help="配置文件路径；不填时使用代码里的 DEFAULT_CONFIG"
    )

    # ==========================================================
    # 新增：main.py 会传这个参数
    # ==========================================================
    ap.add_argument(
        "--run_name",
        default=None,
        help="覆盖配置文件里的 run_name，例如 train、train_1、train_2"
    )

    args = ap.parse_args()

    train_from_config(
        config_path=args.config,
        run_name_override=args.run_name
    )


if __name__ == "__main__":
    main()