# 单文件推理

自行放入 `noisy.wav`。不发布个人录音或原数据集音频。

从 `training` 目录运行：

```bash
python eval_denoise.py --ckpt runs/train/best.pt --wav examples/noisy.wav --out examples/enhanced.wav
```
