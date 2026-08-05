/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file network.h
 * @brief Network constants.
 */

#ifndef __RMAKER_NETWORK_CONSTANTS_H__
#define __RMAKER_NETWORK_CONSTANTS_H__

/* Timeout constants *************************************************************/

#define RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_CRITICAL                 5000 /**< Critical subscription timeout in milliseconds. */
#define RMAKER_NETWORK_SUBSCRIPTION_TIMEOUT_MS_NON_CRITICAL             1000 /**< Non-critical subscription timeout in milliseconds. */
#define RMAKER_NETWORK_SHADOWS_GET_TIMEOUT_MS                           3000 /**< Shadows get timeout in milliseconds. */

/* Event group bits ***************************************************************/

#define RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_CLOUD              (1 << 0) /**< Subscribed to cloud. */
#define RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_CLOUD          (1 << 1) /**< Unsubscribed from cloud. */

#define RMAKER_NETWORK_EVENT_GROUP_BIT_SUBSCRIBED_TO_STATE_CHANGES      (1 << 2) /**< Subscribed to state changes. */
#define RMAKER_NETWORK_EVENT_GROUP_BIT_UNSUBSCRIBED_FROM_STATE_CHANGES  (1 << 3) /**< Unsubscribed from state changes. */

/* Remote control includes ********************************************************/

#define RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_REPORTED          (1 << 4) /**< Indexed shadow reported. */
#define RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_SUBSCRIBED        (1 << 5) /**< Indexed shadow subscribed. */
#define RMAKER_NETWORK_EVENT_GROUP_BIT_INDEXED_SHADOW_UNSUBSCRIBED      (1 << 6) /**< Indexed shadow unsubscribed. */

#define RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_REPORTED            (1 << 7) /**< Named shadow reported. */
#define RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_SUBSCRIBED          (1 << 8) /**< Named shadow subscribed. */
#define RMAKER_NETWORK_EVENT_GROUP_BIT_NAMED_SHADOW_UNSUBSCRIBED        (1 << 9) /**< Named shadow unsubscribed. */

#endif /* __RMAKER_NETWORK_CONSTANTS_H__ */
