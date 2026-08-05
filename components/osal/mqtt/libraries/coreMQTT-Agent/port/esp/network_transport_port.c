/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* ESP transport port: implements the network_transport_port contract against esp-aws-iot's
 * esp-tls NetworkContext. This is the only ESP code that touches NetworkContext fields. */

#include "network_transport_port.h"
#include "osal_semaphore.h"
#include "esp_tls.h"

/* NetworkContext accessors. */

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

/* Transport interface: esp-aws-iot exposes espTlsTransport{Send,Recv} and no socket-fd getter. */

int32_t iTlsTransportSend( NetworkContext_t *pxNetworkContext, const void *pvData, size_t uxDataLen )
{
    return espTlsTransportSend( pxNetworkContext, pvData, uxDataLen );
}

int32_t iTlsTransportRecv( NetworkContext_t *pxNetworkContext, void *pvData, size_t uxDataLen )
{
    return espTlsTransportRecv( pxNetworkContext, pvData, uxDataLen );
}

int iTlsGetSocketFd( NetworkContext_t *pxNetworkContext )
{
    int lSockFd = -1;

    if ( ( pxNetworkContext != NULL ) && ( pxNetworkContext->pxTls != NULL ) &&
            ( esp_tls_get_conn_sockfd( pxNetworkContext->pxTls, &lSockFd ) != ESP_OK ) ) {
        lSockFd = -1;
    }

    return lSockFd;
}
