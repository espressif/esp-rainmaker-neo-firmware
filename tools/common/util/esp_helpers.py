# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from pathlib import Path
from .factory_config import FactoryConfig
from .kconfig import gen_sdkconfig_defaults
import os
import sys
import platform
import subprocess
from typing import Callable, Literal, Optional
import csv
import tempfile
import argparse
from esptool.cmds import (
    detect_chip,
    attach_flash,
    write_flash,
    reset_chip,
    erase_region,
)
from esptool import ESPLoader, get_port_list, parse_port_filters
from esptool.targets import CHIP_DEFS
from .build_job_slot import build_job_slot
from .proc_manager import ProcManager
import gitignorefile
import shutil
import uuid
from dotenv import load_dotenv
import esp_idf_nvs_partition_gen.nvs_partition_gen as nvs_partition_gen

ESP_TARGET = Literal.__getitem__(tuple(CHIP_DEFS.keys()))
NETWORK_TYPE = Literal["wifi", "thread"]

# resolve() first: sys.path entries can be non-normalized (pytest's ``pythonpath``
# keeps the ``../common`` it is given), so __file__ may carry a ".." segment.
env_path = Path(__file__).resolve().parents[3] / ".env"
if not env_path.exists():
    raise FileNotFoundError(f"Environment file not found at {env_path}")
load_dotenv(env_path)

ESP_WIFI_SSID = os.getenv("ESP_WIFI_SSID")
ESP_WIFI_PASSWORD = os.getenv("ESP_WIFI_PASSWORD")
ESP_THREAD_ACTIVE_DATASET = os.getenv("ESP_THREAD_ACTIVE_DATASET")


class NetworkCredentials:
    """
    A class that represents network credentials.
    """

    def __init__(self, network_type: NETWORK_TYPE):
        self.network_type = network_type
        self.sdkconfig_defaults_path = None
        self.nvs_csv_path = None
        self.nvs_binary_path = None

    def get_add_configs(self) -> dict[str, str]:
        """
        Get the additional SDK configurations.
        """
        raise NotImplementedError("get_add_configs() is not implemented")

    def _gen_nvs_csv(self, rows: list[tuple[str, str, str, str]]) -> str:
        """
        Generate a temporary NVS CSV file. Must be deleted after use.
        """
        if self.nvs_csv_path is not None:
            return self.nvs_csv_path

        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".csv", delete=False
        ) as csv_file:
            writer = csv.writer(csv_file)
            writer.writerow(["key", "type", "encoding", "value"])
            writer.writerows(rows)
            self.nvs_csv_path = csv_file.name
            return self.nvs_csv_path

    def _get_nvs_csv_path(self) -> str:
        """
        Get the NVS CSV path to a temporary file. Must be deleted after use.
        """
        raise NotImplementedError("_get_nvs_csv_path() is not implemented")

    def get_nvs_binary_path(self, size: int) -> Optional[Path]:
        """
        Get the NVS binary path.
        """
        if self.nvs_binary_path is not None:
            return self.nvs_binary_path

        try:
            with tempfile.NamedTemporaryFile(
                suffix=".bin", delete=False, delete_on_close=False
            ) as bin_file:
                nvs_bin_path = bin_file.name

            args = argparse.Namespace(
                input=self._get_nvs_csv_path(),
                output=os.path.basename(nvs_bin_path),
                size=f"0x{size:x}",
                version=2,
                outdir=os.path.dirname(nvs_bin_path),
            )
            nvs_partition_gen.generate(args)

            self.nvs_binary_path = nvs_bin_path
            return self.nvs_binary_path
        except Exception:
            try:
                os.unlink(nvs_bin_path)
            except OSError:
                pass
            self.nvs_binary_path = None
            return None

    def destroy(self) -> None:
        """
        Destroy the network credentials.
        """
        if self.nvs_csv_path is not None:
            os.unlink(self.nvs_csv_path)
        if self.nvs_binary_path is not None:
            os.unlink(self.nvs_binary_path)


class NetworkCredentialsWifi(NetworkCredentials):
    """
    A class that represents WiFi network credentials.
    """

    def __init__(self, ssid: str, password: str):
        super().__init__("wifi")
        self.ssid = ssid
        self.password = password

    def get_add_configs(self) -> dict[str, str]:
        """
        Get the additional SDK configurations for WiFi.
        """
        config = {
            "APP_NETWORK_OVER_WIFI": "y",
            "NETWORK_PROV_NETWORK_TYPE_WIFI": "y",
        }
        return config

    def _get_nvs_csv_path(self) -> str:
        """
        Get the temporary NVS CSV path for WiFi.
        """

        rows = [
            ["nvs.net80211", "namespace", "", ""],
            ["sta.ssid", "data", "blob_sz_fill(32;0x00)", self.ssid],
            ["sta.pswd", "data", "blob_fill(64;0x00)", self.password],
            ["sta.scan_method", "data", "u8", 1],
            ["sta.sae_h2e_id", "data", "blob_fill(32;0x00)", ""],
        ]
        return self._gen_nvs_csv(rows)


class NetworkCredentialsThread(NetworkCredentials):
    """
    A class that represents Thread network credentials.
    """

    def __init__(self, active_dataset: str):
        super().__init__("thread")
        self.active_dataset = active_dataset

    def get_add_configs(self) -> dict[str, str]:
        """
        Get the additional SDK configurations for Thread.
        """
        config = {
            "OPENTHREAD_ENABLED": "y",
            "OPENTHREAD_CLI": "n",
            "APP_NETWORK_OVER_THREAD": "y",
            "APP_NETWORK_OVER_THREAD_IPV4_NETWORKING": "y",
            "NETWORK_PROV_NETWORK_TYPE_THREAD": "y",
            "BT_ENABLED": "y",
            "BT_NIMBLE_ENABLED": "y",
        }
        return config

    def _get_nvs_csv_path(self) -> str:
        """
        Get the temporary NVS CSV path for Thread.
        """
        rows = [
            ["openthread", "namespace", "", ""],
            ["OT0100", "data", "hex2bin", self.active_dataset],
        ]
        return self._gen_nvs_csv(rows)


def _get_idf_path() -> Path:
    """
    Get the ESP-IDF path.
    """
    # Check if IDF_PATH is set
    idf_path = os.environ.get("IDF_PATH")
    if idf_path and Path(idf_path).exists():
        return Path(idf_path)

    # Define root locations to search
    roots = []
    system = platform.system()
    if system == "Windows":
        # On Windows, search all drives
        for drive in range(65, 91):  # ASCII A-Z
            drive_path = Path(f"{chr(drive)}:/")
            if drive_path.exists():
                roots.append(drive_path)
    else:
        # On Linux/macOS, search from home and root
        roots = [Path.home(), Path("/")]

    # Recursively search for 'esp-idf'
    for root in roots:
        for folder in root.rglob("esp-idf*"):
            if folder.is_dir() and (folder / "tools" / "idf.py").exists():
                print(f"Found ESP-IDF installation in {folder}")
                return folder

    raise FileNotFoundError("Could not find ESP-IDF installation. Please set IDF_PATH.")


def _get_clean_env():
    """
    Returns a copy of os.environ.
    Only modifies PATH if we are actively running inside a Virtual Environment.
    """
    env = os.environ.copy()

    # 1. Detect if we are in a Venv
    # sys.prefix != sys.base_prefix is the standard way to detect a venv in Python 3.3+
    # We also check for the VIRTUAL_ENV variable standard.
    is_venv = (sys.prefix != sys.base_prefix) or (env.get("VIRTUAL_ENV") is not None)

    if not is_venv:
        # We are on System Python. Do not touch PATH or variables.
        # This prevents us from accidentally removing /usr/bin.
        return env

    # --- We are in a Venv, proceed to clean ---

    # 2. Unset the VIRTUAL_ENV variable (used by prompt scripts)
    env.pop("VIRTUAL_ENV", None)

    # 3. Remove the specific Venv binary directory from PATH
    # Get the directory where the current python executable lives
    venv_bin_dir = os.path.dirname(sys.executable)
    path_sep = os.pathsep

    current_paths = env.get("PATH", "").split(path_sep)

    cleaned_paths = [
        p
        for p in current_paths
        # Normalize paths (handles Windows case-insensitivity and slashes)
        if os.path.normcase(os.path.normpath(p))
        != os.path.normcase(os.path.normpath(venv_bin_dir))
    ]

    env["PATH"] = path_sep.join(cleaned_paths)

    return env


def _export_idf_env(idf_path: Path) -> dict:
    """
    Run the ESP-IDF export script and capture the environment variables.
    Returns a dict suitable for subprocess.run(env=...).
    """
    system = platform.system()

    if system == "Windows":
        # Run PowerShell, export environment after running export.ps1
        # Convert environment to JSON for easier parsing
        cmd = [
            "powershell",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            f'& {{. "{idf_path / "export.ps1"}"; '
            f"[System.Text.Json.JsonSerializer]::Serialize([System.Environment]::GetEnvironmentVariables()) }}",
        ]
    else:
        # Use bash: source export.sh, then dump environment with `env`
        cmd = [
            "bash",
            "-c",
            f'source "{idf_path / "export.sh"}" >/dev/null 2>&1 && env',
        ]

    result = subprocess.run(
        cmd, capture_output=True, text=True, check=True, env=_get_clean_env()
    )

    if system == "Windows":
        import json

        env_dict = json.loads(result.stdout)
        # Convert keys to str
        env = {str(k): str(v) for k, v in env_dict.items()}
    else:
        env = {}
        for line in result.stdout.splitlines():
            key, _, value = line.partition("=")
            env[key] = value

    return env


class EspPartitionInfo:
    def __init__(self, name: str, typ: str, subtype: str, offset: int, size: int):
        self.name = name
        self.type = typ
        self.subtype = subtype
        self.offset = offset
        self.size = size


class EspFirmwareManager:
    _idf_env: dict | None = None

    @classmethod
    def get_idf_env(cls) -> dict:
        if cls._idf_env is None:
            cls._idf_env = _export_idf_env(_get_idf_path())
        return cls._idf_env

    def __init__(
        self,
        target: ESP_TARGET,
        project_dir: Path,
        gitignore_file: Optional[Path] = None,
    ):
        self.target = target
        self.project_dir = project_dir
        self.partition_info = self._get_partition_info()
        self.gitignore_file = gitignore_file

    def get_idf_command(
        self, build_dir: Path, *args: str, project_dir: Optional[Path] = None
    ) -> list[str]:
        """
        Get the IDF command.
        """
        if project_dir is None:
            project_dir = self.project_dir
        return ["idf.py", "-B", str(build_dir), "-C", str(project_dir), *args]

    def _get_partition_info(self) -> dict[str, EspPartitionInfo]:
        """
        Get the partition information from the partition table.
        """

        # Read sdkconfig defaults to see if custom partition table is used.
        # Replace order (last wins): sdkconfig.defaults, sdkconfig.defaults.<target>
        is_custom_partition_table = False
        partition_table_filename = "partitions.csv"
        sdkconfig_defaults_paths = [
            self.project_dir / "sdkconfig.defaults",
            self.project_dir / f"sdkconfig.defaults.{self.target}",
        ]
        for sdkconfig_defaults_path in sdkconfig_defaults_paths:
            if not sdkconfig_defaults_path.exists():
                continue
            for line in sdkconfig_defaults_path.read_text(
                encoding="utf-8"
            ).splitlines():
                if "CONFIG_PARTITION_TABLE_CUSTOM" in line:
                    is_custom_partition_table = (
                        is_custom_partition_table or line.split("=")[-1].strip() == "y"
                    )

                if "CONFIG_PARTITION_TABLE_CUSTOM_FILENAME" in line:
                    partition_table_filename = line.split("=")[-1].strip().strip('"')

        if not is_custom_partition_table:
            raise RuntimeError("Custom factory partition is not used")

        # Read partition table
        partition_info = {}
        partition_table_path = self.project_dir / partition_table_filename

        cached_offset = 0
        for line in partition_table_path.read_text(encoding="utf-8").splitlines():
            line_split = line.split(",")
            if len(line_split) < 5:  # Name, Type, SubType, Offset, Size
                continue

            name, typ, subtype, offset, size = line_split[:5]

            # skip nameless lines
            if len(name.strip()) == 0:
                continue

            # skip header lines
            if offset.strip() == "Offset" or size.strip() == "Size":
                continue

            size_str = size.strip()
            if size_str.endswith("K"):
                size_str = size_str[:-1]
                size_int = int(size_str, base=10) * 1024
            else:
                size_int = int(size_str, base=16)
            try:
                offset_int = int(offset.strip(), base=16)
                cached_offset = offset_int
            except ValueError:
                offset_int = cached_offset
            cached_offset += size_int

            partition_info[name] = EspPartitionInfo(
                name, typ, subtype, offset_int, size_int
            )

        return partition_info

    def erase_partition(
        self, port: str, partition_info: EspPartitionInfo, reset_after: bool = True
    ):
        """
        Erase the partition from the ESP.
        """
        with detect_chip(port=port, connect_attempts=3) as esp:
            attach_flash(esp)
            erase_region(esp, partition_info.offset, partition_info.size)
            if reset_after:
                reset_chip(esp)

    def flash_partition(
        self,
        port: str,
        bin_path: Path,
        partition_info: EspPartitionInfo,
        baud: int = 460800,
        reset_after: bool = True,
        **kwargs,
    ):
        """
        Flash the partition to the ESP.
        If reset_after is True, the ESP will be reset after the flash.
        Baud rate is the baud rate to use for the ESP. Set a higher baud rate for faster flashing.
        All other arguments are passed to the write_flash function.
        See esptool.cmds.write_flash for more details.
        """
        # Use compression by default
        kwargs.setdefault("compress", True)
        with detect_chip(port=port, connect_attempts=3) as esp:
            esp.change_baud(baud)
            attach_flash(esp)
            with open(bin_path, "rb") as bin_file:
                write_flash(esp, [(partition_info.offset, bin_file)], **kwargs)
            if reset_after:
                reset_chip(esp)

    def reset_nvs(self, port: str, reset_after: bool = True):
        """
        Reset the NVS partition.
        This corresponds to an erase of the NVS partition.
        """
        nvs_info = self.partition_info.get("nvs")
        if nvs_info is None:
            raise RuntimeError("NVS partition not found in partition info")
        self.erase_partition(port, nvs_info, reset_after=reset_after)
        print(f"NVS partition erased successfully for '{self.target}' on port {port}")

    def build(
        self,
        build_dir: Path,
        use_temp_project_dir: bool = False,
        add_configs: dict[str, str] = {},
    ):
        """
        Build the firmware with the additional configs.
        If use_temp_project_dir is True, the project directory will be cloned and used instead of the original project directory.
        """
        project_dir = self.project_dir
        if use_temp_project_dir:
            # Clone the project directory to a temporary directory in the same parent folder as the original project directory
            project_dir = (
                self.project_dir.parent
                / f"{self.project_dir.name}_temp-{uuid.uuid4().hex[:8]}"
            )
            ignore_fn = (
                gitignorefile.ignore([str(self.gitignore_file)])
                if self.gitignore_file
                else gitignorefile.ignore()
            )
            shutil.copytree(
                self.project_dir, project_dir, ignore=ignore_fn, dirs_exist_ok=True
            )

        # Build and flash the firmware with the additional configs
        idf_env = {**EspFirmwareManager.get_idf_env()}
        add_defaults = None
        sdkconfig_defaults_path = str(self.project_dir / "sdkconfig.defaults")
        if add_configs:
            add_defaults = gen_sdkconfig_defaults(add_configs)
            sdkconfig_defaults_path = f"{sdkconfig_defaults_path};{add_defaults}"

        # Set the sdkconfig path to the build directory
        sdkconfig_path = str(build_dir / "sdkconfig")

        try:
            with build_job_slot():
                ProcManager.run_cmd_sync(
                    self.get_idf_command(
                        build_dir,
                        f"-DSDKCONFIG_DEFAULTS={sdkconfig_defaults_path}",
                        f"-DSDKCONFIG={sdkconfig_path}",
                        "--ccache",
                        "set-target",
                        self.target,
                        "build",
                        project_dir=project_dir,
                    ),
                    env=idf_env,
                )
        finally:
            if use_temp_project_dir:
                shutil.rmtree(project_dir, ignore_errors=True)
            if add_defaults:
                os.unlink(add_defaults)

    def flash(
        self, port: str, build_dir: Path, baud: int = 460800, reset_after: bool = True
    ):
        """
        Flash the firmware to the ESP.
        """
        import json

        # Read flasher_args.json file
        flasher_args_path = build_dir / "flasher_args.json"
        if not flasher_args_path.exists():
            raise FileNotFoundError(
                f"flasher_args.json not found at {flasher_args_path}"
            )

        with open(flasher_args_path, "r") as f:
            flasher_args = json.load(f)

        flash_files = flasher_args.get("flash_files", {})
        if not flash_files:
            raise ValueError("No flash_files found in flasher_args.json")

        # Flash each file
        with detect_chip(port=port, connect_attempts=3) as esp:
            esp.change_baud(baud)
            attach_flash(esp)
            for addr_hex, filename in flash_files.items():
                offset = int(addr_hex, 16)
                file_path = build_dir / filename

                if not file_path.exists():
                    raise FileNotFoundError(f"Binary file not found: {file_path}")

                with open(file_path, "rb") as bin_file:
                    write_flash(esp, [(offset, bin_file)], compress=True)
            reset_chip(esp, reset_mode="hard-reset" if reset_after else "no-reset")

    def build_and_flash(
        self,
        build_dir: Path,
        port: str,
        factory_config: FactoryConfig,
        add_configs: dict[str, str] = {},
    ):
        """
        Build and flash the firmware with the additional configs, and the factory partition.
        """
        # Build the firmware
        self.build(build_dir, add_configs=add_configs)
        factory_config.read_factory_config(build_dir / "config" / "sdkconfig.h")

        # Flash the factory partition
        esp_bin_path = factory_config.to_idf_binary()
        self.flash_partition(port, esp_bin_path, self.partition_info["fctry"])
        factory_config.clean_up()

        # Flash the firmware
        self.flash(port, build_dir)


class EspPortManager:
    @staticmethod
    def get_ports(
        port_compatible_fn: Optional[Callable[[ESPLoader], bool]] = None,
    ) -> list[tuple[str, str]]:
        """
        Get the ESP ports.
        Returns [ (esp_target, esp_port), ... ]
        """
        esp_ports = []
        for port in get_port_list(*parse_port_filters([])):
            print(f"Checking port: {port}")
            try:
                with detect_chip(port=port, connect_attempts=3) as chip:
                    if port_compatible_fn is not None and not port_compatible_fn(chip):
                        print(f"Port {port} is not compatible, skipping")
                        continue
                    for chip_target, chip_def in CHIP_DEFS.items():
                        if chip_def.CHIP_NAME == chip.CHIP_NAME:
                            esp_ports.append((chip_target, port))
                            break

                    continue
            except Exception as e:
                msg = str(e).lower()
                print(f"Error checking port: {port} -> {msg}")
                continue

        return esp_ports
