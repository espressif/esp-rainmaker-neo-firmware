# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Process-global build options for the test/ firmware builder.

These are set once from the pytest master (via the manager-server initializer,
``conftest.server_process_setup``) and read inside the build server process where
firmware is actually compiled. Mirrors ``build_job_slot.configure_max_concurrent_build_jobs``.
"""

from __future__ import annotations

_force_ipv4 = False


def configure_force_ipv4(enabled: bool) -> None:
    """Enable forcing IPv4-only in firmware builds (sets CONFIG_OSAL_MQTT_CORE_FORCE_IPV4=y).

    Set from the manager-server initializer before any build runs.
    """
    global _force_ipv4
    _force_ipv4 = bool(enabled)


def force_ipv4_enabled() -> bool:
    """True if firmware builds should force IPv4-only broker connections."""
    return _force_ipv4
