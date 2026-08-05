# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
import time
from datetime import datetime, timedelta
from random import shuffle

import os
import re

from fwlib.instances.request import (
    FirmwareInstanceRequest,
    REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT,
)


from util.ota_aws import (
    OTAFilesConfig,
    OTAFile,
    ota_warn,
    RmngOtaInfo,
    RmngOtaDownloadWindow,
    DayTime,
)
from rmng_backend import Group
from payload import extract_shadow_reported_state
from typing import Optional, Callable
import threading
from functools import partial

# Poll interval in seconds
POLL_INTERVAL = 5
# Max attempts to track job execution
MAX_ATTEMPTS = 60

# OTA versions
OTA_VERSIONS = [
    "1.1.0",
    "1.5.0",
    "2.0.0",
    "2.5.0",
    "3.0.0",
]

# Patch-only bumps over the device's baseline (1.0.0). These exercise the patch
# field of the version comparator, which was previously dropped (so 1.0.0, 1.0.1,
# 1.0.2 all compared equal and every patch upgrade was rejected as too low).
PATCH_VERSIONS = [
    "1.0.1",
    "1.0.2",
    "1.0.3",
]


# Helper functions for creating time-aware download windows
def create_invalid_daily_window():
    """Create a daily window that excludes the current hour"""
    now = datetime.now()
    start_h = (now.hour - 2) % 24
    end_h = (now.hour - 1) % 24
    return RmngOtaDownloadWindow(
        start_time=now - timedelta(hours=1),
        end_time=now + timedelta(hours=2),
        daily_start=DayTime(start_h, 0),
        daily_end=DayTime(end_h, 0),
    )


def create_valid_daily_window():
    """Create a daily window that includes typical test execution hours"""
    # Broad window that covers most business hours
    return RmngOtaDownloadWindow(
        start_time=datetime.now() - timedelta(hours=1),
        end_time=datetime.now() + timedelta(hours=2),
        daily_start=DayTime(0, 0),  # All day
        daily_end=DayTime(23, 59),
    )


# Helper function to create a mock job document
def create_mock_job_document(
    protocols: Optional[list[str]] = ["MQTT"],
    streamname: Optional[str] = "AFR_OTA_STREAM",
    filepath: Optional[str] = "/",
    fileid: Optional[int] = 0,
    certfile: Optional[str] = "/",
    filesize: Optional[int] = 1024,
    sig_sha256_ecdsa: Optional[str] = "test-signature",
    fw_version: Optional[str] = "1.0.0",
    file_md5: Optional[str] = None,
) -> dict:
    """Create a mock job document"""
    afr_ota = {}
    rmng_ota = {}
    if protocols is not None:
        afr_ota["protocols"] = protocols
    if streamname is not None:
        afr_ota["streamname"] = streamname
    if filesize is not None or sig_sha256_ecdsa is not None:
        afr_ota["files"] = [{}]
        if filepath is not None:
            afr_ota["files"][0]["filepath"] = filepath
        if fileid is not None:
            afr_ota["files"][0]["fileid"] = fileid
        if certfile is not None:
            afr_ota["files"][0]["certfile"] = certfile
        if filesize is not None:
            afr_ota["files"][0]["filesize"] = filesize
        if sig_sha256_ecdsa is not None:
            afr_ota["files"][0]["sig-sha256-ecdsa"] = sig_sha256_ecdsa
    if fw_version is not None:
        rmng_ota["fw_version"] = fw_version
    if file_md5 is not None:
        rmng_ota["file_md5"] = file_md5
    return {
        "afr_ota": afr_ota,
        "rmng_ota": rmng_ota,
    }


@pytest.fixture(scope="session")
def ota_manager(pytestconfig):
    """
    Fixture to get the OTA manager.
    """
    shared_manager = pytestconfig.worker_shared_managers
    return shared_manager.get_ota_manager()


@pytest.fixture(scope="session")
def firmware_instance_request_ota_default(request):
    # Dynamically parametrized in conftest.py
    return request.param


@pytest.fixture(scope="session")
def firmware_instance_request_ota_all(request):
    # Dynamically parametrized in conftest.py
    return request.param


@pytest.fixture(scope="session")
def firmware_instance_request_ota_no_sig_verify(request):
    # Dynamically parametrized in conftest.py - OTA sim with signature verify disabled, no codesign cert
    return request.param


def _firmware_instance_ota(instance_request, firmware_instance_manager):
    """
    Fixture to create a firmware instance for OTA testing.
    """
    instance = firmware_instance_manager.dispatch(instance_request)
    yield instance
    firmware_instance_manager.return_instance(instance_request, instance)


@pytest.fixture(scope="function")
def firmware_instance_ota(
    firmware_instance_request_ota_default, firmware_instance_manager
):
    """
    Fixture to create a firmware instance for OTA testing, using the default OTA instance.
    """
    yield from _firmware_instance_ota(
        firmware_instance_request_ota_default, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def firmware_instance_ota_all(
    firmware_instance_request_ota_all, firmware_instance_manager
):
    """
    Fixture to create a firmware instance for OTA testing, using all OTA instances.
    """
    yield from _firmware_instance_ota(
        firmware_instance_request_ota_all, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def firmware_instance_ota_no_sig_verify(
    firmware_instance_request_ota_no_sig_verify, firmware_instance_manager
):
    """
    Fixture to create a firmware instance for OTA testing with signature verification disabled and no codesign cert.
    Use with jobs that skip signing (null signature) to test the non-enabled path.
    """
    yield from _firmware_instance_ota(
        firmware_instance_request_ota_no_sig_verify, firmware_instance_manager
    )


class ShadowTrackedTestEnv:
    """
    This tracks the named shadow updates and waits for a status subset.
    """

    @staticmethod
    def _recursive_update(base_dict, update_dict):
        """
        Recursively update a dictionary with another dictionary.
        """
        for key, value in update_dict.items():
            if (
                isinstance(value, dict)
                and key in base_dict
                and isinstance(base_dict[key], dict)
            ):
                ShadowTrackedTestEnv._recursive_update(base_dict[key], value)
            else:
                base_dict[key] = value

    def __init__(self, instance, user, group_id):
        self.instance = instance
        self.user = user
        self.group_id = group_id
        self.shadow_name = f"params-{group_id}"

        # Shadow state thread
        self.shadow_update_thread = None
        self.shadow_state = {}
        self.keep_alive = False

    def _shadow_update_thread(self):
        """
        Thread to read the shadow updates and update the shadow state.
        """
        while self.keep_alive:
            shadow_data = self.user.read_shadow_queue(timeout=0.1)  # every 100ms
            if shadow_data:
                extracted_state = extract_shadow_reported_state(shadow_data)
                if extracted_state:
                    self._recursive_update(self.shadow_state, extracted_state)
                else:
                    ota_warn(
                        f"Warning: No reported state in shadow data: {shadow_data}"
                    )

    def kill_instance(self) -> bool:
        # Activate killswitch and wait for offline
        self.update_state({"OTA Remote": {"Kill": True}})
        if not self.wait_on_state({"online": False}):
            print("WARN: Node did not signal it is offline after activating killswitch")
            return False

        # Stop the instance
        return self.instance.stop()

    def rollback_instance(self, start_after: bool = True) -> bool:
        """
        Rollback the instance to the original state.
        """
        # Kill the instance
        if not self.kill_instance():
            return False
        if not self.instance.reset_ota_state():
            return False
        if not start_after:
            return True

        # Start the instance and wait for online
        if not self.instance.start():
            return False
        return self.wait_on_state({"online": True})

    def setup_user(self):
        # Connect the user to MQTT and subscribe to the named shadow
        assert self.user.mqtt_connect(), "Failed to connect the user"
        self.user.subscribe_to_named_shadows(
            thing_name=self.instance.get_assoc_instance().node_thing_name,
            named_shadows=[self.shadow_name],
        )

        # Start the shadow update thread
        self.keep_alive = True
        self.shadow_update_thread = threading.Thread(target=self._shadow_update_thread)
        self.shadow_update_thread.start()

    def setup_instance(self):
        # Start the instance and wait for online
        self.instance.start()
        assert self.wait_on_state({"online": True}, timeout=60), (
            "Node did not signal it is online"
        )

        # Force default diagnostics state to pass. The DRet params are PROP_FLAG_PERSIST
        # in ota-sim, so a previous test can leave them on PENDING and they survive the
        # reboot -- hence the reset.
        default_diag_state = {"OTA Remote": {"DRet Init": 2, "DRet MQTT": 2}}
        if not self._dict_contains(self.shadow_state, default_diag_state):
            self.update_state(default_diag_state)
            assert self.wait_on_state(default_diag_state, timeout=60), (
                "Node did not acknowledge the default diagnostics state"
            )

    def cleanup_user(self):
        # Unsubscribe from the named shadow and disconnect from MQTT
        self.user.unsubscribe_from_named_shadows(
            thing_name=self.instance.get_assoc_instance().node_thing_name,
            named_shadows=[self.shadow_name],
        )
        self.user.mqtt_disconnect_and_wait()

    def cleanup_instance(self):
        # Kill the instance
        if not self.rollback_instance(start_after=False):
            print("WARN: Failed to kill instance, continuing anyway")

        # Stop the shadow update thread
        self.keep_alive = False
        self.shadow_update_thread.join()

    def _dict_contains(self, container: dict, target: dict) -> bool:
        """
        Check if target dict structure is contained within container dict.
        Handles nested dictionaries recursively.
        """
        for key, value in target.items():
            if key not in container:
                return False
            if isinstance(value, dict) and isinstance(container[key], dict):
                if not self._dict_contains(container[key], value):
                    return False
            elif container[key] != value:
                return False
        return True

    def wait_on_state(self, state_dict: dict, timeout: float = 30.0) -> bool:
        """
        Wait for state_dict to be present in shadow_state['state']['reported'].
        Returns True if found within timeout, False otherwise.
        state_dict can have multiple levels of nested dictionaries.
        """
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self._dict_contains(self.shadow_state, state_dict):
                return True
            time.sleep(0.5)  # every 500ms
        return False

    def update_state(self, state_dict: dict):
        """
        Update the shadow state with the given state_dict.
        """
        self.user.mqtt_publish_to_topic(
            thing_name=self.instance.factory_config.thing_name,
            topic_name=f"params-{self.group_id}/params",
            data=state_dict,
        )


def _associated_shadow_tracked_test_env_dormant_instance(instance, user):
    """
    Fixture to create a shadow tracked test environment with a firmware instance and a test user, associated to a new group.
    """

    # Do user-node association
    group_api = Group(user=user)
    group_id = group_api.create_group(group_name="Test Associated Group")
    result = user.do_user_node_assoc(
        device=instance.get_assoc_instance(), group_id=group_id
    )
    assert result is None, f"Association failed with error: {result}"

    test_env = ShadowTrackedTestEnv(instance, user, group_id)
    test_env.setup_user()

    # Yield the test environment
    yield test_env

    # Clean up
    test_env.cleanup_user()
    group_api.delete_group(group_id=group_id)


def _associated_shadow_tracked_test_env(instance, user):
    """
    Fixture to create a shadow tracked test environment with a firmware instance and a test user, associated to a new group.
    """
    for test_env in _associated_shadow_tracked_test_env_dormant_instance(
        instance, user
    ):
        test_env.setup_instance()
        yield test_env
        test_env.cleanup_instance()


@pytest.fixture(scope="function")
def associated_shadow_tracked_test_env_dormant_instance(
    firmware_instance_ota, test_user1
):
    """
    Fixture to create a shadow tracked test environment with a firmware instance and a test user, associated to a new group.
    Using the default OTA instance.
    """
    yield from _associated_shadow_tracked_test_env_dormant_instance(
        firmware_instance_ota, test_user1
    )


@pytest.fixture(scope="function")
def associated_shadow_tracked_test_env(firmware_instance_ota, test_user1):
    """
    Fixture to create a shadow tracked test environment with a firmware instance and a test user, associated to a new group.
    Using the default OTA instance.
    """
    yield from _associated_shadow_tracked_test_env(firmware_instance_ota, test_user1)


@pytest.fixture(scope="function")
def associated_shadow_tracked_test_env_all(firmware_instance_ota_all, test_user1):
    """
    Fixture to create a shadow tracked test environment with a firmware instance and a test user, associated to a new group.
    Using all OTA instances.
    """
    yield from _associated_shadow_tracked_test_env(
        firmware_instance_ota_all, test_user1
    )


@pytest.fixture(scope="function")
def associated_shadow_tracked_test_env_no_sig_verify(
    firmware_instance_ota_no_sig_verify, test_user1
):
    """
    Fixture to create a shadow tracked test environment for OTA with signature verification disabled (no codesign cert).
    Use with ota_job(..., skip_signing=True) to test the non-enabled path.
    """
    yield from _associated_shadow_tracked_test_env(
        firmware_instance_ota_no_sig_verify, test_user1
    )


def _ota_version_binary_builder(instance_request, firmware_instance_manager):
    """
    Fixture to get a function that builds a binary file with the given version.
    """

    def _build_version_binary(version: str):
        return firmware_instance_manager.build_version_binary_if_not_built(
            instance_request, version
        )

    yield _build_version_binary


@pytest.fixture(scope="function")
def ota_version_binary_builder(
    firmware_instance_request_ota_default, firmware_instance_manager
):
    """
    Fixture to get a function that builds a binary file with the given version.
    """
    yield from _ota_version_binary_builder(
        firmware_instance_request_ota_default, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def ota_version_binary_builder_all(
    firmware_instance_request_ota_all, firmware_instance_manager
):
    """
    Fixture to get a function that builds a binary file with the given version.
    """
    yield from _ota_version_binary_builder(
        firmware_instance_request_ota_all, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def ota_version_binary_builder_no_sig_verify(
    firmware_instance_request_ota_no_sig_verify, firmware_instance_manager
):
    """
    Fixture to get a function that builds a binary for the no-signature-verify OTA instance.
    """
    yield from _ota_version_binary_builder(
        firmware_instance_request_ota_no_sig_verify, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def device_sim_version_binary_builder(
    firmware_instance_request_ota_default, firmware_instance_manager
):
    """
    Build a binary at a given version from the device-sim example (a different
    project than ota-sim). Reuses the existing device-sim factory by deriving
    a host_ctrl-default request that shares target + network with the OTA request.

    Used to validate that the OTA image-header verifier rejects a cross-project
    binary (matching version, mismatching project_name) without any binary
    tampering — the device-sim build is internally consistent (correct image
    SHA256 and, for ESP-IDF, a fresh signature via the standard build path).
    """
    device_sim_request = FirmwareInstanceRequest(
        instance_type=REQUEST_FIRMWARE_INSTANCE_TYPE_HOST_CTRL_DEFAULT,
        target=firmware_instance_request_ota_default.target,
        network_type=firmware_instance_request_ota_default.network_type,
    )

    def _build(version: str):
        return firmware_instance_manager.build_version_binary_if_not_built(
            device_sim_request, version
        )

    yield _build


@pytest.fixture(scope="function")
def ota_default_version_binary(ota_version_binary_builder):
    """
    Fixture to get the default version binary and version.
    """
    return ota_version_binary_builder(OTA_VERSIONS[0]), OTA_VERSIONS[0]


@pytest.fixture(scope="function")
def mock_ota_job(ota_manager):
    """
    Fixture to create a mock OTA job.
    """
    job_ids = []

    def _mock_ota_job(job_document: dict, thing_name: str):
        job_id = ota_manager.create_mock_job_with_document(
            document=job_document, thing_name=thing_name
        )
        assert job_id is not None, "Failed to create mock OTA job"
        job_ids.append(job_id)
        return job_id

    yield _mock_ota_job

    # Cleanup: force cancel, wait for terminal state, then delete (with throttle retry).
    # Best-effort: emit a summary if any job failed cleanup so it's visible in test output.
    failed = []
    for job_id in job_ids:
        try:
            ota_manager.cancel_custom_job(job_id, force=True)
        except Exception as e:
            ota_warn(f"Cancel failed for mock job {job_id}: {e}")
        try:
            if not ota_manager.delete_job(job_id):
                failed.append(job_id)
        except Exception as e:
            ota_warn(f"Delete failed for mock job {job_id}: {e}")
            failed.append(job_id)
        time.sleep(6)  # pace under AWS IoT delete-job rate limit
    if failed:
        ota_warn(f"Mock OTA job cleanup failed for {len(failed)} job(s): {failed}")


@pytest.fixture(scope="function")
def ota_job(ota_manager):
    """
    Fixture to create an OTA job and handle cleanup.

    This fixture is designed to be used with pytest.mark.parametrize
    to provide the specific binary_path, thing_name, and binary_name.
    """
    job_ids = []

    def _create_job(
        binary_path, thing_name, ota_info: RmngOtaInfo, skip_signing: bool = False
    ):
        file_config = OTAFilesConfig(
            files=[
                OTAFile(
                    name=f"ota-sim-{ota_info.fw_version}.bin",
                    path=binary_path,
                    file_id=0,
                    needs_signing=not skip_signing,
                )
            ]
        )
        job_id = ota_manager.create_rmng_ota_job(
            files_config=file_config,
            ota_info=ota_info,
            thing_name=thing_name,
        )
        assert job_id is not None, "Failed to create job"
        job_ids.append(job_id)
        return job_id

    yield _create_job

    # Cleanup: force cancel, then delete. delete_job() now waits out CANCELING and
    # uses force=True + throttle-aware retry, so the cancel→delete race is handled.
    failed = []
    for job_id in job_ids:
        try:
            if job_id.startswith("ota-update-"):
                ota_manager.cancel_ota_job(job_id, force=True)
            else:
                ota_manager.cancel_custom_job(job_id, force=True)
        except Exception as e:
            ota_warn(f"Cancel failed for job {job_id}: {e}")
        try:
            if not ota_manager.delete_job(job_id):
                failed.append(job_id)
        except Exception as e:
            ota_warn(f"Delete failed for job {job_id}: {e}")
            failed.append(job_id)
        time.sleep(6)  # pace under AWS IoT delete-job rate limit
    if failed:
        ota_warn(f"OTA job cleanup failed for {len(failed)} job(s): {failed}")


@pytest.mark.firmware
def test_ota_single_version(
    ota_version_binary_builder_all,
    associated_shadow_tracked_test_env_all,
    ota_manager,
    ota_job,
):
    """
    Test OTA with a single version update.
    """
    test_env = associated_shadow_tracked_test_env_all
    thing_name = test_env.instance.factory_config.thing_name
    version = OTA_VERSIONS[0]
    binary_path = ota_version_binary_builder_all(version)

    # Create an OTA job using the fixture
    job_id = ota_job(binary_path, thing_name, RmngOtaInfo(fw_version=version))
    time.sleep(10)

    # Track the job
    job_status = ota_manager.wait_for_execution_status(job_id, thing_name, "SUCCEEDED")
    assert job_status is not None, "OTA job failed or timed out"

    # Check that the firmware version is the expected version in cloud
    assert test_env.wait_on_state({"OTA Remote": {"FW Version": version}}, timeout=5), (
        f"Firmware version is not '{version}'"
    )


@pytest.mark.firmware
def test_ota_invalid_job_documents(
    associated_shadow_tracked_test_env_dormant_instance, ota_manager, mock_ota_job
):
    """
    Test OTA with invalid job documents.
    """
    test_env = associated_shadow_tracked_test_env_dormant_instance
    thing_name = test_env.instance.factory_config.thing_name

    # Create jobs with invalid job documents
    job_statuses = {}
    for invalid_job_document, expected_status in [
        # No protocols
        (create_mock_job_document(protocols=None), "REJECTED"),
        # MQTT present but not first: the Jobs parser reads protocols[0] only, so this
        # takes the HTTP branch and fails on the update_data_url fields we never emit
        (create_mock_job_document(protocols=["HTTP", "MQTT"]), "REJECTED"),
        # No streamname with MQTT protocol
        (create_mock_job_document(protocols=["MQTT"], streamname=None), "REJECTED"),
        # No filepath
        (create_mock_job_document(filepath=None), "REJECTED"),
        # No fileid
        (create_mock_job_document(fileid=None), "REJECTED"),
        # No certfile
        (create_mock_job_document(certfile=None), "REJECTED"),
        # No filesize
        (create_mock_job_document(filesize=None), "REJECTED"),
        # No sig-sha256-ecdsa
        (create_mock_job_document(sig_sha256_ecdsa=None), "REJECTED"),
        # No firmware version
        (create_mock_job_document(fw_version=None), "REJECTED"),
    ]:
        job_id = mock_ota_job(invalid_job_document, thing_name)
        job_statuses[job_id] = expected_status
        assert job_statuses[job_id] is not None, "OTA job failed or timed out"

    # Wait for jobs to be created
    time.sleep(10)

    # Start the instance after job creation to avoid race conditions
    test_env.setup_instance()

    # Wait for jobs to be processed
    time.sleep(15)

    try:
        # Check that the jobs are in the expected statuses
        for job_id, expected_status in job_statuses.items():
            job_status = ota_manager.wait_for_execution_status(
                job_id, thing_name, expected_status, poll_interval=3, max_attempts=10
            )
            assert job_status is not None, (
                f"Unexpected status for OTA job {job_id} - expected {expected_status}"
            )
    finally:
        test_env.cleanup_instance()


@pytest.mark.firmware
def test_ota_rejects_oversize_image_reference(
    associated_shadow_tracked_test_env_dormant_instance, ota_manager, mock_ota_job
):
    """
    A streamname longer than the AWS MQTT file-downloader's STREAM_NAME_MAX_LEN
    (parsed from the vendored header) would overflow a fixed-size stack buffer inside
    the vendored library, crashing the device. The transport validator must reject the
    job at parse before any download setup runs.
    """
    test_env = associated_shadow_tracked_test_env_dormant_instance
    thing_name = test_env.instance.factory_config.thing_name

    # Build a streamname that exceeds the AWS MQTT file-downloader's STREAM_NAME_MAX_LEN.
    long_streamname = "AFR_OTA-" + "x" * (_MQTT_STREAM_NAME_MAX_LEN + 1)
    assert len(long_streamname) > _MQTT_STREAM_NAME_MAX_LEN, (
        "fixture invariant: streamname must exceed STREAM_NAME_MAX_LEN"
    )

    # fw_version must exceed the running firmware's version, else the job is rejected
    # for FW_VERSION_TOO_LOW before the image-ref validator runs.
    invalid_job_document = create_mock_job_document(
        protocols=["MQTT"], streamname=long_streamname, fw_version="9.9.9"
    )
    job_id = mock_ota_job(invalid_job_document, thing_name)

    # Wait for job creation
    time.sleep(10)

    # Start the instance after job creation to avoid race conditions
    test_env.setup_instance()

    try:
        job_status = ota_manager.wait_for_execution_status(
            job_id,
            thing_name,
            "REJECTED",
            expected_details_check=_reason_check(
                _OTA_REASONS["ESP_RMAKER_OTA_REJECTED_REASON_IMAGE_REFERENCE_INVALID"]
            ),
            poll_interval=3,
            max_attempts=15,
        )
        assert job_status is not None, (
            f"Expected OTA job to be REJECTED with reason "
            f"'{_OTA_REASONS['ESP_RMAKER_OTA_REJECTED_REASON_IMAGE_REFERENCE_INVALID']}' "
            f"for oversize streamname ({len(long_streamname)} chars)"
        )
    finally:
        test_env.cleanup_instance()


@pytest.mark.firmware
@pytest.mark.parametrize(
    "versions_to_test,min_versions,download_windows,expected_order,final_statuses",
    [
        # Basic upgrade to highest available version
        (
            [OTA_VERSIONS[0], OTA_VERSIONS[1], OTA_VERSIONS[2]],
            [None, None, None],
            [None, None, None],
            [OTA_VERSIONS[2]],
            ["REJECTED", "REJECTED", "SUCCEEDED"],
        ),
        # Min version requirements block some upgrades
        (
            [OTA_VERSIONS[4], OTA_VERSIONS[3], OTA_VERSIONS[2]],
            [OTA_VERSIONS[1], OTA_VERSIONS[0], None],
            [None, None, None],
            [OTA_VERSIONS[2], OTA_VERSIONS[4]],
            ["SUCCEEDED", "REJECTED", "SUCCEEDED"],
        ),
        # Multiple upgrades in sequence
        (
            [OTA_VERSIONS[2], OTA_VERSIONS[3], OTA_VERSIONS[4]],
            [None, OTA_VERSIONS[2], OTA_VERSIONS[3]],
            [None, None, None],
            [OTA_VERSIONS[2], OTA_VERSIONS[3], OTA_VERSIONS[4]],
            ["SUCCEEDED", "SUCCEEDED", "SUCCEEDED"],
        ),
        # Complex case with mixed constraints
        (
            [OTA_VERSIONS[3], OTA_VERSIONS[2], OTA_VERSIONS[1], OTA_VERSIONS[0]],
            [OTA_VERSIONS[0], None, OTA_VERSIONS[1], None],
            [None, None, None, None],
            [OTA_VERSIONS[2], OTA_VERSIONS[3]],
            ["SUCCEEDED", "SUCCEEDED", "REJECTED", "REJECTED"],
        ),
        # Higher version blocked by min requirement
        (
            [OTA_VERSIONS[4], OTA_VERSIONS[2], OTA_VERSIONS[1]],
            [OTA_VERSIONS[3], None, None],
            [None, None, None],
            [OTA_VERSIONS[2]],
            ["QUEUED", "SUCCEEDED", "REJECTED"],
        ),
        # All versions require higher min version
        (
            [OTA_VERSIONS[2], OTA_VERSIONS[1], OTA_VERSIONS[0]],
            [OTA_VERSIONS[1], OTA_VERSIONS[1], OTA_VERSIONS[1]],
            [None, None, None],
            [],
            ["QUEUED", "QUEUED", "QUEUED"],
        ),
        # Upgrade to middle version, highest blocked
        (
            [OTA_VERSIONS[4], OTA_VERSIONS[3], OTA_VERSIONS[2]],
            [OTA_VERSIONS[4], None, None],
            [None, None, None],
            [OTA_VERSIONS[3]],
            ["QUEUED", "SUCCEEDED", "REJECTED"],
        ),
        # Versions in invalid time window get ignored
        (
            [OTA_VERSIONS[2], OTA_VERSIONS[1], OTA_VERSIONS[0]],
            [None, None, None],
            [
                # Invalid validity period
                RmngOtaDownloadWindow(
                    start_time=datetime.now() - timedelta(days=2),
                    end_time=datetime.now() - timedelta(days=1),
                    daily_start=DayTime(0, 0),
                    daily_end=DayTime(23, 59),
                ),
                # Invalid daily window
                RmngOtaDownloadWindow(
                    start_time=datetime.now() - timedelta(hours=2),
                    end_time=datetime.now() + timedelta(hours=1),
                    daily_start=DayTime((datetime.now().hour - 2) % 24, 0),
                    daily_end=DayTime((datetime.now().hour - 1) % 24, 0),
                ),
                # Both invalid
                RmngOtaDownloadWindow(
                    start_time=datetime.now() - timedelta(days=2),
                    end_time=datetime.now() - timedelta(days=1),
                    daily_start=DayTime((datetime.now().hour - 2) % 24, 0),
                    daily_end=DayTime((datetime.now().hour - 1) % 24, 0),
                ),
            ],
            [],
            ["QUEUED", "QUEUED", "QUEUED"],
        ),
        # Patch-only bumps: highest patch wins, lower patches rejected as too low.
        # Regression guard: with the patch field dropped, all of these compared
        # equal to the running 1.0.0 and every job was rejected.
        (
            [PATCH_VERSIONS[0], PATCH_VERSIONS[1], PATCH_VERSIONS[2]],
            [None, None, None],
            [None, None, None],
            [PATCH_VERSIONS[2]],
            ["REJECTED", "REJECTED", "SUCCEEDED"],
        ),
        # Sequential patch upgrades chained via min_fw_version: each patch bump
        # must apply in order (1.0.1 -> 1.0.2 -> 1.0.3).
        (
            [PATCH_VERSIONS[0], PATCH_VERSIONS[1], PATCH_VERSIONS[2]],
            [None, PATCH_VERSIONS[0], PATCH_VERSIONS[1]],
            [None, None, None],
            [PATCH_VERSIONS[0], PATCH_VERSIONS[1], PATCH_VERSIONS[2]],
            ["SUCCEEDED", "SUCCEEDED", "SUCCEEDED"],
        ),
        # Lower version valid time window, higher version invalid - use lower first
        (
            [OTA_VERSIONS[2], OTA_VERSIONS[1]],
            [None, None],
            [
                RmngOtaDownloadWindow(
                    start_time=datetime.now() - timedelta(days=2),
                    end_time=datetime.now() - timedelta(days=1),
                    daily_start=DayTime(0, 0),
                    daily_end=DayTime(23, 59),
                ),
                RmngOtaDownloadWindow(
                    start_time=datetime.now() - timedelta(hours=1),
                    end_time=datetime.now() + timedelta(hours=2),
                    daily_start=DayTime(0, 0),
                    daily_end=DayTime(23, 59),
                ),
            ],
            [OTA_VERSIONS[1]],
            ["QUEUED", "SUCCEEDED"],
        ),
        # Valid time window, single case
        (
            [OTA_VERSIONS[2]],
            [None],
            [
                RmngOtaDownloadWindow(
                    start_time=datetime.now() - timedelta(hours=1),
                    end_time=datetime.now() + timedelta(hours=2),
                    daily_start=DayTime(0, 0),
                    daily_end=DayTime(23, 59),
                )
            ],
            [OTA_VERSIONS[2]],
            ["SUCCEEDED"],
        ),
    ],
    ids=[
        "basic_upgrade",
        "min_version_blocks",
        "sequential_upgrades",
        "mixed_constraints",
        "higher_version_blocked",
        "all_blocked_by_min",
        "middle_version_selected",
        "invalid_time_windows_ignored",
        "patch_bump_ordering",
        "patch_bump_sequential",
        "lower_valid_higher_invalid",
        "valid_time_window_single",
    ],
)
def test_ota_version_ordering(
    ota_version_binary_builder,
    associated_shadow_tracked_test_env_dormant_instance,
    ota_manager,
    ota_job,
    versions_to_test,
    min_versions,
    download_windows,
    expected_order,
    final_statuses,
):
    """
    Test OTA with multiple versions, and ensure they are executed in the correct order.
    """
    test_env = associated_shadow_tracked_test_env_dormant_instance
    thing_name = test_env.instance.factory_config.thing_name

    # Make the OTA jobs in random order
    configs = [
        RmngOtaInfo(
            fw_version=version,
            min_fw_version=min_version,
            download_window=download_window,
        )
        for version, min_version, download_window in zip(
            versions_to_test, min_versions, download_windows
        )
    ]
    shuffle(configs)

    job_ids = {}
    for config in configs:
        binary_path = ota_version_binary_builder(config.fw_version)
        job_id = ota_job(binary_path, thing_name, config)
        job_ids[config.fw_version] = job_id

    # Wait for jobs to be created
    time.sleep(10)

    # Start the instance after job creation to avoid race conditions
    test_env.setup_instance()

    try:
        # Track the jobs
        def track_job(job_id: str, version: str):
            assert test_env.wait_on_state(
                {"OTA Remote": {"Job FW Version": version}}, timeout=30
            ), f"Did not start job for version '{version}' as expected"
            job_status = ota_manager.wait_for_execution_status(
                job_id, thing_name, "SUCCEEDED"
            )
            assert job_status is not None, "OTA job failed or timed out"
            assert test_env.wait_on_state(
                {"OTA Remote": {"FW Version": version}}, timeout=5
            ), f"Firmware version is not '{version}'"

        # Ensure they are executed in the correct order
        if expected_order:
            for version in expected_order:
                assert version in job_ids, f"Job ID for version {version} not found"
                track_job(job_ids[version], version)
        else:
            time.sleep(
                30
            )  # Ensure that the firmware has sufficient time to receive the jobs and make decisions

        # Ensure all other jobs are as expected
        for version, final_status in zip(versions_to_test, final_statuses):
            if version not in expected_order:
                # Try for a max of 30 seconds to see if the job is in the expected final status
                job_status = ota_manager.wait_for_execution_status(
                    job_ids[version],
                    thing_name,
                    final_status,
                    poll_interval=3,
                    max_attempts=10,
                )
                assert job_status is not None, (
                    f"Unexpected status for OTA job for version '{version}' - expected {final_status}"
                )
    finally:
        test_env.cleanup_instance()


_OTA_STATUS_DETAILS_HEADER = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "..",
        "components",
        "esp_rmaker_neo_ota",
        "include",
        "esp_rmaker_ota_status_details.h",
    )
)


def _parse_ota_reasons(header_path: str) -> dict[str, str]:
    """Parse #define ESP_RMAKER_OTA_(FAILED|REJECTED)_REASON_* "literal" from the C header.

    Single source of truth for reason strings — keeps the Python tests aligned with the
    C macros without manual duplication.
    """
    pattern = re.compile(
        r'^\s*#define\s+(ESP_RMAKER_OTA_(?:FAILED|REJECTED)_REASON_\w+)\s+"([^"]*)"',
        re.MULTILINE,
    )
    with open(header_path, "r") as f:
        return dict(pattern.findall(f.read()))


_OTA_REASONS = _parse_ota_reasons(_OTA_STATUS_DETAILS_HEADER)


# Mirror of STREAM_NAME_MAX_LEN from the AWS MQTT file-downloader sourced via esp-aws-iot.
# Not parsed dynamically because that checkout is only populated after a build.
# Keep in sync with the C macro if it ever changes upstream.
_MQTT_STREAM_NAME_MAX_LEN = 44


def _details_reason_equals(expected_reason: str, details: dict) -> bool:
    """Module-level details-check predicate (picklable under xdist)."""
    return details.get("reason") == expected_reason


def _reason_check(expected_reason: str) -> Callable[[dict], bool]:
    """Build a picklable details check asserting the job's FAILED/REJECTED reason matches."""
    return partial(_details_reason_equals, expected_reason)


def _download_completed_check(details: dict) -> bool:
    try:
        downloaded, total = (
            int(details.get("downloaded_bytes", 0)),
            int(details.get("total_bytes", 0)),
        )
        return downloaded > 0 and total > 0 and downloaded == total
    except ValueError:
        return False


# Common helper functions for OTA diagnostics tests
def _set_statuses_and_wait_for_job(
    test_env,
    ota_manager,
    ota_job,
    binary_path,
    thing_name,
    ota_version,
    statuses: dict,
    expected_job_status: str,
    expected_details_check: Optional[Callable[[dict], bool]] = None,
) -> str:
    """Set OTA statuses and wait for job completion."""
    test_env.update_state({"OTA Remote": statuses})

    # Create an OTA job using the fixture
    job_id = ota_job(binary_path, thing_name, RmngOtaInfo(fw_version=ota_version))
    time.sleep(10)

    # Track the job
    job_status = ota_manager.wait_for_execution_status(
        job_id,
        thing_name,
        expected_job_status,
        expected_details_check=expected_details_check,
    )
    assert job_status is not None, "OTA job failed or timed out"

    return job_id


def _get_old_fw_version(test_env) -> str:
    """
    Get the old firmware version from the shadow state.
    """
    old_fw_version = test_env.shadow_state.get("OTA Remote", {}).get("FW Version")
    assert old_fw_version is not None, "Failed to get old firmware version"
    return old_fw_version


@pytest.mark.firmware
@pytest.mark.parametrize(
    "test_name,return_values,expected_job_status,final_state_template",
    [
        (
            "failure_at_initialization",
            {"DRet Init": 0},
            "FAILED",
            {"FW Version": "old_version", "DStage": 1, "DStatus": 0},
        ),
        (
            "failure_at_post_mqtt",
            {"DRet Init": 1, "DRet MQTT": 0},
            "FAILED",
            {"FW Version": "old_version", "DStage": 2, "DStatus": 0},
        ),
        (
            "success_at_post_mqtt",
            {"DRet Init": 1, "DRet MQTT": 2},
            "SUCCEEDED",
            {"FW Version": "ota_version", "DStage": 2, "DStatus": 2},
        ),
        (
            "rollback_timeout_after_pending_post_mqtt",
            {"DRet Init": 1, "DRet MQTT": 1},
            "FAILED",
            {"FW Version": "old_version", "DStage": 2, "DStatus": 1},
        ),
    ],
    ids=[
        "init_failure",
        "post_mqtt_failure",
        "post_mqtt_success",
        "rollback_timeout_pending",
    ],
)
def test_ota_diagnostics_flow(
    ota_default_version_binary,
    associated_shadow_tracked_test_env,
    ota_manager,
    ota_job,
    test_name,
    return_values,
    expected_job_status,
    final_state_template,
):
    """
    Test OTA diagnostics flow with different failure/success scenarios.
    """
    print(f"\n=== Test: {test_name} ===")

    test_env = associated_shadow_tracked_test_env
    thing_name = test_env.instance.factory_config.thing_name
    binary_path, ota_version = ota_default_version_binary

    # Resolve template values
    final_state = final_state_template.copy()
    if final_state.get("FW Version") == "old_version":
        old_fw_version = _get_old_fw_version(test_env)
        final_state["FW Version"] = old_fw_version
    elif final_state.get("FW Version") == "ota_version":
        final_state["FW Version"] = ota_version

    # Execute diagnostics flow
    _set_statuses_and_wait_for_job(
        test_env,
        ota_manager,
        ota_job,
        binary_path,
        thing_name,
        ota_version,
        return_values,
        expected_job_status,
    )
    assert test_env.wait_on_state({"OTA Remote": final_state}, timeout=5), (
        f"Failed final state check for {test_name}"
    )

    print(f"=== Test {test_name} completed successfully ===")


@pytest.mark.firmware
@pytest.mark.parametrize(
    "test_name,mark_as,final_status",
    [
        ("mark_valid_after_pending_post_mqtt", True, "SUCCEEDED"),
        ("mark_invalid_after_pending_post_mqtt", False, "FAILED"),
    ],
    ids=["mark_valid_succeeds", "mark_invalid_fails"],
)
def test_ota_diagnostics_pending_and_mark_flow(
    ota_default_version_binary,
    associated_shadow_tracked_test_env,
    ota_manager,
    ota_job,
    test_name,
    mark_as,
    final_status,
):
    """
    Test OTA diagnostics pending and mark flow with different outcomes.
    """
    print(f"\n=== Test: {test_name} ===")

    test_env = associated_shadow_tracked_test_env
    thing_name = test_env.instance.factory_config.thing_name
    binary_path, ota_version = ota_default_version_binary

    # Execute pending and mark flow
    job_id = _set_statuses_and_wait_for_job(
        test_env,
        ota_manager,
        ota_job,
        binary_path,
        thing_name,
        ota_version,
        {"DRet Init": 1, "DRet MQTT": 1},
        "IN_PROGRESS",
        expected_details_check=_download_completed_check,
    )
    assert test_env.wait_on_state(
        {
            "online": True,
            "OTA Remote": {"FW Version": ota_version, "DStage": 2, "DStatus": 1},
        },
        timeout=120,
    ), "Node did not signal it is online after reboot"
    test_env.update_state({"OTA Remote": {"Mark": mark_as}})
    assert (
        ota_manager.wait_for_execution_status(job_id, thing_name, final_status)
        is not None
    ), f"OTA job failed or timed out for {test_name}"

    print(f"=== Test {test_name} completed successfully ===")


@pytest.mark.firmware
def test_ota_signature_verify_disabled_passes(
    ota_version_binary_builder_no_sig_verify,
    associated_shadow_tracked_test_env_no_sig_verify,
    ota_manager,
    ota_job,
):
    """
    When RMNG_OTA_SIGNATURE_VERIFY_ENABLE=n, the device has no codesign cert and accepts jobs with no signature.
    Ensure an OTA job with skip_signing=True (null signature) still succeeds.
    """
    test_env = associated_shadow_tracked_test_env_no_sig_verify
    thing_name = test_env.instance.factory_config.thing_name
    version = OTA_VERSIONS[0]
    binary_path = ota_version_binary_builder_no_sig_verify(version)

    job_id = ota_job(
        binary_path, thing_name, RmngOtaInfo(fw_version=version), skip_signing=True
    )
    time.sleep(10)

    job_status = ota_manager.wait_for_execution_status(job_id, thing_name, "SUCCEEDED")
    assert job_status is not None, (
        "OTA job failed or timed out when signature verify is disabled"
    )

    assert test_env.wait_on_state({"OTA Remote": {"FW Version": version}}, timeout=5), (
        f"Firmware version is not '{version}' after OTA with signature verify disabled"
    )


@pytest.mark.firmware
def test_ota_signature_verify_enabled_rejects_unsigned_job(
    ota_version_binary_builder,
    associated_shadow_tracked_test_env,
    ota_manager,
    ota_job,
):
    """
    When RMNG_OTA_SIGNATURE_VERIFY_ENABLE=y, the device requires a valid signature in the job document.
    Ensure an OTA job with skip_signing=True is rejected. ota_aws.py mimics AWS default behavior by
    writing the literal "N/A" for unsigned files, so the device rejects with INVALID_BASE64 rather than
    SIGNATURE_MISSING.
    """
    test_env = associated_shadow_tracked_test_env
    thing_name = test_env.instance.factory_config.thing_name
    version = OTA_VERSIONS[0]
    binary_path = ota_version_binary_builder(version)

    job_id = ota_job(
        binary_path, thing_name, RmngOtaInfo(fw_version=version), skip_signing=True
    )
    time.sleep(10)

    job_status = ota_manager.wait_for_execution_status(
        job_id,
        thing_name,
        "REJECTED",
        expected_details_check=_reason_check(
            _OTA_REASONS["ESP_RMAKER_OTA_REJECTED_REASON_SIGNATURE_INVALID_BASE64"]
        ),
        poll_interval=3,
        max_attempts=15,
    )
    assert job_status is not None, (
        f"Expected OTA job to be REJECTED with reason '{_OTA_REASONS['ESP_RMAKER_OTA_REJECTED_REASON_SIGNATURE_INVALID_BASE64']}' "
        f"when signature verify is enabled and job has invalid signature"
    )


@pytest.mark.firmware
def test_ota_rejects_version_mismatch(
    ota_version_binary_builder,
    associated_shadow_tracked_test_env,
    ota_manager,
    ota_job,
):
    """
    Build a binary at version A but declare version B in the job document.
    The device must accept the job (B is a valid upgrade target) and start downloading,
    but then reject at the post-download image-header verification step because
    the embedded version (A) does not match the job-declared version (B).
    """
    test_env = associated_shadow_tracked_test_env
    thing_name = test_env.instance.factory_config.thing_name

    actual_version = OTA_VERSIONS[1]  # binary truly contains this version
    declared_version = OTA_VERSIONS[2]  # job document lies and claims this
    binary_path = ota_version_binary_builder(actual_version)
    old_fw_version = _get_old_fw_version(test_env)

    job_id = ota_job(
        binary_path,
        thing_name,
        RmngOtaInfo(fw_version=declared_version),
    )
    time.sleep(10)

    job_status = ota_manager.wait_for_execution_status(
        job_id,
        thing_name,
        "FAILED",
        expected_details_check=_reason_check(
            _OTA_REASONS["ESP_RMAKER_OTA_FAILED_REASON_IMAGE_HEADER_INVALID"]
        ),
        poll_interval=5,
        max_attempts=24,
    )
    assert job_status is not None, (
        f"Expected OTA job to FAIL with reason '{_OTA_REASONS['ESP_RMAKER_OTA_FAILED_REASON_IMAGE_HEADER_INVALID']}' "
        f"when binary version ({actual_version}) does not match declared version ({declared_version})"
    )
    # Device must not have rebooted into the bogus image.
    assert test_env.wait_on_state(
        {"OTA Remote": {"FW Version": old_fw_version}}, timeout=5
    ), "Firmware version should be unchanged after rejected mismatched binary"


@pytest.mark.firmware
def test_ota_rejects_project_name_mismatch(
    device_sim_version_binary_builder,
    associated_shadow_tracked_test_env,
    ota_manager,
    ota_job,
):
    """
    Push a device-sim binary as the OTA payload to a device running ota-sim.
    The two examples have different PROJECT_NAME values, so the downloaded
    image's embedded project_name will not match the running app's. The OTA
    image-header verification step must reject the job before reboot.

    This is the realistic shape of the attack: an attacker (or a misconfigured
    deployment) tries to flash a binary built for a different product onto the
    device. The cross-project binary is internally well-formed — correct image
    SHA256 and, on ESP-IDF, a valid signature — so the failure exercises the
    project_name comparison rather than any pre-existing integrity check.
    """
    test_env = associated_shadow_tracked_test_env
    thing_name = test_env.instance.factory_config.thing_name

    version = OTA_VERSIONS[1]
    rogue_binary = device_sim_version_binary_builder(version)
    old_fw_version = _get_old_fw_version(test_env)

    job_id = ota_job(
        rogue_binary,
        thing_name,
        RmngOtaInfo(fw_version=version),
    )
    time.sleep(10)

    job_status = ota_manager.wait_for_execution_status(
        job_id,
        thing_name,
        "FAILED",
        expected_details_check=_reason_check(
            _OTA_REASONS["ESP_RMAKER_OTA_FAILED_REASON_IMAGE_HEADER_INVALID"]
        ),
        poll_interval=5,
        max_attempts=24,
    )
    assert job_status is not None, (
        f"Expected OTA job to FAIL with reason '{_OTA_REASONS['ESP_RMAKER_OTA_FAILED_REASON_IMAGE_HEADER_INVALID']}' "
        f"when binary project_name does not match running project_name"
    )
    assert test_env.wait_on_state(
        {"OTA Remote": {"FW Version": old_fw_version}}, timeout=5
    ), "Firmware version should be unchanged after rejected cross-project binary"


# ---------------------------------------------------------------------------
# Helper: poll shadow until "Resume Offset" > 0
# ---------------------------------------------------------------------------


def _wait_resume_offset_positive(test_env, timeout: float = 120.0) -> int:
    """
    Poll test_env.shadow_state until "OTA Remote"."Resume Offset" > 0.
    Returns the observed offset (> 0) or 0 on timeout.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        offset = test_env.shadow_state.get("OTA Remote", {}).get("Resume Offset", 0)
        if isinstance(offset, int) and offset > 0:
            return offset
        time.sleep(3)
    return 0


def _details_downloaded_bytes_at_least(min_bytes: int, details: dict) -> bool:
    """Module-level predicate (picklable via partial) for downloaded_bytes threshold."""
    try:
        return int(details.get("downloaded_bytes", 0)) >= min_bytes
    except (ValueError, TypeError):
        return False


def _downloaded_bytes_at_least(min_bytes: int) -> Callable[[dict], bool]:
    """Return a picklable predicate that passes once downloaded_bytes >= min_bytes."""
    return partial(_details_downloaded_bytes_at_least, min_bytes)


@pytest.mark.firmware
def test_ota_resume_after_interrupt(
    ota_version_binary_builder_all,
    associated_shadow_tracked_test_env_all,
    ota_manager,
    ota_job,
):
    """
    Interrupt a download partway through, restart the instance (stop→start without
    reset_ota_state), and assert that the device genuinely resumed (Resume Offset > 0)
    before the job reaches SUCCEEDED.

    Relies on reboot-persistent storage for the partial image + NVS tracker:
    POSIX preserves run_folder/partitions/ across stop/start; ESP keeps the OTA
    partition + NVS in flash across the reset.  stop() genuinely halts the
    device on both targets (POSIX: SIGTERM the process group; ESP: reset into
    the bootloader), so it does not keep downloading while stopped — which is
    what lets us freeze it during job propagation and interrupt it mid-download.
    """
    test_env = associated_shadow_tracked_test_env_all
    thing_name = test_env.instance.factory_config.thing_name
    version = OTA_VERSIONS[0]
    binary_path = ota_version_binary_builder_all(version)

    # --- Step 1: create the job while the device is frozen ---------------
    # The AWS IoT job execution takes ~10s to propagate before the device can
    # see it.  If we let the device run during that window it can download to
    # completion before we ever observe IN_PROGRESS.  So stop() the device
    # first (halted, not downloading), create the job, wait out propagation,
    # then boot it fresh so the download starts under our observation.
    assert test_env.instance.stop(), "Failed to stop instance before job creation"

    # file_md5 injected by tooling — required to enable resume.
    job_id = ota_job(binary_path, thing_name, RmngOtaInfo(fw_version=version))
    time.sleep(10)

    # --- Step 2: boot fresh and catch the download in flight -------------
    assert test_env.instance.start(), "Failed to start instance after job creation"
    assert test_env.wait_on_state({"online": True}, timeout=120), (
        "Instance did not come online after fresh start"
    )
    # Re-assert the diagnostics return state defensively. It is PROP_FLAG_PERSIST in
    # ota-sim so it does survive the restart; this only matters if an earlier test left it
    # on something other than "pass".
    test_env.update_state({"OTA Remote": {"DRet Init": 2, "DRet MQTT": 2}})

    # downloaded_bytes > 0 means at least one MQTT block / HTTPS chunk boundary
    # has been persisted to the tracker — sufficient for a valid resume.
    partial_detail = ota_manager.wait_for_execution_status(
        job_id,
        thing_name,
        "IN_PROGRESS",
        expected_details_check=_downloaded_bytes_at_least(1),
        poll_interval=0.5,
        max_attempts=240,
    )
    assert partial_detail is not None, (
        "Download did not reach IN_PROGRESS with downloaded_bytes > 0 before timeout"
    )

    # --- Step 3: interrupt -----------------------------------------------
    # stop() keeps the partial image + tracker intact (no reset_ota_state).
    assert test_env.instance.stop(), "Failed to stop instance mid-download"

    # --- Step 4: resume --------------------------------------------------
    assert test_env.instance.start(), "Failed to restart instance"
    assert test_env.wait_on_state({"online": True}, timeout=120), (
        "Instance did not come back online after restart"
    )
    # Re-assert the diagnostics return state defensively (persisted, see step 2)
    test_env.update_state({"OTA Remote": {"DRet Init": 2, "DRet MQTT": 2}})

    # --- Step 5: assert resume -------------------------------------------
    resume_offset = _wait_resume_offset_positive(test_env, timeout=120)
    assert resume_offset > 0, (
        "Resume Offset never became > 0; device may have restarted the download "
        "from scratch (or CONFIG_RMNG_OTA_RESUME is disabled)"
    )

    # --- Step 6: assert end-to-end success -------------------------------
    # Step 2 interrupts as soon as a single byte has been persisted, so the resume offset
    # is a small fraction of the image and nearly the whole download still has to happen
    # inside this window. Take wait_for_execution_status' default budget, same as every
    # other end-to-end SUCCEEDED wait here -- the override this replaced allowed half of
    # it, which is not enough for a full download.
    job_status = ota_manager.wait_for_execution_status(job_id, thing_name, "SUCCEEDED")
    assert job_status is not None, "OTA job did not reach SUCCEEDED after resume"

    assert test_env.wait_on_state(
        {"OTA Remote": {"FW Version": version}}, timeout=30
    ), f"Firmware version not updated to '{version}' after resumed OTA"
