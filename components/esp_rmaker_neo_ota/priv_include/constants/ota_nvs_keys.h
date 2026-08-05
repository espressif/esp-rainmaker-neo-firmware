/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file ota_nvs_keys.h
 * @brief NVS constants
 */

#ifndef __OTA_NVS_KEYS_H__
#define __OTA_NVS_KEYS_H__

/* Common constants **************************************************************/

#include "constants/esp_rmaker_nvs_common.h"

/* OTA state **************************************************************/

#define RMAKER_NVS_OTA_NAMESPACE        "ota"           /**< OTA namespace */
#define RMAKER_NVS_OTA_KEY_LAST_JOB_ID  "last_jobid"    /**< Last job ID */
#define RMAKER_NVS_OTA_KEY_LAST_FILETYPE "last_ftype"   /**< Last filetype */
#define RMAKER_NVS_OTA_KEY_LAST_VERSION "last_ver"      /**< Last version number for updating */
#define RMAKER_NVS_OTA_KEY_STATUS_FLAGS "flags"         /**< Flags for the OTA status */
#define RMAKER_NVS_OTA_KEY_RESUME_DESC  "rsm_desc"      /**< Auto-resume descriptor (image identity + transport) */
#define RMAKER_NVS_OTA_KEY_RESUME_DATA  "rsm_data"      /**< Auto-resume progress tracker (MQTT bitmap / HTTP byte count) */

#endif /* __OTA_NVS_KEYS_H__ */
