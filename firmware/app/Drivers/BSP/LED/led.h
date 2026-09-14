/**
 ****************************************************************************************************
 * @file        led.h
 * @version     V1.0
 * @brief       LED 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */	

#ifndef __LED_H
#define __LED_H

#include "./SYSTEM/sys/sys.h"


/******************************************************************************************/
/* 引脚 定义 */

#define LED0_GPIO_PORT                  GPIOC
#define LED0_GPIO_PIN                   GPIO_PIN_13
#define LED0_GPIO_CLK                   RCU_GPIOC     /* GPIOC时钟使能 */

#define LED1_GPIO_PORT                  GPIOJ
#define LED1_GPIO_PIN                   GPIO_PIN_8
#define LED1_GPIO_CLK                   RCU_GPIOJ     /* GPIOJ时钟使能 */


/* LED端口定义 */
#define LED0(x)   do{ x ? \
                      gpio_bit_write(LED0_GPIO_PORT, LED0_GPIO_PIN, SET) : \
                      gpio_bit_write(LED0_GPIO_PORT, LED0_GPIO_PIN, RESET); \
                  }while(0)         /* LED0 = RED */  

#define LED1(x)   do{ x ? \
                      gpio_bit_write(LED1_GPIO_PORT, LED1_GPIO_PIN, SET) : \
                      gpio_bit_write(LED1_GPIO_PORT, LED1_GPIO_PIN, RESET); \
                  }while(0)         /* LED1 = GREEN */ 
                  
/* LED取反定义 */
#define LED0_TOGGLE()   do{ gpio_bit_toggle(LED0_GPIO_PORT, LED0_GPIO_PIN); }while(0)   /* 翻转LED0 */
#define LED1_TOGGLE()   do{ gpio_bit_toggle(LED1_GPIO_PORT, LED1_GPIO_PIN); }while(0)   /* 翻转LED1 */

/******************************************************************************************/

void led_init(void);                /* 初始化LED */

#endif





