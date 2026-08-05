/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file host_ctrl_processing.h
 * @brief Processing functions for the host control.
 */

#ifndef __HOST_CTRL_PRIVATE_PROCESSING_H__
#define __HOST_CTRL_PRIVATE_PROCESSING_H__

/* Includes *******************************************************/

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* RMNG includes */
#include "esp_rmaker_val.h"

/* Types *******************************************************/

/**
 * @brief The handler function type.
 */
typedef void (*esp_rmaker_host_ctrl_payload_handler_t)(uint8_t *buffer, size_t buffer_length);

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Find and nullify delimiters in the buffer.
 * @param[in] buffer The buffer to search.
 * @param[in] buffer_length The length of the buffer.
 * @param[out] p_delimiters The array of pointers to the delimiters.
 * @param[in] expected_count The expected count of delimiters.
 * @return True if the delimiters count is equal to the expected count, false otherwise.
 */
bool esp_rmaker_host_ctrl_find_and_nullify_delimiters(uint8_t *buffer, size_t buffer_length, char *p_delimiters[], uint8_t expected_count);

/**
 * @brief Get the param value from the buffer, of format `<param_data_type><param_value>`.
 * @param[in] param_value_start The start pointer of the param value.
 * @param[in] param_value_end The end pointer of the param value.
 * @param[out] p_param_value The pointer to the param value.
 * @return True if the param value is valid, false otherwise.
 */
bool esp_rmaker_host_ctrl_get_param_value(char *param_value_start, char *param_value_end, esp_rmaker_param_val_t *p_param_value);

/**
 * @brief Format the param value to the buffer.
 * @param[in] p_param_value Pointer to the param value.
 * @param[out] buffer The buffer to format the param value to. If NULL, the function will return the required length of the buffer.
 * @param[in] buffer_length The maximum length of the buffer.
 * @return The length of the formatted param value.
 */
int esp_rmaker_host_ctrl_format_param_value(esp_rmaker_param_val_t *p_param_value, char *buffer, size_t buffer_length);

/**
 * @brief Add a flag character to the buffer.
 * @param[in] flag_c The flag character to add.
 * @param[in,out] p_buffer The pointer to the buffer.
 * @param[in,out] p_buffer_length The length of the buffer.
 * @return True if the flag character was added, false otherwise.
 */
bool esp_rmaker_host_ctrl_add_flag_c_to_buffer(char flag_c, char **p_buffer, size_t *p_buffer_length);

/**
 * @brief Send a response to the host control.
 * @param[in] response_code The response code.
 */
void esp_rmaker_host_ctrl_send_response(const char response_code);

/**
 * @brief Send a response with a payload to the host control.
 * @param[in] response_code The response code.
 * @param[in] payload The payload to send.
 * @param[in] payload_length The length of the payload.
 */
void esp_rmaker_host_ctrl_send_response_with_payload(const char response_code, const char *payload, size_t payload_length);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_CTRL_PRIVATE_PROCESSING_H__ */
