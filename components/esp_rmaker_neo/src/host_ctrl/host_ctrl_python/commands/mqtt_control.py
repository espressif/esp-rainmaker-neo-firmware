# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from ..commands import Command, CommunicationProtocol
from ..color import ColorFormatter


class CommandMqttControlForceNetworkFailure(Command):
    """
    Represents a command to force all network operations (connect, send, recv) to fail.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_mqtt_control}{protocol.mqtt_control_force_network_failure}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Forcing {ColorFormatter.ok_cyan('all network operations (connect, send, recv)')} to {ColorFormatter.error('fail')}"
        )


class CommandMqttControlRestoreNetworkDefault(Command):
    """
    Represents a command to restore default network operations (connect, send, recv) settings.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_mqtt_control}{protocol.mqtt_control_restore_network_default}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Restoring {ColorFormatter.ok_cyan('default network operations (connect, send, recv)')} settings"
        )


class CommandMqttControlForceOperationsFailure(Command):
    """
    Represents a command to force all MQTT operations (publish, subscribe, unsubscribe) to fail.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_mqtt_control}{protocol.mqtt_control_force_operations_failure}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Forcing {ColorFormatter.ok_cyan('all MQTT operations (publish, subscribe, unsubscribe)')} to {ColorFormatter.error('fail')}"
        )


class CommandMqttControlRestoreOperationsDefault(Command):
    """
    Represents a command to restore default MQTT operations (publish, subscribe, unsubscribe) settings.
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_mqtt_control}{protocol.mqtt_control_restore_operations_default}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(
            f"-> Command: Restoring {ColorFormatter.ok_cyan('default MQTT operations (publish, subscribe, unsubscribe)')} settings"
        )


class CommandMqttControlDisconnect(Command):
    """
    Represents a command to explicitly disconnect MQTT (calls esp_rmaker_mqtt_impl.disconnect).
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_mqtt_control}{protocol.mqtt_control_disconnect}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(f"-> Command: {ColorFormatter.ok_cyan('MQTT disconnect')}")


class CommandMqttControlConnect(Command):
    """
    Represents a command to explicitly (re)connect MQTT (calls esp_rmaker_mqtt_impl.connect).
    """

    def build(self, protocol: CommunicationProtocol) -> str:
        return f"{protocol.command_mqtt_control}{protocol.mqtt_control_connect}"

    def log(self, protocol: CommunicationProtocol) -> None:
        print(f"-> Command: {ColorFormatter.ok_cyan('MQTT connect')}")
