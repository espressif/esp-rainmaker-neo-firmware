/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file path.h
 * @brief Data model path separator constants.
 *
 * A "path" is a single string that uniquely identifies a parameter. Used by
 * timeseries and automation trigger payloads.
 *
 * - Format: "<device_id>.<param_id>"            e.g. "Light.Power"
 *
 * The separator must not appear in any device id or param id.
 */

#ifndef __PATH_CONSTANTS_H__
#define __PATH_CONSTANTS_H__

#define RMAKER_PATH_SEPARATOR_CHAR '.'
#define RMAKER_PATH_SEPARATOR_STR  "."

#endif /* __PATH_CONSTANTS_H__ */
