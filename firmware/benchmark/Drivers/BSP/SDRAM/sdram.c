/**
 ****************************************************************************************************
 * @file        sdram.c
 * @version     V1.0
 * @brief       SDRAM 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */
 
#include "./BSP/SDRAM/sdram.h"
#include "./SYSTEM/usart/usart.h" 
#include "./SYSTEM/delay/delay.h"


/* 定义 SDRAM 模式寄存器的内容 */
/* 突发长度 */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001)
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002)
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0003)

/* 突发访问的地址模式 */
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000)
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008)

/* 列地址选通延迟(CAS Latency) */
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020)
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030)

/* 写模式 */
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000)
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200)

#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000)

#define SDRAM_TIMEOUT                            ((uint32_t)0x0000FFFF)

/**
 * @brief       初始化SDRAM
 * @param       sdram_device: EXMC SDRAM devicex
 * @retval      无
 */
void sdram_init(uint32_t sdram_device)
{
    exmc_sdram_parameter_struct        sdram_init_struct;
    exmc_sdram_timing_parameter_struct  sdram_timing_init_struct;
    exmc_sdram_command_parameter_struct     sdram_command_init_struct;
   
    uint32_t command_content = 0, bank_select;
    uint32_t timeout = SDRAM_TIMEOUT;
  
    /* IO 及 时钟配置 */  
    rcu_periph_clock_enable(RCU_EXMC);      /* 使能EXMC时钟 */
    rcu_periph_clock_enable(RCU_GPIOC);     /* 使能GPIOC时钟 */
    rcu_periph_clock_enable(RCU_GPIOD);     /* 使能GPIOD时钟 */
    rcu_periph_clock_enable(RCU_GPIOE);     /* 使能GPIOE时钟 */
    rcu_periph_clock_enable(RCU_GPIOF);     /* 使能GPIOF时钟 */
    rcu_periph_clock_enable(RCU_GPIOG);     /* 使能GPIOG时钟 */       

    /* 设置PC0/4/5  复用推挽输出 */
	  gpio_af_set(GPIOC, GPIO_AF_12, GPIO_PIN_0 | GPIO_PIN_4 | GPIO_PIN_5);
    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_0 | GPIO_PIN_4 | GPIO_PIN_5);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_0 | GPIO_PIN_4 | GPIO_PIN_5);
                            
    /* 设置PD0/1/8/9/10/14/15  复用推挽输出 */
	  gpio_af_set(GPIOD, GPIO_AF_12, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 |
                  GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 |GPIO_PIN_9 |
                            GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15);

    /* 设置PE0/1/7~15  复用推挽输出 */
	  gpio_af_set(GPIOE, GPIO_AF_12, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                GPIO_PIN_11 | GPIO_PIN_12 |GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOE, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                  GPIO_PIN_11 | GPIO_PIN_12 |GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 |GPIO_PIN_9 | GPIO_PIN_10 |
                            GPIO_PIN_11 | GPIO_PIN_12 |GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);

    /* PF0~5/11~15  复用推挽输出 */
    gpio_af_set(GPIOF, GPIO_AF_12, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
                GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOF, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
                  GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOF, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 |GPIO_PIN_4 | GPIO_PIN_5 |
                            GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 |GPIO_PIN_14 | GPIO_PIN_15);
	
    /* 设置PG0~2/4/5/8/15  复用推挽输出 */
    gpio_af_set(GPIOG, GPIO_AF_12, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15);
    gpio_mode_set(GPIOG, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15);
    gpio_output_options_set(GPIOG, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15);
     
     /* 指定命令所发送的SDRAM device */
    if(EXMC_SDRAM_DEVICE0 == sdram_device) {
        bank_select = EXMC_SDRAM_DEVICE0_SELECT;
    } else {
        bank_select = EXMC_SDRAM_DEVICE1_SELECT;
    }

    exmc_sdram_struct_para_init(&sdram_init_struct);        /* 初始化结构体exmc_sdram_parameter_struct */
      
    /* 配置SDRAM时序寄存器 */
    sdram_timing_init_struct.load_mode_register_delay = 2;  /* 加载模式寄存器延迟为2个时钟周期 */
    sdram_timing_init_struct.exit_selfrefresh_delay = 11;   /* 退出自刷新的延迟为11个时钟周期 */
    sdram_timing_init_struct.row_address_select_delay = 10; /* 行地址选择延迟为10个时钟周期 */
    sdram_timing_init_struct.auto_refresh_delay = 10;       /* 自动刷新延迟为10个时钟周期 */
    sdram_timing_init_struct.write_recovery_delay = 2;      /* 写恢复延迟为2个时钟周期 */
    sdram_timing_init_struct.row_precharge_delay = 3;       /* 行预充电延迟为3个时钟周期 */
    sdram_timing_init_struct.row_to_column_delay = 3;       /* 行至列的延迟为3个时钟周期 */

    /* 配置SDRAM控制寄存器 */
    sdram_init_struct.sdram_device = sdram_device;                           /* 选择EXMC SDRAM devicex */
    sdram_init_struct.column_address_width = EXMC_SDRAM_COW_ADDRESS_9;       /* 9位列地址位宽 */ 
    sdram_init_struct.row_address_width = EXMC_SDRAM_ROW_ADDRESS_13;         /* 13位行地址位宽 */
    sdram_init_struct.data_width = EXMC_SDRAM_DATABUS_WIDTH_16B;             /* 16位SDRAM数据总线宽度 */
    sdram_init_struct.internal_bank_number = EXMC_SDRAM_4_INTER_BANK;        /* 4个内部Banks */
    sdram_init_struct.cas_latency = EXMC_CAS_LATENCY_3_SDCLK;                /* CAS延迟为3个SDRAM存储器时钟周期 */
    sdram_init_struct.write_protection = DISABLE;                            /* 禁用写保护，允许写访问 */
    sdram_init_struct.sdclock_config = EXMC_SDCLK_PERIODS_2_CK_EXMC;         /* SDRAM时钟周期为2个CK_EXMC周期 SDCLK=CK_EXMC/2=300M/2=150M=6.7ns */
    sdram_init_struct.burst_read_switch = ENABLE;                            /* 使能突发读 */
    sdram_init_struct.pipeline_read_delay = EXMC_PIPELINE_DELAY_2_CK_EXMC;   /* 流水线读数据延迟2个CK_EXMC周期 */
    sdram_init_struct.timing = &sdram_timing_init_struct;                    /* 读写时序参数 */                                 
    exmc_sdram_init(&sdram_init_struct);                                     /* 初始化EXMC SDRAM device */ 

    /* 时钟配置使能 */
    sdram_command_init_struct.command = EXMC_SDRAM_CLOCK_ENABLE;                       /* 时钟使能命令 */
    sdram_command_init_struct.bank_select = bank_select;                               /* 选择SDRAM devicex */
    sdram_command_init_struct.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_1_SDCLK;   /* 连续的自动刷新个数(CMD=011时有效) */
    sdram_command_init_struct.mode_register_content = 0;                               /* 模式寄存器内容 */
    /* 等待SDRAM控制器就绪 */
    while((exmc_flag_get(sdram_device, EXMC_SDRAM_FLAG_NREADY) != RESET) && (timeout > 0)) 
    {  
        timeout--;
    }

    exmc_sdram_command_config(&sdram_command_init_struct);   /* 向SDRAM发送命令 */

    delay_ms(10);   /* 延时10ms */

    /* 对所有存储区预充电 */
    sdram_command_init_struct.command = EXMC_SDRAM_PRECHARGE_ALL;                      /* 所有存储区预充电命令 */
    sdram_command_init_struct.bank_select = bank_select;
    sdram_command_init_struct.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_1_SDCLK;   
    sdram_command_init_struct.mode_register_content = 0;
    
    /* 等待SDRAM控制器就绪 */
    timeout = SDRAM_TIMEOUT;
    while((exmc_flag_get(sdram_device, EXMC_SDRAM_FLAG_NREADY) != RESET) && (timeout > 0)) 
    {  
        timeout--;
    }

    exmc_sdram_command_config(&sdram_command_init_struct);   /* 向SDRAM发送命令 */

    /* 设置自刷新次数 */
    sdram_command_init_struct.command = EXMC_SDRAM_AUTO_REFRESH;                       /* 自动刷新命令  */
    sdram_command_init_struct.bank_select = bank_select;
    sdram_command_init_struct.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_8_SDCLK;   /* 8个连续自动刷新周期 */
    sdram_command_init_struct.mode_register_content = 0;
    
    /* 等待SDRAM控制器就绪 */
    timeout = SDRAM_TIMEOUT;
    while((exmc_flag_get(sdram_device, EXMC_SDRAM_FLAG_NREADY) != RESET) && (timeout > 0)) 
    {  
        timeout--;
    }

    exmc_sdram_command_config(&sdram_command_init_struct);   /* 向SDRAM发送命令 */

    /* 设置SDRAM的模式寄存器 */
    /* 配置模式寄存器,SDRAM的bit0~bit2为指定突发访问的长度，
     * bit3为指定突发访问的类型，bit4~bit6为CAS值，bit7和bit8为运行模式
     * bit9为指定的写突发模式，bit10和bit11位保留位 */
    command_content = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1    |                      /* 设置突发长度:1(可以是1/2/4/8) */ 
                      SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      |                       /* 设置突发类型:连续(可以是连续/交错) */ 
                      SDRAM_MODEREG_CAS_LATENCY_3              |                       /* 设置CAS Latency值:3(可以是2/3) */ 
                      SDRAM_MODEREG_OPERATING_MODE_STANDARD    |                       /* 设置操作模式:0,标准模式 */ 
                      SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;                            /* 设置突发写模式:1,单次写入 */ 

    sdram_command_init_struct.command = EXMC_SDRAM_LOAD_MODE_REGISTER;                 /* 加载模式寄存器命令 */ 
    sdram_command_init_struct.bank_select = bank_select; 
    sdram_command_init_struct.auto_refresh_number = EXMC_SDRAM_AUTO_REFLESH_1_SDCLK;   
    sdram_command_init_struct.mode_register_content = command_content;                 /* 模式寄存器内容 */ 

    /* 等待SDRAM控制器就绪 */
    timeout = SDRAM_TIMEOUT;
    while((exmc_flag_get(sdram_device, EXMC_SDRAM_FLAG_NREADY) != RESET) && (timeout > 0)) 
    {  
        timeout--;
    }

    exmc_sdram_command_config(&sdram_command_init_struct);   /* 向SDRAM发送模式寄存器的值 */

    /**
     * 配置自动刷新间隔(以SDCLK频率计数),计算方法:
     * ARINTV=SDRAM刷新周期/行数-20=SDRAM刷新周期(us)*SDCLK频率(Mhz)/行数-20
     * 我们使用的SDRAM刷新周期为64ms,SDCLK=300/2=150Mhz,行数为8192(2^13).
     * 所以,ARINTV=64*1000*150/8192-20=1151
     */
    exmc_sdram_refresh_count_set(7.81 * 150 - 20);

    /* 等待SDRAM控制器就绪 */
    timeout = SDRAM_TIMEOUT;
    while((exmc_flag_get(sdram_device, EXMC_SDRAM_FLAG_NREADY) != RESET) && (timeout > 0)) 
    {   
        timeout--;
    }
}

/**
 * @brief       从SDRAM指定地址(addr + SDRAM_DEVICE0_ADDR)开始,连续写入n个字节
 * @param       pbuf    : 数据存储区
 * @param       addr    : 开始写入的地址
 * @param       n       : 要写入的字节数
 * @retval      无
 */
void sdram_writebuffer_8(uint8_t *pbuf, uint32_t addr, uint32_t n)
{
    for (; n != 0; n--)
    {
        *(volatile uint8_t *)(SDRAM_DEVICE0_ADDR + addr) = *pbuf;
        addr++;
        pbuf++;
    }
}

/**
 * @brief       从SDRAM指定地址(addr + SDRAM_DEVICE0_ADDR)开始,连续读取n个字节
 * @param       pbuf    : 数据存储区
 * @param       addr    : 开始读取的地址
 * @param       n       : 要读取的字节数
 * @retval      无
 */
void sdram_readbuffer_8(uint8_t *pbuf, uint32_t addr, uint32_t n)
{
    for (; n != 0; n--)
    {
        *pbuf++ = *(volatile uint8_t *)(SDRAM_DEVICE0_ADDR + addr);
        addr++;
    }
}








