# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from .base import (
    FirmwareInstanceFactory,
    FirmwareInstanceFactoryOta,
    FirmwareInstanceFactoryHostCtrl,
    FirmwareInstance,
    FirmwareInstanceHostCtrl,
    FirmwareInstanceOta,
    LoggingStream,
)
import json
import time

from util.esp_helpers import (
    EspFirmwareManager,
    NetworkCredentials,
)
from util.factory_config import FactoryConfigFactory, FactoryConfig
from rmng_backend import User
from typing import Optional
from host_ctrl_python.commands import PortManagerMultiplexed
from host_ctrl_python.globals import Globals as HostCtrlGlobals
from ..filepaths import (
    RMNG_SDK_DEVICE_SIM_ESP_DIR,
    RMNG_SDK_OTA_SIM_ESP_DIR,
    RMNG_SDK_DEVICE_SIM_ESP_BINARY_NAME,
    RMNG_SDK_OTA_SIM_ESP_BINARY_NAME,
    RMNG_SDK_GITIGNORE_FILE,
)
from pathlib import Path
import re
from typing import Literal
from esptool.cmds import reset_chip, detect_chip

# --- Firmware instance factory ---


class FirmwareInstanceFactoryEsp(FirmwareInstanceFactory):
    """
    A class that represents a factory for ESP firmware instances.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        project_dir: Path,
        monitor_ready_pattern: re.Pattern,
        target: str,
        network_credentials: NetworkCredentials,
        binary_name: str,
        should_log: bool = True,
    ):
        # used by get_id(), must be set before super().__init__()
        self.target = target
        self.network_credentials = network_credentials

        FirmwareInstanceFactory.__init__(
            self, factory_config_factory=factory_config_factory, should_log=should_log
        )
        self.monitor_ready_pattern = monitor_ready_pattern
        self.fw_manager = EspFirmwareManager(
            target=target,
            project_dir=project_dir,
            gitignore_file=RMNG_SDK_GITIGNORE_FILE,
        )
        self.partition_info = self.fw_manager.partition_info
        self.build_dir = self._get_build_dir()
        self.baudrate = 115200
        self.network_credentials = network_credentials
        self.binary_name = binary_name

    def reset_nvs(self, port: str, reset_after: bool = True):
        """
        Reset the NVS partition.
        This corresponds to an erase of the NVS partition.
        """
        return self.fw_manager.reset_nvs(port, reset_after=reset_after)

    def reset_nvs_with_network_credentials(self, port: str, reset_after: bool = True):
        """
        Reset the NVS partition with the CSV file.
        If reset_after is True, the ESP will be reset after the reset.
        """

        nvs_info = self.fw_manager.partition_info.get("nvs")
        if nvs_info is None:
            raise RuntimeError("NVS partition not found in partition info")

        # Flash the NVS binary
        self.fw_manager.flash_partition(
            port,
            self.network_credentials.get_nvs_binary_path(nvs_info.size),
            nvs_info,
            reset_after=reset_after,
        )
        print(
            f"Network credentials flashed successfully for '{self.network_credentials.network_type}'"
        )

    def get_id(self) -> str:
        """
        Get the ID of the firmware.
        """
        return f"firmware-{self.target}-{self.get_tag()}"

    def get_tag(self) -> str:
        """
        {abstract}
        Get the tag of the firmware.
        Should include the network type.
        """
        raise NotImplementedError("get_tag() is not implemented")

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        ret = super()._get_add_configs()
        ret.update(self.network_credentials.get_add_configs())
        return ret

    def build(self, add_configs: dict[str, str | int] = {}):
        """
        Build the underlying firmware.
        """
        # Ensure fresh directory
        self._remake_build_dir(self.build_dir)

        # Build and flash the target
        target_config = {
            **self._get_add_configs(),
            **add_configs,
        }
        self.fw_manager.build(
            build_dir=self.build_dir,
            add_configs=target_config,
            use_temp_project_dir=True,
        )

        # Get the baudrate from the build directory
        project_description_path = self.build_dir / "project_description.json"
        if not project_description_path.exists():
            raise RuntimeError(
                f"Project description file not found at {project_description_path}"
            )
        with open(project_description_path, "r") as f:
            project_description = json.load(f)
            self.baudrate = int(project_description.get("monitor_baud", 115200))

    def build_version_binary_if_not_built(self, version_str: str) -> Path:
        """
        Build a binary with a specific firmware version if it is not built.
        Returns the path to the binary.
        """
        build_dir = self._get_build_dir(version_str)
        with self._get_build_dir_lock(build_dir):
            if self._is_built_locked(build_dir):
                return build_dir / self.binary_name

            # Ensure fresh directory
            self._remake_build_dir(build_dir)

            # Force IDF to use project version from config
            add_configs = {
                **self._get_add_configs(),
                "APP_PROJECT_VER_FROM_CONFIG": "y",
                "APP_PROJECT_VER": version_str,
            }
            # Use a temporary project directory for thread safe build
            self.fw_manager.build(
                build_dir, add_configs=add_configs, use_temp_project_dir=True
            )

            # Mark the build directory as built
            self._mark_as_built_locked(build_dir)

            # Return the path to the binary
            return build_dir / self.binary_name

    def _flash_port(self, port: str) -> None:
        """
        Flash the firmware to the port.
        """
        # Flash the firmware
        self.fw_manager.flash(port, self.build_dir)
        # Reset the NVS partition with the network credentials
        self.reset_nvs_with_network_credentials(port, reset_after=False)

    def destroy(self) -> None:
        """
        Destroy the factory.
        """
        if self.network_credentials is not None:
            self.network_credentials.destroy()


class FirmwareInstanceFactoryEspHostCtrl(
    FirmwareInstanceFactoryEsp, FirmwareInstanceFactoryHostCtrl
):
    """
    A class that represents a factory for ESP firmware instances with a host_ctrl.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        target: str,
        network_credentials: NetworkCredentials,
        request_type: FirmwareInstanceFactoryHostCtrl.REQUEST_TYPES = FirmwareInstanceFactoryHostCtrl.REQUEST_TYPE_DEFAULT,
        should_log: bool = True,
    ):
        self._set_request_type(request_type)
        super().__init__(
            factory_config_factory=factory_config_factory,
            project_dir=RMNG_SDK_DEVICE_SIM_ESP_DIR,
            monitor_ready_pattern=r"device-sim ready",
            target=target,
            network_credentials=network_credentials,
            binary_name=RMNG_SDK_DEVICE_SIM_ESP_BINARY_NAME,
            should_log=should_log,
        )

    def get_tag(self) -> str:
        """
        Get the tag of the firmware.
        """
        return f"host_ctrl-{self.network_credentials.network_type}-{self.request_type}"

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        ret = FirmwareInstanceFactoryEsp._get_add_configs(self)
        ret.update(FirmwareInstanceFactoryHostCtrl._get_add_configs(self))
        return ret

    def get_next_instance(self, port: str, **kwargs) -> "FirmwareInstanceEspHostCtrl":
        """
        Get the next firmware instance.
        """
        # Build the firmware if it is not built
        self._build_if_not_built()

        # Get the factory config
        factory_config = self.factory_config_factory.create()
        factory_config.read_factory_config(self.build_dir / "config" / "sdkconfig.h")

        # Flash the firmware and return the instance
        self._flash_port(port)
        return FirmwareInstanceEspHostCtrl(
            tag=self.get_tag(),
            factory_config=factory_config,
            fw_manager=self.fw_manager,
            port=port,
            baudrate=self.baudrate,
            monitor_ready_pattern=self.monitor_ready_pattern,
            should_log=self.should_log,
        )


class FirmwareInstanceFactoryEspOta(
    FirmwareInstanceFactoryEsp, FirmwareInstanceFactoryOta
):
    """
    A class that represents a factory for ESP firmware instances for OTA testing.
    """

    def __init__(
        self,
        factory_config_factory: FactoryConfigFactory,
        target: str,
        network_credentials: NetworkCredentials,
        ota_type: FirmwareInstanceFactoryOta.OTA_TYPES,
        should_log: bool = True,
    ):
        self.target = target
        self._set_ota_type(ota_type)
        FirmwareInstanceFactoryEsp.__init__(
            self,
            factory_config_factory=factory_config_factory,
            project_dir=RMNG_SDK_OTA_SIM_ESP_DIR,
            monitor_ready_pattern=r"ota-sim ready",
            target=target,
            network_credentials=network_credentials,
            binary_name=RMNG_SDK_OTA_SIM_ESP_BINARY_NAME,
            should_log=should_log,
        )

    def get_tag(self) -> str:
        """
        Get the tag of the firmware.
        """
        return f"ota-{self.network_credentials.network_type}-{self.ota_type}"

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        ret = FirmwareInstanceFactoryEsp._get_add_configs(self)
        ret.update(FirmwareInstanceFactoryOta._get_add_configs(self))
        return ret

    def build(self, add_configs: dict[str, str | int] = {}):
        """
        Build the firmware instance.
        """
        super().build(add_configs=add_configs)

        # Get path to the original binary
        self.base_binary_path = self.build_dir / self.binary_name
        if not self.base_binary_path.exists():
            raise RuntimeError(
                f"OTA binary path {self.base_binary_path} does not exist"
            )

    def get_next_instance(self, port: str, **kwargs) -> "FirmwareInstanceEspOta":
        """
        Get the next firmware instance.
        """
        # Build the firmware if it is not built
        self._build_if_not_built()

        # Get the factory config
        factory_config = self.factory_config_factory.create()
        factory_config.read_factory_config(self.build_dir / "config" / "sdkconfig.h")

        # Flash the firmware
        self._flash_port(port)
        return FirmwareInstanceEspOta(
            tag=self.get_tag(),
            factory_config=factory_config,
            fw_manager=self.fw_manager,
            port=port,
            baudrate=self.baudrate,
            base_binary_path=self.base_binary_path,
            monitor_ready_pattern=self.monitor_ready_pattern,
            should_log=self.should_log,
        )


# --- Firmware instance ---


class MultiplexedPortLoggingStream(LoggingStream):
    """
    A class that represents a stream for logging to a multiplexed port.
    """

    def __init__(self, multiplexed_port: PortManagerMultiplexed):
        self.monitor_stream = multiplexed_port.get_monitor_stream()

    def read(self, size: int = 1024, timeout: float = 0.5) -> Optional[bytes]:
        old_timeout = self.monitor_stream.timeout
        self.monitor_stream.timeout = timeout
        data = self.monitor_stream.read(size=size)
        self.monitor_stream.timeout = old_timeout
        return data


class FirmwareInstanceEsp(FirmwareInstance):
    """
    A class that represents a ESP firmware instance.
    """

    def __init__(
        self,
        tag: str,
        factory_config: FactoryConfig,
        fw_manager: EspFirmwareManager,
        port: str,
        baudrate: int,
        monitor_ready_pattern: re.Pattern,
        should_log: bool = True,
        admin_user: Optional[User] = None,
    ):
        self.tag = tag
        self.port_timeout = 10  # 10 seconds to wait for data in
        FirmwareInstance.__init__(
            self,
            factory_config=factory_config,
            log_thread_timeout=self.port_timeout + 1,
            should_log=should_log,
            admin_user=admin_user,
        )
        self.fw_manager = fw_manager
        self.monitor_ready_pattern = monitor_ready_pattern
        self.port = port
        self.port_manager = PortManagerMultiplexed(
            port, baudrate, HostCtrlGlobals.protocol, timeout=self.port_timeout
        )

        # Flash the factory partition
        self._flash_factory_partition()

        # Register the factory config with AWS
        self.factory_config.register_with_cloud(admin_user=self.admin_user)

    def get_tag(self) -> str:
        return self.tag

    def _flash_factory_partition(self) -> None:
        """
        Flash the factory partition.
        """
        fctry_info = self.fw_manager.partition_info.get("fctry")
        if fctry_info is None:
            raise RuntimeError("Factory partition not found in partition info")
        bin_path = self.factory_config.to_idf_binary()
        self.fw_manager.flash_partition(
            self.port, bin_path, fctry_info, reset_after=False
        )
        bin_path.unlink()

    def _esp_reset(self, reset_mode: Literal["hard-reset", "no-reset"] = "hard-reset"):
        """
        Reset the ESP chip.
        reset_mode: Reset mode to use (
            ``"hard-reset"``: perform a hard reset using the RTS control line,
            ``"no-reset"``: stay in bootloader,
        )
        """
        with detect_chip(port=self.port, connect_attempts=3) as esp:
            reset_chip(esp, reset_mode=reset_mode)

    def reset_nvs(self, port: str, reset_after: bool = True):
        """
        Reset the NVS partition.
        This corresponds to an erase of the NVS partition.
        """
        return self.fw_manager.reset_nvs(port, reset_after=reset_after)

    def pause_multiplexed_monitor_logging(self) -> None:
        """
        Stop the background thread that reads the multiplexed monitor stream.

        Use before :func:`flush_monitor_buffer` or exclusive ``read_monitor_line`` sequences
        so test code is the sole consumer of device console bytes.
        """
        if self.should_log:
            self._stop_logging_thread()

    def resume_multiplexed_monitor_logging(self) -> None:
        """Restart monitor logging after :meth:`pause_multiplexed_monitor_logging`."""
        if self.should_log and self.is_running():
            self._start_logging_thread(MultiplexedPortLoggingStream(self.port_manager))

    def wait_for_device_ready(self) -> bool:
        """
        Wait for the device to be ready.
        """
        # Pattern: ensure device is ready
        print("Waiting for device to be ready")
        monitor_ready_pattern = re.compile(self.monitor_ready_pattern)
        found_ready_pattern = False
        timeout = time.time() + 30  # 30 seconds to wait for the device to be ready
        while time.time() < timeout:
            line = self.port_manager.read_monitor_line()
            if line is None:
                time.sleep(0.1)
                continue

            line = line.decode("utf-8", errors="replace")
            print(line, end="")
            if monitor_ready_pattern.search(line):
                found_ready_pattern = True
                break

        return found_ready_pattern

    def start(self) -> bool:
        """
        Start the firmware instance.
        """
        # Reset the chip to start
        self._esp_reset(reset_mode="hard-reset")

        # Start the port manager
        self.port_manager.connect()

        # Wait for the device to be ready
        if not self.wait_for_device_ready():
            print(
                "WARN: Device not ready after timeout, could not start firmware instance"
            )
            self.port_manager.disconnect()
            self._esp_reset(reset_mode="no-reset")
            return False

        # Start the logging thread
        self._start_logging_thread(
            log_stream=MultiplexedPortLoggingStream(self.port_manager)
        )
        self._mark_as_running()
        return True

    def stop(self) -> bool:
        """
        Stop the firmware instance.
        """
        # Stop the port manager
        self.port_manager.disconnect()

        # Stop the logging thread
        self._stop_logging_thread()

        # Reset the chip into bootloader mode
        self._esp_reset(reset_mode="no-reset")
        self._mark_as_not_running()
        return True

    def destroy(self) -> None:
        """
        Destroy the firmware instance.
        """
        # No extra destruction is needed for ESP firmware instances
        self._destroy_internal()

    def __setstate__(self, state: dict):
        super().__setstate__(state)
        _log_stream = MultiplexedPortLoggingStream(self.port_manager)
        if self.is_running():
            self._start_logging_thread(log_stream=_log_stream)


class FirmwareInstanceEspHostCtrl(FirmwareInstanceEsp, FirmwareInstanceHostCtrl):
    """
    A class that represents a ESP firmware instance with a host_ctrl.
    """

    def __init__(
        self,
        tag: str,
        factory_config: FactoryConfig,
        fw_manager: EspFirmwareManager,
        port: str,
        baudrate: int,
        monitor_ready_pattern: re.Pattern,
        should_log: bool = True,
    ):
        FirmwareInstanceEsp.__init__(
            self,
            tag=tag,
            factory_config=factory_config,
            fw_manager=fw_manager,
            port=port,
            baudrate=baudrate,
            monitor_ready_pattern=monitor_ready_pattern,
            should_log=should_log,
        )
        self._setup_host_ctrl_variables(self.port_manager)


class FirmwareInstanceEspOta(FirmwareInstanceEsp, FirmwareInstanceOta):
    """
    A class that represents a ESP firmware instance for OTA testing.
    """

    def __init__(
        self,
        tag: str,
        factory_config: FactoryConfig,
        fw_manager: EspFirmwareManager,
        port: str,
        baudrate: int,
        base_binary_path: Path,
        monitor_ready_pattern: re.Pattern,
        should_log: bool = True,
    ):
        FirmwareInstanceEsp.__init__(
            self,
            tag=tag,
            factory_config=factory_config,
            fw_manager=fw_manager,
            port=port,
            baudrate=baudrate,
            monitor_ready_pattern=monitor_ready_pattern,
            should_log=should_log,
        )
        self.partition_info = self.fw_manager.partition_info
        self.base_binary_path = base_binary_path

    def reset_ota_state(self) -> bool:
        """
        Reset the OTA state of the firmware instance.
        """
        # Stop the firmware instance
        if not self.stop():
            return False

        # Erase the OTA data partition
        ota_data_info = self.partition_info.get("ota_data")
        if ota_data_info is None:
            print(
                "WARN: OTA data partition not found in partition info, cannot reset OTA state"
            )
            return False
        self.fw_manager.erase_partition(self.port, ota_data_info)

        # Re-flash the original binary
        if self.base_binary_path is None:
            print("WARN: Base binary path is not set, cannot reset OTA state")
            return False
        ota_0_info = self.partition_info.get("ota_0")
        if ota_0_info is None:
            print(
                "WARN: OTA 0 partition not found in partition info, cannot reset OTA state"
            )
            return False
        self.fw_manager.flash_partition(
            self.port, self.base_binary_path, ota_0_info, baud=460800, reset_after=False
        )

        return True
