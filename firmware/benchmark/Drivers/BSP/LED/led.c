/**
 ****************************************************************************************************
 * @file        led.c
 * @version     V1.0
 * @brief       LED 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 开发板
 *
 * 实验平台:    GD32H759IMT6小系统板
 ****************************************************************************************************
 */

#include "./BSP/LED/led.h"


/**
 * @brief       初始化LED相关IO口, 并使能时钟
 * @param       无
 * @retval      无
 */
void led_init(void)
{
    rcu_periph_clock_enable(LED0_GPIO_CLK);     /* 使能LED0时钟 */
    rcu_periph_clock_enable(LED1_GPIO_CLK);     /* 使能LED1时钟 */
    
    /* LED0引脚模式设置 */
    gpio_mode_set(LED0_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED0_GPIO_PIN);
    gpio_output_options_set(LED0_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, LED0_GPIO_PIN);
  
    /* LED1引脚模式设置 */
    gpio_mode_set(LED1_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LED1_GPIO_PIN);
    gpio_output_options_set(LED1_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, LED1_GPIO_PIN);
	  
    LED0(1);                                    /* 关闭LED0 */ 
    LED1(1);                                    /* 关闭LED1 */
}