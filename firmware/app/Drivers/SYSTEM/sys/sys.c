/**
 ****************************************************************************************************
 * @file        sys.c
 * @version     V1.0
 * @brief       一些系统相关的函数代码
 ****************************************************************************************************
 *
 * V1.0
 * 将头文件包含路径改成相对路径,避免重复包含路径的麻烦
 *
 ****************************************************************************************************
 */

#include "./SYSTEM/sys/sys.h"


/**
 * @brief       执行: WFI指令(执行完该指令进入低功耗状态, 等待中断唤醒)
 * @param       无
 * @retval      无
 */
void sys_wfi_set(void)
{
    __ASM volatile("wfi");
}

/**
 * @brief       关闭所有中断(但是不包括fault和NMI中断)
 * @param       无
 * @retval      无
 */
void sys_intx_disable(void)
{
    __ASM volatile("cpsid i");
}

/**
 * @brief       开启所有中断
 * @param       无
 * @retval      无
 */
void sys_intx_enable(void)
{
    __ASM volatile("cpsie i");
}

/**
 * @brief       设置堆栈顶部地址
 * @param       addr: 堆栈顶部地址
 * @retval      无
 */
void sys_msr_msp(uint32_t addr)
{
    __set_MSP(addr);     /* 设置堆栈顶部地址 */
}

/**
 * @brief       系统软复位
 * @param       无
 * @retval      无
 */
void sys_soft_reset(void)
{
    NVIC_SystemReset();
}

/**
 * @brief       使能GD32H7的I-Cache, 同时开启D Cache的强制透写
 * @param       无
 * @retval      无
 */
void sys_cache_enable(void)
{
    SCB_EnableICache();      /* 使能I-Cache */
    SCB_EnableDCache();      /* 使能D-Cache */
    SCB->CACR |= 1 << 2;     /* 强制D-Cache透写,如果不开启透写,实际使用中可能会遇到各种问题 */
}