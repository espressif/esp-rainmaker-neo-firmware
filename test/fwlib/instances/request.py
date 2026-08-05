# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from threading import Condition, Lock
from typing import Callable, Literal, Optional
from pathlib import Path

from esptool import ESPLoader

from util.esp_helpers import (
    EspPortManager,
    NETWORK_TYPE,
    ESP_WIFI_SSID,
    ESP_WIFI_PASSWORD,
    ESP_THREAD_ACTIVE_DATASET,
    NetworkCredentialsWifi,
    NetworkCredentialsThread,
)
from util.factory_config import FactoryConfigFactory
from resource_pool import ResourcePool
from .base import (
    FirmwareInstance,
    FirmwareInstanceFactoryOta,
    FirmwareInstanceFactoryHostCtrl,
)
from .esp import (
    FirmwareInstanceEsp,
    FirmwareInstanceFactoryEsp,
    FirmwareInstanceFactoryEspHostCtrl,
    FirmwareInstanceFactoryEspOta,
)
from .posix import (
    FirmwareInstancePosix,
    FirmwareInstanceFactoryPosix,
    FirmwareInstanceFactoryPosixHostCtrl,
    FirmwareInstanceFactoryPosixOta,
    merge_posix_factory_coverage_reports,
)

REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT = "host_ctrl_default"
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_NO_BASIC_INGEST = "host_ctrl_no_basic_ingest"
# Local control split into SEC1 / SEC2 build variants so itests can cover both
# security versions (see ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_{1,2} in Kconfig).
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC1 = "host_ctrl_local_ctrl_sec1"
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC2 = "host_ctrl_local_ctrl_sec2"
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC1 = (
    "host_ctrl_lc_chal_resp_sec1"
)
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC2 = (
    "host_ctrl_lc_chal_resp_sec2"
)
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_ON_CHAL_RESP = "host_ctrl_on_chal_resp"
REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE = "host_ctrl_bridge"
REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR = "ota_mqtt_cbor"
REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY = (
    "ota_mqtt_cbor_no_sig_verify"
)
REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_JSON = "ota_mqtt_json"
REQUEST_FIRMWARE_INSTANCE_TYPES = Literal[
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_NO_BASIC_INGEST,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC1,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC2,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC1,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC2,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_ON_CHAL_RESP,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE,
    REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR,
    REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY,
    REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_JSON,
]


class FirmwareInstanceRequest:
    """
    A class that represents a request for a firmware instance.
    """

    # Targets that support WiFi
    TARGETS_SUPPORT_WIFI = [
        "esp32",
        "esp8266",
        "esp32c2",
        "esp32c3",
        "esp32c5",
        "esp32c6",
        "esp32c61",
        "esp32s2",
        "esp32s3",
        "posix",
    ]
    # Targets that support Thread
    TARGETS_SUPPORT_THREAD = [
        "esp32c5",
        "esp32c6",
        "esp32h2",
    ]

    def __init__(
        self,
        instance_type: REQUEST_FIRMWARE_INSTANCE_TYPES,
        target: str,
        network_type: NETWORK_TYPE,
    ):
        self.instance_type = instance_type
        self.target = target
        self.network_type = network_type
        if not self.supports_wifi() and not self.supports_thread():
            raise ValueError(
                f"Target {self.target} does not support {self.network_type}"
            )

    def is_ota(self) -> bool:
        """
        Return True if the request is for an OTA firmware instance, False otherwise.
        """
        return self.instance_type in [
            REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR,
            REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY,
            REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_JSON,
        ]

    def get_ota_type(self) -> FirmwareInstanceFactoryOta.OTA_TYPES:
        """
        Return the OTA type of the request.
        """
        if self.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR:
            return FirmwareInstanceFactoryOta.OTA_TYPE_MQTT_CBOR
        elif (
            self.instance_type
            == REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY
        ):
            return FirmwareInstanceFactoryOta.OTA_TYPE_MQTT_CBOR_NO_SIG_VERIFY
        elif self.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_JSON:
            return FirmwareInstanceFactoryOta.OTA_TYPE_MQTT_JSON
        else:
            raise ValueError(f"Invalid OTA type: {self.instance_type}")

    def is_host_ctrl(self) -> bool:
        """
        Return True if the request is for a host_ctrl firmware instance, False otherwise.
        """
        return self.instance_type in [
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_NO_BASIC_INGEST,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC1,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC2,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC1,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC2,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_ON_CHAL_RESP,
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE,
        ]

    def get_host_ctrl_request_type(
        self,
    ) -> FirmwareInstanceFactoryHostCtrl.REQUEST_TYPES:
        """
        Return the host_ctrl request type for this request.
        """
        if self.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT:
            return FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_DEFAULT
        elif (
            self.instance_type
            == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_NO_BASIC_INGEST
        ):
            return FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_NO_BASIC_INGEST
        elif (
            self.instance_type
            == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC1
        ):
            return FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_LOCAL_CTRL_SEC1
        elif (
            self.instance_type
            == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC2
        ):
            return FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_LOCAL_CTRL_SEC2
        elif (
            self.instance_type
            == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC1
        ):
            return (
                FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC1
            )
        elif (
            self.instance_type
            == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC2
        ):
            return (
                FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC2
            )
        elif (
            self.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_ON_CHAL_RESP
        ):
            return FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_ON_NETWORK_CHAL_RESP
        elif self.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE:
            return FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_BRIDGE
        else:
            raise ValueError(f"Invalid host_ctrl instance type: {self.instance_type}")

    def is_esp(self) -> bool:
        """
        Return True if the request is for an ESP firmware instance, False otherwise.
        """
        return self.target.strip()[:3] == "esp"

    def is_posix(self) -> bool:
        """
        Return True if the request is for a POSIX firmware instance, False otherwise.
        """
        return self.target == "posix"

    def supports_wifi(self) -> bool:
        """
        Return True if the request supports WiFi, False otherwise.
        """
        return (
            self.target in FirmwareInstanceRequest.TARGETS_SUPPORT_WIFI
            and self.network_type == "wifi"
        )

    def supports_thread(self) -> bool:
        """
        Return True if the request supports Thread, False otherwise.
        """
        return (
            self.target in FirmwareInstanceRequest.TARGETS_SUPPORT_THREAD
            and self.network_type == "thread"
        )

    def to_key(self) -> str:
        """
        Return the key of the request.
        """
        return f"{self.instance_type}-{self.target}-{self.network_type}"

    @staticmethod
    def from_key(key: str) -> "FirmwareInstanceRequest":
        """
        Return a firmware instance request from a key.
        """
        key_split = key.split("-")
        if len(key_split) != 3:
            raise ValueError(f"Invalid key: {key}")
        return FirmwareInstanceRequest(
            instance_type=key_split[0],
            target=key_split[1],
            network_type=key_split[2],
        )

    @staticmethod
    def from_target(
        target: str, instance_type: REQUEST_FIRMWARE_INSTANCE_TYPES
    ) -> list["FirmwareInstanceRequest"]:
        """
        Return a list of firmware instance requests for the target.
        """
        requests = []
        if target in FirmwareInstanceRequest.TARGETS_SUPPORT_WIFI:
            requests.append(FirmwareInstanceRequest(instance_type, target, "wifi"))
        if target in FirmwareInstanceRequest.TARGETS_SUPPORT_THREAD:
            requests.append(FirmwareInstanceRequest(instance_type, target, "thread"))
        return requests


class TagLog:
    """
    A class that represents a tag and log.
    """

    def __init__(self, tag: str):
        self.tag = tag

    def _log(self, message: str) -> None:
        """
        Log a message.
        """
        print(f"\033[1;33m[{self.tag}]\033[0m {message}")


class FirmwareInstancePool(TagLog):
    """
    A class that represents a pool of firmware instances.
    """

    def __init__(self, tag: str):
        super().__init__(f"Firmware Instance Pool: {tag}")

    def acquire(self) -> FirmwareInstance:
        """
        {abstract}
        Acquire a firmware instance from the pool.
        """
        raise NotImplementedError("acquire() is not implemented")

    def release(self, instance: FirmwareInstance) -> None:
        """
        {abstract}
        Release a firmware instance to the pool.
        """
        raise NotImplementedError("release() is not implemented")

    def destroy(self) -> None:
        """
        {abstract}
        Destroy the pool.
        """
        raise NotImplementedError("destroy() is not implemented")


class EspPortState:
    """
    A class that represents an ESP port.
    """

    def __init__(self, port: str):
        self.port = port
        self.instance = None

    def mark_used(self, instance: FirmwareInstanceEsp):
        """
        Mark the port as used by the given instance.
        """
        self.instance = instance

    def is_used(self) -> bool:
        """
        Check if the port is used.
        """
        return self.instance is not None

    def matches_tag(self, tag: str) -> bool:
        """
        Check if the port matches the tag.
        """
        return self.instance is not None and self.instance.get_tag() == tag

    def get_instance(self) -> Optional[FirmwareInstanceEsp]:
        """
        Get the instance on the port.
        """
        return self.instance

    def cannibalize(self) -> None:
        """
        Cannibalize the port.
        """
        if self.instance is not None:
            self.instance.destroy()
            self.instance = None

    def __repr__(self) -> str:
        return f"EspPortState(port={self.port}, instance_tag={self.instance.get_tag() if self.instance is not None else 'None'})"


class EspPortPool(TagLog):
    """
    A class that represents a pool of ESP ports.
    """

    def __init__(self, target_ports_map: dict[str, list[str]]):
        """
        target_ports_map: A map of target to list of ports.
        conditions: Conditions for various ESP targets.
        """
        super().__init__("ESP Port Pool")
        self.target_to_ports = {}
        for target, ports in target_ports_map.items():
            # Initialize the ports queues
            if not isinstance(ports, list):
                raise ValueError(f"Ports for target {target} must be a list")
            if len(ports) == 0:
                raise ValueError(f"Ports for target {target} must not be empty")
            self.target_to_ports[target] = (
                Condition(),
                [EspPortState(port) for port in ports],
            )

    def get_all_targets(self) -> list[str]:
        """
        Get all the targets in the port pool.
        """
        return list(self.target_to_ports.keys())

    def get_next_port(self, target: str, tag: str) -> EspPortState:
        """
        Get the next port from the pool with the following priority:
        1. Matching tag
        2. Unused port
        3. Used port; underlying instance will be destroyed
        Will block until a port is available. Returns the port state.
        """
        condition, ports = self.target_to_ports[target]
        with condition:
            while len(ports) == 0:
                condition.wait()

            last_unused_i = None
            for i, port in enumerate(ports):
                if port.matches_tag(tag):
                    # Found a port marked with the given tag, return it
                    return ports.pop(i)
                if not port.is_used():
                    # Save the index of the last unmarked port
                    last_unused_i = i

            if last_unused_i is not None:
                # Return the last unused port
                return ports.pop(last_unused_i)
            else:
                # No unmarked ports found, return the first port
                port = ports.pop(0)
                port.cannibalize()
                return port

    def release_port(self, target: str, port: EspPortState) -> None:
        """
        Release a port state to the pool.
        """
        condition, ports = self.target_to_ports[target]
        with condition:
            ports.append(port)
            condition.notify_all()
        self._log(f"Released port {port} for target {target}")

    def destroy(self) -> None:
        """
        Destroy the pool.
        """
        for condition, ports in self.target_to_ports.values():
            with condition:
                for port in ports:
                    port.cannibalize()


class FirmwareInstancePoolEsp(FirmwareInstancePool):
    """
    A class that represents a pool of ESP firmware instances.
    """

    def __init__(
        self,
        instance_factory: FirmwareInstanceFactoryEsp,
        port_pool: EspPortPool,
    ):
        super().__init__("ESP")
        self.target = instance_factory.target
        self.instance_tag = instance_factory.get_tag()
        self.instance_factory = instance_factory
        self.port_pool = port_pool

    def acquire(self) -> FirmwareInstanceEsp:
        self._log("Acquiring instance from pool")
        # Get the next port state (blocking)
        port_state = self.port_pool.get_next_port(self.target, self.instance_tag)
        instance = port_state.get_instance()
        if instance is None:
            instance = self.instance_factory.get_next_instance(port_state.port)
        self._log(f"Acquired instance: {instance} on port {port_state.port}")
        return instance

    def release(self, instance: FirmwareInstanceEsp) -> None:
        self._log(f"Releasing instance: {instance}")
        port_state = EspPortState(instance.port)
        port_state.mark_used(instance)
        self.port_pool.release_port(self.target, port_state)
        self._log(f"Released instance: {instance} on port {port_state.port}")

    def destroy(self) -> None:
        """
        Destroy the pool. Only the EspPortPool should be destroyed.
        """
        pass


class FirmwareInstancePoolPosix(FirmwareInstancePool):
    """
    A class that represents a pool of POSIX firmware instances.
    """

    def __init__(
        self,
        instance_factory: FirmwareInstanceFactoryPosix,
    ):
        super().__init__("POSIX")
        self.firmware_pool = ResourcePool(
            lambda: self._init_instance(),
            lambda instance: self._reset_instance(instance),
            lambda instance: self._destroy_instance(instance),
        )
        self.instance_factory = instance_factory

    def _init_instance(self) -> FirmwareInstancePosix:
        return self.instance_factory.get_next_instance()

    def _reset_instance(self, instance: FirmwareInstancePosix) -> None:
        # do nothing
        pass

    def _destroy_instance(self, instance: FirmwareInstancePosix) -> None:
        instance.destroy()

    def acquire(self) -> FirmwareInstancePosix:
        self._log("Acquiring instance from pool")
        instance = self.firmware_pool.acquire()
        self._log(f"Acquired instance: {instance}")
        return instance

    def release(self, instance: FirmwareInstancePosix) -> None:
        self._log(f"Releasing instance: {instance}")
        self.firmware_pool.release(instance)
        self._log(f"Released instance: {instance} to pool")

    def destroy(self) -> None:
        """
        Destroy the pool.
        """
        self.firmware_pool.drain_free()


class FirmwareInstanceDispatcher(TagLog):
    """
    A class that dispatches firmware instances based on incoming requests, and manages instance returns.
    """

    def __init__(
        self,
        tag: str,
        factory_config_factory: FactoryConfigFactory,
    ):
        super().__init__(f"Dispatcher: {tag}")
        self.factory_config_factory = factory_config_factory
        self.pools = {}
        self.pools_lock = Lock()

    def _init_pool(self, request: FirmwareInstanceRequest) -> FirmwareInstancePool:
        """
        {abstract}
        Initialize the pool for the request.
        """
        raise NotImplementedError("_init_pool() is not implemented")

    def _get_pool(self, request: FirmwareInstanceRequest) -> FirmwareInstancePool:
        """
        Get the pool for the request.
        """
        request_key = request.to_key()
        with self.pools_lock:
            if request_key not in self.pools:
                self.pools[request_key] = self._init_pool(request)
            return self.pools[request_key]

    def dispatch(self, request: FirmwareInstanceRequest) -> FirmwareInstance:
        """
        Dispatch the firmware instance based on the request.
        Returns the firmware instance.
        """
        self._log(f"Dispatching request: {request.to_key()}")
        instance = self._get_pool(request).acquire()
        self._log(f"Acquired instance: {instance} for request: {request.to_key()}")
        return instance

    def return_instance(
        self, request: FirmwareInstanceRequest, instance: FirmwareInstance
    ) -> None:
        """
        Return the firmware instance.
        """
        self._log(f"Returning instance: {instance} for request: {request.to_key()}")
        self._get_pool(request).release(instance)
        self._log(f"Released instance: {instance} for request: {request.to_key()}")

    def build_version_binary_if_not_built(
        self, request: FirmwareInstanceRequest, version_str: str
    ) -> Path:
        """
        Build a version binary if it is not built.
        """
        self._log(
            f"Building version binary for request: {request.to_key()} with version: {version_str}"
        )
        bin_path = self._get_pool(
            request
        ).instance_factory.build_version_binary_if_not_built(version_str)
        self._log(
            f"Built version binary for request: {request.to_key()} with version: {version_str} @ {bin_path}"
        )
        return bin_path

    def destroy(self) -> None:
        """
        Destroy the dispatcher.
        """
        self._log("Destroying dispatcher")
        with self.pools_lock:
            for pool in self.pools.values():
                pool.destroy()

        self._log("Destroyed dispatcher")


class FirmwareInstanceDispatcherEsp(FirmwareInstanceDispatcher):
    """
    A class that dispatches ESP firmware instances based on incoming requests.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        port_pool: EspPortPool,
        factory_config_factory_no_codesign: Optional[FactoryConfigFactory] = None,
        factory_config_factory_bridge: Optional[FactoryConfigFactory] = None,
    ):
        """
        factory_config_factory: The factory configuration factory.
        port_pool: The port pool.
        factory_config_factory_no_codesign: Optional factory without codesign cert (for OTA with signature verify disabled).
        factory_config_factory_bridge: Optional factory for bridge host_ctrl instances (registers thing with capabilities=["bridge"]).
        """
        super().__init__("ESP", factory_config_factory)
        self.port_pool = port_pool
        self.factory_config_factory_no_codesign = factory_config_factory_no_codesign
        self.factory_config_factory_bridge = factory_config_factory_bridge

        # Initialize the network credentials
        self.wifi_credentials = None
        self.thread_credentials = None
        if ESP_WIFI_SSID and ESP_WIFI_PASSWORD:
            self.wifi_credentials = NetworkCredentialsWifi(
                ESP_WIFI_SSID, ESP_WIFI_PASSWORD
            )
        else:
            print(
                "WARN: ESP_WIFI_SSID and ESP_WIFI_PASSWORD must be set in the .env file, WiFi credentials will not be set"
            )
        if ESP_THREAD_ACTIVE_DATASET:
            self.thread_credentials = NetworkCredentialsThread(
                ESP_THREAD_ACTIVE_DATASET
            )
        else:
            print(
                "WARN: ESP_THREAD_ACTIVE_DATASET must be set in the .env file, Thread credentials will not be set"
            )
        if self.wifi_credentials is None and self.thread_credentials is None:
            raise ValueError("WiFi or Thread credentials must be set")

    def _init_pool(self, request: FirmwareInstanceRequest) -> FirmwareInstancePool:
        """
        Initialize the pool for the request.
        """
        if not request.is_esp():
            raise ValueError(f"Invalid request: {request.to_key()}")

        # Initialize the network credentials
        network_credentials = None
        if request.supports_wifi() and self.wifi_credentials is not None:
            network_credentials = self.wifi_credentials
        elif request.supports_thread() and self.thread_credentials is not None:
            network_credentials = self.thread_credentials
        else:
            raise ValueError(
                f"No network credentials found for request: {request.to_key()}"
            )

        instance_factory = None
        if request.is_ota():
            config_factory = self.factory_config_factory
            if (
                request.instance_type
                == REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY
                and self.factory_config_factory_no_codesign is not None
            ):
                config_factory = self.factory_config_factory_no_codesign
            instance_factory = FirmwareInstanceFactoryEspOta(
                factory_config_factory=config_factory,
                target=request.target,
                network_credentials=network_credentials,
                ota_type=request.get_ota_type(),
            )
        elif request.is_host_ctrl():
            host_ctrl_config_factory = self.factory_config_factory
            if (
                request.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE
                and self.factory_config_factory_bridge is not None
            ):
                host_ctrl_config_factory = self.factory_config_factory_bridge
            instance_factory = FirmwareInstanceFactoryEspHostCtrl(
                factory_config_factory=host_ctrl_config_factory,
                target=request.target,
                network_credentials=network_credentials,
                request_type=request.get_host_ctrl_request_type(),
            )
        else:
            raise ValueError(f"Invalid request: {request.to_key()}")
        return FirmwareInstancePoolEsp(
            instance_factory=instance_factory,
            port_pool=self.port_pool,
        )


class FirmwareInstanceDispatcherPosix(FirmwareInstanceDispatcher):
    """
    A class that dispatches POSIX firmware instances based on incoming requests.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        factory_config_factory_no_codesign: Optional[FactoryConfigFactory] = None,
        factory_config_factory_bridge: Optional[FactoryConfigFactory] = None,
    ):
        super().__init__("POSIX", factory_config_factory)
        self.factory_config_factory_no_codesign = factory_config_factory_no_codesign
        self.factory_config_factory_bridge = factory_config_factory_bridge

    def _init_pool(self, request: FirmwareInstanceRequest) -> FirmwareInstancePool:
        """
        Initialize the pool for the request.
        """
        if not request.is_posix():
            raise ValueError(f"Invalid request: {request.to_key()}")

        if request.is_ota():
            config_factory = self.factory_config_factory
            if (
                request.instance_type
                == REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY
                and self.factory_config_factory_no_codesign is not None
            ):
                config_factory = self.factory_config_factory_no_codesign
            instance_factory = FirmwareInstanceFactoryPosixOta(
                factory_config_factory=config_factory,
                ota_type=request.get_ota_type(),
            )
        elif request.is_host_ctrl():
            host_ctrl_config_factory = self.factory_config_factory
            if (
                request.instance_type == REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE
                and self.factory_config_factory_bridge is not None
            ):
                host_ctrl_config_factory = self.factory_config_factory_bridge
            instance_factory = FirmwareInstanceFactoryPosixHostCtrl(
                factory_config_factory=host_ctrl_config_factory,
                request_type=request.get_host_ctrl_request_type(),
            )
        else:
            raise ValueError(f"Invalid request: {request.to_key()}")
        return FirmwareInstancePoolPosix(instance_factory=instance_factory)

    def generate_coverage_reports(self) -> list[str]:
        factories: list[FirmwareInstanceFactoryPosix] = []
        for pool in self.pools.values():
            if not isinstance(pool, FirmwareInstancePoolPosix):
                continue
            instance_factory = pool.instance_factory
            if not isinstance(instance_factory, FirmwareInstanceFactoryPosix):
                continue
            factories.append(instance_factory)
        seen: set[int] = set()
        uniq: list[FirmwareInstanceFactoryPosix] = []
        for f in factories:
            if id(f) not in seen:
                seen.add(id(f))
                uniq.append(f)
        if not uniq:
            return []
        should_log = uniq[0].should_log
        merged = merge_posix_factory_coverage_reports(uniq, should_log=should_log)
        if merged:
            return [merged]
        coverage_reports: list[str] = []
        for instance_factory in uniq:
            coverage_report_path = instance_factory.generate_coverage_report()
            if coverage_report_path is None:
                continue
            coverage_reports.append(coverage_report_path)
        return coverage_reports


class FirmwareInstanceManager:
    """
    Overall manager for firmware instances.
    """

    def __init__(
        self,
        port_compatible_fn: Optional[Callable[[ESPLoader], bool]] = None,
        thing_name_prefix: str = "test_node",
        key_type: str = "ec",
        codesign_cert_path: Optional[Path] = None,
        test_esp: bool = True,
        test_posix: bool = True,
    ):
        """
        port_compatible_fn: A function to check if a port is compatible.
        host_ctrl_tests_count: The number of tests to run with FirmwareInstanceHostCtrl.
        ota_tests_count: The number of tests to run with FirmwareInstanceOta.
        test_esp: If False, skip ``EspPortManager`` / ``EspPortPool`` / ``FirmwareInstanceDispatcherEsp``.
        test_posix: If False, skip ``FirmwareInstanceDispatcherPosix``.
        """
        self.factory_config_factory = FactoryConfigFactory(
            thing_name_prefix=thing_name_prefix,
            key_type=key_type,
            codesign_cert_path=codesign_cert_path,
        )
        self.factory_config_factory_no_codesign = FactoryConfigFactory(
            thing_name_prefix=thing_name_prefix,
            key_type=key_type,
            codesign_cert_path=None,
        )
        # Bridge nodes register with capabilities=["bridge"] so the admin
        # registration path attaches the bridge IAM policy to the thing.
        self.factory_config_factory_bridge = FactoryConfigFactory(
            thing_name_prefix=thing_name_prefix,
            key_type=key_type,
            codesign_cert_path=codesign_cert_path,
            capabilities=["bridge"],
        )

        if test_esp:
            self._init_esp_port_pool(port_compatible_fn=port_compatible_fn)
            self.esp_dispatcher = FirmwareInstanceDispatcherEsp(
                factory_config_factory=self.factory_config_factory,
                port_pool=self.esp_port_pool,
                factory_config_factory_no_codesign=self.factory_config_factory_no_codesign,
                factory_config_factory_bridge=self.factory_config_factory_bridge,
            )
        else:
            self.esp_port_pool = None
            self.esp_dispatcher = None

        if test_posix:
            self.posix_dispatcher = FirmwareInstanceDispatcherPosix(
                factory_config_factory=self.factory_config_factory,
                factory_config_factory_no_codesign=self.factory_config_factory_no_codesign,
                factory_config_factory_bridge=self.factory_config_factory_bridge,
            )
        else:
            self.posix_dispatcher = None

    def _init_esp_port_pool(
        self,
        port_compatible_fn: Optional[Callable[[ESPLoader], bool]] = None,
    ):
        """
        Initialize the ESP port pool (calls ``EspPortManager.get_ports``).
        """
        target_to_ports = {}
        for target, port in EspPortManager.get_ports(
            port_compatible_fn=port_compatible_fn
        ):
            if target not in target_to_ports:
                target_to_ports[target] = []
            target_to_ports[target].append(port)

        self.esp_port_pool = EspPortPool(target_to_ports)

    def get_requests(
        self,
        instance_type: REQUEST_FIRMWARE_INSTANCE_TYPES,
        add_esp: bool = True,
        add_posix: bool = True,
    ) -> list[FirmwareInstanceRequest]:
        """
        Return a list of all the requests for that instance type.
        """
        requests = []
        if add_esp and self.esp_port_pool is not None:
            # Add the ESP requests
            for target in self.esp_port_pool.get_all_targets():
                requests.extend(
                    FirmwareInstanceRequest.from_target(target, instance_type)
                )
        if add_posix and self.posix_dispatcher is not None:
            # Add the POSIX requests
            requests.extend(FirmwareInstanceRequest.from_target("posix", instance_type))

        return requests

    def set_codesign_cert_path(self, codesign_cert_path: Path) -> None:
        """
        Set the codesign certificate path.
        """
        self.factory_config_factory.codesign_cert_path = codesign_cert_path

    def dispatch(self, request: FirmwareInstanceRequest) -> FirmwareInstance:
        """
        Dispatch the firmware instance based on the request.
        """
        if request.is_esp():
            if self.esp_dispatcher is None:
                raise RuntimeError("ESP firmware is disabled (e.g. pytest --no-esp)")
            return self.esp_dispatcher.dispatch(request)
        elif request.is_posix():
            if self.posix_dispatcher is None:
                raise RuntimeError(
                    "POSIX firmware is disabled (e.g. pytest --no-posix)"
                )
            return self.posix_dispatcher.dispatch(request)
        else:
            raise ValueError(
                f"Could not determine target type for request: {request.to_key()}"
            )

    def return_instance(
        self, request: FirmwareInstanceRequest, instance: FirmwareInstance
    ) -> None:
        """
        Return the firmware instance to the appropriate pool.
        """
        if request.is_esp():
            if self.esp_dispatcher is None:
                raise RuntimeError("ESP firmware is disabled (e.g. pytest --no-esp)")
            self.esp_dispatcher.return_instance(request, instance)
        elif request.is_posix():
            if self.posix_dispatcher is None:
                raise RuntimeError(
                    "POSIX firmware is disabled (e.g. pytest --no-posix)"
                )
            self.posix_dispatcher.return_instance(request, instance)
        else:
            raise ValueError(
                f"Could not determine target type for request: {request.to_key()}"
            )

    def build_version_binary_if_not_built(
        self, request: FirmwareInstanceRequest, version_str: str
    ) -> Path:
        """
        Build a version binary if it is not built.
        """
        if request.is_esp():
            if self.esp_dispatcher is None:
                raise RuntimeError("ESP firmware is disabled (e.g. pytest --no-esp)")
            return self.esp_dispatcher.build_version_binary_if_not_built(
                request, version_str
            )
        elif request.is_posix():
            if self.posix_dispatcher is None:
                raise RuntimeError(
                    "POSIX firmware is disabled (e.g. pytest --no-posix)"
                )
            return self.posix_dispatcher.build_version_binary_if_not_built(
                request, version_str
            )
        else:
            raise ValueError(
                f"Could not determine target type for request: {request.to_key()}"
            )

    def generate_coverage_reports(self) -> list[str]:
        """
        Generate POSIX coverage report(s).

        Each build tree keeps ``posix-build-coverage.json`` (merged run shards). The
        session normally returns one HTML path: a merged report under
        ``<cwd>/build/coverage_reports/posix-merged-coverage`` when several POSIX factories
        (e.g. device-sim and ota-sim) all used shard gcda; otherwise one report per
        factory. The list is empty when POSIX is disabled or reporting fails.
        """
        if not self.posix_dispatcher:
            return []
        return self.posix_dispatcher.generate_coverage_reports()

    def destroy(self) -> None:
        """
        Destroy the manager.
        """
        if self.esp_port_pool is not None:
            self.esp_port_pool.destroy()
        if self.esp_dispatcher is not None:
            self.esp_dispatcher.destroy()
        if self.posix_dispatcher is not None:
            self.posix_dispatcher.destroy()
