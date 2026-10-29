/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file osal_ota_events.h
 * @brief OTA event posts that the platform implementations share.
 */

#ifndef __OSAL_OTA_EVENTS_H__
#define __OSAL_OTA_EVENTS_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Post the app valid event, if an event base is registered.
 */
void osal_ota_events_post_app_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* __OSAL_OTA_EVENTS_H__ */
