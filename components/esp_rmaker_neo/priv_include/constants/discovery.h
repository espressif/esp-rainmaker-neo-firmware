/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file discovery.h
 * @brief Local discovery constants.
 */

#ifndef __DISCOVERY_CONSTANTS_H__
#define __DISCOVERY_CONSTANTS_H__

/* Local control service **************************************************************/

/* Wire contract, and 15 bytes after the underscore is the RFC 6763 section 7.2 ceiling for a
 * service name - "esp_rmaker_ctrl" is exactly at it. The "esp_rmaker_" prefix alone
 * costs 11, so there is room for four more characters and no more: a more descriptive
 * name (`_esp_rmaker_local_ctrl`, `_esp_rmaker_local`) does not fit. "local" would be
 * redundant anyway, since mDNS is link-local by definition. Which endpoint sets a node
 * actually serves comes from the `cap` TXT record below, not from the service name. */
#define RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_TYPE "_esp_rmaker_ctrl"
#define RMAKER_DISCOVERY_LOCAL_CTRL_SERVICE_PROTOCOL "_tcp"
#define RMAKER_DISCOVERY_LOCAL_CTRL_TXT_ITEM_NODE_ID "node_id"
#define RMAKER_DISCOVERY_LOCAL_CTRL_TXT_ITEM_CAP "cap"

#endif /* __DISCOVERY_CONSTANTS_H__ */
