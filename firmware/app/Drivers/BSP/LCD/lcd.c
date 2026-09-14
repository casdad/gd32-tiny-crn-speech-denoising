/**
 ****************************************************************************************************
 * @file        lcd.c
 * @version     V1.0
 * @brief       LCD显示应用函数 代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */
 
#include "stdlib.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/LCD/lcdfont.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"


/* LCD的画笔颜色和背景色 */
uint32_t g_point_color = RED;            /* 画笔颜色 */
uint32_t g_back_color  = 0XFFFFFFFF;     /* 背景色 */

/* 管理LCD重要参数 */
_lcd_dev lcddev;


/**
 * @brief       LCD延时函数,仅用于部分在mdk -O1时间优化时需要设置的地方
 * @param       t:延时的数值
 * @retval      无
 */
void lcd_opt_delay(uint32_t i)
{
    while (i--);
}

/**
 * @brief       准备写GRAM
 * @param       无
 * @retval      无
 */
void lcd_write_ram_prepare(void)
{
    LCD_CmdWrite(0x04);
}

/**
 * @brief       颜色转换
 * @note        将RGB565颜色转换成RGB888颜色
 * @param       rgb565 : RGB565颜色
 * @retval      RGB888颜色值
 */
uint32_t lcd_rgb565torgb888(uint16_t rgb565)
{
    uint16_t r, g, b;
    uint32_t rgb888;
  
    r = (rgb565 & 0XF800) >> 8;
    g = (rgb565 & 0X07E0) >> 3;
    b = (rgb565 & 0X001F) << 3;
  
    rgb888 = (r << 16) | (g << 8) | b;
  
    return rgb888;
}

/**
 * @brief       读取某个点的颜色值
 * @param       x,y:坐标
 * @retval      此点的颜色
 */
uint32_t lcd_read_point(uint16_t x, uint16_t y)
{
#if GD32_EXMC_16

    volatile uint16_t color;  
  
    lcd_set_cursor(x, y);
    LCD_CmdWrite(0x04);      
    LCD_DataRead();
    color = LCD_DataRead();
  
    return  color;
  
#endif

#if GD32_SPI
/* SPI读颜色数据的时候，速度不能太快，
 * 如果读出来的颜色数据不正确，可以加大分频系数降低SPI时钟频率。
 */
    uint16_t a, b;  
  
    lcd_set_cursor(x, y);
    LCD_CmdWrite(0x04); 

    SPI_CS(0);	
    spi0_read_write_byte(0xc0);      
    spi0_read_write_byte(0xff);
    a = spi0_read_write_byte(0xff);
    b = spi0_read_write_byte(0xff);
    SPI_CS(1);  
  
    return  a | (b << 8);  
  
#endif  
}

/**
 * @brief       LCD开启显示
 * @param       无
 * @retval      无
 */
void lcd_display_on(void)
{
    Display_ON();
}

/**
 * @brief       LCD关闭显示
 * @param       无
 * @retval      无
 */
void lcd_display_off(void)
{
    Display_OFF();
}

/**
 * @brief       设置光标位置
 * @param       x,y: 坐标
 * @retval      无
 */
void lcd_set_cursor(uint16_t x, uint16_t y)
{
		LCD_CmdWrite(0x5F);
		LCD_DataWrite(x);  
		LCD_CmdWrite(0x60);	   
		LCD_DataWrite(x >> 8);
		LCD_CmdWrite(0x61);
		LCD_DataWrite(y);
		LCD_CmdWrite(0x62);	   
		LCD_DataWrite(y >> 8);	
}

/**
 * @brief       设置MCU写入内存方向(仅用于底图寻址模式为Block 模式)
 * @note
 *              注意:其他函数可能会受到此函数设置的影响,
 *              所以,一般设置为L2R_U2D即可,如果设置为其他扫描方式,可能导致显示不正常.
 *
 * @param       dir:0~3,代表4个方向(具体定义见lcd.h)
 * @retval      无
 */
void lcd_scan_dir(uint8_t dir)
{
    switch (dir)   /* 写入内存方向 */
    {
        case L2R_U2D:
            MemWrite_Left_Right_Top_Down();   /* 从左到右,从上到下 */
            break;

        case R2L_U2D:
            MemWrite_Right_Left_Top_Down();   /* 从右到左,从上到下 */
            break;

        case U2D_L2R:
            MemWrite_Top_Down_Left_Right();   /* 从上到下,从左到右 */
            break;

        case D2U_L2R:
            MemWrite_Down_Top_Left_Right();   /* 从下到上,从左到右 */
            break;
    }
    
    if (dir < 2)
    {
        lcddev.width = LCD_XSIZE_TFT;
        lcddev.height = LCD_YSIZE_TFT;     
    }
    else
    {
        lcddev.width = LCD_YSIZE_TFT;
        lcddev.height = LCD_XSIZE_TFT;        
    }
}

/**
 * @brief       画点
 * @param       x,y: 坐标
 * @param       color: 点的颜色
 * @retval      无
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint32_t color)
{
    lcd_set_cursor(x, y);         /* 设置光标位置 */
    lcd_write_ram_prepare();      /* 开始写入GRAM */
    LCD_DataWrite_Pixel(color);
}

/**
 * @brief       设置LCD显示方向
 * @param       dir:屏幕显示方向
 *   @arg       0,竖屏显示,写入方向:D2U_L2R;
 *   @arg       1,横屏显示,写入方向:L2R_U2D;
 * @retval      无
 */
void lcd_display_dir(uint8_t dir)
{
    lcddev.dir = dir;          /* 竖屏/横屏 */

    if (dir == 0)              /* 竖屏显示 */
    {
        MemWrite_Down_Top_Left_Right();     
        lcddev.width = LCD_YSIZE_TFT;
        lcddev.height = LCD_XSIZE_TFT;              
    }  
    else                       /* 横屏显示 */
    {
        MemWrite_Left_Right_Top_Down();    
        lcddev.width = LCD_XSIZE_TFT;
        lcddev.height = LCD_YSIZE_TFT;   
    }
}

/**
 * @brief       设置窗口,并自动设置画点坐标到窗口左上角(sx,sy).
 * @param       sx,sy:窗口起始坐标(左上角)
 * @param       width,height:窗口宽度和高度,必须大于0!!
 * @note        窗体大小:width*height.
 *
 * @retval      无
 */
void lcd_set_window(uint16_t sx, uint16_t sy, uint16_t width, uint16_t height)
{
		Active_Window_XY(sx, sy);							/* 设定工作窗口的坐标 */
		Active_Window_WH(width, height);	    /* 设定工作窗口的宽度（必须为4的倍数）和高度 */
		lcd_set_cursor(sx, sy); 
}

/**
 * @brief       设置显示窗口
 * @param       无
 * @retval      无
 */
void lcd_init(void)
{
    Select_Main_Window_16bpp();						          /* 选择显示颜色深度 */
    Main_Image_Start_Address(layer1_start_addr);	  /* 设定主窗口起始地址（SDRAM中显示到屏幕的内容的起始地址） */
    Main_Image_Width(LCD_XSIZE_TFT);			        	/* 设定主窗口的宽度 */
    Main_Window_Start_XY(0, 0);						          /* 设定主窗口显示的起始坐标 */
    Canvas_Image_Start_address(layer1_start_addr);	/* 设定写入到SDRAM的起始地址 */
    Canvas_image_width(LCD_XSIZE_TFT);				      /* 设定写入SDRAM的宽度（必须为4的倍数） */
    Active_Window_XY(0, 0);							            /* 设定工作窗口的坐标 */
    Active_Window_WH(LCD_XSIZE_TFT, LCD_YSIZE_TFT);	/* 设定工作窗口的宽度（必须为4的倍数）和高度 */			 
    delay_ms(50);

    lcddev.width = LCD_XSIZE_TFT;
    lcddev.height = LCD_YSIZE_TFT;  

#if GD32_SPI
    Select_SFI_Dual_Mode0();  /* 使用快速读FLASH命令(3Bh):参看W25Q128的读取数据指令3Bh */    
#endif

    lcd_bl_on();              /* 点亮背光 */
    lcd_clear(WHITE);  
}

/**
 * @brief       点亮背光
 * @param       无
 * @retval      无
 */
void lcd_bl_on(void)
{
#if GD32_EXMC_16   /* LT7381模块(使用EXMC驱动)使用LCD_BL引脚控制背光 */
  
    rcu_periph_clock_enable(LCD_BL_GPIO_CLK);     /* 使能LCD_BL引脚时钟 */
    
    /* 设置LCD_BL引脚 推挽输出 */
    gpio_mode_set(LCD_BL_GPIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, LCD_BL_GPIO_PIN);
    gpio_output_options_set(LCD_BL_GPIO_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, LCD_BL_GPIO_PIN);    
  
	  LCD_BL(1);                                    /* 点亮背光 */  
#endif
  
#if GD32_SPI       /* LT7680模块(SPI接口驱动)使用内部PWM控制背光 */
		LT768_PWM1_Init(1, 0, 100, 100, 100);         /* 启用PWM1打开背光 */
#endif 
}

/**
 * @brief       清屏函数
 * @param       color: 要清屏的颜色
 * @retval      无
 */
void lcd_clear(uint32_t color)
{  
    LT768_DrawSquare_Fill(0, 0, LCD_XSIZE_TFT - 1, LCD_YSIZE_TFT - 1, color);	
}

/**
 * @brief       在指定区域内填充单个颜色
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 * @param       color: 要填充的颜色
 * @retval      无
 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint32_t color)
{  
    LT768_DrawSquare_Fill(sx, sy, ex, ey, color);
}

/**
 * @brief       在指定区域内填充指定颜色块
 * @note        此函数仅支持垂直扫描方向为从上到下，写入内存方向为L2R_U2D的颜色数组填充.
 * @param       (sx,sy),(ex,ey):填充矩形对角坐标,区域大小为:(ex - sx + 1) * (ey - sy + 1)
 * @param       color: 要填充的颜色数组首地址
 * @retval      无
 */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{  
    uint16_t height, width;
    uint16_t i, j;
  
    width = ex - sx + 1;                               /* 得到填充的宽度 */
    height = ey - sy + 1;                              /* 高度 */

    for (i = 0; i < height; i++)
    {
        lcd_set_cursor(sx, sy + i);                    /* 设置光标位置 */
        lcd_write_ram_prepare();                       /* 开始写入GRAM */

        for (j = 0; j < width; j++)
        {
            LCD_DataWrite_Pixel(color[i * width + j]); /* 写入数据 */
        }
    }
}

/**
 * @brief       画线
 * @param       x1,y1: 起点坐标
 * @param       x2,y2: 终点坐标
 * @param       color: 线的颜色
 * @retval      无
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    LT768_DrawLine(x1, y1, x2, y2, color);
}

/**
 * @brief       画水平线
 * @param       x,y  : 起点坐标
 * @param       len  : 线长度
 * @param       color: 矩形的颜色
 * @retval      无
 */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint32_t color)
{
    if ((len == 0) || (x > lcddev.width) || (y > lcddev.height)) return;

    LT768_DrawLine(x, y, x + len - 1, y, color);
}

/**
 * @brief       画矩形
 * @param       x1,y1: 起点坐标
 * @param       x2,y2: 终点坐标
 * @param       color: 矩形的颜色
 * @retval      无
 */
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    LT768_DrawSquare(x1, y1, x2, y2, color);
}

/**
 * @brief       画圆
 * @param       x0,y0: 圆中心坐标
 * @param       r    : 半径
 * @param       color: 圆的颜色
 * @retval      无
 */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint32_t color)
{
    LT768_DrawCircle(x0, y0, r, color);
}

/**
 * @brief       填充实心圆
 * @param       x,y  : 圆中心坐标
 * @param       r    : 半径
 * @param       color: 圆的颜色
 * @retval      无
 */
void lcd_fill_circle(uint16_t x, uint16_t y, uint16_t r, uint32_t color)
{
    LT768_DrawCircle_Fill(x, y, r, color);
}

/**
 * @brief       在指定位置显示一个字符
 * @param       x,y   : 坐标
 * @param       chr   : 要显示的字符:' '--->'~'
 * @param       size  : 字体大小 16/24/32
 * @param       mode  : 叠加方式(1); 非叠加方式(0);
 * @param       color : 字符的颜色;
 * @retval      无
 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint32_t color)
{
    char temp[2] = {0};
		temp[0] = chr;
    
	  LT768_Select_Internal_Font_Init(size, 1, 1, mode, 1); 
    LT768_Print_Internal_Font_String(x, y, color, g_back_color, temp);	
}

/**
 * @brief       平方函数, m^n
 * @param       m: 底数
 * @param       n: 指数
 * @retval      m的n次方
 */
static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;

    while (n--)
    {    
        result *= m;
    }

    return result;
}

/**
 * @brief       显示len个数字(高位为0则不显示)
 * @param       x,y   : 起始坐标
 * @param       num   : 数值(0 ~ 2^32)
 * @param       len   : 显示数字的位数
 * @param       size  : 选择字体 16/24/32
 * @param       color : 数字的颜色;
 * @retval      无
 */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint32_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                               /* 按总显示位数循环 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                       /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                   /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);  /* 显示空格,占位 */
                continue;                                                   /* 继续下个一位 */
            }
            else
            {
                enshow = 1;                                                 /* 使能显示 */
            }
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);   /* 显示字符 */
    }
}

/**
 * @brief       扩展显示len个数字(高位是0也显示)
 * @param       x,y   : 起始坐标
 * @param       num   : 数值(0 ~ 2^32)
 * @param       len   : 显示数字的位数
 * @param       size  : 选择字体 16/24/32
 * @param       mode  : 显示模式
 *              [7]:0,不填充;1,填充0.
 *              [6:1]:保留
 *              [0]:0,非叠加显示;1,叠加显示.
 * @param       color : 数字的颜色;
 * @retval      无
 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint32_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;

    for (t = 0; t < len; t++)                                                            /* 按总显示位数循环 */
    {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;                                    /* 获取对应位的数字 */

        if (enshow == 0 && t < (len - 1))                                                /* 没有使能显示,且还有位要显示 */
        {
            if (temp == 0)
            {
                if (mode & 0X80)                                                         /* 高位需要填充0 */
                {
                    lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0X01, color); /* 用0占位 */
                }
                else
                {
                    lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0X01, color); /* 用空格占位 */
                }

                continue;
            }
            else
            {
                enshow = 1;                                                              /* 使能显示 */
            }
        }

        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0X01, color);
    }
}

/**
 * @brief       显示字符串
 * @param       x,y         : 起始坐标
 * @param       width,height: 区域大小
 * @param       size        : 选择字体 16/24/32
 * @param       p           : 字符串首地址
 * @param       color       : 字符串的颜色;
 * @retval      无
 */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint32_t color)
{
    uint8_t x0 = x;
  
    width += x;
    height += y;

    while ((*p <= '~') && (*p >= ' '))   /* 判断是不是非法字符! */
    {
        if (x >= width)
        {
            x = x0;
            y += size;
        }

        if (y >= height) 
        {
            break;                       /* 退出 */
        }

        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}








