/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_protocol.h
 * @brief Protocol constants
 */

#ifndef __OTA_PROTOCOL_H__
#define __OTA_PROTOCOL_H__

#include "sdkconfig.h"

#if CONFIG_ESP_RMAKER_NEO_OTA_USE_MQTT
#define RMNG_OTA_PROTOCOL "MQTT"
#else
#error "Invalid OTA transport"
#endif

#endif /* __OTA_PROTOCOL_H__ */
