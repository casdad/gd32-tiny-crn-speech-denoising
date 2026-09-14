#ifndef I2C_H
#define I2C_H

#include "gd32h7xx.h"
#include "./SYSTEM/delay/delay.h"

/*
 * ES7210 控制口改为 GPIOK 软件 I2C：
 *   PK0 = SCL / EE_SCL
 *   PK1 = SDA / EE_SDA
 *
 * 这组线在 GD32H759IMT6 Core Board V1.3 上连接板载 AT24C02 EEPROM，
 * 并带 4.7K 上拉。ES7210 可以与 EEPROM 共用 I2C 总线，地址不同即可。
 */
#define I2C_SPEED           100000
#define I2C_PAGE_SIZE       8

#define RCU_GPIO_I2C        RCU_GPIOK
#define I2C_SCL_PORT        GPIOK
#define I2C_SDA_PORT        GPIOK
#define I2C_SCL_PIN         GPIO_PIN_0      /* PK0 = EE_SCL */
#define I2C_SDA_PIN         GPIO_PIN_1      /* PK1 = EE_SDA */

void i2c_config(void);

#endif /* I2C_H */
