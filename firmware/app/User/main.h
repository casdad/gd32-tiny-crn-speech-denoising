/*!
    \file    main.h
    \brief   主程序头文件

    \version 2025-07-12, V1.0.0, 针对GD32H7xx系列
*/

/*
    Copyright (c) 2025, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

/* 定义采样点数 */
#define SAMPLE_AUDIO_SIZE       480  /* 10ms @ 48kHz; RNNoise also uses 480 samples */

/*
 * RX DMA software queue.
 * 原来的代码只有 buffer_ready 一个标志位，主循环被 SD/LCD/触摸阻塞时会丢掉中间 DMA 帧，
 * 造成“实际录了 42 秒，但 WAV 只有 24 秒”。这里把每个完成的 DMA 帧复制进队列，
 * 主循环按顺序消费，避免硬件双缓冲被覆盖。
 */
#define AUDIO_RX_QUEUE_DEPTH   1024U
#define AUDIO_RX_QUEUE_WORDS_PER_FRAME  (2U * SAMPLE_AUDIO_SIZE)
#define AUDIO_RX_QUEUE_FRAME_BYTES      (AUDIO_RX_QUEUE_WORDS_PER_FRAME * sizeof(uint32_t))
#define AUDIO_RX_QUEUE_TOTAL_BYTES      (AUDIO_RX_QUEUE_DEPTH * AUDIO_RX_QUEUE_FRAME_BYTES)
#define AUDIO_RX_QUEUE_BASE_ADDR        ((uint32_t)0xC0000000U)

/*
 * 大容量 RX 队列放在板载 SDRAM 固定地址，不再作为普通全局数组定义。
 * 这样可避免启动代码在 sdram_init() 之前清零 .bss 导致 HardFault。
 * SAI DMA 首级双缓冲仍在片内 SRAM；DMA ISR 只用 CPU 把完成帧复制到 SDRAM 队列。
 */
#define audio_rx_queue ((uint32_t (*)[AUDIO_RX_QUEUE_WORDS_PER_FRAME])AUDIO_RX_QUEUE_BASE_ADDR)

/* 函数声明 */
void rcu_config(void);
void i2c_config(void);
void dac_dma_config(void);
void adc_dma_config(void);
void sai_config(void);
void process_buffer(uint32_t (*buf)[2 * SAMPLE_AUDIO_SIZE]);

/* 外部缓冲区声明 */
extern uint32_t tdm_buffer[2][2 * SAMPLE_AUDIO_SIZE];
extern uint32_t output_buffer[2][2 * SAMPLE_AUDIO_SIZE];
extern uint16_t left_mic[SAMPLE_AUDIO_SIZE];
extern uint16_t right_mic[SAMPLE_AUDIO_SIZE];
extern uint16_t ref_audio[SAMPLE_AUDIO_SIZE];
extern uint16_t null_audio[SAMPLE_AUDIO_SIZE];

/* 双缓冲控制变量 */
extern volatile uint8_t current_tdm_buffer;    /* 当前使用的TDM缓冲区索引: 0或1 */
extern volatile uint8_t current_output_buffer; /* 当前使用的输出缓冲区索引: 0或1 */
extern volatile uint8_t buffer_ready;          /* 兼容旧逻辑：队列非空时为 1 */
extern volatile uint16_t audio_rx_q_wr;        /* RX 队列写指针，中断中更新 */
extern volatile uint16_t audio_rx_q_rd;        /* RX 队列读指针，主循环更新 */
extern volatile uint16_t audio_rx_q_count;     /* RX 队列当前帧数 */
extern volatile uint16_t audio_rx_q_highwater; /* RX 队列历史最高水位，用于判断是否接近溢出 */
extern volatile uint32_t audio_rx_q_drop;      /* RX 队列满导致丢弃的帧数 */
extern volatile uint8_t audio_rx_q_accept;     /* 1=DMA ISR 可以入队；0=暂停入队用于 STOP 刷尾 */
extern volatile uint8_t output_buffer_ready[2]; /* 输出缓冲区就绪标志: 1表示对应缓冲区已填充完毕 */
extern volatile uint8_t dma_playing_buffer;     /* 当前DMA正在播放的缓冲区 */

#endif /* MAIN_H */

