/*
 * main.c
 * 实验4串口通信/音频工程 + LT7680 LCD 同步显示版本
 *
 * 功能：
 * 1. 串口 printf 的关键提示通过 uart_lcd_show() 同步显示到 LCD；
 * 2. PC 通过串口发送一帧数据后，串口和 LCD 同步显示 RX 内容；
 * 3. LCD 刷新只放在主循环/初始化流程中，不放到中断里。
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#define APP_SERIAL_LOG_ENABLE 1
#if !APP_SERIAL_LOG_ENABLE
#define printf(...) ((void)0)
#endif

/* Conservative USART trim: keep USART/printf/LT7680 dependencies safe. */
#define APP_UART_CONTROL_ENABLE          0U
#define APP_RUNTIME_SERIAL_STATUS_ENABLE 0U
#define APP_PIN_WAVE_DEBUG_ENABLE        0U

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/delay.h"
#include "./BSP/LED/led.h"
#include "./BSP/LCD/lcd.h"
#include "./BSP/Codec/ES7210.h"
#include "./BSP/Codec/ES8311.h"
#include "./BSP/MPU/mpu.h"
#include "./BSP/SDRAM/sdram.h"
#include "./BSP/TOUCH/touch.h"
#include "main.h"
#include "../RNNoise_src/rnnoise.h"
#include "sd_wav_recorder.h"
#include "crn_model.h"
#include "ff.h"
#include "perf_meter.h"

/* SAI 音频参数 */
#define SAI_SAMPLE_RATE        48000
#define SAI_MCLK_FREQ          (256 * SAI_SAMPLE_RATE)

/* LCD 文本显示参数 */
#define LCD_TEXT_X             10
#define LCD_TEXT_START_Y       30
#define LCD_TEXT_LINE_H        30
#define LCD_TEXT_FONT_SIZE     24
#define LCD_TEXT_BUF_SIZE      128

/* RNNoise 固定帧长：官方模型每帧处理 480 个 PCM 采样点 */
#define RNNOISE_FRAME_SIZE     480

/*
 * 本版本不使用扬声器和 ES8311，只保留 ES7210 采集 + RNNoise/LCD 监控。
 * 好处：不初始化播放 Codec，不启动 SAI Block1，不驱动 PE3，避免和音频输入线冲突。
 */
#define ENABLE_ES8311_OUTPUT   0

/*
 * RNNoise 输入通道选择：
 * 0 = 自动选择当前块内 ch1~ch4 中能量最大的一路；
 * 1/2/3/4 = 固定使用对应 TDM 通道。
 * LCD 波形正式观察阶段建议固定通道，当前先固定 CH2。
 * 如果 CH2 效果不好，再改成 1 测 CH1。
 */
#define RNNOISE_MIC_CHANNEL    2

/*
 * RNNoise 输入软件增益：
 * 0=x1, 1=x2, 2=x4, 3=x8, 4=x16。
 * 你现在 raw/clean 很低，48 kHz 版先用 x4，避免前级削顶导致 RNNoise 把人声误判为噪声。
 */
#define RNNOISE_MIC_GAIN_SHIFT 2

/*
 * LCD RAW/CLEAN 双波形显示配置。
 * 注意：LCD 只做趋势观察，不要把刷新频率调太高，否则会影响实时音频处理。
 */
#define ENABLE_LCD_WAVEFORM        1
#define LCD_WAVE_POINTS            160U
#define LCD_WAVE_REFRESH_BUFFERS_IDLE  20U   /* 非录音约 200ms 刷新一次 */
#define LCD_WAVE_REFRESH_BUFFERS_REC   100U  /* 录音中约 1s 刷新一次，避免拖慢触摸/写卡 */
#define LCD_LEVEL_MAX              3000U
#define LCD_WAVE_DRAW_GAIN_SHIFT   3

/*
 * 波形坐标轴显示配置。
 * X 轴表示当前缓存中的采样点序号，Y 轴表示 16bit PCM 幅度。
 * 为了不压缩波形太多，只在波形框内部显示简化坐标：+32K / 0 / -32K，X:0 / N/2 / N。
 */
#define LCD_WAVE_AXIS_ENABLE       1
#define LCD_WAVE_AXIS_LEFT_W       40U
#define LCD_WAVE_AXIS_BOTTOM_H     20U
#define LCD_WAVE_AXIS_RIGHT_PAD    6U
#define LCD_WAVE_AXIS_TOP_PAD      2U

/* LCD2 触摸功能：PF6/PF7 软件 I2C，支持常见 GT911 / FT5x06 触摸控制器。 */
#define ENABLE_TOUCH_UI            1

/*
 * 触摸调试扫描频率。
 * 不再依赖 audio_cnt，即使音频 DMA 没跑起来，也能看到触摸坐标打印。
 * 数值越小扫描越频繁；如果担心影响音频，可以改成 80000U 或 100000U。
 */
#define TOUCH_POLL_LOOP_DIV        500U
#define TOUCH_POLL_LOOP_DIV_RECORD 2U     /* 录音中约每 20ms 扫一次触摸，方便按 STOP */
#define STOP_DRAIN_MAX_FRAMES 50U     /* STOP 时最多补写 0.5s 队列，避免 BUF 很高时卡屏 */
#define TOUCH_RELEASE_CONFIRM_COUNT 3U  /* 连续 3 次读不到触摸才确认松手 */
#define TOUCH_ACTION_COOLDOWN_FRAMES 40U /* 两次按钮动作至少间隔约 0.4s */
#define TOUCH_FORCE_UNLOCK_FRAMES 120U   /* 约 1.2s 强制解锁，避免 GT911 release 漏报后卡在 STOP */

/* CRN 实时显示控制。
 * CRN_REALTIME_WAVE_ENABLE=1 时，CRN 模式不再只画 mask 曲线，
 * 而是复用离线链路中的 STFT -> mask -> iFFT -> OLA，实时重建一帧 clean PCM 给 LCD 显示。
 * 注意：录音中仍然禁止实时 CRN，保证 SD 写 RAW 不丢帧；STOP 后继续离线生成 CRNxxxx.WAV。
 */
#define CRN_REALTIME_WAVE_ENABLE     1U    /* 1=CRN 实时降噪波形显示；0=退回只显示 mask 曲线 */
/*
 * CRN clean 波形是真 FFT+CRN+iFFT+OLA，GD32 上连续 10ms 跑一次会明显挤占触摸/I2C。
 * SAFE 版只在 LCD 需要刷新前和低频预览节拍上运行 CRN，其余 DMA 帧只更新 RAW/录音。
 * 这样波形仍会周期性刷新，但触摸、REC/STOP 和长时间运行优先保证不卡死。
 */
#define CRN_REALTIME_WAVE_PROCESS_DIV 20U   /* 非录音 CRN 每 20 个音频帧最多跑一次 clean 重建，约 200ms，与 LCD 刷新同步 */
#define CRN_PREVIEW_INVOKE_DIV       10U   /* 仅在 CRN_REALTIME_WAVE_ENABLE=0 时使用：约 100ms 更新一次 mask */
#define CRN_PREVIEW_QUEUE_KEEP       1U    /* 非录音 CRN 预览只保留最新 1 帧，旧帧直接丢弃，防止长时间堆积 */
#define CRN_REALTIME_PREVIEW_ENABLE  1U    /* 保留 CRN 状态/MASK min-max 显示 */

/* SD 卡 WAV 录音功能。点击底部 REC 按钮开始录制，再次点击 STOP 保存文件。 */
#define ENABLE_SD_WAV_RECORD       1

/* SD 卡 LCD 截图功能。点击底部 SHOT 按钮，保存当前屏幕为 24-bit BMP。 */
#define ENABLE_SD_SCREENSHOT       1


/* 双引擎模式：
 * RAW = 原始直通显示；
 * RNN = 原 RNNoise 实时波形；
 * CRN = CRN 实时降噪波形显示：PCM -> STFT -> mask -> iFFT -> OLA -> clean PCM。
 * 录音阶段仍只保存 RAW，STOP 后继续离线生成 CRNxxxx.WAV。
 */
typedef enum {
    DENOISE_MODE_RAW = 0,
    DENOISE_MODE_RNN = 1,
    DENOISE_MODE_CRN = 2
} denoise_mode_t;

static denoise_mode_t g_denoise_mode = DENOISE_MODE_RNN;

/* 全局缓冲区标志位 */
volatile uint8_t current_tdm_buffer = 0;
volatile uint8_t current_output_buffer = 0;
volatile uint8_t buffer_ready = 0;
volatile uint16_t audio_rx_q_wr = 0U;
volatile uint16_t audio_rx_q_rd = 0U;
volatile uint16_t audio_rx_q_count = 0U;
volatile uint16_t audio_rx_q_highwater = 0U;
volatile uint32_t audio_rx_q_drop = 0U;
volatile uint8_t audio_rx_q_accept = 1U;
volatile uint8_t output_buffer_ready[2] = {0, 0};
volatile uint8_t dma_playing_buffer = 0;

/* DMA 中断计数器, 用于统计 DMA 触发次数 */
volatile uint32_t dma_tx_irq_cnt = 0;
volatile uint32_t dma_rx_irq_cnt = 0;

/* LCD 当前显示行 */
static uint16_t g_lcd_text_y = LCD_TEXT_START_Y;

/* RNNoise 状态与内部 480 点分帧缓冲区
 * 说明：main.h 中的 SAMPLE_AUDIO_SIZE 可以先保持 1024，
 * 这里会自动把连续 PCM 流拆成 480 点一帧送入 RNNoise。
 */
static DenoiseState *g_rnnoise_st = NULL;
static uint8_t g_rnnoise_enable = 0;
static uint8_t g_rnnoise_has_output = 0;
static uint16_t g_rnnoise_in_index = 0;
static uint16_t g_rnnoise_out_index = RNNOISE_FRAME_SIZE;
static uint32_t g_rnnoise_frame_cnt = 0;
static float g_rnnoise_last_vad = 0.0f;
static float g_rnnoise_in_frame[RNNOISE_FRAME_SIZE];
static float g_rnnoise_out_frame[RNNOISE_FRAME_SIZE];

/*
 * RNNoise 状态静态内存。
 * rnnoise_create(NULL) 在 Keil 裸机工程里依赖 malloc/heap，heap 不够时会返回 NULL。
 * 这里改成 rnnoise_get_size() + rnnoise_init()，避免因为堆太小导致 RNNoise init failed。
 * 如果串口提示 state buffer too small，把下面数值加大到 120000 或更高。
 */
#define RNNOISE_STATE_MEM_SIZE 80000U
static uint8_t g_rnnoise_state_mem[RNNOISE_STATE_MEM_SIZE] __attribute__((aligned(8)));

/* RNNoise LCD 监控面板数据 */
static uint32_t g_raw_abs_level = 0;
static uint32_t g_out_abs_level = 0;
static uint8_t g_vad_percent = 0;
static uint8_t g_selected_mic_channel = RNNOISE_MIC_CHANNEL;
static uint8_t g_runtime_mic_channel = RNNOISE_MIC_CHANNEL;
static uint8_t g_runtime_gain_shift = RNNOISE_MIC_GAIN_SHIFT;
static uint8_t g_rnnoise_bypass = 0U;
static uint8_t g_touch_ready = 0U;
static uint16_t g_touch_last_x = 0U;
static uint16_t g_touch_last_y = 0U;
static uint8_t g_touch_has_last = 0U;

/*
 * STOP 防连击锁。
 * RN ON 时，点击 STOP 后会阻塞执行离线 RNNoise 处理。处理期间如果手指/触摸芯片
 * 仍保留在底部按钮区域，UI 从 STOP 切回 REC 后，同一次触摸可能被旧的强制解锁逻辑
 * 当成一次新的 REC 点击，导致“处理完成后自动又开始录音”。
 * 置 1 后，必须连续读到无触摸释放确认，才允许下一次按钮动作。
 */
static volatile uint8_t g_touch_require_release = 0U;

/*
 * 录音实时优先策略：
 * - 录音时永远只保存 RAW，绝不在实时路径里跑 RNNoise；
 * - RN 按钮只决定 STOP 后是否自动离线生成 RNNxxxx.WAV；
 * - 这样可以避免 RN=ON 时 BUF 堆满、触摸卡死。
 */
static uint8_t g_offline_rnn_busy = 0U;
static uint8_t g_offline_rnn_percent = 0U;
static int g_offline_rnn_last_error = 0;
static char g_offline_rnn_filename[16] = "NONE";
static char g_offline_rnn_input_filename[16] = "NONE";
static int16_t g_record_raw_frame[SAMPLE_AUDIO_SIZE] __attribute__((aligned(32)));

/* CRN 推理状态。
 * 旧的 crn_probe_update_from_pcm() 仍保留作 mask 曲线回退；
 * CRN_REALTIME_WAVE_ENABLE=1 时，LCD CLEAN 面板显示 iFFT/OLA 重建出来的 CRN clean 波形。
 */
static uint8_t g_crn_ready = 0U;
static uint32_t g_crn_frame_cnt = 0U;
static int g_crn_last_error = 0;
static int8_t g_crn_context[CRN_MODEL_CONTEXT][CRN_MODEL_BANDS];
static int8_t g_crn_input_q[CRN_MODEL_INPUT_SIZE];
static int8_t g_crn_output_q[CRN_MODEL_OUTPUT_SIZE];
static uint32_t g_crn_mask_abs_level = 0U;
static int8_t g_crn_mask_min = 0;
static int8_t g_crn_mask_max = 0;
static uint32_t g_crn_preview_div_cnt = 0U;

/* CRN 第二版离线音频后处理参数。
 * 与 Python 训练/推理保持同一套特征定义：
 * 48 kHz, hop=480, win=960, n_fft=1024, 32 band, 8 帧上下文。
 * 说明：GD32 生成的模型输入/输出量化参数没有单独导出成宏。
 * 这里按常见 full-int8 logistic 输出约定处理：
 *   input feature: log1p(10*band_mag) 近似映射到 [-128,127]
 *   output mask  : int8 [-128,127] 近似反量化到 [0,1]
 * 若后续从 tflite/报告中拿到精确 scale/zero_point，只需要调下面几个宏。
 */
#define CRN_OFFLINE_N_FFT              1024U
#define CRN_OFFLINE_BINS               513U
#define CRN_OFFLINE_WIN                960U
#define CRN_OFFLINE_HOP                480U
#define CRN_INPUT_FEATURE_Q_MUL        24.0f
#define CRN_INPUT_FEATURE_Q_ZERO       (-128)
#define CRN_OUTPUT_MASK_DEQ_ZERO       (-128)
#define CRN_OUTPUT_MASK_DEQ_SCALE      (1.0f / 255.0f)
#define CRN_OLA_GAIN                   1.6f


/*
 * LCD 显示模式控制。
 * 0 = 启动阶段允许 uart_lcd_show() 在 LCD 上打印调试信息；
 * 1 = 仪表盘模式，禁止 uart_lcd_show() 再写 LCD，避免旧文字压到波形后面。
 */
static uint8_t g_dashboard_mode = 0U;
static uint8_t g_ui_need_full_clear = 1U;

/* LCD RAW/CLEAN 波形缓存：每次 process_buffer() 抽样保存一屏数据 */
static int16_t g_lcd_raw_wave[LCD_WAVE_POINTS];
static int16_t g_lcd_clean_wave[LCD_WAVE_POINTS];
static uint16_t g_lcd_wave_count = 0;

/* 函数声明 */
void rcu_config(void);
void dac_dma_config(void);
void adc_dma_config(void);
void sai_config(void);
void process_buffer(uint32_t (*buf)[2 * SAMPLE_AUDIO_SIZE]);
static void process_audio_frame(const uint32_t *frame);
static uint8_t audio_rx_queue_peek(uint16_t *idx);
static void audio_rx_queue_consume(void);
static void audio_rx_queue_trim_to(uint16_t keep);
static void audio_rx_queue_reset(void);
static void audio_rx_queue_pause(void);
static void audio_rx_queue_drain_all(void);
static void audio_rx_queue_drain_limited(uint16_t max_frames);
static uint8_t sdram_audio_queue_test(void);
static void sdram_audio_queue_clear(void);
static void uart_lcd_show(const char *fmt, ...);
static void make_lcd_safe_string(char *dst, uint16_t dst_size, uint8_t *src, uint16_t src_len);
static void rnnoise_port_init(void);
static int16_t rnnoise_stream_process_sample(int16_t mic_sample);
static int16_t clip16(float x);
static uint32_t abs16_level(int16_t x);
static int16_t mic_apply_gain(int16_t x);
static void rnnoise_stream_reset(void);
static int rnnoise_offline_process_last_file(void);
static int crn_offline_process_last_file(void);
static const char *rnnoise_mode_text(void);
static const char *denoise_mode_text(void);
static const char *clean_panel_title(void);
static void denoise_mode_next(void);
static void crn_probe_reset(void);
static void crn_probe_update_from_pcm(const int16_t *pcm, uint16_t n);
static int16_t crn_curve_sample(uint32_t i, uint32_t total);
static void crn_realtime_reset(void);
static void crn_realtime_process_frame(const int16_t raw[CRN_OFFLINE_HOP],
                                       int16_t raw_for_display[CRN_OFFLINE_HOP],
                                       int16_t clean_for_display[CRN_OFFLINE_HOP]);
static void lcd_draw_level_bar(uint16_t x,
                               uint16_t y,
                               uint16_t w,
                               uint16_t h,
                               uint32_t level,
                               uint32_t color);
static void rnnoise_lcd_panel_update(void);
static void ui_dashboard_request_full_clear(void);
static void lcd_wave_reset(void);
static void lcd_wave_push(int16_t raw, int16_t clean);
static uint16_t lcd_wave_to_y(int16_t sample, uint16_t top, uint16_t height);
static void lcd_draw_waveform(const int16_t *data,
                              uint16_t points,
                              uint16_t x0,
                              uint16_t y0,
                              uint16_t w,
                              uint16_t h,
                              uint32_t color);
static uint32_t pin_edge_count(uint32_t gpio_periph, uint32_t pin, uint32_t loops);
static void sai_pin_wave_debug(const char *tag);
static void touch_ui_init(void);
static void touch_ui_poll(void);
static void touch_ui_service(uint8_t force_audio_clock);
static void touch_ui_handle(uint16_t x, uint16_t y);
static void touch_ui_handle_point(const touch_point_t *pt);
/* 页面优化版触摸按钮由 ui_layout_update() 动态计算，不再固定在顶部。 */

/*
 * 串口和 LCD 同步显示函数。
 * 注意：不要在中断函数里调用本函数。
 */
static void uart_lcd_show(const char *fmt, ...)
{
    char buf[LCD_TEXT_BUF_SIZE];
    char lcd_buf[LCD_TEXT_BUF_SIZE];
    va_list args;
    uint16_t i;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* 串口显示 */
    printf("%s\r\n", buf);

    /*
     * 仪表盘启动后，LCD 只由 rnnoise_lcd_panel_update() 统一绘制。
     * 这里继续写 LCD 会造成你看到的“上一页文字在波形背后”。
     */
    if(g_dashboard_mode != 0U) {
        return;
    }

    /* LCD 只适合直接显示英文、数字、常用 ASCII；其它字符替换成 '.' */
    for(i = 0; i < sizeof(lcd_buf) - 1; i++) {
        if(buf[i] == '\0') {
            break;
        }

        if((buf[i] >= ' ') && (buf[i] <= '~')) {
            lcd_buf[i] = buf[i];
        } else {
            lcd_buf[i] = '.';
        }
    }
    lcd_buf[i] = '\0';

    /* 超出屏幕时清屏回到第一行 */
    if(g_lcd_text_y > (lcddev.height - LCD_TEXT_LINE_H - 5)) {
        g_lcd_text_y = LCD_TEXT_START_Y;
        lcd_clear(BLACK);
    }

    lcd_fill(0, g_lcd_text_y, lcddev.width - 1, g_lcd_text_y + LCD_TEXT_LINE_H - 1, BLACK);
    lcd_show_string(LCD_TEXT_X,
                    g_lcd_text_y,
                    lcddev.width - 2 * LCD_TEXT_X,
                    LCD_TEXT_FONT_SIZE,
                    LCD_TEXT_FONT_SIZE,
                    lcd_buf,
                    CYAN);

    g_lcd_text_y += LCD_TEXT_LINE_H;
}

/*
 * 把串口接收缓冲区整理成 LCD 可显示字符串。
 * 回车、换行、中文或不可见字符会显示成 '.'。
 */
static void make_lcd_safe_string(char *dst, uint16_t dst_size, uint8_t *src, uint16_t src_len)
{
    uint16_t i;
    uint16_t copy_len;

    if(dst_size == 0) {
        return;
    }

    copy_len = (src_len < (dst_size - 1)) ? src_len : (dst_size - 1);

    for(i = 0; i < copy_len; i++) {
        if((src[i] >= ' ') && (src[i] <= '~')) {
            dst[i] = (char)src[i];
        } else {
            dst[i] = '.';
        }
    }

    dst[copy_len] = '\0';
}


/*
 * SAI 引脚波形粗检测。
 * 作用：不用示波器时，粗略判断 MCLK/BCLK/FS/SD 线上有没有跳变。
 * 注意：这个方法只能判断“是否跳变”，不能精确测频率；最终仍以示波器/逻辑分析仪为准。
 */
static uint32_t pin_edge_count(uint32_t gpio_periph, uint32_t pin, uint32_t loops)
{
    uint32_t cnt = 0;
    uint32_t last;
    uint32_t now;

    last = gpio_input_bit_get(gpio_periph, pin);

    while(loops--) {
        now = gpio_input_bit_get(gpio_periph, pin);
        if(now != last) {
            cnt++;
            last = now;
        }
    }

    return cnt;
}

/*
 * 打印当前 SAI 四根关键线的跳变计数。
 * 当前工程接线定义：
 * PG7 -> SAI0_MCLK0 / ES7210 MCLK
 * PE5 -> SAI0_SCK0  / ES7210 BCLK
 * PE4 -> SAI0_FS0   / ES7210 LRCK/FS
 * PD6 -> SAI0_SD0   / ES7210 DOUT/SDOUT
 */
static void sai_pin_wave_debug(const char *tag)
{
    uint32_t mclk_cnt;
    uint32_t bclk_cnt;
    uint32_t fs_cnt;
    uint32_t sd_cnt;

    mclk_cnt = pin_edge_count(GPIOG, GPIO_PIN_7, 300000U);
    bclk_cnt = pin_edge_count(GPIOE, GPIO_PIN_5, 300000U);
    fs_cnt   = pin_edge_count(GPIOE, GPIO_PIN_4, 300000U);
    sd_cnt   = pin_edge_count(GPIOD, GPIO_PIN_6, 300000U);

    printf("WAVE %s: PG7_MCLK=%lu PE5_BCLK=%lu PE4_FS=%lu PD6_SD=%lu\r\n",
           tag,
           (unsigned long)mclk_cnt,
           (unsigned long)bclk_cnt,
           (unsigned long)fs_cnt,
           (unsigned long)sd_cnt);
}


/*
 * float 转 int16_t，防止 RNNoise 输出超过 16 bit PCM 范围。
 */
static int16_t clip16(float x)
{
    if(x > 32767.0f) {
        return 32767;
    }

    if(x < -32768.0f) {
        return -32768;
    }

    return (int16_t)x;
}

/*
 * 计算 16 bit PCM 的绝对值幅度。
 */
static uint32_t abs16_level(int16_t x)
{
    if(x < 0) {
        return (uint32_t)(-x);
    }

    return (uint32_t)x;
}

/*
 * 给麦克风输入做软件增益，并做 16 bit 饱和限幅。
 */
static int16_t mic_apply_gain(int16_t x)
{
    int32_t y;

    if(g_runtime_gain_shift == 0U) {
        return x;
    }

    y = ((int32_t)x) << g_runtime_gain_shift;

    if(y > 32767) {
        y = 32767;
    } else if(y < -32768) {
        y = -32768;
    }

    return (int16_t)y;
}

static const char *denoise_mode_text(void)
{
    switch(g_denoise_mode) {
    case DENOISE_MODE_RAW:
        return "RAW";
    case DENOISE_MODE_RNN:
        return "RNN";
    case DENOISE_MODE_CRN:
        return "CRN";
    default:
        return "UNK";
    }
}

/*
 * 右侧 CLEAN 面板标题。
 * 原代码在 RAW/RNN 模式下都显示 "CLEAN / RNNOISE OUT"，
 * 导致 RAW 截图或非 RNN 截图看起来像 RNNoise 模式。
 * 这里按当前模式明确显示，截图时一眼能分辨 RAW/RNN/CRN。
 */
static const char *clean_panel_title(void)
{
    switch(g_denoise_mode) {
    case DENOISE_MODE_RAW:
        return "RAW PASS-THROUGH";
    case DENOISE_MODE_RNN:
        return "CLEAN / RNNOISE OUT";
    case DENOISE_MODE_CRN:
#if CRN_REALTIME_WAVE_ENABLE
        return "CRN CLEAN WAVE";
#else
        return "CRN MASK CURVE";
#endif
    default:
        return "CLEAN OUTPUT";
    }
}

static const char *rnnoise_mode_text(void)
{
#if ENABLE_SD_WAV_RECORD
    if(wavrec_is_recording() != 0U) {
        return denoise_mode_text();
    }
#endif

    if(g_offline_rnn_busy != 0U) {
        return "PROC";
    }

    return denoise_mode_text();
}

static void denoise_mode_apply(void)
{
    /* g_rnnoise_bypass 保留给旧 RNNoise 处理分支：只有 RNN 模式才跑 RNNoise。 */
    g_rnnoise_bypass = (g_denoise_mode == DENOISE_MODE_RNN) ? 0U : 1U;
}

static void denoise_mode_next(void)
{
    if(g_denoise_mode == DENOISE_MODE_RAW) {
        g_denoise_mode = DENOISE_MODE_RNN;
    } else if(g_denoise_mode == DENOISE_MODE_RNN) {
        g_denoise_mode = DENOISE_MODE_CRN;
    } else {
        g_denoise_mode = DENOISE_MODE_RAW;
    }

    denoise_mode_apply();
    rnnoise_stream_reset();
    crn_probe_reset();
    crn_realtime_reset();

#if ENABLE_SD_WAV_RECORD
    if(wavrec_is_recording() == 0U) {
        audio_rx_queue_reset();
    }
#else
    audio_rx_queue_reset();
#endif

    ui_dashboard_request_full_clear();
}

static void crn_probe_reset(void)
{
    memset(g_crn_context, 0, sizeof(g_crn_context));
    memset(g_crn_input_q, 0, sizeof(g_crn_input_q));
    memset(g_crn_output_q, 0, sizeof(g_crn_output_q));
    g_crn_mask_abs_level = 0U;
    g_crn_mask_min = 0;
    g_crn_mask_max = 0;
    g_crn_preview_div_cnt = 0U;
}

static void crn_probe_update_from_pcm(const int16_t *pcm, uint16_t n)
{
    uint16_t b;
    uint16_t i;
    uint16_t start;
    uint16_t end;
    uint32_t sum;
    uint32_t avg;
    int32_t q;
    uint32_t abs_sum = 0U;
    int8_t v;
    int8_t min_v;
    int8_t max_v;

    if((pcm == NULL) || (n == 0U) || (g_crn_ready == 0U)) {
        return;
    }

    /* 时间上下文左移，最后一行填当前块的 32 个粗略能量特征。
     * 这不是最终语音增强特征，仅用于第一版验证 CRN invoke 与 UI 显示。
     */
    memmove(&g_crn_context[0][0], &g_crn_context[1][0],
            (CRN_MODEL_CONTEXT - 1U) * CRN_MODEL_BANDS * sizeof(int8_t));

    for(b = 0U; b < CRN_MODEL_BANDS; b++) {
        start = (uint16_t)(((uint32_t)b * (uint32_t)n) / CRN_MODEL_BANDS);
        end = (uint16_t)(((uint32_t)(b + 1U) * (uint32_t)n) / CRN_MODEL_BANDS);
        if(end <= start) {
            end = start + 1U;
        }
        if(end > n) {
            end = n;
        }

        sum = 0U;
        for(i = start; i < end; i++) {
            sum += abs16_level(pcm[i]);
        }
        avg = sum / (uint32_t)(end - start);

        /* 粗略把 16bit 幅度压到 int8。真实版将用 log1p(10*band_mag) + 量化参数。 */
        q = (int32_t)(avg / 128U);
        if(q > 127) {
            q = 127;
        }
        g_crn_context[CRN_MODEL_CONTEXT - 1U][b] = (int8_t)q;
    }

    for(i = 0U; i < CRN_MODEL_CONTEXT; i++) {
        memcpy(&g_crn_input_q[i * CRN_MODEL_BANDS], g_crn_context[i], CRN_MODEL_BANDS);
    }

    if(crn_model_invoke_256(g_crn_input_q, g_crn_output_q) == 0) {
        g_crn_frame_cnt++;
        min_v = g_crn_output_q[0];
        max_v = g_crn_output_q[0];
        abs_sum = 0U;
        for(b = 0U; b < CRN_MODEL_OUTPUT_SIZE; b++) {
            v = g_crn_output_q[b];
            if(v < min_v) {
                min_v = v;
            }
            if(v > max_v) {
                max_v = v;
            }
            abs_sum += (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
        }
        g_crn_mask_min = min_v;
        g_crn_mask_max = max_v;
        g_crn_mask_abs_level = (abs_sum * 20U) / CRN_MODEL_OUTPUT_SIZE;
        g_crn_last_error = 0;
    } else {
        g_crn_last_error = crn_model_last_error();
    }
}

static int16_t crn_curve_sample(uint32_t i, uint32_t total)
{
    uint32_t b;
    int32_t v;

    if(total == 0U) {
        return 0;
    }

    b = (i * CRN_MODEL_OUTPUT_SIZE) / total;
    if(b >= CRN_MODEL_OUTPUT_SIZE) {
        b = CRN_MODEL_OUTPUT_SIZE - 1U;
    }

    v = ((int32_t)g_crn_output_q[b]) * 256;
    if(v > 32767) {
        v = 32767;
    } else if(v < -32768) {
        v = -32768;
    }

    return (int16_t)v;
}

static void rnnoise_stream_reset(void)
{
    uint16_t i;

    for(i = 0U; i < RNNOISE_FRAME_SIZE; i++) {
        g_rnnoise_in_frame[i] = 0.0f;
        g_rnnoise_out_frame[i] = 0.0f;
    }

    g_rnnoise_in_index = 0U;
    g_rnnoise_out_index = RNNOISE_FRAME_SIZE;
    g_rnnoise_has_output = 0U;
    g_rnnoise_last_vad = 0.0f;
    g_vad_percent = 0U;

    if((g_rnnoise_enable != 0U) && (g_rnnoise_st != NULL)) {
        rnnoise_init(g_rnnoise_st, NULL);
    }
}


/*
 * 在 LCD 上画一个音量条。
 * RAW/CLEAN 使用 LCD_LEVEL_MAX 做满格参考，便于低幅度麦克风也能看清变化。
 */
static void lcd_draw_level_bar(uint16_t x,
                               uint16_t y,
                               uint16_t w,
                               uint16_t h,
                               uint32_t level,
                               uint32_t color)
{
    uint16_t fill_w;

    if(level > LCD_LEVEL_MAX) {
        level = LCD_LEVEL_MAX;
    }

    fill_w = (uint16_t)((level * w) / LCD_LEVEL_MAX);

    lcd_draw_rectangle(x, y, x + w, y + h, GRAY);

    if((w > 2U) && (h > 2U)) {
        lcd_fill(x + 1U, y + 1U, x + w - 1U, y + h - 1U, BLACK);
    }

    if(fill_w > 2U) {
        lcd_fill(x + 1U, y + 1U, x + fill_w, y + h - 1U, color);
    }
}

static void lcd_wave_reset(void)
{
#if ENABLE_LCD_WAVEFORM
    g_lcd_wave_count = 0U;
#endif
}

static void lcd_wave_push(int16_t raw, int16_t clean)
{
#if ENABLE_LCD_WAVEFORM
    if(g_lcd_wave_count < LCD_WAVE_POINTS) {
        g_lcd_raw_wave[g_lcd_wave_count] = raw;
        g_lcd_clean_wave[g_lcd_wave_count] = clean;
        g_lcd_wave_count++;
    }
#else
    (void)raw;
    (void)clean;
#endif
}

static uint16_t lcd_wave_to_y(int16_t sample, uint16_t top, uint16_t height)
{
    int32_t mid;
    int32_t amp;
    int32_t scaled;
    int32_t y;

    mid = (int32_t)top + ((int32_t)height / 2);
    amp = ((int32_t)height / 2) - 4;

#if LCD_WAVE_DRAW_GAIN_SHIFT > 0
    scaled = ((int32_t)sample) << LCD_WAVE_DRAW_GAIN_SHIFT;
#else
    scaled = (int32_t)sample;
#endif

    if(scaled > 32767) {
        scaled = 32767;
    } else if(scaled < -32768) {
        scaled = -32768;
    }

    y = mid - ((scaled * amp) / 32768);

    if(y < (int32_t)top) {
        y = (int32_t)top;
    } else if(y > ((int32_t)top + (int32_t)height - 1)) {
        y = (int32_t)top + (int32_t)height - 1;
    }

    return (uint16_t)y;
}

static void lcd_draw_waveform(const int16_t *data,
                              uint16_t points,
                              uint16_t x0,
                              uint16_t y0,
                              uint16_t w,
                              uint16_t h,
                              uint32_t color)
{
    uint16_t i;
    uint16_t last_x;
    uint16_t last_y;
    uint16_t x;
    uint16_t y;

    if(points < 2U) {
        return;
    }

    /* 中线，用于观察静音回零 */
    lcd_draw_line(x0, y0 + h / 2U, x0 + w, y0 + h / 2U, GRAY);

    last_x = x0;
    last_y = lcd_wave_to_y(data[0], y0, h);

    for(i = 1U; i < points; i++) {
        x = x0 + (uint16_t)(((uint32_t)i * (uint32_t)w) / (uint32_t)(points - 1U));
        y = lcd_wave_to_y(data[i], y0, h);

        lcd_draw_line(last_x, last_y, x, y, color);

        last_x = x;
        last_y = y;
    }
}

/* ---------------- Optimized LCD dashboard UI ----------------
 * 页面布局目标：
 * 1. 顶部状态栏：系统标题、触摸芯片、RN/VAD 快速状态；
 * 2. 中部：RAW 输入波形 + CLEAN 降噪后波形；
 * 3. 右侧或下方：实时状态面板；
 * 4. 底部：大触摸按钮，避免原来顶部小按钮不容易按。
 *
 * 注意：LCD 只显示 ASCII 字符，避免直接显示中文。
 */
#define UI_MARGIN                 8U
#define UI_HEADER_H               40U
#define UI_FOOTER_H               58U
#define UI_BUTTON_COUNT           6U

#define UI_BTN_CH                 0U
#define UI_BTN_GAIN               1U
#define UI_BTN_RN                 2U
#define UI_BTN_REC                3U
#define UI_BTN_SHOT               4U
#define UI_BTN_REFRESH            5U

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} ui_rect_t;

typedef struct {
    uint8_t landscape;
    ui_rect_t header;
    ui_rect_t raw;
    ui_rect_t clean;
    ui_rect_t status;
    ui_rect_t footer;
    ui_rect_t btn[UI_BUTTON_COUNT];
} ui_layout_t;

static uint16_t ui_min_u16(uint16_t a, uint16_t b)
{
    return (a < b) ? a : b;
}

static uint16_t ui_rect_x2(const ui_rect_t *r)
{
    uint32_t x2;

    if(r->w == 0U) {
        return r->x;
    }

    x2 = (uint32_t)r->x + (uint32_t)r->w - 1U;
    if(x2 >= lcddev.width) {
        x2 = lcddev.width - 1U;
    }

    return (uint16_t)x2;
}

static uint16_t ui_rect_y2(const ui_rect_t *r)
{
    uint32_t y2;

    if(r->h == 0U) {
        return r->y;
    }

    y2 = (uint32_t)r->y + (uint32_t)r->h - 1U;
    if(y2 >= lcddev.height) {
        y2 = lcddev.height - 1U;
    }

    return (uint16_t)y2;
}

static uint8_t ui_point_in_rect(uint16_t x, uint16_t y, const ui_rect_t *r)
{
    if((x >= r->x) && (x <= ui_rect_x2(r)) &&
       (y >= r->y) && (y <= ui_rect_y2(r))) {
        return 1U;
    }

    return 0U;
}

static void ui_fill_rect(const ui_rect_t *r, uint32_t color)
{
    if((r->w == 0U) || (r->h == 0U)) {
        return;
    }

    lcd_fill(r->x, r->y, ui_rect_x2(r), ui_rect_y2(r), color);
}

static void ui_draw_rect(const ui_rect_t *r, uint32_t color)
{
    if((r->w == 0U) || (r->h == 0U)) {
        return;
    }

    lcd_draw_rectangle(r->x, r->y, ui_rect_x2(r), ui_rect_y2(r), color);
}

static void ui_show_text_bg(uint16_t x,
                            uint16_t y,
                            uint16_t w,
                            uint8_t size,
                            const char *text,
                            uint32_t color,
                            uint32_t bg_color)
{
    uint16_t x2;
    uint16_t y2;

    if((text == NULL) || (w < size)) {
        return;
    }

    if(x >= lcddev.width || y >= lcddev.height) {
        return;
    }

    x2 = x + w - 1U;
    if(x2 >= lcddev.width) {
        x2 = lcddev.width - 1U;
    }

    y2 = y + size - 1U;
    if(y2 >= lcddev.height) {
        y2 = lcddev.height - 1U;
    }

    /*
     * 先用指定背景色清掉文本区域，避免短字符串覆盖长字符串时留下残影。
     * 注意：按钮被选中时背景不是 BLACK，如果这里固定清 BLACK，
     * 黑色文字会被画在黑色底上，导致 CH / GAIN 按钮看不到字。
     */
    lcd_fill(x, y, x2, y2, bg_color);
    g_back_color = bg_color;
    lcd_show_string(x, y, w, size, size, (char *)text, color);
}

static void ui_show_text(uint16_t x,
                         uint16_t y,
                         uint16_t w,
                         uint8_t size,
                         const char *text,
                         uint32_t color)
{
    ui_show_text_bg(x, y, w, size, text, color, BLACK);
}

static const char *ui_noise_level_text(void)
{
    if(g_raw_abs_level < 350U) {
        return "LOW";
    }

    if(g_raw_abs_level < 1200U) {
        return "MID";
    }

    return "HIGH";
}

static uint32_t ui_noise_level_color(void)
{
    if(g_raw_abs_level < 350U) {
        return GREEN;
    }

    if(g_raw_abs_level < 1200U) {
        return YELLOW;
    }

    return RED;
}

static void ui_layout_update(ui_layout_t *l)
{
    uint16_t w;
    uint16_t h;
    uint16_t m;
    uint16_t footer_y;
    uint16_t content_y;
    uint16_t content_h;
    uint16_t status_w;
    uint16_t wave_w;
    uint16_t button_gap;
    uint16_t button_w;
    uint16_t i;

    w = lcddev.width;
    h = lcddev.height;
    m = UI_MARGIN;

    l->landscape = (w >= h) ? 1U : 0U;

    l->header.x = 0U;
    l->header.y = 0U;
    l->header.w = w;
    l->header.h = UI_HEADER_H;

    footer_y = h - UI_FOOTER_H;
    l->footer.x = 0U;
    l->footer.y = footer_y;
    l->footer.w = w;
    l->footer.h = UI_FOOTER_H;

    content_y = UI_HEADER_H + m;
    if(footer_y > (content_y + m)) {
        content_h = footer_y - content_y - m;
    } else {
        content_h = 100U;
    }

    if(l->landscape != 0U) {
        status_w = (w >= 760U) ? 170U : 145U;
        wave_w = (w - status_w - 4U * m) / 2U;

        l->raw.x = m;
        l->raw.y = content_y;
        l->raw.w = wave_w;
        l->raw.h = content_h;

        l->clean.x = l->raw.x + wave_w + m;
        l->clean.y = content_y;
        l->clean.w = wave_w;
        l->clean.h = content_h;

        l->status.x = l->clean.x + wave_w + m;
        l->status.y = content_y;
        l->status.w = w - l->status.x - m;
        l->status.h = content_h;
    } else {
        uint16_t status_h;
        uint16_t wave_h;

        status_h = 140U;
        if(content_h > (status_h + 3U * m + 120U)) {
            wave_h = (content_h - status_h - 2U * m) / 2U;
        } else {
            wave_h = 100U;
            status_h = 100U;
        }

        l->raw.x = m;
        l->raw.y = content_y;
        l->raw.w = w - 2U * m;
        l->raw.h = wave_h;

        l->clean.x = m;
        l->clean.y = l->raw.y + wave_h + m;
        l->clean.w = w - 2U * m;
        l->clean.h = wave_h;

        l->status.x = m;
        l->status.y = l->clean.y + wave_h + m;
        l->status.w = w - 2U * m;
        l->status.h = ui_min_u16(status_h, h - l->status.y - UI_FOOTER_H - m);
    }

    button_gap = 8U;
    button_w = (w - (UI_BUTTON_COUNT + 1U) * button_gap) / UI_BUTTON_COUNT;

    for(i = 0U; i < UI_BUTTON_COUNT; i++) {
        l->btn[i].x = button_gap + i * (button_w + button_gap);
        l->btn[i].y = footer_y + 8U;
        l->btn[i].w = button_w;
        l->btn[i].h = UI_FOOTER_H - 16U;
    }
}

static void ui_draw_header(const ui_layout_t *l)
{
    char buf[128];
    ui_rect_t line;

    ui_fill_rect(&l->header, DARKBLUE);
    ui_show_text(8U, 5U, 260U, 16U, "AI Noise Suppression", WHITE);

    snprintf(buf,
             sizeof(buf),
             "MODE:%s  VAD:%u%%  SD:%s  TP:%s  BMP:%s  REC:%02lu:%02lu",
             rnnoise_mode_text(),
             (unsigned int)g_vad_percent,
             wavrec_is_ready() ? "OK" : wavrec_status_text(),
             g_touch_ready ? "OK" : "NONE",
             sdshot_status_text(),
             (unsigned long)(wavrec_elapsed_seconds() / 60U),
             (unsigned long)(wavrec_elapsed_seconds() % 60U));
    ui_show_text(8U, 22U, l->header.w - 16U, 16U, buf, wavrec_is_recording() ? RED : CYAN);

    line.x = 0U;
    line.y = l->header.h - 1U;
    line.w = l->header.w;
    line.h = 1U;
    ui_fill_rect(&line, CYAN);
}

static void ui_draw_button(const ui_rect_t *r,
                           const char *text,
                           uint32_t color,
                           uint8_t active)
{
    uint16_t text_x;
    uint16_t text_y;

    (void)active;

    /*
     * 按钮只保留方框和文字，不填充背景。
     * footer 每次重画前已经被 ui_fill_rect(&l->footer, BLACK) 清黑，
     * 所以这里不要再填充蓝色/黄色/绿色背景。
     */
    ui_draw_rect(r, color);

    text_x = r->x + 6U;
    text_y = r->y + (r->h > 18U ? ((r->h - 16U) / 2U) : 2U);
    ui_show_text_bg(text_x, text_y, r->w - 12U, 16U, text, color, BLACK);
}

static void ui_draw_rnn_progress_footer(const ui_layout_t *l)
{
    char buf[96];
    uint16_t bar_x;
    uint16_t bar_y;
    uint16_t bar_w;
    uint16_t bar_h;
    uint32_t level;

    ui_fill_rect(&l->footer, BLACK);

    snprintf(buf,
             sizeof(buf),
             "POST PROCESS %u%%",
             (unsigned int)g_offline_rnn_percent);
    ui_show_text_bg(l->footer.x + 12U,
                    l->footer.y + 6U,
                    l->footer.w - 24U,
                    16U,
                    buf,
                    MAGENTA,
                    BLACK);

    snprintf(buf,
             sizeof(buf),
             "%s -> %s",
             g_offline_rnn_input_filename,
             g_offline_rnn_filename);
    ui_show_text_bg(l->footer.x + 12U,
                    l->footer.y + 24U,
                    l->footer.w - 24U,
                    16U,
                    buf,
                    CYAN,
                    BLACK);

    bar_x = l->footer.x + 12U;
    bar_w = l->footer.w - 24U;
    bar_h = 12U;
    bar_y = l->footer.y + l->footer.h - bar_h - 6U;
    level = ((uint32_t)g_offline_rnn_percent * LCD_LEVEL_MAX) / 100U;
    lcd_draw_level_bar(bar_x, bar_y, bar_w, bar_h, level, MAGENTA);
}

static void ui_draw_footer_buttons(const ui_layout_t *l)
{
    char buf[32];

    if(g_offline_rnn_busy != 0U) {
        ui_draw_rnn_progress_footer(l);
        return;
    }

    ui_fill_rect(&l->footer, BLACK);

    snprintf(buf, sizeof(buf), "CH%u", (unsigned int)g_runtime_mic_channel);
    ui_draw_button(&l->btn[UI_BTN_CH], buf, CYAN, 1U);

    snprintf(buf, sizeof(buf), "MIC x%u", (unsigned int)(1U << g_runtime_gain_shift));
    ui_draw_button(&l->btn[UI_BTN_GAIN], buf, YELLOW, 1U);

    ui_draw_button(&l->btn[UI_BTN_RN], denoise_mode_text(), GREEN, 1U);

#if ENABLE_SD_WAV_RECORD
    ui_draw_button(&l->btn[UI_BTN_REC], wavrec_is_recording() ? "STOP" : "REC", wavrec_is_recording() ? RED : CYAN, wavrec_is_recording());
#else
    ui_draw_button(&l->btn[UI_BTN_REC], "REC N/A", GRAY, 0U);
#endif

#if ENABLE_SD_SCREENSHOT
    ui_draw_button(&l->btn[UI_BTN_SHOT], "SHOT", MAGENTA, 0U);
#else
    ui_draw_button(&l->btn[UI_BTN_SHOT], "SHOT N/A", GRAY, 0U);
#endif

    ui_draw_button(&l->btn[UI_BTN_REFRESH], "REFRESH", WHITE, 0U);
}


static void ui_draw_wave_axes(const ui_rect_t *panel,
                              uint16_t plot_x,
                              uint16_t plot_y,
                              uint16_t plot_w,
                              uint16_t plot_h,
                              uint16_t points)
{
    char buf[16];
    uint16_t x1;
    uint16_t y1;
    uint16_t mid_y;
    uint16_t q1_y;
    uint16_t q3_y;
    uint16_t q1_x;
    uint16_t mid_x;
    uint16_t q3_x;
    uint16_t label_x;
    uint16_t bottom_y;
    uint16_t n_label;

    if((plot_w < 30U) || (plot_h < 30U)) {
        return;
    }

    x1 = plot_x + plot_w;
    y1 = plot_y + plot_h;
    mid_y = plot_y + plot_h / 2U;
    q1_y = plot_y + plot_h / 4U;
    q3_y = plot_y + (plot_h * 3U) / 4U;
    q1_x = plot_x + plot_w / 4U;
    mid_x = plot_x + plot_w / 2U;
    q3_x = plot_x + (plot_w * 3U) / 4U;

    /* 外框与坐标轴 */
    lcd_draw_rectangle(plot_x, plot_y, x1, y1, GRAYBLUE);
    lcd_draw_line(plot_x, plot_y, plot_x, y1, GRAY);      /* Y axis */
    lcd_draw_line(plot_x, mid_y, x1, mid_y, GRAY);        /* zero axis */
    lcd_draw_line(plot_x, y1, x1, y1, GRAY);              /* X axis */

    /* 参考网格线：幅度四分位与时间四分位 */
    lcd_draw_line(plot_x, q1_y, x1, q1_y, GRAYBLUE);
    lcd_draw_line(plot_x, q3_y, x1, q3_y, GRAYBLUE);
    lcd_draw_line(q1_x, plot_y, q1_x, y1, GRAYBLUE);
    lcd_draw_line(mid_x, plot_y, mid_x, y1, GRAYBLUE);
    lcd_draw_line(q3_x, plot_y, q3_x, y1, GRAYBLUE);

    /* Y 轴标签：16bit PCM 幅度，单位约为 sample count。 */
    label_x = panel->x + 4U;
    ui_show_text_bg(label_x, plot_y, 34U, 16U, "+32K", GRAY, BLACK);

    if(mid_y >= 8U) {
        ui_show_text_bg(label_x + 12U, mid_y - 8U, 22U, 16U, "0", GRAY, BLACK);
    }

    if(y1 >= 16U) {
        ui_show_text_bg(label_x, y1 - 16U, 34U, 16U, "-32K", GRAY, BLACK);
    }

    /* X 轴标签：当前波形缓存采样点序号。 */
    bottom_y = y1 + 3U;
    if(bottom_y + 16U < ui_rect_y2(panel)) {
        n_label = (points > 0U) ? (points - 1U) : 0U;

        ui_show_text_bg(plot_x, bottom_y, 24U, 16U, "0", GRAY, BLACK);

        snprintf(buf, sizeof(buf), "%u", (unsigned int)(n_label / 2U));
        if(mid_x > 18U) {
            ui_show_text_bg(mid_x - 14U, bottom_y, 36U, 16U, buf, GRAY, BLACK);
        }

        snprintf(buf, sizeof(buf), "%u", (unsigned int)n_label);
        if(x1 > 36U) {
            ui_show_text_bg(x1 - 30U, bottom_y, 36U, 16U, buf, GRAY, BLACK);
        }
    }
}

static void ui_draw_wave_panel(const ui_rect_t *r,
                               const char *title,
                               const int16_t *data,
                               uint16_t points,
                               uint32_t color)
{
    uint16_t title_y;
    uint16_t wave_y;
    uint16_t wave_h;
    uint16_t plot_x;
    uint16_t plot_y;
    uint16_t plot_w;
    uint16_t plot_h;

    ui_fill_rect(r, BLACK);
    ui_draw_rect(r, GRAY);

    title_y = r->y + 5U;
    ui_show_text(r->x + 8U, title_y, r->w - 16U, 16U, title, color);

    if(r->h > 34U) {
        wave_y = r->y + 26U;
        wave_h = r->h - 32U;
    } else {
        wave_y = r->y + 2U;
        wave_h = r->h - 4U;
    }

#if LCD_WAVE_AXIS_ENABLE
    if((r->w > (LCD_WAVE_AXIS_LEFT_W + LCD_WAVE_AXIS_RIGHT_PAD + 30U)) &&
       (wave_h > (LCD_WAVE_AXIS_BOTTOM_H + LCD_WAVE_AXIS_TOP_PAD + 30U))) {
        plot_x = r->x + LCD_WAVE_AXIS_LEFT_W;
        plot_y = wave_y + LCD_WAVE_AXIS_TOP_PAD;
        plot_w = r->w - LCD_WAVE_AXIS_LEFT_W - LCD_WAVE_AXIS_RIGHT_PAD;
        plot_h = wave_h - LCD_WAVE_AXIS_TOP_PAD - LCD_WAVE_AXIS_BOTTOM_H;

        ui_draw_wave_axes(r, plot_x, plot_y, plot_w, plot_h, points);

        if(points >= 2U) {
            lcd_draw_waveform(data,
                              points,
                              plot_x,
                              plot_y,
                              plot_w,
                              plot_h,
                              color);
        }
    } else
#endif
    {
        if((r->w > 16U) && (wave_h > 8U)) {
            lcd_draw_waveform(data,
                              points,
                              r->x + 8U,
                              wave_y,
                              r->w - 16U,
                              wave_h,
                              color);
        }
    }
}

static void ui_draw_metric_line(uint16_t x,
                                uint16_t y,
                                uint16_t w,
                                const char *name,
                                const char *value,
                                uint32_t color)
{
    char buf[48];

    snprintf(buf, sizeof(buf), "%s:%s", name, value);
    ui_show_text(x, y, w, 16U, buf, color);
}

static void ui_draw_status_panel(const ui_layout_t *l)
{
    char buf[48];
    uint16_t x;
    uint16_t y;
    uint16_t line_h;
    uint16_t bar_x;
    uint16_t bar_w;
    uint16_t bar_h;

    ui_fill_rect(&l->status, BLACK);
    ui_draw_rect(&l->status, GRAY);

    x = l->status.x + 8U;
    y = l->status.y + 6U;
    line_h = 20U;

    ui_show_text(x, y, l->status.w - 16U, 16U, "SYSTEM STATUS", WHITE);
    y += line_h;

    snprintf(buf, sizeof(buf), "%u%%", (unsigned int)g_vad_percent);
    ui_draw_metric_line(x, y, l->status.w - 16U, "VAD", buf, CYAN);
    y += line_h;

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)g_raw_abs_level);
    ui_draw_metric_line(x, y, l->status.w - 16U, "RAW", buf, YELLOW);
    y += line_h;

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)g_out_abs_level);
    ui_draw_metric_line(x, y, l->status.w - 16U, "CLEAN", buf, GREEN);
    y += line_h;

    ui_draw_metric_line(x, y, l->status.w - 16U, "NOISE", ui_noise_level_text(), ui_noise_level_color());
    y += line_h;

    snprintf(buf, sizeof(buf), "CH%u x%u",
             (unsigned int)g_selected_mic_channel,
             (unsigned int)(1U << g_runtime_gain_shift));
    ui_draw_metric_line(x, y, l->status.w - 16U, "MIC", buf, WHITE);
    y += line_h;

    ui_draw_metric_line(x, y, l->status.w - 16U, "MODE", rnnoise_mode_text(), (g_denoise_mode == DENOISE_MODE_RAW) ? YELLOW : GREEN);
    y += line_h;

#if CRN_REALTIME_PREVIEW_ENABLE
#if CRN_REALTIME_WAVE_ENABLE
    snprintf(buf, sizeof(buf), "%s SAFE/%u %ld", crn_model_status_text(),
             (unsigned int)CRN_REALTIME_WAVE_PROCESS_DIV, (long)g_crn_frame_cnt);
#else
    snprintf(buf, sizeof(buf), "%s MASK %ld", crn_model_status_text(), (long)g_crn_frame_cnt);
#endif
    ui_draw_metric_line(x, y, l->status.w - 16U, "CRN", buf, g_crn_ready ? MAGENTA : RED);
    y += line_h;

    snprintf(buf, sizeof(buf), "%d/%d", (int)g_crn_mask_min, (int)g_crn_mask_max);
    ui_draw_metric_line(x, y, l->status.w - 16U, "MASK", buf, MAGENTA);
    y += line_h;
#else
    snprintf(buf, sizeof(buf), "%s OFFLINE", crn_model_status_text());
    ui_draw_metric_line(x, y, l->status.w - 16U, "CRN", buf, g_crn_ready ? MAGENTA : RED);
    y += line_h;

    ui_draw_metric_line(x, y, l->status.w - 16U, "MASK", "RT OFF", CYAN);
    y += line_h;
#endif

#if ENABLE_SD_WAV_RECORD
    ui_draw_metric_line(x, y, l->status.w - 16U, "SD", wavrec_status_text(), wavrec_is_ready() ? GREEN : RED);
    y += line_h;

    if(wavrec_is_ready() == 0U) {
        snprintf(buf, sizeof(buf), "ERR %d", wavrec_last_error());
        ui_draw_metric_line(x, y, l->status.w - 16U, "SDERR", buf, RED);
        y += line_h;
    } else {
        ui_draw_metric_line(x, y, l->status.w - 16U, "WAV", wavrec_is_recording() ? "RAW FAST" : "RAW/RNN/CRN", CYAN);
        y += line_h;
    }

    snprintf(buf, sizeof(buf), "%02lu:%02lu",
             (unsigned long)(wavrec_elapsed_seconds() / 60U),
             (unsigned long)(wavrec_elapsed_seconds() % 60U));
    ui_draw_metric_line(x, y, l->status.w - 16U, "TIME", buf, wavrec_is_recording() ? RED : CYAN);
    y += line_h;

    ui_draw_metric_line(x, y, l->status.w - 16U, "FILE", wavrec_filename(), WHITE);
    y += line_h;

    if(g_offline_rnn_busy != 0U) {
        snprintf(buf, sizeof(buf), "%u%%", (unsigned int)g_offline_rnn_percent);
        ui_draw_metric_line(x, y, l->status.w - 16U, "PROC", buf, MAGENTA);
        y += line_h;
    } else if(g_offline_rnn_last_error != 0) {
        snprintf(buf, sizeof(buf), "ERR %d", g_offline_rnn_last_error);
        ui_draw_metric_line(x, y, l->status.w - 16U, "POST", buf, RED);
        y += line_h;
    } else {
        ui_draw_metric_line(x, y, l->status.w - 16U, "POST", g_offline_rnn_filename, MAGENTA);
        y += line_h;
    }
#endif

#if ENABLE_SD_SCREENSHOT
    ui_draw_metric_line(x, y, l->status.w - 16U, "BMP", sdshot_status_text(), sdshot_last_error() == 0 ? MAGENTA : RED);
    y += line_h;
    ui_draw_metric_line(x, y, l->status.w - 16U, "SHOT", sdshot_filename(), WHITE);
    y += line_h;
#endif

    /* TOUCH 坐标不再放右侧状态栏，避免右侧信息过多溢出屏幕。
     * 顶部状态栏保留 TP:OK / TP:NONE 即可。
     */

    if(l->status.h > 190U) {
        bar_x = x;
        bar_w = l->status.w - 16U;
        if(bar_w > 120U) {
            bar_w = 120U;
        }
        bar_h = 10U;

        if((y + 3U * (bar_h + 17U)) < ui_rect_y2(&l->status)) {
            ui_show_text(x, y, l->status.w - 16U, 16U, "LEVEL", WHITE);
            y += 18U;

            lcd_draw_level_bar(bar_x, y, bar_w, bar_h,
                               (uint32_t)g_vad_percent * (LCD_LEVEL_MAX / 100U), CYAN);
            y += bar_h + 10U;

            lcd_draw_level_bar(bar_x, y, bar_w, bar_h, g_raw_abs_level, YELLOW);
            y += bar_h + 10U;

            lcd_draw_level_bar(bar_x, y, bar_w, bar_h, g_out_abs_level, GREEN);
        }
    }
}

static void ui_dashboard_request_full_clear(void)
{
    g_dashboard_mode = 1U;
    g_ui_need_full_clear = 1U;
}

/*
 * LCD RAW/CLEAN 双波形仪表盘。
 * 显示内容：标题栏 + RAW/CLEAN 波形 + 系统状态 + 底部大触摸按钮。
 */
static void rnnoise_lcd_panel_update(void)
{
#if ENABLE_LCD_WAVEFORM
    ui_layout_t layout;

    ui_layout_update(&layout);

    /*
     * 第一次进入仪表盘或按 REFRESH 时，先整屏清黑。
     * 这一步专门清掉启动阶段 uart_lcd_show() 留下的 Step/Touch/RNNoise 文字。
     */
    if((g_dashboard_mode == 0U) || (g_ui_need_full_clear != 0U)) {
        g_dashboard_mode = 1U;
        g_ui_need_full_clear = 0U;
        g_lcd_text_y = LCD_TEXT_START_Y;
        g_back_color = BLACK;
        lcd_clear(BLACK);
    }

    ui_draw_header(&layout);
    ui_draw_wave_panel(&layout.raw,
                       "RAW MIC INPUT",
                       g_lcd_raw_wave,
                       g_lcd_wave_count,
                       YELLOW);
    ui_draw_wave_panel(&layout.clean,
                       clean_panel_title(),
                       g_lcd_clean_wave,
                       g_lcd_wave_count,
                       GREEN);
    ui_draw_status_panel(&layout);
    ui_draw_footer_buttons(&layout);
#endif
}


/* ---------------- LCD2 Touch UI ----------------
 * 触摸线：PF6=SCL，PF7=SDA，PF8=RST，PG3=INT。
 * 底部大按钮功能：CH 切换 / MIC x4-x8-x16 / RN ON-OFF / REC/STOP / SHOT / REFRESH。
 */
static void touch_ui_init(void)
{
#if ENABLE_TOUCH_UI
    touch_init();
    g_touch_ready = touch_is_ready();

    if(g_touch_ready != 0U) {
        uart_lcd_show("Touch init ok: %s", touch_device_name());
    } else {
        uart_lcd_show("Touch init failed: no ACK PF6/PF7");
    }
#endif
}

static uint8_t touch_ui_button_hit(uint16_t x, uint16_t y, const ui_layout_t *layout)
{
    uint8_t i;

    for(i = 0U; i < UI_BUTTON_COUNT; i++) {
        if(ui_point_in_rect(x, y, &layout->btn[i]) != 0U) {
            return i;
        }
    }

    return 0xFFU;
}

static void touch_ui_do_button(uint8_t btn)
{
#if ENABLE_TOUCH_UI
    if(btn == UI_BTN_CH) {
        if(g_runtime_mic_channel == 1U) {
            g_runtime_mic_channel = 2U;
        } else {
            g_runtime_mic_channel = 1U;
        }
        printf("Touch: CH%u\r\n", (unsigned int)g_runtime_mic_channel);
    } else if(btn == UI_BTN_GAIN) {
        /*
         * 48 kHz RNNoise 版默认增益是 x4。
         * 旧逻辑只在 x8 和 x16 之间切换：
         *   x4 -> 按一次变 x16 -> 再按变 x8 -> 再按变 x16，无法回到 x4。
         * 这里改成三档循环：x4 -> x8 -> x16 -> x4。
         */
        if(g_runtime_gain_shift <= 2U) {
            g_runtime_gain_shift = 3U;   /* x8 */
        } else if(g_runtime_gain_shift == 3U) {
            g_runtime_gain_shift = 4U;   /* x16 */
        } else {
            g_runtime_gain_shift = 2U;   /* x4 */
        }
        printf("Touch: GAIN x%u\r\n", (unsigned int)(1U << g_runtime_gain_shift));
    } else if(btn == UI_BTN_RN) {
        denoise_mode_next();
        printf("Touch: MODE %s\r\n", denoise_mode_text());
    } else if(btn == UI_BTN_REC) {
#if ENABLE_SD_WAV_RECORD
        if(wavrec_is_recording() != 0U) {
            g_touch_require_release = 1U;
            audio_rx_queue_pause();
            audio_rx_queue_drain_limited(STOP_DRAIN_MAX_FRAMES);
            if(wavrec_stop() == 0) {
                printf("Touch: REC STOP saved %s\r\n", wavrec_filename());
                if(g_denoise_mode == DENOISE_MODE_RNN) {
                    (void)rnnoise_offline_process_last_file();
                } else if(g_denoise_mode == DENOISE_MODE_CRN) {
                    (void)crn_offline_process_last_file();
                } else {
                    strncpy(g_offline_rnn_filename, "RAW", sizeof(g_offline_rnn_filename) - 1U);
                    g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
                }
                crn_realtime_reset();
                audio_rx_queue_reset();
            } else {
                printf("Touch: REC STOP failed err=%d\r\n", wavrec_last_error());
                audio_rx_queue_reset();
            }
        } else {
            if(wavrec_start() == 0) {
                audio_rx_queue_reset();
                g_offline_rnn_last_error = 0;
                g_offline_rnn_percent = 0U;
                strncpy(g_offline_rnn_input_filename, wavrec_filename(), sizeof(g_offline_rnn_input_filename) - 1U);
                g_offline_rnn_input_filename[sizeof(g_offline_rnn_input_filename) - 1U] = '\0';
                strncpy(g_offline_rnn_filename, (g_denoise_mode == DENOISE_MODE_RAW) ? "RAW" : "PENDING", sizeof(g_offline_rnn_filename) - 1U);
                g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
                rnnoise_stream_reset();
                crn_realtime_reset();
                printf("Touch: REC START RAW FAST %s\r\n", wavrec_filename());
            } else {
                printf("Touch: REC START failed err=%d\r\n", wavrec_last_error());
            }
        }
#else
        printf("Touch: REC disabled\r\n");
#endif
    } else if(btn == UI_BTN_SHOT) {
#if ENABLE_SD_SCREENSHOT
        if(wavrec_is_recording() != 0U) {
            printf("Touch: SHOT refused while recording\r\n");
        } else {
            /*
             * SHOT 会长时间读 LCD + 写 SD。
             * 如果手指还没有离开触摸屏，截图完成后下一轮扫描会再次识别为一次点击，
             * 造成“点一次保存两张图”。这里先锁定本次触摸，要求释放后才能再次触发。
             */
            g_touch_require_release = 1U;

            /* SHOT 会长时间读 LCD + 写 SD，主循环无法消费音频队列。
             * 非录音状态下先暂停 RX 入队，截图后清空统计，避免 BUF/DROP 被截图动作打满。
             */
            audio_rx_queue_pause();
            audio_rx_queue_drain_all();
            rnnoise_lcd_panel_update();
            if(sdshot_save_lcd_bmp() == 0) {
                printf("Touch: SHOT saved %s\r\n", sdshot_filename());
            } else {
                printf("Touch: SHOT failed err=%d\r\n", sdshot_last_error());
            }
            audio_rx_queue_reset();
            rnnoise_lcd_panel_update();
        }
#else
        printf("Touch: SHOT disabled\r\n");
#endif
    } else if(btn == UI_BTN_REFRESH) {
        printf("Touch: REFRESH\r\n");
        ui_dashboard_request_full_clear();
    }
#else
    (void)btn;
#endif
}

static uint8_t touch_ui_handle_candidate(uint16_t x, uint16_t y, uint8_t quiet_no_button)
{
#if ENABLE_TOUCH_UI
    ui_layout_t layout;
    uint8_t btn;

    g_touch_last_x = x;
    g_touch_last_y = y;
    g_touch_has_last = 1U;

    ui_layout_update(&layout);
    btn = touch_ui_button_hit(x, y, &layout);

    if(btn != 0xFFU) {
        touch_ui_do_button(btn);
        return 1U;
    }

    if(quiet_no_button == 0U) {
        printf("Touch: no button at x=%u y=%u\r\n", (unsigned int)x, (unsigned int)y);
    }
#else
    (void)x;
    (void)y;
    (void)quiet_no_button;
#endif

    return 0U;
}

static void touch_ui_handle(uint16_t x, uint16_t y)
{
#if ENABLE_TOUCH_UI
    (void)touch_ui_handle_candidate(x, y, 0U);
#else
    (void)x;
    (void)y;
#endif
}

/*
 * 触摸坐标自适应容错：
 * 有些 LCD2/GT911 板子的触摸坐标与 LCD 显示方向不一致，常见表现是：
 * 串口能打印 Touch point，但点击 REC 外观位置不会触发按钮。
 * 这里不强行要求用户反复改 TOUCH_SWAP_XY / INVERT 宏，而是对 raw 坐标尝试
 * 原始、XY 交换、X/Y 反向等常见组合；只要任意一种落在底部按钮矩形内，
 * 就按该按钮执行。这样 REC/STOP、CH、GAIN、RN、REFRESH 都能命中。
 */
static void touch_ui_handle_point(const touch_point_t *pt)
{
#if ENABLE_TOUCH_UI
    uint16_t w;
    uint16_t h;
    uint16_t rx;
    uint16_t ry;
    uint16_t ix;
    uint16_t iy;

    if(pt == NULL) {
        return;
    }

    w = lcddev.width;
    h = lcddev.height;
    rx = pt->raw_x;
    ry = pt->raw_y;

    /* 1. 先用 touch_map() 后的坐标。 */
    if(touch_ui_handle_candidate(pt->x, pt->y, 1U) != 0U) {
        return;
    }

    /* 2. 直接用 raw 坐标。 */
    if(touch_ui_handle_candidate(rx, ry, 1U) != 0U) {
        return;
    }

    /* 3. raw X/Y 交换。 */
    if(touch_ui_handle_candidate(ry, rx, 1U) != 0U) {
        return;
    }

    if(w > 0U) {
        ix = (rx >= w) ? 0U : (uint16_t)(w - 1U - rx);
        if(touch_ui_handle_candidate(ix, ry, 1U) != 0U) {
            return;
        }
    }

    if(h > 0U) {
        iy = (ry >= h) ? 0U : (uint16_t)(h - 1U - ry);
        if(touch_ui_handle_candidate(rx, iy, 1U) != 0U) {
            return;
        }
    }

    if((w > 0U) && (h > 0U)) {
        ix = (rx >= w) ? 0U : (uint16_t)(w - 1U - rx);
        iy = (ry >= h) ? 0U : (uint16_t)(h - 1U - ry);
        if(touch_ui_handle_candidate(ix, iy, 1U) != 0U) {
            return;
        }
    }

    /* 4. 交换后再按屏幕宽高反向，适配 480x800/800x480 坐标错位。 */
    if(w > 0U) {
        ix = (ry >= w) ? 0U : (uint16_t)(w - 1U - ry);
        if(touch_ui_handle_candidate(ix, rx, 1U) != 0U) {
            return;
        }
    }

    if(h > 0U) {
        iy = (rx >= h) ? 0U : (uint16_t)(h - 1U - rx);
        if(touch_ui_handle_candidate(ry, iy, 1U) != 0U) {
            return;
        }
    }

    if((w > 0U) && (h > 0U)) {
        ix = (ry >= w) ? 0U : (uint16_t)(w - 1U - ry);
        iy = (rx >= h) ? 0U : (uint16_t)(h - 1U - rx);
        if(touch_ui_handle_candidate(ix, iy, 1U) != 0U) {
            return;
        }
    }

    /* 仍然没有命中，则保留最后触摸点显示，方便看坐标偏到哪里。 */
    (void)touch_ui_handle_candidate(pt->x, pt->y, 0U);
#else
    (void)pt;
#endif
}

static void touch_ui_poll(void)
{
#if ENABLE_TOUCH_UI
    static uint8_t was_down = 0U;
    static uint8_t release_count = TOUCH_RELEASE_CONFIRM_COUNT;
    static uint8_t action_locked = 0U;
    static uint32_t last_action_dma = 0U;
    touch_point_t pt;
    uint32_t now_dma;
    uint8_t allow_action;

    if(g_touch_ready == 0U) {
        return;
    }

    now_dma = dma_rx_irq_cnt;

    /* GT911 偶尔会漏报 release，旧逻辑 was_down 一直为 1，
     * 结果 REC 后页面变 STOP，但再次点击 STOP 不会触发。
     */
    if((g_touch_require_release == 0U) &&
       (action_locked != 0U) &&
       (now_dma != 0U) &&
       ((now_dma - last_action_dma) >= TOUCH_FORCE_UNLOCK_FRAMES)) {
        action_locked = 0U;
        was_down = 0U;
        release_count = TOUCH_RELEASE_CONFIRM_COUNT;
    }

    if(touch_read(&pt) != 0U) {
        release_count = 0U;
        allow_action = 0U;

        if(g_touch_require_release != 0U) {
            was_down = 1U;
            action_locked = 1U;
            return;
        }

        if((was_down == 0U) && (action_locked == 0U)) {
            if((now_dma == 0U) || ((now_dma - last_action_dma) >= TOUCH_ACTION_COOLDOWN_FRAMES)) {
                allow_action = 1U;
            }
        }

        if(allow_action != 0U) {
            last_action_dma = now_dma;
            action_locked = 1U;

            /* 调试用：只在真正触发按钮动作时打印一次。 */
            printf("Touch action: x=%u y=%u raw_x=%u raw_y=%u dma=%lu\r\n",
                   (unsigned int)pt.x,
                   (unsigned int)pt.y,
                   (unsigned int)pt.raw_x,
                   (unsigned int)pt.raw_y,
                   (unsigned long)now_dma);

            touch_ui_handle_point(&pt);
            rnnoise_lcd_panel_update();
        }

        was_down = 1U;
    } else {
        if(release_count < TOUCH_RELEASE_CONFIRM_COUNT) {
            release_count++;
        }

        if(release_count >= TOUCH_RELEASE_CONFIRM_COUNT) {
            was_down = 0U;
            action_locked = 0U;
            g_touch_require_release = 0U;
        }
    }
#endif
}

static void touch_ui_service(uint8_t force_audio_clock)
{
#if ENABLE_TOUCH_UI
    static uint32_t touch_loop_cnt = 0U;
    static uint32_t touch_last_dma_rx = 0U;
    uint8_t use_audio_clock = force_audio_clock;

#if ENABLE_SD_WAV_RECORD
    if(wavrec_is_recording() != 0U) {
        use_audio_clock = 1U;
    }
#endif

    if(use_audio_clock != 0U) {
        uint32_t div = TOUCH_POLL_LOOP_DIV_RECORD;

        /* CRN 实时波形预览很耗时，切到 CRN 后触摸必须更优先，避免“波形在动但按钮没反应”。 */
        if(g_denoise_mode == DENOISE_MODE_CRN) {
            div = 1U;
        }

        if((uint32_t)(dma_rx_irq_cnt - touch_last_dma_rx) >= div) {
            touch_last_dma_rx = dma_rx_irq_cnt;
            touch_ui_poll();
        }
    } else {
        touch_loop_cnt++;
        if(touch_loop_cnt >= TOUCH_POLL_LOOP_DIV) {
            touch_loop_cnt = 0U;
            touch_ui_poll();
        }
    }
#else
    (void)force_audio_clock;
#endif
}

/*
 * 初始化 RNNoise。
 * rnnoise_create(NULL) 表示使用 rnn_data.c 中编译进固件的内置模型，
 * 不从 SD 卡或其它文件系统读取模型。
 */
static void rnnoise_port_init(void)
{
    uint16_t i;
    int rn_size;

    for(i = 0; i < RNNOISE_FRAME_SIZE; i++) {
        g_rnnoise_in_frame[i] = 0.0f;
        g_rnnoise_out_frame[i] = 0.0f;
    }

    g_rnnoise_in_index = 0;
    g_rnnoise_out_index = RNNOISE_FRAME_SIZE;
    g_rnnoise_has_output = 0;
    g_rnnoise_frame_cnt = 0;
    g_rnnoise_last_vad = 0.0f;
    g_rnnoise_enable = 0;
    g_rnnoise_st = NULL;

    /*
     * 不用 rnnoise_create(NULL)，避免 malloc/heap 不足。
     * 直接使用静态内存初始化 RNNoise 状态。
     */
    rn_size = rnnoise_get_size();
    printf("RNNoise state size=%d, static buffer=%u\r\n",
           rn_size,
           (unsigned int)RNNOISE_STATE_MEM_SIZE);

    if((rn_size > 0) && ((uint32_t)rn_size <= RNNOISE_STATE_MEM_SIZE)) {
        g_rnnoise_st = (DenoiseState *)g_rnnoise_state_mem;
        rnnoise_init(g_rnnoise_st, NULL);
        g_rnnoise_enable = 1;
        uart_lcd_show("RNNoise init ok static, GRU static");
    } else {
        g_rnnoise_enable = 0;
        uart_lcd_show("RNNoise state buffer too small");
    }
}

/*
 * 单采样点流式处理封装。
 * RNNoise 内部仍然按 480 点一帧处理，这里负责把连续 PCM 自动攒帧。
 * 输出会有约 480 个采样点的算法延迟，属于正常现象。
 */
static int16_t rnnoise_stream_process_sample(int16_t mic_sample)
{
    int16_t out_sample;

    if((g_rnnoise_enable == 0) || (g_rnnoise_st == NULL)) {
        return mic_sample;
    }

    /* 触摸按钮 RN ON/OFF：OFF 时直通，用于肉眼对比 RAW/CLEAN。 */
    if(g_rnnoise_bypass != 0U) {
        return mic_sample;
    }

    /* 先输出上一帧已经处理好的数据；刚启动时没有处理结果，先直通 */
    if((g_rnnoise_has_output != 0) && (g_rnnoise_out_index < RNNOISE_FRAME_SIZE)) {
        out_sample = clip16(g_rnnoise_out_frame[g_rnnoise_out_index]);
        g_rnnoise_out_index++;

        if(g_rnnoise_out_index >= RNNOISE_FRAME_SIZE) {
            g_rnnoise_has_output = 0;
        }
    } else {
        out_sample = mic_sample;
    }

    /* 收集当前输入采样点，满 480 点后送入 RNNoise */
    g_rnnoise_in_frame[g_rnnoise_in_index] = (float)mic_sample;
    g_rnnoise_in_index++;

    if(g_rnnoise_in_index >= RNNOISE_FRAME_SIZE) {
        if(g_rnnoise_frame_cnt < 5U) {
            printf("RN frame begin %u\r\n", (unsigned int)(g_rnnoise_frame_cnt + 1U));
        }

        g_rnnoise_last_vad = rnnoise_process_frame(g_rnnoise_st,
                                                   g_rnnoise_out_frame,
                                                   g_rnnoise_in_frame);
        g_rnnoise_frame_cnt++;

        if((g_rnnoise_frame_cnt <= 5U) || ((g_rnnoise_frame_cnt % 100U) == 0U)) {
            printf("RN frame done=%u vad=%f\r\n",
                   (unsigned int)g_rnnoise_frame_cnt,
                   g_rnnoise_last_vad);
        }

        g_rnnoise_in_index = 0;
        g_rnnoise_out_index = 0;
        g_rnnoise_has_output = 1;
    }

    return out_sample;
}


/* ---------------- Offline RNNoise post-process ----------------
 * 输入：停止录音后保存的 CMPxxxx.WAV。录音阶段写入 L=RAW, R=RAW。
 * 输出：RNNxxxx.WAV，L=RAW, R=RNNoise。
 * 注意：这是阻塞式离线处理，只在 STOP 后运行；录音过程中绝不运行 RNNoise。
 */
#define OFFLINE_WAV_HEADER_SIZE 44U
#define OFFLINE_RNN_MAX_FILE_NUM 9999U
static uint8_t g_offline_in_bytes[RNNOISE_FRAME_SIZE * 4U] __attribute__((aligned(32)));
static uint8_t g_offline_out_bytes[RNNOISE_FRAME_SIZE * 4U] __attribute__((aligned(32)));
static float g_offline_in_frame[RNNOISE_FRAME_SIZE];
static float g_offline_out_frame[RNNOISE_FRAME_SIZE];

static void offline_wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void offline_wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t offline_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int16_t offline_rd16s(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static void offline_make_wav_header(uint8_t h[OFFLINE_WAV_HEADER_SIZE], uint32_t pcm_bytes)
{
    memset(h, 0, OFFLINE_WAV_HEADER_SIZE);
    memcpy(&h[0], "RIFF", 4U);
    offline_wr32(&h[4], 36U + pcm_bytes);
    memcpy(&h[8], "WAVE", 4U);
    memcpy(&h[12], "fmt ", 4U);
    offline_wr32(&h[16], 16U);
    offline_wr16(&h[20], 1U);
    offline_wr16(&h[22], 2U);
    offline_wr32(&h[24], WAVREC_SAMPLE_RATE);
    offline_wr32(&h[28], WAVREC_SAMPLE_RATE * 2U * 16U / 8U);
    offline_wr16(&h[32], 2U * 16U / 8U);
    offline_wr16(&h[34], 16U);
    memcpy(&h[36], "data", 4U);
    offline_wr32(&h[40], pcm_bytes);
}

static int offline_make_rnn_file_from_input(const char *input_name,
                                            char *display,
                                            uint32_t display_len,
                                            char *path,
                                            uint32_t path_len)
{
    uint32_t num = 0U;
    uint32_t i;

    if((input_name == NULL) || (display == NULL) || (path == NULL)) {
        return -203;
    }

    /*
     * Recorder names are CMPxxxx.WAV.  The offline RNN output must keep the
     * same numeric suffix so CMP0017.WAV -> RNN0017.WAV.  Do not search the
     * first free RNN file number, otherwise input/output numbers drift.
     */
    if(strncmp(input_name, "CMP", 3U) != 0) {
        return -204;
    }

    for(i = 3U; i < 7U; i++) {
        if((input_name[i] < '0') || (input_name[i] > '9')) {
            return -205;
        }
        num = num * 10U + (uint32_t)(input_name[i] - '0');
    }

    if((input_name[7] != '.') || (input_name[8] != 'W') ||
       (input_name[9] != 'A') || (input_name[10] != 'V')) {
        return -206;
    }

    snprintf(display, display_len, "RNN%04lu.WAV", (unsigned long)num);
    snprintf(path, path_len, "0:/RNN%04lu.WAV", (unsigned long)num);
    return 0;
}

static int rnnoise_offline_process_last_file(void)
{
#if ENABLE_SD_WAV_RECORD
    FIL fin;
    FIL fout;
    FRESULT res;
    UINT br;
    UINT bw;
    uint8_t header[OFFLINE_WAV_HEADER_SIZE];
    uint8_t out_header[OFFLINE_WAV_HEADER_SIZE];
    char in_path[32];
    char out_path[32];
    char out_name[16];
    uint32_t data_bytes;
    uint32_t total_frames;
    uint32_t frame_idx;
    uint32_t i;
    uint32_t written_pcm = 0U;
    int16_t raw;
    int16_t clean;
    int err;

    if((g_rnnoise_enable == 0U) || (g_rnnoise_st == NULL)) {
        g_offline_rnn_last_error = -200;
        strncpy(g_offline_rnn_filename, "NO RN", sizeof(g_offline_rnn_filename) - 1U);
        g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
        return -1;
    }

    if(wavrec_filename()[0] == 'N') {
        g_offline_rnn_last_error = -201;
        return -1;
    }

    snprintf(in_path, sizeof(in_path), "0:/%s", wavrec_filename());
    err = offline_make_rnn_file_from_input(wavrec_filename(), out_name, sizeof(out_name), out_path, sizeof(out_path));
    if(err != 0) {
        g_offline_rnn_last_error = err;
        return -1;
    }

    g_offline_rnn_busy = 1U;
    g_offline_rnn_percent = 0U;
    g_offline_rnn_last_error = 0;
    strncpy(g_offline_rnn_input_filename, wavrec_filename(), sizeof(g_offline_rnn_input_filename) - 1U);
    g_offline_rnn_input_filename[sizeof(g_offline_rnn_input_filename) - 1U] = '\0';
    strncpy(g_offline_rnn_filename, out_name, sizeof(g_offline_rnn_filename) - 1U);
    g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
    rnnoise_lcd_panel_update();

    printf("OFFLINE RNN: %s -> %s\r\n", in_path, out_path);


    res = f_open(&fin, in_path, FA_READ);
    if(res != FR_OK) {
        g_offline_rnn_last_error = -300 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    res = f_read(&fin, header, sizeof(header), &br);
    if((res != FR_OK) || (br != sizeof(header))) {
        f_close(&fin);
        g_offline_rnn_last_error = -320 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    data_bytes = offline_rd32(&header[40]);
    total_frames = data_bytes / (RNNOISE_FRAME_SIZE * 4U); /* input stereo 16bit */
    if(total_frames == 0U) {
        f_close(&fin);
        g_offline_rnn_last_error = -202;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    res = f_open(&fout, out_path, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK) {
        f_close(&fin);
        g_offline_rnn_last_error = -330 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    offline_make_wav_header(out_header, 0U);
    res = f_write(&fout, out_header, sizeof(out_header), &bw);
    if((res != FR_OK) || (bw != sizeof(out_header))) {
        f_close(&fin);
        f_close(&fout);
        g_offline_rnn_last_error = -340 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    rnnoise_init(g_rnnoise_st, NULL);

    for(frame_idx = 0U; frame_idx < total_frames; frame_idx++) {
        res = f_read(&fin, g_offline_in_bytes, sizeof(g_offline_in_bytes), &br);
        if((res != FR_OK) || (br != sizeof(g_offline_in_bytes))) {
            g_offline_rnn_last_error = -350 - (int)res;
            break;
        }

        for(i = 0U; i < RNNOISE_FRAME_SIZE; i++) {
            raw = offline_rd16s(&g_offline_in_bytes[i * 4U]); /* L channel */
            g_offline_in_frame[i] = (float)raw;
        }

        (void)rnnoise_process_frame(g_rnnoise_st, g_offline_out_frame, g_offline_in_frame);

        for(i = 0U; i < RNNOISE_FRAME_SIZE; i++) {
            raw = offline_rd16s(&g_offline_in_bytes[i * 4U]);
            clean = clip16(g_offline_out_frame[i]);

            g_offline_out_bytes[i * 4U + 0U] = (uint8_t)((uint16_t)raw & 0xFFU);
            g_offline_out_bytes[i * 4U + 1U] = (uint8_t)(((uint16_t)raw >> 8) & 0xFFU);
            g_offline_out_bytes[i * 4U + 2U] = (uint8_t)((uint16_t)clean & 0xFFU);
            g_offline_out_bytes[i * 4U + 3U] = (uint8_t)(((uint16_t)clean >> 8) & 0xFFU);
        }

        res = f_write(&fout, g_offline_out_bytes, sizeof(g_offline_out_bytes), &bw);
        if((res != FR_OK) || (bw != sizeof(g_offline_out_bytes))) {
            g_offline_rnn_last_error = -360 - (int)res;
            break;
        }

        written_pcm += sizeof(g_offline_out_bytes);

        if((frame_idx % 20U) == 0U) {
            g_offline_rnn_percent = (uint8_t)(((frame_idx + 1U) * 100U) / total_frames);
            rnnoise_lcd_panel_update();
        }
    }

    if(g_offline_rnn_last_error == 0) {
        offline_make_wav_header(out_header, written_pcm);
        res = f_lseek(&fout, 0U);
        if(res == FR_OK) {
            res = f_write(&fout, out_header, sizeof(out_header), &bw);
        }
        if((res != FR_OK) || (bw != sizeof(out_header))) {
            g_offline_rnn_last_error = -370 - (int)res;
        }
    }

    f_sync(&fout);
    f_close(&fout);
    f_close(&fin);

    rnnoise_stream_reset();
    g_offline_rnn_busy = 0U;

    if(g_offline_rnn_last_error == 0) {
        g_offline_rnn_percent = 100U;
        strncpy(g_offline_rnn_filename, out_name, sizeof(g_offline_rnn_filename) - 1U);
        g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
        printf("OFFLINE RNN saved: %s\r\n", out_name);
        rnnoise_lcd_panel_update();
        return 0;
    }

    printf("OFFLINE RNN failed err=%d\r\n", g_offline_rnn_last_error);
    rnnoise_lcd_panel_update();
    return -1;
#else
    return -1;
#endif
}


/* ---------------- Offline CRN post-process v2 ----------------
 * 输入：停止录音后保存的 CMPxxxx.WAV。录音阶段写入 L=RAW, R=RAW。
 * 输出：CRNxxxx.WAV，L=RAW, R=CRN enhanced。
 *
 * 处理链路：
 *   PCM -> Hann(960) -> FFT(1024) -> 32 band magnitude
 *   -> log1p(10*x) -> int8[8,32] -> CRN invoke -> 32 band mask
 *   -> 513 bin mask -> iFFT -> overlap-add -> PCM
 */
static const uint16_t g_crn_band_edges[CRN_MODEL_BANDS + 2U] = {
    0U, 4U, 10U, 18U, 27U, 36U, 47U, 58U, 70U, 83U, 96U, 110U,
    124U, 139U, 154U, 170U, 186U, 202U, 219U, 236U, 254U, 272U,
    290U, 309U, 328U, 347U, 367U, 387U, 407U, 427U, 448U, 469U,
    490U, 512U
};

static float g_crn_hann[CRN_OFFLINE_WIN];
static uint8_t g_crn_hann_ready = 0U;
static float g_crn_fft_re[CRN_OFFLINE_N_FFT];
static float g_crn_fft_im[CRN_OFFLINE_N_FFT];
static float g_crn_fft_mag[CRN_OFFLINE_BINS];
static float g_crn_bin_mask[CRN_OFFLINE_BINS];
static float g_crn_ola_tail[CRN_OFFLINE_HOP];
static float g_crn_win_out[CRN_OFFLINE_WIN];
static float g_crn_clean_chunk[CRN_OFFLINE_HOP];
static int16_t g_crn_prev_raw[CRN_OFFLINE_HOP];
static int16_t g_crn_cur_raw[CRN_OFFLINE_HOP];
static int16_t g_crn_zero_raw[CRN_OFFLINE_HOP];
static int8_t g_crn_offline_context[CRN_MODEL_CONTEXT][CRN_MODEL_BANDS];
static int8_t g_crn_offline_input[CRN_MODEL_INPUT_SIZE];
static int8_t g_crn_offline_output[CRN_MODEL_OUTPUT_SIZE];

/* CRN 实时 LCD 波形显示缓存。
 * 这里单独保存上一帧 RAW、OLA tail 和显示帧，避免和录音后离线 CRN 文件生成的 prev/cur raw 混用。
 * FFT、mask、win_out 这些大数组仍复用上面的 CRN 工作区；实时显示和离线处理不会并发执行。
 */
static uint8_t g_crn_rt_have_prev = 0U;
static int16_t g_crn_rt_prev_raw[CRN_OFFLINE_HOP];
static float g_crn_rt_ola_tail[CRN_OFFLINE_HOP];
static int16_t g_crn_rt_display_raw[CRN_OFFLINE_HOP];
static int16_t g_crn_rt_display_clean[CRN_OFFLINE_HOP];
static volatile uint8_t g_crn_rt_process_request = 0U;
static uint32_t g_crn_rt_skip_frames = 0U;

static void crn_offline_init_hann(void)
{
    uint32_t i;
    const float two_pi = 6.2831853071795864769f;

    if(g_crn_hann_ready != 0U) {
        return;
    }

    for(i = 0U; i < CRN_OFFLINE_WIN; i++) {
        g_crn_hann[i] = 0.5f - 0.5f * cosf((two_pi * (float)i) / (float)(CRN_OFFLINE_WIN - 1U));
    }

    g_crn_hann_ready = 1U;
}

static void crn_fft1024(float *re, float *im, uint8_t inverse)
{
    uint32_t i;
    uint32_t j;
    uint32_t bit;
    uint32_t len;
    const float two_pi = 6.2831853071795864769f;

    j = 0U;
    for(i = 1U; i < CRN_OFFLINE_N_FFT; i++) {
        bit = CRN_OFFLINE_N_FFT >> 1;
        while((j & bit) != 0U) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;

        if(i < j) {
            float tr = re[i];
            float ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }

    for(len = 2U; len <= CRN_OFFLINE_N_FFT; len <<= 1) {
        float ang = (inverse != 0U ? two_pi : -two_pi) / (float)len;
        float wlen_re = cosf(ang);
        float wlen_im = sinf(ang);
        uint32_t half = len >> 1;
        uint32_t base;

        for(base = 0U; base < CRN_OFFLINE_N_FFT; base += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            uint32_t k;

            for(k = 0U; k < half; k++) {
                uint32_t u = base + k;
                uint32_t v = u + half;
                float vr = re[v] * w_re - im[v] * w_im;
                float vi = re[v] * w_im + im[v] * w_re;
                float ur = re[u];
                float ui = im[u];

                re[u] = ur + vr;
                im[u] = ui + vi;
                re[v] = ur - vr;
                im[v] = ui - vi;

                {
                    float next_re = w_re * wlen_re - w_im * wlen_im;
                    float next_im = w_re * wlen_im + w_im * wlen_re;
                    w_re = next_re;
                    w_im = next_im;
                }
            }
        }
    }

    if(inverse != 0U) {
        for(i = 0U; i < CRN_OFFLINE_N_FFT; i++) {
            re[i] /= (float)CRN_OFFLINE_N_FFT;
            im[i] /= (float)CRN_OFFLINE_N_FFT;
        }
    }
}

static float crn_band_raw_weight(uint16_t band, uint16_t bin)
{
    uint16_t left;
    uint16_t center;
    uint16_t right;

    if(band >= CRN_MODEL_BANDS) {
        return 0.0f;
    }

    left = g_crn_band_edges[band];
    center = g_crn_band_edges[band + 1U];
    right = g_crn_band_edges[band + 2U];

    if((bin < left) || (bin > right)) {
        return 0.0f;
    }

    if(bin <= center) {
        if(center == left) {
            return 0.0f;
        }
        return ((float)(bin - left)) / ((float)(center - left));
    }

    if(right == center) {
        return 0.0f;
    }

    return ((float)(right - bin)) / ((float)(right - center));
}

static float crn_band_norm_sum(uint16_t band)
{
    uint16_t k;
    float s = 0.0f;

    for(k = g_crn_band_edges[band]; k <= g_crn_band_edges[band + 2U]; k++) {
        s += crn_band_raw_weight(band, k);
    }

    if(s <= 1.0e-12f) {
        s = 1.0f;
    }

    return s;
}

static int8_t crn_feature_to_q(float feature)
{
    int32_t q;

    q = (int32_t)(feature * CRN_INPUT_FEATURE_Q_MUL + (float)CRN_INPUT_FEATURE_Q_ZERO + 0.5f);
    if(q > 127) {
        q = 127;
    } else if(q < -128) {
        q = -128;
    }

    return (int8_t)q;
}

static float crn_output_q_to_mask(int8_t q)
{
    float m;

    m = ((float)((int32_t)q - CRN_OUTPUT_MASK_DEQ_ZERO)) * CRN_OUTPUT_MASK_DEQ_SCALE;
    if(m < 0.0f) {
        m = 0.0f;
    } else if(m > 1.0f) {
        m = 1.0f;
    }

    return m;
}

static int16_t crn_float_to_i16(float x)
{
    if(x > 32767.0f) {
        return 32767;
    }
    if(x < -32768.0f) {
        return -32768;
    }
    return (int16_t)x;
}

static void crn_context_push_and_invoke(const float mag[CRN_OFFLINE_BINS])
{
    uint16_t b;
    uint16_t k;
    uint16_t t;
    int8_t min_v;
    int8_t max_v;
    uint32_t abs_sum;

    memmove(&g_crn_offline_context[0][0],
            &g_crn_offline_context[1][0],
            (CRN_MODEL_CONTEXT - 1U) * CRN_MODEL_BANDS * sizeof(int8_t));

    for(b = 0U; b < CRN_MODEL_BANDS; b++) {
        float weighted = 0.0f;
        float norm = crn_band_norm_sum(b);
        float band_mag;
        float feature;

        for(k = g_crn_band_edges[b]; k <= g_crn_band_edges[b + 2U]; k++) {
            float w = crn_band_raw_weight(b, k) / norm;
            weighted += w * mag[k];
        }

        band_mag = weighted;
        feature = log1pf(10.0f * band_mag);
        g_crn_offline_context[CRN_MODEL_CONTEXT - 1U][b] = crn_feature_to_q(feature);
    }

    for(t = 0U; t < CRN_MODEL_CONTEXT; t++) {
        memcpy(&g_crn_offline_input[t * CRN_MODEL_BANDS],
               g_crn_offline_context[t],
               CRN_MODEL_BANDS);
    }

    {
        uint32_t t_invoke = perf_now();
        int crn_ret = crn_model_invoke_256(g_crn_offline_input, g_crn_offline_output);
        perf_add(PERF_CRN_INVOKE, perf_now() - t_invoke);

        if(crn_ret == 0) {
            g_crn_frame_cnt++;
            min_v = g_crn_offline_output[0];
            max_v = g_crn_offline_output[0];
            abs_sum = 0U;
            for(b = 0U; b < CRN_MODEL_OUTPUT_SIZE; b++) {
                int8_t v = g_crn_offline_output[b];
                if(v < min_v) {
                    min_v = v;
                }
                if(v > max_v) {
                    max_v = v;
                }
                abs_sum += (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
            }
            g_crn_mask_min = min_v;
            g_crn_mask_max = max_v;
            g_crn_mask_abs_level = (abs_sum * 20U) / CRN_MODEL_OUTPUT_SIZE;
            g_crn_last_error = 0;
        } else {
            g_crn_last_error = crn_model_last_error();
            /* 模型失败时保守直通。 */
            for(b = 0U; b < CRN_MODEL_OUTPUT_SIZE; b++) {
                g_crn_offline_output[b] = 127;
            }
        }
    }
}

static void crn_make_bin_mask_from_band_output(void)
{
    uint16_t k;
    uint16_t b;

    for(k = 0U; k < CRN_OFFLINE_BINS; k++) {
        float denom = 0.0f;
        float sum = 0.0f;

        for(b = 0U; b < CRN_MODEL_BANDS; b++) {
            float norm = crn_band_norm_sum(b);
            float fb = crn_band_raw_weight(b, k) / norm;
            denom += fb;
            sum += fb * crn_output_q_to_mask(g_crn_offline_output[b]);
        }

        if(denom > 1.0e-12f) {
            g_crn_bin_mask[k] = sum / denom;
        } else {
            g_crn_bin_mask[k] = 1.0f;
        }

        if(g_crn_bin_mask[k] < 0.02f) {
            g_crn_bin_mask[k] = 0.02f;
        } else if(g_crn_bin_mask[k] > 1.0f) {
            g_crn_bin_mask[k] = 1.0f;
        }
    }
}

static void crn_process_960_window(const int16_t first[CRN_OFFLINE_HOP],
                                   const int16_t second[CRN_OFFLINE_HOP])
{
    uint32_t i;
    uint32_t k;
    uint32_t t_stage_start;

    t_stage_start = perf_now();

    crn_offline_init_hann();

    for(i = 0U; i < CRN_OFFLINE_N_FFT; i++) {
        g_crn_fft_re[i] = 0.0f;
        g_crn_fft_im[i] = 0.0f;
    }

    for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
        g_crn_fft_re[i] = (((float)first[i]) / 32768.0f) * g_crn_hann[i];
        g_crn_fft_re[i + CRN_OFFLINE_HOP] = (((float)second[i]) / 32768.0f) * g_crn_hann[i + CRN_OFFLINE_HOP];
    }

    crn_fft1024(g_crn_fft_re, g_crn_fft_im, 0U);

    for(k = 0U; k < CRN_OFFLINE_BINS; k++) {
        float re = g_crn_fft_re[k];
        float im = g_crn_fft_im[k];
        g_crn_fft_mag[k] = sqrtf(re * re + im * im);
    }

    perf_add(PERF_STFT_BAND, perf_now() - t_stage_start);

    crn_context_push_and_invoke(g_crn_fft_mag);

    t_stage_start = perf_now();
    crn_make_bin_mask_from_band_output();

    /* Apply mask to positive and negative frequency bins. */
    for(k = 0U; k < CRN_OFFLINE_BINS; k++) {
        g_crn_fft_re[k] *= g_crn_bin_mask[k];
        g_crn_fft_im[k] *= g_crn_bin_mask[k];
    }
    for(k = 1U; k < (CRN_OFFLINE_BINS - 1U); k++) {
        uint32_t mirror = CRN_OFFLINE_N_FFT - k;
        g_crn_fft_re[mirror] *= g_crn_bin_mask[k];
        g_crn_fft_im[mirror] *= g_crn_bin_mask[k];
    }

    crn_fft1024(g_crn_fft_re, g_crn_fft_im, 1U);

    for(i = 0U; i < CRN_OFFLINE_WIN; i++) {
        g_crn_win_out[i] = g_crn_fft_re[i] * g_crn_hann[i] * 32768.0f * CRN_OLA_GAIN;
    }

    perf_add(PERF_MASK_ISTFT_OLA, perf_now() - t_stage_start);
}

static void crn_realtime_reset(void)
{
    memset(g_crn_rt_prev_raw, 0, sizeof(g_crn_rt_prev_raw));
    memset(g_crn_rt_ola_tail, 0, sizeof(g_crn_rt_ola_tail));
    memset(g_crn_rt_display_raw, 0, sizeof(g_crn_rt_display_raw));
    memset(g_crn_rt_display_clean, 0, sizeof(g_crn_rt_display_clean));
    memset(g_crn_offline_context, 0, sizeof(g_crn_offline_context));
    memset(g_crn_offline_input, 0, sizeof(g_crn_offline_input));
    memset(g_crn_offline_output, 0, sizeof(g_crn_offline_output));
    g_crn_mask_abs_level = 0U;
    g_crn_mask_min = 0;
    g_crn_mask_max = 0;
    g_crn_preview_div_cnt = 0U;
    g_crn_rt_have_prev = 0U;
    g_crn_rt_process_request = 0U;
    g_crn_rt_skip_frames = 0U;
}

static void crn_realtime_process_frame(const int16_t raw[CRN_OFFLINE_HOP],
                                       int16_t raw_for_display[CRN_OFFLINE_HOP],
                                       int16_t clean_for_display[CRN_OFFLINE_HOP])
{
    uint32_t i;

    if((raw == NULL) || (raw_for_display == NULL) || (clean_for_display == NULL)) {
        return;
    }

    if(g_crn_ready == 0U) {
        memcpy(raw_for_display, raw, sizeof(g_crn_rt_display_raw));
        memcpy(clean_for_display, raw, sizeof(g_crn_rt_display_clean));
        return;
    }

    /* 第一帧只能得到 zero+raw 的后半段 OLA tail，先显示 RAW，下一帧开始显示真正 CRN clean。 */
    if(g_crn_rt_have_prev == 0U) {
        memset(g_crn_zero_raw, 0, sizeof(g_crn_zero_raw));
        crn_process_960_window(g_crn_zero_raw, raw);

        for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
            g_crn_rt_ola_tail[i] = g_crn_win_out[i + CRN_OFFLINE_HOP];
            raw_for_display[i] = raw[i];
            clean_for_display[i] = raw[i];
        }

        memcpy(g_crn_rt_prev_raw, raw, sizeof(g_crn_rt_prev_raw));
        g_crn_rt_have_prev = 1U;
        return;
    }

    /* 当前 480 点和上一帧组成 960 点窗口，输出的是上一帧对齐的 CRN clean 波形。
     * 因此 LCD 上 RAW/CRN OUT 互相对齐，但整体比实时输入延迟约 10ms。
     */
    memcpy(raw_for_display, g_crn_rt_prev_raw, sizeof(g_crn_rt_display_raw));
    crn_process_960_window(g_crn_rt_prev_raw, raw);

    for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
        float clean = g_crn_rt_ola_tail[i] + g_crn_win_out[i];
        clean_for_display[i] = crn_float_to_i16(clean);
        g_crn_rt_ola_tail[i] = g_crn_win_out[i + CRN_OFFLINE_HOP];
    }

    memcpy(g_crn_rt_prev_raw, raw, sizeof(g_crn_rt_prev_raw));
    memcpy(g_crn_output_q, g_crn_offline_output, sizeof(g_crn_output_q));
}

static int offline_make_crn_file_from_input(const char *input_name,
                                            char *display,
                                            uint32_t display_len,
                                            char *path,
                                            uint32_t path_len)
{
    uint32_t num = 0U;
    uint32_t i;

    if((input_name == NULL) || (display == NULL) || (path == NULL)) {
        return -403;
    }

    if(strncmp(input_name, "CMP", 3U) != 0) {
        return -404;
    }

    for(i = 3U; i < 7U; i++) {
        if((input_name[i] < '0') || (input_name[i] > '9')) {
            return -405;
        }
        num = num * 10U + (uint32_t)(input_name[i] - '0');
    }

    if((input_name[7] != '.') || (input_name[8] != 'W') ||
       (input_name[9] != 'A') || (input_name[10] != 'V')) {
        return -406;
    }

    snprintf(display, display_len, "CRN%04lu.WAV", (unsigned long)num);
    snprintf(path, path_len, "0:/CRN%04lu.WAV", (unsigned long)num);
    return 0;
}

static int crn_read_raw_chunk(FIL *fin, int16_t dst[CRN_OFFLINE_HOP])
{
    FRESULT res;
    UINT br;
    uint32_t i;

    res = f_read(fin, g_offline_in_bytes, CRN_OFFLINE_HOP * 4U, &br);
    if((res != FR_OK) || (br != (CRN_OFFLINE_HOP * 4U))) {
        return -1;
    }

    for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
        dst[i] = offline_rd16s(&g_offline_in_bytes[i * 4U]);
    }

    return 0;
}

static int crn_write_raw_clean_chunk(FIL *fout,
                                     const int16_t raw[CRN_OFFLINE_HOP],
                                     const float clean_f[CRN_OFFLINE_HOP],
                                     uint32_t *written_pcm)
{
    FRESULT res;
    UINT bw;
    uint32_t i;

    for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
        int16_t clean = crn_float_to_i16(clean_f[i]);

        g_offline_out_bytes[i * 4U + 0U] = (uint8_t)((uint16_t)raw[i] & 0xFFU);
        g_offline_out_bytes[i * 4U + 1U] = (uint8_t)(((uint16_t)raw[i] >> 8) & 0xFFU);
        g_offline_out_bytes[i * 4U + 2U] = (uint8_t)((uint16_t)clean & 0xFFU);
        g_offline_out_bytes[i * 4U + 3U] = (uint8_t)(((uint16_t)clean >> 8) & 0xFFU);
    }

    res = f_write(fout, g_offline_out_bytes, CRN_OFFLINE_HOP * 4U, &bw);
    if((res != FR_OK) || (bw != (CRN_OFFLINE_HOP * 4U))) {
        return -1;
    }

    *written_pcm += CRN_OFFLINE_HOP * 4U;
    return 0;
}

static int crn_offline_process_last_file(void)
{
#if ENABLE_SD_WAV_RECORD
    FIL fin;
    FIL fout;
    FRESULT res;
    UINT br;
    UINT bw;
    uint8_t header[OFFLINE_WAV_HEADER_SIZE];
    uint8_t out_header[OFFLINE_WAV_HEADER_SIZE];
    char in_path[32];
    char out_path[32];
    char out_name[16];
    uint32_t data_bytes;
    uint32_t total_frames;
    uint32_t frame_idx;
    uint32_t i;
    uint32_t written_pcm = 0U;
    int err;

    if(g_crn_ready == 0U) {
        g_offline_rnn_last_error = -400;
        strncpy(g_offline_rnn_filename, "NO CRN", sizeof(g_offline_rnn_filename) - 1U);
        g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
        return -1;
    }

    if(wavrec_filename()[0] == 'N') {
        g_offline_rnn_last_error = -401;
        return -1;
    }

    snprintf(in_path, sizeof(in_path), "0:/%s", wavrec_filename());
    err = offline_make_crn_file_from_input(wavrec_filename(), out_name, sizeof(out_name), out_path, sizeof(out_path));
    if(err != 0) {
        g_offline_rnn_last_error = err;
        return -1;
    }

    g_offline_rnn_busy = 1U;
    g_offline_rnn_percent = 0U;
    g_offline_rnn_last_error = 0;
    strncpy(g_offline_rnn_input_filename, wavrec_filename(), sizeof(g_offline_rnn_input_filename) - 1U);
    g_offline_rnn_input_filename[sizeof(g_offline_rnn_input_filename) - 1U] = '\0';
    strncpy(g_offline_rnn_filename, out_name, sizeof(g_offline_rnn_filename) - 1U);
    g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
    rnnoise_lcd_panel_update();

    printf("OFFLINE CRN: %s -> %s\r\n", in_path, out_path);

    res = f_open(&fin, in_path, FA_READ);
    if(res != FR_OK) {
        g_offline_rnn_last_error = -410 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    res = f_read(&fin, header, sizeof(header), &br);
    if((res != FR_OK) || (br != sizeof(header))) {
        f_close(&fin);
        g_offline_rnn_last_error = -420 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    data_bytes = offline_rd32(&header[40]);
    total_frames = data_bytes / (CRN_OFFLINE_HOP * 4U);
    if(total_frames == 0U) {
        f_close(&fin);
        g_offline_rnn_last_error = -402;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    res = f_open(&fout, out_path, FA_CREATE_ALWAYS | FA_WRITE);
    if(res != FR_OK) {
        f_close(&fin);
        g_offline_rnn_last_error = -430 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    offline_make_wav_header(out_header, 0U);
    res = f_write(&fout, out_header, sizeof(out_header), &bw);
    if((res != FR_OK) || (bw != sizeof(out_header))) {
        f_close(&fin);
        f_close(&fout);
        g_offline_rnn_last_error = -440 - (int)res;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    memset(g_crn_zero_raw, 0, sizeof(g_crn_zero_raw));
    memset(g_crn_ola_tail, 0, sizeof(g_crn_ola_tail));
    memset(g_crn_offline_context, 0, sizeof(g_crn_offline_context));
    memset(g_crn_offline_input, 0, sizeof(g_crn_offline_input));
    memset(g_crn_offline_output, 0, sizeof(g_crn_offline_output));

    if(crn_read_raw_chunk(&fin, g_crn_prev_raw) != 0) {
        f_close(&fin);
        f_close(&fout);
        g_offline_rnn_last_error = -450;
        g_offline_rnn_busy = 0U;
        return -1;
    }

    /* 第一帧：zero + first，只产生 OLA tail，不立刻写文件。 */
    crn_process_960_window(g_crn_zero_raw, g_crn_prev_raw);
    for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
        g_crn_ola_tail[i] = g_crn_win_out[i + CRN_OFFLINE_HOP];
    }

    for(frame_idx = 1U; frame_idx < total_frames; frame_idx++) {
        if(crn_read_raw_chunk(&fin, g_crn_cur_raw) != 0) {
            g_offline_rnn_last_error = -451;
            break;
        }

        crn_process_960_window(g_crn_prev_raw, g_crn_cur_raw);

        for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
            g_crn_clean_chunk[i] = g_crn_ola_tail[i] + g_crn_win_out[i];
            g_crn_ola_tail[i] = g_crn_win_out[i + CRN_OFFLINE_HOP];
        }

        if(crn_write_raw_clean_chunk(&fout, g_crn_prev_raw, g_crn_clean_chunk, &written_pcm) != 0) {
            g_offline_rnn_last_error = -460;
            break;
        }

        memcpy(g_crn_prev_raw, g_crn_cur_raw, sizeof(g_crn_prev_raw));

        if((frame_idx % 10U) == 0U) {
            g_offline_rnn_percent = (uint8_t)((frame_idx * 100U) / total_frames);
            rnnoise_lcd_panel_update();
        }
    }

    if(g_offline_rnn_last_error == 0) {
        crn_process_960_window(g_crn_prev_raw, g_crn_zero_raw);
        for(i = 0U; i < CRN_OFFLINE_HOP; i++) {
            g_crn_clean_chunk[i] = g_crn_ola_tail[i] + g_crn_win_out[i];
        }

        if(crn_write_raw_clean_chunk(&fout, g_crn_prev_raw, g_crn_clean_chunk, &written_pcm) != 0) {
            g_offline_rnn_last_error = -461;
        }
    }

    if(g_offline_rnn_last_error == 0) {
        offline_make_wav_header(out_header, written_pcm);
        res = f_lseek(&fout, 0U);
        if(res == FR_OK) {
            res = f_write(&fout, out_header, sizeof(out_header), &bw);
        }
        if((res != FR_OK) || (bw != sizeof(out_header))) {
            g_offline_rnn_last_error = -470 - (int)res;
        }
    }

    f_sync(&fout);
    f_close(&fout);
    f_close(&fin);

    g_offline_rnn_busy = 0U;

    if(g_offline_rnn_last_error == 0) {
        g_offline_rnn_percent = 100U;
        strncpy(g_offline_rnn_filename, out_name, sizeof(g_offline_rnn_filename) - 1U);
        g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
        printf("OFFLINE CRN saved: %s\r\n", out_name);
        rnnoise_lcd_panel_update();
        return 0;
    }

    printf("OFFLINE CRN failed err=%d\r\n", g_offline_rnn_last_error);
    rnnoise_lcd_panel_update();
    return -1;
#else
    return -1;
#endif
}


static void sdram_audio_queue_clear(void)
{
    volatile uint32_t *p = (volatile uint32_t *)AUDIO_RX_QUEUE_BASE_ADDR;
    uint32_t words = AUDIO_RX_QUEUE_TOTAL_BYTES / sizeof(uint32_t);
    uint32_t i;

    for(i = 0U; i < words; i++) {
        p[i] = 0U;
    }

    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)AUDIO_RX_QUEUE_BASE_ADDR,
                                      (int32_t)AUDIO_RX_QUEUE_TOTAL_BYTES);
    __DSB();
    __ISB();
}

static uint8_t sdram_audio_queue_test(void)
{
    volatile uint32_t *p = (volatile uint32_t *)AUDIO_RX_QUEUE_BASE_ADDR;
    uint32_t words = AUDIO_RX_QUEUE_TOTAL_BYTES / sizeof(uint32_t);
    uint32_t i;
    uint32_t v;

    for(i = 0U; i < words; i++) {
        p[i] = 0x55555555U ^ i;
    }
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)AUDIO_RX_QUEUE_BASE_ADDR,
                                      (int32_t)AUDIO_RX_QUEUE_TOTAL_BYTES);
    __DSB();
    __ISB();
    for(i = 0U; i < words; i++) {
        v = 0x55555555U ^ i;
        if(p[i] != v) {
            return 0U;
        }
    }

    for(i = 0U; i < words; i++) {
        p[i] = 0xAAAAAAAAU ^ (i << 1);
    }
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)AUDIO_RX_QUEUE_BASE_ADDR,
                                      (int32_t)AUDIO_RX_QUEUE_TOTAL_BYTES);
    __DSB();
    __ISB();
    for(i = 0U; i < words; i++) {
        v = 0xAAAAAAAAU ^ (i << 1);
        if(p[i] != v) {
            return 0U;
        }
    }

    sdram_audio_queue_clear();
    return 1U;
}

int main(void)
{
    uint16_t len;
    uint32_t i;
    uint32_t audio_cnt = 0;
	
	  mpu_memory_protection();

    /* 1. 初始化缓存, 时钟, 延时, 串口, LED */
    sys_cache_enable();
    delay_init(600);
    nvic_priority_group_set(NVIC_PRIGROUP_PRE1_SUB3);
    __enable_irq();
    usart_init(115200);
    perf_init();
    led_init();

    /* 2. 初始化 LCD */
    lcd_port_init();
    LT768_Init();
    lcd_init();
    lcd_display_on();
    lcd_clear(BLACK);

    touch_ui_init();

    uart_lcd_show("SAI TDM audio + LCD + TOUCH start");
    uart_lcd_show("UART cmd disabled, printf kept safe");
    uart_lcd_show("Step 1: clear output buffer");

    /* 3. 清空输出缓冲区数据, 确保初始状态全是静音 */
    for(i = 0; i < 2 * SAMPLE_AUDIO_SIZE; i++) {
        output_buffer[0][i] = 0x00000000;
        output_buffer[1][i] = 0x00000000;
    }

    /* 刷新 DCache, 确保 DMA 能读取到清空后的缓冲区数据 */
    SCB_CleanDCache_by_Addr((uint32_t *)output_buffer[0], 2 * SAMPLE_AUDIO_SIZE * sizeof(uint32_t));
    SCB_CleanDCache_by_Addr((uint32_t *)output_buffer[1], 2 * SAMPLE_AUDIO_SIZE * sizeof(uint32_t));

    output_buffer_ready[0] = 1;
    output_buffer_ready[1] = 1;
    dma_playing_buffer = 0;
    current_output_buffer = 1;

    uart_lcd_show("Step 1.5: RNNoise init");

    /* 初始化 RNNoise，使用 rnn_data.c 中的内置模型 */
    rnnoise_port_init();

    uart_lcd_show("Step 1.6: CRN model init");
    if(crn_model_port_init() == 0) {
        g_crn_ready = 1U;
        g_crn_last_error = 0;
        crn_probe_reset();
        uart_lcd_show("CRN model init ok");
    } else {
        g_crn_ready = 0U;
        g_crn_last_error = crn_model_last_error();
        uart_lcd_show("CRN model init failed err=%d", g_crn_last_error);
    }
    denoise_mode_apply();
    crn_realtime_reset();

    uart_lcd_show("Step 2: enable RX DMA IRQ only");

    /* 4. 只启用采集 DMA 中断。当前没有扬声器，不启用 DMA0 CH0 播放中断。 */
    nvic_irq_enable(DMA0_Channel1_IRQn, 0, 1);
    NVIC_ClearPendingIRQ(DMA0_Channel1_IRQn);
    NVIC_EnableIRQ(DMA0_Channel1_IRQn);

    uart_lcd_show("Step 3: rcu_config");

    /* 5. 配置系统时钟外设 */
    rcu_config();

#if ENABLE_SD_WAV_RECORD
    /*
     * SD 安全版：开机阶段不初始化 SD。
     * 原因：SDIO 和 SAI 都可能使用 PLL1，如果开机初始化 SD 时重新配置 PLL1，
     * 容易造成音频采集和 LCD 波形异常。
     *
     * 现在改成点击 REC 时再初始化 SD；没有 SD 卡时，音频和波形仍然正常运行。
     */
    uart_lcd_show("Step 3.5: SD lazy init on REC");
#endif

    uart_lcd_show("Step 4: no speaker, ES8311 disabled");

    /* 当前没有扬声器，不初始化 ES8311，不使能音频功放。 */
    uart_lcd_show("Step 5: ES8311 skipped");
    uart_lcd_show("Step 6: Soft I2C PK0/PK1 ready for ES7210");

    /*
     * 当前无扬声器，只做 ES7210 采集。
     * 重要：先配置并启动 SAI0 Block0，让 PG7/PE5/PE4 输出 MCLK/BCLK/FS，
     * 再初始化 ES7210。这样 ES7210 配置 ADC/TDM 时已经能看到音频时钟。
     */
    i2c_deinit(I2C1);
    i2c_config();

    uart_lcd_show("Step 7: SAI config start");

    /* 初始化 SAI。MCLK 使用 PG7 输出，不再使用 PG9。 */
    sai_config();

    uart_lcd_show("Step 8: SAI config ok");
    uart_lcd_show("Step 9: ADC DMA config");

    adc_dma_config();

    uart_lcd_show("Step 10: DAC DMA skipped");
    uart_lcd_show("Step 11: clear RX DMA flags");

    /* 当前只使用 DMA0 CH1 采集，不使用 DMA0 CH0 播放。 */
    dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_FTF);
    dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_HTF);
    dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_TAE);
    dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_SDE);
    dma_flag_clear(DMA0, DMA_CH1, DMA_FLAG_FEE);

    uart_lcd_show("Step 11.5: SDRAM init");
    sdram_init(EXMC_SDRAM_DEVICE0);
    if(sdram_audio_queue_test() == 0U) {
        uart_lcd_show("ERROR: SDRAM queue test failed");
        printf("ERROR: SDRAM audio queue test failed\r\n");
        while(1) {
            LED0_TOGGLE();
            delay_ms(200);
        }
    }
    uart_lcd_show("SDRAM OK: RX queue %lu KB", (unsigned long)(AUDIO_RX_QUEUE_TOTAL_BYTES / 1024U));

    audio_rx_queue_reset();

    uart_lcd_show("Step 12: enable RX DMA channel");

    dma_channel_enable(DMA0, DMA_CH1);

    uart_lcd_show("Step 13: enable SAI RX DMA request");

    sai_dma_enable(SAI0, SAI_BLOCK0);

    uart_lcd_show("Step 14: enable SAI RX block");

    sai_enable(SAI0, SAI_BLOCK0);

    delay_ms(20);
#if APP_PIN_WAVE_DEBUG_ENABLE
    sai_pin_wave_debug("before ES7210");
#endif

    uart_lcd_show("Step 15: ES7210 init after SAI clock");

    /*
     * codec_safe_timeout_ES7210.c 中的 es7210_init() 已经带 timeout，
     * 即使 I2C 异常也不会永久卡死。
     */
    es7210_init();

    delay_ms(50);
#if APP_PIN_WAVE_DEBUG_ENABLE
    sai_pin_wave_debug("after ES7210");
#endif

    uart_lcd_show("Step 16: ES7210 init finished");

    delay_ms(20);

    uart_lcd_show("Step 17: audio run");

    /*
     * 启动信息显示完毕后，进入正式仪表盘页面。
     * 先整屏清黑一次，否则启动阶段 Step 文本会残留在波形区背后。
     */
    ui_dashboard_request_full_clear();
    rnnoise_lcd_panel_update();

    while(1) {
        static uint32_t print_cnt = 0;

        /*
         * DMA RX 队列消费。
         * 旧版只有 buffer_ready 一个标志，SD 写入/LCD 刷新稍微阻塞就会漏掉多个 10ms 块。
         * 这里从软件队列按顺序取帧处理，避免硬件双缓冲被覆盖造成录音变短。
         */
        {
            uint8_t processed_frames = 0U;
            uint8_t frame_budget;
            uint16_t qidx;

#if ENABLE_SD_WAV_RECORD
            frame_budget = wavrec_is_recording() ? 8U : 4U;
#else
            frame_budget = 4U;
#endif

#if ENABLE_SD_WAV_RECORD
            if((wavrec_is_recording() == 0U) && (g_denoise_mode == DENOISE_MODE_CRN))
#else
            if(g_denoise_mode == DENOISE_MODE_CRN)
#endif
            {
                /* CRN 实时预览以 UI 响应为第一优先级：只处理最新帧，每轮最多处理 1 帧。 */
                frame_budget = 1U;
                audio_rx_queue_trim_to(CRN_PREVIEW_QUEUE_KEEP);
                touch_ui_service(1U);
            }

            while((processed_frames < frame_budget) && (audio_rx_queue_peek(&qidx) != 0U)) {
#if CRN_REALTIME_WAVE_ENABLE
                g_crn_rt_process_request = 0U;
#if ENABLE_SD_WAV_RECORD
                if((wavrec_is_recording() == 0U) && (g_denoise_mode == DENOISE_MODE_CRN))
#else
                if(g_denoise_mode == DENOISE_MODE_CRN)
#endif
                {
                    /*
                     * 只有在安全预览节拍或即将刷新 LCD 的那一帧才跑完整 CRN clean 重建。
                     * LCD 本来约 200ms 刷一次，没有必要每 10ms 都做 FFT+iFFT+模型。
                     */
                    if((g_crn_rt_skip_frames == 0U) ||
                       (((audio_cnt + 1U) % LCD_WAVE_REFRESH_BUFFERS_IDLE) == 0U)) {
                        g_crn_rt_process_request = 1U;
                        g_crn_rt_skip_frames = (CRN_REALTIME_WAVE_PROCESS_DIV > 0U) ?
                                                (CRN_REALTIME_WAVE_PROCESS_DIV - 1U) : 0U;
                    } else {
                        g_crn_rt_skip_frames--;
                    }
                }
#endif
                process_audio_frame(audio_rx_queue[qidx]);
                g_crn_rt_process_request = 0U;
                audio_rx_queue_consume();
                processed_frames++;

                audio_cnt++;

                if((audio_cnt % 100U) == 0U) {
                    LED0_TOGGLE();
                }

                if((audio_cnt % 3000U) == 0U) {
                    perf_print();
                }

                if(g_denoise_mode == DENOISE_MODE_CRN) {
                    touch_ui_service(1U);
                }

                /*
                 * 每隔几个输入 buffer 刷新一次 RAW/CLEAN 双波形面板。
                 * LCD 刷新比较耗时，频率不要过高。
                 */
                {
                    uint32_t refresh_div;

#if ENABLE_SD_WAV_RECORD
                    refresh_div = wavrec_is_recording() ? LCD_WAVE_REFRESH_BUFFERS_REC : LCD_WAVE_REFRESH_BUFFERS_IDLE;
#else
                    refresh_div = LCD_WAVE_REFRESH_BUFFERS_IDLE;
#endif

                    if((refresh_div != 0U) && ((audio_cnt % refresh_div) == 0U)) {
                        uint32_t t_lcd = perf_now();
                        rnnoise_lcd_panel_update();
                        perf_add(PERF_LCD_REFRESH, perf_now() - t_lcd);
                    }
                }
            }
        }

        /*
         * 触摸扫描不要放进 DMA 中断，也不要依赖 audio_cnt。
         * 旧写法绑定 audio_cnt：如果音频 DMA 没跑起来，触摸就完全不会扫描。
         * 这里改成主循环分频扫描：音频正常时可以用，音频异常时也能单独调试触摸。
         */
#if ENABLE_TOUCH_UI
#if ENABLE_SD_WAV_RECORD
        touch_ui_service((wavrec_is_recording() != 0U) || (g_denoise_mode == DENOISE_MODE_CRN));
#else
        touch_ui_service(g_denoise_mode == DENOISE_MODE_CRN);
#endif
#endif

        /* 打印运行状态，只走串口，不再调用 uart_lcd_show，避免刷屏覆盖监控面板。 */
        print_cnt++;

        if((APP_RUNTIME_SERIAL_STATUS_ENABLE != 0U) && (print_cnt >= 5000000U)) {
            print_cnt = 0;

            printf("audio=%u, mode=%s, rn=%u, crn=%lu, vad=%u%%, raw=%u, clean=%u, tx=%u, rx=%u\r\n",
                   (unsigned int)audio_cnt,
                   denoise_mode_text(),
                   (unsigned int)g_rnnoise_frame_cnt,
                   (unsigned long)g_crn_frame_cnt,
                   (unsigned int)g_vad_percent,
                   (unsigned int)g_raw_abs_level,
                   (unsigned int)g_out_abs_level,
                   (unsigned int)dma_tx_irq_cnt,
                   (unsigned int)dma_rx_irq_cnt);
        }

        /* 串口接收处理：PC 发送一帧数据后，串口和 LCD 同步显示 RX 内容 */
#if APP_UART_CONTROL_ENABLE
        if(g_usart_rx_sta & 0x8000) {
            char rx_lcd_buf[64];

            len = g_usart_rx_sta & 0x3fff;
            make_lcd_safe_string(rx_lcd_buf, sizeof(rx_lcd_buf), g_usart_rx_buf, len);

            uart_lcd_show("RX: %s", rx_lcd_buf);

            /* 串口 m/M 切换 RAW/RNN/CRN 模式。 */
            if((len > 0U) && ((g_usart_rx_buf[0] == 'm') || (g_usart_rx_buf[0] == 'M'))) {
                denoise_mode_next();
                printf("UART: MODE %s\r\n", denoise_mode_text());
                rnnoise_lcd_panel_update();
            }

            /* 串口兜底控制：r/R 切换录音，s/S 保存 LCD 截图。 */
#if ENABLE_SD_WAV_RECORD
            if((len > 0U) && ((g_usart_rx_buf[0] == 'r') || (g_usart_rx_buf[0] == 'R'))) {
                if(wavrec_is_recording() != 0U) {
                    audio_rx_queue_pause();
                    audio_rx_queue_drain_limited(STOP_DRAIN_MAX_FRAMES);
                    if(wavrec_stop() == 0) {
                        printf("UART: REC STOP saved %s\r\n", wavrec_filename());
                        if(g_denoise_mode == DENOISE_MODE_RNN) {
                            (void)rnnoise_offline_process_last_file();
                        } else if(g_denoise_mode == DENOISE_MODE_CRN) {
                            (void)crn_offline_process_last_file();
                        } else {
                            strncpy(g_offline_rnn_filename, "RAW", sizeof(g_offline_rnn_filename) - 1U);
                            g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
                        }
                        crn_realtime_reset();
                        audio_rx_queue_reset();
                    } else {
                        printf("UART: REC STOP failed err=%d\r\n", wavrec_last_error());
                        audio_rx_queue_reset();
                    }
                } else {
                    if(wavrec_start() == 0) {
                        audio_rx_queue_reset();
                        g_offline_rnn_last_error = 0;
                        g_offline_rnn_percent = 0U;
                        strncpy(g_offline_rnn_input_filename, wavrec_filename(), sizeof(g_offline_rnn_input_filename) - 1U);
                        g_offline_rnn_input_filename[sizeof(g_offline_rnn_input_filename) - 1U] = '\0';
                        strncpy(g_offline_rnn_filename, (g_denoise_mode == DENOISE_MODE_RAW) ? "RAW" : "PENDING", sizeof(g_offline_rnn_filename) - 1U);
                        g_offline_rnn_filename[sizeof(g_offline_rnn_filename) - 1U] = '\0';
                        rnnoise_stream_reset();
                        crn_realtime_reset();
                        printf("UART: REC START RAW FAST %s\r\n", wavrec_filename());
                    } else {
                        printf("UART: REC START failed err=%d\r\n", wavrec_last_error());
                    }
                }
                rnnoise_lcd_panel_update();
            }
#endif

#if ENABLE_SD_SCREENSHOT
            if((len > 0U) && ((g_usart_rx_buf[0] == 's') || (g_usart_rx_buf[0] == 'S'))) {
                if(wavrec_is_recording() != 0U) {
                    printf("UART: SHOT refused while recording\r\n");
                } else {
                    audio_rx_queue_pause();
                    audio_rx_queue_drain_all();
                    rnnoise_lcd_panel_update();
                    if(sdshot_save_lcd_bmp() == 0) {
                        printf("UART: SHOT saved %s\r\n", sdshot_filename());
                    } else {
                        printf("UART: SHOT failed err=%d\r\n", sdshot_last_error());
                    }
                    audio_rx_queue_reset();
                    rnnoise_lcd_panel_update();
                }
            }
#endif

            g_usart_rx_sta = 0;
        }
#endif /* APP_UART_CONTROL_ENABLE */
    }
}

/*!
    \brief      配置系统时钟外设
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void rcu_config(void)
{
    /* 开启 GPIO 时钟 */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOG);
    rcu_periph_clock_enable(RCU_GPIOF);    /* LCD2 touch PF6/PF7/PF8 */
    rcu_periph_clock_enable(RCU_GPIOK);    /* PK0/PK1 软件 I2C */

    /* 开启 I2C 时钟 */
    rcu_periph_clock_enable(RCU_I2C1);

    /* 开启 DMA 和 DMAMUX 时钟 */
    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_DMA1);
    rcu_periph_clock_enable(RCU_DMAMUX);
}

/*!
    \brief      配置音频输出 DMA
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void dac_dma_config(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    dma_deinit(DMA0, DMA_CH0);
    dma_single_data_para_struct_init(&dma_init_struct);

    /* 配置 SAI0 Block1 使用 DMA, DMA0 CH0 作为音频播放通道 */
    dma_init_struct.request             = DMA_REQUEST_SAI0_B1;
    dma_init_struct.periph_addr         = (uint32_t)(&SAI_DATA(SAI0, SAI_BLOCK1));
    dma_init_struct.memory0_addr        = (uint32_t)output_buffer[0];
    dma_init_struct.direction           = DMA_MEMORY_TO_PERIPH;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_32BIT;
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH;
    dma_init_struct.number              = 2 * SAMPLE_AUDIO_SIZE;
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;

    dma_single_data_mode_init(DMA0, DMA_CH0, &dma_init_struct);

    /* 配置双缓冲区模式, 设置第二个缓冲区地址 */
    dma_memory_address_config(DMA0, DMA_CH0, DMA_MEMORY_1, (uint32_t)output_buffer[1]);
    dma_switch_buffer_mode_enable(DMA0, DMA_CH0);

    /* 开启 DMA 传输完成中断 */
    dma_interrupt_enable(DMA0, DMA_CH0, DMA_INT_FTF);
}

/*!
    \brief      配置音频输入 DMA
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void adc_dma_config(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    dma_deinit(DMA0, DMA_CH1);
    dma_single_data_para_struct_init(&dma_init_struct);

    /* 配置 SAI0 Block0 使用 DMA, DMA0 CH1 作为音频录音通道 */
    dma_init_struct.request             = DMA_REQUEST_SAI0_B0;
    dma_init_struct.periph_addr         = (uint32_t)(&SAI_DATA(SAI0, SAI_BLOCK0));
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory0_addr        = (uint32_t)tdm_buffer[0];
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_32BIT;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.number              = 2 * SAMPLE_AUDIO_SIZE;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;

    dma_single_data_mode_init(DMA0, DMA_CH1, &dma_init_struct);

    /* 配置双缓冲区模式, 设置第二个缓冲区地址 */
    dma_memory_address_config(DMA0, DMA_CH1, DMA_MEMORY_1, (uint32_t)tdm_buffer[1]);
    dma_switch_buffer_mode_enable(DMA0, DMA_CH1);

    /* 开启 DMA 传输完成中断 */
    dma_interrupt_enable(DMA0, DMA_CH1, DMA_INT_FTF);
}

/*!
    \brief      初始化 SAI 接口
    \param[in]  无
    \param[out] 无
    \retval     无
*/
void sai_config(void)
{
    sai_parameter_struct sai_struct;
    sai_frame_parameter_struct sai_frame_struct;
    sai_slot_parameter_struct sai_slot_struct;

    /* 当前版本只做 ES7210 采集：SAI0 Block0 RX。
     * 不初始化 SAI0 Block1 TX，不配置 PE3 为输出，避免没有扬声器时干扰音频板数据线。
     */
    rcu_periph_clock_enable(RCU_GPIOE);
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_GPIOG);
    rcu_periph_clock_enable(RCU_GPIOF);    /* LCD2 touch PF6/PF7/PF8 */
    rcu_periph_clock_enable(RCU_GPIOK);    /* PK0/PK1 软件 I2C */
    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_DMAMUX);

    /* 配置 SAI 时钟：48 kHz 音频需要 MCLK≈12.288 MHz (= 48 kHz * 256)。
     * HXTAL=25 MHz, PLL1 input=25/25=1 MHz, VCO=344 MHz, PLL1P=344/28≈12.2857 MHz。
     * 误差约 -0.0186%，比原 16 kHz 版 4.100 MHz 接近 4.096 MHz 的误差更小。
     */
    rcu_pll_input_output_clock_range_config(IDX_PLL1, RCU_PLL1RNG_1M_2M, RCU_PLL1VCO_192M_836M);
    rcu_pll1_config(25, 344, 28, 24, 24);
    rcu_sai_clock_config(IDX_SAI0, RCU_SAISRC_PLL1P);
    rcu_pll_clock_output_enable(RCU_PLL1P);
    /* SDIO 已在 BSP/SDIO/sdio_sdcard.c 中改用 PLL0Q，这里只需要打开 SAI 用的 PLL1P。 */
    rcu_osci_on(RCU_PLL1_CK);

    if(ERROR == rcu_osci_stab_wait(RCU_PLL1_CK)) {
        printf("ERROR: PLL1 clock failed\r\n");
        while(1) {
        }
    }

    rcu_periph_clock_enable(RCU_SAI0);

    /* PG7 -> SAI0_MCLK0。PG9 不能输出 SAI0_MCLK0。 */
    gpio_af_set(GPIOG, GPIO_AF_6, GPIO_PIN_7);
    gpio_mode_set(GPIOG, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_7);
    gpio_output_options_set(GPIOG, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_7);

    /* PE4 -> SAI0_FS0, PE5 -> SAI0_SCK0 */
    gpio_af_set(GPIOE, GPIO_AF_6, GPIO_PIN_4 | GPIO_PIN_5);
    gpio_mode_set(GPIOE, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_4 | GPIO_PIN_5);
    gpio_output_options_set(GPIOE, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_4 | GPIO_PIN_5);

    /* PD6 -> SAI0_SD0，作为 ES7210 数据输入。 */
    gpio_af_set(GPIOD, GPIO_AF_6, GPIO_PIN_6);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_6);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_85MHZ, GPIO_PIN_6);

    sai_deinit(SAI0);

    sai_struct_para_init(&sai_struct);
    sai_frame_struct_para_init(&sai_frame_struct);
    sai_slot_struct_para_init(&sai_slot_struct);

    /* 先保持你原来的 2 x 32-bit slot 配置，便于和当前 tdm_buffer[2][2*SAMPLE_AUDIO_SIZE] 匹配。 */
    sai_frame_struct.frame_width          = 64;
    sai_frame_struct.frame_sync_width     = 32;
    sai_frame_struct.frame_sync_function  = SAI_FS_FUNC_START_CHANNEL;
    sai_frame_struct.frame_sync_polarity  = SAI_FS_POLARITY_HIGH;
    sai_frame_struct.frame_sync_offset    = SAI_FS_OFFSET_BEGINNING;
    sai_frame_init(SAI0, SAI_BLOCK0, &sai_frame_struct);

    sai_slot_struct.slot_number           = 2;
    sai_slot_struct.slot_width            = SAI_SLOT_WIDTH_32BIT;
    sai_slot_struct.data_offset           = 0;
    sai_slot_struct.slot_active           = SAI_SLOT_ACTIVE_ALL;
    sai_slot_init(SAI0, SAI_BLOCK0, &sai_slot_struct);

    sai_struct.operating_mode             = SAI_MASTER_RECEIVER;
    sai_struct.protocol                   = SAI_PROTOCOL_POLYMORPHIC;
    sai_struct.data_width                 = SAI_DATAWIDTH_32BIT;
    sai_struct.shift_dir                  = SAI_SHIFT_MSB;
    sai_struct.sample_edge                = SAI_SAMPEDGE_RISING;
    sai_struct.sync_mode                  = SAI_SYNCMODE_ASYNC;
    sai_struct.output_drive               = SAI_OUTPUT_WITH_SAIEN;
    sai_struct.clk_div_bypass             = SAI_CLKDIV_BYPASS_OFF;
    sai_struct.mclk_div                   = SAI_MCLKDIV_1;
    sai_struct.mclk_oversampling          = SAI_MCLK_OVERSAMP_256;
    sai_struct.mclk_enable                = SAI_MCLK_ENABLE;
    sai_struct.fifo_threshold             = SAI_FIFOTH_HALF;

    sai_init(SAI0, SAI_BLOCK0, &sai_struct);
}

/*!
    \brief      处理 TDM 输入缓冲区数据, 重新编排并送到输出缓冲区
    \param[in]  buf: TDM 输入缓冲区指针
    \param[out] 无
    \retval     无
*/
static uint8_t audio_rx_queue_peek(uint16_t *idx)
{
    uint8_t has_frame;

    NVIC_DisableIRQ(DMA0_Channel1_IRQn);
    has_frame = (audio_rx_q_count != 0U) ? 1U : 0U;
    if((has_frame != 0U) && (idx != NULL)) {
        *idx = audio_rx_q_rd;
    }
    NVIC_EnableIRQ(DMA0_Channel1_IRQn);

    return has_frame;
}

static void audio_rx_queue_consume(void)
{
    NVIC_DisableIRQ(DMA0_Channel1_IRQn);
    if(audio_rx_q_count != 0U) {
        audio_rx_q_rd++;
        if(audio_rx_q_rd >= AUDIO_RX_QUEUE_DEPTH) {
            audio_rx_q_rd = 0U;
        }
        audio_rx_q_count--;
    }
    buffer_ready = (audio_rx_q_count != 0U) ? 1U : 0U;
    NVIC_EnableIRQ(DMA0_Channel1_IRQn);
}

static void audio_rx_queue_trim_to(uint16_t keep)
{
    NVIC_DisableIRQ(DMA0_Channel1_IRQn);

    while(audio_rx_q_count > keep) {
        audio_rx_q_rd++;
        if(audio_rx_q_rd >= AUDIO_RX_QUEUE_DEPTH) {
            audio_rx_q_rd = 0U;
        }
        audio_rx_q_count--;
        audio_rx_q_drop++;
    }

    buffer_ready = (audio_rx_q_count != 0U) ? 1U : 0U;
    NVIC_EnableIRQ(DMA0_Channel1_IRQn);
}

static void audio_rx_queue_reset(void)
{
    NVIC_DisableIRQ(DMA0_Channel1_IRQn);
    audio_rx_q_wr = 0U;
    audio_rx_q_rd = 0U;
    audio_rx_q_count = 0U;
    audio_rx_q_drop = 0U;
    audio_rx_q_highwater = 0U;
    audio_rx_q_accept = 1U;
    buffer_ready = 0U;
    NVIC_EnableIRQ(DMA0_Channel1_IRQn);
}

static void audio_rx_queue_pause(void)
{
    NVIC_DisableIRQ(DMA0_Channel1_IRQn);
    audio_rx_q_accept = 0U;
    NVIC_EnableIRQ(DMA0_Channel1_IRQn);
}

static void audio_rx_queue_drain_all(void)
{
    uint16_t qidx;

    while(audio_rx_queue_peek(&qidx) != 0U) {
        process_audio_frame(audio_rx_queue[qidx]);
        audio_rx_queue_consume();
    }
}

static void audio_rx_queue_drain_limited(uint16_t max_frames)
{
    uint16_t qidx;
    uint16_t n = 0U;

    while((n < max_frames) && (audio_rx_queue_peek(&qidx) != 0U)) {
        process_audio_frame(audio_rx_queue[qidx]);
        audio_rx_queue_consume();
        n++;
    }
}

void process_buffer(uint32_t (*buf)[2 * SAMPLE_AUDIO_SIZE])
{
    /* 兼容旧接口：直接处理当前硬件双缓冲。正常主循环现在走 audio_rx_queue。 */
    process_audio_frame(buf[current_tdm_buffer]);
}

static void process_audio_frame(const uint32_t *frame)
{
    int i;
    uint8_t selected_channel;
    uint32_t data1;
    uint32_t data2;
    int16_t ch1;
    int16_t ch2;
    int16_t ch3;
    int16_t ch4;
    int16_t mic_sample;
    int16_t rn_out;
    uint32_t ch_abs_sum[4] = {0U, 0U, 0U, 0U};
    uint32_t ch_level[4];
    uint32_t raw_abs_sum = 0U;
    uint32_t out_abs_sum = 0U;
    uint32_t wave_step;
    uint32_t t_frame_start;
    uint32_t t_stage_start;

    static uint32_t dbg_cnt = 0U;

    if(frame == NULL) {
        return;
    }

    t_frame_start = perf_now();

    if((wavrec_is_recording() == 0U) && ((dbg_cnt++ % 200U) == 0U)) {
        printf("raw32:");
        for(i = 0; i < 16; i++) {
            printf(" %08X", frame[i]);
        }
        printf("\r\n");
    }

    /*
     * 第一遍：拆出 TDM 4 个 16bit 通道，并统计每路平均幅度。
     * 目的：确认真正接麦克风的是 ch1/ch2/ch3/ch4 哪一路。
     */
    t_stage_start = perf_now();

    for(i = 0; i < SAMPLE_AUDIO_SIZE; i++) {
        data1 = frame[2 * i];
        data2 = frame[2 * i + 1];

        ch1 = (int16_t)(data1 >> 16);
        ch2 = (int16_t)(data1 & 0xFFFF);
        ch3 = (int16_t)(data2 >> 16);
        ch4 = (int16_t)(data2 & 0xFFFF);

        left_mic[i]   = (uint16_t)ch1;
        right_mic[i]  = (uint16_t)ch2;
        ref_audio[i]  = (uint16_t)ch3;
        null_audio[i] = (uint16_t)ch4;

        ch_abs_sum[0] += abs16_level(ch1);
        ch_abs_sum[1] += abs16_level(ch2);
        ch_abs_sum[2] += abs16_level(ch3);
        ch_abs_sum[3] += abs16_level(ch4);
    }

    ch_level[0] = ch_abs_sum[0] / SAMPLE_AUDIO_SIZE;
    ch_level[1] = ch_abs_sum[1] / SAMPLE_AUDIO_SIZE;
    ch_level[2] = ch_abs_sum[2] / SAMPLE_AUDIO_SIZE;
    ch_level[3] = ch_abs_sum[3] / SAMPLE_AUDIO_SIZE;

    if((g_runtime_mic_channel >= 1U) && (g_runtime_mic_channel <= 4U)) {
        selected_channel = g_runtime_mic_channel;
    } else {
        selected_channel = 1U;
        if(ch_level[1] > ch_level[selected_channel - 1U]) {
            selected_channel = 2U;
        }
        if(ch_level[2] > ch_level[selected_channel - 1U]) {
            selected_channel = 3U;
        }
        if(ch_level[3] > ch_level[selected_channel - 1U]) {
            selected_channel = 4U;
        }
    }

    g_selected_mic_channel = selected_channel;

    wave_step = SAMPLE_AUDIO_SIZE / LCD_WAVE_POINTS;
    if(wave_step == 0U) {
        wave_step = 1U;
    }
    lcd_wave_reset();

    /*
     * 第二遍：先把选中的麦克风通道整理成连续 480 点 RAW PCM。
     * CRN 实时波形需要整帧 STFT/iFFT，不能像 RNNoise 那样在这个循环里逐点边收边算。
     */
    for(i = 0; i < SAMPLE_AUDIO_SIZE; i++) {
        switch(selected_channel) {
        case 1U:
            mic_sample = (int16_t)left_mic[i];
            break;
        case 2U:
            mic_sample = (int16_t)right_mic[i];
            break;
        case 3U:
            mic_sample = (int16_t)ref_audio[i];
            break;
        case 4U:
        default:
            mic_sample = (int16_t)null_audio[i];
            break;
        }

        mic_sample = mic_apply_gain(mic_sample);
        g_record_raw_frame[i] = mic_sample;
        raw_abs_sum += abs16_level(mic_sample);
    }

    perf_add(PERF_PARSE_LEVEL, perf_now() - t_stage_start);

#if CRN_REALTIME_WAVE_ENABLE
#if ENABLE_SD_WAV_RECORD
    if((wavrec_is_recording() == 0U) && (g_denoise_mode == DENOISE_MODE_CRN) &&
       (g_crn_rt_process_request != 0U))
#else
    if((g_denoise_mode == DENOISE_MODE_CRN) && (g_crn_rt_process_request != 0U))
#endif
    {
        crn_realtime_process_frame(g_record_raw_frame,
                                   g_crn_rt_display_raw,
                                   g_crn_rt_display_clean);
    }
#endif

    /*
     * 第三遍：根据当前模式得到 LCD 右侧显示波形。
     * RNN 是逐采样流式输出；CRN 是每 480 点输出一帧 iFFT/OLA clean 波形。
     */
    for(i = 0; i < SAMPLE_AUDIO_SIZE; i++) {
        mic_sample = g_record_raw_frame[i];

#if ENABLE_SD_WAV_RECORD
        if(wavrec_is_recording() != 0U) {
            /* 录音实时优先：录音中禁止 RNNoise/CRN，右声道临时复制 RAW。 */
            rn_out = mic_sample;
        } else
#endif
        if(g_denoise_mode == DENOISE_MODE_RNN) {
            rn_out = rnnoise_stream_process_sample(mic_sample);
        } else if(g_denoise_mode == DENOISE_MODE_CRN) {
#if CRN_REALTIME_WAVE_ENABLE
            /* CRN clean 与上一帧 RAW 对齐，整体有约 10ms 显示延迟。 */
            mic_sample = g_crn_rt_display_raw[i];
            rn_out = g_crn_rt_display_clean[i];
#else
#if CRN_REALTIME_PREVIEW_ENABLE
            rn_out = crn_curve_sample((uint32_t)i, SAMPLE_AUDIO_SIZE);
#else
            rn_out = mic_sample;
#endif
#endif
        } else {
            rn_out = mic_sample;
        }

        if(((uint32_t)i % wave_step) == 0U) {
            lcd_wave_push(mic_sample, rn_out);
        }

        out_abs_sum += abs16_level(rn_out);

        /* 当前没有扬声器，输出缓冲区仅保留给后续调试，不启动播放 DMA。 */
        output_buffer[current_output_buffer][2 * i] =
            ((uint32_t)(uint16_t)rn_out << 16);
        output_buffer[current_output_buffer][2 * i + 1] =
            ((uint32_t)(uint16_t)rn_out << 16);
    }

#if ENABLE_SD_WAV_RECORD
    if(wavrec_is_recording() != 0U) {
        uint32_t t_tf = perf_now();
        wavrec_write_raw_frame_as_stereo(g_record_raw_frame, SAMPLE_AUDIO_SIZE);
        perf_add(PERF_TF_WRITE, perf_now() - t_tf);
    } else
#endif
    {
#if CRN_REALTIME_PREVIEW_ENABLE
#if !CRN_REALTIME_WAVE_ENABLE
        if(g_denoise_mode == DENOISE_MODE_CRN) {
            if((g_crn_frame_cnt == 0U) || (g_crn_preview_div_cnt >= (CRN_PREVIEW_INVOKE_DIV - 1U))) {
                g_crn_preview_div_cnt = 0U;
                crn_probe_update_from_pcm(g_record_raw_frame, SAMPLE_AUDIO_SIZE);
            } else {
                g_crn_preview_div_cnt++;
            }
        }
#endif
#endif
    }

    g_raw_abs_level = raw_abs_sum / SAMPLE_AUDIO_SIZE;
    g_out_abs_level = out_abs_sum / SAMPLE_AUDIO_SIZE;

    if((wavrec_is_recording() == 0U) && ((dbg_cnt <= 3U) || ((dbg_cnt % 50U) == 0U))) {
        printf("CH_LEVEL sel=ch%u gain=x%u ch1=%u ch2=%u ch3=%u ch4=%u raw=%u clean=%u rn=%u rx=%u\r\n",
               (unsigned int)selected_channel,
               (unsigned int)(1U << g_runtime_gain_shift),
               (unsigned int)ch_level[0],
               (unsigned int)ch_level[1],
               (unsigned int)ch_level[2],
               (unsigned int)ch_level[3],
               (unsigned int)g_raw_abs_level,
               (unsigned int)g_out_abs_level,
               (unsigned int)g_rnnoise_frame_cnt,
               (unsigned int)dma_rx_irq_cnt);
    }

    if((wavrec_is_recording() == 0U) && (dbg_cnt <= 3U)) {
        printf("process done raw=%u clean=%u rn=%u rx=%u\r\n",
               (unsigned int)g_raw_abs_level,
               (unsigned int)g_out_abs_level,
               (unsigned int)g_rnnoise_frame_cnt,
               (unsigned int)dma_rx_irq_cnt);
    }

#if ENABLE_SD_WAV_RECORD
    if(wavrec_is_recording() != 0U) {
        g_vad_percent = 0U;
    } else
#endif
    if(g_denoise_mode != DENOISE_MODE_RNN) {
        g_vad_percent = 0U;
    } else if(g_rnnoise_last_vad <= 0.0f) {
        g_vad_percent = 0U;
    } else if(g_rnnoise_last_vad >= 1.0f) {
        g_vad_percent = 100U;
    } else {
        g_vad_percent = (uint8_t)(g_rnnoise_last_vad * 100.0f + 0.5f);
    }

    SCB_CleanDCache_by_Addr((uint32_t *)output_buffer[current_output_buffer],
                            2 * SAMPLE_AUDIO_SIZE * sizeof(uint32_t));

#if ENABLE_ES8311_OUTPUT
    current_output_buffer = !current_output_buffer;
    output_buffer_ready[current_output_buffer] = 1;
#endif

    perf_add(PERF_FRAME_TOTAL, perf_now() - t_frame_start);
}
