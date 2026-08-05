/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file manager_internal.h
 * @brief Internal entry points into the cloud-manager dispatch path,
 *        intended for cross-module use within the RainMaker Neo core (today: the
 *        bridge subscriber, which receives child from_cloud payloads on
 *        filter #5 and needs to drive the same per-event dispatch as
 *        the self path).
 */

#ifndef __CLOUD_MANAGER_INTERNAL_H__
#define __CLOUD_MANAGER_INTERNAL_H__

#include <stddef.h>

#include "network/mqtt_topics.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Synchronously dispatch a cloud payload to the per-event handlers,
 *        scoping all state mutations and follow-up publishes to ``ctx``.
 *
 * Self path: ``ctx == &esp_rmaker_topic_ctx_self`` (also accepts NULL as
 *            a shorthand for self).
 * Child path: ``ctx`` is the stable per-child topic ctx from the bridge slot.
 *
 * This is the same body as the cloud manager's own work-queue task,
 * exposed for the bridge subscriber filter-#5 dispatch.
 */
void cloud_manager_internal_dispatch_payload(const esp_rmaker_topic_ctx_t *ctx,
        const char *payload_str,
        size_t payload_len);

#ifdef __cplusplus
}
#endif

#endif /* __CLOUD_MANAGER_INTERNAL_H__ */
