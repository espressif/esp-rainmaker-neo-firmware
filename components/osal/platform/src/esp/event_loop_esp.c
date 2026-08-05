/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_event_loop.h"
#include "esp_event.h"
#include "esp_err.h"

osal_err_t osal_event_loop_create_default(void)
{
    esp_err_t esp_err = esp_event_loop_create_default();
    if (esp_err == ESP_ERR_NO_MEM) {
        return OSAL_ERR_NO_MEM;
    }
    if (esp_err == ESP_ERR_INVALID_STATE) {
        return OSAL_ERR_INVALID_STATE;
    }
    if (esp_err != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_event_loop_delete_default(void)
{
    esp_err_t esp_err = esp_event_loop_delete_default();
    if (esp_err != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_event_handler_register(osal_event_base_t event_base, int32_t event_id, osal_event_handler_t event_handler, void *event_handler_arg)
{
    esp_err_t esp_err = esp_event_handler_register((esp_event_base_t) event_base, event_id, (esp_event_handler_t) event_handler, event_handler_arg);
    if (esp_err != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_event_handler_unregister(osal_event_base_t event_base, int32_t event_id, osal_event_handler_t event_handler)
{
    esp_err_t esp_err = esp_event_handler_unregister((esp_event_base_t) event_base, event_id, (esp_event_handler_t) event_handler);
    if (esp_err != ESP_OK && esp_err != ESP_ERR_NOT_FOUND) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}

osal_err_t osal_event_post(osal_event_base_t event_base, int32_t event_id, void *event_data, size_t event_data_size, osal_tick_type_t ticks_to_wait)
{
    esp_err_t esp_err = esp_event_post((esp_event_base_t) event_base, event_id, event_data, event_data_size, ticks_to_wait);
    if (esp_err != ESP_OK) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}
