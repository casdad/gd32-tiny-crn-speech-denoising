#include "./BSP/Codec/ES8311.h"
#include "./BSP/Codec/ES7210.h"
#include <stdio.h>
#include "./BSP/Codec/i2c.h"

/******************************************************************************
 * ES8311 音频编解码器驱动
 * 版本: 1.4.4.1.0425
 * 说明: 本文件实现ES8311和ES7210音频编解码器的初始化和控制
 *****************************************************************************/

/******************************************************************************
 * 芯片相关宏定义
 *****************************************************************************/
/* 芯片标识和状态 */
#define CHIPID              0x8311          /* 芯片ID: 读取0xFD/FE寄存器应得到0x8311 */
#define STATEconfirm        0xFC            /* 状态确认寄存器: 应为0x70 */

/* 音频格式定义 */
#define NORMAL_I2S          0x00            /* I2S标准格式 */
#define NORMAL_LJ           0x01            /* 左对齐格式 */
#define NORMAL_DSPA         0x03            /* DSP模式A */
#define NORMAL_DSPB         0x23            /* DSP模式B */

/* 位宽定义 */
#define Format_Len24        0x00            /* 24位数据格式 */
#define Format_Len20        0x01            /* 20位数据格式 */
#define Format_Len18        0x02            /* 18位数据格式 */
#define Format_Len16        0x03            /* 16位数据格式 */
#define Format_Len32        0x04            /* 32位数据格式 */

/* 电源电压和时钟源选择 */
#define VDDA_3V3            0x00            /* 3.3V电源 */
#define VDDA_1V8            0x01            /* 1.8V电源 */
#define MCLK_PIN            0x00            /* 使用MCLK引脚作为时钟源 */
#define SCLK_PIN            0x01            /* 使用SCLK引脚作为时钟源 */

/******************************************************************************
 * 配置参数定义
 *****************************************************************************/
#define MSMode_MasterSelOn  0               /* 0: 从模式, 1: 主模式 */
#define Ratio               256             /* 时钟比率 = MCLK/LRCK */
#define Format              NORMAL_LJ       /* 选择左对齐(LJ)音频格式 */
#define Format_Len          Format_Len16    /* 选择16位数据格式 */
#define SCLK_DIV            8               /* SCLK分频系数(1~20), SCLK=MCLK/SCLK_DIV */
#define SCLK_INV            0               /* 0: 不反转SCLK, 1: 反转SCLK */
#define MCLK_SOURCE         MCLK_PIN        /* 使用MCLK引脚作为时钟源 */
#define SCLK_STOP           0               /* 0: SCLK连续, 1: SCLK停止 */

/* ADC/DAC通道和增益设置 */
#define ADCChannelSel       1               /* ADC通道选择: 0:CH1(MIC1P/1N), 1:CH2(MIC2P/2N) */
#define DACChannelSel       0               /* DAC通道选择: 0:左, 1:右 */
#define VDDA_VOLTAGE        VDDA_3V3        /* 选择3.3V电源电压 */
#define ADC_PGA_GAIN        13              /* ADC PGA增益(0~14) */
#define ADC_Volume          190             /* ADC音量(0~255), 191:0dB, 0.5dB/步进 */
#define DAC_Volume          180             /* DAC音量(0~255), 191:0dB, 0.5dB/步进 */
#define Dmic_Selon          0               /* 0: 模拟麦克风, 1: 数字麦克风 */
#define ADC2DAC_Sel         0               /* ADC到DAC环回: 0:关闭, 1:开启 */
#define DACHPModeOn         0               /* 0: 正常模式, 1: 高性能模式 */

/* ES8312特定配置 */
#define ES8312_MONONOUT     0               /* 0: 立体声输出, 1: 单声道输出 */
#define ES8312_FM           0               /* 0: 正常模式, 1: FM模式 */

/* 使用 GPIOK 的软件 I2C 控制 ES7210
 *
 * 说明：GD32H759IMT6 Core Board V1.3 手册中，PK0=EE_SCL，PK1=EE_SDA，
 * 这两根线接板载 AT24C02，并带 4.7K 上拉。为了避免不确定 PK0/PK1
 * 对应哪一路硬件 I2C 复用功能，这里直接使用软件 I2C。
 *
 * 接线：
 *   ES7210 SCL -> PK0
 *   ES7210 SDA -> PK1
 *   ES7210 VCC -> 3.3V
 *   ES7210 GND -> GND
 */
#define SOFT_I2C_SCL_PORT      GPIOK
#define SOFT_I2C_SDA_PORT      GPIOK
#define SOFT_I2C_SCL_PIN       GPIO_PIN_0      /* PK0 = EE_SCL */
#define SOFT_I2C_SDA_PIN       GPIO_PIN_1      /* PK1 = EE_SDA */
#define SOFT_I2C_GPIO_RCU      RCU_GPIOK
#define SOFT_I2C_DELAY_US      5U              /* 约 100kHz 级别，调试阶段更稳 */

#define ES7210_ADDR_8BIT       0x80u           /* 7bit 0x40 左移后的地址 */
#define ES8311_ADDR_8BIT       (0x18u << 1)

#define SCL_H()                gpio_bit_set(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN)
#define SCL_L()                gpio_bit_reset(SOFT_I2C_SCL_PORT, SOFT_I2C_SCL_PIN)
#define SDA_H()                gpio_bit_set(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)  /* 开漏释放 */
#define SDA_L()                gpio_bit_reset(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)
#define SDA_READ()             gpio_input_bit_get(SOFT_I2C_SDA_PORT, SOFT_I2C_SDA_PIN)

static void soft_i2c_delay(void)
{
    delay_us(SOFT_I2C_DELAY_US);
}

/**
 * @brief 配置 GPIOK PK0/PK1 为软件 I2C。
 *
 * 使用开漏输出 + 上拉。PK0/PK1 在核心板上本身连接 EEPROM I2C，
 * 板载已有 4.7K 上拉；这里仍打开内部上拉，增强空闲稳定性。
 */
void i2c_config(void)
{
    rcu_periph_clock_enable(SOFT_I2C_GPIO_RCU);

    gpio_mode_set(SOFT_I2C_SCL_PORT,
                  GPIO_MODE_OUTPUT,
                  GPIO_PUPD_PULLUP,
                  SOFT_I2C_SCL_PIN | SOFT_I2C_SDA_PIN);
    gpio_output_options_set(SOFT_I2C_SCL_PORT,
                            GPIO_OTYPE_OD,
                            GPIO_OSPEED_60MHZ,
                            SOFT_I2C_SCL_PIN | SOFT_I2C_SDA_PIN);

    SDA_H();
    SCL_H();
    soft_i2c_delay();

    printf("Soft I2C on PK0/PK1 ready\r\n");
}

static void soft_i2c_start(void)
{
    SDA_H();
    SCL_H();
    soft_i2c_delay();
    SDA_L();
    soft_i2c_delay();
    SCL_L();
    soft_i2c_delay();
}

static void soft_i2c_stop(void)
{
    SDA_L();
    soft_i2c_delay();
    SCL_H();
    soft_i2c_delay();
    SDA_H();
    soft_i2c_delay();
}

static uint8_t soft_i2c_wait_bus_free(const char *tag)
{
    uint16_t timeout = 1000U;

    SDA_H();
    SCL_H();
    soft_i2c_delay();

    while((SDA_READ() == RESET) && (timeout-- > 0U)) {
        /* 如果 EEPROM/ES7210 上次事务异常导致 SDA 被拉低，给几个 SCL 脉冲尝试释放 */
        SCL_L();
        soft_i2c_delay();
        SCL_H();
        soft_i2c_delay();
    }

    if(SDA_READ() == RESET) {
        printf("Soft I2C bus busy at %s: SDA low\r\n", tag);
        soft_i2c_stop();
        return 1U;
    }

    return 0U;
}

/**
 * @brief 软件 I2C 写一个字节。
 * @return 0=收到 ACK，1=NACK
 */
static uint8_t soft_i2c_write_byte(uint8_t byte)
{
    uint8_t i;
    uint8_t nack;

    for(i = 0; i < 8U; i++) {
        if((byte & 0x80U) != 0U) {
            SDA_H();
        } else {
            SDA_L();
        }

        soft_i2c_delay();
        SCL_H();
        soft_i2c_delay();
        SCL_L();
        soft_i2c_delay();
        byte <<= 1;
    }

    /* 第 9 个时钟释放 SDA，读取从机 ACK。ACK=0，NACK=1 */
    SDA_H();
    soft_i2c_delay();
    SCL_H();
    soft_i2c_delay();
    nack = (SDA_READ() != RESET) ? 1U : 0U;
    SCL_L();
    soft_i2c_delay();

    return nack;
}

static uint8_t soft_i2c_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0U;

    SDA_H();

    for(i = 0; i < 8U; i++) {
        data <<= 1;
        SCL_H();
        soft_i2c_delay();
        if(SDA_READ() != RESET) {
            data |= 0x01U;
        }
        SCL_L();
        soft_i2c_delay();
    }

    if(ack != 0U) {
        SDA_L();       /* ACK */
    } else {
        SDA_H();       /* NACK */
    }

    soft_i2c_delay();
    SCL_H();
    soft_i2c_delay();
    SCL_L();
    SDA_H();
    soft_i2c_delay();

    return data;
}

static uint8_t soft_i2c_write_reg(uint8_t addr8, uint8_t reg, uint8_t data, const char *tag)
{
    if(soft_i2c_wait_bus_free(tag) != 0U) {
        return 1U;
    }

    soft_i2c_start();

    if(soft_i2c_write_byte(addr8 & 0xFEU) != 0U) {
        printf("%s I2C NACK: addr=0x%02x\r\n", tag, (unsigned int)(addr8 & 0xFEU));
        soft_i2c_stop();
        return 1U;
    }

    if(soft_i2c_write_byte(reg) != 0U) {
        printf("%s I2C NACK: reg=0x%02x\r\n", tag, (unsigned int)reg);
        soft_i2c_stop();
        return 1U;
    }

    if(soft_i2c_write_byte(data) != 0U) {
        printf("%s I2C NACK: reg=0x%02x data=0x%02x\r\n",
               tag,
               (unsigned int)reg,
               (unsigned int)data);
        soft_i2c_stop();
        return 1U;
    }

    soft_i2c_stop();
    return 0U;
}

static uint8_t soft_i2c_read_reg(uint8_t addr8, uint8_t reg, uint8_t *data, const char *tag)
{
    if(data == NULL) {
        return 1U;
    }

    if(soft_i2c_wait_bus_free(tag) != 0U) {
        return 1U;
    }

    soft_i2c_start();

    if(soft_i2c_write_byte(addr8 & 0xFEU) != 0U) {
        printf("%s read NACK: addrW=0x%02x\r\n", tag, (unsigned int)(addr8 & 0xFEU));
        soft_i2c_stop();
        return 1U;
    }

    if(soft_i2c_write_byte(reg) != 0U) {
        printf("%s read NACK: reg=0x%02x\r\n", tag, (unsigned int)reg);
        soft_i2c_stop();
        return 1U;
    }

    soft_i2c_start();

    if(soft_i2c_write_byte(addr8 | 0x01U) != 0U) {
        printf("%s read NACK: addrR=0x%02x\r\n", tag, (unsigned int)(addr8 | 0x01U));
        soft_i2c_stop();
        return 1U;
    }

    *data = soft_i2c_read_byte(0U);    /* 单字节读取，最后发 NACK */
    soft_i2c_stop();

    return 0U;
}

/**
 * @brief 向 ES8311 写入一个字节。
 * 当前工程 ENABLE_ES8311_OUTPUT=0 时不会调用，保留用于后续输出恢复。
 */
static uint8_t I2CWRNBYTE_CODEC(uint8_t reg, uint8_t data)
{
    return soft_i2c_write_reg(ES8311_ADDR_8BIT, reg, data, "ES8311");
}

/**
 * @brief 向 ES7210 写入一个字节。
 */
static uint8_t i2c_7210(uint8_t reg, uint8_t data)
{
    return soft_i2c_write_reg(ES7210_ADDR_8BIT, reg, data, "ES7210");
}

/**
 * @brief 从 ES7210 读取一个寄存器。
 */
int8_t hw_i2c_read_reg(uint8_t reg_addr, uint8_t *data)
{
    return (soft_i2c_read_reg(ES7210_ADDR_8BIT, reg_addr, data, "ES7210") == 0U) ? 0 : -1;
}


/**
 * @brief 初始化ES7210音频ADC编解码器
 *
 * 配置ES7210寄存器，设置音频参数:
 * - 音频格式(TDM/I2S)
 * - 时钟配置
 * - ADC增益
 * - 音量控制
 * - 工作模式
 */
void es7210_init(void)
{
    uint32_t fail_cnt = 0U;

    printf("ES7210 init enter\r\n");
#define ES7210_WR(reg, val) do { \
        if(i2c_7210((uint8_t)(reg), (uint8_t)(val)) != 0U) { \
            fail_cnt++; \
        } \
    } while(0)
    /* 软复位 */
    ES7210_WR(0x00, 0xFF);     /* 软复位所有寄存器 */
    ES7210_WR(0x00, 0x32);     /* 恢复出厂设置，保持复位 */

    /* 配置音频时钟和参数 */
    ES7210_WR(0x09, 0x20);     /* 配置ADC1/2参数 */
    ES7210_WR(0x0A, 0x10);     /* 配置ADC3/4参数 */

    /* 配置ADC工作模式 */
    ES7210_WR(0x23, 0x2A);     /* 配置ADC1/2模拟控制 */
    ES7210_WR(0x22, 0x0A);     /* 配置ADC时钟控制 */
    ES7210_WR(0x21, 0x2A);     /* 配置ADC3/4模拟控制 */
    ES7210_WR(0x20, 0x0A);     /* 配置ADC时钟控制 */

    /* 配置TDM模式，NFS=(2<<3)表示4通道 */
    ES7210_WR(0x08, (2 << 3) + (0 << 3) + 0x04 + (0 << 1));

    /* 配置标准I2S格式 */
    ES7210_WR(0x11, (0x03 << 5) + 0x01);
    ES7210_WR(0x12, 0x02);

    /* 电源管理 */
    ES7210_WR(0x40, 0xE3 - (0x34 * 0x00)); /* 3.3V时使用0xE3, 1.8V时使用0xAF */

    /* ADC配置 */
    ES7210_WR(0x14, 0x3C);  /* ADC1/2静音控制 */
    ES7210_WR(0x15, 0x3C);  /* ADC3/4静音控制 */
    ES7210_WR(0x17, 0x00);  /* ADC控制状态 */

    /* ALC配置 */
    ES7210_WR(0x41, 0x70);  /* ALC控制1 */
    ES7210_WR(0x42, 0x70);  /* ALC控制2 */

    /* 高通滤波器 */
    ES7210_WR(0x47, 0x08);  /* ADC1高通滤波器 */
    ES7210_WR(0x48, 0x08);  /* ADC2高通滤波器 */
    ES7210_WR(0x49, 0x08);  /* ADC3高通滤波器 */
    ES7210_WR(0x4A, 0x08);  /* ADC4高通滤波器 */

    /* 设置ADC增益 */
    ES7210_WR(0x43, 0x10 + ADC_PGA_GAIN);             /* 左MIC PGA增益 */
    ES7210_WR(0x44, 0x10 + 4);                        /* 参考信号增益，偏小 */
    ES7210_WR(0x45, 0x10 + ADC_PGA_GAIN);             /* 右MIC PGA增益*/
    ES7210_WR(0x46, 0x10 + ADC_PGA_GAIN);             /* ADC4 NULL 声道 PGA增益 */

    /* 设置ADC音量 */
    ES7210_WR(0x1B, ADC_Volume);  /* ADC1音量 */
    ES7210_WR(0x1C, ADC_Volume);  /* ADC2音量 */
    ES7210_WR(0x1D, ADC_Volume);  /* ADC3音量 */
    ES7210_WR(0x1E, ADC_Volume);  /* ADC4音量 */

    /* 时钟配置 */
    ES7210_WR(0x03, SCLK_DIV);        /* SCLK分频 */
    ES7210_WR(0x04, (Ratio >> 8));    /* LRCK分频高字节 */
    ES7210_WR(0x05, (Ratio & 0xFF));  /* LRCK分频低字节 */
    ES7210_WR(0x06, 0x00);            /* 时钟控制 */

    ES7210_WR(0x02, 0xC1);  /* MCLK设置，256*2=512 */
    ES7210_WR(0x07, 0x20);  /* 模式控制 */

    /* 使能通道 */
    ES7210_WR(0x4B, 0x0F);  /* 使能ADC1/2通道 */
    ES7210_WR(0x4C, 0x0F);  /* 使能ADC3/4通道 */

    /* 工作模式设置 */
    ES7210_WR(0x01, 0x20 + (0x20 * MSMode_MasterSelOn)); /* 主/从模式设置 */
    ES7210_WR(0x08, 0x14 + (SCLK_INV << 3) + (0 << 1) + MSMode_MasterSelOn); /* 时钟极性设置 */

    /* 退出复位，开始工作 */
    ES7210_WR(0x00, 0x71);  /* 准备工作 */
    ES7210_WR(0x00, 0x01);  /* 开始工作 */

    printf("ES7210 init done, fail=%u\r\n", (unsigned int)fail_cnt);
#undef ES7210_WR
}


void ES8311_Codec(void)// Standby/Powerdown+resume
{
    I2CWRNBYTE_CODEC(0x45, 0x00);
    I2CWRNBYTE_CODEC(0x01, 0x30);
    I2CWRNBYTE_CODEC(0x02, 0x10);

    if(Ratio == 1536) { //Ratio=MCLK/LRCK=153612M288-8K
        I2CWRNBYTE_CODEC(0x02, 0xA0); //MCLK DIV=6
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 1500) { //Ratio=MCLK/LRCK=150012M-8K
        I2CWRNBYTE_CODEC(0x02, 0x90); //MCLK = 12/5*4 =9M6 1200Ratio
        I2CWRNBYTE_CODEC(0x03, 0x19); //400
        I2CWRNBYTE_CODEC(0x16, 0x20);
        I2CWRNBYTE_CODEC(0x04, 0x19);
        I2CWRNBYTE_CODEC(0x05, 0x22);
    }
    if(Ratio == 1280) { //Ratio=MCLK/LRCK=128024M576-19K2
        I2CWRNBYTE_CODEC(0x02, 0x80); //MCLK DIV=5
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 1024) { //Ratio=MCLK/LRCK=10248M192-8K
        I2CWRNBYTE_CODEC(0x02, 0x60); //MCLK DIV=4
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 960) { //Ratio=MCLK/LRCK=96015M36-16K
        I2CWRNBYTE_CODEC(0x02, 0x20); //MCLK = 15M36 /2=7M68 480Ratio
        I2CWRNBYTE_CODEC(0x03, 0x1E);
        I2CWRNBYTE_CODEC(0x16, 0x20);
        I2CWRNBYTE_CODEC(0x04, 0x1E);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 768) { //Ratio=MCLK/LRCK=76812M288-16K6M144-8K;
        I2CWRNBYTE_CODEC(0x02, 0x40); //MCLK DIV=3
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 750) { //Ratio=MCLK/LRCK=75012M-16K
        I2CWRNBYTE_CODEC(0x02, 0x9A); //MCLK = 12/5*8 =9M6 1200Ratio
        I2CWRNBYTE_CODEC(0x03, 0x19); //400
        I2CWRNBYTE_CODEC(0x16, 0x21);
        I2CWRNBYTE_CODEC(0x04, 0x19);
        I2CWRNBYTE_CODEC(0x05, 0x22);
    }
    if(Ratio == 544) { //Ratio=MCLK/LRCK=544
        //24M-44K1THDNSNR
        I2CWRNBYTE_CODEC(0x02, 0x20); //MCLK DIV=2
        I2CWRNBYTE_CODEC(0x03, 0x11);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x11);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 512) { //Ratio=MCLK/LRCK=51224M576-48K8M192-16K
        I2CWRNBYTE_CODEC(0x02, 0x20); //MCLK DIV=2
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 500) { //Ratio=MCLK/LRCK=50024M-48K8M-16K
        I2CWRNBYTE_CODEC(0x02, 0x92); //CLK =MCLK/5*4=400
        I2CWRNBYTE_CODEC(0x03, 0x19); //ADC 400 FS
        I2CWRNBYTE_CODEC(0x16, 0x21);
        I2CWRNBYTE_CODEC(0x04, 0x19); //DAC 400 FS
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 480) { //Ratio=MCLK/LRCK=48015M36-32K
        I2CWRNBYTE_CODEC(0x02, 0x00); //MCLK = 480Ratio
        I2CWRNBYTE_CODEC(0x03, 0x1E);
        I2CWRNBYTE_CODEC(0x16, 0x20);
        I2CWRNBYTE_CODEC(0x04, 0x1E);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 384) { // Ratio=MCLK/LRCK=38412M288-32K6M144-16K3M072-8K;
        I2CWRNBYTE_CODEC(0x02, 0x00); //
        I2CWRNBYTE_CODEC(0x03, 0x18); //384
        I2CWRNBYTE_CODEC(0x16, 0x21); //384
        I2CWRNBYTE_CODEC(0x04, 0x18); //384
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 320) { //Ratio=MCLK/LRCK=3205M12-16K
        I2CWRNBYTE_CODEC(0x02, 0x92); //320 /5 *4 = 256Ratio
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 272) { //Ratio=MCLK/LRCK=272
        //12M-44K1THDNSNR
        I2CWRNBYTE_CODEC(0x02, 0x00); //MCLK DIV=1
        I2CWRNBYTE_CODEC(0x03, 0x11);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x11);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 256) { //Ratio=MCLK/LRCK=25612M288-48K4M096-16K; 2M048-8K
        I2CWRNBYTE_CODEC(0x02, 0x00); //MCLK DIV=1
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
#if 0//8K
        I2CWRNBYTE_CODEC(0x02, 0x09); //MCLK DIV=1
        I2CWRNBYTE_CODEC(0x03, 0x20);
        I2CWRNBYTE_CODEC(0x16, 0x20);
        I2CWRNBYTE_CODEC(0x04, 0x40);
        I2CWRNBYTE_CODEC(0x05, 0x00);
#endif
    }
    if(Ratio == 250) { //Ratio=MCLK/LRCK=25012M-48K4M-16K
        I2CWRNBYTE_CODEC(0x02, 0x9A); //CLK =MCLK/5*8=400
        I2CWRNBYTE_CODEC(0x03, 0x19); //ADC 400 FS
        I2CWRNBYTE_CODEC(0x16, 0x21);
        I2CWRNBYTE_CODEC(0x04, 0x19); //DAC 400 FS
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 192) { // Ratio=MCLK/LRCK=19212M288-64K6M144-32K3M072-16K; 1M536-8K
        I2CWRNBYTE_CODEC(0x02, 0x51); ////MCLK = MCLK/3*4 =9M6 256Ratio
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 128) { // Ratio=MCLK/LRCK=1286M144-48K2M048-16K; 1M024-8K
        I2CWRNBYTE_CODEC(0x02, 0x0A);
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 64) { // Ratio=MCLK/LRCK=643M072-48K1M024-16K; 512k-8K
        /*512K/8KDVDD1V8*/
        I2CWRNBYTE_CODEC(0x02, 0x10);
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }
    if(Ratio == 48) { // Ratio=MCLK/LRCK=643M072-48K1M024-16K; 512k-8K
        //8K 48FS
        /*48KDVDD3V3LRCK=16KDVDD=1V8*/
        I2CWRNBYTE_CODEC(0x02, 0x1A); //48FS*8=384
        I2CWRNBYTE_CODEC(0x05, 0x21); //128FS
        I2CWRNBYTE_CODEC(0x03, 0x20); //128FS
        I2CWRNBYTE_CODEC(0x16, 0x20);
        I2CWRNBYTE_CODEC(0x04, 0x30); //192FS
    }
    if(Ratio == 32) { // Ratio=MCLK/LRCK=321M536-48K512K-16K;
        //8K 32FS
        /*512K/16K768K/24KDVDD1V8*/
        I2CWRNBYTE_CODEC(0x02, 0x1A);
        I2CWRNBYTE_CODEC(0x03, 0x10);
        I2CWRNBYTE_CODEC(0x16, 0x24);
        I2CWRNBYTE_CODEC(0x04, 0x20);
        I2CWRNBYTE_CODEC(0x05, 0x00);
    }

    I2CWRNBYTE_CODEC(0x06, (SCLK_STOP << 6) + (SCLK_INV << 5) + SCLK_DIV - 1); //SCLK DIV
    I2CWRNBYTE_CODEC(0x07, ((Ratio - 1) >> 8));         //LRCK DIV
    I2CWRNBYTE_CODEC(0x08, ((Ratio - 1) & 0xFF));       //LRCK DIV


    I2CWRNBYTE_CODEC(0x09, (DACChannelSel << 7) + Format + (0x04 << 2));
    I2CWRNBYTE_CODEC(0x0A, Format + (0x04 << 2));

    I2CWRNBYTE_CODEC(0x0B, 0x00);
    I2CWRNBYTE_CODEC(0x0C, 0x00);

    I2CWRNBYTE_CODEC(0x10, (0x1C * DACHPModeOn) + (0x5E * VDDA_VOLTAGE) + 0x03); //3V3031V861

    I2CWRNBYTE_CODEC(0x11, 0x7F);
    I2CWRNBYTE_CODEC(0x01, 0x3F + (MCLK_SOURCE << 7)); //BCLKMCLK
    I2CWRNBYTE_CODEC(0x00, 0x80 + (MSMode_MasterSelOn << 6)); //Slave  Mode
    delay_ms(1);
    I2CWRNBYTE_CODEC(0x0D, 0x01);

    I2CWRNBYTE_CODEC(0x14, (Dmic_Selon << 6) + (ADCChannelSel << 4) + ADC_PGA_GAIN); //CH1+30DB GAIN

    I2CWRNBYTE_CODEC(0x12, 0x28);
    I2CWRNBYTE_CODEC(0x13, 0x00 + (DACHPModeOn << 4));
    /*****************************************/
//ES8311ES8312
    if(ES8312_MONONOUT == 1) {
        I2CWRNBYTE_CODEC(0x12, 0xC8); //ES8312MONOOUT
    }
    if(ES8312_FM == 1) {
        I2CWRNBYTE_CODEC(0x13, 0xA0); //ES8312FM
        //HPOUT 13B0
    }
    /*****************************************/
    I2CWRNBYTE_CODEC(0x0E, 0x02);
    I2CWRNBYTE_CODEC(0x0F, 0x44);
    I2CWRNBYTE_CODEC(0x15, 0x00);
    I2CWRNBYTE_CODEC(0x1B, 0x0A);
    I2CWRNBYTE_CODEC(0x1C, 0x6A);
    I2CWRNBYTE_CODEC(0x37, 0x08);
    I2CWRNBYTE_CODEC(0x44, (ADC2DAC_Sel << 7));
    I2CWRNBYTE_CODEC(0x17, ADC_Volume);
    I2CWRNBYTE_CODEC(0x32, DAC_Volume);
}

void ES8311_Reset(void)//IIS
{
    I2CWRNBYTE_CODEC(0x00, 0x1F);
    I2CWRNBYTE_CODEC(0x00, 0x80);
    delay_ms(1);                   //DELAY_1MS
    I2CWRNBYTE_CODEC(0x0D, 0x01);
}
