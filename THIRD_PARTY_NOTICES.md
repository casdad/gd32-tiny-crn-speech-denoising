# 第三方组件说明

本项目包含原提交工程随附的第三方源码和运行库，原文件中的版权、许可条件和免责声明均予保留。不同组件不因收录于同一仓库而自动适用同一许可证。

| 组件 | 位置（两个固件工程均含） | 说明 |
| --- | --- | --- |
| GigaDevice GD32 驱动 | `firmware/*/Drivers` | 保留各源文件的 GigaDevice 版权与许可条件 |
| GD Embedded AI | `firmware/*/Middlewares/AI`、生成模型文件 | 保留库与头文件；发行和使用应遵循对应 SDK / 工具的适用条款 |
| Arm CMSIS / CMSIS-NN | CMSIS 目录及 AI 头文件 | 保留 Apache-2.0 标识和原始声明 |
| FatFs | `firmware/*/Middlewares/FATFS` | 保留 ChaN 的版权和分发条件 |
| RNNoise / 相关音频代码 | `firmware/*/RNNoise_src` | 保留 Mozilla、Gregor Richards 及各文件作者的原始声明 |

CMSIS 所引用的 Apache-2.0 正文见 `licenses/Apache-2.0.txt`。其他组件的具体条款以文件声明和随附 SDK 授权为准；不能将一个头文件的许可推定覆盖所有二进制组件。

本次发布没有替作者新增统一的 MIT / Apache 项目许可证，也没有改变第三方授权。数据集未随代码发布，使用者需自行获得相应数据许可。
