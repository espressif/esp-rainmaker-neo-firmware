/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "osal_log.h"

#define LIBRARY_LOG_NAME "coreMQTT"

#define EXTRACT_ARGS( ... ) __VA_ARGS__
#define STRIP_PARENS( X ) X
#define REMOVE_PARENS( X ) STRIP_PARENS( EXTRACT_ARGS X )

/* Undefine logging macros if they were defined somewhere else like another AWS/FreeRTOS library. */
#ifdef LogError
#undef LogError
#endif

#ifdef LogWarn
#undef LogWarn
#endif

#ifdef LogInfo
#undef LogInfo
#endif

#ifdef LogDebug
#undef LogDebug
#endif

#define LogError( message, ... ) OSAL_LOGE( LIBRARY_LOG_NAME, REMOVE_PARENS( message ), ##__VA_ARGS__ )
#define LogWarn( message, ... ) OSAL_LOGW( LIBRARY_LOG_NAME, REMOVE_PARENS( message ), ##__VA_ARGS__ )

// pipe both to debug level
#define LogInfo( message, ... ) OSAL_LOGD( LIBRARY_LOG_NAME, REMOVE_PARENS( message ), ##__VA_ARGS__ )
#define LogDebug( message, ... ) OSAL_LOGD( LIBRARY_LOG_NAME, REMOVE_PARENS( message ), ##__VA_ARGS__ )
