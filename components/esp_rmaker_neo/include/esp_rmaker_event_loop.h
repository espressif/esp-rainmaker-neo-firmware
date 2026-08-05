/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_event_loop.h
 * @brief Public interface for the event loop for the ESP RainMaker Neo SDK.
 */

#ifndef __ESP_RMAKER_EVENT_LOOP_H__
#define __ESP_RMAKER_EVENT_LOOP_H__

/* Include common events **********************************************************/

#include "esp_rmaker_common_events.h"

/* Includes **********************************************************************/

/* Platform common includes */
#include "osal_event_loop.h"

/* Constants **********************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event loop base event.
 */
OSAL_EVENT_DECLARE_BASE(RMAKER_EVENT);

#ifdef __cplusplus
}
#endif

/**
 * @brief Events posted on the ``RMAKER_EVENT`` base.
 */
typedef enum {
    /** Wildcard: register a handler with this id to receive every RMAKER_EVENT. */
    RMAKER_EVENT_BASE_ANY = OSAL_EVENT_ID_ANY,

    /* Core Events *****************************************************************/

    /** Core initialized.
     * Event data will be NULL.
     */
    RMAKER_EVENT_INIT_DONE,

    /** Core started.
     * Event data will be NULL.
     */
    RMAKER_EVENT_CORE_STARTED,

    /* Local control Events *************************************************************/

    /** Local control started.
     * Event data will contain the service name.
     */
    RMAKER_EVENT_LOCAL_CTRL_STARTED,

    /** Local control stopped.
     * No event data will be provided.
     */
    RMAKER_EVENT_LOCAL_CTRL_STOPPED,

    /* Bridge Events ***************************************************************/

    /** A bridged child Thing has been created on the cloud and is ready to use.
     * Event data: ``esp_rmaker_event_bridge_child_added_t *`` (see esp_rmaker_bridge.h).
     */
    RMAKER_EVENT_BRIDGE_CHILD_ADDED,

    /** A bridged child Thing has been removed from the cloud.
     * Event data: ``esp_rmaker_event_bridge_child_removed_t *``.
     */
    RMAKER_EVENT_BRIDGE_CHILD_REMOVED,

    /** Add-child cloud request failed (error response or timeout).
     * Event data: ``esp_rmaker_event_bridge_child_failed_t *``.
     */
    RMAKER_EVENT_BRIDGE_CHILD_ADD_FAILED,

    /** Remove-child cloud request failed (error response or timeout).
     * Event data: ``esp_rmaker_event_bridge_child_failed_t *``.
     */
    RMAKER_EVENT_BRIDGE_CHILD_REMOVE_FAILED,

    /** A bridged child's group / subgroup info has been updated from the cloud.
     * Event data: ``esp_rmaker_event_bridge_child_group_info_t *``.
     */
    RMAKER_EVENT_BRIDGE_CHILD_GROUP_INFO_UPDATED,

    /* Claiming Events *************************************************************/

    /** Claiming has started.
     * Only posted when claiming is enabled and the node is not already claimed.
     * Event data will be NULL.
     */
    RMAKER_EVENT_CLAIM_STARTED,

    /** Claiming was successful; the certificate and node ID have been persisted.
     * Event data will be NULL.
     */
    RMAKER_EVENT_CLAIM_SUCCESSFUL,

    /** Claiming failed, or was aborted by the phone app.
     * Event data will be NULL.
     */
    RMAKER_EVENT_CLAIM_FAILED,

} esp_rmaker_event_t;

#endif /* __ESP_RMAKER_EVENT_LOOP_H__ */
