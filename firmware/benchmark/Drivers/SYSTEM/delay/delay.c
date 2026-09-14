/**
 ****************************************************************************************************
 * @file        delay.c
 * @version     V1.1
 * @brief       使用SysTick的普通计数模式进行延时管理(支持ucosii)
 *              提供delay_init初始化函数，delay_us和delay_ms等延时函数
 ****************************************************************************************************
 *
 * V1.1
 * 修改OS_SUPPORT_OS部分代码, 默认仅支持UCOSII 2.93.01版本, 其他OS请参考实现
 * 修改delay_init使用系统时钟
 * 修改delay_us使用时钟读取法计时, 支持OS
 * 修改delay_ms直接使用delay_us计时实现.
 *
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"


static uint32_t g_fac_us = 0;      /* us延时倍乘数 */

/* 如果SYS_SUPPORT_OS定义了，则表示要支持OS(目前支持UCOS) */
#if SYS_SUPPORT_OS

/* 包含相关头文件 (ucos需要使用到) */
#include "os.h"

/* 定义g_fac_ms变量, 代表ms延时的倍乘数, 代表每个节拍的毫秒数, (仅在支持os的时候才需要使用到) */
static uint16_t g_fac_ms = 0;

/*
 *  当delay_us/delay_ms需要支持OS的时候，需要借助OS相关的宏定义和函数来支持
 *  这里定义3个宏定义:
 *      delay_osrunning    : 用于表示OS当前是否正在运行，用来决定是否可以使用相关函数
 *      delay_ostickspersec: 用于表示OS扫描的时钟节拍，delay_init将根据该值来初始化systick
 *      delay_osintnesting : 用于表示OS中断嵌套级别，为嵌套中断时不能调用delay_ms使用该值来决定如何处理
 *  然后定义三个函数:
 *      delay_osschedlock  : 用于执行OS任务调度禁止调度器
 *      delay_osschedunlock: 用于恢复OS任务调度器开启调度
 *      delay_ostimedly    : 用于OS延时，可以引发任务调度
 *
 *  本例子仅支持UCOSII,其他OS请参考移植
 */

/* 支持UCOSII */
#define delay_osrunning     OSRunning           /* OS运行标志,0,未运行;1,正在运行 */
#define delay_ostickspersec OS_TICKS_PER_SEC    /* OS时钟节拍,即每秒调度次数 */
#define delay_osintnesting  OSIntNesting        /* 中断嵌套级别,即中断嵌套次数 */


/**
 * @brief     us级延时时,关闭任务调度,防止打断us级延时
 * @param     无
 * @retval    无
 */
void delay_osschedlock(void)
{
    OSSchedLock();                  /* UCOSII的方式,禁止调度,防止打断us延时 */
}

/**
 * @brief     us级延时时,恢复任务调度
 * @param     无
 * @retval    无
 */
void delay_osschedunlock(void)
{
    OSSchedUnlock();                /* UCOSII的方式,恢复调度 */
}

/**
 * @brief     os延时,可以引发任务调度
 * @param     ticks: 延时的节拍数
 * @retval    无
 */
void delay_ostimedly(uint32_t ticks)
{
    OSTimeDly(ticks);               /* UCOSII延时 */
}

/**
 * @brief     systick中断服务函数,使用OS时用到
 * @param     无
 * @retval    无
 */
void SysTick_Handler(void)
{
    if (delay_osrunning == OS_TRUE) /* OS已经开始运行,正常进行调度处理 */
    {
        OS_CPU_SysTickHandler();    /* 调用 uC/OS-II 的SysTick 中断服务函数 */
    }
}

#endif

/**
 * @brief     初始化延时函数
 * @param     sysclk: 系统时钟频率, 即CPU频率(HCLK), 单位 Mhz
 * @retval    无
 */
void delay_init(uint16_t sysclk)
{
#if SYS_SUPPORT_OS                               /* 如果需要支持OS */
    uint32_t reload;
#endif
    SysTick->CTRL |= (1 << 2);                   /* SYSTICK使用系统时钟源,频率为HCLK */
    g_fac_us = sysclk;                           /* 不论是否支持OS,g_fac_us都需要使用,作为1us的基础时基 */
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;    /* 使能SysTick */
    SysTick->LOAD = 0XFFFFFF;                    /* 注意SysTick计数器为24位，所以最大值为0XFFFFFF */
#if SYS_SUPPORT_OS                               /* 如果需要支持OS */
    reload = sysclk;                             /* 每秒的计数次数,单位为M */
    reload *= 1000000 / delay_ostickspersec;     /* 根据delay_ostickspersec计算溢出时间
                                                  * reload为24位寄存器,最大值为16777216
                                                  */
    g_fac_ms = 1000 / delay_ostickspersec;       /* 代表可以延时的最小单位(毫秒) */
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;   /* 开启SYSTICK中断 */
    SysTick->LOAD = reload;                      /* 每1/delay_ostickspersec秒中断一次 */
#endif
}

/**
 * @brief     延时nus
 * @note      不管是否支持OS, 都使用时钟读取法来实现us延时
 * @param     nus: 要延时的us数
 * @note      nus取值范围: 0 ~ (2^32 / g_fac_us) (g_fac_us一般为系统主频, 请自行计算)
 * @retval    无
 */
void delay_us(uint32_t nus)
{
    uint32_t ticks;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload;
    reload = SysTick->LOAD;             /* LOAD的值 */
    ticks = nus * g_fac_us;             /* 需要的节拍数 */

#if SYS_SUPPORT_OS                      /* 如果需要支持OS */
    delay_osschedlock();                /* 锁定 OS 的任务调度器，防止打断us延时 */
#endif

    told = SysTick->VAL;                /* 刚进入时的计数器值 */

    while (1)
    {
        tnow = SysTick->VAL;

        if (tnow != told)
        {
            if (tnow < told)
            {
                tcnt += told - tnow;    /* 这里注意SYSTICK是一个递减的计数器就可以 */
            }
            else
            {
                tcnt += reload - tnow + told;
            }

            told = tnow;

            if (tcnt >= ticks)
            {
                break;                  /* 时间超过/等于要延迟的时间,则退出 */
            }
        }
    }

#if SYS_SUPPORT_OS                      /* 如果需要支持OS */
    delay_osschedunlock();              /* 恢复 OS 的任务调度器 */
#endif
}

/**
 * @brief     延时nms
 * @param     nms: 要延时的ms数 (0< nms <= (2^32 / g_fac_us / 1000))(g_fac_us一般为系统主频, 请自行计算)
 * @retval    无
 */
void delay_ms(uint16_t nms)
{
#if SYS_SUPPORT_OS  /* 如果需要支持OS, 则根据情况调用os延时函数,释放CPU */
    if (delay_osrunning && delay_osintnesting == 0)     /* 如果OS已经运行并且不在中断里面(中断里面不能进行调度) */
    {
        if (nms >= g_fac_ms)                            /* 延时的时间大于OS的最小计时周期 */
        {
            delay_ostimedly(nms / g_fac_ms);            /* OS延时 */
        }

        nms %= g_fac_ms;                                /* OS已经无法提供这么小的延时了,采用普通方式延时 */
    }
#endif

    delay_us((uint32_t)(nms * 1000));                   /* 普通方式延时 */
}