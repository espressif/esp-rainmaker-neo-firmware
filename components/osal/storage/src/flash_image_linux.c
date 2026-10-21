/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file flash_image_linux.c
 * @brief Emulate flash with the image named by RMNG_FLASH_IMAGE on the ESP-IDF linux target.
 */

/* Includes *******************************************************/

/* Standard includes. */
#include <stdlib.h>
#include <string.h>

/* ESP-IDF includes. */
#include "esp_private/partition_linux.h"

/* Function declarations *******************************************************/

/** @brief Name the flash image before app_main, so it precedes the first partition access. */
__attribute__((constructor)) void osal_flash_image_linux_init(void)
{
    const char *image = getenv("RMNG_FLASH_IMAGE");
    esp_partition_file_mmap_ctrl_t *ctrl = esp_partition_get_file_mmap_ctrl_input();
    if (image && image[0] && ctrl->flash_file_name[0] == '\0') {
        strlcpy(ctrl->flash_file_name, image, sizeof(ctrl->flash_file_name));
    }
}
