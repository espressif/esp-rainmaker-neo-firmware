# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from .commands import CommunicationProtocol


class Globals:
    protocol = CommunicationProtocol()
