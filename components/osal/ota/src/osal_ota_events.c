/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_events.c
 * @brief OTA event registration.
 */

#include "osal_ota.h"
#include "osal_ota_events.h"

#include <stddef.h>

#include "osal_log.h"
#include "osal_ticks.h"

/* Variables ***************************************/

static const char *TAG = "osal_ota_events";

static osal_ota_event_loop_registration_info_t __registration_info = { 0 };

/* Function definitions ***************************************/

osal_err_t osal_ota_event_loop_init(const osal_ota_event_loop_registration_info_t *event_loop_registration_info)
{
    if (event_loop_registration_info == NULL) {
        __registration_info = (osal_ota_event_loop_registration_info_t) {
            0
        };
        return OSAL_ERR_OK;
    }
    if (event_loop_registration_info->event_base == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }
    __registration_info = *event_loop_registration_info;
    return OSAL_ERR_OK;
}

void osal_ota_events_post_app_valid(void)
{
    if (__registration_info.event_base == NULL) {
        return;
    }
    osal_err_t err = osal_event_post(__registration_info.event_base, __registration_info.event_ids.app_valid, NULL, 0, OSAL_MAX_DELAY);
    if (err != OSAL_ERR_OK) {
        OSAL_LOGD(TAG, "Failed to post the app valid event: %d", err);
    }
}
