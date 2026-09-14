#ifndef __TOUCH_H
#define __TOUCH_H

#include <stdint.h>
#include "gd32h7xx.h"

typedef enum {
    TOUCH_DEV_NONE = 0,
    TOUCH_DEV_FT5X06,
    TOUCH_DEV_GT911_5D,
    TOUCH_DEV_GT911_14
} touch_dev_t;

typedef struct {
    uint8_t pressed;
    uint16_t x;
    uint16_t y;
    uint16_t raw_x;
    uint16_t raw_y;
} touch_point_t;

/* LCD2 touch pins on GD32H759IMT6 core board:
 * PF6  -> CTP_SCL / RTP_SCK
 * PF7  -> CTP_SDA / RTP_MOSI
 * PF8  -> CTP_RST / RTP_CS
 * PG3  -> CTP_INT / RTP_PEN
 */
void touch_init(void);
uint8_t touch_is_ready(void);
touch_dev_t touch_device(void);
const char *touch_device_name(void);
uint8_t touch_read(touch_point_t *pt);

#endif
