/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file dm_handlers.h
 * @brief Data model handlers for the host control.
 */

#ifndef __HOST_CTRL_PRIVATE_DATA_MODEL_HANDLERS_H__
#define __HOST_CTRL_PRIVATE_DATA_MODEL_HANDLERS_H__

/* Includes *******************************************************/

/* Standard includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Function declarations *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Handle a data model reset (disables standard services, clears stored values).
 */
void esp_rmaker_host_ctrl_data_model_on_reset(void);

/**
 * @brief Attempt to handle a buffer as a data model command.
 * @param[in] buffer The buffer to handle.
 * @param[in] buffer_length The length of the buffer.
 * @return True if the buffer was handled, false otherwise.
 */
bool esp_rmaker_host_ctrl_data_model_handle_buffer(uint8_t *buffer, size_t buffer_length);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_CTRL_PRIVATE_DATA_MODEL_HANDLERS_H__ */
