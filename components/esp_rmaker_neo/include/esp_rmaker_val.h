/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file esp_rmaker_val.h
 * @brief RainMaker Neo values.
 */

#ifndef __ESP_RMAKER_VAL_H__
#define __ESP_RMAKER_VAL_H__

/* Standard includes */
#include <stdbool.h>
#include <stdint.h>

/* Error includes */
#include "esp_rmaker_error_types.h"

/* Public structures *******************************************************/

/** 64 bit integer with 4-byte alignment, so the value stays at offset 4 in saved 32-bit param blobs. */
typedef int64_t esp_rmaker_int64_t __attribute__((aligned(4)));

/**
 * @brief RainMaker Neo Value Type.
 */
typedef enum {
    /** Invalid */
    RMAKER_VAL_TYPE_INVALID = 0,
    /** Boolean */
    RMAKER_VAL_TYPE_BOOLEAN,
    /** Integer. Mapped to a 32 bit signed integer */
    RMAKER_VAL_TYPE_INTEGER,
    /** Floating point number */
    RMAKER_VAL_TYPE_FLOAT,
    /** NULL terminated string */
    RMAKER_VAL_TYPE_STRING,
    /** NULL terminated JSON Object string Eg. {"name":"value"} */
    RMAKER_VAL_TYPE_OBJECT,
    /** NULL terminated JSON Array string Eg. [1,2,3] */
    RMAKER_VAL_TYPE_ARRAY,
    /** 64 bit signed integer */
    RMAKER_VAL_TYPE_INT64,
} esp_rmaker_val_type_t;

/**
 * @brief RainMaker Neo Value Comparison.
 *
 * Used with esp_rmaker_val_compare(), which tests `val1 <op> val2`.
 */
typedef enum {
    /** val1 is equal to val2 */
    RMAKER_VAL_COMPARE_EQ = 0,
    /** val1 is not equal to val2 */
    RMAKER_VAL_COMPARE_NEQ,
    /** val1 is greater than val2 */
    RMAKER_VAL_COMPARE_GT,
    /** val1 is less than val2 */
    RMAKER_VAL_COMPARE_LT,
    /** val1 is greater than or equal to val2 */
    RMAKER_VAL_COMPARE_GTE,
    /** val1 is less than or equal to val2 */
    RMAKER_VAL_COMPARE_LTE,
} esp_rmaker_val_compare_t;

/**
 * @brief RainMaker Neo Value.
 */
typedef union {
    /** Boolean */
    bool b;
    /** Integer */
    int i;
    /** Float */
    float f;
    /** NULL terminated string */
    char *s;
    /** 64 bit integer */
    esp_rmaker_int64_t i64;
} esp_rmaker_val_t;

/**
 * @brief RainMaker Neo Parameter Value.
 */
typedef struct {
    /** Type of Value */
    esp_rmaker_val_type_t type;
    /** Actual value. Depends on the type */
    esp_rmaker_val_t val;
} esp_rmaker_param_val_t;

/**
 * @brief RainMaker Neo Parameter Bounds.
 */
typedef struct {
    /** Minimum value */
    esp_rmaker_param_val_t min;
    /** Maximum value */
    esp_rmaker_param_val_t max;
    /** Step value */
    esp_rmaker_param_val_t step;
} esp_rmaker_param_bounds_t;

/**
 * @brief One enumerated value a parameter may take, with the text to display for it.
 *
 * Used with esp_rmaker_param_add_valid_values() to declare what a parameter's values
 * *mean*, which bounds cannot express: bounds describe a numeric domain (magnitude,
 * ordering, step), while these declare labels for specific values. Both may be set on
 * one parameter, because ordinal-and-labelled is a real case (a speed that is 0..5,
 * genuinely sliderable, and also wants Off/Low/Medium/High).
 */
typedef struct {
    /** A value the parameter may take. Its type must match the parameter's own. */
    esp_rmaker_param_val_t value;
    /** Display text for this value. Clients may localise it; the value is what stays stable. */
    const char *label;
} esp_rmaker_param_value_label_t;

/* Public functions *******************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise a Boolean value
 *
 * @param[in] bval Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_bool(bool bval);

/**
 * @brief Initialise an Integer value
 *
 * @param[in] ival Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_int(int ival);

/**
 * @brief Initialise a 64 bit Integer value
 *
 * @param[in] ival Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_int64(int64_t ival);

/**
 * @brief Initialise a Float value
 *
 * @param[in] fval Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_float(float fval);

/**
 * @brief Initialise a String value
 *
 * @note sval must be valid UTF-8. The param setters return ESP_RMAKER_INVALID_ARG otherwise.
 *
 * @param[in] sval Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_str(const char *sval);

/**
 * @brief Initialise a JSON object value
 *
 * @note The object is not validated internally. It is the application's
 *       responsibility to ensure that the object is a valid JSON object,
 *       e.g. esp_rmaker_obj("{\"name\":\"value\"}");
 *
 * @param[in] val Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_obj(const char *val);

/**
 * @brief Initialise a JSON array value
 *
 * @note The array is not validated internally. It is the application's
 *       responsibility to ensure that the array is a valid JSON array,
 *       e.g. esp_rmaker_array("[1,2,3]");
 *
 * @param[in] val Initialising value.
 *
 * @return Value structure.
 */
esp_rmaker_param_val_t esp_rmaker_array(const char *val);

/**
 * @brief Compare two values
 *
 * @param[in] val1 First value
 * @param[in] val2 Second value
 * @param[in] compare_type Type of comparison. val1 is compared to val2 based on the compare_type. e.g., GT means val1 > val2.
 *
 * @return ESP_RMAKER_OK if the comparison holds.
 * @return ESP_RMAKER_INVALID_ARG if either value is NULL, the values are of different types, or
 *         the comparison type is not valid for that type (e.g. GT on a boolean).
 * @return ESP_RMAKER_FAIL if the comparison does not hold.
 */
esp_rmaker_error_t esp_rmaker_val_compare(const esp_rmaker_param_val_t *val1, const esp_rmaker_param_val_t *val2, esp_rmaker_val_compare_t compare_type);

/**
 * @brief Copy a value. For string, object, and array types, a new copy is created and must be freed by the caller.
 *
 * @param[in] val Source value
 * @param[out] dest Destination value
 *
 * @return ESP_RMAKER_OK on success, otherwise error code.
 */
esp_rmaker_error_t esp_rmaker_val_copy(const esp_rmaker_param_val_t *val, esp_rmaker_param_val_t *dest);

/**
 * @brief Free a value. For string, object, and array types, the held string is freed.
 *
 * @note The struct itself is not modified, so `val->val.s` is left dangling. Types other than
 *       string, object and array hold nothing to free and the call is a no-op.
 *
 * @param[in] val Value to free
 *
 * @return ESP_RMAKER_OK on success.
 * @return ESP_RMAKER_INVALID_ARG if val is NULL.
 */
esp_rmaker_error_t esp_rmaker_val_free(esp_rmaker_param_val_t *val);

#ifdef __cplusplus
}
#endif

#endif /* __ESP_RMAKER_VAL_H__ */
