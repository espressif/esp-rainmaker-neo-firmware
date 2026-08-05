/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* POSIX transport port: implements the network_transport_port accessors against the POSIX
 * mbedtls + BSD-socket NetworkContext. This is the only POSIX code that touches NetworkContext
 * fields. The transport interface (iTlsTransportSend / iTlsTransportRecv / iTlsGetSocketFd) is
 * implemented natively by port/posix/network_transport.c. */

#include "network_transport_port.h"
#include "osal_semaphore.h"

void vTlsContextSetEndpoint( NetworkContext_t *pxNetworkContext,
                             const char *pcHostname, int xPort, const char **pAlpnProtos )
{
    pxNetworkContext->pcHostname = pcHostname;
    pxNetworkContext->xPort = xPort;
    pxNetworkContext->pAlpnProtos = pAlpnProtos;
}

void vTlsContextSetClientCredentials( NetworkContext_t *pxNetworkContext,
                                      const char *pcClientCert, uint32_t xClientCertSize,
                                      const char *pcClientKey, uint32_t xClientKeySize )
{
    pxNetworkContext->pcClientCert = pcClientCert;
    pxNetworkContext->pcClientCertSize = xClientCertSize;
    pxNetworkContext->pcClientKey = pcClientKey;
    pxNetworkContext->pcClientKeySize = xClientKeySize;
}

void vTlsContextSetDsData( NetworkContext_t *pxNetworkContext, void *pvDsData )
{
    pxNetworkContext->ds_data = pvDsData;
}

bool xTlsContextSemaphoreInit( NetworkContext_t *pxNetworkContext )
{
    pxNetworkContext->pxTls = NULL;
    pxNetworkContext->xTlsContextSemaphore = osal_semaphore_create_mutex();
    return ( pxNetworkContext->xTlsContextSemaphore != NULL );
}

void vTlsContextSemaphoreDeinit( NetworkContext_t *pxNetworkContext )
{
    if ( pxNetworkContext->xTlsContextSemaphore != NULL ) {
        osal_semaphore_delete( pxNetworkContext->xTlsContextSemaphore );
        pxNetworkContext->xTlsContextSemaphore = NULL;
    }
}

bool xTlsContextIsConnected( NetworkContext_t *pxNetworkContext )
{
    return ( pxNetworkContext->pxTls != NULL );
}

void vTlsContextClear( NetworkContext_t *pxNetworkContext )
{
    pxNetworkContext->pxTls = NULL;
}
