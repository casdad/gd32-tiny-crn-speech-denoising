#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "crn_model.h"
#include "gd_nn_interface.h"
#include "nn_model_configure.h"

/*
 * CRN model wrapper for the recorder project.
 *
 * This file only wraps the GD32 Embedded AI generated invoke interface:
 *   int8 input[256]  ->  int8 output[32]
 * It does not implement the real audio STFT/iFFT post-processing yet.
 */

/* input and output pointer required by generated nn_model_configure.h */
int8_t *input_data = (int8_t *)static_buffer_peak;
int8_t *output_data_ptr[1] = {0};

/* report object is required by the generated model struct. We do not print it. */
static nn_model_report_struct g_crn_report;
static nn_uint8 g_model_paras_array_info_buf[8];
static const nn_uint8 *g_model_paras_array_and_data[2] = {
    (const nn_uint8 *)model_paras_arr,
    (const nn_uint8 *)model_paras_data
};

static nn_model g_crn_model;
static uint8_t g_crn_ready = 0U;
static int g_crn_last_error = 0;

#define CRN_NN_MODEL_STRUCT_INIT(m_struct)               \
    do {                                                 \
        (m_struct).user_input = input_data;              \
        (m_struct).user_input_size = INPUT_SIZE * INPUT_TYPE_SIZE;      \
        (m_struct).user_output = (void **)output_data_ptr;              \
        (m_struct).user_output_size = OUTPUT_SIZE * OUTPUT_TYPE_SIZE;   \
        (m_struct).operators_cb_array = func_cb_arr;     \
        (m_struct).model_paras_array = (const nn_uint8 *)g_model_paras_array_and_data; \
        (m_struct).model_paras_array_dict = g_model_paras_array_info_buf;              \
        (m_struct).report_ptr = &g_crn_report;           \
    } while(0)

int crn_model_port_init(void)
{
    error_type ret;

    memset(&g_crn_report, 0, sizeof(g_crn_report));
    memset(g_model_paras_array_info_buf, 0, sizeof(g_model_paras_array_info_buf));
    memset(&g_crn_model, 0, sizeof(g_crn_model));

    g_crn_report.core_clock = SystemCoreClock;
    g_crn_report.model_name = model_name;
    g_crn_report.operator_name = (char **)operator_name;

    /* 本 CRN 每次输入已经包含 8 帧上下文。
     * 为了尽量贴近 Python 推理：model(hist.unsqueeze(0)) 每次 hidden state 从 0 开始，
     * 这里要求 GD32 LSTM 状态每次 invoke 前清零，避免跨窗口重复记忆。
     */
    undirectional_lstm_cell_and_out_state_need_clear_every_invoke = 1U;
    bidirectional_lstm_cell_and_out_state_need_clear_every_invoke = 1U;

    CRN_NN_MODEL_STRUCT_INIT(g_crn_model);

    ret = nn_model_init(&g_crn_model);
    if(ret != ok) {
        g_crn_ready = 0U;
        g_crn_last_error = (int)ret;
        return -1;
    }

    g_crn_ready = 1U;
    g_crn_last_error = 0;
    return 0;
}

int crn_model_is_ready(void)
{
    return (g_crn_ready != 0U) ? 1 : 0;
}

int crn_model_last_error(void)
{
    return g_crn_last_error;
}

const char *crn_model_status_text(void)
{
    if(g_crn_ready != 0U) {
        return "OK";
    }
    return "ERR";
}

int crn_model_invoke_256(const int8_t input[CRN_MODEL_INPUT_SIZE],
                         int8_t output[CRN_MODEL_OUTPUT_SIZE])
{
    error_type ret;

    if((g_crn_ready == 0U) || (input == NULL) || (output == NULL)) {
        return -1;
    }

    memcpy(input_data, input, CRN_MODEL_INPUT_SIZE);

    ret = nn_model_invoke(&g_crn_model);
    if(ret != ok) {
        g_crn_last_error = (int)ret;
        return -2;
    }

    if(output_data_ptr[0] == NULL) {
        g_crn_last_error = -1000;
        return -3;
    }

    memcpy(output, output_data_ptr[0], CRN_MODEL_OUTPUT_SIZE);
    g_crn_last_error = 0;
    return 0;
}
