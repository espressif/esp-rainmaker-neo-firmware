/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_random.h
 * @brief POSIX random number generator header file.
 */

#ifndef __ESP_RANDOM_H__
#define __ESP_RANDOM_H__

#include "osal_random.h"

#define esp_random() osal_random_generate()
#define esp_fill_random(buf, size) osal_random_fill(buf, size)

#endif /* __ESP_RANDOM_H__ */
