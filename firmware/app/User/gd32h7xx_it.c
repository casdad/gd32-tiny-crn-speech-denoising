/*!
    \file    gd32h7xx_it.c
    \brief   interrupt service routines

    \version 2026-02-04, V1.5.0, firmware for GD32H7xx
*/

#include "gd32h7xx_it.h"
#include "main.h"

extern volatile uint8_t current_tdm_buffer;
extern volatile uint8_t current_output_buffer;
extern volatile uint8_t buffer_ready;
extern volatile uint8_t output_buffer_ready[2];
extern volatile uint8_t dma_playing_buffer;
extern volatile uint32_t dma_tx_irq_cnt;
extern volatile uint32_t dma_rx_irq_cnt;

/*!
    \brief      this function handles NMI exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void NMI_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles HardFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void HardFault_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles MemManage exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
#if 0
void MemManage_Handler(void)
{
    while(1) {
    }
}
#endif

/*!
    \brief      this function handles BusFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void BusFault_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles UsageFault exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void UsageFault_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles DebugMon exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void DebugMon_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles SVC exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SVC_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles PendSV exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void PendSV_Handler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles FPU exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void FPU_IRQHandler(void)
{
    while(1) {
    }
}

/*!
    \brief      this function handles SysTick exception
    \param[in]  none
    \param[out] none
    \retval     none
*/
void SysTick_Handler(void)
{
}

/*!
    \brief      DMA0通道0中断处理函数 - 音频输出DMA
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void DMA0_Channel0_IRQHandler(void)
{
    uint8_t current_memory;
    uint8_t current_buffer;
    uint8_t next_buffer;

    /* 处理FIFO错误中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH0, DMA_FLAG_FEE)) {
        dma_flag_clear(DMA0, DMA_CH0, DMA_FLAG_FEE);
    }

    /* 处理传输错误中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH0, DMA_FLAG_TAE)) {
        dma_flag_clear(DMA0, DMA_CH0, DMA_FLAG_TAE);
    }

    /* 处理单数据传输错误中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH0, DMA_FLAG_SDE)) {
        dma_flag_clear(DMA0, DMA_CH0, DMA_FLAG_SDE);
    }

    /* 处理半传输完成中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH0, DMA_FLAG_HTF)) {
        dma_flag_clear(DMA0, DMA_CH0, DMA_FLAG_HTF);
    }

    /* 处理满传输完成中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH0, DMA_FLAG_FTF)) {
        dma_tx_irq_cnt++;

        /* 获取当前正在使用的内存区域 */
        current_memory = dma_using_memory_get(DMA0, DMA_CH0);

        /* 根据当前内存确定当前播放的缓冲区 */
        current_buffer = (current_memory == DMA_MEMORY_0) ? 0 : 1;
        next_buffer = (current_buffer == 0) ? 1 : 0;

        /* 更新当前正在播放的缓冲区索引 */
        dma_playing_buffer = current_buffer;
        /* 设置刚刚完成的缓冲区为未就绪状态，等待新数据填充 */
        output_buffer_ready[next_buffer] = 0;
        /* 更新可填充的缓冲区索引 */
        current_output_buffer = next_buffer;

        /* 清除满传输完成中断标志 */
        dma_flag_clear(DMA0, DMA_CH0, DMA_FLAG_FTF);
    }
}

/*!
    \brief      DMA0通道1中断处理函数 - 音频输入DMA
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void DMA0_Channel1_IRQHandler(void)
{
    uint8_t current_memory;
    uint8_t filled_buffer;
    uint32_t start_addr;
    uint32_t size;

    /* 处理FIFO错误中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH1, DMA_FLAG_FEE)) {
        dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_FEE);
    }

    /* 处理传输错误中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH1, DMA_FLAG_TAE)) {
        dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_TAE);
    }

    /* 处理单数据传输错误中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH1, DMA_FLAG_SDE)) {
        dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_SDE);
    }

    /* 处理半传输完成中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH1, DMA_FLAG_HTF)) {
        dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_HTF);
    }

    /* 处理满传输完成中断 */
    if(SET == dma_flag_get(DMA0, DMA_CH1, DMA_FLAG_FTF)) {
        dma_rx_irq_cnt++;

        /* 获取当前正在使用的内存区域 */
        current_memory = dma_using_memory_get(DMA0, DMA_CH1);

        /* 获取已经填充完成的缓冲区索引 - 注意是相反的内存 */
        filled_buffer = (current_memory == DMA_MEMORY_0) ? 1 : 0;

        /* 使DCache失效，确保CPU能够读取到DMA写入的最新数据 - 对齐到32字节边界 */
        start_addr = (uint32_t)tdm_buffer[filled_buffer];
        size = 2 * SAMPLE_AUDIO_SIZE * sizeof(uint32_t);

        SCB_InvalidateDCache_by_Addr((uint32_t *)start_addr, size);

        /*
         * 关键修复：不要只用 current_tdm_buffer + buffer_ready。
         * 旧逻辑在主循环被 SD 写入/LCD 刷新阻塞时，会被下一次 DMA 中断覆盖，
         * 中间音频块直接丢失，表现为“录了 42 秒，文件只有 24 秒”。
         * 这里在中断里把完成的硬件 DMA 块复制到软件队列，主循环按顺序消费。
         */
        current_tdm_buffer = filled_buffer;
        if((audio_rx_q_accept != 0U) && (audio_rx_q_count < AUDIO_RX_QUEUE_DEPTH)) {
            uint32_t i;
            uint32_t *dst = audio_rx_queue[audio_rx_q_wr];
            uint32_t *src = tdm_buffer[filled_buffer];

            for(i = 0U; i < (2U * SAMPLE_AUDIO_SIZE); i++) {
                dst[i] = src[i];
            }

            audio_rx_q_wr++;
            if(audio_rx_q_wr >= AUDIO_RX_QUEUE_DEPTH) {
                audio_rx_q_wr = 0U;
            }
            audio_rx_q_count++;
            if(audio_rx_q_count > audio_rx_q_highwater) {
                audio_rx_q_highwater = audio_rx_q_count;
            }
            buffer_ready = 1U;
        } else if(audio_rx_q_accept != 0U) {
            audio_rx_q_drop++;
        } else {
            /* STOP/离线处理期间暂停入队，丢弃实时预览帧，不计入录音 DROP。 */
        }

        /* 清除满传输完成中断标志 */
        dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_FTF);
    }
}