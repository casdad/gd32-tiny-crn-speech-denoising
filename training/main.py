"""
PyCharm 一键入口。

你只需要在本文件顶部修改路径，然后直接运行 main.py。
默认流程：生成 filelists -> 训练 smoke 模型 -> 导出 GD32 友好的 ONNX。

注意：CLEAN_ROOT 放 AISHELL-3-Sample 解压后的根目录；NOISE_ROOT 放空调/风扇/键盘等噪声 wav。
"""

from pathlib import Path
import subprocess
import sys


# ===================== 1. 在这里改你的路径 =====================

# 工程根目录：通常不用改，自动定位到 main.py 所在目录。
PROJECT_DIR = Path(__file__).resolve().parent

# AISHELL-3-Sample 解压后的目录。
# 例子：D:\datasets\AISHELL-3-Sample\AISHELL-3-Sample
CLEAN_ROOT = PROJECT_DIR / "data" / "clean"

# 室内噪声目录：里面放 fan/keyboard/air_conditioner/demand 等 wav/flac。
# 例子：D:\datasets\indoor_noise
NOISE_ROOT = PROJECT_DIR / "data" / "noise"

# 选择配置：先用 smoke 验证；完整训练改成 configs/tiny_crn_48k_light.yaml。
CONFIG_PATH = PROJECT_DIR / "configs" / "train.yaml"

# ==========================================================
# 训练输出基础名字。
#
# 第一次运行：runs/train
# 第二次运行：runs/train_1
# 第三次运行：runs/train_2
# ...
#
# 不要再用 RUN_NAME 固定路径，否则会覆盖旧实验。
# ==========================================================
BASE_RUN_NAME = "train"

# 可选：PC 端试听降噪。没有测试 wav 时保持 DO_EVAL = False。
EVAL_NOISY_WAV = PROJECT_DIR / "examples" / "noisy.wav"
EVAL_OUT_WAV = PROJECT_DIR / "examples" / "enhanced.wav"


# ===================== 2. 选择要执行的步骤 =====================

DO_BUILD_FILELISTS = True
DO_TRAIN = True
DO_EXPORT_ONNX = True
DO_MAKE_NPZ = True     # 需要 INT8 量化时再改 True
DO_EVAL = False        # 需要 PC 端试听时再改 True

# NPZ 校准样本数量。smoke 阶段 128 即可，完整量化可设 256/512。
NPZ_NUM_SAMPLES = 256


# ===================== 3. 下面一般不用改 =====================

def run(cmd):
    print("\n>>> " + " ".join(str(x) for x in cmd))
    subprocess.run(cmd, cwd=str(PROJECT_DIR), check=True)


def check_path(path: Path, name: str, should_exist: bool = True):
    if should_exist and not path.exists():
        raise FileNotFoundError(f"{name} 不存在：{path}")


def get_next_run_name(base_name: str):
    """
    自动生成不会覆盖旧实验的 run_name。

    如果 runs/train 不存在：
        返回 train

    如果 runs/train 已存在：
        返回 train_1

    如果 runs/train_1 也存在：
        返回 train_2
    """
    runs_dir = PROJECT_DIR / "runs"
    runs_dir.mkdir(parents=True, exist_ok=True)

    first_dir = runs_dir / base_name
    if not first_dir.exists():
        return base_name

    idx = 1
    while True:
        candidate = f"{base_name}_{idx}"
        candidate_dir = runs_dir / candidate

        if not candidate_dir.exists():
            return candidate

        idx += 1


def main():
    print("PROJECT_DIR =", PROJECT_DIR)
    print("CLEAN_ROOT  =", CLEAN_ROOT)
    print("NOISE_ROOT  =", NOISE_ROOT)
    print("CONFIG_PATH =", CONFIG_PATH)

    check_path(PROJECT_DIR, "PROJECT_DIR")
    check_path(CONFIG_PATH, "CONFIG_PATH")

    # ==========================================================
    # 关键：每次训练前自动生成一个新的实验名
    # ==========================================================
    run_name = get_next_run_name(BASE_RUN_NAME)

    ckpt_path = PROJECT_DIR / "runs" / run_name / "best.pt"
    onnx_out = PROJECT_DIR / "exports" / f"{run_name}_gd32.onnx"
    npz_out = PROJECT_DIR / "exports" / f"{run_name}_calib_input.npz"

    print("RUN_NAME    =", run_name)
    print("CKPT_PATH   =", ckpt_path)
    print("ONNX_OUT    =", onnx_out)
    print("NPZ_OUT     =", npz_out)

    if DO_BUILD_FILELISTS:
        check_path(CLEAN_ROOT, "CLEAN_ROOT")
        check_path(NOISE_ROOT, "NOISE_ROOT")

        run([
            sys.executable,
            "tools/build_filelists.py",
            "--clean_root", str(CLEAN_ROOT),
            "--noise_root", str(NOISE_ROOT),
            "--out_dir", "filelists",
        ])

    if DO_TRAIN:
        run([
            sys.executable,
            "train.py",
            "--config", str(CONFIG_PATH),
            "--run_name", run_name,
        ])

    if DO_EXPORT_ONNX:
        check_path(ckpt_path, "CKPT_PATH")

        run([
            sys.executable,
            "export_onnx.py",
            "--ckpt", str(ckpt_path),
            "--out", str(onnx_out),
        ])

    if DO_MAKE_NPZ:
        run([
            sys.executable,
            "make_npz_for_gd32.py",
            "--config", str(CONFIG_PATH),
            "--clean_list", str(PROJECT_DIR / "filelists" / "val_clean.txt"),
            "--noise_list", str(PROJECT_DIR / "filelists" / "val_noise.txt"),
            "--out", str(npz_out),
            "--num_samples", str(NPZ_NUM_SAMPLES),
        ])

    if DO_EVAL:
        check_path(EVAL_NOISY_WAV, "EVAL_NOISY_WAV")
        check_path(ckpt_path, "CKPT_PATH")

        run([
            sys.executable,
            "eval_denoise.py",
            "--ckpt", str(ckpt_path),
            "--wav", str(EVAL_NOISY_WAV),
            "--out", str(EVAL_OUT_WAV),
        ])

    print("\n完成。")
    print("RUN_NAME:", run_name)
    print("CKPT:", ckpt_path)

    if onnx_out.exists():
        print("ONNX:", onnx_out)

    if npz_out.exists():
        print("NPZ:", npz_out)


if __name__ == "__main__":
    main()
