# 数据路径列表

本目录的六个 TXT 列表由 `tools/build_filelists.py` 生成，不提交原机器的绝对路径。每行是一条本机可访问的音频路径。

从 `training` 目录运行：

```bash
python tools/build_filelists.py --clean_root data/clean --noise_root data/noise --out_dir filelists
```

确认 clean / noise 的 train、val、test 列表均非空。按文件随机切分不保证说话人和录音来源隔离，正式实验应自行制定无泄漏划分。
