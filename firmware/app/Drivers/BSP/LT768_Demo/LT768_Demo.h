/**
 ****************************************************************************************************
 * @file        LT768_Demo.h
 * @version     V1.0
 * @brief       LT7680&LT7381模块 应用测试程序
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    慧勤智远 GD32开发板
 *
 ****************************************************************************************************
 */

#ifndef _LT738_Demo_h
#define _LT738_Demo_h

#include "./BSP/IF_PORT/if_port.h"


#define layer1_start_addr 0                         
#define layer2_start_addr LCD_XSIZE_TFT * LCD_YSIZE_TFT * 2     
#define layer3_start_addr LCD_XSIZE_TFT * LCD_YSIZE_TFT * 2 * 2    
#define layer4_start_addr LCD_XSIZE_TFT * LCD_YSIZE_TFT * 2 * 3    
#define layer5_start_addr LCD_XSIZE_TFT * LCD_YSIZE_TFT * 2 * 4    
#define layer6_start_addr LCD_XSIZE_TFT * LCD_YSIZE_TFT * 2 * 5    
#define layer7_start_addr LCD_XSIZE_TFT * LCD_YSIZE_TFT * 2 * 6  /* 图层在显存的位置 */  

#define Resolution	(LCD_XSIZE_TFT * LCD_YSIZE_TFT)              /* 分辨率 */


void Display_picture(unsigned short x, unsigned short y, unsigned short w, unsigned short h, const unsigned char *bmp); /* 显示图片 */
void GRAY_32(void);  /* 灰阶 */
void WB_block(void); /* 棋盘格 */

#endif




