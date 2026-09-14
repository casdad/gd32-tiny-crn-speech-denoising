import argparse
import random
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parents[1]

# 直接在这里改路径也可以；更推荐改工程根目录 main.py 里的 CLEAN_ROOT / NOISE_ROOT。
DEFAULT_CLEAN_ROOT = PROJECT_DIR / "data" / "clean" / "aishell3_sample"
DEFAULT_NOISE_ROOT = PROJECT_DIR / "data" / "noise"
DEFAULT_OUT_DIR = PROJECT_DIR / "filelists"

AUDIO_EXTS = {".wav", ".flac", ".ogg", ".mp3", ".m4a"}


def project_path(path_like):
    p = Path(path_like)
    return p if p.is_absolute() else PROJECT_DIR / p


def find_audio(root: Path):
    root = Path(root)
    return sorted(str(p) for p in root.rglob("*") if p.suffix.lower() in AUDIO_EXTS)


def split_list(items, seed=1234, train=0.9, val=0.05):
    items = list(items)
    rng = random.Random(seed)
    rng.shuffle(items)
    n = len(items)
    n_train = int(n * train)
    n_val = int(n * val)
    return items[:n_train], items[n_train:n_train+n_val], items[n_train+n_val:]


def write_list(path: Path, items):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(items) + ("\n" if items else ""), encoding="utf-8")


def build_filelists(clean_root=DEFAULT_CLEAN_ROOT, noise_root=DEFAULT_NOISE_ROOT,
                    out_dir=DEFAULT_OUT_DIR, seed=1234):
    clean_root = project_path(clean_root)
    noise_root = project_path(noise_root)
    out_dir = project_path(out_dir)

    out_dir.mkdir(parents=True, exist_ok=True)
    clean = find_audio(clean_root)
    noise = find_audio(noise_root)
    if not clean:
        raise SystemExit(f"No clean audio found in {clean_root}")
    if not noise:
        raise SystemExit(f"No noise audio found in {noise_root}")

    tr_c, va_c, te_c = split_list(clean, seed)
    tr_n, va_n, te_n = split_list(noise, seed + 1)
    write_list(out_dir / "train_clean.txt", tr_c)
    write_list(out_dir / "val_clean.txt", va_c)
    write_list(out_dir / "test_clean.txt", te_c)
    write_list(out_dir / "train_noise.txt", tr_n)
    write_list(out_dir / "val_noise.txt", va_n)
    write_list(out_dir / "test_noise.txt", te_n)
    print(f"clean_root: {clean_root}")
    print(f"noise_root: {noise_root}")
    print(f"out_dir: {out_dir}")
    print(f"clean: train={len(tr_c)} val={len(va_c)} test={len(te_c)}")
    print(f"noise: train={len(tr_n)} val={len(va_n)} test={len(te_n)}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean_root", default=str(DEFAULT_CLEAN_ROOT))
    ap.add_argument("--noise_root", default=str(DEFAULT_NOISE_ROOT))
    ap.add_argument("--out_dir", default=str(DEFAULT_OUT_DIR))
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()
    build_filelists(args.clean_root, args.noise_root, args.out_dir, args.seed)


if __name__ == "__main__":
    main()
