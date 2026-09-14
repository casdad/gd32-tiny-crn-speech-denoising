#include "touch.h"
#include <stdio.h>
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LCD/lcd.h"

/* ---- User-adjustable coordinate transform ----
 * If touch X/Y direction is wrong, change these three macros and rebuild.
 */
#define TOUCH_SWAP_XY       0
#define TOUCH_INVERT_X      0
#define TOUCH_INVERT_Y      0

#define TOUCH_SCL_PORT      GPIOF
#define TOUCH_SCL_PIN       GPIO_PIN_6
#define TOUCH_SDA_PORT      GPIOF
#define TOUCH_SDA_PIN       GPIO_PIN_7
#define TOUCH_RST_PORT      GPIOF
#define TOUCH_RST_PIN       GPIO_PIN_8
#define TOUCH_INT_PORT      GPIOG
#define TOUCH_INT_PIN       GPIO_PIN_3

#define FT5X06_ADDR         0x38U
#define GT911_ADDR_5D       0x5DU
#define GT911_ADDR_14       0x14U

#define GT911_CTRL_REG      0x8040U
#define GT911_PID_REG       0x8140U
#define GT911_GSTID_REG     0x814EU
#define GT911_TP1_REG       0x8150U

/* 调试开关：打印 GT911 状态寄存器，确认是否真的有触摸数据 ready。 */
#define TOUCH_GT911_DEBUG   1

static touch_dev_t g_touch_dev = TOUCH_DEV_NONE;
static uint8_t g_touch_ready = 0U;

static void tiic_delay(void)
{
    delay_us(3U);
}

static void sda_out(void)
{
    gpio_mode_set(TOUCH_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, TOUCH_SDA_PIN);
    gpio_output_options_set(TOUCH_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_60MHZ, TOUCH_SDA_PIN);
}

static void sda_in(void)
{
    gpio_mode_set(TOUCH_SDA_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, TOUCH_SDA_PIN);
}

static void scl_high(void) { gpio_bit_set(TOUCH_SCL_PORT, TOUCH_SCL_PIN); }
static void scl_low(void)  { gpio_bit_reset(TOUCH_SCL_PORT, TOUCH_SCL_PIN); }
static void sda_high(void) { gpio_bit_set(TOUCH_SDA_PORT, TOUCH_SDA_PIN); }
static void sda_low(void)  { gpio_bit_reset(TOUCH_SDA_PORT, TOUCH_SDA_PIN); }
static uint8_t sda_read(void) { return gpio_input_bit_get(TOUCH_SDA_PORT, TOUCH_SDA_PIN) ? 1U : 0U; }

static void tiic_start(void)
{
    sda_out();
    sda_high();
    scl_high();
    tiic_delay();
    sda_low();
    tiic_delay();
    scl_low();
}

static void tiic_stop(void)
{
    sda_out();
    scl_low();
    sda_low();
    tiic_delay();
    scl_high();
    tiic_delay();
    sda_high();
    tiic_delay();
}

static uint8_t tiic_wait_ack(void)
{
    uint16_t t = 0U;

    sda_in();
    sda_high();
    tiic_delay();
    scl_high();
    tiic_delay();

    while(sda_read()) {
        t++;
        if(t > 250U) {
            scl_low();
            sda_out();
            return 0U;
        }
        tiic_delay();
    }

    scl_low();
    sda_out();
    return 1U;
}

static void tiic_ack(void)
{
    scl_low();
    sda_out();
    sda_low();
    tiic_delay();
    scl_high();
    tiic_delay();
    scl_low();
}

static void tiic_nack(void)
{
    scl_low();
    sda_out();
    sda_high();
    tiic_delay();
    scl_high();
    tiic_delay();
    scl_low();
}

static uint8_t tiic_send_byte(uint8_t data)
{
    uint8_t i;

    sda_out();
    scl_low();

    for(i = 0U; i < 8U; i++) {
        if(data & 0x80U) {
            sda_high();
        } else {
            sda_low();
        }
        data <<= 1;
        tiic_delay();
        scl_high();
        tiic_delay();
        scl_low();
        tiic_delay();
    }

    return tiic_wait_ack();
}

static uint8_t tiic_read_byte(uint8_t ack)
{
    uint8_t i;
    uint8_t data = 0U;

    sda_in();

    for(i = 0U; i < 8U; i++) {
        scl_low();
        tiic_delay();
        scl_high();
        data <<= 1;
        if(sda_read()) {
            data |= 1U;
        }
        tiic_delay();
    }

    if(ack) {
        tiic_ack();
    } else {
        tiic_nack();
    }

    return data;
}

static uint8_t tiic_probe(uint8_t addr7)
{
    uint8_t ok;
    tiic_start();
    ok = tiic_send_byte((uint8_t)(addr7 << 1));
    tiic_stop();
    return ok;
}

static uint8_t tiic_write_reg8(uint8_t addr7, uint8_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    tiic_start();
    if(!tiic_send_byte((uint8_t)(addr7 << 1))) { tiic_stop(); return 0U; }
    if(!tiic_send_byte(reg)) { tiic_stop(); return 0U; }

    for(i = 0U; i < len; i++) {
        if(!tiic_send_byte(buf[i])) { tiic_stop(); return 0U; }
    }

    tiic_stop();
    return 1U;
}

static uint8_t tiic_read_reg8(uint8_t addr7, uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    tiic_start();
    if(!tiic_send_byte((uint8_t)(addr7 << 1))) { tiic_stop(); return 0U; }
    if(!tiic_send_byte(reg)) { tiic_stop(); return 0U; }

    tiic_start();
    if(!tiic_send_byte((uint8_t)((addr7 << 1) | 1U))) { tiic_stop(); return 0U; }

    for(i = 0U; i < len; i++) {
        buf[i] = tiic_read_byte((i + 1U) < len);
    }

    tiic_stop();
    return 1U;
}

static uint8_t tiic_write_reg16(uint8_t addr7, uint16_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t i;

    tiic_start();
    if(!tiic_send_byte((uint8_t)(addr7 << 1))) { tiic_stop(); return 0U; }
    if(!tiic_send_byte((uint8_t)(reg >> 8))) { tiic_stop(); return 0U; }
    if(!tiic_send_byte((uint8_t)(reg & 0xFFU))) { tiic_stop(); return 0U; }

    for(i = 0U; i < len; i++) {
        if(!tiic_send_byte(buf[i])) { tiic_stop(); return 0U; }
    }

    tiic_stop();
    return 1U;
}

static uint8_t tiic_read_reg16(uint8_t addr7, uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    tiic_start();
    if(!tiic_send_byte((uint8_t)(addr7 << 1))) { tiic_stop(); return 0U; }
    if(!tiic_send_byte((uint8_t)(reg >> 8))) { tiic_stop(); return 0U; }
    if(!tiic_send_byte((uint8_t)(reg & 0xFFU))) { tiic_stop(); return 0U; }

    tiic_start();
    if(!tiic_send_byte((uint8_t)((addr7 << 1) | 1U))) { tiic_stop(); return 0U; }

    for(i = 0U; i < len; i++) {
        buf[i] = tiic_read_byte((i + 1U) < len);
    }

    tiic_stop();
    return 1U;
}

static void touch_gpio_init(void)
{
    rcu_periph_clock_enable(RCU_GPIOF);
    rcu_periph_clock_enable(RCU_GPIOG);

    gpio_mode_set(TOUCH_SCL_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, TOUCH_SCL_PIN);
    gpio_output_options_set(TOUCH_SCL_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_60MHZ, TOUCH_SCL_PIN);

    gpio_mode_set(TOUCH_SDA_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, TOUCH_SDA_PIN);
    gpio_output_options_set(TOUCH_SDA_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_60MHZ, TOUCH_SDA_PIN);

    gpio_bit_set(TOUCH_SCL_PORT, TOUCH_SCL_PIN);
    gpio_bit_set(TOUCH_SDA_PORT, TOUCH_SDA_PIN);

    gpio_mode_set(TOUCH_RST_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP, TOUCH_RST_PIN);
    gpio_output_options_set(TOUCH_RST_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, TOUCH_RST_PIN);
    gpio_bit_set(TOUCH_RST_PORT, TOUCH_RST_PIN);

    gpio_mode_set(TOUCH_INT_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, TOUCH_INT_PIN);
}

static uint8_t gt911_detect(uint8_t addr7)
{
    uint8_t id[6] = {0U};

    if(!tiic_probe(addr7)) {
        return 0U;
    }

    if(!tiic_read_reg16(addr7, GT911_PID_REG, id, 6U)) {
        return 0U;
    }

#if TOUCH_GT911_DEBUG
    printf("GT911 ACK addr=0x%02X PID=%c%c%c%c raw=%02X %02X %02X %02X %02X %02X\r\n",
           (unsigned int)addr7,
           ((id[0] >= 32U) && (id[0] <= 126U)) ? id[0] : '.',
           ((id[1] >= 32U) && (id[1] <= 126U)) ? id[1] : '.',
           ((id[2] >= 32U) && (id[2] <= 126U)) ? id[2] : '.',
           ((id[3] >= 32U) && (id[3] <= 126U)) ? id[3] : '.',
           (unsigned int)id[0], (unsigned int)id[1], (unsigned int)id[2],
           (unsigned int)id[3], (unsigned int)id[4], (unsigned int)id[5]);
#endif

    /* Product ID normally contains ASCII like "911", "9147", "917S".
     * Some modules do not return readable characters, so an ACK is already useful.
     */
    return 1U;
}

static void gt911_enter_read_mode(uint8_t addr7)
{
    uint8_t v;

    /* 和原触摸实验保持一致：软复位 GT911，再退出复位进入坐标读取状态。
       只 ACK 不代表芯片已经开始上报 0x814E 触摸状态。 */
    v = 0x02U;
    (void)tiic_write_reg16(addr7, GT911_CTRL_REG, &v, 1U);
    delay_ms(10U);

    v = 0x00U;
    (void)tiic_write_reg16(addr7, GT911_CTRL_REG, &v, 1U);
    delay_ms(10U);

    /* 清一次坐标状态寄存器，避免上电残留状态影响后续扫描。 */
    v = 0x00U;
    (void)tiic_write_reg16(addr7, GT911_GSTID_REG, &v, 1U);
}

static uint8_t ft5x06_detect(void)
{
    uint8_t id = 0U;

    if(!tiic_probe(FT5X06_ADDR)) {
        return 0U;
    }

    (void)tiic_read_reg8(FT5X06_ADDR, 0xA3U, &id, 1U);
    return 1U;
}

static void touch_map(uint16_t raw_x, uint16_t raw_y, uint16_t *x, uint16_t *y)
{
    uint16_t tx = raw_x;
    uint16_t ty = raw_y;
    uint16_t tmp;
    uint16_t w = lcddev.width;
    uint16_t h = lcddev.height;

#if TOUCH_SWAP_XY
    tmp = tx;
    tx = ty;
    ty = tmp;
#endif

#if TOUCH_INVERT_X
    if(w > 0U) {
        tx = (tx >= w) ? 0U : (uint16_t)(w - 1U - tx);
    }
#endif

#if TOUCH_INVERT_Y
    if(h > 0U) {
        ty = (ty >= h) ? 0U : (uint16_t)(h - 1U - ty);
    }
#endif

    if((w > 0U) && (tx >= w)) {
        tx = (uint16_t)(w - 1U);
    }
    if((h > 0U) && (ty >= h)) {
        ty = (uint16_t)(h - 1U);
    }

    *x = tx;
    *y = ty;
}

void touch_init(void)
{
    g_touch_dev = TOUCH_DEV_NONE;
    g_touch_ready = 0U;

    touch_gpio_init();

    /* Reset touch controller. Keep INT pulled up for common GT911/FT5x06 modules. */
    gpio_bit_reset(TOUCH_RST_PORT, TOUCH_RST_PIN);
    delay_ms(20U);
    gpio_bit_set(TOUCH_RST_PORT, TOUCH_RST_PIN);
    delay_ms(80U);

    if(gt911_detect(GT911_ADDR_5D)) {
        gt911_enter_read_mode(GT911_ADDR_5D);
        g_touch_dev = TOUCH_DEV_GT911_5D;
        g_touch_ready = 1U;
        return;
    }

    if(gt911_detect(GT911_ADDR_14)) {
        gt911_enter_read_mode(GT911_ADDR_14);
        g_touch_dev = TOUCH_DEV_GT911_14;
        g_touch_ready = 1U;
        return;
    }

    if(ft5x06_detect()) {
        g_touch_dev = TOUCH_DEV_FT5X06;
        g_touch_ready = 1U;
        return;
    }
}

uint8_t touch_is_ready(void)
{
    return g_touch_ready;
}

touch_dev_t touch_device(void)
{
    return g_touch_dev;
}

const char *touch_device_name(void)
{
    switch(g_touch_dev) {
    case TOUCH_DEV_FT5X06:  return "FT5X06";
    case TOUCH_DEV_GT911_5D:return "GT911@5D";
    case TOUCH_DEV_GT911_14:return "GT911@14";
    default:               return "NONE";
    }
}

static uint8_t gt911_read_point(uint8_t addr7, touch_point_t *pt)
{
    uint8_t status;
    uint8_t buf[4];
    uint8_t zero = 0U;
    uint8_t n;
    uint16_t x;
    uint16_t y;
#if TOUCH_GT911_DEBUG
    static uint32_t dbg_cnt = 0U;
    static uint8_t last_status = 0xFFU;
#endif

    if(!tiic_read_reg16(addr7, GT911_GSTID_REG, &status, 1U)) {
#if TOUCH_GT911_DEBUG
        printf("GT911 read 0x814E failed addr=0x%02X\r\n", (unsigned int)addr7);
#endif
        return 0U;
    }

#if TOUCH_GT911_DEBUG
    /* 每隔一段时间打印一次状态；状态变化时也打印。
       你触摸屏幕时，正常应该看到 status 变成 0x81/0x82 等。 */
    dbg_cnt++;
    if((status != last_status) || ((dbg_cnt % 100U) == 0U)) {
        last_status = status;
        printf("GT911 status=0x%02X n=%u int=%u\r\n",
               (unsigned int)status,
               (unsigned int)(status & 0x0FU),
               (unsigned int)gpio_input_bit_get(TOUCH_INT_PORT, TOUCH_INT_PIN));
    }
#endif

    n = status & 0x0FU;

    if((status & 0x80U) == 0U) {
        return 0U;
    }

    if(n == 0U) {
        (void)tiic_write_reg16(addr7, GT911_GSTID_REG, &zero, 1U);
        return 0U;
    }

    if(n > 5U) {
        (void)tiic_write_reg16(addr7, GT911_GSTID_REG, &zero, 1U);
        return 0U;
    }

    /* 按原触摸实验 gt9xxx_scan() 的方式解析：
       0x8150:X_L, 0x8151:X_H, 0x8152:Y_L, 0x8153:Y_H。
       之前版本偏移了一字节，会导致坐标不对。 */
    if(!tiic_read_reg16(addr7, GT911_TP1_REG, buf, 4U)) {
        (void)tiic_write_reg16(addr7, GT911_GSTID_REG, &zero, 1U);
        return 0U;
    }

    x = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    y = (uint16_t)buf[2] | ((uint16_t)buf[3] << 8);

    (void)tiic_write_reg16(addr7, GT911_GSTID_REG, &zero, 1U);

#if TOUCH_GT911_DEBUG
    printf("GT911 raw bytes: %02X %02X %02X %02X -> x=%u y=%u\r\n",
           (unsigned int)buf[0], (unsigned int)buf[1],
           (unsigned int)buf[2], (unsigned int)buf[3],
           (unsigned int)x, (unsigned int)y);
#endif

    pt->pressed = 1U;
    pt->raw_x = x;
    pt->raw_y = y;
    touch_map(x, y, &pt->x, &pt->y);
    return 1U;
}

static uint8_t ft5x06_read_point(touch_point_t *pt)
{
    uint8_t n = 0U;
    uint8_t buf[4];
    uint16_t x;
    uint16_t y;

    if(!tiic_read_reg8(FT5X06_ADDR, 0x02U, &n, 1U)) {
        return 0U;
    }

    n &= 0x0FU;
    if(n == 0U) {
        return 0U;
    }

    if(!tiic_read_reg8(FT5X06_ADDR, 0x03U, buf, 4U)) {
        return 0U;
    }

    x = (uint16_t)((buf[0] & 0x0FU) << 8) | buf[1];
    y = (uint16_t)((buf[2] & 0x0FU) << 8) | buf[3];

    pt->pressed = 1U;
    pt->raw_x = x;
    pt->raw_y = y;
    touch_map(x, y, &pt->x, &pt->y);
    return 1U;
}

uint8_t touch_read(touch_point_t *pt)
{
    if(pt == 0) {
        return 0U;
    }

    pt->pressed = 0U;
    pt->x = 0U;
    pt->y = 0U;
    pt->raw_x = 0U;
    pt->raw_y = 0U;

    if(!g_touch_ready) {
        return 0U;
    }

    switch(g_touch_dev) {
    case TOUCH_DEV_GT911_5D:
        return gt911_read_point(GT911_ADDR_5D, pt);
    case TOUCH_DEV_GT911_14:
        return gt911_read_point(GT911_ADDR_14, pt);
    case TOUCH_DEV_FT5X06:
        return ft5x06_read_point(pt);
    default:
        return 0U;
    }
}
