/**
 ****************************************************************************************************
 * @file        if_port.h
 * @version     V1.0
 * @brief       LT7680 & LT7381模块 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */	

#ifndef _if_port_h
#define _if_port_h

#include "./SYSTEM/sys/sys.h"


/* 选择驱动方式 */
#define GD32_EXMC_16	0		  /* 使用16位8080模式驱动显示模块(LT7381) */
#define GD32_SPI		  1		  /* 使用4线SPI模式驱动显示模块(LT7680) */


/******************************************************************************************/
/* LCD_RST 引脚 定义 */

#define LCD_RST_GPIO_PORT            GPIOJ
#define LCD_RST_GPIO_PIN             GPIO_PIN_11
#define LCD_RST_GPIO_CLK             RCU_GPIOJ     /* GPIOJ时钟使能 */

/* LCD复位引脚 */
#define LCD_RST(x)       do{ x ? \
                             gpio_bit_write(LCD_RST_GPIO_PORT, LCD_RST_GPIO_PIN, SET) : \
                             gpio_bit_write(LCD_RST_GPIO_PORT, LCD_RST_GPIO_PIN, RESET); \
                         }while(0)     
                         
/******************************************************************************************/
/* 选择EXMC总线驱动 */
#if GD32_EXMC_16
      
/* LCD RST/BL/WR/RD/CS/RS 引脚 定义 
 * LCD_D0~D15,由于引脚太多,就不在这里定义了,直接在exmc_init_16里面修改.所以在移植的时候,除了改
 * 这6个IO口, 还得改exmc_init_16里面的D0~D15所在的IO口.
 */

/* LCD复位引脚为开发板LCD接口共用 所以不在这里定义 LCD_RST引脚 */                         
//#define LCD_RST_GPIO_PORT               GPIOJ
//#define LCD_RST_GPIO_PIN                GPIO_PIN_11
//#define LCD_RST_GPIO_CLK                RCU_GPIOJ     /* GPIOJ时钟使能 */
                         
#define LCD_BL_GPIO_PORT                GPIOJ
#define LCD_BL_GPIO_PIN                 GPIO_PIN_9
#define LCD_BL_GPIO_CLK                 RCU_GPIOJ     /* GPIOJ时钟使能 */

#define LCD_WR_GPIO_PORT                GPIOD
#define LCD_WR_GPIO_PIN                 GPIO_PIN_5
#define LCD_WR_GPIO_CLK                 RCU_GPIOD     /* GPIOD时钟使能 */

#define LCD_RD_GPIO_PORT                GPIOD
#define LCD_RD_GPIO_PIN                 GPIO_PIN_4
#define LCD_RD_GPIO_CLK                 RCU_GPIOD     /* GPIOD时钟使能 */

/* LCD_CS(需要根据LCD_EXMC_NEX设置正确的IO口) 和 LCD_RS(需要根据LCD_EXMC_AX设置正确的IO口) 引脚 定义 */
#define LCD_CS_GPIO_PORT                GPIOG
#define LCD_CS_GPIO_PIN                 GPIO_PIN_9
#define LCD_CS_GPIO_CLK                 RCU_GPIOG     /* GPIOG时钟使能 */

#define LCD_RS_GPIO_PORT                GPIOG
#define LCD_RS_GPIO_PIN                 GPIO_PIN_2
#define LCD_RS_GPIO_CLK                 RCU_GPIOG     /* GPIOG时钟使能 */

/* EXMC相关参数 定义 
 * 注意: 我们默认是通过EXMC块0来连接LCD, 块0有4个片选: EXMC_NE0~3
 *
 * 修改LCD_EXMC_NEX, 对应的LCD_CS_GPIO相关设置也得改
 * 修改LCD_EXMC_AX , 对应的LCD_RS_GPIO相关设置也得改
 */
#define LCD_EXMC_NEX             1              /* 使用EXMC_NE1接LCD_CS,取值范围只能是: 0 ~ 3 */
#define LCD_EXMC_AX              12             /* 使用EXMC_A12接LCD_RS,取值范围是: 0 ~ 25 */

#define LCD_EXMC_SNWTCFGX        EXMC_SNWTCFG(LCD_EXMC_NEX)     /* 写时序寄存器(EXMC_SNWTCFGx,x=0,1,2,3),根据LCD_EXMC_NEX自动确定 */

/* LCD_BASE的详细计算方法:
 * 我们一般使用EXMC的块0(BANK0)来驱动TFTLCD液晶屏, 块0地址范围总大小为256MB,均分成4块:
 * 存储块0(EXMC_NE0)地址范围: 0X6000 0000 ~ 0X63FF FFFF
 * 存储块1(EXMC_NE1)地址范围: 0X6400 0000 ~ 0X67FF FFFF
 * 存储块2(EXMC_NE2)地址范围: 0X6800 0000 ~ 0X6BFF FFFF
 * 存储块3(EXMC_NE3)地址范围: 0X6C00 0000 ~ 0X6FFF FFFF
 *
 * 我们需要根据硬件连接方式选择合适的片选(连接LCD_CS)和地址线(连接LCD_RS)
 * 开发板使用EXMC_NE1连接LCD_CS, EXMC_A12连接LCD_RS ,16位数据线,计算方法如下:
 * 首先EXMC_NE1的基地址为: 0X6400 0000;     NEx的基址为(x=0/1/2/3): 0X6000 0000 + (0X400 0000 * x)
 * EXMC_A12对应地址值: 2^12 * 2 = 0X2000;   EXMC_Ay对应的地址为(y = 0 ~ 25): 2^y * 2
 *
 * LCD_BASE0,对应LCD_RS = 0(LCD寄存器); LCD_BASE1,对应LCD_RS = 1(LCD数据)
 * 则 LCD_BASE1的地址为:  0X6400 0000 + 2^12 * 2 = 0X6400 2000
 *    LCD_BASE0的地址可以为 A12 = 0(即LCD_RS = 0)的其他地址.
 * LCD_BASE0和LCD_BASE1均为16位数据宽度
 * 因此 LCD_BASE0的地址可以为LCD_BASE0 = LCD_BASE1 - 2 = 0X6400 2000 - 2
 *
 * 更加通用的计算公式为((片选脚EXMC_NEx)x=0/1/2/3, (RS接地址线EXMC_Ay)y=0~25):
 *          LCD_BASE0 = (0X6000 0000 + (0X400 0000 * x)) | (2^y * 2 - 2)
 *          等效于(使用移位操作)
 *          LCD_BASE0 = (0X6000 0000 + (0X400 0000 * x)) | ((1 << y) * 2 - 2)
 */
#define LCD_BASE0        (uint32_t)((0X60000000 + (0X4000000 * LCD_EXMC_NEX)) | (((1 << LCD_EXMC_AX) * 2) - 2))
#define LCD_BASE1        (uint32_t)((0X60000000 + (0X4000000 * LCD_EXMC_NEX)) | ((1 << LCD_EXMC_AX) * 2))

/* LCD背光控制 */
#define LCD_BL(x)        do{ x ? \
                             gpio_bit_write(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, SET) : \
                             gpio_bit_write(LCD_BL_GPIO_PORT, LCD_BL_GPIO_PIN, RESET); \
                         }while(0)  
                         
#endif

/******************************************************************************************/
/* 选择SPI驱动 */
#if GD32_SPI	

/* SPI0 引脚 定义 */
#define SPI0_CS_GPIO_PORT            GPIOB
#define SPI0_CS_GPIO_PIN             GPIO_PIN_8
#define SPI0_CS_GPIO_CLK             RCU_GPIOB     /* GPIOB时钟使能 */

#define SPI0_SCK_GPIO_PORT           GPIOB
#define SPI0_SCK_GPIO_PIN            GPIO_PIN_3
#define SPI0_SCK_GPIO_AF             GPIO_AF_5
#define SPI0_SCK_GPIO_CLK            RCU_GPIOB     /* GPIOB时钟使能 */

#define SPI0_MISO_GPIO_PORT          GPIOB
#define SPI0_MISO_GPIO_PIN           GPIO_PIN_4
#define SPI0_MISO_GPIO_AF            GPIO_AF_5
#define SPI0_MISO_GPIO_CLK           RCU_GPIOB     /* GPIOB时钟使能 */

#define SPI0_MOSI_GPIO_PORT          GPIOD
#define SPI0_MOSI_GPIO_PIN           GPIO_PIN_7
#define SPI0_MOSI_GPIO_AF            GPIO_AF_5
#define SPI0_MOSI_GPIO_CLK           RCU_GPIOD     /* GPIOD时钟使能 */

/* SPI片选信号 */
#define SPI_CS(x) 	     do{ x ? \
                             gpio_bit_write(SPI0_CS_GPIO_PORT, SPI0_CS_GPIO_PIN, SET) : \
                             gpio_bit_write(SPI0_CS_GPIO_PORT, SPI0_CS_GPIO_PIN, RESET); \
                         }while(0)               

/* SPI总线速度设置 */
#define SPI_SPEED_2         0
#define SPI_SPEED_4         1
#define SPI_SPEED_8         2
#define SPI_SPEED_16        3
#define SPI_SPEED_32        4
#define SPI_SPEED_64        5
#define SPI_SPEED_128       6
#define SPI_SPEED_256       7
                         
#endif
                        
/******************************************************************************************/
/* 函数声明 */

void lcd_port_init(void);                 /* LCD模块接口初始化 */
void LCD_CmdWrite(uint8_t cmd);           /* 向LT7680&LT7381写命令 */
void LCD_DataWrite(uint8_t data);         /* 向LT7680&LT7381写数据 */
void LCD_DataWrite_Pixel(uint16_t data);  /* 向LT7680&LT7381写像素点的数据   */
uint8_t LCD_StatusRead(void);             /* 读状态寄存器 */
uint16_t LCD_DataRead(void);              /* 读指令寄存器数据 */
                         
/* EXMC总线驱动相关函数 */
void exmc_sram_init(void);                
void exmc_init_16(void);
void EXMC_16_CmdWrite(uint8_t cmd);
void EXMC_16_DataWrite(uint8_t data);
void EXMC_16_DataWrite_Pixel(uint16_t data);
uint8_t EXMC_16_StatusRead(void);
uint16_t EXMC_16_DataRead(void);

/* SPI驱动相关函数 */                        
void spi_io_init(void);
void spi0_set_speed(uint8_t speed);
uint8_t spi0_read_write_byte(uint8_t txdata);
void SPI_CmdWrite(uint8_t cmd);
void SPI_DataWrite(uint8_t data);
void SPI_DataWrite_Pixel(uint16_t data);
uint8_t SPI_StatusRead(void);
uint16_t SPI_DataRead(void);
	 
void Delay_us(uint32_t time);  /* 延时函数us级(自定义) */
void Delay_ms(uint32_t time);  /* 延时函数ms级(自定义) */


#endif





