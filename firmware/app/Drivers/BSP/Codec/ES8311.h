
#ifndef _ES8311_H
#define _ES8311_H

#ifdef __cplusplus
extern "C" {
#endif

#include "gd32h7xx.h"
#include "./SYSTEM/delay/delay.h"

void rcu_config(void);
void i2c_config(void);
void i2s_player(void);
void i2s_record(void);
void ES8311_Codec(void);
void ES8311_Reset(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_RECORDER_H