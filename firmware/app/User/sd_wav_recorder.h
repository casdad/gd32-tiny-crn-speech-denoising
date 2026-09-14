#ifndef __SD_WAV_RECORDER_H
#define __SD_WAV_RECORDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAVREC_SAMPLE_RATE      48000U
#define WAVREC_CHANNELS         2U    /* stereo: L=RAW input, R=CLEAN/RNNoise output */
#define WAVREC_BITS_PER_SAMPLE  16U

uint8_t wavrec_init(void);
uint8_t wavrec_is_ready(void);
uint8_t wavrec_is_recording(void);
int wavrec_start(void);
int wavrec_stop(void);
void wavrec_write_sample(int16_t sample);
void wavrec_write_pair(int16_t raw_sample, int16_t clean_sample);
void wavrec_write_pcm16(const int16_t *samples, uint32_t count);
void wavrec_write_raw_frame_as_stereo(const int16_t *raw_samples, uint32_t count);
const char *wavrec_filename(void);
const char *wavrec_status_text(void);
uint32_t wavrec_data_bytes(void);
uint32_t wavrec_elapsed_seconds(void);
uint32_t wavrec_sd_capacity_kb(void);
int wavrec_last_error(void);

/* LCD screen shot: save current LCD content to SD card as 24-bit BMP. */
int sdshot_save_lcd_bmp(void);
const char *sdshot_filename(void);
const char *sdshot_status_text(void);
int sdshot_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* __SD_WAV_RECORDER_H */
