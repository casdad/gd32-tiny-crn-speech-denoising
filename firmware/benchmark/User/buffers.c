/*!
    \file    buffers.c
    \brief   音频缓冲区定义文件
*/

#include "main.h"

/* 单声道数据缓冲区，32字节对齐，方便 DCache 和 DMA 操作 */
__attribute__((aligned(32))) uint16_t left_mic[SAMPLE_AUDIO_SIZE];
__attribute__((aligned(32))) uint16_t right_mic[SAMPLE_AUDIO_SIZE];
__attribute__((aligned(32))) uint16_t ref_audio[SAMPLE_AUDIO_SIZE];
__attribute__((aligned(32))) uint16_t null_audio[SAMPLE_AUDIO_SIZE];

/* TDM 采集双缓冲区：每 2 个 32 位数据保存 4 个 16 位通道 */
__attribute__((aligned(32))) uint32_t tdm_buffer[2][2 * SAMPLE_AUDIO_SIZE];

/*
 * 软件录音队列已迁移到 SDRAM 固定地址 0xC0000000，见 main.h 中 audio_rx_queue 宏。
 * 这里不再定义普通全局数组，避免启动阶段在 SDRAM 初始化前清零 .bss。
 */

/* 播放输出双缓冲区：每 2 个 32 位数据保存左右声道输出 */
__attribute__((aligned(32))) uint32_t output_buffer[2][2 * SAMPLE_AUDIO_SIZE];