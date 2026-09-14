# 发布前验证记录

验证日期：2026-09-14。以下区分静态检查、PC 功能测试和未执行的硬件验证，不将合成音频测试当作降噪质量评估。

## 环境

Windows，Python 3.12.14，独立 CPU 虚拟环境。实际安装版本：

| 依赖 | 版本 |
| --- | --- |
| PyTorch | 2.5.1+cpu |
| NumPy | 2.5.3 |
| SciPy | 1.18.1 |
| SoundFile | 0.14.0 |
| ONNX | 1.22.0 |
| ONNX Runtime | 1.30.0 |
| Matplotlib | 3.11.2 |
| PyYAML | 6.0.3 |

没有声称这是原作者训练环境，也没有声称支持任意最新依赖组合。安装时原本配置的镜像发生 TLS 连接错误，改用 PyPI 官方源成功安装；未关闭证书验证。

## 静态与完整性检查：通过

- 两个 Keil 项目选择的器件均为 `GD32H759IM`。
- 全部 179 个项目文件引用及非空 include 路径存在。
- 原样复制的 426 个固件文件 SHA-256 与提交材料一致。
- 除 `main.py` 路径整理外，18 个训练 Python / YAML 文件与原材料逐字节一致；Git 文本换行归一化可能改变检出后的换行字节，不改变代码内容。
- Python AST 语法检查通过。
- 原权重、原 ONNX、两份 C 模型和两份 AI 库共六项产物的 hash 检查通过。
- 必需 `.lib`、`best.pt` 和 ONNX 未被 `.gitignore` 排除；编译输出、数据列表和校准 NPZ 被排除。
- 在待发布文本中扫描常见 token、私钥标记和原机器用户路径，未发现命中。这不是对所有可能敏感内容的形式化证明。

复现命令（仓库根目录）：

```bash
python tools/verify_repository.py
```

## PC 模型冒烟测试：通过

```bash
python tools/smoke_test.py
```

- 安全加载原始 checkpoint（`weights_only=True`），严格匹配模型键；checkpoint 记录 epoch 为 **49**。
- 参数量 **71,248**，输入 `[2,8,32,1]`，输出 `[2,32]`，数值有限且在 `[0,1]`。
- PyTorch 原模型与 NCHW 导出包装器输出一致。
- 48 kHz 合成语音的特征提取、离线重建通过；输出长度与输入一致。
- 单次损失计算、反向传播和 AdamW 更新通过。
- 原 ONNX checker、固定输入输出形状和 opset 13 检查通过。
- 四个 YAML 配置均可构造模型并完成相应形状的前向计算。

测试不覆盖原权重，不将任何模型或生成音频保存回发布目录。

## 最小端到端管线：通过

安装额外验证依赖，并指定一个尚不存在的输出目录：

```bash
python -m pip install -r training/requirements-validation.txt
python tools/pipeline_test.py ../pipeline-validation-new
```

脚本只生成自己的合成数据，不访问真实训练集；拒绝覆盖已经存在的目录。精确设置为：

- RNG seed 2026，20 个 1 秒正弦信号文件 + 20 个 1 秒高斯噪声文件，48 kHz。
- 使用原列表生成器，每类得到 18 / 1 / 1 的 train / val / test 划分。
- 使用正式配置的网络结构；测试覆盖配置为 1 epoch、4 个训练样本、2 个验证样本、batch=2、segment=0.2 秒、workers=0、AMP 关闭、可视化关闭。
- 调用原 `train_from_config` 生成 checkpoint，再调用原 `export_onnx` 导出并检查新 ONNX。
- 调用原 `make_npz`，得到 `data: float32 [4,8,32,1]`。
- 调用原 `eval_file` 写 WAV，确认 48 kHz、48,000 个样本和有限数值。
- 用 10 组 `uniform(0,8)` 输入比较**原始** PyTorch 权重与**原始** ONNX，绝对最大误差为 **8.34465027e-7**，通过 `atol=1e-5, rtol=1e-4` 检查。

最后一项只证明上述输入上的 PC 浮点 PyTorch / ONNX 一致性，**不包含 INT8 转换产物或 MCU**。最小训练未固定所有训练随机状态，复跑损失不要求完全相同。

测试期间出现原代码 AMP 接口弃用、checkpoint 默认加载行为及 LSTM 导出的警告，但上述验证均正常完成；新运行环境或升级依赖时应重新测试。

## 未执行

- 原始语音数据集上的完整 80 epoch 训练和统计复现实验。
- STOI/PESQ 实际评分及重新生成真实噪声效果表。
- GD Embedded AI Tool 转换、精确 INT8 scale/zero_point 对齐和 MCU 数值回归。
- Keil 编译、下载器连接、固件烧录、实际录音及板端性能测量。

仓库中的既有训练曲线与 SNR 表仅作为原始实验记录保留，不能替代以上未执行项目。
