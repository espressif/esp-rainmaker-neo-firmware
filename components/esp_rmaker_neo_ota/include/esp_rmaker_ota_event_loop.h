/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file esp_rmaker_ota_event_loop.h
 * @brief Public header for the OTA event loop events.
 */

#ifndef __ESP_RMAKER_OTA_EVENT_LOOP_H__
#define __ESP_RMAKER_OTA_EVENT_LOOP_H__

#include "osal_event_loop.h"
#include "esp_rmaker_ota_status_details.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @cond **/
/** ESP RainMaker Neo Event Base */
OSAL_EVENT_DECLARE_BASE(RMAKER_OTA_EVENT);
/** @endcond **/

#ifdef __cplusplus
}
#endif

/** ESP RainMaker Neo OTA Events */
typedef enum {
    /** Wildcard: register a handler with this id to receive every RMAKER_OTA_EVENT. */
    RMAKER_OTA_EVENT_BASE_ANY = OSAL_EVENT_ID_ANY,

    /* Invalid event. Used for internal handling only */
    RMAKER_OTA_EVENT_INVALID = 0,
    /**
     * RainMaker Neo OTA is Starting
     * - Event data: *esp_rmaker_ota_status_details_t
     * - Type: ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_STARTING
     */
    RMAKER_OTA_EVENT_STARTING,
    /**
     * RainMaker Neo OTA Fetch request ignored
     * This event is reported when the OTA state machine is not in an appropriate state to handle a fetch request via esp_rmaker_ota_fetch().
     * e.g., the OTA state machine is currently processing a job.
     * You can use this event to trigger a delayed fetch request.
     * - Event data: NULL
     */
    RMAKER_OTA_EVENT_FETCH_REQUEST_IGNORED,
    /**
     * RainMaker Neo OTA download resumed from a previous partial download.
     * Only fired when CONFIG_RMNG_OTA_RESUME=y and a matching persisted tracker was found.
     * - Event data: *uint32_t  (resume byte offset - bytes already present at the start of this attempt)
     */
    RMAKER_OTA_EVENT_RESUMED,
    /**
     * RainMaker Neo OTA in progress
     * - Event data: *esp_rmaker_ota_status_details_t
     * - Type: ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_IN_PROGRESS
     */
    RMAKER_OTA_EVENT_IN_PROGRESS,
    /**
     * RainMaker Neo OTA Successful
     * - Event data: *esp_rmaker_ota_status_details_t
     * - Type: ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_SUCCEEDED
     */
    RMAKER_OTA_EVENT_SUCCESSFUL,
    /**
     * RainMaker Neo OTA Failed
     * - Event data: *esp_rmaker_ota_status_details_t
     * - Type: ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_FAILED
     */
    RMAKER_OTA_EVENT_FAILED,
    /**
     * RainMaker Neo OTA Rejected
     * - Event data: *esp_rmaker_ota_status_details_t
     * - Type: ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_REJECTED
     */
    RMAKER_OTA_EVENT_REJECTED,
    /**
     * RainMaker Neo OTA Delayed
     * - Event data: *esp_rmaker_ota_status_details_t
     * - Type: ESP_RMAKER_OTA_STATUS_DETAILS_TYPE_DELAYED
     */
    RMAKER_OTA_EVENT_DELAYED,
    /**
     * OTA Image has been flashed and active partition changed. Reboot is requested. Applicable only if Auto reboot is disabled
     * - Event data: NULL
     */
    RMAKER_OTA_EVENT_REQ_FOR_REBOOT,
    /**
     * RainMaker Neo OTA Error Occurred
     * - Event data: *esp_rmaker_ota_error_reason_t
     */
    RMAKER_OTA_EVENT_ERROR_OCCURRED,
} esp_rmaker_ota_event_t;

#endif /* __ESP_RMAKER_OTA_EVENT_LOOP_H__ */
