# 提交材料与仓库对应关系

## 来源选择

使用原始提交材料顶层 `代码` 目录作为来源。与比赛命名子目录中的同名代码副本比较了 933 个筛选后文件，未发现差异。此处不发布本机绝对路径。

| 原材料相对位置 | 仓库位置 | 处理 |
| --- | --- | --- |
| 开发板代码 / GD32H779I | `firmware/app` | 保留源码、驱动、库和 Keil 项目 |
| 开发板数据测试代码 / GD32H779I | `firmware/benchmark` | 保留独立性能测试工程 |
| 模型训练代码(含数据测试) / Tiny-CRN | `training` | 保留全部 Python 源码、YAML 配置与说明 |
| Tiny-CRN / runs / train / best.pt | `training/runs/train/best.pt` | 原始字节复制 |
| Tiny-CRN / exports / train_gd32.onnx | `training/exports/train_gd32.onnx` | 原始字节复制 |
| Tiny-CRN / runs / train / train_log.csv | `docs/results/train_log.csv` | 原始训练记录 |
| Tiny-CRN / runs / train / figures / loss_curve.png | `docs/results/loss_curve.png` | 原始训练曲线 |
| Tiny-CRN / eval_results / table_6_1_for_paper.csv | `docs/results/evaluation_summary.csv` | 原始 PC 离线评估汇总 |

GD AI 工具的完整模板目录未重复上传，部署所需的生成模型、头文件和 Cortex-M7 AI 库已经随两个固件工程保留。

## 原样保留与人工修改

固件 `.c/.h/.s/.ld/.lib/.uvprojx` 按原始字节保留；新增两个工程原本缺失的 `readme.txt`。训练源码仅修改 `main.py` 的三个本机路径。训练依赖增加 `matplotlib`，原训练 README 顶部增加发布版说明。

根目录中文 README、分析文档、验证脚本、忽略规则和发布说明为本次新增。

`artifacts.sha256.json` 记录模型和运行库的 SHA-256，用于识别本次发布的原始产物，不把不同格式模型的不同 hash 当作数值不一致证明。

## 未发布内容

编译输出、IDE 用户配置、缓存、绝对路径 TXT 列表、原始音频、校准 NPZ、last.pt、每轮图表、每轮波形 CSV、论文、照片、视频和完整参赛压缩包均未上传。未删除或修改原始提交材料。
