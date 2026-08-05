# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

# ruff: noqa: E402 -- sys.path prelude must run before the imports below

import sys
from pathlib import Path

_FACTORY_NVS_GEN_ROOT = str(Path(__file__).resolve().parents[2] / "factory_nvs_gen")
if _FACTORY_NVS_GEN_ROOT not in sys.path:
    sys.path.insert(0, _FACTORY_NVS_GEN_ROOT)
from factory_nvs_gen import (
    get_idf_bin as factory_nvs_gen_get_idf_bin,
    get_posix_bin as factory_nvs_gen_get_posix_bin,
)

import re
import shutil
import uuid
from tempfile import gettempdir
from .node_crypto import generate_key_and_cert, split_combined_cert_pem

# Not the usual "insert if absent": tools/common must come *first*. Importing
# rmng_backend prepends the backend clone to sys.path, and that tree carries its own
# credentials_store; re-seating tools/common ahead of it keeps this the SDK's copy.
_TOOLS_COMMON = str(Path(__file__).resolve().parents[1])
if _TOOLS_COMMON in sys.path:
    sys.path.remove(_TOOLS_COMMON)
sys.path.insert(0, _TOOLS_COMMON)
from credentials_store import RM_CONFIG
import time
from typing import Optional

from rmng_backend import Device, User
from util.cloud import destroy_node
from util.node_registrar import node_registrar_identity

# Node registration goes through the backend py_sdk, which retries nothing: the admin
# registration Lambda can answer 5xx while the backend is loaded (many workers registering
# at once), so retry the whole call. The failure a raise carries does not say whether it was
# transient, so a fatal one is retried too before it surfaces.
_REGISTER_MAX_ATTEMPTS = 4
_REGISTER_RETRY_DELAY_SEC = 1.0


class FactoryConfig:
    """
    A class that represents a factory config.
    """

    def __init__(
        self,
        thing_name_prefix: str,
        key_type: str = "ec",
        codesign_cert_path: Optional[Path] = None,
        capabilities: Optional[list[str]] = None,
    ):
        if key_type not in ("ec", "rsa"):
            raise ValueError("Invalid key type. Use 'ec' or 'rsa'.")

        # Thread-safe unique thing name: use milliseconds since epoch and a random UUID to avoid collisions
        self.thing_name = (
            f"{thing_name_prefix}-{int(time.time_ns() / 1000)}-{uuid.uuid4().hex[:8]}"
        )
        # Capability strings forwarded to the registration API; drives
        # per-capability server-side policy attachment.
        self.capabilities = list(capabilities) if capabilities else None

        # Must be read from sdkconfig.h
        self.factory_part_label = None
        self.factory_namespace = None

        self.key, self.combined_cert = generate_key_and_cert(self.thing_name, key_type)
        self.node_cert, self.node_ca_cert = split_combined_cert_pem(self.combined_cert)
        self.mqtt_host = RM_CONFIG["IoTEndpointUrl"]
        self.mqtt_port = 8883
        # temporary files
        self.key_file_path = None
        self.node_cert_file_path = None
        self.factory_config_json_path = None
        self.codesign_cert_path = codesign_cert_path

    def _get_temp_folder(self) -> Path:
        temp_folder = Path(gettempdir()) / self.thing_name
        temp_folder.mkdir(parents=True, exist_ok=True)
        return temp_folder

    def _make_temp_file(self, filename: str, content: str) -> Path:
        temp_file = self._get_temp_folder() / filename
        temp_file.write_text(content)
        return temp_file

    def _make_temp_credentials(self):
        """
        Make temporary files for the credentials.
        """
        # Clean old files first
        self._clean_temp_credentials()

        # Make new files
        self.key_file_path = self._make_temp_file(f"{self.thing_name}.key", self.key)
        self.node_cert_file_path = self._make_temp_file(
            f"{self.thing_name}.crt", self.node_cert
        )

    def _clean_temp_credentials(self):
        """
        Clean the temporary files for the credentials.
        """
        if self.key_file_path is not None:
            self.key_file_path.unlink()
            self.key_file_path = None
        if self.node_cert_file_path is not None:
            self.node_cert_file_path.unlink()
            self.node_cert_file_path = None

    def _get_factory_data(self) -> dict[str, any]:
        """
        Get the factory data.
        """
        self._make_temp_credentials()
        factory_data = {
            "client_key": str(self.key_file_path),
            "client_cert": str(self.node_cert_file_path),
            "mqtt_host": self.mqtt_host,
            "mqtt_port": self.mqtt_port,
            "node_id": self.thing_name,
        }
        if self.codesign_cert_path is not None:
            factory_data["codesign_cert"] = str(self.codesign_cert_path)
        return factory_data

    def read_factory_config(self, sdkconfig_path: Path) -> None:
        """
        Read the factory config from the sdkconfig.h file.
        """
        label_re = re.compile(
            r'#define CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME\s+"([^"]*)"'
        )
        namespace_re = re.compile(
            r'#define CONFIG_ESP_RMAKER_FACTORY_NAMESPACE\s+"([^"]*)"'
        )
        with open(sdkconfig_path, "r") as f:
            for line in f:
                m = label_re.match(line.strip())
                if m:
                    self.factory_part_label = m.group(1)
                m = namespace_re.match(line.strip())
                if m:
                    self.factory_namespace = m.group(1)
        if self.factory_part_label is None or self.factory_namespace is None:
            raise RuntimeError("Failed to read factory config from sdkconfig.h")

    def to_idf_binary(self) -> Path:
        if self.factory_namespace is None:
            raise RuntimeError("Factory namespace is not set")
        factory_data = self._get_factory_data()
        out_path = self._get_temp_folder() / f"{self.thing_name}_factory.bin"
        if not factory_nvs_gen_get_idf_bin(
            self.factory_namespace, factory_data, out_path.parent, out_path
        ).exists():
            raise RuntimeError(f"Failed to generate factory binary: {out_path}")
        return out_path

    def to_posix_binary(self) -> Path:
        if self.factory_part_label is None:
            raise RuntimeError("Factory part label is not set")
        if self.factory_namespace is None:
            raise RuntimeError("Factory namespace is not set")
        factory_data = self._get_factory_data()
        out_dir = self._get_temp_folder()
        out_path = factory_nvs_gen_get_posix_bin(
            self.factory_part_label,
            self.factory_namespace,
            factory_data,
            out_dir,
            out_dir,
        )
        if not out_path.exists():
            raise RuntimeError(f"Failed to generate factory binary: {out_dir}")
        return out_path

    def _to_device(self) -> Device:
        """This factory config as the py_sdk Device the registration helpers speak in."""
        return Device(
            node_thing_name=self.thing_name,
            node_key=self.key,
            node_combined_cert=self.combined_cert,
            ca_cert=RM_CONFIG["CACert"],
            iot_endpoint=RM_CONFIG["IoTEndpointUrl"],
            region=RM_CONFIG["StackRegion"],
        )

    def register_with_cloud(self, admin_user: Optional[User] = None) -> bool:
        """
        Register the factory config with the cloud.
        """
        device = self._to_device()

        # Register through the admin API if an admin user is provided
        if admin_user is not None:
            return admin_user.register_node(
                device=device, capabilities=self.capabilities
            )

        # Otherwise invoke the admin registration Lambda directly, as a superadmin: the
        # handler resolves its caller from the identity and answers 401 without one.
        last_error = None
        for attempt in range(_REGISTER_MAX_ATTEMPTS):
            try:
                return device.register_test_node(
                    capabilities=self.capabilities,
                    caller_identity=node_registrar_identity(),
                )
            except Exception as e:
                last_error = e
                if attempt < _REGISTER_MAX_ATTEMPTS - 1:
                    delay = _REGISTER_RETRY_DELAY_SEC * (2**attempt)
                    print(
                        f"[Cloud] Node registration failed ({e}), retrying in {delay}s "
                        f"({attempt + 1}/{_REGISTER_MAX_ATTEMPTS})..."
                    )
                    time.sleep(delay)
        raise RuntimeError(
            f"Node registration failed after {_REGISTER_MAX_ATTEMPTS} attempts: {last_error}"
        )

    def destroy_from_cloud(self, admin_user: Optional[User] = None) -> bool:
        """
        Destroy the factory config from the cloud.
        """
        if admin_user is not None:
            print(
                "[WARNING] Destroying factory config from cloud with admin user is not implemented yet"
            )
            return False

        # Destroy with Lambda function if no admin user provided
        return destroy_node(
            RM_CONFIG["StackRegion"], RM_CONFIG["IoTEndpointUrl"], self.thing_name
        )

    def clean_up(self) -> None:
        """
        Clean up the temporary files.
        """
        shutil.rmtree(self._get_temp_folder(), ignore_errors=True)


class FactoryConfigFactory:
    def __init__(
        self,
        thing_name_prefix: str,
        key_type: str = "ec",
        codesign_cert_path: Optional[Path] = None,
        capabilities: Optional[list[str]] = None,
    ):
        self.thing_name_prefix = thing_name_prefix
        self.key_type = key_type
        self.codesign_cert_path = codesign_cert_path
        self.capabilities = list(capabilities) if capabilities else None

    def create(self) -> FactoryConfig:
        return FactoryConfig(
            thing_name_prefix=self.thing_name_prefix,
            key_type=self.key_type,
            codesign_cert_path=self.codesign_cert_path,
            capabilities=list(self.capabilities) if self.capabilities else None,
        )
