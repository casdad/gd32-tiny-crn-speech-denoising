/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for FatFs on GD32H759 SDIO SD card          */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include "./BSP/SDIO/sd_conf.h"
#include <stdio.h>

#define DEV_SD_CARD    0U       /* FatFs drive number: 0: */
#define SD_SECTOR_SIZE 512U
#define SD_BLOCK_SIZE  1U

static volatile uint8_t s_sd_inited = 0U;
static volatile sd_error_enum s_last_sd_status = SD_OK;

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != DEV_SD_CARD) {
        return STA_NOINIT;
    }

    return s_sd_inited ? 0 : STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize(BYTE pdrv)
{
    sd_error_enum sd_status;

    if (pdrv != DEV_SD_CARD) {
        return STA_NOINIT;
    }

    if (s_sd_inited) {
        return 0;
    }

    sd_status = sdio_sd_init();
    s_last_sd_status = sd_status;
    printf("disk_initialize: sdio_sd_init status=%d\r\n", (int)sd_status);
    if (sd_status == SD_OK) {
        s_sd_inited = 1U;
        return 0;
    }

    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    sd_error_enum sd_status;

    if ((pdrv != DEV_SD_CARD) || (buff == 0) || (count == 0U)) {
        return RES_PARERR;
    }

    if (!s_sd_inited) {
        if (disk_initialize(pdrv) & STA_NOINIT) {
            return RES_NOTRDY;
        }
    }

    sd_status = sd_read_disk((uint8_t *)buff, (uint32_t)sector, (uint32_t)count);
    s_last_sd_status = sd_status;
    if (sd_status != SD_OK) {
        printf("disk_read failed: sector=%lu count=%u sd_status=%d\r\n",
               (unsigned long)sector, (unsigned int)count, (int)sd_status);
    }
    return (sd_status == SD_OK) ? RES_OK : RES_ERROR;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    sd_error_enum sd_status;

    if ((pdrv != DEV_SD_CARD) || (buff == 0) || (count == 0U)) {
        return RES_PARERR;
    }

    if (!s_sd_inited) {
        if (disk_initialize(pdrv) & STA_NOINIT) {
            return RES_NOTRDY;
        }
    }

    sd_status = sd_write_disk((uint8_t *)buff, (uint32_t)sector, (uint32_t)count);
    s_last_sd_status = sd_status;
    if (sd_status != SD_OK) {
        printf("disk_write failed: sector=%lu count=%u sd_status=%d\r\n",
               (unsigned long)sector, (unsigned int)count, (int)sd_status);
    }
    return (sd_status == SD_OK) ? RES_OK : RES_ERROR;
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != DEV_SD_CARD) {
        return RES_PARERR;
    }

    if (!s_sd_inited) {
        if (disk_initialize(pdrv) & STA_NOINIT) {
            return RES_NOTRDY;
        }
    }

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;

    case GET_SECTOR_COUNT:
        if (buff == 0) return RES_PARERR;
        /* sd_card_capacity_get() returns KB in this project, 1KB = 2 sectors. */
        *(LBA_t *)buff = (LBA_t)sd_card_capacity_get() * 2U;
        return RES_OK;

    case GET_SECTOR_SIZE:
        if (buff == 0) return RES_PARERR;
        *(WORD *)buff = SD_SECTOR_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        if (buff == 0) return RES_PARERR;
        *(DWORD *)buff = SD_BLOCK_SIZE;
        return RES_OK;

    case CTRL_TRIM:
        return RES_OK;

    default:
        return RES_PARERR;
    }
}
