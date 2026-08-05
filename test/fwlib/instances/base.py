# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# include the host_ctrl and the factory_nvs_gen module
from io import IOBase
import sys
import os
import logging
from ..filepaths import RMNG_SDK_HOST_CTRL_MODULE_DIR

sys.path.append(str(RMNG_SDK_HOST_CTRL_MODULE_DIR))

from host_ctrl_python.host_ctrl import NodeHostCtrl
from host_ctrl_python.commands import PortManager

from util.factory_config import FactoryConfigFactory, FactoryConfig
from rmng_backend import User

from pathlib import Path
import shutil
from threading import Thread, Lock
from typing import Optional, Literal

from cryptography.hazmat.primitives.asymmetric import ec, rsa, padding
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.serialization import load_pem_private_key
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import utils as crypto_utils
from cryptography import x509


class AssocCapable:
    """
    A class that can do challenge-response association.
    """

    def __init__(self, node_thing_name):
        self.node_thing_name = node_thing_name

    def _sign_challenge_with_key(self, challenge, key) -> str:
        """
        Sign a challenge using the private key.
        raises ValueError if the key type is unsupported.
        returns the signature as a hex string.
        """
        # Load the private key
        private_key = load_pem_private_key(
            key.encode(),
            password=None,
        )

        # Determine the key type and choose the appropriate signing algorithm
        if isinstance(private_key, rsa.RSAPrivateKey):
            # For RSA keys
            signature = private_key.sign(
                challenge.encode(),
                padding.PSS(
                    mgf=padding.MGF1(hashes.SHA256()),
                    salt_length=padding.PSS.MAX_LENGTH,
                ),
                hashes.SHA256(),
            )

        elif isinstance(private_key, ec.EllipticCurvePrivateKey):
            # For EC keys
            signature = private_key.sign(challenge.encode(), ec.ECDSA(hashes.SHA256()))
            # Convert signature to DER format
            der_signature = crypto_utils.encode_dss_signature(
                *crypto_utils.decode_dss_signature(signature)
            )
            signature = der_signature

        else:
            raise ValueError("Unsupported key type")

        return signature.hex()

    def sign_challenge(self, challenge):
        """
        Sign a challenge using the private key.
        """
        raise NotImplementedError("sign_challenge() not implemented")


class AssocCapableInstance(AssocCapable):
    """
    Used to associate the firmware instance without a host_ctrl.
    """

    def __init__(self, factory_config: FactoryConfig):
        super().__init__(factory_config.thing_name)
        self.private_key = factory_config.key

    def sign_challenge(self, challenge: str) -> str:
        """
        Sign a challenge using the private key.
        """
        return self._sign_challenge_with_key(challenge, self.private_key)


class LoggingStream:
    """
    A class that represents a stream for logging.
    """

    def __init__(self, stream: IOBase):
        self.stream = stream

    def read(self, size: int = 1024, timeout: float = 0.5) -> Optional[bytes]:
        """
        {abstract}
        Read data from the stream.
        Returns the data read from the stream, empty bytes on timeout, or None if the stream is closed.
        """
        raise NotImplementedError("read() is not implemented")


class FirmwareInstance:
    """
    A class that represents a firmware instance of a NodeHostCtrl.
    """

    def __init__(
        self,
        factory_config: FactoryConfig,
        log_thread_timeout: float = 5.0,
        should_log: bool = True,
        admin_user: Optional[User] = None,
    ):
        self.factory_config = factory_config
        self.assoc_instance = AssocCapableInstance(factory_config)
        self.should_log = should_log
        self._log_thread = None
        self.log_thread_timeout = log_thread_timeout
        self._logger: Optional[logging.Logger] = None
        self._line_buffer = ""
        self._is_running = False
        self.admin_user = admin_user

    def _setup_logging(self):
        """Set up logging for firmware output."""
        if not self.should_log:
            return

        # Create a logger specific to this firmware instance
        self._logger = logging.getLogger(self.factory_config.thing_name)

        # Only set up handlers if not already configured
        if not self._logger.handlers:
            # Create console handler that respects pytest capture
            self._logger_handler = logging.StreamHandler(sys.stdout)
            # Use a simple formatter without timestamps for cleaner output
            formatter = logging.Formatter("%(message)s")
            self._logger_handler.setFormatter(formatter)
            self._logger.addHandler(self._logger_handler)
            self._logger.setLevel(logging.INFO)
            # Prevent propagation to root logger to avoid duplicate output
            self._logger.propagate = False
        else:
            self._logger_handler = self._logger.handlers[0]

    def _start_logging_thread(self, log_stream: LoggingStream):
        """
        Start the logging thread.
        log_stream: The stream that will be used to log the output.
        """
        # Set up logging when starting the thread (after get_id() is available)
        self._setup_logging()

        self._log_thread_running = True
        self._log_thread = Thread(
            target=self._log_thread_fn, args=(log_stream,), daemon=True
        )
        self._log_thread.start()

    def _stop_logging_thread(self):
        """
        Stop the logging thread.
        """
        if self._log_thread is not None:
            self._log_thread_running = False
            self._log_thread.join(
                timeout=self.log_thread_timeout
            )  # Give thread log_thread_timeout seconds to finish
            if self._log_thread.is_alive():
                print(
                    f"Warning: logging thread did not exit cleanly after {self.log_thread_timeout} seconds"
                )

            self._log_thread = None

    def _log_thread_fn(self, log_stream: LoggingStream):
        """
        Log the output from the serial port using logging module, buffering for complete lines.
        """
        if not self._logger:
            return

        try:
            while self._log_thread_running:
                if self._logger_handler.stream.closed:
                    break

                data = log_stream.read(1024)
                if data is None:
                    # EOF reached
                    break

                if len(data) == 0:
                    # Empty bytes, timeout reached
                    continue

                # Decode the data and add to buffer
                decoded_data = data.decode("utf-8", errors="replace")
                self._line_buffer += decoded_data

                # Split on newlines and log complete lines
                lines = self._line_buffer.split("\n")
                self._line_buffer = lines[-1]  # Keep incomplete line in buffer

                # Log all complete lines (excluding the last incomplete one)
                for line in lines[:-1]:
                    if (
                        line and not self._logger_handler.stream.closed
                    ):  # Skip empty lines
                        self._logger.info(line)

        except Exception as e:
            print(
                f"Exception in logging thread for {self.factory_config.thing_name}: {e}"
            )
            # Log stream was closed
            pass
        finally:
            # Log any remaining buffered data when thread ends
            if self._line_buffer and self._logger:
                self._logger.info(self._line_buffer.rstrip("\r"))
                self._line_buffer = ""

        print(
            f"Logging thread for {self.factory_config.thing_name} ended, running: {self._log_thread_running}"
        )

    def _mark_as_running(self):
        """
        Mark the firmware instance as running.
        """
        self._is_running = True

    def _mark_as_not_running(self):
        """
        Mark the firmware instance as not running.
        """
        self._is_running = False

    def is_running(self) -> bool:
        """
        Check if the firmware instance is running.
        """
        return self._is_running

    def start(self) -> bool:
        """
        {abstract}
        Start the firmware instance.
        Returns True if the firmware instance was started successfully, False otherwise.
        """
        raise NotImplementedError("start() is not implemented")

    def stop(self) -> bool:
        """
        {abstract}
        Stop the firmware instance.
        Returns True if the firmware instance was stopped successfully, False otherwise.
        """
        raise NotImplementedError("stop() is not implemented")

    def get_assoc_instance(self) -> AssocCapable:
        """
        Returns the association instance.
        """
        return self.assoc_instance

    def _destroy_internal(self) -> None:
        """
        Internal common implementation of destroy.
        """
        # Stop the firmware instance if running
        if self._is_running:
            self.stop()

        # Destroy the factory config from AWS
        self.factory_config.destroy_from_cloud(admin_user=self.admin_user)

        # Clean up the factory config
        self.factory_config.clean_up()

    def destroy(self) -> None:
        """
        {abstract}
        Destroy the firmware instance.
        """
        raise NotImplementedError("destroy() is not implemented")

    def __getstate__(self) -> dict:
        if self.is_running():
            self._stop_logging_thread()
        state = self.__dict__.copy()
        state.pop("_logger", None)
        state.pop("_logger_handler", None)
        state.pop("_log_thread", None)
        return state

    def __setstate__(self, state: dict):
        self.__dict__.update(state)
        self._setup_logging()
        self._log_thread = None


class FirmwareInstanceHostCtrl(FirmwareInstance):
    """
    A class that represents a firmware instance with a host_ctrl.
    """

    def _setup_host_ctrl_variables(self, port_manager: PortManager):
        """
        Set up the host_ctrl variables.
        """
        self.port_manager = port_manager

    def get_host_ctrl(self) -> NodeHostCtrl:
        """
        Returns the NodeHostCtrl object used to control the firmware instance.
        """
        if self.port_manager is None:
            raise ValueError("Host control port manager must be set")
        cert = x509.load_pem_x509_certificate(self.factory_config.node_cert.encode())
        public_key_pem = (
            cert.public_key()
            .public_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PublicFormat.SubjectPublicKeyInfo,
            )
            .decode()
        )
        return NodeHostCtrl(
            self.port_manager, should_log=self.should_log, public_key_pem=public_key_pem
        )


class FirmwareInstanceOta(FirmwareInstance):
    """
    A class that represents a firmware instance for OTA testing.
    """

    def reset_ota_state(self) -> bool:
        """
        {abstract}
        Reset the OTA state of the firmware instance.
        This corresponds to wiping any OTA data from the firmware instance.
        You should manually start the firmware instance after resetting the OTA state.
        Returns True if the OTA state was reset successfully, False otherwise.
        """
        raise NotImplementedError("reset_ota_state() is not implemented")


class FirmwareInstanceFactory:
    """
    A class that represents a factory for firmware instances.
    """

    def __init__(
        self, factory_config_factory: FactoryConfigFactory, should_log: bool = True
    ):
        self.factory_config_factory = factory_config_factory
        self.already_built_dirs = set()
        self._overall_build_lock = Lock()
        self._build_dirs_locks = {}
        self.build_dir = self._get_build_dir()  # Build directory for the base binary
        self.should_log = should_log

    def get_id(self) -> str:
        """
        Get the ID of the firmware instance.
        """
        raise NotImplementedError("get_id() is not implemented")

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        By default, returns an empty dictionary.
        """
        return {}

    def _get_build_dir(self, version_str: str = None) -> Path:
        """
        Get the build directory for the given target.
        """

        # Build the folder name
        folder_name = self.get_id()
        if version_str is not None:
            folder_name += f"-v{version_str}"

        # Build the build directory
        cwd = os.getcwd()
        build_dir = Path(cwd) / "build" / folder_name

        return build_dir

    def _remake_build_dir(self, build_dir: Path):
        """
        Remake the build directory.
        """
        shutil.rmtree(build_dir, ignore_errors=True)
        build_dir.mkdir(parents=True, exist_ok=True)

    def _get_build_dir_lock(self, build_dir: Path) -> Lock:
        """
        Get the lock for the build directory.
        """
        with self._overall_build_lock:
            if build_dir not in self._build_dirs_locks:
                self._build_dirs_locks[build_dir] = Lock()
            return self._build_dirs_locks[build_dir]

    def _mark_as_built_locked(self, build_dir: Path):
        """
        Mark the build directory as built.
        This should be called with the build directory lock held.
        """
        self.already_built_dirs.add(build_dir)

    def _is_built_locked(self, build_dir: Path) -> bool:
        """
        Check if the build directory is built.
        This should be called with the build directory lock held.
        """
        return build_dir in self.already_built_dirs

    def _build_if_not_built(self):
        """
        Build the firmware if it is not built.
        """
        build_dir = self.build_dir
        with self._get_build_dir_lock(build_dir):
            if not self._is_built_locked(build_dir):
                self.build()
                self._mark_as_built_locked(build_dir)

    def build(self, add_configs: dict[str, str | int] = {}):
        """
        {abstract}
        Build the underlying firmware.
        """
        raise NotImplementedError("build() is not implemented")

    def build_version_binary_if_not_built(self, version_str: str) -> Path:
        """
        {abstract}
        Build a binary with a specific firmware version if it is not built.
        Returns the path to the binary.
        """
        raise NotImplementedError(
            "build_version_binary_if_not_built() is not implemented"
        )

    def get_next_instance(self, **kwargs) -> FirmwareInstance:
        """
        Get the next firmware instance.
        kwargs: Additional keyword arguments to pass to the get_next_instance() method.
        """
        raise NotImplementedError("get_next_instance() is not implemented")

    def destroy(self) -> None:
        """
        Destroy the factory.
        """
        raise NotImplementedError("destroy() is not implemented")


class FirmwareInstanceFactoryOta(FirmwareInstanceFactory):
    OTA_TYPE_MQTT_CBOR = "mqtt_cbor"
    OTA_TYPE_MQTT_CBOR_NO_SIG_VERIFY = "mqtt_cbor_no_sig_verify"
    OTA_TYPE_MQTT_JSON = "mqtt_json"
    OTA_TYPES = Literal[
        OTA_TYPE_MQTT_CBOR,
        OTA_TYPE_MQTT_CBOR_NO_SIG_VERIFY,
        OTA_TYPE_MQTT_JSON,
    ]
    """
    A class that represents a factory for OTA firmware instances.
    """

    def _set_ota_type(self, ota_type: OTA_TYPES):
        """
        Set the OTA type.
        """
        self.ota_type = ota_type

    def _get_type_specific_sdkconfig_dict(self) -> dict[str, str]:
        """
        Get the type-specific SDK config dictionary.
        """
        if self.ota_type == self.OTA_TYPE_MQTT_CBOR:
            return {
                "RMNG_OTA_TRANSPORT_MQTT": "y",
                "RMNG_OTA_MQTT_DATA_TYPE_CBOR": "y",
                "RMNG_OTA_SIGNATURE_VERIFY_ENABLE": "y",
            }
        elif self.ota_type == self.OTA_TYPE_MQTT_CBOR_NO_SIG_VERIFY:
            return {
                "RMNG_OTA_TRANSPORT_MQTT": "y",
                "RMNG_OTA_MQTT_DATA_TYPE_CBOR": "y",
                "RMNG_OTA_SIGNATURE_VERIFY_ENABLE": "n",
            }
        elif self.ota_type == self.OTA_TYPE_MQTT_JSON:
            return {
                "RMNG_OTA_TRANSPORT_MQTT": "y",
                "RMNG_OTA_MQTT_DATA_TYPE_JSON": "y",
                "RMNG_OTA_SIGNATURE_VERIFY_ENABLE": "y",
            }
        else:
            raise ValueError(f"Invalid OTA type: {self.ota_type}")

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        ret = super()._get_add_configs()
        ret.update(self._get_type_specific_sdkconfig_dict())
        return ret


class FirmwareInstanceFactoryHostCtrl(FirmwareInstanceFactory):
    REQUEST_TYPE_DEFAULT = "default"
    REQUEST_TYPE_NO_BASIC_INGEST = "no_basic_ingest"
    # Local control SEC1 / SEC2 build variants (challenge-response disabled).
    REQUEST_TYPE_LOCAL_CTRL_SEC1 = "local_ctrl_sec1"
    REQUEST_TYPE_LOCAL_CTRL_SEC2 = "local_ctrl_sec2"
    # Local control with challenge-response, SEC1 / SEC2 build variants.
    REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC1 = "lc_chal_resp_sec1"
    REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC2 = "lc_chal_resp_sec2"
    REQUEST_TYPE_ON_NETWORK_CHAL_RESP = "on_chal_resp"
    REQUEST_TYPE_BRIDGE = "bridge"
    # Default local-control HTTP port in test firmware builds. ESP-IDF devices do not need a
    # unique port per build; POSIX parallel runs can override per instance when supported.
    _DEFAULT_LOCAL_CTRL_HTTP_PORT = 49152
    # Local control security version Kconfig choice options (mutually exclusive).
    _LOCAL_CTRL_SEC_VERSION_1_KCONFIG = "ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_1"
    _LOCAL_CTRL_SEC_VERSION_2_KCONFIG = "ESP_RMAKER_LOCAL_CTRL_SEC_VERSION_2"
    REQUEST_TYPES = Literal[
        REQUEST_TYPE_DEFAULT,
        REQUEST_TYPE_NO_BASIC_INGEST,
        REQUEST_TYPE_LOCAL_CTRL_SEC1,
        REQUEST_TYPE_LOCAL_CTRL_SEC2,
        REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC1,
        REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC2,
        REQUEST_TYPE_ON_NETWORK_CHAL_RESP,
        REQUEST_TYPE_BRIDGE,
    ]
    """
    A class that represents a factory for host_ctrl firmware instances.
    """

    def _set_request_type(self, request_type: REQUEST_TYPES):
        """
        Set the host_ctrl request type.
        """
        self.request_type = request_type

    def _local_ctrl_sec_version_kconfig(self, sec_version: int) -> dict[str, str]:
        """
        Select the local control security version Kconfig choice option. Only the chosen
        option is set to "y"; the choice handles deselecting the others.
        """
        if sec_version == 1:
            return {self._LOCAL_CTRL_SEC_VERSION_1_KCONFIG: "y"}
        elif sec_version == 2:
            return {self._LOCAL_CTRL_SEC_VERSION_2_KCONFIG: "y"}
        else:
            raise ValueError(
                f"Unsupported local control security version: {sec_version}"
            )

    def _get_type_specific_sdkconfig_dict(self) -> dict[str, str]:
        """
        Get the type-specific SDK config dictionary.
        """
        port = self._DEFAULT_LOCAL_CTRL_HTTP_PORT
        if self.request_type == self.REQUEST_TYPE_DEFAULT:
            return {
                "ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_HTTP_PORT": port,
                "ESP_RMAKER_MQTT_USE_BASIC_INGEST": "y",
            }
        elif self.request_type == self.REQUEST_TYPE_NO_BASIC_INGEST:
            return {
                "ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_HTTP_PORT": port,
                "ESP_RMAKER_MQTT_USE_BASIC_INGEST": "n",
            }
        elif self.request_type in (
            self.REQUEST_TYPE_LOCAL_CTRL_SEC1,
            self.REQUEST_TYPE_LOCAL_CTRL_SEC2,
        ):
            sec_version = (
                1 if self.request_type == self.REQUEST_TYPE_LOCAL_CTRL_SEC1 else 2
            )
            return {
                "ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_HTTP_PORT": port,
                "ESP_RMAKER_MQTT_USE_BASIC_INGEST": "y",
                **self._local_ctrl_sec_version_kconfig(sec_version),
            }
        elif self.request_type in (
            self.REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC1,
            self.REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC2,
        ):
            sec_version = (
                1
                if self.request_type == self.REQUEST_TYPE_LOCAL_CTRL_CHAL_RESP_SEC1
                else 2
            )
            return {
                "ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE": "y",
                "ESP_RMAKER_LOCAL_CTRL_HTTP_PORT": port,
                "ESP_RMAKER_MQTT_USE_BASIC_INGEST": "y",
                **self._local_ctrl_sec_version_kconfig(sec_version),
            }
        elif self.request_type == self.REQUEST_TYPE_ON_NETWORK_CHAL_RESP:
            # Security version is left at the Kconfig default (SEC2) on purpose: the test
            # pushes a known PoP over host_ctrl before the service starts, standing in for
            # the manufacturing PoP a real device carries, so this exercises the shipping
            # configuration rather than a weakened one.
            return {
                "ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE": "y",
                "ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_HTTP_PORT": port,
                "ESP_RMAKER_MQTT_USE_BASIC_INGEST": "y",
            }
        elif self.request_type == self.REQUEST_TYPE_BRIDGE:
            return {
                "ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_CHAL_RESP_ENABLE": "n",
                "ESP_RMAKER_LOCAL_CTRL_HTTP_PORT": port,
                "ESP_RMAKER_MQTT_USE_BASIC_INGEST": "y",
                "RMNG_BRIDGE_ENABLED": "y",
                # Maximum support required.
                "RMNG_BRIDGE_MAX_CHILDREN": 400,
                "RMAKER_WORK_QUEUE_TASK_QUEUE_SIZE": 800,
            }
        else:
            raise ValueError(f"Invalid request type: {self.request_type}")

    def _get_add_configs(self) -> dict[str, str | int]:
        """
        Get the additional configs for this factory.
        """
        ret = super()._get_add_configs()
        ret.update(self._get_type_specific_sdkconfig_dict())
        return ret
