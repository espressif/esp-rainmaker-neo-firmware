/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_core.h
 * @brief RainMaker Neo core: umbrella header and node lifecycle entry points.
 */

#ifndef __ESP_RMAKER_CORE_H__
#define __ESP_RMAKER_CORE_H__

/* Includes *******************************************************/

/* Node model includes. */
#include "esp_rmaker_node.h"

/* Configuration */
#include "sdkconfig.h"

/* Data model includes. */
#include "esp_rmaker_data_model.h"

/* Standard type includes. */
#include "esp_rmaker_standard_types.h"
#include "esp_rmaker_standard_params.h"
#include "esp_rmaker_standard_devices.h"
#include "esp_rmaker_standard_services.h"

/* Value includes. */
#include "esp_rmaker_val.h"

/* Error includes. */
#include "esp_rmaker_error_types.h"

/* Flow includes. */
#include "esp_rmaker_flow.h"

/* Event loop includes. */
#include "esp_rmaker_event_loop.h"

/* Credentials includes. */
#include "esp_rmaker_credentials_access.h"

/* Serial console includes. */
#include "esp_rmaker_console.h"

#endif /* __ESP_RMAKER_CORE_H__ */
