/*
 * SPDX-FileCopyrightText: 2020 Amazon.com, Inc. or its affiliates
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * @file core_agent_message.c
 * @brief Implements functions to interact with queues.
 *
 * Adapted from coreMQTT-Agent's freertos_agent_message.c.
 */

/* Standard includes. */
#include <string.h>
#include <stdio.h>

/* Kernel includes. */
#include "osal_queue.h"
#include "osal_semaphore.h"

/* Header include. */
#include "core_agent_message.h"
#include "core_mqtt_agent_message_interface.h"

/*-----------------------------------------------------------*/

bool Agent_MessageSend( MQTTAgentMessageContext_t *pMsgCtx,
                        MQTTAgentCommand_t *const *pCommandToSend,
                        uint32_t blockTimeMs )
{
    osal_err_t queueStatus = OSAL_ERR_FAIL;

    if ( ( pMsgCtx != NULL ) && ( pCommandToSend != NULL ) ) {
        queueStatus = osal_queue_send( pMsgCtx->queue, pCommandToSend, osal_ticks_from_ms( blockTimeMs ) );
    }

    return ( queueStatus == OSAL_ERR_OK ) ? true : false;
}

/*-----------------------------------------------------------*/

bool Agent_MessageReceive( MQTTAgentMessageContext_t *pMsgCtx,
                           MQTTAgentCommand_t **pReceivedCommand,
                           uint32_t blockTimeMs )
{
    osal_err_t queueStatus = OSAL_ERR_FAIL;

    if ( ( pMsgCtx != NULL ) && ( pReceivedCommand != NULL ) ) {
        queueStatus = osal_queue_receive( pMsgCtx->queue, pReceivedCommand, osal_ticks_from_ms( blockTimeMs ) );
    }

    return ( queueStatus == OSAL_ERR_OK ) ? true : false;
}
