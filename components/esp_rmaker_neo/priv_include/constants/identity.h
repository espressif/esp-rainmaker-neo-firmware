/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file identity.h
 * @brief Identity-related size constants (thing name, group, subgroup).
 *
 * These are general identity limits, shared by topic builders, NVS
 * records, MQTT subscribe callbacks, and the bridge module. Keep all
 * size-of-an-identifier macros here so callers cannot drift on buffer
 * sizes (cf. former local ``__THING_NAME_MAX`` in mqtt_topics.c and
 * ad-hoc ``[96]`` buffers in the bridge module).
 */

#ifndef __CONSTANTS_IDENTITY_H__
#define __CONSTANTS_IDENTITY_H__

/* Thing name ********************************************************************/

/**
 * @brief Maximum length of an AWS IoT Thing name (excluding NUL).
 *
 * AWS IoT Core limits Thing names to 128 ASCII characters. Bridged child
 * Thing names follow the form ``<parent>--<suffix>`` and remain within
 * this cap because the parent and suffix are each constrained well below
 * half this length by the Lambda.
 */
#define RMAKER_THING_NAME_LEN_MAX        128

/**
 * @brief Buffer size for a NUL-terminated AWS IoT Thing name.
 */
#define RMAKER_THING_NAME_BUFFER_SIZE    (RMAKER_THING_NAME_LEN_MAX + 1)

/* Group / subgroup IDs **********************************************************/

/**
 * @brief Maximum length of a primary group ID (excluding NUL).
 */
#define RMAKER_GROUP_PRIMARY_LEN_MAX     6

/**
 * @brief Buffer size for a NUL-terminated primary group ID.
 */
#define RMAKER_GROUP_PRIMARY_BUFFER_SIZE (RMAKER_GROUP_PRIMARY_LEN_MAX + 1)

/**
 * @brief Maximum length of a subgroup ID (excluding NUL).
 */
#define RMAKER_SUBGROUP_LEN_MAX          3

/**
 * @brief Buffer size for a NUL-terminated subgroup ID.
 */
#define RMAKER_SUBGROUP_BUFFER_SIZE      (RMAKER_SUBGROUP_LEN_MAX + 1)

/**
 * @brief Maximum number of subgroups a node may belong to.
 */
#define RMAKER_SUBGROUP_MAX_COUNT        3

/* Composed group_info_str *******************************************************/

/**
 * @brief Maximum length of the composed group info string
 *        ``"<primary>[-<subgroup>]{0,RMAKER_SUBGROUP_MAX_COUNT}"``
 *        (excluding NUL). Each appended subgroup costs one ``'-'``
 *        plus up to ``RMAKER_SUBGROUP_LEN_MAX`` characters.
 */
#define RMAKER_GROUP_INFO_STR_LEN_MAX \
    (RMAKER_GROUP_PRIMARY_LEN_MAX + \
     RMAKER_SUBGROUP_MAX_COUNT * (1 + RMAKER_SUBGROUP_LEN_MAX))

/**
 * @brief Buffer size for a NUL-terminated group_info_str.
 */
#define RMAKER_GROUP_INFO_STR_BUFFER_SIZE (RMAKER_GROUP_INFO_STR_LEN_MAX + 1)

#endif /* __CONSTANTS_IDENTITY_H__ */
