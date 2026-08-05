/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_trigger_codec.h
 * @brief Compact binary codec for automation trigger details.
 *
 * Trigger details arrive from the cloud as a JSON array. This codec is the
 * single JSON reader: ::esp_rmaker_trigger_details_encode parses that JSON
 * once into a compact binary blob, and everything downstream (both the live
 * trigger list built on receipt and the list rebuilt from NVS on boot) is
 * driven off the blob via the iterator below. There is intentionally no
 * binary->JSON path: the blob is the canonical persisted + parsed form, so a
 * trigger-format change touches only this codec, not a second JSON parser.
 *
 * The codec is *structural*: it captures id/enabled/path/operator and the
 * value's JSON lexical category (bool/int/float/string/object/array). It does
 * NOT resolve ``path`` to an update id or know the data-model value type;
 * the consumer (automation service) does that, since it needs the node.
 *
 * Binary layout (little-endian):
 * @verbatim
 *   [u8  version = 1]
 *   [u8  count]                       // number of triggers, 0..255
 *   repeat count times:
 *     [u8  flags]                     // bit0: enabled; bits1-3: value type
 *     [u8  op_code]                   // 0 eq,1 ne,2 gt,3 lt,4 ge,5 le
 *     [u16 id_len][id bytes]
 *     [u16 path_len][path bytes]
 *     [value]                         // by value type:
 *                                     //   bool   -> u8 (0/1)
 *                                     //   int    -> i32
 *                                     //   float  -> f32
 *                                     //   string -> [u16 len][bytes]
 *                                     //   object -> [u16 len][raw json bytes]
 *                                     //   array  -> [u16 len][raw json bytes]
 * @endverbatim
 */

#ifndef __UTIL_ESP_RMAKER_TRIGGER_CODEC_H__
#define __UTIL_ESP_RMAKER_TRIGGER_CODEC_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_rmaker_error_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Current binary blob version (first byte). */
#define RMAKER_TRIGGER_CODEC_VERSION 1u

/** Value lexical types (3 bits in the per-trigger flags byte). */
#define RMAKER_TRIGGER_VT_BOOL   0u
#define RMAKER_TRIGGER_VT_INT    1u
#define RMAKER_TRIGGER_VT_FLOAT  2u
#define RMAKER_TRIGGER_VT_STRING 3u
#define RMAKER_TRIGGER_VT_OBJECT 4u
#define RMAKER_TRIGGER_VT_ARRAY  5u

/** Operator codes (stable, persisted). */
#define RMAKER_TRIGGER_OP_EQ 0u
#define RMAKER_TRIGGER_OP_NE 1u
#define RMAKER_TRIGGER_OP_GT 2u
#define RMAKER_TRIGGER_OP_LT 3u
#define RMAKER_TRIGGER_OP_GE 4u
#define RMAKER_TRIGGER_OP_LE 5u

/**
 * @brief One decoded trigger. String/object/array pointers reference the
 *        source blob and stay valid only while it does (not NUL-terminated).
 */
typedef struct {
    const char *id;            /**< Trigger id (not NUL-terminated). */
    size_t      id_len;
    const char *path;          /**< Param path (not NUL-terminated). */
    size_t      path_len;
    uint8_t     op_code;       /**< RMAKER_TRIGGER_OP_*. */
    bool        enabled;
    uint8_t     value_type;    /**< RMAKER_TRIGGER_VT_*. */
    bool        value_bool;    /**< Valid when value_type == BOOL. */
    int32_t     value_int;     /**< Valid when value_type == INT. */
    float       value_float;   /**< Valid when value_type == FLOAT. */
    const char *value_str;     /**< Valid when value_type is STRING/OBJECT/ARRAY (raw bytes). */
    size_t      value_str_len;
} esp_rmaker_trigger_entry_t;

/**
 * @brief Single-pass decode cursor. Treat as opaque; init via
 *        ::esp_rmaker_trigger_details_iter_begin.
 */
typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         off;
    size_t         remaining;
} esp_rmaker_trigger_iter_t;

/**
 * @brief Encode a trigger-details JSON array into the compact binary blob.
 *
 * @param[in]  json     JSON array string (cloud trigger details).
 * @param[in]  json_len Length of ``json`` (excluding any NUL).
 * @param[out] out_buf  On success, heap blob; caller frees. NULL on error.
 * @param[out] out_len  On success, blob length in bytes.
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG, ESP_RMAKER_NO_MEM, or
 *         ESP_RMAKER_FAIL on a JSON shape this codec cannot represent
 *         (missing id/path/operator, unknown operator, null value,
 *         >255 triggers, id/path/value longer than 65535).
 */
esp_rmaker_error_t esp_rmaker_trigger_details_encode(const char *json, size_t json_len,
        uint8_t **out_buf, size_t *out_len);

/**
 * @brief Begin iterating a decoded blob. Validates the header.
 *
 * @param[in]  buf       Blob from ::esp_rmaker_trigger_details_encode.
 * @param[in]  buf_len   Blob length.
 * @param[out] it        Cursor, initialised on success.
 * @param[out] out_count Trigger count (may be NULL).
 * @return ESP_RMAKER_OK, ESP_RMAKER_INVALID_ARG, or ESP_RMAKER_FAIL on a
 *         truncated / wrong-version / corrupt blob.
 */
esp_rmaker_error_t esp_rmaker_trigger_details_iter_begin(const uint8_t *buf, size_t buf_len,
        esp_rmaker_trigger_iter_t *it, size_t *out_count);

/**
 * @brief Fetch the next trigger from the cursor.
 *
 * @param[in,out] it  Cursor.
 * @param[out]    out Filled on success (pointers reference the blob).
 * @return ESP_RMAKER_OK on a yielded entry, ESP_RMAKER_NOT_FOUND when the
 *         iteration is complete, or ESP_RMAKER_FAIL on a corrupt blob.
 */
esp_rmaker_error_t esp_rmaker_trigger_details_iter_next(esp_rmaker_trigger_iter_t *it,
        esp_rmaker_trigger_entry_t *out);

#ifdef __cplusplus
}
#endif

#endif /* __UTIL_ESP_RMAKER_TRIGGER_CODEC_H__ */
