#include <stdio.h>
#include <string.h>

#define APP_SERIAL_LOG_ENABLE 0
#if !APP_SERIAL_LOG_ENABLE
#define printf(...) ((void)0)
#endif
#include <stdint.h>

#include "./SYSTEM/delay/delay.h"
#include "./BSP/SDIO/sd_conf.h"
#include "./BSP/LCD/lcd.h"
#include "ff.h"
#include "sd_wav_recorder.h"

/*
 * FatFs based WAV recorder ported from SD_FATFS_AUDIO_Project_fixed.
 *
 * Why this version replaces the previous raw-FAT32 writer:
 * 1. The old module directly parsed and modified FAT32 sectors. It only accepted
 *    a narrow FAT32 layout and was easy to fail on cards formatted differently.
 * 2. The example project proves the board-side SDIO + FatFs path can create a
 *    playable WAV file through f_mount/f_open/f_write/f_sync/f_close.
 * 3. This file keeps the original public wavrec_* API, so main.c/UI/RNNoise code
 *    does not need to be rewritten.
 */

#define WAV_HEADER_SIZE             44U
#define WAV_BYTE_RATE               (WAVREC_SAMPLE_RATE * WAVREC_CHANNELS * WAVREC_BITS_PER_SAMPLE / 8U)
#define WAV_BLOCK_ALIGN             (WAVREC_CHANNELS * WAVREC_BITS_PER_SAMPLE / 8U)
#define WAVREC_SD_INIT_RETRIES      3U
#define WAVREC_MAX_FILENAME_NUM     9999U

/* Larger write buffer reduces SD/FatFs blocking compared with writing each sample. */
#define WAVREC_WRITE_BUF_SIZE       65536U

/* Set to 1 only when you really want the MCU to format the card if no file system exists. */
#define WAVREC_AUTO_MKFS            0

typedef struct {
    uint8_t mounted;
    uint8_t ready;
    uint8_t recording;
    int last_error;

    uint32_t data_bytes;
    uint32_t samples_written;

    char filename[16];              /* Display name: CMP0001.WAV */
    char filepath[24];              /* FatFs path: 0:/CMP0001.WAV */

    uint32_t buf_pos;
} wavrec_ctx_t;

static wavrec_ctx_t g_rec;
static FATFS g_sd_fs;
static FIL g_wav_file;
static uint8_t g_write_buf[WAVREC_WRITE_BUF_SIZE] __attribute__((aligned(32)));
#define WAVREC_RAW_FRAME_CHUNK_SAMPLES 512U
static uint8_t g_raw_frame_buf[WAVREC_RAW_FRAME_CHUNK_SAMPLES * 4U] __attribute__((aligned(32)));

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static void wav_make_header(uint8_t h[WAV_HEADER_SIZE], uint32_t pcm_bytes)
{
    memset(h, 0, WAV_HEADER_SIZE);

    memcpy(&h[0], "RIFF", 4U);
    wr32(&h[4], 36U + pcm_bytes);
    memcpy(&h[8], "WAVE", 4U);
    memcpy(&h[12], "fmt ", 4U);
    wr32(&h[16], 16U);                       /* PCM fmt chunk size */
    wr16(&h[20], 1U);                        /* Audio format: PCM */
    wr16(&h[22], WAVREC_CHANNELS);
    wr32(&h[24], WAVREC_SAMPLE_RATE);
    wr32(&h[28], WAV_BYTE_RATE);
    wr16(&h[32], WAV_BLOCK_ALIGN);
    wr16(&h[34], WAVREC_BITS_PER_SAMPLE);
    memcpy(&h[36], "data", 4U);
    wr32(&h[40], pcm_bytes);
}

static void set_fatfs_error(FRESULT res)
{
    if(res == FR_OK) {
        g_rec.last_error = 0;
    } else if(res == FR_NO_FILESYSTEM) {
        /* Keep legacy UI meaning: FAT/FAT32/exFAT/NTFS problem. */
        g_rec.last_error = -14;
    } else if((res == FR_NOT_READY) || (res == FR_DISK_ERR) || (res == FR_INT_ERR)) {
        g_rec.last_error = -10 - (int)res;
    } else {
        g_rec.last_error = -100 - (int)res;
    }
}

static void make_file_name(uint32_t num, char *display, uint32_t display_len, char *path, uint32_t path_len)
{
    snprintf(display, display_len, "CMP%04lu.WAV", (unsigned long)num);
    snprintf(path, path_len, "0:/CMP%04lu.WAV", (unsigned long)num);
}

static FRESULT find_unique_file(void)
{
    uint32_t num;
    FILINFO info;
    FRESULT res;
    char display[16];
    char path[24];

    for(num = 1U; num <= WAVREC_MAX_FILENAME_NUM; num++) {
        make_file_name(num, display, sizeof(display), path, sizeof(path));
        res = f_stat(path, &info);
        if(res == FR_NO_FILE) {
            strncpy(g_rec.filename, display, sizeof(g_rec.filename) - 1U);
            g_rec.filename[sizeof(g_rec.filename) - 1U] = '\0';
            strncpy(g_rec.filepath, path, sizeof(g_rec.filepath) - 1U);
            g_rec.filepath[sizeof(g_rec.filepath) - 1U] = '\0';
            return FR_OK;
        }
        if(res != FR_OK) {
            return res;
        }
    }

    return FR_DENIED;
}

static FRESULT wavrec_mount(void)
{
    FRESULT res;
    uint8_t retry;

    if(g_rec.mounted != 0U) {
        return FR_OK;
    }

    for(retry = 0U; retry < WAVREC_SD_INIT_RETRIES; retry++) {
        res = f_mount(&g_sd_fs, "0:", 1);
        printf("FatFs mount try %u res=%d\r\n", (unsigned int)(retry + 1U), (int)res);

#if WAVREC_AUTO_MKFS
        if(res == FR_NO_FILESYSTEM) {
            static uint8_t work[FF_MAX_SS] __attribute__((aligned(32)));
            MKFS_PARM opt;

            opt.fmt = FM_ANY;
            opt.n_fat = 0;
            opt.align = 0;
            opt.n_root = 0;
            opt.au_size = 0;

            printf("No filesystem. Start f_mkfs...\r\n");
            res = f_mkfs("0:", &opt, work, sizeof(work));
            printf("f_mkfs res=%d\r\n", (int)res);
            if(res == FR_OK) {
                f_mount(NULL, "0:", 1);
                res = f_mount(&g_sd_fs, "0:", 1);
                printf("f_mount after mkfs res=%d\r\n", (int)res);
            }
        }
#endif

        if(res == FR_OK) {
            g_rec.mounted = 1U;
            g_rec.ready = 1U;
            g_rec.last_error = 0;
            return FR_OK;
        }

        set_fatfs_error(res);
        delay_ms(30);
    }

    return res;
}

static FRESULT wavrec_flush_buffer(void)
{
    FRESULT res;
    UINT bw;

    if(g_rec.buf_pos == 0U) {
        return FR_OK;
    }

    res = f_write(&g_wav_file, g_write_buf, g_rec.buf_pos, &bw);
    if((res == FR_OK) && (bw != g_rec.buf_pos)) {
        res = FR_DISK_ERR;
    }

    if(res != FR_OK) {
        set_fatfs_error(res);
        printf("REC f_write failed res=%d bw=%u need=%u\r\n",
               (int)res,
               (unsigned int)bw,
               (unsigned int)g_rec.buf_pos);
        return res;
    }

    g_rec.buf_pos = 0U;
    return FR_OK;
}

static FRESULT wavrec_write_bytes(const uint8_t *data, uint32_t len, uint8_t count_as_pcm)
{
    uint32_t done = 0U;
    FRESULT res;

    if(g_rec.recording == 0U) {
        return FR_INVALID_OBJECT;
    }

    if((data == NULL) && (len != 0U)) {
        return FR_INVALID_PARAMETER;
    }

    while(done < len) {
        uint32_t space = WAVREC_WRITE_BUF_SIZE - g_rec.buf_pos;
        uint32_t chunk = len - done;

        if(chunk > space) {
            chunk = space;
        }

        memcpy(&g_write_buf[g_rec.buf_pos], &data[done], chunk);
        g_rec.buf_pos += chunk;
        done += chunk;

        if(count_as_pcm != 0U) {
            g_rec.data_bytes += chunk;
        }

        if(g_rec.buf_pos >= WAVREC_WRITE_BUF_SIZE) {
            res = wavrec_flush_buffer();
            if(res != FR_OK) {
                return res;
            }
        }
    }

    return FR_OK;
}

static FRESULT wavrec_rewrite_header(void)
{
    FRESULT res;
    UINT bw;
    uint8_t header[WAV_HEADER_SIZE];

    wav_make_header(header, g_rec.data_bytes);

    res = f_lseek(&g_wav_file, 0U);
    if(res != FR_OK) {
        set_fatfs_error(res);
        return res;
    }

    res = f_write(&g_wav_file, header, sizeof(header), &bw);
    if((res == FR_OK) && (bw != sizeof(header))) {
        res = FR_DISK_ERR;
    }

    if(res != FR_OK) {
        set_fatfs_error(res);
    }

    return res;
}

uint8_t wavrec_init(void)
{
    FRESULT res;

    /* Do not clear filename here; after STOP the UI should still show saved file. */
    g_rec.ready = 0U;
    g_rec.mounted = 0U;
    g_rec.recording = 0U;
    g_rec.buf_pos = 0U;
    g_rec.last_error = 0;

    res = wavrec_mount();
    if(res != FR_OK) {
        printf("FatFs mount failed res=%d err=%d. Format SD as FAT/FAT32 on PC.\r\n",
               (int)res,
               g_rec.last_error);
        return 0U;
    }

    /* Only for printing capacity/debug info. FatFs has already initialized the disk. */
    card_info_get();

    printf("SD/FatFs ready: %lu KB, WAV stereo REC=L RAW R RAW; offline RNN=L RAW R CLEAN\r\n",
           (unsigned long)sd_card_capacity_get());
    return 1U;
}

uint8_t wavrec_is_ready(void)
{
    return g_rec.ready;
}

uint8_t wavrec_is_recording(void)
{
    return g_rec.recording;
}

int wavrec_start(void)
{
    FRESULT res;
    UINT bw;
    uint8_t header[WAV_HEADER_SIZE];

    if(g_rec.recording != 0U) {
        return 0;
    }

    if(g_rec.ready == 0U) {
        printf("REC: SD not ready, try FatFs init now...\r\n");
        if(wavrec_init() == 0U) {
            printf("REC: SD/FatFs init failed err=%d\r\n", g_rec.last_error);
            return -1;
        }
    }

    printf("REC: find unique WAV name...\r\n");
    res = find_unique_file();
    if(res != FR_OK) {
        set_fatfs_error(res);
        printf("REC: find unique file failed res=%d err=%d\r\n", (int)res, g_rec.last_error);
        return -1;
    }
    printf("REC: selected %s\r\n", g_rec.filepath);

    res = f_open(&g_wav_file, g_rec.filepath, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK) {
        set_fatfs_error(res);
        printf("REC: f_open %s failed res=%d err=%d\r\n", g_rec.filepath, (int)res, g_rec.last_error);
        return -1;
    }

    g_rec.data_bytes = 0U;
    g_rec.samples_written = 0U;
    g_rec.buf_pos = 0U;
    g_rec.recording = 1U;

    wav_make_header(header, 0U);
    res = f_write(&g_wav_file, header, sizeof(header), &bw);
    if((res == FR_OK) && (bw != sizeof(header))) {
        res = FR_DISK_ERR;
    }

    if(res != FR_OK) {
        set_fatfs_error(res);
        g_rec.recording = 0U;
        f_close(&g_wav_file);
        printf("REC: write header failed res=%d err=%d bw=%u\r\n", (int)res, g_rec.last_error, (unsigned int)bw);
        return -1;
    }

    printf("REC start: %s\r\n", g_rec.filename);
    return 0;
}

int wavrec_stop(void)
{
    FRESULT res;

    if(g_rec.recording == 0U) {
        return 0;
    }

    res = wavrec_flush_buffer();
    if(res != FR_OK) {
        g_rec.recording = 0U;
        f_close(&g_wav_file);
        return -1;
    }

    res = wavrec_rewrite_header();
    if(res != FR_OK) {
        g_rec.recording = 0U;
        f_close(&g_wav_file);
        return -1;
    }

    res = f_sync(&g_wav_file);
    if(res != FR_OK) {
        set_fatfs_error(res);
        g_rec.recording = 0U;
        f_close(&g_wav_file);
        return -1;
    }

    res = f_close(&g_wav_file);
    if(res != FR_OK) {
        set_fatfs_error(res);
        g_rec.recording = 0U;
        return -1;
    }

    g_rec.recording = 0U;
    g_rec.last_error = 0;

    printf("REC saved: %s, %lu bytes PCM, %lu sec\r\n",
           g_rec.filename,
           (unsigned long)g_rec.data_bytes,
           (unsigned long)wavrec_elapsed_seconds());
    return 0;
}

void wavrec_write_sample(int16_t sample)
{
    /* Backward-compatible mono call: write same sample to L/R. */
    wavrec_write_pair(sample, sample);
}

void wavrec_write_pair(int16_t raw_sample, int16_t clean_sample)
{
    uint8_t b[4];
    FRESULT res;

    if(g_rec.recording == 0U) {
        return;
    }

    /* WAV little-endian, stereo interleaved: L=RAW input, R=CLEAN/RNNoise output. */
    b[0] = (uint8_t)((uint16_t)raw_sample & 0xFFU);
    b[1] = (uint8_t)(((uint16_t)raw_sample >> 8) & 0xFFU);
    b[2] = (uint8_t)((uint16_t)clean_sample & 0xFFU);
    b[3] = (uint8_t)(((uint16_t)clean_sample >> 8) & 0xFFU);

    res = wavrec_write_bytes(b, sizeof(b), 1U);
    if(res != FR_OK) {
        printf("REC write error res=%d err=%d\r\n", (int)res, g_rec.last_error);
        g_rec.recording = 0U;
        f_close(&g_wav_file);
    } else {
        g_rec.samples_written++;
    }
}

void wavrec_write_pcm16(const int16_t *samples, uint32_t count)
{
    uint32_t i;

    if(samples == NULL) {
        return;
    }

    for(i = 0U; i < count; i++) {
        wavrec_write_sample(samples[i]);
    }
}

void wavrec_write_raw_frame_as_stereo(const int16_t *raw_samples, uint32_t count)
{
    uint32_t done = 0U;

    if((raw_samples == NULL) || (g_rec.recording == 0U)) {
        return;
    }

    while(done < count) {
        uint32_t n;
        uint32_t i;
        FRESULT res;

        n = count - done;
        if(n > WAVREC_RAW_FRAME_CHUNK_SAMPLES) {
            n = WAVREC_RAW_FRAME_CHUNK_SAMPLES;
        }

        for(i = 0U; i < n; i++) {
            int16_t s = raw_samples[done + i];

            /* 录音实时优先：L=RAW，R=RAW。STOP 后离线生成 RNNxxxx.WAV。 */
            g_raw_frame_buf[i * 4U + 0U] = (uint8_t)((uint16_t)s & 0xFFU);
            g_raw_frame_buf[i * 4U + 1U] = (uint8_t)(((uint16_t)s >> 8) & 0xFFU);
            g_raw_frame_buf[i * 4U + 2U] = g_raw_frame_buf[i * 4U + 0U];
            g_raw_frame_buf[i * 4U + 3U] = g_raw_frame_buf[i * 4U + 1U];
        }

        res = wavrec_write_bytes(g_raw_frame_buf, n * 4U, 1U);
        if(res != FR_OK) {
            printf("REC frame write error res=%d err=%d\r\n", (int)res, g_rec.last_error);
            g_rec.recording = 0U;
            f_close(&g_wav_file);
            return;
        }

        done += n;
    }

    g_rec.samples_written += count;
}

const char *wavrec_filename(void)
{
    return (g_rec.filename[0] != '\0') ? g_rec.filename : "NONE";
}

const char *wavrec_status_text(void)
{
    if(g_rec.ready == 0U) {
        if(g_rec.last_error == -14) {
            return "FAT ERR";
        }

        if(g_rec.last_error != 0) {
            return "SD ERR";
        }

        return "NO INIT";
    }

    if(g_rec.recording != 0U) {
        return "REC";
    }

    if(g_rec.filename[0] != '\0') {
        return "SAVED";
    }

    return "READY";
}

uint32_t wavrec_data_bytes(void)
{
    return g_rec.data_bytes;
}

uint32_t wavrec_elapsed_seconds(void)
{
    return g_rec.samples_written / WAVREC_SAMPLE_RATE;
}

uint32_t wavrec_sd_capacity_kb(void)
{
    if(g_rec.ready == 0U) {
        return 0U;
    }

    return sd_card_capacity_get();
}

int wavrec_last_error(void)
{
    return g_rec.last_error;
}

/* -------------------------------------------------------------------------- */
/* LCD screenshot to SD card: 24-bit BMP                                      */
/* -------------------------------------------------------------------------- */

#define SDSHOT_BMP_HEADER_SIZE      54U
#define SDSHOT_MAX_FILENAME_NUM     9999U
#define SDSHOT_LINE_BUF_SIZE        4096U   /* enough for 1024*3 + padding */

typedef struct {
    uint8_t busy;
    int last_error;
    uint32_t saved_count;
    char filename[16];              /* Display name: SCR0001.BMP */
    char filepath[24];              /* FatFs path: 0:/SCR0001.BMP */
} sdshot_ctx_t;

static sdshot_ctx_t g_shot;
static FIL g_bmp_file;
static uint8_t g_bmp_line_buf[SDSHOT_LINE_BUF_SIZE] __attribute__((aligned(32)));

static void sdshot_set_fatfs_error(FRESULT res)
{
    if(res == FR_OK) {
        g_shot.last_error = 0;
    } else if(res == FR_NO_FILESYSTEM) {
        g_shot.last_error = -14;
    } else if((res == FR_NOT_READY) || (res == FR_DISK_ERR) || (res == FR_INT_ERR)) {
        g_shot.last_error = -10 - (int)res;
    } else {
        g_shot.last_error = -100 - (int)res;
    }
}

static void sdshot_make_file_name(uint32_t num,
                                  char *display,
                                  uint32_t display_len,
                                  char *path,
                                  uint32_t path_len)
{
    snprintf(display, display_len, "SCR%04lu.BMP", (unsigned long)num);
    snprintf(path, path_len, "0:/SCR%04lu.BMP", (unsigned long)num);
}

static FRESULT sdshot_find_unique_file(void)
{
    uint32_t num;
    FILINFO info;
    FRESULT res;
    char display[16];
    char path[24];

    for(num = 1U; num <= SDSHOT_MAX_FILENAME_NUM; num++) {
        sdshot_make_file_name(num, display, sizeof(display), path, sizeof(path));
        res = f_stat(path, &info);
        if(res == FR_NO_FILE) {
            strncpy(g_shot.filename, display, sizeof(g_shot.filename) - 1U);
            g_shot.filename[sizeof(g_shot.filename) - 1U] = '\0';
            strncpy(g_shot.filepath, path, sizeof(g_shot.filepath) - 1U);
            g_shot.filepath[sizeof(g_shot.filepath) - 1U] = '\0';
            return FR_OK;
        }
        if(res != FR_OK) {
            return res;
        }
    }

    return FR_DENIED;
}

static void sdshot_make_bmp_header(uint8_t h[SDSHOT_BMP_HEADER_SIZE],
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t row_bytes)
{
    uint32_t pixel_bytes;
    uint32_t file_size;

    pixel_bytes = row_bytes * height;
    file_size = SDSHOT_BMP_HEADER_SIZE + pixel_bytes;

    memset(h, 0, SDSHOT_BMP_HEADER_SIZE);
    h[0] = 'B';
    h[1] = 'M';
    wr32(&h[2], file_size);
    wr32(&h[10], SDSHOT_BMP_HEADER_SIZE);

    /* BITMAPINFOHEADER */
    wr32(&h[14], 40U);
    wr32(&h[18], width);
    wr32(&h[22], height);           /* positive height: BMP bottom-up rows */
    wr16(&h[26], 1U);               /* planes */
    wr16(&h[28], 24U);              /* 24-bit BGR */
    wr32(&h[30], 0U);               /* BI_RGB */
    wr32(&h[34], pixel_bytes);
    wr32(&h[38], 2835U);            /* 72 DPI approx */
    wr32(&h[42], 2835U);
    wr32(&h[46], 0U);
    wr32(&h[50], 0U);
}

int sdshot_save_lcd_bmp(void)
{
    FRESULT res;
    UINT bw;
    uint8_t header[SDSHOT_BMP_HEADER_SIZE];
    uint32_t width;
    uint32_t height;
    uint32_t row_bytes;
    uint32_t raw_row_bytes;
    uint32_t x;
    int32_t y;
    uint16_t c;
    uint8_t r;
    uint8_t g;
    uint8_t b;

    if(g_shot.busy != 0U) {
        g_shot.last_error = -30;
        return -1;
    }

    if(g_rec.recording != 0U) {
        /* Avoid mixing long BMP writes with the open WAV file. */
        g_shot.last_error = -31;
        printf("SHOT: stop recording before screenshot\r\n");
        return -1;
    }

    width = lcddev.width;
    height = lcddev.height;
    if((width == 0U) || (height == 0U)) {
        g_shot.last_error = -32;
        printf("SHOT: invalid LCD size %lu x %lu\r\n", (unsigned long)width, (unsigned long)height);
        return -1;
    }

    raw_row_bytes = width * 3U;
    row_bytes = (raw_row_bytes + 3U) & ~3U;  /* BMP rows must be 4-byte aligned. */
    if(row_bytes > SDSHOT_LINE_BUF_SIZE) {
        g_shot.last_error = -33;
        printf("SHOT: line buffer too small row=%lu buf=%u\r\n",
               (unsigned long)row_bytes,
               (unsigned int)SDSHOT_LINE_BUF_SIZE);
        return -1;
    }

    g_shot.busy = 1U;

    if(wavrec_init() == 0U) {
        g_shot.busy = 0U;
        g_shot.last_error = wavrec_last_error();
        printf("SHOT: SD init failed err=%d\r\n", g_shot.last_error);
        return -1;
    }

    res = sdshot_find_unique_file();
    if(res != FR_OK) {
        sdshot_set_fatfs_error(res);
        g_shot.busy = 0U;
        printf("SHOT: find unique file failed res=%d err=%d\r\n", (int)res, g_shot.last_error);
        return -1;
    }

    res = f_open(&g_bmp_file, g_shot.filepath, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK) {
        sdshot_set_fatfs_error(res);
        g_shot.busy = 0U;
        printf("SHOT: f_open %s failed res=%d err=%d\r\n", g_shot.filepath, (int)res, g_shot.last_error);
        return -1;
    }

    sdshot_make_bmp_header(header, width, height, row_bytes);
    res = f_write(&g_bmp_file, header, sizeof(header), &bw);
    if((res == FR_OK) && (bw != sizeof(header))) {
        res = FR_DISK_ERR;
    }
    if(res != FR_OK) {
        sdshot_set_fatfs_error(res);
        f_close(&g_bmp_file);
        g_shot.busy = 0U;
        printf("SHOT: write BMP header failed res=%d err=%d\r\n", (int)res, g_shot.last_error);
        return -1;
    }

    printf("SHOT: saving %s %lu x %lu ...\r\n",
           g_shot.filename,
           (unsigned long)width,
           (unsigned long)height);

    /* BMP positive-height image stores rows from bottom to top. */
    for(y = (int32_t)height - 1; y >= 0; y--) {
        memset(g_bmp_line_buf, 0, row_bytes);

        for(x = 0U; x < width; x++) {
            c = (uint16_t)lcd_read_point((uint16_t)x, (uint16_t)y);

            /* RGB565 -> 24-bit BGR */
            r = (uint8_t)((((uint32_t)c >> 11) & 0x1FU) * 255U / 31U);
            g = (uint8_t)((((uint32_t)c >> 5)  & 0x3FU) * 255U / 63U);
            b = (uint8_t)(( ((uint32_t)c)       & 0x1FU) * 255U / 31U);

            g_bmp_line_buf[x * 3U + 0U] = b;
            g_bmp_line_buf[x * 3U + 1U] = g;
            g_bmp_line_buf[x * 3U + 2U] = r;
        }

        res = f_write(&g_bmp_file, g_bmp_line_buf, row_bytes, &bw);
        if((res == FR_OK) && (bw != row_bytes)) {
            res = FR_DISK_ERR;
        }
        if(res != FR_OK) {
            sdshot_set_fatfs_error(res);
            f_close(&g_bmp_file);
            g_shot.busy = 0U;
            printf("SHOT: write row y=%ld failed res=%d err=%d\r\n", (long)y, (int)res, g_shot.last_error);
            return -1;
        }
    }

    res = f_sync(&g_bmp_file);
    if(res != FR_OK) {
        sdshot_set_fatfs_error(res);
        f_close(&g_bmp_file);
        g_shot.busy = 0U;
        printf("SHOT: f_sync failed res=%d err=%d\r\n", (int)res, g_shot.last_error);
        return -1;
    }

    res = f_close(&g_bmp_file);
    if(res != FR_OK) {
        sdshot_set_fatfs_error(res);
        g_shot.busy = 0U;
        printf("SHOT: f_close failed res=%d err=%d\r\n", (int)res, g_shot.last_error);
        return -1;
    }

    g_shot.saved_count++;
    g_shot.last_error = 0;
    g_shot.busy = 0U;

    printf("SHOT saved: %s\r\n", g_shot.filename);
    return 0;
}

const char *sdshot_filename(void)
{
    return (g_shot.filename[0] != '\0') ? g_shot.filename : "NONE";
}

const char *sdshot_status_text(void)
{
    if(g_shot.busy != 0U) {
        return "BUSY";
    }

    if(g_shot.last_error != 0) {
        return "ERR";
    }

    if(g_shot.filename[0] != '\0') {
        return "SAVED";
    }

    return "READY";
}

int sdshot_last_error(void)
{
    return g_shot.last_error;
}

