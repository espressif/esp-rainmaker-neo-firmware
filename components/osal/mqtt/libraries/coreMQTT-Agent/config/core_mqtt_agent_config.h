/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "core_mqtt_common_logging.h"

#ifdef LIBRARY_LOG_NAME
#undef LIBRARY_LOG_NAME
#endif
#define LIBRARY_LOG_NAME "coreMQTT-Agent"

/* Wait only a short interval each iteration for faster
 * processing of incoming data. */
#define MQTT_AGENT_MAX_EVENT_QUEUE_WAIT_TIME       ( 10U )

/* We will be downloading the OTA image one block at a time.
 * Limit the number of outstanding ACK we need. */
#define MQTT_AGENT_MAX_OUTSTANDING_ACKS            ( 10U )
