# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import boto3
import subprocess
import os
from botocore.exceptions import ClientError
import sys
import time
from pathlib import Path

from multiprocessing.managers import BaseManager
from multiprocessing import Queue
import threading
from typing import Optional
from esptool import ESPLoader
from esptool.cmds import detect_flash_size, attach_flash

# test/ and tools/common reach sys.path via ``pythonpath`` in pytest.ini.

from rmng_backend import (
    DRX_PATH,
    User,
    Group,
    generate_random_email,
)
from util.ota_aws import OTAManager
from helpers.webhook_mock import TEST_INFRA_STACK, webhook_mock_infra
from resource_pool import ResourcePool
from fwlib.instances.request import (
    FirmwareInstanceManager,
    REQUEST_FIRMWARE_INSTANCE_TYPES,
    REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR,
    REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY,
    REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_JSON,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_NO_BASIC_INGEST,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC1,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC2,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC1,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC2,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_ON_CHAL_RESP,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE,
)
from credentials_store import RM_CONFIG

### Globals ###

# Set in the manager server process by ``server_process_setup`` from ``--no-esp`` / ``--no-posix``.
_TEST_ESP = True
_TEST_POSIX = True


def pytest_addoption(parser):
    parser.addoption(
        "--no-esp",
        action="store_true",
        default=False,
        help="Skip ESP firmware instances (default: run ESP tests)",
    )
    parser.addoption(
        "--no-posix",
        action="store_true",
        default=False,
        help="Skip POSIX firmware instances (default: run POSIX tests)",
    )
    parser.addoption(
        "--max-concurrent-build-jobs",
        type=int,
        default=None,
        metavar="N",
        help=(
            "Cap concurrent CMake/Ninja/idf.py build jobs in the firmware manager server "
            "(default: env PYTEST_MAX_CONCURRENT_BUILD_JOBS or 2; 0 = unlimited)"
        ),
    )
    parser.addoption(
        "--force-ipv4",
        action="store_true",
        default=False,
        help=(
            "Build firmware with CONFIG_OSAL_MQTT_CORE_FORCE_IPV4=y so the broker is reached "
            "over IPv4 only. Use on runners whose IPv6 egress is black-holed (avoids the "
            "~60s connect stall). POSIX builds only."
        ),
    )


class HostCtrlStdout:
    def __init__(self, queue, is_err_stream: bool = False):
        self.queue = queue
        self.is_err_stream = is_err_stream

    def write(self, message):
        # We put the message in the queue.
        # The Master process triggers the actual print()
        self.queue.put((self.is_err_stream, message))

    def flush(self):
        pass


def server_process_setup(
    log_queue,
    test_esp: bool = True,
    test_posix: bool = True,
    max_concurrent_build_jobs: int = 2,
    force_ipv4: bool = False,
):
    """
    This runs INSIDE the Server Process immediately on startup.
    We hijack stdout/stderr and point them to the queue.
    """
    global _TEST_ESP, _TEST_POSIX
    _TEST_ESP = test_esp
    _TEST_POSIX = test_posix
    # A spawned child copies the parent's ``sys.path``, but not module state: these two
    # modules are imported fresh here, so their process-wide knobs must be re-applied.
    from util.build_job_slot import configure_max_concurrent_build_jobs
    from util.build_opts import configure_force_ipv4

    configure_max_concurrent_build_jobs(max_concurrent_build_jobs)
    configure_force_ipv4(force_ipv4)
    sys.stdout = HostCtrlStdout(log_queue, is_err_stream=False)
    sys.stderr = HostCtrlStdout(log_queue, is_err_stream=True)


class SharedManagers(BaseManager):
    """
    Shared managers for the test suite.
    """

    def __init__(
        self,
        port: int = 0,
        test_esp: bool = True,
        test_posix: bool = True,
        max_concurrent_build_jobs: int = 2,
        force_ipv4: bool = False,
    ):
        super().__init__(address=("127.0.0.1", port), authkey=b"secret")
        self._test_esp = test_esp
        self._test_posix = test_posix
        self._max_concurrent_build_jobs = max_concurrent_build_jobs
        self._force_ipv4 = force_ipv4

    def start(self, log_queue: Queue):
        self.log_queue = log_queue
        super().start(
            initializer=server_process_setup,
            initargs=(
                log_queue,
                self._test_esp,
                self._test_posix,
                self._max_concurrent_build_jobs,
                self._force_ipv4,
            ),
        )

    def end_queue(self):
        self.log_queue.put((False, None))


def monitor_logs(queue, capture_output=True):
    while True:
        is_err_stream, message = queue.get()
        if message is None:  # Sentinel to stop
            break
        # Only write to console if output is not being captured (i.e., -s flag used)
        if not capture_output:
            if is_err_stream:
                sys.stderr.write(message)
                sys.stderr.flush()
            else:
                sys.stdout.write(message)
                sys.stdout.flush()


class FlagManager:
    def __init__(self):
        self.flags = {}
        self._lock = threading.Lock()

    def get_and_set_flag(self, flag: str, value: bool) -> bool:
        with self._lock:
            old_value = self.flags.get(flag, False)
            self.flags[flag] = value
            return old_value


class SysOutManager:
    def __init__(self):
        self.sysout_file = open("sysout.txt", "a")
        self.lock = threading.Lock()

    def print(self, line: str):
        with self.lock:
            self.sysout_file.write(f"{line}\n")
            self.sysout_file.flush()

    def close(self):
        with self.lock:
            self.sysout_file.close()


class SummaryManager:
    def __init__(self):
        self.summary_lines = []
        self._lock = threading.Lock()

    def add_summary_section(
        self, label: str, lines: list[str], side_line_len: int = 20
    ):
        with self._lock:
            self.summary_lines.append(
                f"{'=' * side_line_len} {label} {'=' * side_line_len}"
            )
            self.summary_lines.extend(lines)
            self.summary_lines.append("-" * (side_line_len * 2 + 2 + len(label)))

    def get_summary_lines(self) -> list[str]:
        with self._lock:
            return self.summary_lines.copy()


# Globals for the master process to use
_SERVER_INSTANCE = None
_FLAG_MANAGER = None
_OTA_MANAGER = None
_FIRMWARE_INSTANCE_MANAGER = None
_USER_POOL = None
_ADMIN_USER_POOL = None
_SUMMARY_MANAGER = None
_SYS_OUT_MANAGER = None


def _get_flag_manager():
    global _FLAG_MANAGER
    if _FLAG_MANAGER is None:
        _FLAG_MANAGER = FlagManager()
    return _FLAG_MANAGER


def _get_ota_manager():
    global _OTA_MANAGER
    if _OTA_MANAGER is None:
        _OTA_MANAGER = OTAManager()
    return _OTA_MANAGER


def _get_firmware_instance_manager():
    global _FIRMWARE_INSTANCE_MANAGER
    if _FIRMWARE_INSTANCE_MANAGER is None:
        _FIRMWARE_INSTANCE_MANAGER = FirmwareInstanceManager(
            port_compatible_fn=_check_esp_port_compatible,
            test_esp=_TEST_ESP,
            test_posix=_TEST_POSIX,
        )
    return _FIRMWARE_INSTANCE_MANAGER


def _get_user_pool():
    global _USER_POOL
    if _USER_POOL is None:
        # Lambda defers name lookup, since purge_leaked_user_groups is defined
        # further down. on_acquire runs a threshold-guarded purge so a user
        # leaked by a crashed/timed-out prior holder is cleaned before reuse,
        # without stomping another suite's in-flight groups on the same user.
        _USER_POOL = ResourcePool(
            _init_user,
            _reset_user,
            deinit_user,
            on_acquire=lambda u: purge_leaked_user_groups(u),
        )
    return _USER_POOL


def _get_admin_user_pool():
    global _ADMIN_USER_POOL
    if _ADMIN_USER_POOL is None:
        _ADMIN_USER_POOL = ResourcePool(
            _init_admin_user,
            _reset_user,
            deinit_user,
            on_acquire=lambda u: purge_leaked_user_groups(u),
        )
    return _ADMIN_USER_POOL


def _get_summary_manager():
    global _SUMMARY_MANAGER
    if _SUMMARY_MANAGER is None:
        _SUMMARY_MANAGER = SummaryManager()
    return _SUMMARY_MANAGER


def _get_sys_out_manager():
    global _SYS_OUT_MANAGER
    if _SYS_OUT_MANAGER is None:
        _SYS_OUT_MANAGER = SysOutManager()
    return _SYS_OUT_MANAGER


def _register_managers(manager: SharedManagers):
    manager.register("get_flag_manager", _get_flag_manager)
    manager.register("get_ota_manager", _get_ota_manager)
    manager.register("get_firmware_instance_manager", _get_firmware_instance_manager)
    manager.register("get_summary_manager", _get_summary_manager)
    manager.register("get_sys_out_manager", _get_sys_out_manager)


def _get_server_port_file(config) -> Path:
    return config.rootpath / "server_port"


def _get_server_port(config) -> int:
    # Get from file
    with open(_get_server_port_file(config), "r") as f:
        return int(f.read())


def _write_server_port(config, port: int):
    # Write to file
    with open(_get_server_port_file(config), "w") as f:
        f.write(str(port))


def _check_esp_port_compatible(loader: ESPLoader) -> bool:
    # Get flash size
    attach_flash(loader)
    flash_size = detect_flash_size(loader)
    if flash_size is None:
        return False
    flash_size = flash_size.strip()

    # Convert flash size string to bytes
    int_str, unit_str = flash_size[:-2], flash_size[-2:].upper()
    if unit_str == "KB":
        flash_size_int = int(int_str) * 1024
    elif unit_str == "MB":
        flash_size_int = int(int_str) * 1024 * 1024
    elif unit_str == "GB":
        flash_size_int = int(int_str) * 1024 * 1024 * 1024
    else:
        print(f"Unknown flash size unit: {unit_str}")
        return False

    # Need at least 4MB
    if flash_size_int < 4 * 1024 * 1024:
        print(f"Flash size is too small: {flash_size} < 4MB")
        return False

    return True


def _webhook_mock_session_items_need_mock(items) -> bool:
    return any(
        "webhook_mock_setup" in getattr(item, "fixturenames", ()) for item in items
    )


# Nodeids from workers do not include fixturenames; keep in sync with tests that request
# ``webhook_mock_setup`` (see test_firmware.py).
_WEBHOOK_TEST_NODEID_FRAGMENTS = (
    "test_firmware_alexa_notification",
    "test_firmware_gva_notification",
    "test_firmware_alexa_control",
)


def _webhook_mock_session_ids_need_mock(ids) -> bool:
    return any(
        any(frag in nodeid for frag in _WEBHOOK_TEST_NODEID_FRAGMENTS) for nodeid in ids
    )


def _webhook_mock_retryable_client_error(exc: ClientError) -> bool:
    code = exc.response.get("Error", {}).get("Code", "")
    return code in (
        "Throttling",
        "ThrottlingException",
        "TooManyRequestsException",
        "ProvisionedThroughputExceededException",
        "ServiceUnavailable",
        "InternalServerError",
        "RequestTimeout",
        "PriorRequestNotComplete",
        "ConnectionError",
    )


def _webhook_mock_enable():
    """
    Point rmng-notifications at the in-cloud webhook mock, and ensure dummy Alexa SSM
    params exist.

    The presence of webhook_mock_base_url is the switch the notifications Lambda reads
    (notifications_main.go resolveMockBaseURL); without it, proactive Alexa/GVA reports go
    to the real Amazon/Google endpoints and never reach the mock. webhook_mock_api_key is
    the gateway API key the Lambda must send with each delivery.

    No-op when the test infra is not deployed — the tests that need it skip on the same
    signal (see webhook_mock_setup in test_firmware.py).

    Always applies enabled state when this runs; we intentionally do not disable webhook mock
    on teardown because the deployment may be shared by concurrent test runs.

    Retries with exponential backoff when another process is updating the same deployment.
    """
    infra = webhook_mock_infra()
    if infra is None:
        print(
            f"Webhook mock not enabled: stack {TEST_INFRA_STACK} is not deployed "
            "(run `make itest-setup` in the backend repo). Alexa/GVA tests will skip."
        )
        return
    base_url, api_key = infra

    max_attempts = 10
    base_delay_s = 1.0
    max_delay_s = 60.0
    cmd = [
        "python",
        DRX_PATH,
        "update-env",
        "rmng-notifications",
        f"webhook_mock_base_url={base_url}",
        f"webhook_mock_api_key={api_key}",
    ]
    last_exc: Optional[BaseException] = None

    for attempt in range(max_attempts):
        try:
            if attempt == 0:
                print(
                    "Ensuring webhook mock is enabled for Alexa/GVA testing "
                    "(controller; retries with backoff if deployment is busy)..."
                )
            subprocess.run(cmd, check=True)
            ssm = boto3.client("ssm", region_name=RM_CONFIG["StackRegion"])

            def set_ssm_if_not_exists(name, value):
                try:
                    ssm.get_parameter(Name=name, WithDecryption=True)
                except ssm.exceptions.ParameterNotFound:
                    ssm.put_parameter(
                        Name=name, Value=value, Type="String", Overwrite=True
                    )

            set_ssm_if_not_exists("/rmng/alexa/client_id", "dummy_client_id")
            set_ssm_if_not_exists("/rmng/alexa/client_secret", "dummy_client_secret")
            return
        except subprocess.CalledProcessError as e:
            last_exc = e
        except ClientError as e:
            if not _webhook_mock_retryable_client_error(e):
                raise
            last_exc = e

        delay_s = min(base_delay_s * (2**attempt), max_delay_s)
        if attempt + 1 < max_attempts:
            print(
                f"webhook_mock enable attempt {attempt + 1}/{max_attempts} failed, "
                f"retrying in {delay_s:.1f}s: {last_exc}"
            )
            time.sleep(delay_s)

    assert last_exc is not None
    raise last_exc


def pytest_collection_finish(session):
    """
    Non-xdist: controller collects in-process, so enable here when needed.

    With pytest-xdist, workers collect and the controller does not run this hook in a useful
    way for global setup; see ``pytest_xdist_node_collection_finished``.
    """
    if hasattr(session.config, "workerinput"):
        return
    if getattr(session.config.option, "collectonly", False):
        return
    if not _webhook_mock_session_items_need_mock(session.items):
        return
    _webhook_mock_enable()
    session.config._webhook_mock_enabled_for_session = True


def pytest_xdist_node_collection_finished(node, ids):
    """
    xdist only: runs on the controller each time a worker finishes collecting (same ids per
    worker). Enable the shared webhook mock once before any test runs.
    """
    if getattr(node.config.option, "collectonly", False):
        return
    if not _webhook_mock_session_ids_need_mock(ids):
        return
    if getattr(node.config, "_webhook_mock_enabled_for_session", False):
        return
    _webhook_mock_enable()
    node.config._webhook_mock_enabled_for_session = True


def _resolved_max_concurrent_build_jobs(config) -> int:
    opt = config.getoption("--max-concurrent-build-jobs")
    if opt is not None:
        return int(opt)
    env_val = os.environ.get("PYTEST_MAX_CONCURRENT_BUILD_JOBS")
    if env_val is not None and env_val.strip() != "":
        return int(env_val)
    return 2


def pytest_configure(config):
    """
    Called before test run. If we are the master process,
    start the server and write connection info to a file.
    """
    manager = None
    if hasattr(config, "workerinput"):
        server_port = _get_server_port(config)
        manager = SharedManagers(port=server_port)
        _register_managers(manager)
        manager.connect()
    else:
        # Require initialization on the master process only
        # Make the shared manager instance
        log_queue = Queue()
        test_esp = not config.getoption("--no-esp")
        test_posix = not config.getoption("--no-posix")
        max_build = _resolved_max_concurrent_build_jobs(config)
        force_ipv4 = config.getoption("--force-ipv4")
        manager = SharedManagers(
            test_esp=test_esp,
            test_posix=test_posix,
            max_concurrent_build_jobs=max_build,
            force_ipv4=force_ipv4,
        )

        # Register the managers required
        _register_managers(manager)

        # Start and connect the manager server
        manager.start(log_queue=log_queue)
        global _SERVER_INSTANCE
        _SERVER_INSTANCE = manager

        # Save the server port for workers to connect to
        server_port = manager.address[1]
        _write_server_port(config, server_port)

        # Start the log monitor thread
        # Check if output should be captured (True) or displayed with -s (False)
        capture_output = config.getoption("capture", "fd") != "no"
        log_monitor_thread = threading.Thread(
            target=monitor_logs, args=(log_queue, capture_output), daemon=True
        )
        log_monitor_thread.start()

    # Save the manager to the config
    config.worker_shared_managers = manager


def pytest_sessionfinish(session, exitstatus):
    """Cleanup after tests finish."""
    if hasattr(session.config, "workerinput"):
        # We are a worker. Do not clean up.
        return

    # Clean up the underlying managers
    shared_manager = session.config.worker_shared_managers
    summary_manager = shared_manager.get_summary_manager()
    firmware_instance_manager = shared_manager.get_firmware_instance_manager()

    # Best-effort cleanup of any OTA resources this run created. Only touches tracked
    # IDs, so it is safe on the shared AWS account. Skipped if no OTA test ever ran.
    if _OTA_MANAGER is not None:
        try:
            _OTA_MANAGER.cleanup_created_resources()
        except Exception as e:
            print(f"WARNING: OTA cleanup_created_resources failed: {e}")

    # Stop simulators and drain pools first so POSIX processes exit and flush .gcda into
    # run-*/gcda/ before generate_coverage_report() scans the build tree.
    coverage_reports: list[str] = []
    if firmware_instance_manager is not None:
        firmware_instance_manager.destroy()
        coverage_reports = firmware_instance_manager.generate_coverage_reports()
    if coverage_reports:
        summary_manager.add_summary_section(
            "Coverage Reports",
            [f"-> {coverage_report}" for coverage_report in coverage_reports],
        )

    # Print the summary lines
    lines = summary_manager.get_summary_lines()
    if len(lines) > 0:
        print("\n")
        print("#" * 80)
        print("Test Suite Summary:")
        for msg in lines:
            print(msg)
        print("#" * 80)
        print("\n")

    # Close the sys out manager
    sys_out_manager = shared_manager.get_sys_out_manager()
    sys_out_manager.close()

    # Stop the manager server
    shared_manager.shutdown()
    shared_manager.end_queue()


# Dynamically parametrize the firmware instances
def pytest_generate_tests(metafunc):
    """
    Dynamically parametrize the firmware instances.
    """
    # Get the firmware instance manager
    firmware_instance_manager = (
        metafunc.config.worker_shared_managers.get_firmware_instance_manager()
    )

    test_esp = not metafunc.config.getoption("--no-esp")
    test_posix = not metafunc.config.getoption("--no-posix")

    def get_params(
        instance_type: REQUEST_FIRMWARE_INSTANCE_TYPES,
        add_esp: Optional[bool] = None,
        add_posix: Optional[bool] = None,
    ):
        ae = test_esp if add_esp is None else add_esp
        ap = test_posix if add_posix is None else add_posix
        requests = firmware_instance_manager.get_requests(
            instance_type, add_esp=ae, add_posix=ap
        )
        return requests, [request.to_key() for request in requests]

    # OTA instances
    params_cbor, ids_cbor = get_params(REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR)
    params_cbor_no_sig, ids_cbor_no_sig = get_params(
        REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_CBOR_NO_SIG_VERIFY
    )
    params_json, ids_json = get_params(REQUEST_FIRMWARE_INSTANCE_TYPE_OTA_MQTT_JSON)
    if "firmware_instance_request_ota_default" in metafunc.fixturenames:
        # Default to CBOR
        metafunc.parametrize(
            "firmware_instance_request_ota_default",
            params_cbor,
            ids=ids_cbor,
            scope="session",
        )
    if "firmware_instance_request_ota_all" in metafunc.fixturenames:
        # All OTA instances
        all_params, all_ids = (
            params_cbor + params_json,
            ids_cbor + ids_json,
        )
        metafunc.parametrize(
            "firmware_instance_request_ota_all",
            all_params,
            ids=all_ids,
            scope="session",
        )
    if "firmware_instance_request_ota_no_sig_verify" in metafunc.fixturenames:
        # OTA with signature verification disabled, no codesign cert
        metafunc.parametrize(
            "firmware_instance_request_ota_no_sig_verify",
            params_cbor_no_sig,
            ids=ids_cbor_no_sig,
            scope="session",
        )
    # Host control instances
    if "firmware_instance_request_host_ctrl" in metafunc.fixturenames:
        # Default host_ctrl request type used by existing firmware tests.
        params, ids = get_params(REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT)
        metafunc.parametrize(
            "firmware_instance_request_host_ctrl", params, ids=ids, scope="session"
        )
    if "firmware_instance_request_host_ctrl_local_ctrl" in metafunc.fixturenames:
        # Local control (no challenge-response) over both SEC1 and SEC2 build variants.
        params_sec1, ids_sec1 = get_params(
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC1
        )
        params_sec2, ids_sec2 = get_params(
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LOCAL_CTRL_SEC2
        )
        metafunc.parametrize(
            "firmware_instance_request_host_ctrl_local_ctrl",
            params_sec1 + params_sec2,
            ids=ids_sec1 + ids_sec2,
            scope="session",
        )
    if "firmware_instance_request_host_ctrl_lc_chal_resp" in metafunc.fixturenames:
        # Local control challenge-response over both SEC1 and SEC2 build variants.
        params_sec1, ids_sec1 = get_params(
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC1
        )
        params_sec2, ids_sec2 = get_params(
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_LC_CHAL_RESP_SEC2
        )
        metafunc.parametrize(
            "firmware_instance_request_host_ctrl_lc_chal_resp",
            params_sec1 + params_sec2,
            ids=ids_sec1 + ids_sec2,
            scope="session",
        )
    if "firmware_instance_request_host_ctrl_on_chal_resp" in metafunc.fixturenames:
        params, ids = get_params(REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_ON_CHAL_RESP)
        metafunc.parametrize(
            "firmware_instance_request_host_ctrl_on_chal_resp",
            params,
            ids=ids,
            scope="session",
        )
    if "firmware_instance_request_host_ctrl_bridge" in metafunc.fixturenames:
        params, ids = get_params(REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_BRIDGE)
        metafunc.parametrize(
            "firmware_instance_request_host_ctrl_bridge",
            params,
            ids=ids,
            scope="session",
        )
    if (
        "firmware_instance_request_host_ctrl_basic_ingest_variants"
        in metafunc.fixturenames
    ):
        params_default, ids_default = get_params(
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT
        )
        params_nbi, ids_nbi = get_params(
            REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_NO_BASIC_INGEST
        )
        metafunc.parametrize(
            "firmware_instance_request_host_ctrl_basic_ingest_variants",
            params_default + params_nbi,
            ids=ids_default + ids_nbi,
            scope="session",
        )


@pytest.fixture(scope="session")
def firmware_instance_manager(pytestconfig):
    """
    Fixture to get the firmware instance manager.
    """
    shared_manager = pytestconfig.worker_shared_managers
    return shared_manager.get_firmware_instance_manager()


@pytest.fixture(scope="session")
def user_pool():
    """
    Fixture to get the user pool.
    """
    user_pool = _get_user_pool()
    yield user_pool
    user_pool.drain_free()


@pytest.fixture(scope="session")
def admin_user_pool():
    """
    Fixture to get the admin user pool.
    """
    admin_user_pool = _get_admin_user_pool()
    yield admin_user_pool
    admin_user_pool.drain_free()


# Post collection setup (after counting in pytest_generate_tests)
@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(session, items):
    if not hasattr(session.config, "worker_shared_managers"):
        # No shared managers found
        return

    # Get the shared managers
    shared_manager = session.config.worker_shared_managers

    # Check if the items have already been checked
    flag_manager = shared_manager.get_flag_manager()
    if flag_manager.get_and_set_flag("items_checked", True):
        # Items have already been checked. Do not modify them.
        return

    # Count the number of tests after parametrization, excluding skipped tests
    for item in items:
        # Skip if the test is marked with @pytest.mark.skip
        if item.get_closest_marker("skip"):
            continue

        # Skip Alexa/GVA notification firmware tests on esp32c2 before any fixtures run (no binary build)
        if "esp32c2" in item.nodeid and (
            "test_firmware_alexa_notification" in item.nodeid
            or "test_firmware_gva_notification" in item.nodeid
        ):
            item.add_marker(
                pytest.mark.skip(
                    reason="Alexa/GVA notification tests not supported on esp32c2 yet"
                )
            )
            continue

        if (
            "firmware_instance_ota" in item.fixturenames
            or "firmware_instance_ota_all" in item.fixturenames
        ):
            ota_manager = shared_manager.get_ota_manager()
            firmware_instance_manager = shared_manager.get_firmware_instance_manager()

            if not ota_manager.get_latest_code_signing_profile():
                ota_manager.setup_infrastructure()
            codesign_cert_path = ota_manager.get_codesign_cert_path()
            assert codesign_cert_path is not None, "No code signing certificate found"
            firmware_instance_manager.set_codesign_cert_path(codesign_cert_path)

            break  # only do this once


# Post test summary
@pytest.fixture(scope="session")
def summary_manager(pytestconfig):
    shared_manager = pytestconfig.worker_shared_managers
    yield shared_manager.get_summary_manager()


@pytest.fixture(scope="session")
def add_summary_section(summary_manager):
    """
    Fixture to add a summary section to the post test summary.
    Usage: summary_message("section label", ["line 1", "line 2", "line 3"])
    """

    def _add_message(label: str, lines: list[str]):
        summary_manager.add_summary_section(label, lines)

    return _add_message


@pytest.fixture(scope="session")
def sys_out_manager(pytestconfig):
    shared_manager = pytestconfig.worker_shared_managers
    yield shared_manager.get_sys_out_manager()


@pytest.fixture(scope="session")
def sys_out_print(sys_out_manager):
    def _print(line: str):
        sys_out_manager.print(line)

    return _print


def delete_user_groups(user):
    # Ensure fresh credentials before listing groups
    user.get_aws_credentials()

    group_api = Group(user=user)
    groups = group_api.list_groups()
    for group in groups.get("groups", []):
        print("Deleting group", group["group_id"])
        group_api.delete_group(group_id=group["group_id"], warn_error=True)


# Acquire-time group count above which a pooled user is treated as leaked and
# purged before use. The assume_role session policy grows ~4 ARNs/group and STS
# caps inline session policies at 2048 chars, so a user with this many groups
# risks a 500 ("Error assuming role") during MQTT setup. Kept comfortably below
# that limit but above any single test's legitimate in-flight group count, so a
# concurrent suite's transient groups on the same shared user are never purged.
ACQUIRE_PURGE_GROUP_THRESHOLD = 4


def purge_leaked_user_groups(user):
    """on_acquire hook: purge groups only when a pooled user is clearly leaked.

    release() resets users on the happy path; this covers prior holders that
    crashed/timed out and never released. Guarded by a threshold so it does not
    delete the small number of groups another suite may have in flight on the
    same shared user."""
    try:
        user.get_aws_credentials()
        groups = Group(user=user).list_groups().get("groups", [])
    except Exception as e:
        print(f"Warning listing groups on acquire: {e}")
        return
    if len(groups) < ACQUIRE_PURGE_GROUP_THRESHOLD:
        return
    print(
        f"Acquire purge: user has {len(groups)} groups (>= {ACQUIRE_PURGE_GROUP_THRESHOLD}), clearing leaked state"
    )
    delete_user_groups(user)


def _init_test_user(password: str, is_admin: bool = False):
    """Initialize a user with a unique, randomly generated email address.

    No mailbox is read here — the backend lambda force-creates and confirms the
    user in Cognito — so a readable inbox is not required. The random address is
    collision-safe across xdist workers and across the backend itest suite.

    Tries to authenticate with existing user first, only creates new user if authentication fails.
    """
    user_tag = "admin user" if is_admin else "user"
    email = generate_random_email()
    if not email:
        raise ValueError("Failed to generate test email address")

    # Both pool ids are always passed: the SDK picks the admin pool for admin sign-in and
    # the end-user pool for end-user provisioning, and its cleanup helpers try both.
    user = User(
        username=email,
        password=password,
        region=RM_CONFIG["StackRegion"],
        identity_pool_id=RM_CONFIG["IdentityPoolId"],
        api_gateway_url=RM_CONFIG["ApiGatewayUrl"],
        user_api_gateway_url=RM_CONFIG["UserApiGatewayUrl"],
        iot_endpoint=RM_CONFIG["IoTEndpointUrl"],
        admin_user_pool_id=RM_CONFIG["AdminUserPoolId"],
        admin_client_id=RM_CONFIG["AdminUserPoolClientId"],
        end_user_pool_id=RM_CONFIG["UserPoolId"],
        is_super_admin=is_admin,
    )
    user.test_email = email

    # Try to authenticate first - if successful, user already exists and we can reuse it
    try:
        response = user.signin(is_admin=is_admin)
        if response.status_code == 200:
            # Verify that tokens were actually set - if not, the user might exist but be in a bad state
            if user.token:
                # User exists and authentication succeeded - reuse it
                print(f"[User] Reusing existing {user_tag}: {email}")
                # Still need to register client and get credentials
                try:
                    user.register_client(
                        platform_type="ios-dummy",
                        mobile_device_token="ios-user-device-token",
                    )
                except Exception as e:
                    # Client might already be registered, that's okay
                    print(f"[User] Note: Client registration: {e}")
                # Get credentials - if this fails, we'll fall through to create a new user
                creds = user.get_aws_credentials()
                if creds:
                    return user
                else:
                    print(
                        f"[User] Failed to get credentials for existing {user_tag}, will create new {user_tag}"
                    )
            else:
                print(
                    f"[User] Signin succeeded but no token received, will create new {user_tag}"
                )
    except Exception as e:
        print(f"[User] Authentication failed ({user_tag} may not exist): {e}")

    # User doesn't exist or authentication failed - create new user (or lambda may force-set password if user exists)
    print(f"[User] Creating new {user_tag}: {email}")
    if is_admin:
        # Super admins live in the admin Cognito pool with custom:super_admin stamped; the
        # end-user provisioning path does not apply to them.
        if (
            user.create_super_admin_via_cognito(
                email=user.username, password=user.password
            )
            is None
        ):
            raise Exception(f"Failed to provision {user_tag} {email} in the admin pool")
        register_response = user.signin(is_admin=True)
    else:
        register_response = user.register_user_via_lambda(
            email=user.username, password=user.password
        )
    # Lambda may have created the user or force-set password; allow Cognito propagation then get credentials before register_client
    time.sleep(2)
    creds = user.get_aws_credentials()
    if (
        not creds
        and isinstance(register_response, dict)
        and "already exists" in str(register_response.get("message", ""))
    ):
        # Retry once after delay for Cognito propagation when lambda force-set password
        time.sleep(3)
        creds = user.get_aws_credentials()
    if not creds:
        raise Exception(
            f"Failed to obtain AWS credentials after registration. {user_tag.capitalize()} may need to authenticate first."
        )
    user.register_client(
        platform_type="ios-dummy", mobile_device_token="ios-user-device-token"
    )
    return user


def _init_user():
    return _init_test_user("TestPassword1!", is_admin=False)


def _reset_user(user):
    # Clean up groups created by this user between tests
    try:
        delete_user_groups(user)
    except Exception as e:
        print(f"Warning cleaning user groups: {e}")
    # Best-effort disconnect MQTT if connected
    try:
        user.mqtt_disconnect_and_wait()
    except Exception:
        pass


def deinit_user(user):
    # Reset the user
    _reset_user(user)


def _init_admin_user():
    return _init_test_user("SuperAdminPassword1!", is_admin=True)


@pytest.fixture
def test_user1(user_pool):
    user = user_pool.acquire()
    print("[User] Acquired user:", user.sub)
    try:
        yield user
    finally:
        user_pool.release(user)


@pytest.fixture
def test_user2(user_pool):
    user = user_pool.acquire()
    print("[User] Acquired user:", user.sub)
    try:
        yield user
    finally:
        user_pool.release(user)


@pytest.fixture
def test_user3(user_pool):
    user = user_pool.acquire()
    print("[User] Acquired user:", user.sub)
    try:
        yield user
    finally:
        user_pool.release(user)


@pytest.fixture
def test_user4(user_pool):
    user = user_pool.acquire()
    print("[User] Acquired user:", user.sub)
    try:
        yield user
    finally:
        user_pool.release(user)


@pytest.fixture
def test_user5(user_pool):
    user = user_pool.acquire()
    print("[User] Acquired user:", user.sub)
    try:
        yield user
    finally:
        user_pool.release(user)


@pytest.fixture
def super_admin_user(admin_user_pool):
    user = admin_user_pool.acquire()
    print("[User] Acquired super-admin user:", user.sub)
    try:
        yield user
    finally:
        admin_user_pool.release(user)
