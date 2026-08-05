/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file network_transport_port.h
 * @brief Transport port contract - the single seam between the coreMQTT-Agent impl and the
 *        target-specific NetworkContext.
 *
 * The ESP and POSIX builds use different NetworkContext definitions (esp-aws-iot's esp-tls port
 * on ESP, rmng's mbedtls + BSD-socket port on POSIX). This header declares the port API the impl
 * uses; the implementations - the ONLY code that touches NetworkContext fields - live in the
 * per-target sources (port/esp/network_transport_port.c, port/posix/network_transport_port.c).
 * If the two NetworkContext structs ever drift, the break is confined to the affected target's
 * .c (POSIX host build + ESP target build both run in CI), not the impl.
 *
 * "network_transport.h" resolves per target via the include path (NetworkContext_t + the
 * xTlsConnect/xTlsDisconnect/vTlsSet*Timeout transport API shared by both ports).
 */
#ifndef NETWORK_TRANSPORT_PORT_H
#define NETWORK_TRANSPORT_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "network_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NetworkContext accessors (implemented per target). */
void vTlsContextSetEndpoint( NetworkContext_t *pxNetworkContext,
                             const char *pcHostname, int xPort, const char **pAlpnProtos );
void vTlsContextSetClientCredentials( NetworkContext_t *pxNetworkContext,
                                      const char *pcClientCert, uint32_t xClientCertSize,
                                      const char *pcClientKey, uint32_t xClientKeySize );
void vTlsContextSetDsData( NetworkContext_t *pxNetworkContext, void *pvDsData );

/** @brief Initialize the TLS context: clear the connection handle and create the guard mutex.
 *  @return true on success, false if the mutex could not be created. */
bool xTlsContextSemaphoreInit( NetworkContext_t *pxNetworkContext );
void vTlsContextSemaphoreDeinit( NetworkContext_t *pxNetworkContext );

/** @brief Whether a TLS connection is currently allocated on the context. */
bool xTlsContextIsConnected( NetworkContext_t *pxNetworkContext );
/** @brief Clear the TLS connection handle (after disconnect). */
void vTlsContextClear( NetworkContext_t *pxNetworkContext );

/* Transport interface used by the agent (implemented per target). On POSIX these are provided by
 * rmng's port/posix/network_transport.c; on ESP the port wraps esp-aws-iot's espTlsTransport*. */
int32_t iTlsTransportSend( NetworkContext_t *pxNetworkContext, const void *pvData, size_t uxDataLen );
int32_t iTlsTransportRecv( NetworkContext_t *pxNetworkContext, void *pvData, size_t uxDataLen );
int iTlsGetSocketFd( NetworkContext_t *pxNetworkContext );

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_TRANSPORT_PORT_H */
