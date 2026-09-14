# Tiny-CRN 轻量级边缘语音降噪模型代码说明

> GitHub 发布版说明：请先阅读仓库根目录中文 README。本文保留原工程使用说明；下文列出的 `filelists/*.txt`、校准 NPZ、`eval_results/`、`last.pt` 和批量训练图表不随仓库发布，需要自行生成。已保留 `runs/train/best.pt` 和 `exports/train_gd32.onnx`。正式训练请使用新的 `--run_name`，避免覆盖已有权重。板端实时性、量化参数和测试指标的限定以根目录 README 与 `docs/PROJECT_ANALYSIS.md` 为准。

本工程用于训练、测试和导出适配 GD32H759I 开发板部署的轻量级 CRN（Convolutional Recurrent Network）语音降噪模型。模型以 48 kHz 音频为输入，采用 STFT 频带特征、8 帧上下文和 32 维频带掩蔽输出，用于实现端侧语音增强、ONNX 导出、GD Embedded AI Tool 转换以及板端推理验证。

---

## 1. 工程整体结构

```text
Tiny-CRN/
├── configs/              # 训练配置文件
├── tinycrn/              # 核心模型、特征、数据集和可视化模块
├── tools/                # 辅助工具脚本
├── filelists/            # 训练、验证、测试音频路径列表
├── runs/                 # 训练输出结果与模型权重
├── exports/              # ONNX 模型和 GD32 量化校准数据
├── eval_results/         # 6.2 降噪效果测试输出结果
├── examples/             # 单文件推理示例输入/输出目录
├── main.py               # PyCharm 一键运行入口
├── train.py              # 模型训练主脚本
├── eval.py               # 多噪声、多信噪比降噪效果评估脚本
├── eval_denoise.py       # 单个 wav 文件离线降噪脚本
├── export_onnx.py        # 导出 GD32 友好的 ONNX 模型
├── make_npz_for_gd32.py  # 生成 GD Embedded AI Tool 量化校准 NPZ
└── requirements.txt      # Python 依赖列表
```

---

## 2. 主要文件夹说明

### 2.1 `configs/`

存放模型训练和特征提取参数配置文件。训练脚本会根据 YAML 文件读取采样率、STFT 参数、模型结构、训练轮数、batch size、学习率、数据列表路径等信息。

| 文件 | 作用 |
|---|---|
| `train.yaml` | 当前正式训练配置，采样率 48 kHz，窗长 960，帧移 480，32 维频带特征，8 帧上下文，训练 80 轮。 |
| `tiny_crn_48k_light.yaml` | 48 kHz 轻量训练配置，适合较完整训练但训练量小于正式配置。 |
| `tiny_crn_48k_smoke.yaml` | 48 kHz 快速冒烟测试配置，只训练少量 epoch，用于验证代码流程是否跑通。 |
| `tiny_crn_16k.yaml` | 16 kHz 配置示例，用于低采样率实验，不是当前 GD32H759I 板端部署的主配置。 |

---

### 2.2 `tinycrn/`

工程核心代码包，包含音频读取、在线混噪、特征提取、数据集构造、模型结构和训练可视化等模块。

| 文件 | 作用 |
|---|---|
| `__init__.py` | Python 包初始化文件，使 `tinycrn` 文件夹可被其他脚本导入。 |
| `audio.py` | 音频基础处理模块，包括单声道读取、重采样、随机截取、RMS 计算、按指定 SNR 混合干净语音与噪声、读取文件列表等函数。 |
| `dataset.py` | 在线混噪数据集模块。`OnTheFlyNoisyDataset` 会随机选取干净语音和噪声，按随机 SNR 混合，并生成训练所需的输入特征、理想掩蔽标签和频带能量。 |
| `features.py` | 特征提取模块，包括 STFT 幅度谱计算、频带滤波矩阵、频带特征归一化、理想掩蔽构造、频带掩蔽映射回频点等功能。 |
| `model.py` | TinyCRN 模型结构定义模块。模型由二维卷积、深度可分离卷积、逐点卷积、单层 LSTM 和全连接 Sigmoid 输出层组成，输出 32 维频带掩蔽。 |
| `visualize.py` | 训练过程可视化模块，用于保存训练损失曲线、频带误差曲线、波形图、频谱图和训练过程指标 CSV/TXT。 |

---

### 2.3 `tools/`

存放辅助脚本，不直接参与模型前向推理，但用于数据准备、实验对比和结果可视化。

| 文件 | 作用 |
|---|---|
| `build_filelists.py` | 扫描干净语音目录和噪声目录，自动生成训练、验证和测试文件列表，输出到 `filelists/`。 |
| `compare_runs.py` | 对比不同训练实验的 `train_log.csv`，绘制训练损失、验证损失和频带谱线对比图。 |
| `plot_wav_spectrum.py` | 对指定 noisy、clean、enhanced wav 文件绘制平均频谱对比图，用于论文或调试展示。 |

---

### 2.4 `filelists/`

存放训练、验证和测试所用音频路径列表。每个 TXT 文件每行对应一个音频文件路径。

| 文件 | 作用 |
|---|---|
| `train_clean.txt` | 训练集干净语音路径列表。 |
| `train_noise.txt` | 训练集噪声路径列表。 |
| `val_clean.txt` | 验证集干净语音路径列表。 |
| `val_noise.txt` | 验证集噪声路径列表。 |
| `test_clean.txt` | 测试集干净语音路径列表。 |
| `test_noise.txt` | 测试集噪声路径列表。 |

这些文件通常由 `tools/build_filelists.py` 或 `main.py` 自动生成。

---

### 2.5 `runs/`

存放模型训练输出结果。每次训练会在 `runs/` 下生成一个实验目录，例如 `runs/train/` 或 `runs/tiny_crn_48k_light/`。

| 文件/子目录 | 作用 |
|---|---|
| `best.pt` | 验证损失最优时保存的 PyTorch 模型权重，用于导出 ONNX 和离线推理。 |
| `last.pt` | 最后一轮训练后的模型权重，用于继续训练或检查最终状态。 |
| `train_log.csv` | 每轮训练损失和验证损失记录，可用于绘制训练曲线。 |
| `figures/` | 训练过程中自动生成的损失图、频带误差图、波形图和频谱图。 |

---

### 2.6 `exports/`

存放模型导出和量化校准相关文件。

| 文件 | 作用 |
|---|---|
| `train_gd32.onnx` | 由 `export_onnx.py` 导出的 ONNX 模型，用于导入 GD Embedded AI Tool。 |
| `train.npz` | 由 `make_npz_for_gd32.py` 生成的量化校准输入数据，用于 GD Embedded AI Tool 进行 int8 量化校准。 |

---

### 2.7 `eval_results/`

存放论文第 6.2 节降噪效果测试的输出结果。

| 文件/子目录 | 作用 |
|---|---|
| `detail_metrics.csv` | 每条测试样本的详细指标，包括 Noisy、传统方法、CRN 的 SNR、STOI、PESQ、频带 MAE 等。 |
| `summary_metrics.csv` | 按噪声类型和输入 SNR 汇总后的平均指标。 |
| `table_6_1_for_paper.csv` | 可直接用于论文表 6-1 的结果表。 |
| `audio/` | 保存测试过程中生成的 clean、noisy、traditional、crn wav 文件。 |
| `figures/` | 保存降噪前后波形与频谱对比图，可用于论文图 6-2。 |

---

### 2.8 `examples/`

用于存放单文件降噪测试的输入和输出音频。例如可以放入 `noisy.wav`，运行 `eval_denoise.py` 后生成 `enhanced.wav`。

---

### 2.9 `.idea/` 与 `__pycache__/`

| 文件夹 | 作用 |
|---|---|
| `.idea/` | PyCharm 工程配置文件夹，仅与本地 IDE 设置有关，不影响模型训练和推理。提交正式代码时可以保留，也可以删除。 |
| `__pycache__/` | Python 运行时生成的缓存文件夹，不影响源码功能，提交正式代码时可以删除。 |

---

## 3. 根目录主要脚本说明

### 3.1 `main.py`

PyCharm 一键运行入口。该脚本把数据列表生成、模型训练、ONNX 导出、NPZ 校准数据生成和单文件评估串联在一起。使用时只需要在文件顶部修改 `CLEAN_ROOT`、`NOISE_ROOT` 和 `CONFIG_PATH` 等路径，然后点击运行。

主要流程如下：

```text
生成 filelists → 训练模型 → 导出 ONNX → 生成 NPZ 校准数据 → 可选单文件降噪测试
```

脚本中的开关含义：

| 参数 | 作用 |
|---|---|
| `DO_BUILD_FILELISTS` | 是否重新生成训练/验证/测试文件列表。 |
| `DO_TRAIN` | 是否开始训练模型。 |
| `DO_EXPORT_ONNX` | 是否将 best.pt 导出为 ONNX。 |
| `DO_MAKE_NPZ` | 是否生成 GD32 量化校准用 NPZ。 |
| `DO_EVAL` | 是否对单个 noisy wav 文件进行离线降噪测试。 |

---

### 3.2 `train.py`

模型训练主程序。该脚本读取 `configs/*.yaml` 配置文件，构造 `OnTheFlyNoisyDataset` 数据集，训练 TinyCRN 模型，并保存训练日志和模型权重。

常用运行方式：

```bash
python train.py --config configs/train.yaml
```

如果要指定新的实验名：

```bash
python train.py --config configs/train.yaml --run_name train_1
```

输出结果默认保存到：

```text
runs/<run_name>/best.pt
runs/<run_name>/last.pt
runs/<run_name>/train_log.csv
runs/<run_name>/figures/
```

---

### 3.3 `eval.py`

多噪声、多信噪比降噪效果评估脚本，适合生成论文第 6.2 节所需的实验结果。脚本会从测试集文件列表中读取干净语音和噪声，按设定的输入 SNR 混合，分别计算 Noisy、传统谱减法和 CRN 模型的降噪指标，并保存 CSV 和对比图。

默认测试场景包括：

| 场景 | 输入 SNR |
|---|---:|
| 风扇/空调噪声 | -5 dB |
| 键盘敲击噪声 | 0 dB |
| 室内人声背景 | 5 dB |
| 环境混响/室内噪声 | 10 dB |

运行方式：

```bash
python eval.py
```

输出目录：

```text
eval_results/
```

---

### 3.4 `eval_denoise.py`

单个 wav 文件离线降噪脚本。该脚本加载训练好的 `best.pt`，读取指定的 noisy wav 文件，经过 TinyCRN 推理后输出增强语音。

运行方式：

```bash
python eval_denoise.py --ckpt runs/train/best.pt --wav examples/noisy.wav --out examples/enhanced.wav
```

该脚本适合快速试听模型效果，但不用于批量统计论文表 6-1。

---

### 3.5 `export_onnx.py`

模型导出脚本。该脚本将 PyTorch 的 `best.pt` 转换为 ONNX 模型，并通过 `GD32ExportWrapper` 将模型输入格式整理为适合 GD Embedded AI Tool 使用的形式。

运行方式：

```bash
python export_onnx.py --ckpt runs/train/best.pt --out exports/train_gd32.onnx
```

导出模型输入输出格式：

```text
ONNX 输入：[1, 1, 8, 32]  # NCHW
GD32/TFLite 期望输入：[1, 8, 32, 1]  # NHWC
输出：[1, 32]  # 32 维频带掩蔽
```

---

### 3.6 `make_npz_for_gd32.py`

GD Embedded AI Tool 量化校准数据生成脚本。该脚本从验证集语音和噪声中随机生成若干 8×32×1 的模型输入样本，并保存为 `.npz` 文件。

运行方式：

```bash
python make_npz_for_gd32.py \
  --config configs/train.yaml \
  --clean_list filelists/val_clean.txt \
  --noise_list filelists/val_noise.txt \
  --out exports/train.npz \
  --num_samples 256
```

生成的 `train.npz` 可导入 GD Embedded AI Tool，用于 int8 量化校准。

---

### 3.7 `requirements.txt`

Python 依赖说明文件。主要依赖包括：

```text
numpy
scipy
soundfile
tqdm
PyYAML
onnx
PyTorch
```

安装基础依赖：

```bash
pip install -r requirements.txt
```

PyTorch 建议根据电脑 CUDA 版本单独安装，例如：

```bash
pip install torch torchaudio --index-url https://download.pytorch.org/whl/cu121
```

如果需要计算 STOI 和 PESQ，可额外安装：

```bash
pip install pystoi pesq
```

---

## 4. 推荐使用流程

### 4.1 准备数据

准备两个数据目录：

```text
data/clean/   # 干净语音，例如 AISHELL-3
 data/noise/  # 噪声音频，例如风扇、空调、键盘、人声背景等
```

也可以在 `main.py` 中直接把 `CLEAN_ROOT` 和 `NOISE_ROOT` 改成自己的绝对路径。

---

### 4.2 生成文件列表

```bash
python tools/build_filelists.py --clean_root data/clean --noise_root data/noise --out_dir filelists
```

生成后会得到：

```text
filelists/train_clean.txt
filelists/train_noise.txt
filelists/val_clean.txt
filelists/val_noise.txt
filelists/test_clean.txt
filelists/test_noise.txt
```

---

### 4.3 训练模型

正式训练：

```bash
python train.py --config configs/train.yaml
```

快速测试训练流程：

```bash
python train.py --config configs/tiny_crn_48k_smoke.yaml
```

---

### 4.4 导出 ONNX

```bash
python export_onnx.py --ckpt runs/train/best.pt --out exports/train_gd32.onnx
```

---

### 4.5 生成 GD32 量化校准数据

```bash
python make_npz_for_gd32.py --config configs/train.yaml --out exports/train.npz --num_samples 256
```

---

### 4.6 单文件离线降噪

```bash
python eval_denoise.py --ckpt runs/train/best.pt --wav examples/noisy.wav --out examples/enhanced.wav
```

---

### 4.7 生成论文第 6.2 节测试结果

```bash
python eval.py
```

运行后主要查看：

```text
eval_results/table_6_1_for_paper.csv
eval_results/figures/
eval_results/audio/
```

---

## 5. 关键模型参数

当前主配置 `configs/train.yaml` 的核心参数如下：

| 参数 | 数值 | 含义 |
|---|---:|---|
| `sample_rate` | 48000 | 采样率 48 kHz，对应 GD32H759I 音频采集设置。 |
| `win_length` | 960 | STFT 窗长，约 20 ms。 |
| `hop_length` | 480 | 帧移，约 10 ms。 |
| `n_fft` | 1024 | FFT 点数。 |
| `n_bands` | 32 | 频带特征维度。 |
| `context_frames` | 8 | 模型输入上下文帧数，覆盖约 80 ms。 |
| `batch_size` | 48 | 训练批大小。 |
| `epochs` | 80 | 正式训练轮数。 |

模型输入输出：

```text
输入：[B, 8, 32, 1]
输出：[B, 32]
```

其中输出的 32 维结果表示频带掩蔽，后续会映射回 STFT 频点，用于对含噪语音幅度谱加权，并结合原始相位完成语音重建。

---

## 6. 与论文内容的对应关系

| 论文内容 | 代码或输出文件 |
|---|---|
| CRN 网络结构与参数量 | `tinycrn/model.py`，训练时控制台输出参数量，论文表 3-1。 |
| 训练参数 | `configs/train.yaml`，论文表 3-2。 |
| 训练损失曲线 | `runs/train/train_log.csv`，`runs/train/figures/`，论文图 3-3。 |
| 模型导出流程 | `export_onnx.py`，`exports/train_gd32.onnx`，论文图 3-6。 |
| GD32 量化校准数据 | `make_npz_for_gd32.py`，`exports/train.npz`。 |
| 降噪效果测试 | `eval.py`，`eval_results/table_6_1_for_paper.csv`，论文表 6-1。 |
| 波形与频谱对比图 | `eval_results/figures/`，论文图 6-2。 |
| 单文件降噪试听 | `eval_denoise.py`，`examples/enhanced.wav`。 |

---

## 7. 注意事项

1. `filelists/*.txt` 中的路径必须真实存在，否则训练和评估会报错。
2. 当前板端部署主流程对应 48 kHz、480 点帧移、10 ms 音频帧，不建议随意修改为 16 kHz 或其他帧移，否则训练端和板端特征会不一致。
3. `eval.py` 中的四类噪声场景可根据 `test_noise.txt` 中实际噪声文件顺序调整。
4. `runs/`、`exports/` 和 `eval_results/` 属于运行结果目录，重新训练或评估后会产生新的结果文件。
5. `.idea/` 和 `__pycache__/` 不是核心源码，正式提交代码时可删除，以减小工程体积。
6. 若需要导入 GD Embedded AI Tool，应优先使用 `exports/train_gd32.onnx` 和 `exports/train.npz`。
