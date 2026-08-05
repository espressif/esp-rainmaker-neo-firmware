/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* POSIX + mbedTLS transport implementation using platform-common primitives. */

#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#include "network_transport.h"

#include "osal_log.h"
#include "osal_semaphore.h"
#include "osal_ticks.h"
#include "osal_task.h"

#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_ciphersuites.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>
#include <psa/crypto.h>

#include "osal_ca_bundle.h"

static const char *TAG = "osal_mqtt_transport";

struct TlsContext {
    mbedtls_net_context net_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt clicert;
    mbedtls_pk_context pkey;
    bool handshakeCompleted;
};

static Timeouts_t timeouts = { .connectionTimeoutMs = 4000, .sendTimeoutMs = 10000, .recvTimeoutMs = 2000 };

void vTlsSetConnectTimeout( uint16_t connectionTimeoutMs )
{
    timeouts.connectionTimeoutMs = connectionTimeoutMs;
}

void vTlsSetSendTimeout( uint16_t sendTimeoutMs )
{
    timeouts.sendTimeoutMs = sendTimeoutMs;
}

void vTlsSetRecvTimeout( uint16_t recvTimeoutMs )
{
    timeouts.recvTimeoutMs = recvTimeoutMs;
}

/*
 * Establish a TCP connection with a bounded per-address timeout.
 *
 * The vendored mbedtls_net_connect() (mbedtls/library/net_sockets.c) resolves with
 * AF_UNSPEC and then calls a BLOCKING connect() with no timeout, trying each returned
 * address in turn. On a host whose IPv6 egress is black-holed, the IPv6 address (which
 * getaddrinfo typically returns first) stalls until the OS SYN timeout (~60-120s) before
 * the loop falls back to IPv4. That single stall dwarfs every flag-wait in the posix
 * integration tests and shows up as ~60s MQTT reconnects. We cannot patch the vendored
 * mbedTLS source (it is synced from upstream), so we do our own non-blocking connect
 * here, bounding each address attempt to connectionTimeoutMs, and hand the connected fd
 * to mbedTLS exactly as mbedtls_net_connect() would. A dead address now fails fast and
 * we move on to the next one instead of blocking.
 */
static int prvTcpConnectWithTimeout( mbedtls_net_context *pxNetCtx,
                                     const char *pcHost,
                                     const char *pcPort,
                                     uint32_t timeoutMs )
{
    struct addrinfo hints;
    struct addrinfo *pxAddrList = NULL;
    struct addrinfo *pxCur = NULL;
    int xResult = MBEDTLS_ERR_NET_UNKNOWN_HOST;

    memset( &hints, 0, sizeof( hints ) );
#if defined( CONFIG_OSAL_MQTT_CORE_FORCE_IPV4 ) && CONFIG_OSAL_MQTT_CORE_FORCE_IPV4
    /* Skip IPv6 (AAAA) entirely: on hosts with black-holed IPv6 egress an IPv6 address
     * tried first would stall connect() for the full OS SYN timeout before falling back. */
    hints.ai_family = AF_INET;
#else
    hints.ai_family = AF_UNSPEC;
#endif
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    if ( getaddrinfo( pcHost, pcPort, &hints, &pxAddrList ) != 0 ) {
        return MBEDTLS_ERR_NET_UNKNOWN_HOST;
    }

    for ( pxCur = pxAddrList; pxCur != NULL; pxCur = pxCur->ai_next ) {
        int fd = socket( pxCur->ai_family, pxCur->ai_socktype, pxCur->ai_protocol );
        if ( fd < 0 ) {
            xResult = MBEDTLS_ERR_NET_SOCKET_FAILED;
            continue;
        }

        int flags = fcntl( fd, F_GETFL, 0 );
        if ( flags < 0 || fcntl( fd, F_SETFL, flags | O_NONBLOCK ) < 0 ) {
            close( fd );
            xResult = MBEDTLS_ERR_NET_CONNECT_FAILED;
            continue;
        }

        int rc = connect( fd, pxCur->ai_addr, pxCur->ai_addrlen );
        if ( rc != 0 ) {
            if ( errno != EINPROGRESS ) {
                close( fd );
                xResult = MBEDTLS_ERR_NET_CONNECT_FAILED;
                continue;
            }

            /* Connection in progress: wait up to timeoutMs for the socket to become
             * writable, then check SO_ERROR for the actual result. */
            fd_set xWriteSet;
            FD_ZERO( &xWriteSet );
            FD_SET( fd, &xWriteSet );
            struct timeval xTv;
            xTv.tv_sec = ( time_t )( timeoutMs / 1000U );
            xTv.tv_usec = ( suseconds_t )( ( timeoutMs % 1000U ) * 1000U );

            int sel = select( fd + 1, NULL, &xWriteSet, NULL, &xTv );
            if ( sel <= 0 ) {
                OSAL_LOGW( TAG, "TCP connect candidate timed out after %" PRIu32 " ms, trying next address", timeoutMs );
                close( fd );
                xResult = MBEDTLS_ERR_NET_CONNECT_FAILED;
                continue;
            }

            int soErr = 0;
            socklen_t soErrLen = sizeof( soErr );
            if ( getsockopt( fd, SOL_SOCKET, SO_ERROR, &soErr, &soErrLen ) < 0 || soErr != 0 ) {
                close( fd );
                xResult = MBEDTLS_ERR_NET_CONNECT_FAILED;
                continue;
            }
        }

        /* Connected. Restore the original (blocking) mode so the fd matches what
         * mbedtls_net_connect() would have returned; the caller sets non-blocking
         * explicitly right after via mbedtls_net_set_nonblock(). */
        (void) fcntl( fd, F_SETFL, flags );
        pxNetCtx->fd = fd;
        xResult = 0;
        break;
    }

    freeaddrinfo( pxAddrList );
    return xResult;
}

static void prv_mbedtls_init( NetworkContext_t *pxNetworkContext )
{
    psa_status_t psa_status = psa_crypto_init();
    if ( psa_status != PSA_SUCCESS ) {
        OSAL_LOGE( TAG, "Failed to initialize PSA crypto: %" PRId32, (int32_t)psa_status );
        return;
    }

    TlsContext_t *pxTls = ( TlsContext_t * ) malloc( sizeof( TlsContext_t ) );
    if ( pxTls == NULL ) {
        OSAL_LOGE( TAG, "Failed to allocate memory for TLS context" );
        return;
    }

    mbedtls_net_init( &pxTls->net_ctx );
    mbedtls_ssl_init( &pxTls->ssl_ctx );
    mbedtls_ssl_config_init( &pxTls->ssl_conf );
    mbedtls_x509_crt_init( &pxTls->cacert );
    mbedtls_x509_crt_init( &pxTls->clicert );
    mbedtls_pk_init( &pxTls->pkey );
    pxTls->handshakeCompleted = false;

    pxNetworkContext->pxTls = pxTls;
}

static void prv_mbedtls_free( NetworkContext_t *pxNetworkContext )
{
    TlsContext_t *pxTls = ( TlsContext_t * ) pxNetworkContext->pxTls;
    if ( pxTls == NULL ) {
        return;
    }

    mbedtls_ssl_free( &pxTls->ssl_ctx );
    mbedtls_ssl_config_free( &pxTls->ssl_conf );
    mbedtls_x509_crt_free( &pxTls->cacert );
    mbedtls_x509_crt_free( &pxTls->clicert );
    mbedtls_pk_free( &pxTls->pkey );
    mbedtls_net_free( &pxTls->net_ctx );
    free( pxTls );
    pxNetworkContext->pxTls = NULL;
}

static int prv_wait_on_fd( int fd, bool want_read, uint32_t timeout_ms )
{
    struct timeval tv;
    tv.tv_sec = (time_t)( timeout_ms / 1000 );
    tv.tv_usec = (suseconds_t)( ( timeout_ms % 1000 ) * 1000 );

    fd_set read_fds;
    fd_set write_fds;
    fd_set err_fds;
    FD_ZERO( &read_fds );
    FD_ZERO( &write_fds );
    FD_ZERO( &err_fds );
    FD_SET( fd, want_read ? &read_fds : &write_fds );
    FD_SET( fd, &err_fds );

    int ret = select( fd + 1,
                      want_read ? &read_fds : NULL,
                      want_read ? NULL : &write_fds,
                      &err_fds,
                      &tv );
    if ( ret <= 0 ) {
        return ret; /* 0 timeout, -1 error */
    }
    if ( FD_ISSET( fd, &err_fds ) ) {
        return -1;
    }
    return 1;
}

TlsTransportStatus_t xTlsConnect( NetworkContext_t *pxNetworkContext )
{
    if ( pxNetworkContext == NULL || pxNetworkContext->pcHostname == NULL || pxNetworkContext->xPort <= 0 ) {
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }

    if ( pxNetworkContext->xTlsContextSemaphore == NULL ) {
        pxNetworkContext->xTlsContextSemaphore = osal_semaphore_create_mutex();
        if ( pxNetworkContext->xTlsContextSemaphore == NULL ) {
            return TLS_TRANSPORT_INSUFFICIENT_MEMORY;
        }
    }

    osal_err_t sem_rc = osal_semaphore_take( pxNetworkContext->xTlsContextSemaphore,
                        OSAL_MAX_DELAY );
    if ( sem_rc != OSAL_ERR_OK ) {
        return TLS_TRANSPORT_INTERNAL_ERROR;
    }

    TlsTransportStatus_t xResult = TLS_TRANSPORT_CONNECT_FAILURE;
    int ret;
    const char *pers = "rmng-mqtt";

    prv_mbedtls_init( pxNetworkContext );

    if ( pxNetworkContext->pxTls == NULL ) {
        return TLS_TRANSPORT_INTERNAL_ERROR;
    }

    TlsContext_t *pxTls = pxNetworkContext->pxTls;
    const unsigned char *cacrt_start = NULL;
    const unsigned char *cacrt_end = NULL;
    osal_ca_bundle_get(&cacrt_start, &cacrt_end);
    size_t cacrt_all_pem_size = (size_t)(cacrt_end - cacrt_start);
    if (cacrt_all_pem_size > 0) {
        ret = mbedtls_x509_crt_parse( &pxTls->cacert,
                                      cacrt_start,
                                      cacrt_all_pem_size );
        if ( ret < 0 ) {
            OSAL_LOGE( TAG, "Failed parsing CA cert: -0x%04x", -ret );
            xResult = TLS_TRANSPORT_INVALID_CREDENTIALS;
            goto exit;
        }
    } else {
        OSAL_LOGE( TAG, "CA certificate bundle is empty" );
        xResult = TLS_TRANSPORT_INVALID_CREDENTIALS;
        goto exit;
    }

    if ( pxNetworkContext->pcClientCert != NULL && pxNetworkContext->pcClientCertSize > 0 &&
            pxNetworkContext->pcClientKey != NULL && pxNetworkContext->pcClientKeySize > 0 ) {
        ret = mbedtls_x509_crt_parse( &pxTls->clicert,
                                      (const unsigned char *) pxNetworkContext->pcClientCert,
                                      pxNetworkContext->pcClientCertSize );
        if ( ret < 0 ) {
            OSAL_LOGE( TAG, "Failed parsing client cert: -0x%04x", -ret );
            xResult = TLS_TRANSPORT_INVALID_CREDENTIALS;
            goto exit;
        }
        ret = mbedtls_pk_parse_key( &pxTls->pkey,
                                    (const unsigned char *) pxNetworkContext->pcClientKey,
                                    pxNetworkContext->pcClientKeySize,
                                    NULL, 0 );
        if ( ret < 0 ) {
            OSAL_LOGE( TAG, "Failed parsing client key: -0x%04x", -ret );
            xResult = TLS_TRANSPORT_INVALID_CREDENTIALS;
            goto exit;
        }
    }

    {
        char port_str[8];
        snprintf( port_str, sizeof( port_str ), "%d", pxNetworkContext->xPort );
        ret = prvTcpConnectWithTimeout( &pxTls->net_ctx,
                                        pxNetworkContext->pcHostname,
                                        port_str,
                                        timeouts.connectionTimeoutMs );
        if ( ret != 0 ) {
            OSAL_LOGE( TAG, "TCP connect failed: -0x%04x", -ret );
            xResult = TLS_TRANSPORT_CONNECT_FAILURE;
            goto exit;
        }
    }

    mbedtls_net_set_nonblock( &pxTls->net_ctx );

    if ( ( ret = mbedtls_ssl_config_defaults( &pxTls->ssl_conf,
                 MBEDTLS_SSL_IS_CLIENT,
                 MBEDTLS_SSL_TRANSPORT_STREAM,
                 MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 ) {
        OSAL_LOGE( TAG, "ssl_config_defaults failed: -0x%04x", -ret );
        goto exit;
    }

    mbedtls_ssl_conf_authmode( &pxTls->ssl_conf,
                               ( pxNetworkContext->pcServerRootCA != NULL && pxNetworkContext->pcServerRootCASize > 0 )
                               ? MBEDTLS_SSL_VERIFY_REQUIRED
                               : MBEDTLS_SSL_VERIFY_OPTIONAL );
    if ( pxNetworkContext->pcServerRootCA != NULL && pxNetworkContext->pcServerRootCASize > 0 ) {
        mbedtls_ssl_conf_ca_chain( &pxTls->ssl_conf, &pxTls->cacert, NULL );
    }
    if ( pxNetworkContext->pcClientCert != NULL && pxNetworkContext->pcClientCertSize > 0 &&
            pxNetworkContext->pcClientKey != NULL && pxNetworkContext->pcClientKeySize > 0 ) {
        if ( ( ret = mbedtls_ssl_conf_own_cert( &pxTls->ssl_conf,
                                                &pxTls->clicert,
                                                &pxTls->pkey ) ) != 0 ) {
            OSAL_LOGE( TAG, "ssl_conf_own_cert failed: -0x%04x", -ret );
            goto exit;
        }
    }

    if ( pxNetworkContext->pAlpnProtos != NULL ) {
        /* mbedTLS expects const char* array terminated by NULL. */
        mbedtls_ssl_conf_alpn_protocols( &pxTls->ssl_conf, pxNetworkContext->pAlpnProtos );
    }

    if ( ( ret = mbedtls_ssl_setup( &pxTls->ssl_ctx, &pxTls->ssl_conf ) ) != 0 ) {
        OSAL_LOGE( TAG, "ssl_setup failed: -0x%04x", -ret );
        goto exit;
    }

    if ( !pxNetworkContext->disableSni && pxNetworkContext->pcHostname != NULL ) {
        if ( ( ret = mbedtls_ssl_set_hostname( &pxTls->ssl_ctx, pxNetworkContext->pcHostname ) ) != 0 ) {
            OSAL_LOGE( TAG, "set_hostname failed: -0x%04x", -ret );
            goto exit;
        }
    }

    mbedtls_ssl_set_bio( &pxTls->ssl_ctx,
                         &pxTls->net_ctx,
                         mbedtls_net_send,
                         mbedtls_net_recv,
                         NULL );

    {
        osal_tick_type_t start = osal_task_get_tick_count();
        uint32_t deadline = timeouts.connectionTimeoutMs;
        do {
            ret = mbedtls_ssl_handshake( &pxTls->ssl_ctx );
            if ( ret == 0 ) {
                xResult = TLS_TRANSPORT_SUCCESS;
                pxTls->handshakeCompleted = true;
                break;
            }
            if ( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ) {
                bool want_read = ( ret == MBEDTLS_ERR_SSL_WANT_READ );
                uint32_t elapsed = osal_ms_from_ticks( osal_task_get_tick_count() - start );
                if ( elapsed >= deadline ) {
                    xResult = TLS_TRANSPORT_CONNECT_FAILURE;
                    break;
                }
                uint32_t remain = deadline - elapsed;
                int sel = prv_wait_on_fd( pxTls->net_ctx.fd, want_read, remain );
                if ( sel <= 0 ) {
                    xResult = TLS_TRANSPORT_CONNECT_FAILURE;
                    break;
                }
                continue;
            } else {
                OSAL_LOGE( TAG, "handshake failed: -0x%04x", -ret );
                xResult = TLS_TRANSPORT_HANDSHAKE_FAILED;
                break;
            }
        } while ( 1 );
    }

exit:
    osal_semaphore_give( pxNetworkContext->xTlsContextSemaphore );
    if ( xResult != TLS_TRANSPORT_SUCCESS ) {
        prv_mbedtls_free( pxNetworkContext );
    }
    return xResult;
}

TlsTransportStatus_t xTlsDisconnect( NetworkContext_t *pxNetworkContext )
{
    if ( pxNetworkContext == NULL ) {
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }

    if ( pxNetworkContext->pxTls == NULL ) {
        return TLS_TRANSPORT_INVALID_PARAMETER;
    }
    TlsContext_t *pxTls = pxNetworkContext->pxTls;

    if ( pxNetworkContext->xTlsContextSemaphore == NULL ) {
        return TLS_TRANSPORT_DISCONNECT_FAILURE;
    }
    if ( osal_semaphore_take( pxNetworkContext->xTlsContextSemaphore, OSAL_MAX_DELAY ) != OSAL_ERR_OK ) {
        return TLS_TRANSPORT_DISCONNECT_FAILURE;
    }

    /* Best-effort close notify if handshake was completed. */
    if ( pxTls->handshakeCompleted ) {
        (void) mbedtls_ssl_close_notify( &pxTls->ssl_ctx );
    }

    prv_mbedtls_free( pxNetworkContext );
    osal_semaphore_give( pxNetworkContext->xTlsContextSemaphore );
    return TLS_TRANSPORT_SUCCESS;
}

int32_t iTlsTransportSend( NetworkContext_t *pxNetworkContext,
                           const void *pvData, size_t uxDataLen )
{
    if ( pxNetworkContext == NULL || pvData == NULL || uxDataLen == 0 ) {
        return -1;
    }
    if ( pxNetworkContext->pxTls == NULL ) {
        return -1;
    }
    TlsContext_t *pxTls = pxNetworkContext->pxTls;

    if ( osal_semaphore_take( pxNetworkContext->xTlsContextSemaphore,
                              osal_ticks_from_ms( timeouts.sendTimeoutMs ) ) != OSAL_ERR_OK ) {
        return -1;
    }

    int32_t total_sent = 0;
    osal_tick_type_t start = osal_task_get_tick_count();

    while ( total_sent < (int32_t) uxDataLen ) {
        int ret = mbedtls_ssl_write( &pxTls->ssl_ctx,
                                     (const unsigned char *) ( (const uint8_t *) pvData + total_sent ),
                                     (size_t) ( uxDataLen - (size_t) total_sent ) );
        if ( ret > 0 ) {
            total_sent += ret;
            continue;
        }
        if ( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ) {
            uint32_t elapsed = osal_ms_from_ticks( osal_task_get_tick_count() - start );
            if ( elapsed >= timeouts.sendTimeoutMs ) {
                break;
            }
            uint32_t remain = timeouts.sendTimeoutMs - elapsed;
            if ( prv_wait_on_fd( pxTls->net_ctx.fd, false, remain ) <= 0 ) {
                break;
            }
            continue;
        }
        /* Hard error */
        total_sent = -1;
        break;
    }

    osal_semaphore_give( pxNetworkContext->xTlsContextSemaphore );
    return total_sent;
}

int iTlsGetSocketFd( NetworkContext_t *pxNetworkContext )
{
    if ( pxNetworkContext == NULL || pxNetworkContext->pxTls == NULL ) {
        return -1;
    }
    return pxNetworkContext->pxTls->net_ctx.fd;
}

int32_t iTlsTransportRecv( NetworkContext_t *pxNetworkContext,
                           void *pvData, size_t uxDataLen )
{
    if ( pxNetworkContext == NULL || pvData == NULL || uxDataLen == 0 ) {
        return -1;
    }
    if ( pxNetworkContext->pxTls == NULL ) {
        return -1;
    }
    TlsContext_t *pxTls = pxNetworkContext->pxTls;

    if ( osal_semaphore_take( pxNetworkContext->xTlsContextSemaphore,
                              osal_ticks_from_ms( timeouts.recvTimeoutMs ) ) != OSAL_ERR_OK ) {
        return -1;
    }

    int32_t bytes_read = 0;
    osal_tick_type_t start = osal_task_get_tick_count();

    do {
        int ret = mbedtls_ssl_read( &pxTls->ssl_ctx,
                                    (unsigned char *) pvData,
                                    (size_t) uxDataLen );
        if ( ret > 0 ) {
            bytes_read = ret;
            break;
        }
        if ( ret == 0 ) {
            /* Connection closed */
            bytes_read = -1;
            break;
        }
        if ( ret == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET ) {
            /* Ignore new session ticket.
             * This error code is experimental and may change with future mbedTLS versions.
             */
            continue;
        }

        if ( ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE ) {
            uint32_t elapsed = osal_ms_from_ticks( osal_task_get_tick_count() - start );
            if ( elapsed >= timeouts.recvTimeoutMs ) {
                /* timeout */
                bytes_read = 0;
                break;
            }
            uint32_t remain = timeouts.recvTimeoutMs - elapsed;
            if ( prv_wait_on_fd( pxTls->net_ctx.fd, true, remain ) <= 0 ) {
                bytes_read = 0;
                break;
            }
            continue;
        }
        /* Hard error */
        OSAL_LOGE( TAG, "mbedtls_ssl_read failed: -0x%04X", -ret );
        bytes_read = -1;
        break;
    } while ( 1 );

    osal_semaphore_give( pxNetworkContext->xTlsContextSemaphore );
    return bytes_read;
}
