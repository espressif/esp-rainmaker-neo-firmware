/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>

#include "osal_storage.h"

/* Storage layout: sequence of variable-length records
 * [u16 key_len][u32 val_len][u8 type][key bytes][val bytes]
 * The latest record for a key is the authoritative value.
 */

typedef struct {
    char *key;
    uint16_t key_len;
    uint8_t *value;
    uint32_t value_len;
    osal_storage_type_t type;
} nvs_posix_record_t;

typedef struct {
    int fd;
    char path[512];
} nvs_posix_handle_t;

typedef struct {
    int fd;
    char path[512];
    off_t current_offset;
    bool valid;
    osal_storage_type_t requested_type;
} nvs_posix_iterator_t;

static const char *k_base_dir = "nvs_persistent";
static const char *k_base_dir_default = "nvs";
static pthread_mutex_t g_nvs_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Per-partition init state: deinit on one label must not drop another (e.g. factory vs main). */
#define NVS_POSIX_PARTITION_SLOTS 16
typedef struct {
    char label[128];
    bool inited;
} nvs_posix_partition_slot_t;

static nvs_posix_partition_slot_t g_partition_slots[NVS_POSIX_PARTITION_SLOTS];

static const char *normalize_partition_name(const char *partition_name)
{
    return (partition_name && partition_name[0] != '\0') ? partition_name : k_base_dir_default;
}

/**
 * @return slot index, or -1 if the table is full and @a norm is not present.
 */
static int find_or_allocate_slot_locked(const char *norm)
{
    int first_empty = -1;

    for (int i = 0; i < NVS_POSIX_PARTITION_SLOTS; i++) {
        if (g_partition_slots[i].label[0] == '\0') {
            if (first_empty < 0) {
                first_empty = i;
            }
            continue;
        }
        if (strcmp(g_partition_slots[i].label, norm) == 0) {
            return i;
        }
    }
    return first_empty;
}

static bool partition_inited_locked(const char *norm)
{
    for (int i = 0; i < NVS_POSIX_PARTITION_SLOTS; i++) {
        if (g_partition_slots[i].label[0] != '\0' && strcmp(g_partition_slots[i].label, norm) == 0) {
            return g_partition_slots[i].inited;
        }
    }
    return false;
}

static osal_err_t ensure_base_dir(void)
{
    struct stat st;
    if (stat(k_base_dir, &st) == -1) {
        if (mkdir(k_base_dir, 0700) != 0) {
            return OSAL_ERR_FAIL;
        }
    }
    return OSAL_ERR_OK;
}

static void build_path(char *out, size_t out_size, const char *partition, const char *name_space)
{
    assert(name_space && name_space[0] != '\0');
    snprintf(out, out_size, "%s/%s-%s.bin", k_base_dir, partition ? partition : k_base_dir_default, name_space);
}

static osal_err_t read_next_record(int fd, nvs_posix_record_t *record)
{
    uint16_t klen;
    uint32_t vlen;
    uint8_t type_byte;
    ssize_t r;

    r = read(fd, &klen, sizeof(klen));
    if (r == 0) {
        return OSAL_ERR_NVS_KEY_NOT_FOUND; /* EOF */
    }
    if (r != (ssize_t)sizeof(klen)) {
        return OSAL_ERR_FAIL;
    }

    r = read(fd, &vlen, sizeof(vlen));
    if (r != (ssize_t)sizeof(vlen)) {
        return OSAL_ERR_FAIL;
    }

    r = read(fd, &type_byte, sizeof(type_byte));
    if (r != (ssize_t)sizeof(type_byte)) {
        return OSAL_ERR_FAIL;
    }

    if (klen == 0 || klen >= OSAL_STORAGE_KEY_MAX_LENGTH) {
        return OSAL_ERR_FAIL;
    }

    char *k = (char *)malloc(klen);
    if (!k) {
        return OSAL_ERR_NO_MEM;
    }
    r = read(fd, k, klen);
    if (r != (ssize_t)klen) {
        free(k);
        return OSAL_ERR_FAIL;
    }

    uint8_t *v = NULL;
    if (vlen > 0) {
        v = (uint8_t *)malloc(vlen);
        if (!v) {
            free(k);
            return OSAL_ERR_NO_MEM;
        }
        r = read(fd, v, vlen);
        if (r != (ssize_t)vlen) {
            free(k);
            free(v);
            return OSAL_ERR_FAIL;
        }
    }

    record->key = k;
    record->key_len = klen;
    record->value = v;
    record->value_len = vlen;
    record->type = (osal_storage_type_t)type_byte;
    return OSAL_ERR_OK;
}

static osal_err_t write_record(int fd, const nvs_posix_record_t *record)
{
    uint16_t klen = record->key_len;
    uint32_t vlen = record->value_len;
    uint8_t type_byte = (uint8_t)record->type;

    if (write(fd, &klen, sizeof(klen)) != (ssize_t)sizeof(klen)) {
        return OSAL_ERR_FAIL;
    }
    if (write(fd, &vlen, sizeof(vlen)) != (ssize_t)sizeof(vlen)) {
        return OSAL_ERR_FAIL;
    }
    if (write(fd, &type_byte, sizeof(type_byte)) != (ssize_t)sizeof(type_byte)) {
        return OSAL_ERR_FAIL;
    }
    if (write(fd, record->key, klen) != (ssize_t)klen) {
        return OSAL_ERR_FAIL;
    }
    if (vlen > 0 && write(fd, record->value, vlen) != (ssize_t)vlen) {
        return OSAL_ERR_FAIL;
    }
    return OSAL_ERR_OK;
}


osal_err_t osal_storage_init(char *partition_label)
{
    const char *norm = normalize_partition_name(partition_label);

    pthread_mutex_lock(&g_nvs_mutex);
    int idx = find_or_allocate_slot_locked(norm);
    if (idx < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    bool assigned_label = false;
    if (g_partition_slots[idx].label[0] == '\0') {
        strncpy(g_partition_slots[idx].label, norm, sizeof(g_partition_slots[idx].label) - 1);
        g_partition_slots[idx].label[sizeof(g_partition_slots[idx].label) - 1] = '\0';
        assigned_label = true;
    }
    if (!g_partition_slots[idx].inited) {
        osal_err_t rc = ensure_base_dir();
        if (rc != OSAL_ERR_OK) {
            if (assigned_label) {
                g_partition_slots[idx].label[0] = '\0';
            }
            pthread_mutex_unlock(&g_nvs_mutex);
            return rc;
        }
        g_partition_slots[idx].inited = true;
    }
    pthread_mutex_unlock(&g_nvs_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_deinit(char *partition_label)
{
    const char *norm = normalize_partition_name(partition_label);

    pthread_mutex_lock(&g_nvs_mutex);
    for (int i = 0; i < NVS_POSIX_PARTITION_SLOTS; i++) {
        if (g_partition_slots[i].label[0] != '\0' && strcmp(g_partition_slots[i].label, norm) == 0) {
            if (!g_partition_slots[i].inited) {
                pthread_mutex_unlock(&g_nvs_mutex);
                return OSAL_ERR_NVS_NOT_INITIALIZED;
            }
            g_partition_slots[i].inited = false;
            pthread_mutex_unlock(&g_nvs_mutex);
            return OSAL_ERR_OK;
        }
    }
    pthread_mutex_unlock(&g_nvs_mutex);
    /* No slot: align with ESP-IDF nvs_flash_deinit_partition (already not initialized). */
    return OSAL_ERR_NVS_NOT_INITIALIZED;
}

osal_err_t osal_storage_reset(char *partition_label)
{
    const char *label = (partition_label && partition_label[0] != '\0') ? partition_label : k_base_dir_default;

    pthread_mutex_lock(&g_nvs_mutex);

    osal_err_t rc = ensure_base_dir();
    if (rc != OSAL_ERR_OK) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return rc;
    }

    DIR *dir = opendir(k_base_dir);
    if (!dir) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    size_t label_len = strlen(label);
    const char *suffix = ".bin";
    size_t suffix_len = 4;
    int had_error = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;

        if (name[0] == '.') {
            continue; /* skip hidden/parent/current */
        }

        size_t name_len = strlen(name);
        if (name_len < label_len + 1 + suffix_len) {
            continue; /* too short to match "<label>-*.bin" */
        }
        if (strncmp(name, label, label_len) != 0) {
            continue; /* different partition */
        }
        if (name[label_len] != '-') {
            continue; /* require separator after label */
        }
        if (strcmp(name + (name_len - suffix_len), suffix) != 0) {
            continue; /* must end with .bin */
        }

        char full_path[600];
        snprintf(full_path, sizeof(full_path), "%s/%s", k_base_dir, name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            if (unlink(full_path) != 0) {
                had_error = 1; /* record failure but continue best-effort */
            }
        }
    }

    closedir(dir);
    for (int i = 0; i < NVS_POSIX_PARTITION_SLOTS; i++) {
        if (g_partition_slots[i].label[0] != '\0' && strcmp(g_partition_slots[i].label, label) == 0) {
            g_partition_slots[i].inited = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_nvs_mutex);
    return had_error ? OSAL_ERR_FAIL : OSAL_ERR_OK;
}

osal_err_t osal_storage_open(const char *partition_name, const char *name_space, osal_storage_open_mode_t mode, osal_storage_handle_t *p_handle)
{
    if (!p_handle || !name_space) {
        return OSAL_ERR_INVALID_ARG;
    }
    const char *norm = normalize_partition_name(partition_name);
    pthread_mutex_lock(&g_nvs_mutex);
    bool inited = partition_inited_locked(norm);
    pthread_mutex_unlock(&g_nvs_mutex);
    /* Match ESP-IDF: nvs_open_from_partition uses ESP_ERR_NVS_PART_NOT_FOUND -> PARTITION_NOT_FOUND. */
    if (!inited) {
        return OSAL_ERR_NVS_PARTITION_NOT_FOUND;
    }

    nvs_posix_handle_t *h = (nvs_posix_handle_t *)calloc(1, sizeof(nvs_posix_handle_t));
    if (!h) {
        return OSAL_ERR_NO_MEM;
    }

    build_path(h->path, sizeof(h->path), partition_name, name_space);

    osal_err_t rc = ensure_base_dir();
    if (rc != OSAL_ERR_OK) {
        free(h);
        return rc;
    }

    /* Open with requested mode. Create only for read-write. */
    int flags = (mode == OSAL_STORAGE_OPEN_READONLY) ? O_RDONLY : (O_RDWR | O_CREAT);
    h->fd = open(h->path, flags, 0644);
    if (h->fd < 0) {
        /* Map read-only failure due to non-existent file to namespace not found */
        if (mode == OSAL_STORAGE_OPEN_READONLY && errno == ENOENT) {
            free(h);
            return OSAL_ERR_NVS_NAMESPACE_NOT_FOUND;
        }
        free(h);
        return OSAL_ERR_FAIL;
    }

    *p_handle = (osal_storage_handle_t)h;
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_close(osal_storage_handle_t handle)
{
    if (!handle) {
        return OSAL_ERR_INVALID_ARG;
    }
    nvs_posix_handle_t *h = (nvs_posix_handle_t *)handle;
    if (h->fd >= 0) {
        fsync(h->fd);
        close(h->fd);
        h->fd = -1;
    }
    free(h);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_get(osal_storage_handle_t handle, const char *key, void *value, size_t *p_value_len, osal_storage_type_t type)
{
    /* type is not used for validation for now */
    (void)type;

    if (!handle || !key || !p_value_len) {
        return OSAL_ERR_INVALID_ARG;
    }
    nvs_posix_handle_t *h = (nvs_posix_handle_t *)handle;

    pthread_mutex_lock(&g_nvs_mutex);
    if (lseek(h->fd, 0, SEEK_SET) < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    /* Track the last occurrence; presence must be independent of value length */
    uint8_t *last_val = NULL;
    uint32_t last_len = 0;
    bool found = false;

    for (;;) {
        nvs_posix_record_t record;
        memset(&record, 0, sizeof(record));
        osal_err_t rc = read_next_record(h->fd, &record);
        if (rc == OSAL_ERR_NVS_KEY_NOT_FOUND) {
            break; /* EOF */
        }
        if (rc != OSAL_ERR_OK) {
            free(record.key);
            free(record.value);
            free(last_val);
            pthread_mutex_unlock(&g_nvs_mutex);
            return rc;
        }

        if (record.key && record.key_len == (uint16_t)strlen(key) && memcmp(record.key, key, record.key_len) == 0) {
            found = true;
            free(last_val);
            last_val = record.value;
            last_len = record.value_len;
            record.value = NULL; /* Don't free this, we're using it */
        } else {
            free(record.value);
        }
        free(record.key);
    }

    if (!found) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_NVS_KEY_NOT_FOUND;
    }

    if (!value) {
        *p_value_len = last_len;
        free(last_val);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_OK;
    }

    if (*p_value_len < last_len) {
        *p_value_len = last_len;
        free(last_val);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_NO_MEM;
    }

    if (last_len > 0) {
        memcpy(value, last_val, last_len);
    }
    *p_value_len = last_len;
    free(last_val);
    pthread_mutex_unlock(&g_nvs_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_set(osal_storage_handle_t handle, const char *key, const void *value, size_t value_len, osal_storage_type_t type)
{
    if (!handle || !key || (!value && value_len > 0)) {
        return OSAL_ERR_INVALID_ARG;
    }
    nvs_posix_handle_t *h = (nvs_posix_handle_t *)handle;

    pthread_mutex_lock(&g_nvs_mutex);

    /* Rewrite file excluding existing key, then append new record */
    char tmp_path[600];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", h->path);
    int tfd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (tfd < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    if (lseek(h->fd, 0, SEEK_SET) < 0) {
        close(tfd);
        unlink(tmp_path);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    for (;;) {
        nvs_posix_record_t record;
        memset(&record, 0, sizeof(record));
        osal_err_t rc = read_next_record(h->fd, &record);
        if (rc == OSAL_ERR_NVS_KEY_NOT_FOUND) {
            break;
        }
        if (rc != OSAL_ERR_OK) {
            free(record.key);
            free(record.value);
            close(tfd);
            unlink(tmp_path);
            pthread_mutex_unlock(&g_nvs_mutex);
            return rc;
        }
        if (!(record.key && record.key_len == (uint16_t)strlen(key) && memcmp(record.key, key, record.key_len) == 0)) {
            if (write_record(tfd, &record) != OSAL_ERR_OK) {
                free(record.key);
                free(record.value);
                close(tfd);
                unlink(tmp_path);
                pthread_mutex_unlock(&g_nvs_mutex);
                return OSAL_ERR_FAIL;
            }
        }
        free(record.key);
        free(record.value);
    }

    nvs_posix_record_t new_record;
    new_record.key = (char *)key;
    new_record.key_len = (uint16_t)strlen(key);
    new_record.value = (uint8_t *)value;
    new_record.value_len = (uint32_t)value_len;
    new_record.type = type;

    if (write_record(tfd, &new_record) != OSAL_ERR_OK) {
        close(tfd);
        unlink(tmp_path);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    if (fsync(tfd) != 0) {
        close(tfd);
        unlink(tmp_path);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    close(tfd);

    /* Replace */
    close(h->fd);
    if (rename(tmp_path, h->path) != 0) {
        /* Best-effort: reopen original even if rename failed */
        h->fd = open(h->path, O_RDWR | O_CREAT, 0644);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    h->fd = open(h->path, O_RDWR | O_CREAT, 0644);
    if (h->fd < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    pthread_mutex_unlock(&g_nvs_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_erase(osal_storage_handle_t handle, const char *key)
{
    if (!handle || !key) {
        return OSAL_ERR_INVALID_ARG;
    }
    nvs_posix_handle_t *h = (nvs_posix_handle_t *)handle;

    pthread_mutex_lock(&g_nvs_mutex);

    char tmp_path[600];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", h->path);
    int tfd = open(tmp_path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (tfd < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    if (lseek(h->fd, 0, SEEK_SET) < 0) {
        close(tfd);
        unlink(tmp_path);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    bool found = false;
    for (;;) {
        nvs_posix_record_t record;
        memset(&record, 0, sizeof(record));
        osal_err_t rc = read_next_record(h->fd, &record);
        if (rc == OSAL_ERR_NVS_KEY_NOT_FOUND) {
            break;
        }
        if (rc != OSAL_ERR_OK) {
            free(record.key);
            free(record.value);
            close(tfd);
            unlink(tmp_path);
            pthread_mutex_unlock(&g_nvs_mutex);
            return rc;
        }
        if (record.key && record.key_len == (uint16_t)strlen(key) && memcmp(record.key, key, record.key_len) == 0) {
            found = true;
        } else {
            if (write_record(tfd, &record) != OSAL_ERR_OK) {
                free(record.key);
                free(record.value);
                close(tfd);
                unlink(tmp_path);
                pthread_mutex_unlock(&g_nvs_mutex);
                return OSAL_ERR_FAIL;
            }
        }
        free(record.key);
        free(record.value);
    }

    if (fsync(tfd) != 0) {
        close(tfd);
        unlink(tmp_path);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    close(tfd);

    close(h->fd);
    if (rename(tmp_path, h->path) != 0) {
        h->fd = open(h->path, O_RDWR | O_CREAT, 0644);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    h->fd = open(h->path, O_RDWR | O_CREAT, 0644);
    if (h->fd < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    pthread_mutex_unlock(&g_nvs_mutex);
    return found ? OSAL_ERR_OK : OSAL_ERR_NVS_KEY_NOT_FOUND;
}

osal_err_t osal_storage_erase_all(osal_storage_handle_t handle)
{
    if (!handle) {
        return OSAL_ERR_INVALID_ARG;
    }
    nvs_posix_handle_t *h = (nvs_posix_handle_t *)handle;

    pthread_mutex_lock(&g_nvs_mutex);
    if (ftruncate(h->fd, 0) != 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    if (lseek(h->fd, 0, SEEK_SET) < 0) {
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }
    pthread_mutex_unlock(&g_nvs_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_commit(osal_storage_handle_t handle)
{
    if (!handle) {
        return OSAL_ERR_INVALID_ARG;
    }
    nvs_posix_handle_t *h = (nvs_posix_handle_t *)handle;
    return (fsync(h->fd) == 0) ? OSAL_ERR_OK : OSAL_ERR_FAIL;
}

osal_err_t osal_storage_entry_find(const char *partition_name, const char *name_space, osal_storage_type_t type, osal_storage_iterator_t *iterator)
{
    if (!iterator || !name_space) {
        return OSAL_ERR_INVALID_ARG;
    }
    const char *norm = normalize_partition_name(partition_name);
    pthread_mutex_lock(&g_nvs_mutex);
    bool inited = partition_inited_locked(norm);
    pthread_mutex_unlock(&g_nvs_mutex);
    /* Match ESP-IDF nvs_entry_find: ESP_ERR_NVS_PART_NOT_FOUND -> PARTITION_NOT_FOUND. */
    if (!inited) {
        return OSAL_ERR_NVS_PARTITION_NOT_FOUND;
    }

    nvs_posix_iterator_t *iter = (nvs_posix_iterator_t *)calloc(1, sizeof(nvs_posix_iterator_t));
    if (!iter) {
        return OSAL_ERR_NO_MEM;
    }

    build_path(iter->path, sizeof(iter->path), partition_name, name_space);

    osal_err_t rc;
    rc = ensure_base_dir();
    if (rc != OSAL_ERR_OK) {
        free(iter);
        return rc;
    }

    /* Open file for reading */
    iter->fd = open(iter->path, O_RDONLY);
    if (iter->fd < 0) {
        if (errno == ENOENT) {
            free(iter);
            return OSAL_ERR_NVS_NAMESPACE_NOT_FOUND;
        }
        free(iter);
        return OSAL_ERR_FAIL;
    }

    /* Start from the beginning */
    iter->current_offset = 0;
    iter->valid = true;
    iter->requested_type = type;

    /* Position iterator at the first matching entry */
    rc = osal_storage_entry_next((osal_storage_iterator_t *) &iter);
    if (rc != OSAL_ERR_OK && rc != OSAL_ERR_NVS_KEY_NOT_FOUND) {
        free(iter);
        return rc;
    }

    *iterator = (osal_storage_iterator_t)iter;
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_entry_get_info(osal_storage_iterator_t iterator, osal_storage_entry_t *p_entry)
{
    if (!iterator || !p_entry) {
        return OSAL_ERR_INVALID_ARG;
    }

    nvs_posix_iterator_t *iter = (nvs_posix_iterator_t *)iterator;
    if (!iter->valid || iter->fd < 0) {
        return OSAL_ERR_FAIL;
    }

    pthread_mutex_lock(&g_nvs_mutex);

    /* Read the record at the current position */
    nvs_posix_record_t record;
    memset(&record, 0, sizeof(record));
    osal_err_t rc = read_next_record(iter->fd, &record);

    if (rc != OSAL_ERR_OK) {
        free(record.key);
        free(record.value);
        pthread_mutex_unlock(&g_nvs_mutex);
        return rc;
    }

    /* Copy the key and return it */
    size_t key_len = record.key_len;
    if (key_len >= OSAL_STORAGE_KEY_MAX_LENGTH) {
        key_len = OSAL_STORAGE_KEY_MAX_LENGTH - 1; /* Leave space for null terminator */
    }
    memcpy(p_entry->key, record.key, key_len);
    p_entry->key[key_len] = '\0'; /* Null terminate */

    /* Update current offset for next iteration */
    iter->current_offset = lseek(iter->fd, 0, SEEK_CUR);
    if (iter->current_offset < 0) {
        free(record.key);
        free(record.value);
        pthread_mutex_unlock(&g_nvs_mutex);
        return OSAL_ERR_FAIL;
    }

    /* Clean up record */
    free(record.key);
    free(record.value);

    pthread_mutex_unlock(&g_nvs_mutex);
    return OSAL_ERR_OK;
}

osal_err_t osal_storage_entry_next(osal_storage_iterator_t *iterator)
{
    if (!iterator || *iterator == NULL) {
        return OSAL_ERR_INVALID_ARG;
    }

    nvs_posix_iterator_t *iter = (nvs_posix_iterator_t *)*iterator;
    if (!iter->valid || iter->fd < 0) {
        return OSAL_ERR_FAIL;
    }

    pthread_mutex_lock(&g_nvs_mutex);

    /* Loop until we find an entry that matches the requested type or reach EOF */
    for (;;) {
        nvs_posix_record_t record;
        memset(&record, 0, sizeof(record));
        osal_err_t rc = read_next_record(iter->fd, &record);

        if (rc == OSAL_ERR_NVS_KEY_NOT_FOUND) {
            /* EOF - no more entries */
            pthread_mutex_unlock(&g_nvs_mutex);
            return OSAL_ERR_NVS_KEY_NOT_FOUND;
        }

        if (rc != OSAL_ERR_OK) {
            free(record.key);
            free(record.value);
            pthread_mutex_unlock(&g_nvs_mutex);
            return rc;
        }

        /* Check if this record matches the requested type */
        if (record.type == iter->requested_type) {
            /* This entry matches, rewind to the beginning of this record for get_info to read */
            off_t current_pos = lseek(iter->fd, 0, SEEK_CUR);
            if (current_pos < 0) {
                free(record.key);
                free(record.value);
                pthread_mutex_unlock(&g_nvs_mutex);
                return OSAL_ERR_FAIL;
            }

            /* Calculate the size of this record to rewind */
            size_t record_size = sizeof(uint16_t) + sizeof(uint32_t) + sizeof(uint8_t) + record.key_len + record.value_len;
            off_t record_start = current_pos - record_size;

            if (lseek(iter->fd, record_start, SEEK_SET) < 0) {
                free(record.key);
                free(record.value);
                pthread_mutex_unlock(&g_nvs_mutex);
                return OSAL_ERR_FAIL;
            }

            /* Update current offset */
            iter->current_offset = record_start;

            /* Clean up record */
            free(record.key);
            free(record.value);

            pthread_mutex_unlock(&g_nvs_mutex);
            return OSAL_ERR_OK;
        } else {
            /* This entry doesn't match the requested type, skip it */
            free(record.key);
            free(record.value);
        }
    }
}

osal_err_t osal_storage_release_iterator(osal_storage_iterator_t iterator)
{
    if (!iterator) {
        return OSAL_ERR_INVALID_ARG;
    }

    nvs_posix_iterator_t *iter = (nvs_posix_iterator_t *)iterator;
    if (iter->fd >= 0) {
        close(iter->fd);
        iter->fd = -1;
    }
    iter->valid = false;
    free(iter);
    return OSAL_ERR_OK;
}
