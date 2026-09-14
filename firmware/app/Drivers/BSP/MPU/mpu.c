/**
 ****************************************************************************************************
 * @file        mpu.c
 * @version     V1.0
 * @brief       MPU内存保护 驱动代码
 ****************************************************************************************************
 * @attention   Waiken-Smart 慧勤智远
 *
 * 实验平台:    GD32H759IMT6小系统板
 *
 ****************************************************************************************************
 */
 
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/MPU/mpu.h"


/**
 * @brief       设置某个区域的MPU保护
 * @param       baseaddr:MPU保护区域的基地址(首地址)
 * @param       size:MPU保护区域的大小(单位为字节，必须是32的倍数)
 * @param       rnum:MPU保护区域编号,范围:0~15,最大支持16个保护区域
 * @param       de:禁止指令访问;MPU_INSTRUCTION_EXEC_PERMIT,允许指令访问;MPU_INSTRUCTION_EXEC_NOT_PERMIT,禁止指令访问
 * @param       ap:访问权限,访问关系如下:
 *   @arg       MPU_AP_NO_ACCESS,无访问（特权&用户都不可访问）
 *   @arg       MPU_AP_PRIV_RW,仅支持特权读写访问
 *   @arg       MPU_AP_PRIV_RW_UNPRIV_RO,禁止用户写访问（特权可读写访问）
 *   @arg       MPU_AP_FULL_ACCESS,全访问（特权&用户都可以访问）
 *   @arg       MPU_AP_PRIV_RO,仅支持特权读访问
 *   @arg       MPU_AP_PRIV_UNPRIV_RO,只读（特权&用户都不可以写）
 * @param       sen : 是否允许共用;MPU_ACCESS_NON_SHAREABLE,不允许;MPU_ACCESS_SHAREABLE,允许
 * @param       cen : 是否允许cache;MPU_ACCESS_NON_CACHEABLE,不允许;MPU_ACCESS_CACHEABLE,允许
 * @param       ben : 是否允许缓冲;MPU_ACCESS_NON_BUFFERABLE,不允许;MPU_ACCESS_BUFFERABLE,允许
 * @retval      无
 */
void mpu_set_protection(uint32_t baseaddr, uint32_t size, uint32_t rnum, uint8_t de, uint8_t ap, uint8_t sen, uint8_t cen, uint8_t ben)
{
    mpu_region_init_struct mpu_init_struct;
    mpu_region_struct_para_init(&mpu_init_struct);

    ARM_MPU_Disable();                                           /* 配置MPU之前先关闭MPU,配置完成以后再使能MPU */
  
    mpu_init_struct.region_number       = rnum;                  /* 设置保护区域编号 */
    mpu_init_struct.region_base_address = baseaddr;              /* 设置保护区域基地址 */
    mpu_init_struct.instruction_exec    = de;                    /* 是否允许指令访问 */
    mpu_init_struct.access_permission   = ap;                    /* 设置访问权限 */
    mpu_init_struct.tex_type            = MPU_TEX_TYPE0;         /* 设置TEX类型为type 0 */
    mpu_init_struct.access_shareable    = sen;                   /* 是否允许共用 */
    mpu_init_struct.access_cacheable    = cen;                   /* 是否允许cache */
    mpu_init_struct.access_bufferable   = ben;                   /* 是否允许缓冲 */
    mpu_init_struct.subregion_disable   = 0X00;                  /* 禁止子区域 */
    mpu_init_struct.region_size         = size;                  /* 设置保护区域大小 */    
    mpu_region_config(&mpu_init_struct);                         /* 配置MPU区域 */
    mpu_region_enable();                                         /* 使能MPU区域 */
                
    ARM_MPU_Enable(MPU_MODE_PRIV_DEFAULT);                       /* 设置完毕,使能MPU保护 */
}
 
/**
 * @brief       设置需要保护的存储块
 * @note        必须对部分存储区域进行MPU保护,否则可能导致程序运行异常
 *              比如MCU屏不显示,摄像头采集数据出错等等问题...
 * @param       无
 * @retval      无
 */
void mpu_memory_protection(void)
{
    /* 保护整个ITCM,共512K字节 */
    mpu_set_protection( 0x00000000,                 /* 区域基地址 */
                        MPU_REGION_SIZE_512KB,      /* 区域大小 */
                        MPU_REGION_NUMBER0,         /* 区域编号 */
                        MPU_INSTRUCTION_EXEC_PERMIT,/* 允许指令访问 */
                        MPU_AP_FULL_ACCESS,         /* 全访问 */
                        MPU_ACCESS_NON_SHAREABLE,   /* 禁止共用 */
                        MPU_ACCESS_CACHEABLE,       /* 允许cache */
                        MPU_ACCESS_BUFFERABLE);     /* 允许缓冲 */

    /* 保护整个DTCM,共512K字节 */
    mpu_set_protection( 0x20000000,                 /* 区域基地址 */
                        MPU_REGION_SIZE_512KB,      /* 区域大小 */
                        MPU_REGION_NUMBER1,         /* 区域编号 */
                        MPU_INSTRUCTION_EXEC_PERMIT,/* 允许指令访问 */
                        MPU_AP_FULL_ACCESS,         /* 全访问 */
                        MPU_ACCESS_NON_SHAREABLE,   /* 禁止共用 */
                        MPU_ACCESS_CACHEABLE,       /* 允许cache */
                        MPU_ACCESS_BUFFERABLE);     /* 允许缓冲 */

    /* 保护整个AXI SRAM,共1024K字节 */
    mpu_set_protection( 0x24000000,                 /* 区域基地址 */
                        MPU_REGION_SIZE_1MB,        /* 区域大小 */
                        MPU_REGION_NUMBER2,         /* 区域编号 */
                        MPU_INSTRUCTION_EXEC_PERMIT,/* 允许指令访问 */
                        MPU_AP_FULL_ACCESS,         /* 全访问 */
                        MPU_ACCESS_NON_SHAREABLE,   /* 禁止共用 */
                        MPU_ACCESS_CACHEABLE,       /* 允许cache */
                        MPU_ACCESS_BUFFERABLE);     /* 允许缓冲 */

    /* 保护整个SRAM0和SRAM1,共32K字节 */
    mpu_set_protection( 0x30000000,                 /* 区域基地址 */
                        MPU_REGION_SIZE_32KB,       /* 区域大小 */
                        MPU_REGION_NUMBER3,         /* 区域编号 */
                        MPU_INSTRUCTION_EXEC_PERMIT,/* 允许指令访问 */
                        MPU_AP_FULL_ACCESS,         /* 全访问 */
                        MPU_ACCESS_NON_SHAREABLE,   /* 禁止共用 */
                        MPU_ACCESS_CACHEABLE,       /* 允许cache */
                        MPU_ACCESS_BUFFERABLE);     /* 允许缓冲 */

    /* 保护MCU LCD屏所在的EXMC区域,共64M字节 */
    mpu_set_protection( 0x64000000,                 /* 区域基地址 */
                        MPU_REGION_SIZE_64MB,       /* 区域大小 */
                        MPU_REGION_NUMBER4,         /* 区域编号 */
                        MPU_INSTRUCTION_EXEC_PERMIT,/* 允许指令访问 */
                        MPU_AP_FULL_ACCESS,         /* 全访问 */
                        MPU_ACCESS_NON_SHAREABLE,   /* 禁止共用 */
                        MPU_ACCESS_NON_CACHEABLE,   /* 禁止cache */
                        MPU_ACCESS_NON_BUFFERABLE); /* 禁止缓冲 */
                        
    /* 保护SDRAM区域,共32M字节 */
    mpu_set_protection( 0xC0000000,                 /* 区域基地址 */
                        MPU_REGION_SIZE_32MB,       /* 区域大小 */
                        MPU_REGION_NUMBER5,         /* 区域编号 */
                        MPU_INSTRUCTION_EXEC_PERMIT,/* 允许指令访问 */
                        MPU_AP_FULL_ACCESS,         /* 全访问 */
                        MPU_ACCESS_NON_SHAREABLE,   /* 禁止共用 */
                        MPU_ACCESS_CACHEABLE,       /* 允许cache */
                        MPU_ACCESS_BUFFERABLE);     /* 允许缓冲 */                               
}

/**
 * @brief       MemManage错误处理中断
 * @note        进入此中断以后,将无法恢复程序运行!!
 * @param       无
 * @retval      无
 */
void MemManage_Handler(void)
{
    LED1(0);                            /* 点亮LED1(绿灯) */
    printf("Mem Access Error!!\r\n");   /* 输出错误信息 */
    delay_ms(1000);
    printf("Soft Reseting...\r\n");     /* 提示软件重启 */
    delay_ms(1000);
    sys_soft_reset();                   /* 软复位 */
}




