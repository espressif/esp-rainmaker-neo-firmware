/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cloud.h
 * @brief Cloud constants.
 */

/* Header guard ******************************************************************/

#ifndef __CONSTANTS_CLOUD_H__
#define __CONSTANTS_CLOUD_H__

#include "constants/identity.h"

/* Event constants ****************************************************************/

#define RMAKER_CLOUD_EVENT_NAME_MAX_LEN             32

/**
 * Most events honoured from a single cloud payload. The dispatcher buffers the
 * names on the stack before walking them, so this bounds that buffer against an
 * "event" array whose length is whatever the payload says. Only 9 distinct event
 * names are dispatchable at all, so this is well clear of any real payload.
 */
#define RMAKER_CLOUD_EVENT_MAX_COUNT                16

/* getGroupInfo constants *********************************************************/
/* Kept as aliases for source compatibility - prefer the identity.h names. */

#define RMAKER_CLOUD_GROUP_INFO_PRIMARY_BUFFER_SIZE     RMAKER_GROUP_PRIMARY_BUFFER_SIZE
#define RMAKER_CLOUD_GROUP_INFO_SUBGROUP_MAX_COUNT      RMAKER_SUBGROUP_MAX_COUNT
#define RMAKER_CLOUD_GROUP_INFO_SUBGROUP_BUFFER_SIZE    RMAKER_SUBGROUP_BUFFER_SIZE

/* Header guard ******************************************************************/

#endif /* __CONSTANTS_CLOUD_H__ */
