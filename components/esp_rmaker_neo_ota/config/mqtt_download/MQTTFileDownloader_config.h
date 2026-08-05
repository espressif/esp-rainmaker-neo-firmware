/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Configuration for AWS IoT Core MQTT File Streams Embedded C library
 */

#ifndef MQTT_FILE_DOWNLOADER_CONFIG_H
#define MQTT_FILE_DOWNLOADER_CONFIG_H

#include "sdkconfig.h"

/**
 * @brief Configure the Maximum size of the data payload.
 * Ensure this does not exceed the MQTT buffer size.
 * @note This final size also cannot be smaller than 256 bytes, otherwise the AWS IoT Core will reject the request.
 */
#ifdef CONFIG_RMNG_OTA_MQTT_DATA_TYPE_CBOR
// CBOR will add ~30 bytes overhead to the block size
#define mqttFileDownloader_CONFIG_BLOCK_SIZE  (CONFIG_RMNG_OTA_MQTT_BLOCK_SIZE - 30)
#if mqttFileDownloader_CONFIG_BLOCK_SIZE < 256
#error "Minimum config block size (CBOR): 286 bytes"
#endif
#elif CONFIG_RMNG_OTA_MQTT_DATA_TYPE_JSON
// JSON will add ~35% overhead to the block size (block is base64 encoded)
#define mqttFileDownloader_CONFIG_BLOCK_SIZE  (CONFIG_RMNG_OTA_MQTT_BLOCK_SIZE * 100 / 135)
#if mqttFileDownloader_CONFIG_BLOCK_SIZE < 256
#error "Minimum config block size (JSON): 346 bytes"
#endif
#else
#error "Invalid data type configuration: RMNG_OTA_MQTT_DATA_TYPE"
#endif

#endif /* MQTT_FILE_DOWNLOADER_CONFIG_H */
