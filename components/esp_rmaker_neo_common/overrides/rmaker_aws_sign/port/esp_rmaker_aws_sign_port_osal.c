/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_aws_sign_port_osal.c
 * @brief osal-backed port for the rmaker_aws_sign component.
 */

#include "esp_rmaker_aws_sign_port_osal.h"
#include "esp_rmaker_aws_sign_port_osal_priv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <time.h>

#include "esp_rmaker_aws_sign_esp_port.h"

#include "osal_log.h"
#include "osal_mem_alloc.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"
#include "osal_time.h"
#include "osal_timesync.h"

/* Memory ops *******************************************************************/

/* osal's allocators are macros, so they cannot be assigned to function pointers. */
static void *__mem_malloc(size_t size)
{
    return OSAL_MALLOC_EXTRAM(size);
}

static void *__mem_calloc(size_t num, size_t size)
{
    return OSAL_CALLOC_EXTRAM(num, size);
}

/* Time ops *********************************************************************/

/* Through osal, not time(): the virtual scheduler retargets ``osal_get_time``. */
static time_t __get_time(time_t *p_time)
{
    return osal_get_time(p_time);
}

static bool __is_synced(void)
{
    return osal_timesync_is_synced();
}

/* Mutex ops ********************************************************************/

static esp_rmaker_aws_sign_mutex_t __mutex_create(void)
{
    return (esp_rmaker_aws_sign_mutex_t) osal_semaphore_create_mutex();
}

static void __mutex_lock(esp_rmaker_aws_sign_mutex_t m)
{
    osal_semaphore_take((osal_semaphore_handle_t) m, OSAL_MAX_DELAY);
}

static void __mutex_unlock(esp_rmaker_aws_sign_mutex_t m)
{
    osal_semaphore_give((osal_semaphore_handle_t) m);
}

static void __mutex_destroy(esp_rmaker_aws_sign_mutex_t m)
{
    osal_semaphore_delete((osal_semaphore_handle_t) m);
}

/* Log ops **********************************************************************/

/* Not ``__log``: glibc declares that as the internal alias for ``log()``. */
static void __port_log(esp_rmaker_aws_sign_log_level_t level, const char *tag, const char *message)
{
    switch (level) {
    case ESP_RMAKER_AWS_SIGN_LOG_ERROR:
        OSAL_LOGE(tag, "%s", message);
        break;
    case ESP_RMAKER_AWS_SIGN_LOG_WARN:
        OSAL_LOGW(tag, "%s", message);
        break;
    case ESP_RMAKER_AWS_SIGN_LOG_INFO:
        OSAL_LOGI(tag, "%s", message);
        break;
    default:
        OSAL_LOGD(tag, "%s", message);
        break;
    }
}

/* Port table *******************************************************************/

const esp_rmaker_aws_sign_port_config_t *esp_rmaker_aws_sign_port_osal_get(void)
{
    /* Not const: the crypto and JSON tables are extern objects, not constant
     * expressions, so they are copied in on the first call. */
    static esp_rmaker_aws_sign_port_config_t port = {
        .mem = {
            .malloc = __mem_malloc,
            .calloc = __mem_calloc,
            /* heap_caps allocations are released with plain free(). */
            .free = free,
        },
        .time = {
            .get_time = __get_time,
            .is_synced = __is_synced,
        },
        .mutex = {
            .create = __mutex_create,
            .lock = __mutex_lock,
            .unlock = __mutex_unlock,
            .destroy = __mutex_destroy,
        },
        .log = __port_log,
    };
    static bool initialized = false;

    /* Idempotent: tests fetch the table on every setup. A failed setup is tried again on the next call. */
    if (!initialized) {
        if (esp_rmaker_aws_sign_port_osal_http_setup() != ESP_OK) {
            return NULL;
        }
        /* PSA-backed and nothing else, so the component's own table is taken as-is. */
        port.crypto = esp_rmaker_aws_sign_esp_crypto_ops;
        port.json = esp_rmaker_aws_sign_port_osal_json_ops;
        port.http = esp_rmaker_aws_sign_port_osal_http_ops;
        initialized = true;
    }
    return &port;
}
