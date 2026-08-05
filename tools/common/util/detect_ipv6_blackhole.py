#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Detect whether this host's IPv6 egress is black-holed for a given TCP endpoint.

Background: the POSIX coreMQTT transport resolves the broker with AF_UNSPEC and tries
addresses one at a time. If the broker publishes an IPv6 (AAAA) address that is returned
first, and the host's IPv6 path silently drops the SYN (black hole), connect() stalls for
the full OS SYN timeout (~60s) before falling back to IPv4. CI uses this probe to decide
whether to build firmware with CONFIG_OSAL_MQTT_CORE_FORCE_IPV4=y (pytest --force-ipv4).

Exit code:
  0  -> IPv6 is BLACK-HOLED for host:port (AAAA exists but the v6 connect timed out).
        Caller should force IPv4.
  1  -> No action needed: no AAAA record, or v6 connected, or v6 failed fast
        (unreachable/refused -> no stall risk).

Usage: detect_ipv6_blackhole.py <host> <port> [connect_timeout_seconds]
"""

from __future__ import annotations

import socket
import sys
import time


def is_ipv6_blackholed(host: str, port: int, timeout: float = 5.0) -> bool:
    try:
        infos = socket.getaddrinfo(host, port, socket.AF_INET6, socket.SOCK_STREAM)
    except socket.gaierror:
        infos = []
    if not infos:
        # No IPv6 address published -> the transport never tries v6 -> no stall.
        print(f"[ipv6-detect] {host}: no AAAA record; IPv4-only path, no stall risk")
        return False

    addr = infos[0][4]
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    t0 = time.monotonic()
    try:
        sock.connect(addr)
        print(
            f"[ipv6-detect] {host}: IPv6 connect succeeded ({addr[0]}); not black-holed"
        )
        return False
    except (socket.timeout, TimeoutError):
        elapsed = time.monotonic() - t0
        print(
            f"[ipv6-detect] {host}: IPv6 connect to {addr[0]} timed out after "
            f"{elapsed:.1f}s -> BLACK-HOLED"
        )
        return True
    except OSError as exc:
        # Fast failure (ENETUNREACH / ECONNREFUSED): connect() returns immediately,
        # so the transport falls back to IPv4 with no stall. No need to force IPv4.
        print(f"[ipv6-detect] {host}: IPv6 connect failed fast ({exc}); no stall risk")
        return False
    finally:
        sock.close()


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 1
    host = argv[1]
    port = int(argv[2])
    timeout = float(argv[3]) if len(argv) > 3 else 5.0
    return 0 if is_ipv6_blackholed(host, port, timeout) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
