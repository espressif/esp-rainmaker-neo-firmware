/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_aws_sign_port_osal_json.c
 * @brief JSON ops for the rmaker_aws_sign port, over osal's json_parser.
 *
 * json_parser tokenizes; the member walk, the duplicate-key check and the
 * unescaping are here, because it copies token bytes verbatim and reports an
 * absent key and a wrongly-typed one the same way.
 */

#include "esp_rmaker_aws_sign_port_osal_priv.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "json_parser.h"

#include "osal_log.h"

static const char *TAG = "rmng_aws_sign_json";

/** First token past @p tok and everything nested under it. */
static const json_tok_t *__skip_value(const jparse_ctx_t *ctx, const json_tok_t *tok)
{
    const json_tok_t *end = ctx->tokens + ctx->num_tokens;
    const json_tok_t *next = tok + 1;
    while (next < end && next->start < tok->end) {
        next++;
    }
    return next;
}

static bool __key_matches(const jparse_ctx_t *ctx, const json_tok_t *tok, const char *key)
{
    const size_t key_len = strlen(key);
    return tok->type == JSMN_STRING && (size_t)(tok->end - tok->start) == key_len
           && memcmp(ctx->js + tok->start, key, key_len) == 0;
}

/**
 * @brief Find the value of @p key among the members of @p obj.
 *
 * @param[out] out The value token, or NULL when the key is absent.
 * @return ESP_OK, or ESP_ERR_INVALID_RESPONSE for a malformed member or a duplicate @p key -
 *         RFC 8259 leaves the winner of a duplicate undefined, so it is refused, not resolved.
 */
static esp_err_t __member_value(const jparse_ctx_t *ctx, const json_tok_t *obj,
                                const char *key, const json_tok_t **out)
{
    const json_tok_t *tok = obj + 1;
    const json_tok_t *end = ctx->tokens + ctx->num_tokens;

    *out = NULL;
    for (int i = 0; i < obj->size; i++) {
        /* A key carries exactly one child, its value; jsmn accepts a bare string where a
         * member belongs, which is where a missing colon shows up. */
        if (tok + 1 >= end || tok->type != JSMN_STRING || tok->size != 1) {
            OSAL_LOGE(TAG, "Malformed object member");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (__key_matches(ctx, tok, key)) {
            if (*out != NULL) {
                OSAL_LOGE(TAG, "Duplicate key '%s'", key);
                return ESP_ERR_INVALID_RESPONSE;
            }
            *out = tok + 1;
        }
        tok = __skip_value(ctx, tok + 1);
    }
    return ESP_OK;
}

static int __hex4(const char *p)
{
    int value = 0;
    for (int i = 0; i < 4; i++) {
        const char c = p[i];
        int digit;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        } else {
            return -1;
        }
        value = (value << 4) | digit;
    }
    return value;
}

/** Encode one code point as UTF-8. Returns bytes written, or 0 if it does not fit. */
static size_t __utf8_encode(uint32_t cp, char *dst, size_t space)
{
    if (cp < 0x80 && space >= 1) {
        dst[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800 && space >= 2) {
        dst[0] = (char)(0xC0 | (cp >> 6));
        dst[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000 && space >= 3) {
        dst[0] = (char)(0xE0 | (cp >> 12));
        dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp < 0x110000 && space >= 4) {
        dst[0] = (char)(0xF0 | (cp >> 18));
        dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        dst[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

/**
 * @brief Unescape one JSON string token into @p dst, NUL-terminated.
 *
 * Rejects raw control characters, lone surrogates, bad or truncated \\uXXXX, \\u0000 (it
 * cannot survive the NUL termination) and anything that does not fit @p dst_size.
 */
static esp_err_t __unescape(const char *src, size_t src_len, char *dst, size_t dst_size, size_t *out_len)
{
    size_t written = 0;

    for (size_t i = 0; i < src_len; i++) {
        uint32_t cp;
        if ((unsigned char)src[i] < 0x20) {
            OSAL_LOGE(TAG, "Raw control character in string value");
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (src[i] != '\\') {
            cp = (unsigned char)src[i];
            if (written + 1 >= dst_size) {
                return ESP_ERR_INVALID_ARG;
            }
            dst[written++] = src[i];
            continue;
        }

        if (++i >= src_len) {
            OSAL_LOGE(TAG, "Truncated escape in string value");
            return ESP_ERR_INVALID_RESPONSE;
        }
        switch (src[i]) {
        case '"':  cp = '"';  break;
        case '\\': cp = '\\'; break;
        case '/':  cp = '/';  break;
        case 'b':  cp = '\b'; break;
        case 'f':  cp = '\f'; break;
        case 'n':  cp = '\n'; break;
        case 'r':  cp = '\r'; break;
        case 't':  cp = '\t'; break;
        case 'u': {
            if (i + 4 >= src_len) {
                OSAL_LOGE(TAG, "Truncated \\uXXXX escape");
                return ESP_ERR_INVALID_RESPONSE;
            }
            const int unit = __hex4(src + i + 1);
            if (unit < 0) {
                OSAL_LOGE(TAG, "Bad hex in \\uXXXX escape");
                return ESP_ERR_INVALID_RESPONSE;
            }
            i += 4;
            cp = (uint32_t)unit;
            if (cp >= 0xDC00 && cp <= 0xDFFF) {
                OSAL_LOGE(TAG, "Lone low surrogate");
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                /* A high surrogate is only meaningful paired with the low one that follows. */
                if (i + 6 >= src_len || src[i + 1] != '\\' || src[i + 2] != 'u') {
                    OSAL_LOGE(TAG, "Lone high surrogate");
                    return ESP_ERR_INVALID_RESPONSE;
                }
                const int low = __hex4(src + i + 3);
                if (low < 0xDC00 || low > 0xDFFF) {
                    OSAL_LOGE(TAG, "High surrogate not followed by a low surrogate");
                    return ESP_ERR_INVALID_RESPONSE;
                }
                i += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint32_t)low - 0xDC00);
            }
            if (cp == 0) {
                OSAL_LOGE(TAG, "\\u0000 cannot be represented in a NUL-terminated value");
                return ESP_ERR_INVALID_RESPONSE;
            }
            break;
        }
        default:
            OSAL_LOGE(TAG, "Unknown escape in string value");
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (written + 1 >= dst_size) {
            return ESP_ERR_INVALID_ARG;
        }
        const size_t n = __utf8_encode(cp, dst + written, dst_size - written - 1);
        if (n == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        written += n;
    }

    dst[written] = '\0';
    *out_len = written;
    return ESP_OK;
}

/** Fail closed: a caller that skips the return value must see nothing, not a subset. */
static void __clear(esp_rmaker_aws_sign_json_field_t fields[], size_t num)
{
    for (size_t i = 0; i < num; i++) {
        fields[i].found = false;
        fields[i].len = 0;
        if (fields[i].buf != NULL && fields[i].buf_size > 0) {
            fields[i].buf[0] = '\0';
        }
    }
}

static esp_err_t __read_strs(const char *json, size_t len, const char *object,
                             esp_rmaker_aws_sign_json_field_t fields[], size_t num)
{
    if (json == NULL || (fields == NULL && num > 0) || len == 0 || len > INT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < num; i++) {
        if (fields[i].key == NULL || fields[i].buf == NULL || fields[i].buf_size == 0) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    __clear(fields, num);

    jparse_ctx_t ctx;
    if (json_parse_start(&ctx, json, (int)len) != OS_SUCCESS) {
        OSAL_LOGE(TAG, "Failed to parse JSON document");
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = ESP_OK;
    const json_tok_t *target = ctx.tokens;
    if (target->type != JSMN_OBJECT) {
        OSAL_LOGE(TAG, "JSON root is not an object");
        err = ESP_ERR_INVALID_RESPONSE;
        goto end;
    }

    if (object != NULL) {
        const json_tok_t *nested = NULL;
        err = __member_value(&ctx, target, object, &nested);
        if (err != ESP_OK) {
            goto end;
        }
        if (nested == NULL || nested->type != JSMN_OBJECT) {
            /* Nothing to read from: every field stays absent, which is not an error. */
            goto end;
        }
        target = nested;
    }

    for (size_t i = 0; i < num; i++) {
        const json_tok_t *value = NULL;
        err = __member_value(&ctx, target, fields[i].key, &value);
        if (err != ESP_OK) {
            goto end;
        }
        if (value == NULL) {
            continue;
        }
        if (value->type != JSMN_STRING) {
            OSAL_LOGE(TAG, "Key '%s' is not a string", fields[i].key);
            err = ESP_ERR_INVALID_RESPONSE;
            goto end;
        }
        err = __unescape(ctx.js + value->start, (size_t)(value->end - value->start),
                         fields[i].buf, fields[i].buf_size, &fields[i].len);
        if (err != ESP_OK) {
            OSAL_LOGE(TAG, "Failed to read key '%s': 0x%x", fields[i].key, (int)err);
            goto end;
        }
        fields[i].found = true;
    }

end:
    json_parse_end(&ctx);
    if (err != ESP_OK) {
        __clear(fields, num);
    }
    return err;
}

const esp_rmaker_aws_sign_json_ops_t esp_rmaker_aws_sign_port_osal_json_ops = {
    .read_strs = __read_strs,
};
