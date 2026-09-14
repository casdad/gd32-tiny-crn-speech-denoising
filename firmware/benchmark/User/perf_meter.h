#ifndef PERF_METER_H
#define PERF_METER_H

#include <stdint.h>

extern volatile uint8_t g_perf_uart_enable;

/*
 * DWT cycle counter based performance meter for GD32H759 / Cortex-M7.
 * Used to collect the data for Table 6-2: single-frame processing latency breakdown.
 */
typedef enum {
    PERF_DMA_ISR = 0,          /* DMA interrupt copy/enqueue */
    PERF_PARSE_LEVEL,          /* TDM channel parsing + level statistics */
    PERF_STFT_BAND,            /* STFT + magnitude / band feature preparation */
    PERF_CRN_INVOKE,           /* CRN model forward inference only */
    PERF_MASK_ISTFT_OLA,       /* mask mapping + iFFT + OLA reconstruction */
    PERF_LCD_REFRESH,          /* LCD dashboard refresh */
    PERF_TF_WRITE,             /* TF-card write path */
    PERF_FRAME_TOTAL,          /* process_audio_frame() total cost */
    PERF_ITEM_NUM
} perf_item_t;

void perf_init(void);
uint32_t perf_now(void);
void perf_add(perf_item_t item, uint32_t cycles);
void perf_print(void);
void perf_reset(void);

#endif /* PERF_METER_H */
