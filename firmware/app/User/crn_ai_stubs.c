#include <stdint.h>
#include "gd_nn_report.h"

/*
 * The recorder project already uses its own SysTick/delay implementation.
 * These lightweight stubs satisfy optional timing/report symbols used by
 * GD32 Embedded AI without taking over SysTick.
 */
uint32_t sys_count = 0U;

void gd_nn_report_printf(nn_model_report_struct *report)
{
    (void)report;
}

void gd_nn_measure_time_start(void)
{
    sys_count = 0U;
}

float gd_nn_measure_time_get(float scale, uint32_t clock)
{
    (void)scale;
    (void)clock;
    return 0.0f;
}

void gd_nn_measure_time_stop(void)
{
    sys_count = 0U;
}
