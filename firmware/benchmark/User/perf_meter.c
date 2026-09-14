#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "gd32h7xx.h"
#include "perf_meter.h"

typedef struct {
    uint64_t sum_cycles;
    uint32_t max_cycles;
    uint32_t count;
} perf_stat_t;

volatile uint8_t g_perf_uart_enable = 0U;

static perf_stat_t g_perf[PERF_ITEM_NUM];

static const char *g_perf_name[PERF_ITEM_NUM] = {
    "DMA_ISR",
    "PARSE_LEVEL",
    "STFT_BAND",
    "CRN_INVOKE",
    "MASK_ISTFT_OLA",
    "LCD_REFRESH",
    "TF_WRITE",
    "FRAME_TOTAL"
};

void perf_init(void)
{
    memset(g_perf, 0, sizeof(g_perf));

    /* Enable DWT CYCCNT. Cortex-M7 supports this counter. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t perf_now(void)
{
    return DWT->CYCCNT;
}

void perf_add(perf_item_t item, uint32_t cycles)
{
    if(item >= PERF_ITEM_NUM) {
        return;
    }

    g_perf[item].sum_cycles += cycles;

    if(cycles > g_perf[item].max_cycles) {
        g_perf[item].max_cycles = cycles;
    }

    g_perf[item].count++;
}

static uint32_t cycles_to_us(uint32_t cycles)
{
    if(SystemCoreClock == 0U) {
        return 0U;
    }

    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / (uint64_t)SystemCoreClock);
}

void perf_print(void)
{
    uint32_t i;

    g_perf_uart_enable = 1U;

    printf("\r\n========== PERF STAT ==========" "\r\n");

    for(i = 0U; i < PERF_ITEM_NUM; i++) {
        uint32_t avg_cycles;
        uint32_t avg_us;
        uint32_t max_us;

        if(g_perf[i].count == 0U) {
            continue;
        }

        avg_cycles = (uint32_t)(g_perf[i].sum_cycles / g_perf[i].count);
        avg_us = cycles_to_us(avg_cycles);
        max_us = cycles_to_us(g_perf[i].max_cycles);

        printf("%-16s avg=%lu.%03lu ms, max=%lu.%03lu ms, n=%lu\r\n",
               g_perf_name[i],
               (unsigned long)(avg_us / 1000U),
               (unsigned long)(avg_us % 1000U),
               (unsigned long)(max_us / 1000U),
               (unsigned long)(max_us % 1000U),
               (unsigned long)g_perf[i].count);
    }

    printf("================================" "\r\n");

    g_perf_uart_enable = 0U;
}

void perf_reset(void)
{
    memset(g_perf, 0, sizeof(g_perf));
}
