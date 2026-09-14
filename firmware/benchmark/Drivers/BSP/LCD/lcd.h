/**
 ****************************************************************************************************
 * @file        lcd.h
 * @version     V1.0
 * @brief       LCD显示应用函数 代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */

#ifndef __LCD_H
#define __LCD_H

#include "stdlib.h"
#include "./SYSTEM/sys/sys.h"
#include "./BSP/LT768_Lib/LT768_Lib.h"
#include "./BSP/LT768/LT768.h"
#include "./BSP/LT768_Demo/LT768_Demo.h"
#include "./BSP/IF_PORT/if_port.h"


/******************************************************************************************/

/* LCD重要参数集 */
typedef struct
{
    uint16_t width;     /* LCD 宽度 */
    uint16_t height;    /* LCD 高度 */
    uint16_t id;        /* LCD ID */
    uint8_t dir;        /* 横屏还是竖屏控制：0，竖屏；1，横屏。 */
} _lcd_dev;

/* LCD参数 */
extern _lcd_dev lcddev; /* 管理LCD重要参数 */

/* LCD的画笔颜色和背景色 */
extern uint32_t  g_point_color;     /* 默认红色 */
extern uint32_t  g_back_color;      /* 背景颜色,默认为白色 */ 


/******************************************************************************************/
/* MCU写入内存方向和LCD颜色 定义 */

/* 写入内存方向定义 */
#define L2R_U2D         0           /* 从左到右,从上到下 */
#define R2L_U2D         1           /* 从右到左,从上到下 */

#define U2D_L2R         2           /* 从上到下,从左到右 */
#define D2U_L2R         3           /* 从下到上,从左到右 */

#define DFT_SCAN_DIR    L2R_U2D     /* 默认的写入方向 */

/* 常用画笔颜色 */
#define WHITE           0xFFFF      /* 白色 */
#define BLACK           0x0000      /* 黑色 */
#define RED             0xF800      /* 红色 */
#define GREEN           0x07E0      /* 绿色 */
#define BLUE            0x001F      /* 蓝色 */ 
#define MAGENTA         0XF81F      /* 品红色/紫红色 = BLUE + RED */
#define YELLOW          0XFFE0      /* 黄色 = GREEN + RED */
#define CYAN            0X07FF      /* 青色 = GREEN + BLUE */   

/* 非常用颜色 */
#define BROWN           0XBC40      /* 棕色 */
#define BRRED           0XFC07      /* 棕红色 */
#define GRAY            0X8430      /* 灰色 */ 
#define DARKBLUE        0X01CF      /* 深蓝色 */
#define LIGHTBLUE       0X7D7C      /* 浅蓝色 */ 
#define GRAYBLUE        0X5458      /* 灰蓝色 */ 
#define LIGHTGREEN      0X841F      /* 浅绿色 */  
#define LGRAY           0XC618      /* 浅灰色(PANNEL),窗体背景色 */ 
#define LGRAYBLUE       0XA651      /* 浅灰蓝色(中间层颜色) */ 
#define LBBLUE          0X2B12      /* 浅棕蓝色(选择条目的反色) */ 

/******************************************************************************************/
/* 函数声明 */

void lcd_init(void);                        /* 初始化LCD */ 
void lcd_display_on(void);                  /* 开显示 */ 
void lcd_display_off(void);                 /* 关显示 */
void lcd_scan_dir(uint8_t dir);             /* 设置屏幕扫描方向 */ 
void lcd_display_dir(uint8_t dir);          /* 设置屏幕显示方向 */ 
void lcd_bl_on(void);                       /* 点亮背光 */

void lcd_write_ram_prepare(void);                              /* 准备写GRAM */ 
void lcd_set_cursor(uint16_t x, uint16_t y);                   /* 设置光标 */ 
uint32_t lcd_rgb565torgb888(uint16_t rgb565);                  /* 将RGB565转换为RGB888 */
uint32_t lcd_read_point(uint16_t x, uint16_t y);               /* 读点 */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color);   /* 画点 */

void lcd_clear(uint32_t color);                                                                /* LCD清屏 */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint32_t color);                      /* 填充实心圆 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint32_t color);                     /* 画圆 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint32_t color);                     /* 画水平线 */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height);                /* 设置窗口 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color);             /* 纯色填充矩形 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);      /* 彩色填充矩形 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);        /* 画直线 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color);   /* 画矩形 */

void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint32_t color);                       /* 显示一个字符 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint32_t color);                     /* 显示数字 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint32_t color);      /* 扩展显示数字 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint32_t color);   /* 显示字符串 */


#endif









