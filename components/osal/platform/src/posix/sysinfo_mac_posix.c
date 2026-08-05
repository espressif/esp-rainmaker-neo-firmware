/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sysinfo_mac_posix.c
 * @brief POSIX MAC address implementation.
 *
 * There is no "base" MAC on a host, so the lowest-numbered non-loopback interface that
 * reports a usable link-layer address stands in for one. Selecting on interface index rather
 * than taking whatever `getifaddrs()` happens to return first matters: a development host
 * carries bridges, tunnels and container interfaces in no particular order, and the answer
 * should not depend on which of them enumerates first.
 *
 * The link-layer socket address is spelled differently per family -- `sockaddr_ll`/AF_PACKET
 * on Linux, `sockaddr_dl`/AF_LINK on macOS and the BSDs -- so both are handled here.
 */

/* Includes **************************************************************/

/* Declarations */
#include "osal_sysinfo.h"

/* Standard C headers */
#include <string.h>

/* POSIX */
#include <net/if.h>
#include <ifaddrs.h>

#if defined(__linux__)
#include <netpacket/packet.h>
#else
#include <net/if_dl.h>
#endif

/* Public function definitions **********************************************************/

osal_err_t osal_sysinfo_get_base_mac(uint8_t *mac, size_t mac_len)
{
    if (!mac || mac_len < OSAL_MAC_ADDR_LEN) {
        return OSAL_ERR_INVALID_ARG;
    }

    struct ifaddrs *ifaddr = NULL;
    if (getifaddrs(&ifaddr) != 0) {
        return OSAL_ERR_FAIL;
    }

    uint8_t best_mac[OSAL_MAC_ADDR_LEN] = {0};
    unsigned int best_index = 0;
    for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL || (ifa->ifa_flags & IFF_LOOPBACK)) {
            continue;
        }

        const uint8_t *candidate = NULL;
#if defined(__linux__)
        if (ifa->ifa_addr->sa_family != AF_PACKET) {
            continue;
        }
        const struct sockaddr_ll *link_addr = (const struct sockaddr_ll *)(const void *)ifa->ifa_addr;
        if (link_addr->sll_halen != OSAL_MAC_ADDR_LEN) {
            continue;
        }
        candidate = (const uint8_t *)link_addr->sll_addr;
#else
        if (ifa->ifa_addr->sa_family != AF_LINK) {
            continue;
        }
        const struct sockaddr_dl *link_addr = (const struct sockaddr_dl *)(const void *)ifa->ifa_addr;
        if (link_addr->sdl_alen != OSAL_MAC_ADDR_LEN) {
            continue;
        }
        candidate = (const uint8_t *)LLADDR(link_addr);
#endif
        /* An all-zero address identifies nothing; placeholder interfaces report one. */
        static const uint8_t zero_mac[OSAL_MAC_ADDR_LEN] = {0};
        if (memcmp(candidate, zero_mac, OSAL_MAC_ADDR_LEN) == 0) {
            continue;
        }

        unsigned int index = if_nametoindex(ifa->ifa_name);
        if (index == 0) {
            continue;
        }
        if (best_index == 0 || index < best_index) {
            best_index = index;
            memcpy(best_mac, candidate, OSAL_MAC_ADDR_LEN);
        }
    }

    freeifaddrs(ifaddr);

    if (best_index == 0) {
        return OSAL_ERR_NOT_FOUND;
    }
    memcpy(mac, best_mac, OSAL_MAC_ADDR_LEN);
    return OSAL_ERR_OK;
}
