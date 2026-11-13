/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_processing_impl.c
 * @brief Processing functions for the host control.
 */

/* Includes *******************************************************/

/* Declarations */
#include "host_ctrl_processing.h"

/* Standard includes */
#include <string.h>
#include <stdlib.h>

/* Constants */
#include "esp_rmaker_host_ctrl_constants.h"

/* External I/O common includes */
#include "osal_ext_io.h"

/* Platform common includes */
#include "osal_log.h"

/* Private variables *********************************************************/

/**
 * @brief The tag for logging.
 */
static const char *TAG = "rmng_hc_processing";

/**
 * @brief Correlation id echoed on the next response; empty if the command carried none.
 * @note One slot only, valid while a single task handles one command at a time.
 */
static char __response_seq[RMAKER_HOST_CTRL_SEQ_DIGITS + 1];

/* Private function declarations *********************************************/

bool esp_rmaker_host_ctrl_find_and_nullify_delimiters(uint8_t *buffer, size_t buffer_length, char *p_delimiters[], uint8_t expected_count)
{
    uint8_t delimiter_count = 0;

    for (uint8_t i = 0; i < buffer_length; i++) {
        if (buffer[i] == RMAKER_HOST_CTRL_DELIMITER_CHAR) {
            if (delimiter_count >= expected_count) {
                // More delimiters than expected
                return false;
            }

            p_delimiters[delimiter_count++] = (char *) &buffer[i];
            buffer[i] = '\0';
        }
    }

    return delimiter_count == expected_count;
}

bool esp_rmaker_host_ctrl_get_param_value(char *param_value_start, char *param_value_end, esp_rmaker_param_val_t *p_param_value)
{
    if (param_value_end - param_value_start <= 1) {
        OSAL_LOGE(TAG, "Invalid param value: %s", param_value_start);
        return false;
    }

    switch (param_value_start[0]) {
    case RMAKER_HOST_CTRL_DATA_TYPE_CHAR_INT:
        p_param_value->type = RMAKER_VAL_TYPE_INTEGER;
        p_param_value->val.i = atoi(param_value_start + 1);
        break;
    case RMAKER_HOST_CTRL_DATA_TYPE_CHAR_FLOAT:
        p_param_value->type = RMAKER_VAL_TYPE_FLOAT;
        p_param_value->val.f = atof(param_value_start + 1);
        break;
    case RMAKER_HOST_CTRL_DATA_TYPE_CHAR_BOOLEAN:
        p_param_value->type = RMAKER_VAL_TYPE_BOOLEAN;
        p_param_value->val.b = atoi(param_value_start + 1);
        break;
    case RMAKER_HOST_CTRL_DATA_TYPE_CHAR_STRING:
        p_param_value->type = RMAKER_VAL_TYPE_STRING;
        p_param_value->val.s = param_value_start + 1; // No need to strdup, the value is allocated by the add/create functions
        break;
    case RMAKER_HOST_CTRL_DATA_TYPE_CHAR_OBJECT:
        p_param_value->type = RMAKER_VAL_TYPE_OBJECT;
        p_param_value->val.s = param_value_start + 1; // No need to strdup, the value is allocated by the add/create functions
        break;
    case RMAKER_HOST_CTRL_DATA_TYPE_CHAR_ARRAY:
        p_param_value->type = RMAKER_VAL_TYPE_ARRAY;
        p_param_value->val.s = param_value_start + 1; // No need to strdup, the value is allocated by the add/create functions
        break;
    default:
        OSAL_LOGE(TAG, "Invalid data type character encountered: %c", param_value_start[0]);
        return false;
    }

    return true;
}

int esp_rmaker_host_ctrl_format_param_value(esp_rmaker_param_val_t *p_param_value, char *buffer, size_t buffer_length)
{
    switch (p_param_value->type) {
    case RMAKER_VAL_TYPE_INTEGER:
        return snprintf(buffer, buffer_length, "%c%d", RMAKER_HOST_CTRL_DATA_TYPE_CHAR_INT, p_param_value->val.i);
    case RMAKER_VAL_TYPE_FLOAT:
        return snprintf(buffer, buffer_length, "%c%f", RMAKER_HOST_CTRL_DATA_TYPE_CHAR_FLOAT, p_param_value->val.f);
    case RMAKER_VAL_TYPE_BOOLEAN:
        return snprintf(buffer, buffer_length, "%c%d", RMAKER_HOST_CTRL_DATA_TYPE_CHAR_BOOLEAN, p_param_value->val.b);
    case RMAKER_VAL_TYPE_STRING:
        return snprintf(buffer, buffer_length, "%c%s", RMAKER_HOST_CTRL_DATA_TYPE_CHAR_STRING, p_param_value->val.s);
    case RMAKER_VAL_TYPE_OBJECT:
        return snprintf(buffer, buffer_length, "%c%s", RMAKER_HOST_CTRL_DATA_TYPE_CHAR_OBJECT, p_param_value->val.s);
    case RMAKER_VAL_TYPE_ARRAY:
        return snprintf(buffer, buffer_length, "%c%s", RMAKER_HOST_CTRL_DATA_TYPE_CHAR_ARRAY, p_param_value->val.s);
    default:
        OSAL_LOGE(TAG, "Invalid param value type: %d", p_param_value->type);
        return -1;
    }
}

bool esp_rmaker_host_ctrl_add_flag_c_to_buffer(char flag_c, char **p_buffer, size_t *p_buffer_length)
{
    if (*p_buffer_length <= 0) {
        return false;
    }

    **p_buffer = flag_c;
    (*p_buffer)++;
    (*p_buffer_length)--;
    return true;
}

void esp_rmaker_host_ctrl_set_response_seq(const char *seq)
{
    if (seq == NULL) {
        __response_seq[0] = '\0';
        return;
    }
    memcpy(__response_seq, seq, RMAKER_HOST_CTRL_SEQ_DIGITS);
    __response_seq[RMAKER_HOST_CTRL_SEQ_DIGITS] = '\0';
}

void esp_rmaker_host_ctrl_take_response_seq(uint8_t **p_buffer, size_t *p_buffer_length)
{
    esp_rmaker_host_ctrl_set_response_seq(NULL);
    if (*p_buffer_length > RMAKER_HOST_CTRL_SEQ_DIGITS + 1 && (*p_buffer)[0] == RMAKER_HOST_CTRL_SEQ_CHAR) {
        esp_rmaker_host_ctrl_set_response_seq((const char *)*p_buffer + 1);
        *p_buffer += RMAKER_HOST_CTRL_SEQ_DIGITS + 1;
        *p_buffer_length -= RMAKER_HOST_CTRL_SEQ_DIGITS + 1;
    }
}

void esp_rmaker_host_ctrl_send_response(const char response_code)
{
    esp_rmaker_host_ctrl_send_response_with_payload(response_code, NULL, 0);
}

void esp_rmaker_host_ctrl_send_response_with_payload(const char response_code, const char *payload, size_t payload_length)
{
    size_t seq_length = __response_seq[0] != '\0' ? RMAKER_HOST_CTRL_SEQ_DIGITS + 1 : 0;
    char buffer[seq_length + payload_length + 2];
    char *cur = buffer;

    if (seq_length > 0) {
        *cur++ = RMAKER_HOST_CTRL_SEQ_CHAR;
        memcpy(cur, __response_seq, RMAKER_HOST_CTRL_SEQ_DIGITS);
        cur += RMAKER_HOST_CTRL_SEQ_DIGITS;
    }
    *cur++ = response_code;
    if (payload != NULL && payload_length > 0) {
        memcpy(cur, payload, payload_length);
        cur += payload_length;
    }
    *cur++ = RMAKER_HOST_CTRL_END_CHAR;

    size_t buffer_length = (size_t)(cur - buffer);
    OSAL_LOGD(TAG, "Sending response: %.*s, length: %d", (int)buffer_length, buffer, (int)buffer_length);
    osal_ext_io_write_line(buffer, buffer_length);
}
