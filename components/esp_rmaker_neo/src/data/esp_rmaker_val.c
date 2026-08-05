/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_val.c
 * @brief RainMaker Neo value handling functions.
 */

/* Declarations */
#include "esp_rmaker_val.h"

/* Standard includes */
#include <string.h>
#include <stdbool.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Platform common includes */
#include "osal_log.h"
#include "osal_mem_alloc.h"

static const char *TAG = "rmng_dm_val";

esp_rmaker_param_val_t esp_rmaker_bool(bool bval)
{
    esp_rmaker_param_val_t param_val = {
        .type = RMAKER_VAL_TYPE_BOOLEAN,
        .val.b = bval
    };
    return param_val;
}

esp_rmaker_param_val_t esp_rmaker_int(int ival)
{
    esp_rmaker_param_val_t param_val = {
        .type = RMAKER_VAL_TYPE_INTEGER,
        .val.i = ival
    };
    return param_val;
}

esp_rmaker_param_val_t esp_rmaker_float(float fval)
{
    esp_rmaker_param_val_t param_val = {
        .type = RMAKER_VAL_TYPE_FLOAT,
        .val.f = fval
    };
    return param_val;
}

esp_rmaker_param_val_t esp_rmaker_str(const char *sval)
{
    esp_rmaker_param_val_t param_val = {
        .type = RMAKER_VAL_TYPE_STRING,
        .val.s = (char *)sval
    };
    return param_val;
}

esp_rmaker_param_val_t esp_rmaker_obj(const char *val)
{
    esp_rmaker_param_val_t param_val = {
        .type = RMAKER_VAL_TYPE_OBJECT,
        .val.s = (char *)val
    };
    return param_val;
}

esp_rmaker_param_val_t esp_rmaker_array(const char *val)
{
    esp_rmaker_param_val_t param_val = {
        .type = RMAKER_VAL_TYPE_ARRAY,
        .val.s = (char *)val
    };
    return param_val;
}

esp_rmaker_error_t esp_rmaker_val_compare(const esp_rmaker_param_val_t *val1, const esp_rmaker_param_val_t *val2, esp_rmaker_val_compare_t compare_type)
{
    if (!val1 || !val2) {
        OSAL_LOGE(TAG, "Passed NULL pointers to esp_rmaker_val_compare: %p, %p.", val1, val2);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (val1->type != val2->type) {
        OSAL_LOGE(TAG, "Values are of different types: %d, %d.", val1->type, val2->type);
        return ESP_RMAKER_INVALID_ARG;
    }

    bool result = false;
    switch (val1->type) {
    case RMAKER_VAL_TYPE_BOOLEAN:
        switch (compare_type) {
        case RMAKER_VAL_COMPARE_EQ:
            result = val1->val.b == val2->val.b;
            break;
        case RMAKER_VAL_COMPARE_NEQ:
            result = val1->val.b != val2->val.b;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid comparison type for boolean: %d.", compare_type);
            return ESP_RMAKER_INVALID_ARG;
        }
        break;
    case RMAKER_VAL_TYPE_INTEGER:
        switch (compare_type) {
        case RMAKER_VAL_COMPARE_EQ:
            result = val1->val.i == val2->val.i;
            break;
        case RMAKER_VAL_COMPARE_NEQ:
            result = val1->val.i != val2->val.i;
            break;
        case RMAKER_VAL_COMPARE_GT:
            result = val1->val.i > val2->val.i;
            break;
        case RMAKER_VAL_COMPARE_LT:
            result = val1->val.i < val2->val.i;
            break;
        case RMAKER_VAL_COMPARE_GTE:
            result = val1->val.i >= val2->val.i;
            break;
        case RMAKER_VAL_COMPARE_LTE:
            result = val1->val.i <= val2->val.i;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid comparison type for integer: %d.", compare_type);
            return ESP_RMAKER_INVALID_ARG;
        }
        break;
    case RMAKER_VAL_TYPE_FLOAT:
        switch (compare_type) {
        case RMAKER_VAL_COMPARE_EQ:
            result = val1->val.f == val2->val.f;
            break;
        case RMAKER_VAL_COMPARE_NEQ:
            result = val1->val.f != val2->val.f;
            break;
        case RMAKER_VAL_COMPARE_GT:
            result = val1->val.f > val2->val.f;
            break;
        case RMAKER_VAL_COMPARE_LT:
            result = val1->val.f < val2->val.f;
            break;
        case RMAKER_VAL_COMPARE_GTE:
            result = val1->val.f >= val2->val.f;
            break;
        case RMAKER_VAL_COMPARE_LTE:
            result = val1->val.f <= val2->val.f;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid comparison type for float: %d.", compare_type);
            return ESP_RMAKER_INVALID_ARG;
        }
        break;
    case RMAKER_VAL_TYPE_STRING:
    case RMAKER_VAL_TYPE_OBJECT:
    case RMAKER_VAL_TYPE_ARRAY:
        switch (compare_type) {
        case RMAKER_VAL_COMPARE_EQ:
            result = strcmp(val1->val.s, val2->val.s) == 0;
            break;
        case RMAKER_VAL_COMPARE_NEQ:
            result = strcmp(val1->val.s, val2->val.s) != 0;
            break;
        default:
            OSAL_LOGE(TAG, "Invalid comparison type for string-like values: %d.", compare_type);
            return ESP_RMAKER_INVALID_ARG;
        }
        break;
    default:
        OSAL_LOGE(TAG, "Invalid value type: %d.", val1->type);
        return ESP_RMAKER_INVALID_ARG;
    }

    return result ? ESP_RMAKER_OK : ESP_RMAKER_FAIL;
}

esp_rmaker_error_t esp_rmaker_val_copy(const esp_rmaker_param_val_t *val, esp_rmaker_param_val_t *dest)
{
    if (!val || !dest) {
        OSAL_LOGE(TAG, "Passed NULL pointers to esp_rmaker_val_copy: %p, %p.", val, dest);
        return ESP_RMAKER_INVALID_ARG;
    }
    *dest = *val;
    if (val->type == RMAKER_VAL_TYPE_STRING || val->type == RMAKER_VAL_TYPE_OBJECT || val->type == RMAKER_VAL_TYPE_ARRAY) {
        dest->val.s = OSAL_STRDUP_EXTRAM(val->val.s);
        if (!dest->val.s) {
            OSAL_LOGE(TAG, "Failed to allocate memory for the value of val %p.", val);
            return ESP_RMAKER_NO_MEM;
        }
    }
    return ESP_RMAKER_OK;
}

esp_rmaker_error_t esp_rmaker_val_free(esp_rmaker_param_val_t *val)
{
    if (!val) {
        OSAL_LOGE(TAG, "Passed NULL pointer to esp_rmaker_val_free: %p.", val);
        return ESP_RMAKER_INVALID_ARG;
    }
    if (val->type == RMAKER_VAL_TYPE_STRING || val->type == RMAKER_VAL_TYPE_OBJECT || val->type == RMAKER_VAL_TYPE_ARRAY) {
        free(val->val.s);
    }
    return ESP_RMAKER_OK;
}
