#ifndef CRN_MODEL_H
#define CRN_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRN_MODEL_INPUT_SIZE   256U
#define CRN_MODEL_OUTPUT_SIZE  32U
#define CRN_MODEL_CONTEXT      8U
#define CRN_MODEL_BANDS        32U

int crn_model_port_init(void);
int crn_model_is_ready(void);
int crn_model_invoke_256(const int8_t input[CRN_MODEL_INPUT_SIZE],
                         int8_t output[CRN_MODEL_OUTPUT_SIZE]);
const char *crn_model_status_text(void);
int crn_model_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* CRN_MODEL_H */
