/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file services.h
 * @brief Service constants.
 */

#ifndef __SERVICES_CONSTANTS_H__
#define __SERVICES_CONSTANTS_H__

/* Timezone service **************************************************************/

#define RMAKER_SERVICES_TIMEZONE_SERVICE_ID     "Time"
#define RMAKER_SERVICES_TIMEZONE_PARAM_ID       "TZ"
#define RMAKER_SERVICES_TIMEZONE_POSIX_PARAM_ID "TZ-POSIX"

/* System service **************************************************************/

#define RMAKER_SERVICES_SYSTEM_SERVICE_ID             "System"
#define RMAKER_SERVICES_SYSTEM_REBOOT_PARAM_ID        "Reboot"
#define RMAKER_SERVICES_SYSTEM_NETWORK_RESET_PARAM_ID "Network-Reset"
#define RMAKER_SERVICES_SYSTEM_FACTORY_RESET_PARAM_ID "Factory-Reset"

/* Schedule service **************************************************************/

#define RMAKER_SERVICES_SCHEDULE_SERVICE_ID "Schedule"
#define RMAKER_SERVICES_SCHEDULE_PARAM_ID   "Schedules"

/* Local control service **************************************************************/

#define RMAKER_SERVICES_LOCAL_CTRL_SERVICE_ID          "Local Control"
#define RMAKER_SERVICES_LOCAL_CTRL_POP_PARAM_ID        "POP"
#define RMAKER_SERVICES_LOCAL_CTRL_TYPE_PARAM_ID       "Type"
#define RMAKER_SERVICES_LOCAL_CTRL_USERNAME_PARAM_ID   "Username"

#endif /* __SERVICES_CONSTANTS_H__ */
