# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Common utility functions for testing payloads.
"""


def extract_shadow_reported_state(shadow: dict) -> dict | None:
    """
    Extracts the reported state from a shadow.
    """
    if isinstance(shadow, dict) and "state" in shadow:
        state = shadow["state"]
        if "reported" in state and state["reported"]:
            return state["reported"]
    return None
