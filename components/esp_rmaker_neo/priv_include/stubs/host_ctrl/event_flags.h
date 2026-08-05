/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file event_flags.h
 * @brief This is a stub file for the event flags. If remote control is enabled,
 *        this file will be replaced with the actual implementation.
 */

#ifndef __ESP_RMAKER_EVENT_FLAGS_H__
#define __ESP_RMAKER_EVENT_FLAGS_H__

#define esp_rmaker_event_flags_set_online()                         do {} while (0)
#define esp_rmaker_event_flags_set_started()                        do {} while (0)
#define esp_rmaker_event_flags_set_stopped()                        do {} while (0)
#define esp_rmaker_event_flags_set_state_reported()                 do {} while (0)
#define esp_rmaker_event_flags_set_timeseries_reported()            do {} while (0)
#define esp_rmaker_event_flags_set_node_config_sent()               do {} while (0)
#define esp_rmaker_event_flags_set_notification_sent()              do {} while (0)
#define esp_rmaker_event_flags_set_state_started_listening()        do {} while (0)
#define esp_rmaker_event_flags_set_group_info_received()            do {} while (0)
#define esp_rmaker_event_flags_set_alexa_enabled_received()         do {} while (0)
#define esp_rmaker_event_flags_set_gva_enabled_received()           do {} while (0)
#define esp_rmaker_event_flags_set_sched_version_received()         do {} while (0)
#define esp_rmaker_event_flags_set_sched_details_received()         do {} while (0)
#define esp_rmaker_event_flags_set_trigger_version_received()       do {} while (0)
#define esp_rmaker_event_flags_set_trigger_details_received()       do {} while (0)

#define esp_rmaker_event_flags_clear_online()                       do {} while (0)
#define esp_rmaker_event_flags_clear_all()                          do {} while (0)
#define esp_rmaker_event_flags_clear_group_info_received()          do {} while (0)
#define esp_rmaker_event_flags_clear_alexa_enabled_received()       do {} while (0)
#define esp_rmaker_event_flags_clear_gva_enabled_received()         do {} while (0)
#define esp_rmaker_event_flags_clear_sched_version_received()       do {} while (0)
#define esp_rmaker_event_flags_clear_trigger_version_received()     do {} while (0)

#endif /* __ESP_RMAKER_EVENT_FLAGS_H__ */
