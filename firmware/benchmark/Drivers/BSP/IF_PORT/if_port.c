/**
 ****************************************************************************************************
 * @file        if_port.c
 * @version     V1.0
 * @brief       LT7680 & LT7381模块 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */
 
#include "./BSP/IF_PORT/if_port.h"
#include "./SYSTEM/delay/delay.h"


void Delay_us(uint32_t time)
{    
    delay_us(time);
}

void Delay_ms(uint32_t time)
{    
    delay_ms(time);
}


// -------------------------------------------------------- EXMC总线驱动 -------------------------------------------------------------

#if GD32_EXMC_16

/**
 * @brief       初始化TFTLCD液晶屏的EXMC配置
 * @param       无
 * @retval      无
 */
void exmc_sram_init(void)
{
    exmc_norsram_parameter_struct  nor_init_struct;
    exmc_norsram_timing_parameter_struct  sram_timing_read;
    exmc_norsram_timing_parameter_struct  sram_timing_write;
      
    rcu_periph_clock_enable(RCU_EXMC);   /* 使能EXMC时钟 */
	  
  	/* 配置读时序参数 */
    sram_timing_read.asyn_access_mode = EXMC_ACCESS_MODE_A;            /* 异步访问模式A */
    sram_timing_read.syn_data_latency = EXMC_DATALAT_2_CLK;            /* NOR Flash数据延时，此处未用到 */
    sram_timing_read.syn_clk_division = EXMC_SYN_CLOCK_RATIO_DISABLE;  /* 同步模式时钟分频比，此处未用到  */
    sram_timing_read.bus_latency = 0;                                  /* 总线延迟时间，此处未用到 */
    /* 液晶驱动IC读数据的时候，速度不能太快 */
    sram_timing_read.asyn_data_setuptime = 110;                        /* 数据建立时间(DSET)为110个CK_EXMC 1/300M = 3.3ns * 110 = 363ns */
    sram_timing_read.asyn_address_holdtime = 0;                        /* 地址保持时间(AHLD)模式A未用到 */
    sram_timing_read.asyn_address_setuptime = 15;                      /* 地址建立时间(ASET)为15个CK_EXMC = 3.3ns * 15 = 49.5ns */
  
	  /* 配置写时序参数 */
  	sram_timing_write.asyn_access_mode = EXMC_ACCESS_MODE_A;           /* 异步访问模式A */
    sram_timing_write.syn_data_latency = EXMC_DATALAT_2_CLK;
    sram_timing_write.syn_clk_division = EXMC_SYN_CLOCK_RATIO_DISABLE;
    sram_timing_write.bus_latency = 0;
    /* 某些液晶驱动IC的写信号脉宽，初始化的时候需要设置大一些 */    
    sram_timing_write.asyn_data_setuptime = 15;                        /* 数据建立时间(DSET)为15个CK_EXMC = 3.3ns * 15 = 49.5ns */
    sram_timing_write.asyn_address_holdtime = 0;                  
    sram_timing_write.asyn_address_setuptime = 15;                     /* 地址建立时间(ASET)为15个CK_EXMC = 3.3ns * 15 = 49.5ns */

    /* 配置EXMC参数 */
    exmc_norsram_struct_para_init(&nor_init_struct);                   /* 初始化结构体exmc_norsram_parameter_struct参数为默认值 */
       
    nor_init_struct.norsram_region = EXMC_BANK0_NORSRAM_REGION1;       /* 选择EXMC_BANK0_NORSRAM_REGION1 */   
    nor_init_struct.write_mode = EXMC_ASYN_WRITE;                      /* 选择写操作模式(同步模式或者异步模式)，此处未用到 */
    nor_init_struct.extended_mode = ENABLE;                            /* 使能扩展模式 */
    nor_init_struct.asyn_wait = DISABLE;                               /* 禁用异步等待功能 */
    nor_init_struct.nwait_signal = DISABLE;                            /* 在同步突发模式中，使能或者禁用NWAIT信号，此处未用到 */
    nor_init_struct.memory_write = ENABLE;                             /* 使能EXMC对外部存储器写操作 */
    nor_init_struct.nwait_config = EXMC_NWAIT_CONFIG_BEFORE;           /* NWAIT信号配置，只在同步模式有效，此处未用到 */ 
    nor_init_struct.nwait_polarity = EXMC_NWAIT_POLARITY_LOW;          /* NWAIT信号极性，此处未用到 */
    nor_init_struct.burst_mode = DISABLE;                              /* 禁用突发模式 */
    nor_init_struct.databus_width = EXMC_NOR_DATABUS_WIDTH_16B;        /* 存储器数据总线宽度16位 */
    nor_init_struct.memory_type = EXMC_MEMORY_TYPE_SRAM;               /* 外部存储器的类型为SRAM */
    nor_init_struct.address_data_mux = DISABLE;                        /* 数据线/地址线不复用 */
    nor_init_struct.cram_page_size = EXMC_CRAM_AUTO_SPLIT;             /* 页边界自动突发分割 */
    nor_init_struct.read_write_timing = &sram_timing_read;             /* 读时序参数 */
    nor_init_struct.write_timing = &sram_timing_write;                 /* 写时序参数 */

    exmc_norsram_init(&nor_init_struct);                               /* 初始化EXMC NOR/SRAM bank0 region1 */
	
    exmc_norsram_enable(EXMC_BANK0_NORSRAM_REGION1);                   /* 使能EXMC NOR/SRAM bank0 region1 */
}

/**
 * @brief       初始化EXMC接口
 * @param       无
 * @retval      无
 */
void exmc_init_16(void)
{ 		
    /* IO 及 时钟配置 */
    rcu_periph_clock_enable(LCD_CS_GPIO_CLK);    /* 使能LCD_CS引脚时钟 */
    rcu_periph_clock_enable(LCD_RS_GPIO_CLK);    /* 使能LCD_RS引脚时钟 */
    rcu_periph_clock_enable(LCD_WR_GPIO_CLK);    /* 使能LCD_WR引脚时钟 */
    rcu_periph_clock_enable(LCD_RD_GPIO_CLK);    /* 使能LCD_RD引脚时钟 */
  
    rcu_periph_clock_enable(RCU_GPIOD);          /* 使能GPIOD时钟 */
    rcu_periph_clock_enable(RCU_GPIOE);          /* 使能GPIOE时钟 */    

    /* 设置LCD_CS引脚(EXMC_NEX)  复用推挽输出 */
    gpio_af_set(LCD_CS_GPIO_PORT, GPIO_AF_12, LCD_CS_GPIO_PIN);
    gpio_mode_set(LCD_CS_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, LCD_CS_GPIO_PIN);
    gpio_output_options_set(LCD_CS_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, LCD_CS_GPIO_PIN);
	
    /* 设置LCD_RS引脚(EXMC_AX)  复用推挽输出 */
    gpio_af_set(LCD_RS_GPIO_PORT, GPIO_AF_12, LCD_RS_GPIO_PIN);
    gpio_mode_set(LCD_RS_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, LCD_RS_GPIO_PIN);
    gpio_output_options_set(LCD_RS_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, LCD_RS_GPIO_PIN); 

    /* 设置LCD_WR引脚(EXMC_NWE)  复用推挽输出 */
    gpio_af_set(LCD_WR_GPIO_PORT, GPIO_AF_12, LCD_WR_GPIO_PIN);
    gpio_mode_set(LCD_WR_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, LCD_WR_GPIO_PIN);
    gpio_output_options_set(LCD_WR_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, LCD_WR_GPIO_PIN);
	
    /* 设置LCD_RD引脚(EXMC_NOE)  复用推挽输出 */
    gpio_af_set(LCD_RD_GPIO_PORT, GPIO_AF_12, LCD_RD_GPIO_PIN);
    gpio_mode_set(LCD_RD_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, LCD_RD_GPIO_PIN);
    gpio_output_options_set(LCD_RD_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, LCD_RD_GPIO_PIN); 
    
    /* 设置PD0,1,8,9,10,14,15  复用推挽输出 */
	  gpio_af_set(GPIOD, GPIO_AF_12, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |  GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |  GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |  GPIO_PIN_14 | GPIO_PIN_15);

    /* 设置PE7~15  复用推挽输出 */
    gpio_af_set(GPIOE, GPIO_AF_12, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_mode_set(GPIOE, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
    gpio_output_options_set(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
        
    exmc_sram_init();           /* 初始化EXMC NOR/SRAM配置 */
    Delay_ms(50);
}

void EXMC_16_CmdWrite(uint8_t cmd)
{
    cmd = cmd;
    *(volatile uint16_t *)(LCD_BASE0) = (cmd);
}

void EXMC_16_DataWrite(uint8_t data)
{
  	data = data;
  	*(volatile uint16_t *)(LCD_BASE1) = (data);
}

void EXMC_16_DataWrite_Pixel(uint16_t data)
{
    data = data;
    *(volatile uint16_t *)(LCD_BASE1) = (data);
}

uint8_t EXMC_16_StatusRead(void)
{
    uint16_t temp = 0;
    temp = *(volatile uint16_t *)(LCD_BASE0);
    return temp;
}

uint16_t EXMC_16_DataRead(void)
{
    uint16_t temp = 0;
    temp = *(volatile uint16_t *)(LCD_BASE1);
    return temp;
}

#endif

// -------------------------------------------------------- SPI驱动 -------------------------------------------------------------

#if GD32_SPI


/**
 * @brief       SPI接口初始化
 * @param       无
 * @retval      无
 */
void spi_io_init(void)
{    
	  spi_parameter_struct spi_init_struct;

    rcu_periph_clock_enable(SPI0_CS_GPIO_CLK);         /* 使能SPI0_CS引脚时钟 */
    rcu_periph_clock_enable(SPI0_SCK_GPIO_CLK);        /* 使能SPI0_SCK引脚时钟 */
    rcu_periph_clock_enable(SPI0_MISO_GPIO_CLK);       /* 使能SPI0_MISO引脚时钟 */
    rcu_periph_clock_enable(SPI0_MOSI_GPIO_CLK);       /* 使能SPI0_MOSI引脚时钟 */
    rcu_periph_clock_enable(RCU_SPI0);                 /* 使能SPI0时钟 */  
    rcu_spi_clock_config(IDX_SPI0, RCU_SPISRC_PER);    /* 配置SPI0时钟源选择，选择CK_PER时钟作为SPI0源时钟 */
  
    /* 配置使用的SPI0引脚:
             SPI0_CS->PB8
             SPI0_SCK->PB3
             SPI0_MISO->PB4
             SPI0_MOSI->PD7 */

    /* 配置SPI0引脚的复用功能 */
    gpio_af_set(SPI0_SCK_GPIO_PORT, SPI0_SCK_GPIO_AF, SPI0_SCK_GPIO_PIN);  
    gpio_af_set(SPI0_MISO_GPIO_PORT, SPI0_MISO_GPIO_AF, SPI0_MISO_GPIO_PIN);
    gpio_af_set(SPI0_MOSI_GPIO_PORT, SPI0_MOSI_GPIO_AF, SPI0_MOSI_GPIO_PIN);
    
    /* SPI0_CS引脚模式设置 推挽输出 */
    gpio_mode_set(SPI0_CS_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, SPI0_CS_GPIO_PIN);
    gpio_output_options_set(SPI0_CS_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, SPI0_CS_GPIO_PIN);

    /* SPI0_SCK引脚模式设置 复用推挽输出 */
    gpio_mode_set(SPI0_SCK_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SPI0_SCK_GPIO_PIN);
    gpio_output_options_set(SPI0_SCK_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, SPI0_SCK_GPIO_PIN);

    /* SPI0_MISO引脚模式设置 复用推挽输出 */
    gpio_mode_set(SPI0_MISO_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SPI0_MISO_GPIO_PIN);
    gpio_output_options_set(SPI0_MISO_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, SPI0_MISO_GPIO_PIN);

    /* SPI0_MOSI引脚模式设置 复用推挽输出 */
    gpio_mode_set(SPI0_MOSI_GPIO_PORT, GPIO_MODE_AF, GPIO_PUPD_NONE, SPI0_MOSI_GPIO_PIN);
    gpio_output_options_set(SPI0_MOSI_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, SPI0_MOSI_GPIO_PIN);

    spi_i2s_deinit(SPI0);                                              /* 复位SPI0 */
    spi_struct_para_init(&spi_init_struct);                            /* 初始化SPI结构体中所有参数为默认值 */
        
	  spi_init_struct.trans_mode           = SPI_TRANSMODE_FULLDUPLEX;   /* 传输模式配置:设置为全双工模式 */
    spi_init_struct.device_mode          = SPI_MASTER;                 /* 主机或设备模式配置;设置为主机模式 */
    spi_init_struct.data_size            = SPI_DATASIZE_8BIT;          /* 数据帧格式配置;8位数据帧格式 */ 
    spi_init_struct.clock_polarity_phase = SPI_CK_PL_HIGH_PH_2EDGE;    /* 空闲状态下,CLK保持高电平,在第二个时钟跳变沿采集第一个数据 */
    spi_init_struct.nss                  = SPI_NSS_SOFT;               /* NSS由软件控制:NSS输入值由SPI_CTL0寄存器的NSSI位决定 */
    spi_init_struct.prescale             = SPI_PSC_2;                  /* 预分频器配置;设置SPI时钟预分频值为2 */
    spi_init_struct.endian               = SPI_ENDIAN_MSB;             /* 大端或小端模式配置;先发送最高有效位 */  
    spi_init(SPI0, &spi_init_struct);                                  /* 初始化SPI0 */
    
    spi_byte_access_enable(SPI0);                                      /* 使能SPI字节访问模式 */
    
    spi_nss_output_enable(SPI0);                                       /* 主机模式NSS输出使能 */
    
    spi_enable(SPI0);	                                                 /* 使能SPI0 */	
}

/**
 * @brief       SPI0速度设置函数
 * @note        SPI0时钟源选择CK_PER, 为64Mhz
 *              CK_SPI = SPI_KERCLK / 2^(speed + 1)
 * @param       speed: SPI时钟分频系数
 * @retval      无
 */
void spi0_set_speed(uint8_t speed)
{
	  speed &= 0X07;                   /* 限制范围 */
	  spi_disable(SPI0);	             /* SPI失能 */
	  SPI_CFG0(SPI0) &= ~(7 << 28);    /* 先清零原来的分频设置 */
	  SPI_CFG0(SPI0) |= speed << 28;   /* 设置SPI时钟分频系数 */
	  spi_enable(SPI0);	               /* SPI使能 */
}

/**
 * @brief       SPI0读写一个字节数据
 * @param       txdata: 要发送的数据(1字节)
 * @retval      接收到的数据(1字节)
 */
uint8_t spi0_read_write_byte(uint8_t txdata)
{   
    uint8_t rxdata = 0;
  
    spi_master_transfer_start(SPI0, SPI_TRANS_START);       /* 主机启动传输 */
        
	  while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_TP));   /* 等待发送缓冲区空 */

	  spi_i2s_data_transmit(SPI0, txdata);                    /* 发送一个字节数据 */
   
	  while (RESET == spi_i2s_flag_get(SPI0, SPI_FLAG_RP));   /* 等待接收缓冲区非空 */
    
	  rxdata = spi_i2s_data_receive(SPI0);                    /* 接收一个字节数据 */
  	 
    return rxdata;                                          /* 返回收到的数据 */
}

void SPI_CmdWrite(uint8_t cmd)
{  
    SPI_CS(0);		
    spi0_read_write_byte(0x00);      /* 代表MCU要写入指令寄存器地址 */
    spi0_read_write_byte(cmd);
    SPI_CS(1);		
}

void SPI_DataWrite(uint8_t data)
{ 

    SPI_CS(0);	
    spi0_read_write_byte(0x80);      /* 代表MCU要写入数据到寄存器或是显示内存中 */
    spi0_read_write_byte(data);
    SPI_CS(1);
}

void SPI_DataWrite_Pixel(uint16_t data)
{ 

    SPI_CS(0);	
    spi0_read_write_byte(0x80);
    spi0_read_write_byte(data);	
    spi0_read_write_byte(data >> 8);	
    SPI_CS(1);
}

uint8_t SPI_StatusRead(void)
{
    uint8_t temp = 0;	
  
    SPI_CS(0);	
    spi0_read_write_byte(0x40);      /* 代表MCU要读取状态寄存器的数据 */
    temp = spi0_read_write_byte(0xff);
    SPI_CS(1);
  
    return temp;
}

uint16_t SPI_DataRead(void)
{
    uint16_t temp = 0;	
    
    SPI_CS(0);	
    spi0_read_write_byte(0xc0);      /* 代表MCU要读取指令寄存器的数据 */
    temp = spi0_read_write_byte(0xff);
    SPI_CS(1);
  
    return temp;
}

#endif



//-----------------------------------------------------------------------------------------------------------------------------------
/* 不管使用哪种接口，这些接口主要要实现以下5个函数的功能，
 * 而LT7680&LT7381的其他的功能都是基于这5个函数来实现的。
 */

/**
 * @brief       向LT7680&LT7381写命令 
 * @param       cmd: 要写入的命令
 * @retval      无
 */
void LCD_CmdWrite(uint8_t cmd)
{
#if GD32_EXMC_16
    EXMC_16_CmdWrite(cmd);
#endif
    
#if GD32_SPI
    SPI_CmdWrite(cmd);
#endif
}

/**
 * @brief       向LT7680&LT7381写数据
 * @param       data: 要写入的数据
 * @retval      无
 */
void LCD_DataWrite(uint8_t data)
{
#if GD32_EXMC_16
    EXMC_16_DataWrite(data);
#endif
    
#if GD32_SPI
    SPI_DataWrite(data);
#endif
}

/**
 * @brief       向LT7680&LT7381写像素点的数据  
 * @param       data: 像素点的颜色值
 * @retval      无
 */
void LCD_DataWrite_Pixel(uint16_t data)
{	
#if GD32_EXMC_16
    EXMC_16_DataWrite_Pixel(data);
#endif
    
#if GD32_SPI
    SPI_DataWrite_Pixel(data);
#endif
}

/**
 * @brief       读状态寄存器 
 * @param       无
 * @retval      读取到的状态寄存器值
 */
uint8_t LCD_StatusRead(void)
{
    uint8_t temp = 0;
      
#if GD32_EXMC_16
    temp = EXMC_16_StatusRead();
#endif
    
#if GD32_SPI
    temp = SPI_StatusRead();
#endif
    
    return temp;
}

/**
 * @brief       读寄存器数据
 * @param       无
 * @retval      读取到的寄存器或内存的数据
 */
uint16_t LCD_DataRead(void)
{
    uint16_t temp = 0;
      
#if GD32_EXMC_16
    temp = EXMC_16_DataRead();
#endif
    
#if GD32_SPI
    temp = SPI_DataRead();
#endif
    
    return temp;
}
	  
/**
 * @brief       LCD模块接口初始化 
 * @param       无
 * @retval      无
 */
void lcd_port_init(void)
{	   
#if GD32_EXMC_16
    exmc_init_16();
#endif
    
#if GD32_SPI
    spi_io_init();
#endif
}








