/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file bridge_child_nvs.c
 * @brief Per-child NVS persistence implementation.
 */

#include "bridge/bridge_child_nvs.h"

#ifdef CONFIG_RMNG_BRIDGE_ENABLED

#include "bridge/bridge_internal.h"
#include "constants/nvs.h"

#include "osal_storage.h"
#include "osal_log.h"
#include "osal_semaphore.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "rmng_br_child_nvs";

/* Module mutex serialising read-modify-write cycles across children.
 * NVS itself is internally thread-safe; this mutex protects the
 * load->mutate->store sequence used by the atomic RMW helpers. */
static osal_semaphore_handle_t __mutex = NULL;

/* Key derivation **************************************************************/

/* NVS key buffer (RMAKER_NVS_KEY_LEN_MAX + NUL). */
typedef struct {
    char buf[RMAKER_NVS_KEY_LEN_MAX + 1];
} __nvs_key_t;

static esp_rmaker_error_t __derive_key(esp_rmaker_bridge_child_handle_t child, __nvs_key_t *out)
{
    if (!child || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    const char *nvs_key = bridge_internal_child_nvs_key(child);
    if (!nvs_key || nvs_key[0] == '\0') {
        return ESP_RMAKER_INVALID_ARG;
    }
    /* The slot's nvs_key is already SHA-256(local_id) hex-encoded and
     * NUL-terminated within the cap; just copy it across. */
    size_t n = strlen(nvs_key);
    if (n > RMAKER_NVS_KEY_LEN_MAX) {
        return ESP_RMAKER_INVALID_ARG;
    }
    memcpy(out->buf, nvs_key, n);
    out->buf[n] = '\0';
    return ESP_RMAKER_OK;
}

/* Open / close ****************************************************************/

static esp_rmaker_error_t __open(osal_storage_open_mode_t mode, osal_storage_handle_t *out_h)
{
    osal_err_t err = osal_storage_open(RMAKER_NVS_PART_NAME,
                                       RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE,
                                       mode, out_h);
    if (err != OSAL_ERR_OK) {
        /* Missing namespace is normal before the first child is persisted
         * (e.g. on a fresh boot). Log at debug rather than error. */
        if (err == OSAL_ERR_NVS_NAMESPACE_NOT_FOUND) {
            OSAL_LOGD(TAG, "osal_storage_open(%s): namespace not present yet: %d",
                      RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE, (int)err);
        } else {
            OSAL_LOGE(TAG, "osal_storage_open(%s) failed: %d",
                      RMAKER_NVS_BRIDGE_CHILDREN_NAMESPACE, (int)err);
        }
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Erase an entry under an existing RW handle. Best-effort; logs on failure. */
static void __erase_under_handle(osal_storage_handle_t h, const char *key)
{
    if (osal_storage_erase(h, key) != OSAL_ERR_OK) {
        return;
    }
    (void)osal_storage_commit(h);
}

/* Raw load: returns ESP_RMAKER_OK with a populated record, or
 * ESP_RMAKER_NOT_FOUND on absent/version-mismatch/size-mismatch (and
 * erases the entry on mismatch so a follow-up store starts clean). */
static esp_rmaker_error_t __load_raw(const char *key, bridge_child_nvs_record_t *out)
{
    osal_storage_handle_t h;
    esp_rmaker_error_t err = __open(OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != ESP_RMAKER_OK) {
        return err;
    }

    size_t len = 0;
    osal_err_t nerr = osal_storage_get(h, key, NULL, &len, OSAL_STORAGE_TYPE_BINARY);
    if (nerr != OSAL_ERR_OK) {
        osal_storage_close(h);
        return ESP_RMAKER_NOT_FOUND;
    }
    if (len != sizeof(*out)) {
        OSAL_LOGW(TAG, "Entry for key '%s' has stale size %zu (expected %zu); erasing",
                  key, len, sizeof(*out));
        __erase_under_handle(h, key);
        osal_storage_close(h);
        return ESP_RMAKER_NOT_FOUND;
    }
    len = sizeof(*out);
    nerr = osal_storage_get(h, key, out, &len, OSAL_STORAGE_TYPE_BINARY);
    if (nerr != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "osal_storage_get('%s') failed after sizing: %d", key, (int)nerr);
        osal_storage_close(h);
        return ESP_RMAKER_FAIL;
    }
    if (out->nvs_version != BRIDGE_CHILD_NVS_VERSION) {
        OSAL_LOGW(TAG, "Entry for key '%s' has stale on-disk version %u (expected %u); erasing",
                  key, (unsigned)out->nvs_version, (unsigned)BRIDGE_CHILD_NVS_VERSION);
        __erase_under_handle(h, key);
        osal_storage_close(h);
        return ESP_RMAKER_NOT_FOUND;
    }
    osal_storage_close(h);
    return ESP_RMAKER_OK;
}

/* Raw store. Sets ``nvs_version`` before persisting. */
static esp_rmaker_error_t __store_raw(const char *key, const bridge_child_nvs_record_t *in)
{
    osal_storage_handle_t h;
    esp_rmaker_error_t err = __open(OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    bridge_child_nvs_record_t copy = *in;
    copy.nvs_version = BRIDGE_CHILD_NVS_VERSION;
    osal_err_t nerr = osal_storage_set(h, key, &copy, sizeof(copy), OSAL_STORAGE_TYPE_BINARY);
    if (nerr != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "osal_storage_set('%s') failed: %d", key, (int)nerr);
        osal_storage_close(h);
        return ESP_RMAKER_FAIL;
    }
    nerr = osal_storage_commit(h);
    osal_storage_close(h);
    if (nerr != OSAL_ERR_OK) {
        OSAL_LOGE(TAG, "osal_storage_commit('%s') failed: %d", key, (int)nerr);
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

/* Initialise a fresh in-memory record with unset sentinels. */
static void __record_init(bridge_child_nvs_record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->nvs_version = BRIDGE_CHILD_NVS_VERSION;
    r->sched_ver = -1;
    r->trigger_ver = -1;
    r->ncfg_checksum_set = 0;
}

/* Public ***********************************************************************/

esp_rmaker_error_t bridge_child_nvs_init(void)
{
    if (__mutex) {
        return ESP_RMAKER_OK;
    }
    __mutex = osal_semaphore_create_mutex();
    if (!__mutex) {
        OSAL_LOGE(TAG, "Failed to create mutex");
        return ESP_RMAKER_FAIL;
    }
    return ESP_RMAKER_OK;
}

static void __lock(void)
{
    if (__mutex) {
        osal_semaphore_take(__mutex, OSAL_MAX_DELAY);
    }
}

static void __unlock(void)
{
    if (__mutex) {
        osal_semaphore_give(__mutex);
    }
}

esp_rmaker_error_t bridge_child_nvs_load(esp_rmaker_bridge_child_handle_t child, bridge_child_nvs_record_t *out)
{
    if (!out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    err = __load_raw(key.buf, out);
    __unlock();
    return err;
}

esp_rmaker_error_t bridge_child_nvs_store(esp_rmaker_bridge_child_handle_t child, const bridge_child_nvs_record_t *in)
{
    if (!in) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    err = __store_raw(key.buf, in);
    __unlock();
    return err;
}

esp_rmaker_error_t bridge_child_nvs_erase(esp_rmaker_bridge_child_handle_t child)
{
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    osal_storage_handle_t h;
    err = __open(OSAL_STORAGE_OPEN_READWRITE, &h);
    if (err != ESP_RMAKER_OK) {
        __unlock();
        return err;
    }
    __erase_under_handle(h, key.buf);
    osal_storage_close(h);
    __unlock();
    return ESP_RMAKER_OK;
}

/* Atomic RMW helpers. ``ESP_RMAKER_NOT_FOUND`` from load -> start from
 * a freshly-initialised record. */

esp_rmaker_error_t bridge_child_nvs_set_sched_ver(esp_rmaker_bridge_child_handle_t child, int32_t sched_ver)
{
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    bridge_child_nvs_record_t r;
    esp_rmaker_error_t load_err = __load_raw(key.buf, &r);
    if (load_err == ESP_RMAKER_NOT_FOUND) {
        __record_init(&r);
    } else if (load_err != ESP_RMAKER_OK) {
        __unlock();
        return load_err;
    }
    r.sched_ver = sched_ver;
    err = __store_raw(key.buf, &r);
    __unlock();
    return err;
}

esp_rmaker_error_t bridge_child_nvs_set_trigger_ver(esp_rmaker_bridge_child_handle_t child, int32_t trigger_ver)
{
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    bridge_child_nvs_record_t r;
    esp_rmaker_error_t load_err = __load_raw(key.buf, &r);
    if (load_err == ESP_RMAKER_NOT_FOUND) {
        __record_init(&r);
    } else if (load_err != ESP_RMAKER_OK) {
        __unlock();
        return load_err;
    }
    r.trigger_ver = trigger_ver;
    err = __store_raw(key.buf, &r);
    __unlock();
    return err;
}

esp_rmaker_error_t bridge_child_nvs_set_node_config(esp_rmaker_bridge_child_handle_t child,
        const uint8_t checksum[BRIDGE_CHILD_NCFG_CHECKSUM_LEN])
{
    if (!checksum) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    bridge_child_nvs_record_t r;
    esp_rmaker_error_t load_err = __load_raw(key.buf, &r);
    if (load_err == ESP_RMAKER_NOT_FOUND) {
        __record_init(&r);
    } else if (load_err != ESP_RMAKER_OK) {
        __unlock();
        return load_err;
    }
    memcpy(r.ncfg_checksum, checksum, BRIDGE_CHILD_NCFG_CHECKSUM_LEN);
    r.ncfg_checksum_set = 1;
    err = __store_raw(key.buf, &r);
    __unlock();
    return err;
}

esp_rmaker_error_t bridge_child_nvs_set_node_tags(esp_rmaker_bridge_child_handle_t child,
        const uint8_t checksum[BRIDGE_CHILD_NCFG_CHECKSUM_LEN])
{
    if (!checksum) {
        return ESP_RMAKER_INVALID_ARG;
    }
    __nvs_key_t key;
    esp_rmaker_error_t err = __derive_key(child, &key);
    if (err != ESP_RMAKER_OK) {
        return err;
    }
    __lock();
    bridge_child_nvs_record_t r;
    esp_rmaker_error_t load_err = __load_raw(key.buf, &r);
    if (load_err == ESP_RMAKER_NOT_FOUND) {
        __record_init(&r);
    } else if (load_err != ESP_RMAKER_OK) {
        __unlock();
        return load_err;
    }
    memcpy(r.tags_checksum, checksum, BRIDGE_CHILD_NCFG_CHECKSUM_LEN);
    r.tags_checksum_set = 1;
    err = __store_raw(key.buf, &r);
    __unlock();
    return err;
}

#endif /* CONFIG_RMNG_BRIDGE_ENABLED */
