/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file common_events.c
 * @brief Common events implementation.
 */

/* Common events includes */
#include "esp_rmaker_common_events.h"

/* Define the common event base */
OSAL_EVENT_DEFINE_BASE(RMAKER_COMMON_EVENT);
