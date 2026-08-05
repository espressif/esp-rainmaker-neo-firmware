/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_trigger_codec.c
 * @brief Compact binary codec for automation trigger details.
 *        See ::util/esp_rmaker_trigger_codec.h for the wire layout.
 */

#include "util/esp_rmaker_trigger_codec.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "json_parser.h"
#include "osal_log.h"
#include "osal_mem_alloc.h"

static const char *TAG = "rmng_trigger_codec";

#define FLAG_ENABLED   0x01u
#define FLAG_VT_SHIFT  1
#define FLAG_VT_MASK   0x0Eu   /* bits 1-3 */

/* Operator names indexed by RMAKER_TRIGGER_OP_*. Stable order (persisted). */
static const char *const OP_NAMES[] = { "eq", "ne", "gt", "lt", "ge", "le" };
#define OP_COUNT ((uint8_t)(sizeof(OP_NAMES) / sizeof(OP_NAMES[0])))

/* ---- growable byte buffer (encode) ---- */

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
    bool     oom;
} bytebuf_t;

static void bb_reserve(bytebuf_t *b, size_t extra)
{
    if (b->oom || b->len + extra <= b->cap) {
        return;
    }
    size_t ncap = b->cap ? b->cap : 64;
    while (ncap < b->len + extra) {
        ncap *= 2;
    }
    uint8_t *n = (uint8_t *)OSAL_REALLOC_EXTRAM(b->data, ncap);
    if (!n) {
        b->oom = true;
        return;
    }
    b->data = n;
    b->cap = ncap;
}

static void bb_u8(bytebuf_t *b, uint8_t v)
{
    bb_reserve(b, 1);
    if (!b->oom) {
        b->data[b->len++] = v;
    }
}

static void bb_u16(bytebuf_t *b, uint16_t v)
{
    bb_u8(b, (uint8_t)(v & 0xFF));
    bb_u8(b, (uint8_t)((v >> 8) & 0xFF));
}

static void bb_bytes(bytebuf_t *b, const void *p, size_t n)
{
    bb_reserve(b, n);
    if (!b->oom) {
        memcpy(b->data + b->len, p, n);
        b->len += n;
    }
}

/* ---- encode ---- */

static int op_to_code(const char *op, uint8_t *out)
{
    for (uint8_t i = 0; i < OP_COUNT; i++) {
        if (strcmp(op, OP_NAMES[i]) == 0) {
            *out = i;
            return 0;
        }
    }
    return -1;
}

/* Append a string-like value: [u16 len][bytes]. Reads via the supplied
 * strlen/str getter pair so the same code serves string/object/array. */
static int encode_strlike(jparse_ctx_t *jctx, bytebuf_t *b,
                          int (*get_strlen)(jparse_ctx_t *, const char *, int *),
                          int (*get_str)(jparse_ctx_t *, const char *, char *, int))
{
    int slen = 0;
    if (get_strlen(jctx, "value", &slen) != OS_SUCCESS || slen < 0 || slen > 0xFFFF) {
        return -1;
    }
    char *s = (char *)OSAL_MALLOC_EXTRAM((size_t)slen + 1);
    if (!s) {
        return -1;
    }
    if (get_str(jctx, "value", s, slen + 1) != OS_SUCCESS) {
        free(s);
        return -1;
    }
    bb_u16(b, (uint16_t)slen);
    bb_bytes(b, s, (size_t)slen);
    free(s);
    return 0;
}

/* Probe the "value" field, preserving its JSON lexical category. Order:
 * string/object/array (token-typed getters), then int (rejects "1.5"/"true"),
 * float, bool. Appends the value bytes; returns the VT tag, or -1. */
static int encode_value(jparse_ctx_t *jctx, bytebuf_t *b, uint8_t *out_vt)
{
    int slen = 0;
    if (json_obj_get_strlen(jctx, "value", &slen) == OS_SUCCESS) {
        if (encode_strlike(jctx, b, json_obj_get_strlen, json_obj_get_string) != 0) {
            return -1;
        }
        *out_vt = RMAKER_TRIGGER_VT_STRING;
        return 0;
    }
    if (json_obj_get_object_strlen(jctx, "value", &slen) == OS_SUCCESS) {
        if (encode_strlike(jctx, b, json_obj_get_object_strlen, json_obj_get_object_str) != 0) {
            return -1;
        }
        *out_vt = RMAKER_TRIGGER_VT_OBJECT;
        return 0;
    }
    if (json_obj_get_array_strlen(jctx, "value", &slen) == OS_SUCCESS) {
        if (encode_strlike(jctx, b, json_obj_get_array_strlen, json_obj_get_array_str) != 0) {
            return -1;
        }
        *out_vt = RMAKER_TRIGGER_VT_ARRAY;
        return 0;
    }
    int iv = 0;
    if (json_obj_get_int(jctx, "value", &iv) == OS_SUCCESS) {
        int32_t v = (int32_t)iv;
        bb_bytes(b, &v, sizeof(v));
        *out_vt = RMAKER_TRIGGER_VT_INT;
        return 0;
    }
    float fv = 0.0f;
    if (json_obj_get_float(jctx, "value", &fv) == OS_SUCCESS) {
        bb_bytes(b, &fv, sizeof(fv));
        *out_vt = RMAKER_TRIGGER_VT_FLOAT;
        return 0;
    }
    bool bv = false;
    if (json_obj_get_bool(jctx, "value", &bv) == OS_SUCCESS) {
        bb_u8(b, bv ? 1u : 0u);
        *out_vt = RMAKER_TRIGGER_VT_BOOL;
        return 0;
    }
    return -1; /* null / absent value not representable */
}

/* Read a required string field into a heap buffer. Returns NULL on failure. */
static char *get_required_string(jparse_ctx_t *jctx, const char *name)
{
    int len = 0;
    if (json_obj_get_strlen(jctx, name, &len) != OS_SUCCESS || len < 0 || len > 0xFFFF) {
        return NULL;
    }
    char *s = (char *)OSAL_MALLOC_EXTRAM((size_t)len + 1);
    if (!s) {
        return NULL;
    }
    if (json_obj_get_string(jctx, name, s, len + 1) != OS_SUCCESS) {
        free(s);
        return NULL;
    }
    return s;
}

esp_rmaker_error_t esp_rmaker_trigger_details_encode(const char *json, size_t json_len,
        uint8_t **out_buf, size_t *out_len)
{
    if (!json || !out_buf || !out_len) {
        return ESP_RMAKER_INVALID_ARG;
    }
    *out_buf = NULL;
    *out_len = 0;

    jparse_ctx_t jctx;
    if (json_parse_start(&jctx, json, (int)json_len) != OS_SUCCESS) {
        OSAL_LOGE(TAG, "encode: JSON parse start failed");
        return ESP_RMAKER_FAIL;
    }

    bytebuf_t b = {0};
    bb_u8(&b, (uint8_t)RMAKER_TRIGGER_CODEC_VERSION);
    size_t count_pos = b.len;
    bb_u8(&b, 0); /* placeholder count, patched after the loop */

    esp_rmaker_error_t err = ESP_RMAKER_OK;
    int emitted = 0;
    for (int i = 0; json_arr_get_object(&jctx, i) == OS_SUCCESS; i++) {
        /* Disabled triggers are not installed and may legitimately omit
         * path/operator/value, so drop them entirely at encode. The cloud
         * re-sends the full set whenever any trigger changes. */
        bool enabled = true;
        json_obj_get_bool(&jctx, "enabled", &enabled);
        if (!enabled) {
            json_arr_leave_object(&jctx);
            continue;
        }

        if (emitted >= 0xFF) {
            OSAL_LOGE(TAG, "encode: too many enabled triggers (> 255)");
            json_arr_leave_object(&jctx);
            err = ESP_RMAKER_FAIL;
            break;
        }

        char *id = get_required_string(&jctx, "id");
        char *path = get_required_string(&jctx, "path");
        char op[12] = {0};
        uint8_t op_code = 0;

        if (!id || !path
                || json_obj_get_string(&jctx, "operator", op, sizeof(op)) != OS_SUCCESS
                || op_to_code(op, &op_code) != 0) {
            OSAL_LOGE(TAG, "encode: trigger %d missing/invalid id/path/operator", i);
            free(id);
            free(path);
            json_arr_leave_object(&jctx);
            err = ESP_RMAKER_FAIL;
            break;
        }

        /* Reserve the flags byte; fill the value first to learn its type. */
        size_t flags_pos = b.len;
        bb_u8(&b, 0); /* placeholder */
        bb_u8(&b, op_code);
        bb_u16(&b, (uint16_t)strlen(id));
        bb_bytes(&b, id, strlen(id));
        bb_u16(&b, (uint16_t)strlen(path));
        bb_bytes(&b, path, strlen(path));

        uint8_t vt = 0;
        int vrc = encode_value(&jctx, &b, &vt);
        free(id);
        free(path);
        json_arr_leave_object(&jctx);

        if (vrc != 0) {
            OSAL_LOGE(TAG, "encode: trigger %d value not representable", i);
            err = ESP_RMAKER_FAIL;
            break;
        }
        if (!b.oom) {
            b.data[flags_pos] = (uint8_t)(FLAG_ENABLED | ((vt << FLAG_VT_SHIFT) & FLAG_VT_MASK));
        }
        emitted++;
    }

    json_parse_end(&jctx);

    if (err == ESP_RMAKER_OK && !b.oom) {
        b.data[count_pos] = (uint8_t)emitted;
    }

    if (err == ESP_RMAKER_OK && b.oom) {
        err = ESP_RMAKER_NO_MEM;
    }
    if (err != ESP_RMAKER_OK) {
        free(b.data);
        return err;
    }
    *out_buf = b.data;
    *out_len = b.len;
    return ESP_RMAKER_OK;
}

/* ---- decode (single-pass iterator) ---- */

esp_rmaker_error_t esp_rmaker_trigger_details_iter_begin(const uint8_t *buf, size_t buf_len,
        esp_rmaker_trigger_iter_t *it, size_t *out_count)
{
    if (!buf || !it) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (buf_len < 2 || buf[0] != RMAKER_TRIGGER_CODEC_VERSION) {
        OSAL_LOGW(TAG, "decode: bad header (len %zu, ver %u)", buf_len, buf_len ? buf[0] : 0);
        return ESP_RMAKER_FAIL;
    }
    it->buf = buf;
    it->len = buf_len;
    it->off = 2;
    it->remaining = buf[1];
    if (out_count) {
        *out_count = buf[1];
    }
    return ESP_RMAKER_OK;
}

/* Bounds-checked readers over the cursor. */
static int cur_u8(esp_rmaker_trigger_iter_t *it, uint8_t *out)
{
    if (it->off + 1 > it->len) {
        return -1;
    }
    *out = it->buf[it->off++];
    return 0;
}

static int cur_u16(esp_rmaker_trigger_iter_t *it, uint16_t *out)
{
    if (it->off + 2 > it->len) {
        return -1;
    }
    *out = (uint16_t)(it->buf[it->off] | (it->buf[it->off + 1] << 8));
    it->off += 2;
    return 0;
}

/* Read [u16 len][bytes]; yields a pointer into the blob. */
static int cur_blob(esp_rmaker_trigger_iter_t *it, const char **p, size_t *n)
{
    uint16_t len;
    if (cur_u16(it, &len) != 0 || it->off + len > it->len) {
        return -1;
    }
    *p = (const char *)(it->buf + it->off);
    *n = len;
    it->off += len;
    return 0;
}

esp_rmaker_error_t esp_rmaker_trigger_details_iter_next(esp_rmaker_trigger_iter_t *it,
        esp_rmaker_trigger_entry_t *out)
{
    if (!it || !out) {
        return ESP_RMAKER_INVALID_ARG;
    }
    if (it->remaining == 0) {
        return ESP_RMAKER_NOT_FOUND;
    }

    memset(out, 0, sizeof(*out));
    uint8_t flags = 0;
    if (cur_u8(it, &flags) != 0 || cur_u8(it, &out->op_code) != 0) {
        return ESP_RMAKER_FAIL;
    }
    out->enabled = (flags & FLAG_ENABLED) != 0;
    out->value_type = (uint8_t)((flags & FLAG_VT_MASK) >> FLAG_VT_SHIFT);
    if (out->op_code >= OP_COUNT) {
        return ESP_RMAKER_FAIL;
    }
    if (cur_blob(it, &out->id, &out->id_len) != 0
            || cur_blob(it, &out->path, &out->path_len) != 0) {
        return ESP_RMAKER_FAIL;
    }

    switch (out->value_type) {
    case RMAKER_TRIGGER_VT_BOOL: {
        uint8_t v;
        if (cur_u8(it, &v) != 0) {
            return ESP_RMAKER_FAIL;
        }
        out->value_bool = v != 0;
        break;
    }
    case RMAKER_TRIGGER_VT_INT: {
        if (it->off + 4 > it->len) {
            return ESP_RMAKER_FAIL;
        }
        memcpy(&out->value_int, it->buf + it->off, 4);
        it->off += 4;
        break;
    }
    case RMAKER_TRIGGER_VT_FLOAT: {
        if (it->off + 4 > it->len) {
            return ESP_RMAKER_FAIL;
        }
        memcpy(&out->value_float, it->buf + it->off, 4);
        it->off += 4;
        break;
    }
    case RMAKER_TRIGGER_VT_STRING:
    case RMAKER_TRIGGER_VT_OBJECT:
    case RMAKER_TRIGGER_VT_ARRAY:
        if (cur_blob(it, &out->value_str, &out->value_str_len) != 0) {
            return ESP_RMAKER_FAIL;
        }
        break;
    default:
        return ESP_RMAKER_FAIL;
    }

    it->remaining--;
    return ESP_RMAKER_OK;
}
