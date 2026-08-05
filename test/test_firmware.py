# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import pytest
from pathlib import Path
import copy
import json
import random
from random import shuffle
from datetime import datetime as dt, timedelta as td
from zoneinfo import ZoneInfo
from time import time as time_sec, sleep
from helpers.node_host_ctrl import (
    operate_with_named_shadow_connection,
    read_named_shadow,
    restart_node_host_ctrl_without_reset,
    start_node_host_ctrl,
    wait_for_node_state_reported,
    wait_for_cloud_params_update,
    wait_for_cloud_params_update_via_group_broadcast,
    wait_for_cloud_params_update_via_group_subgroup,
)
from helpers.config import (
    _get_param_update_payload,
    _get_param_update_payload_typed,
    _randomize_config,
    _set_test_node_config,
    _update_params_local,
    _verify_config_with_indexed_shadow_reported,
    _verify_config_with_named_shadow_reported,
    _verify_config_with_node_state,
    _verify_default_tags_uploaded,
    _verify_uploaded_node_config,
)
from helpers.automation import (
    automation_create_and_test as _automation_create_and_test,
    concurrent_automation_test as _concurrent_automation_test,
    get_automation_node_config as _get_automation_node_config,
    wire_actions as _wire_actions,
    wire_triggers as _wire_triggers,
)
from helpers.scheduling import (
    get_scheduling_node_config as _get_scheduling_node_config,
    scheduling_set_anti_action as _scheduling_set_anti_action_helper,
    scheduling_set_schedules as _scheduling_set_schedules_helper,
    scheduling_verify_schedule as _scheduling_verify_schedule_helper,
)
from helpers.webhook_mock import (
    TEST_INFRA_STACK,
    webhook_mock_infra,
    webhook_mock_validate,
)
from helpers.fixtures import (
    _associate_to_new_group,
    _associated_user1_node_host_ctrl,
    _associated_user1_node_host_ctrl_with_user1_connected,
    _firmware_instance_host_ctrl,
    _firmware_instance_request_host_ctrl_wrapper,
    _node_host_ctrl,
)
from fwlib.instances.posix import FirmwareInstancePosixHostCtrl
from host_ctrl_python.host_ctrl import NodeConfig
from util.protobuf import ChalRespStatus, Status
from rmng_backend import Group
from credentials_store import RM_CONFIG
from util.local_ctrl import (
    LocalController,
    CAPABILITY_LOCAL_CTRL,
    CAPABILITY_CHAL_RESP,
    SEC2_USERNAME as LOCAL_CTRL_SEC2_USERNAME,
)

# Stands in for the manufacturing PoP a real device carries for on-network association;
# pushed to the node over host_ctrl before the local endpoints service starts.
ON_NETWORK_CHAL_RESP_POP = "abcd1234"


# Configs
CONFIGS = [
    # Light
    # (Path(__file__).parent / "data" / "node_config_va_light.json", Path(__file__).parent / "data" / "node_tags.json"),
    # Multi
    # (Path(__file__).parent / "data" / "node_config_va_multi.json", Path(__file__).parent / "data" / "node_tags.json"),
    # Switch
    # (Path(__file__).parent / "data" / "node_config_va_switch.json", Path(__file__).parent / "data" / "node_tags.json"),
    # Varied data types
    (
        Path(__file__).parent / "data" / "node_config_va_varied_dtypes.json",
        Path(__file__).parent / "data" / "node_tags_varied_dtypes.json",
    ),
]
CONFIG_IDS = [
    f"config:'{config_path.stem}'-tags:'{tags_path.stem}'"
    for config_path, tags_path in CONFIGS
]


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl():
    # Dynamically parametrized in conftest.py
    pass


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_local_ctrl():
    # Dynamically parametrized in conftest.py over LOCAL_CTRL_SEC1 + LOCAL_CTRL_SEC2
    pass


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_lc_chal_resp():
    # Dynamically parametrized in conftest.py over LC_CHAL_RESP_SEC1 + LC_CHAL_RESP_SEC2
    pass


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_on_chal_resp():
    # Dynamically parametrized in conftest.py
    pass


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_basic_ingest_variants():
    # Dynamically parametrized in conftest.py over DEFAULT + NO_BASIC_INGEST
    pass


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_wrapper(
    firmware_instance_request_host_ctrl, add_summary_section
):
    yield from _firmware_instance_request_host_ctrl_wrapper(
        firmware_instance_request_host_ctrl, add_summary_section
    )


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_local_ctrl_wrapper(
    firmware_instance_request_host_ctrl_local_ctrl, add_summary_section
):
    yield from _firmware_instance_request_host_ctrl_wrapper(
        firmware_instance_request_host_ctrl_local_ctrl, add_summary_section
    )


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_lc_chal_resp_wrapper(
    firmware_instance_request_host_ctrl_lc_chal_resp, add_summary_section
):
    yield from _firmware_instance_request_host_ctrl_wrapper(
        firmware_instance_request_host_ctrl_lc_chal_resp, add_summary_section
    )


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_on_chal_resp_wrapper(
    firmware_instance_request_host_ctrl_on_chal_resp, add_summary_section
):
    yield from _firmware_instance_request_host_ctrl_wrapper(
        firmware_instance_request_host_ctrl_on_chal_resp, add_summary_section
    )


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_basic_ingest_variants_wrapper(
    firmware_instance_request_host_ctrl_basic_ingest_variants, add_summary_section
):
    yield from _firmware_instance_request_host_ctrl_wrapper(
        firmware_instance_request_host_ctrl_basic_ingest_variants, add_summary_section
    )


@pytest.fixture(scope="function")
def firmware_instance_host_ctrl(
    firmware_instance_request_host_ctrl_wrapper, firmware_instance_manager
):
    yield from _firmware_instance_host_ctrl(
        firmware_instance_request_host_ctrl_wrapper, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def firmware_instance_host_ctrl_local_ctrl(
    firmware_instance_request_host_ctrl_local_ctrl_wrapper, firmware_instance_manager
):
    yield from _firmware_instance_host_ctrl(
        firmware_instance_request_host_ctrl_local_ctrl_wrapper,
        firmware_instance_manager,
    )


@pytest.fixture(scope="function")
def firmware_instance_host_ctrl_lc_chal_resp(
    firmware_instance_request_host_ctrl_lc_chal_resp_wrapper, firmware_instance_manager
):
    yield from _firmware_instance_host_ctrl(
        firmware_instance_request_host_ctrl_lc_chal_resp_wrapper,
        firmware_instance_manager,
    )


@pytest.fixture(scope="function")
def firmware_instance_host_ctrl_on_chal_resp(
    firmware_instance_request_host_ctrl_on_chal_resp_wrapper, firmware_instance_manager
):
    yield from _firmware_instance_host_ctrl(
        firmware_instance_request_host_ctrl_on_chal_resp_wrapper,
        firmware_instance_manager,
    )


@pytest.fixture(scope="function")
def firmware_instance_host_ctrl_basic_ingest_variants(
    firmware_instance_request_host_ctrl_basic_ingest_variants_wrapper,
    firmware_instance_manager,
):
    yield from _firmware_instance_host_ctrl(
        firmware_instance_request_host_ctrl_basic_ingest_variants_wrapper,
        firmware_instance_manager,
    )


@pytest.fixture(scope="function")
def node_host_ctrl(request, firmware_instance_host_ctrl, add_summary_section):
    yield from _node_host_ctrl(
        request, firmware_instance_host_ctrl, add_summary_section
    )


@pytest.fixture(scope="function")
def node_host_ctrl_local_ctrl(
    request, firmware_instance_host_ctrl_local_ctrl, add_summary_section
):
    yield from _node_host_ctrl(
        request, firmware_instance_host_ctrl_local_ctrl, add_summary_section
    )


@pytest.fixture(scope="function")
def node_host_ctrl_lc_chal_resp(
    request, firmware_instance_host_ctrl_lc_chal_resp, add_summary_section
):
    yield from _node_host_ctrl(
        request, firmware_instance_host_ctrl_lc_chal_resp, add_summary_section
    )


@pytest.fixture(scope="function")
def node_host_ctrl_on_chal_resp(
    request, firmware_instance_host_ctrl_on_chal_resp, add_summary_section
):
    yield from _node_host_ctrl(
        request, firmware_instance_host_ctrl_on_chal_resp, add_summary_section
    )


@pytest.fixture(scope="function")
def node_host_ctrl_basic_ingest_variants(
    request, firmware_instance_host_ctrl_basic_ingest_variants, add_summary_section
):
    yield from _node_host_ctrl(
        request, firmware_instance_host_ctrl_basic_ingest_variants, add_summary_section
    )


@pytest.fixture(scope="session")
def webhook_mock_setup():
    """
    Marks tests that need the notifications webhook mock. Actual enable/disable runs
    once per test session on the xdist controller (see test/conftest.py).

    Skips when the mock's stack is not deployed: without it the notifications Lambda
    delivers to the real Amazon/Google endpoints, so every readback would fail on a
    missing payload rather than on anything this suite controls.
    """
    if webhook_mock_infra() is None:
        pytest.skip(
            f"webhook mock not deployed ({TEST_INFRA_STACK} absent); "
            "run `make itest-setup` in the backend repo"
        )
    yield


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl(node_host_ctrl, user_pool):
    """
    Fixture to create a node host_ctrl session associated with test_user1.
    """
    yield from _associated_user1_node_host_ctrl(node_host_ctrl, user_pool)


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_with_user1_connected(
    associated_user1_node_host_ctrl,
):
    """
    Fixture to create a node host_ctrl session associated with test_user1.
    """
    yield from _associated_user1_node_host_ctrl_with_user1_connected(
        associated_user1_node_host_ctrl
    )


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_local_ctrl(node_host_ctrl_local_ctrl, user_pool):
    """
    Fixture to create a node host_ctrl session associated with test_user1, parametrized over
    the SEC1 / SEC2 local control build variants.
    """
    yield from _associated_user1_node_host_ctrl(node_host_ctrl_local_ctrl, user_pool)


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_local_ctrl_with_user1_connected(
    associated_user1_node_host_ctrl_local_ctrl,
):
    """
    Fixture to create a node host_ctrl session associated with test_user1 (SEC1 / SEC2 local
    control variants), with user1 MQTT connected.
    """
    yield from _associated_user1_node_host_ctrl_with_user1_connected(
        associated_user1_node_host_ctrl_local_ctrl
    )


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_lc_chal_resp(
    node_host_ctrl_lc_chal_resp, user_pool
):
    """
    Fixture to create a node host_ctrl session associated with test_user1 with local control challenge-response service.
    """
    yield from _associated_user1_node_host_ctrl(node_host_ctrl_lc_chal_resp, user_pool)


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_lc_chal_resp_with_user1_connected(
    associated_user1_node_host_ctrl_lc_chal_resp,
):
    """
    Fixture to create a node host_ctrl session associated with test_user1.
    """
    yield from _associated_user1_node_host_ctrl_with_user1_connected(
        associated_user1_node_host_ctrl_lc_chal_resp
    )


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_basic_ingest_variants(
    node_host_ctrl_basic_ingest_variants, user_pool
):
    """
    Fixture parametrized over DEFAULT + NO_BASIC_INGEST host_ctrl build variants.
    """
    yield from _associated_user1_node_host_ctrl(
        node_host_ctrl_basic_ingest_variants, user_pool
    )


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected(
    associated_user1_node_host_ctrl_basic_ingest_variants,
):
    """
    Fixture parametrized over DEFAULT + NO_BASIC_INGEST host_ctrl build variants, with user1 MQTT connected.
    """
    yield from _associated_user1_node_host_ctrl_with_user1_connected(
        associated_user1_node_host_ctrl_basic_ingest_variants
    )


@pytest.fixture(scope="function", params=CONFIGS, ids=CONFIG_IDS)
def configured_node_host_ctrl(request, node_host_ctrl):
    """
    Fixture to configure the node host_ctrl.
    """

    config_json_path, tags_json_path = request.param
    return node_host_ctrl, _set_test_node_config(
        node_host_ctrl, config_json_path, tags_json_path
    )


@pytest.fixture(scope="function", params=CONFIGS, ids=CONFIG_IDS)
def configured_associated_user1_node_host_ctrl(
    request, associated_user1_node_host_ctrl
):
    """
    Fixture to configure the node host_ctrl.
    """

    config_json_path, tags_json_path = request.param
    node_host_ctrl, user, group_id = associated_user1_node_host_ctrl
    return (
        node_host_ctrl,
        user,
        group_id,
        _set_test_node_config(node_host_ctrl, config_json_path, tags_json_path),
    )


@pytest.fixture(scope="function", params=CONFIGS, ids=CONFIG_IDS)
def configured_associated_user1_node_host_ctrl_with_user1_connected(
    request, associated_user1_node_host_ctrl_with_user1_connected
):
    """
    Fixture to configure the node host_ctrl.
    """

    config_json_path, tags_json_path = request.param
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    return (
        node_host_ctrl,
        user,
        group_id,
        _set_test_node_config(node_host_ctrl, config_json_path, tags_json_path),
    )


@pytest.fixture(scope="function", params=CONFIGS, ids=CONFIG_IDS)
def configured_associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected(
    request, associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
):
    """
    Fixture to configure the node host_ctrl, parametrized over DEFAULT + NO_BASIC_INGEST build variants.
    """

    config_json_path, tags_json_path = request.param
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    return (
        node_host_ctrl,
        user,
        group_id,
        _set_test_node_config(node_host_ctrl, config_json_path, tags_json_path),
    )


def _do_with_network_failure(node_host_ctrl, func):
    """
    Do a function with network forced to fail.
    """
    assert node_host_ctrl.mqtt_control_force_network_failure(), (
        "Failed to force all network operations to fail"
    )
    try:
        func()
    except Exception as e:
        if not node_host_ctrl.mqtt_control_restore_network_default():
            print(
                "Warning: Failed to restore default network operations settings after error handling"
            )
        raise e

    assert node_host_ctrl.mqtt_control_restore_network_default(), (
        "Failed to restore default network operations settings after error handling"
    )


@pytest.mark.firmware
def test_firmware_unit_tests(firmware_instance_host_ctrl):
    """
    Run the firmware unit tests.
    Currently only supported for POSIX firmware instances.
    """

    instance, _ = firmware_instance_host_ctrl
    if not isinstance(instance, FirmwareInstancePosixHostCtrl):
        # Assume passed
        return

    try:
        instance.run_unit_tests()
    except Exception as e:
        pytest.fail(f"Unit tests failed: {e}")


@pytest.mark.firmware
def test_firmware_start_sequence(
    configured_associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected,
):
    """
    Test the firmware start sequence.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Node start sequence
    start_node_host_ctrl(node_host_ctrl)

    # Get group info
    assert group_id is not None, "Node did not receive group id"

    # Operate with the named shadow connection
    def op_func():
        # Read named shadow
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)

        # Check online status
        assert named_shadow_reported.get("online"), "Node should be online"

        # Check parameters
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    # Read indexed shadow
    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"

    # Check online status
    assert indexed_shadow_reported.get("online"), "Node should be online"

    # Verify the config with the indexed shadow reported
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_default_tags_uploaded(configured_associated_user1_node_host_ctrl):
    """
    Verify that the default tags (name, type, fw_version, model) added by the firmware
    on node create are uploaded and present in the indexed shadow.
    """
    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl
    )
    start_node_host_ctrl(node_host_ctrl)
    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_default_tags_uploaded(node_host_ctrl, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_start_sequence_error_handling(
    configured_associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected,
):
    """
    Test the firmware start sequence error handling with network failure.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    def op_func():
        # Clear any stale flags
        node_host_ctrl.clear_on_online()
        node_host_ctrl.clear_on_all_cloud_get_events()
        node_host_ctrl.clear_on_state_reported()
        node_host_ctrl.clear_on_node_config_sent()

        # Start the node
        assert not node_host_ctrl.start(timeout_ms=5000), (
            "Node should not finish starting despite network failure"
        )
        assert not node_host_ctrl.wait_on_online(5000), (
            "Node signalled it is online despite network failure"
        )
        assert not node_host_ctrl.wait_on_all_cloud_get_events(5000), (
            "Node received cloud get events despite network failure"
        )
        assert not node_host_ctrl.wait_on_state_reported(5000), (
            "Node reported state despite network failure"
        )
        assert not node_host_ctrl.wait_on_node_config_sent(5000), (
            "Node sent node config despite network failure"
        )

        # Force mqtt operations to fail
        assert node_host_ctrl.mqtt_control_force_operations_failure(), (
            "Failed to force all MQTT operations to fail"
        )

    _do_with_network_failure(node_host_ctrl, op_func)

    assert not node_host_ctrl.wait_on_online(10000), (
        "Node should not signal it is online despite operations failure"
    )
    assert not node_host_ctrl.wait_on_all_cloud_get_events(10000), (
        "Node should not receive cloud get events despite operations failure"
    )
    assert not node_host_ctrl.wait_on_state_reported(5000), (
        "Node should not report state despite operations failure"
    )
    assert not node_host_ctrl.wait_on_node_config_sent(5000), (
        "Node should not send node config despite operations failure"
    )

    # Restore mqtt operations
    assert node_host_ctrl.mqtt_control_restore_operations_default(), (
        "Failed to restore default MQTT operations settings"
    )

    # Start the node
    RECOVERY_TIMEOUT_MS = 120000  # Generously long timeout for recovery since the node uses exponential backoff for retries.
    assert node_host_ctrl.wait_on_online(RECOVERY_TIMEOUT_MS), (
        "Node should signal it is online after MQTT recovery"
    )
    assert node_host_ctrl.wait_on_all_cloud_get_events(RECOVERY_TIMEOUT_MS), (
        "Node should receive cloud get events after MQTT recovery"
    )
    assert node_host_ctrl.wait_on_state_reported(RECOVERY_TIMEOUT_MS), (
        "Node should report state after MQTT recovery"
    )
    assert node_host_ctrl.wait_on_node_config_sent(RECOVERY_TIMEOUT_MS), (
        "Node should send node config after MQTT recovery"
    )

    # Operate with the named shadow connection
    def op_func():
        def wait_for_proper_state_report(timeout_ms=60000):
            start_time = time_sec()
            while (time_sec() - start_time) * 1000 < timeout_ms:
                named_shadow = read_named_shadow(user, thing_name, group_id)
                if (
                    named_shadow
                    and named_shadow.get("online")
                    and "ncfg_ver" in named_shadow
                ):
                    try:
                        _verify_config_with_named_shadow_reported(
                            node_config, named_shadow
                        )
                        return True
                    except Exception:
                        pass
                sleep(1.0)
            return False

        # Read named shadow
        named_shadow_reported = wait_for_proper_state_report(timeout_ms=10000)
        assert named_shadow_reported, (
            "Node should be online and have proper state report"
        )

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    # Read indexed shadow
    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"

    # Check online status
    assert indexed_shadow_reported.get("online"), "Node should be online"

    # Verify the config with the indexed shadow reported
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_node_config(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test the firmware node config.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Get the node config from cloud
    uploaded_config = user.get_node_config(
        group_id=group_id, subgroup_id=None, node_id=thing_name
    )
    assert uploaded_config is not None, " Node config not found in cloud"
    _verify_uploaded_node_config(uploaded_config, node_config)

    # Clear any stale flags
    node_host_ctrl.clear_on_node_config_sent()

    # Restart the node without resetting
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    # Ensure node did not re-send its configuration
    assert not node_host_ctrl.wait_on_node_config_sent(5000), (
        "Node re-sent its configuration"
    )


@pytest.mark.firmware
def test_firmware_group_info_empty(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test the firmware with an empty group info (i.e., unassociated node).
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )

    # Get the old group info
    old_group_info = group_id

    # Dissociate the node by:
    # 1. Making a new empty group
    # 2. Associating the node to the new group
    # 3. Deleting the new empty group
    user1_group_api = Group(user=user)
    new_group_id = _associate_to_new_group(
        "Test Empty Group", node_host_ctrl, user, user1_group_api
    )
    assert new_group_id is not None, "Failed to create new empty group"
    user1_group_api.delete_group(group_id=new_group_id)

    def on_finish():
        node_host_ctrl.clear_on_group_info()
        node_host_ctrl.clear_on_state_reported()
        user.do_user_node_assoc(device=node_host_ctrl, group_id=old_group_info)
        assert node_host_ctrl.wait_on_group_info(5000), (
            "Node did not receive group info after re-association"
        )
        sleep(1)  # wait for forced reconnect event
        assert node_host_ctrl.wait_on_online(60000), (
            "Node did not go online after re-association"
        )
        assert node_host_ctrl.wait_on_state_reported(5000), (
            "Node did not report state after re-association"
        )
        node_host_ctrl.clear_on_group_info()  # clear any stale flags

    try:
        # Start the node
        start_node_host_ctrl(node_host_ctrl)

        # Check the group info
        group_info = node_host_ctrl.get_group_info_str()
        assert group_info is None, "Node has group info"

        def op_func(op_group_id):
            named_shadow_reported = read_named_shadow(
                user, node_host_ctrl.node_thing_name, op_group_id
            )
            print(
                f"Named shadow reported:\n{json.dumps(named_shadow_reported, indent=2)}"
            )
            _verify_config_with_named_shadow_reported(
                node_config, named_shadow_reported
            )

        # Check the groupless params
        thing_name = node_host_ctrl.node_thing_name
        assert thing_name is not None, "Node did not receive thing name"

        # Read through the node host_ctrl because users do not have access to the named shadow without group information, i.e., "params-".
        named_shadow_reported = node_host_ctrl.get_named_shadow(10000)
        assert named_shadow_reported is not None, "Node did not receive named shadow"

        # Move "params" to the top level and align with unstructured format
        named_shadow_params = named_shadow_reported.pop("params")
        named_shadow_reported.update(named_shadow_params)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

        # Associate the node back to a new group
        node_host_ctrl.clear_on_group_info()
        node_host_ctrl.clear_on_state_reported()
        new_group_id = _associate_to_new_group(
            "Test Empty Group", node_host_ctrl, user, user1_group_api
        )
        assert new_group_id is not None, "Failed to create new empty group"
        assert node_host_ctrl.wait_on_group_info(5000), (
            "Node did not receive group info after migration"
        )
        sleep(1)  # wait for forced reconnect event
        assert node_host_ctrl.wait_on_online(60000), (
            "Node did not go online after migration"
        )
        assert node_host_ctrl.wait_on_state_reported(5000), (
            "Node did not report state after migration"
        )
        node_host_ctrl.clear_on_group_info()  # clear any stale flags
        sleep(1)  # wait for parameter updates to reach the cloud

        # Check the params in the new group using the user
        operate_with_named_shadow_connection(
            lambda: op_func(new_group_id), user, thing_name, new_group_id
        )
    except Exception as e:
        on_finish()
        raise e

    on_finish()


@pytest.mark.firmware
def test_firmware_latency_update_params(
    request, associated_user1_node_host_ctrl_with_user1_connected, add_summary_section
):
    """
    Test the firmware latency update of parameters.
    """

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Set the node config
    node_config = {
        "devices": [],
        "services": ["latency"],
        "tags": {},
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    def node_time_ms() -> int:
        t = node_host_ctrl.get_current_time_ms()
        assert t is not None, "Failed to get current time (ms) from node"
        return t

    # Roundtrip test (host wall clock is not used; device time matches latency recv_ts)
    def roundtrip_test(
        in_val: int,
        user_to_device_latency_ms: list[int],
        device_to_user_latency_ms: list[int],
    ):
        update_payload = {
            "latency": {
                "in_val": in_val,
            }
        }

        start_time_ms = node_time_ms()

        # Update the param
        wait_for_cloud_params_update(
            node_host_ctrl, user, thing_name, group_id, update_payload
        )

        # Verify the param from cloud
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)
        cloud_latency_service = named_shadow_reported.get("latency", {})
        assert cloud_latency_service.get("in_val") == in_val, (
            "Param in_val does not match"
        )
        recv_ts = cloud_latency_service.get("recv_ts")
        recv_ts_rem_ms = cloud_latency_service.get("recv_ts_rem_ms")
        assert recv_ts is not None, "Param recv_ts has no value"
        assert recv_ts_rem_ms is not None, "Param recv_ts_rem_ms has no value"
        recv_ts_ms = int(recv_ts) * 1000 + int(recv_ts_rem_ms)
        end_time_ms = node_time_ms()
        user_to_device = recv_ts_ms - start_time_ms
        device_to_user = end_time_ms - recv_ts_ms
        assert user_to_device >= 0, (
            f"User to device latency is negative, user_to_device: {user_to_device}, start_time_ms: {start_time_ms}, recv_ts_ms: {recv_ts_ms}"
        )
        assert device_to_user >= 0, (
            f"Device to user latency is negative, device_to_user: {device_to_user}, recv_ts_ms: {recv_ts_ms}, end_time_ms: {end_time_ms}"
        )
        user_to_device_latency_ms.append(user_to_device)
        device_to_user_latency_ms.append(device_to_user)

    def get_latency_stats_lines(
        latency_name: str, latency_ms: list[int], disclaimer: str = ""
    ) -> list[str]:
        stats = [
            f"Statistics for {latency_name} ({len(latency_ms)} samples):",
            f"-> min: {min(latency_ms):.2f} ms",
            f"-> max: {max(latency_ms):.2f} ms",
            f"-> avg: {sum(latency_ms) / len(latency_ms):.2f} ms",
        ]
        if disclaimer:
            stats.append(f"! {disclaimer}")
        return stats

    def op_func():
        # do 20 roundtrip tests
        user_to_device_latency_ms = []
        device_to_user_latency_ms = []
        for i in range(20):
            roundtrip_test(i, user_to_device_latency_ms, device_to_user_latency_ms)
        assert len(user_to_device_latency_ms) == 20, (
            "Number of user to device latencies does not match"
        )
        assert len(device_to_user_latency_ms) == 20, (
            "Number of device to user latencies does not match"
        )

        # Calculate statistics
        user_to_device_stats_lines = get_latency_stats_lines(
            "User to device", user_to_device_latency_ms
        )
        device_to_user_stats_lines = get_latency_stats_lines(
            "Device to user",
            device_to_user_latency_ms,
            "+0.5s for state report timer; +2s for state propagation to the cloud",
        )
        stats_lines = (
            user_to_device_stats_lines
            + ["--------------------------------"]
            + device_to_user_stats_lines
        )

        # Print in test
        for line in stats_lines:
            print(line)

        # Print in summary
        add_summary_section(
            f"Latency Statistics for '{request.node.nodeid.split('::')[-1]}'",
            stats_lines,
        )

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)


@pytest.mark.firmware
def test_firmware_update_params_local(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test the firmware local updating of parameters.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Get group info
    assert group_id is not None, "Node did not receive group id"

    def update_params():
        _update_params_local(node_host_ctrl, node_config)

    # Update the params with different values
    node_host_ctrl.clear_on_state_reported()
    _randomize_config(node_config)
    update_params()

    # Make sure the changes are reported
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state with changes"
    )

    # Verify the config with the named shadow reported
    def op_func():
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    # Verify the config with the indexed shadow reported
    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_update_params_cloud(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test the firmware cloud updating of parameters.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Get new config values
    _randomize_config(node_config)

    # Get cloud update payload
    update_payload = _get_param_update_payload(node_config)

    # Update the params
    wait_for_cloud_params_update(
        node_host_ctrl, user, thing_name, group_id, update_payload
    )

    # Verify the config with the node state
    _verify_config_with_node_state(node_host_ctrl, node_config)

    # Verify the config with the named shadow reported
    def op_func():
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    # Verify the config with the indexed shadow reported
    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
@pytest.mark.parametrize("source", ["local", "cloud"])
def test_firmware_same_value_update_reports(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
    source,
):
    """
    A param update carrying the SAME values as the current state must still report state,
    whether it arrives locally or from the cloud. The equal-value path only skips the
    redundant NVS write-back; it no longer short-circuits the state report (or automation
    evaluation).
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Get group info
    assert group_id is not None, "Node did not receive group id"

    def apply_params():
        # Clear first, apply, then let the caller wait for the report - keeps the
        # local and cloud paths symmetric (no path consumes the flag internally).
        node_host_ctrl.clear_on_state_reported()
        if source == "local":
            _update_params_local(node_host_ctrl, node_config)
        else:
            update_payload = _get_param_update_payload(node_config)
            assert user.mqtt_publish_to_topic(
                thing_name=thing_name,
                topic_name=f"params-{group_id}/params",
                data=update_payload,
            ), "Failed to publish cloud params update"

    # First update with fresh values establishes a known state (and reports it).
    _randomize_config(node_config)
    apply_params()
    assert node_host_ctrl.wait_on_state_reported(5000), (
        f"Node did not report state on the initial {source} update"
    )

    # Re-apply the IDENTICAL values. With the fix this still triggers a state report.
    apply_params()
    assert node_host_ctrl.wait_on_state_reported(5000), (
        f"Node did not report state on a same-value {source} update"
    )

    # The state must still match the (unchanged) values.
    _verify_config_with_node_state(node_host_ctrl, node_config)


@pytest.mark.firmware
def test_firmware_update_params_group_broadcast(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Update params via group broadcast topic (node in group, no subgroups).
    Verifies node state, named shadow, and indexed shadow.
    """
    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"
    assert group_id is not None, "Node did not receive group id"

    start_node_host_ctrl(node_host_ctrl)
    _randomize_config(node_config)
    update_payload = _get_param_update_payload_typed(node_config)

    wait_for_cloud_params_update_via_group_broadcast(
        node_host_ctrl, user, group_id, update_payload
    )

    _verify_config_with_node_state(node_host_ctrl, node_config)

    def op_func():
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_update_params_group_subgroup(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Add node to one subgroup; update params via subgroup topic; verify.
    """
    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"
    assert group_id is not None, "Node did not receive group id"

    start_node_host_ctrl(node_host_ctrl)
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_group_info()
    node_host_ctrl.clear_on_state_started_listening()
    group_api = Group(user=user)
    subgroup_id = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Test Subgroup Group Params"
    )
    assert subgroup_id is not None, "Failed to create subgroup"
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_id, node_id=thing_name
    )
    assert node_host_ctrl.wait_on_group_info(5000), (
        "Node did not receive group info after subgroup add"
    )
    assert node_host_ctrl.wait_on_state_started_listening(5000), (
        "Node did not start listening for state changes"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after group info change"
    )

    _randomize_config(node_config)
    update_payload = _get_param_update_payload_typed(node_config)
    wait_for_cloud_params_update_via_group_subgroup(
        node_host_ctrl, user, group_id, subgroup_id, update_payload
    )

    _verify_config_with_node_state(node_host_ctrl, node_config)
    group_info_str = f"{group_id}-{subgroup_id}"

    def op_func():
        named_shadow_reported = read_named_shadow(user, thing_name, group_info_str)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_info_str)

    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_update_params_group_broadcast_with_subgroup(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Add node to one subgroup; update params via group broadcast topic (all devices in group); verify.
    """
    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"
    assert group_id is not None, "Node did not receive group id"

    start_node_host_ctrl(node_host_ctrl)
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_group_info()
    node_host_ctrl.clear_on_state_started_listening()
    group_api = Group(user=user)
    subgroup_id = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Test Subgroup Broadcast"
    )
    assert subgroup_id is not None, "Failed to create subgroup"
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_id, node_id=thing_name
    )
    assert node_host_ctrl.wait_on_group_info(5000), (
        "Node did not receive group info after subgroup add"
    )
    assert node_host_ctrl.wait_on_state_started_listening(5000), (
        "Node did not start listening for state changes"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after group info change"
    )

    _randomize_config(node_config)
    update_payload = _get_param_update_payload_typed(node_config)
    wait_for_cloud_params_update_via_group_broadcast(
        node_host_ctrl, user, group_id, update_payload
    )

    _verify_config_with_node_state(node_host_ctrl, node_config)
    group_info_str = f"{group_id}-{subgroup_id}"

    def op_func():
        named_shadow_reported = read_named_shadow(user, thing_name, group_info_str)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_info_str)

    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_update_params_group_after_subgroup_removed(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Add node to subgroup then remove; update params via group broadcast; verify.
    """
    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"
    assert group_id is not None, "Node did not receive group id"

    start_node_host_ctrl(node_host_ctrl)
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_group_info()
    node_host_ctrl.clear_on_state_started_listening()
    group_api = Group(user=user)
    subgroup_id = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Test Subgroup To Remove"
    )
    assert subgroup_id is not None, "Failed to create subgroup"
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_id, node_id=thing_name
    )
    assert node_host_ctrl.wait_on_group_info(5000), (
        "Node did not receive group info after subgroup add"
    )
    assert node_host_ctrl.wait_on_state_started_listening(5000), (
        "Node did not start listening for state changes after subgroup add"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after subgroup add"
    )

    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_group_info()
    node_host_ctrl.clear_on_state_started_listening()
    group_api.remove_node_from_subgroup(
        group_id=group_id, subgroup_id=subgroup_id, node_id=thing_name
    )
    assert node_host_ctrl.wait_on_group_info(5000), (
        "Node did not receive group info after subgroup remove"
    )
    assert node_host_ctrl.wait_on_state_started_listening(5000), (
        "Node did not start listening for state changes after subgroup remove"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after subgroup remove"
    )

    _randomize_config(node_config)
    update_payload = _get_param_update_payload_typed(node_config)
    wait_for_cloud_params_update_via_group_broadcast(
        node_host_ctrl, user, group_id, update_payload
    )

    _verify_config_with_node_state(node_host_ctrl, node_config)

    def op_func():
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
    assert indexed_shadow_reported is not None, "Node did not receive indexed shadow"
    _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported)


@pytest.mark.firmware
def test_firmware_update_params_group_broadcast_same_type_devices(
    associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Multiple devices share the same device type and parameter type but distinct names.
    A single group control payload keyed by device type must update *every* matching
    device on the node.
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"
    assert group_id is not None, "Node did not receive group id"

    device_type = "esp.device.light"
    param_type = "esp.param.power"
    device_ids = ["light_a", "light_b", "light_c"]

    node_config = {
        "devices": [
            {
                "id": did,
                "type": device_type,
                "params": [
                    {
                        "id": "Power",
                        "type": param_type,
                        "data_type": "bool",
                        "value": False,
                        "properties": ["read", "write"],
                    },
                ],
            }
            for did in device_ids
        ],
        "services": [],
        "tags": {},
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    start_node_host_ctrl(node_host_ctrl)

    # Sanity: every device starts at False
    for name in device_ids:
        param = node_host_ctrl.get_param(name, "Power")
        assert param is not None and param.value is False, (
            f"{name}.Power did not start as False"
        )

    update_payload = {device_type: {"params": {param_type: True}}}
    wait_for_cloud_params_update_via_group_broadcast(
        node_host_ctrl, user, group_id, update_payload
    )

    # Every same-type device instance must have received the update
    for name in device_ids:
        param = node_host_ctrl.get_param(name, "Power")
        assert param is not None, f"{name}.Power not found after update"
        assert param.value is True, (
            f"{name}.Power was not updated by group control payload (got {param.value})"
        )


# @pytest.mark.skip(reason="Skipping MQTT control test until branch is merged")
@pytest.mark.firmware
def test_firmware_update_params_error_handling(
    configured_associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test the firmware error handling of parameter updates.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Force all MQTT operations to fail
    assert node_host_ctrl.mqtt_control_force_operations_failure(), (
        "Failed to force all MQTT operations to fail"
    )
    default_mqtt_settings_restored = False

    try:
        # Update the params
        node_host_ctrl.clear_on_state_reported()
        _randomize_config(node_config)
        _update_params_local(node_host_ctrl, node_config)

        # Verify the config with the node state
        _verify_config_with_node_state(node_host_ctrl, node_config)

        # Make sure nothing is reported
        assert not node_host_ctrl.wait_on_state_reported(5000), (
            "Node reported state with MQTT operations forced to fail"
        )

        # Restore default MQTT settings
        assert node_host_ctrl.mqtt_control_restore_operations_default(), (
            "Failed to restore default MQTT operations settings"
        )
        default_mqtt_settings_restored = True

        # Make sure a report is re-attempted and successful
        assert node_host_ctrl.wait_on_state_reported(60000), (
            "Node did not report state with changes after restoring default MQTT settings"
        )
        sleep(1)  # wait for parameter updates to reach the cloud

        # Verify the config with the named shadow reported
        def op_func():
            named_shadow_reported = read_named_shadow(user, thing_name, group_id)
            _verify_config_with_named_shadow_reported(
                node_config, named_shadow_reported
            )

        operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

        # Verify the config with the indexed shadow reported
        indexed_shadow_reported = node_host_ctrl.get_indexed_shadow(10000)
        assert indexed_shadow_reported is not None, (
            "Node did not receive indexed shadow"
        )
        _verify_config_with_indexed_shadow_reported(
            node_config, indexed_shadow_reported
        )
    except Exception as e:
        if (
            not default_mqtt_settings_restored
            and not node_host_ctrl.mqtt_control_restore_operations_default()
        ):
            print(
                "Warning: Failed to restore default MQTT operations settings after error handling"
            )
        raise e


@pytest.mark.firmware
def test_firmware_group_migration(configured_associated_user1_node_host_ctrl):
    """
    Test the firmware group migration.
    """

    node_host_ctrl, user, group_id, node_config = (
        configured_associated_user1_node_host_ctrl
    )
    group_api = Group(user=user)
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    def get_group_info_str(group_id, subgroup_ids=[]):
        if not subgroup_ids:
            return group_id
        return f"{group_id}-{'-'.join(sorted(subgroup_ids))}"

    def pre_migration():
        node_host_ctrl.clear_on_group_info()
        node_host_ctrl.clear_on_state_started_listening()
        node_host_ctrl.clear_on_state_reported()

    def check_migration(group_info_str, is_primary_change=False):
        # Wait for group info, state started listening, and state reported
        assert node_host_ctrl.wait_on_group_info(5000), (
            "Node did not receive group info"
        )
        if is_primary_change:
            assert node_host_ctrl.wait_on_online(60000), "Node did not go online"
        # For a primary change, the initial force reconnect may not get the new policy in time, so this waits for the reconnect mechanism to re-fetch the policy.
        assert node_host_ctrl.wait_on_state_started_listening(
            60000 if is_primary_change else 5000
        ), "Node did not start listening for state changes on new group info"
        assert node_host_ctrl.wait_on_state_reported(5000), (
            "Node did not report state with changes"
        )
        node_host_ctrl.clear_on_group_info()  # clear any stale flags

        # Verify the group info
        current_group_info_str = node_host_ctrl.get_group_info_str()
        assert current_group_info_str == group_info_str, (
            f"Expected group ID {group_info_str}, but got {current_group_info_str}"
        )

        # Publish update to thing
        _randomize_config(node_config)
        cloud_update_payload = _get_param_update_payload(node_config)
        wait_for_cloud_params_update(
            node_host_ctrl, user, thing_name, group_info_str, cloud_update_payload
        )

        # verify config with local node state
        _verify_config_with_node_state(node_host_ctrl, node_config)

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Get old group info (restore after test)
    old_group_info_str = group_id

    # Associate to a new group
    pre_migration()
    group_id = _associate_to_new_group("Test Group 1", node_host_ctrl, user, group_api)
    assert group_id is not None, "Node did not receive group id"

    def restore_old_group_info():
        if not old_group_info_str:
            return
        node_host_ctrl.clear_on_group_info()
        current_group_info_str = node_host_ctrl.get_group_info_str()
        if current_group_info_str == old_group_info_str:
            return
        user.do_user_node_assoc(device=node_host_ctrl, group_id=old_group_info_str)
        check_migration(old_group_info_str, is_primary_change=True)

    try:
        user.mqtt_connect()
        # Check migration
        check_migration(group_id, is_primary_change=True)

        # Add to subgroup and verify group info
        subgroup_ids = []
        for i in range(3):
            subgroup_id = group_api.create_subgroup(
                group_id=group_id, subgroup_name=f"Test Subgroup {i + 1}"
            )
            pre_migration()
            group_api.add_node_to_subgroup(
                group_id=group_id, subgroup_id=subgroup_id, node_id=thing_name
            )
            subgroup_ids.append(subgroup_id)
            check_migration(get_group_info_str(group_id, subgroup_ids))

        shuffle(subgroup_ids)
        # Remove from subgroup and verify group info
        for i in range(2):
            pre_migration()
            group_api.remove_node_from_subgroup(
                group_id=group_id, subgroup_id=subgroup_ids[i], node_id=thing_name
            )
            check_migration(get_group_info_str(group_id, subgroup_ids[i + 1 :]))

        # change to new group and verify group info.
        pre_migration()
        group_id = _associate_to_new_group(
            "Test Group 2", node_host_ctrl, user, group_api
        )
        assert group_id is not None, "Node did not receive group id"

        check_migration(group_id, is_primary_change=True)
    finally:
        try:
            restore_old_group_info()
        except Exception as e:
            print(f"Warning: Failed to restore old group info on cleanup: {e}")
        user.mqtt_disconnect_and_wait()


@pytest.mark.firmware
def test_firmware_time_control(associated_user1_node_host_ctrl_with_user1_connected):
    """
    Test the firmware time control.
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # save old timezone
    old_timezone = node_host_ctrl.get_current_timezone()
    assert old_timezone is not None, "Failed to get device timezone"

    device_time = node_host_ctrl.get_current_time()
    assert device_time is not None, "Failed to get device time"

    device_time += td(seconds=10)
    assert node_host_ctrl.time_control_set_time(device_time), (
        "Failed to set device time"
    )
    assert node_host_ctrl.get_current_time() == device_time, (
        "Device time does not match set time"
    )

    assert node_host_ctrl.time_control_advance_time(td(seconds=10)), (
        "Failed to advance device time"
    )
    assert node_host_ctrl.get_current_time() == device_time + td(seconds=10), (
        "Device time does not match advanced time"
    )

    # Timezone service testing
    node_config = {
        "devices": [],
        "services": ["timezone"],
        "tags": {},
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )
    start_node_host_ctrl(node_host_ctrl)

    ny_tz = "America/New_York"
    ny_tz_posix = "EST5EDT,M3.2.0,M11.1.0"
    kigali_tz_posix = "CAT-2"

    try:
        # Try with timezone only
        wait_for_cloud_params_update(
            node_host_ctrl,
            user,
            thing_name,
            group_id,
            {
                "Time": {
                    "TZ": ny_tz,
                }
            },
        )
        expected = {
            "Time": {
                "TZ": ny_tz,
                "TZ-POSIX": ny_tz_posix,
            }
        }

        def check_values(state, expected_values):
            for device_id, device_params in expected_values.items():
                state_params = state.get(device_id)
                assert state_params is not None, (
                    f"Device {device_id} not found in state"
                )
                for param_id, param_value in device_params.items():
                    assert state_params.get(param_id) == param_value, (
                        f"Param {device_id}::{param_id} value does not match, expected {param_value} but got {state_params.get(param_id)}"
                    )

        # Verify the config with the named shadow reported
        def op_func():
            nonlocal expected
            named_shadow_reported = read_named_shadow(user, thing_name, group_id)
            check_values(named_shadow_reported, expected)

            # Try with just timezone POSIX
            wait_for_cloud_params_update(
                node_host_ctrl,
                user,
                thing_name,
                group_id,
                {
                    "Time": {
                        "TZ-POSIX": kigali_tz_posix,
                    }
                },
            )
            expected = {
                "Time": {
                    "TZ-POSIX": kigali_tz_posix,
                }
            }

            # Verify the config with the named shadow reported
            named_shadow_reported = read_named_shadow(user, thing_name, group_id)
            check_values(named_shadow_reported, expected)

            # Try a local update
            node_host_ctrl.clear_on_state_reported()
            node_host_ctrl.update_timezone(ny_tz)
            wait_for_node_state_reported(node_host_ctrl)
            expected = {
                "Time": {
                    "TZ": ny_tz,
                    "TZ-POSIX": ny_tz_posix,
                }
            }
            named_shadow_reported = read_named_shadow(user, thing_name, group_id)
            check_values(named_shadow_reported, expected)

        operate_with_named_shadow_connection(op_func, user, thing_name, group_id)

    except Exception as e:
        node_host_ctrl.update_timezone(old_timezone)
        assert node_host_ctrl.get_current_timezone() == old_timezone, (
            "Device timezone does not match set timezone"
        )
        raise e

    node_host_ctrl.update_timezone(old_timezone)
    assert node_host_ctrl.get_current_timezone() == old_timezone, (
        "Device timezone does not match set timezone"
    )


### Scheduling tests ###


def _scheduling_set_anti_action(node_host_ctrl, anti_action: dict):
    """Self-node thin wrapper: node_host_ctrl drives + flushes through itself."""
    _scheduling_set_anti_action_helper(node_host_ctrl, node_host_ctrl, anti_action)


def _scheduling_verify_schedule(node_host_ctrl, trigger_time: dt, schedule: dict):
    """Self-node thin wrapper around :func:`scheduling_verify_schedule`."""
    _scheduling_verify_schedule_helper(
        node_host_ctrl, node_host_ctrl, trigger_time, schedule
    )


def _scheduling_set_schedules(
    node_host_ctrl, user, group_id, thing_name, schedules: list
):
    """Self-node thin wrapper around :func:`scheduling_set_schedules`."""
    _scheduling_set_schedules_helper(
        user, group_id, node_host_ctrl, thing_name, schedules
    )


@pytest.mark.firmware
@pytest.mark.firmware
def test_firmware_scheduling_cyclical(
    associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test cyclical firmware scheduling behavior.
    """
    print("\n=== Test: cyclical_schedules ===")

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # get zone info for node
    node_timezone = node_host_ctrl.get_current_timezone()
    assert node_timezone is not None, "Failed to get device timezone"
    zone_info = ZoneInfo(node_timezone)

    # Make scheduling node config
    node_config = _get_scheduling_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # wake up schedule: 7:30 AM on Monday to Friday.
    wake_up_schedule = {
        "name": "Wake Up",
        "id": "wake_up",
        "triggers": [
            {
                "m": 7 * 60 + 30,  # 7:30 AM
                "d": 0x1F,  # 0x1F = Monday to Friday
            },
        ],
        "action": {
            "light": {
                "Power": True,
                "Brightness": 100,
            },
            "curtain": {
                "Position": 0,
            },
            "aircon": {
                "Power": False,
            },
        },
        "anti_action": {
            "light": {
                "Power": False,
                "Brightness": 0,
            },
            "curtain": {
                "Position": 100,
            },
            "aircon": {
                "Power": True,
            },
        },
    }
    # bed time schedule: 9:00 PM on Monday, Wednesday and Friday.
    bed_time_schedule = {
        "name": "Bed Time",
        "id": "bed_time",
        "triggers": [
            {
                "m": 21 * 60,  # 9:00 PM
                "d": 0x15,  # 0x15 = Monday, Wednesday and Friday
            },
        ],
        "action": {
            "light": {
                "Power": True,
                "Brightness": 20,
            },
            "curtain": {
                "Position": 100,
            },
            "aircon": {
                "Power": True,
                "Temperature": 22.5,
            },
        },
        "anti_action": {
            "light": {
                "Power": False,
                "Brightness": 0,
            },
            "curtain": {
                "Position": 0,
            },
            "aircon": {
                "Power": False,
                "Temperature": 25.0,
            },
        },
    }

    # set the start time to 7:00 AM on Monday, 15 September 2025
    start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
    node_host_ctrl.time_control_set_time(start_time)
    assert node_host_ctrl.get_current_time() == start_time, (
        "Device time does not match set time"
    )

    # set schedules
    schedules = [wake_up_schedule, bed_time_schedule]
    _scheduling_set_schedules(node_host_ctrl, user, group_id, thing_name, schedules)

    # Create schedule lookup by id
    schedule_lookup = {s["id"]: s for s in schedules}

    # Verify for an entire week: wake up is Monday-Friday at 7:30 AM (0x1F) and
    # bed time is Monday, Wednesday and Friday at 9:00 PM (0x15). Each entry
    # below advances the clock and waits for a state report, so listing a time
    # that cannot fire would hang the test.
    for required_time, schedule_id in [
        # Monday: 7:30 AM wake up, 9:00 PM bed time
        (dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info), "wake_up"),
        (dt(2025, 9, 15, 21, 0, 0, tzinfo=zone_info), "bed_time"),
        # Tuesday: 7:30 AM wake up only
        (dt(2025, 9, 16, 7, 30, 0, tzinfo=zone_info), "wake_up"),
        # Wednesday: 7:30 AM wake up, 9:00 PM bed time
        (dt(2025, 9, 17, 7, 30, 0, tzinfo=zone_info), "wake_up"),
        (dt(2025, 9, 17, 21, 0, 0, tzinfo=zone_info), "bed_time"),
        # Thursday: 7:30 AM wake up only
        (dt(2025, 9, 18, 7, 30, 0, tzinfo=zone_info), "wake_up"),
        # Friday: 7:30 AM wake up, 9:00 PM bed time
        (dt(2025, 9, 19, 7, 30, 0, tzinfo=zone_info), "wake_up"),
        (dt(2025, 9, 19, 21, 0, 0, tzinfo=zone_info), "bed_time"),
        # Saturday and Sunday: nothing fires.
    ]:
        schedule = schedule_lookup[schedule_id]
        _scheduling_verify_schedule(node_host_ctrl, required_time, schedule)

    print("=== Test cyclical_schedules completed successfully ===")


@pytest.mark.firmware
def test_firmware_scheduling_one_time(
    associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Test one-time firmware scheduling.
    """
    print("\n=== Test: one_time_schedules ===")

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Make scheduling node config
    node_config = _get_scheduling_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # get current time on node
    current_time = node_host_ctrl.get_current_time()
    assert current_time is not None, "Failed to get current time"

    # one-time schedule: fires once, 10 minutes out.
    one_time_schedule = {
        "name": "One-Time",
        "id": "one_time",
        "triggers": [
            {
                "rsec": 10 * 60,  # 10 minutes
            },
        ],
        "action": {
            "light": {
                "Power": True,
                "Brightness": 100,
            },
            "curtain": {
                "Position": 0,
            },
        },
        "anti_action": {
            "light": {
                "Power": False,
                "Brightness": 0,
            },
            "curtain": {
                "Position": 100,
            },
        },
    }

    # set the schedules
    _scheduling_set_schedules(
        node_host_ctrl, user, group_id, thing_name, [one_time_schedule]
    )

    # Verify the schedule.
    for required_time in [
        current_time + td(minutes=10),
    ]:
        _scheduling_verify_schedule(node_host_ctrl, required_time, one_time_schedule)

    # clear schedules
    _scheduling_set_schedules(node_host_ctrl, user, group_id, thing_name, [])

    print("=== Test one_time_schedules completed successfully ===")


@pytest.mark.firmware
def test_firmware_scheduling_timezone_change(
    associated_user1_node_host_ctrl_with_user1_connected,
):
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # get zone info for node
    node_timezone = node_host_ctrl.get_current_timezone()
    assert node_timezone is not None, "Failed to get device timezone"
    zone_info = ZoneInfo(node_timezone)

    # Make scheduling node config
    node_config = _get_scheduling_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # zonal schedule
    zonal_schedule = {
        "name": "Zonal",
        "id": "zonal",
        "triggers": [
            {
                "m": 7 * 60 + 30,  # 7:30 AM
                "d": 0x1F,  # 0x1F = Monday to Friday
            },
        ],
        "action": {
            "light": {
                "Power": True,
                "Brightness": 100,
            },
            "curtain": {
                "Position": 0,
            },
            "aircon": {
                "Power": False,
            },
        },
        "anti_action": {
            "light": {
                "Power": False,
                "Brightness": 0,
            },
            "curtain": {
                "Position": 100,
            },
            "aircon": {
                "Power": True,
            },
        },
    }

    # set time to 7:00 AM on Monday, 15 September 2025
    start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
    node_host_ctrl.time_control_set_time(start_time)
    assert node_host_ctrl.get_current_time() == start_time, (
        "Device time does not match set time"
    )

    # set the schedules
    _scheduling_set_schedules(
        node_host_ctrl, user, group_id, thing_name, [zonal_schedule]
    )

    # verify the schedule works
    _scheduling_verify_schedule(
        node_host_ctrl, dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info), zonal_schedule
    )

    try:
        # try a few timezone changes
        for timezone in [
            "America/New_York",
            "Europe/London",
            "Africa/Kigali",
        ]:
            this_zone_info = ZoneInfo(timezone)
            this_start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=this_zone_info)
            node_host_ctrl.time_control_set_time(this_start_time)
            assert node_host_ctrl.get_current_time() == this_start_time, (
                "Device time does not match set time"
            )
            node_host_ctrl.update_timezone(timezone)
            assert node_host_ctrl.get_current_time() == this_start_time, (
                "Device time does not match set time"
            )
            assert node_host_ctrl.get_current_timezone() == timezone, (
                "Device timezone does not match set timezone"
            )
            _scheduling_verify_schedule(
                node_host_ctrl, this_start_time + td(minutes=30), zonal_schedule
            )
    except Exception as e:
        node_host_ctrl.update_timezone(node_timezone)
        assert node_host_ctrl.get_current_timezone() == node_timezone, (
            "Device timezone does not match set timezone"
        )
        raise e
    node_host_ctrl.update_timezone(node_timezone)
    assert node_host_ctrl.get_current_timezone() == node_timezone, (
        "Device timezone does not match set timezone"
    )


@pytest.mark.firmware
@pytest.mark.parametrize(
    "test_name,automation_name,triggers,trigger_bounds_keys,actions,anti_actions",
    [
        # Test 1: Simple trigger with action - Temperature triggers light control
        (
            "temperature_trigger_light_action",
            "Temperature Light Control",
            [
                {
                    "id": "temp-high",
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "operator": "gt",
                    "value": 30.0,
                }
            ],
            ["temp_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Power",
                    "value": True,
                },
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Brightness",
                    "value": 80,
                },
            ],
            [
                {"device": "light", "param": "Power", "value": False},
                {"device": "light", "param": "Brightness", "value": 0},
            ],
        ),
        # Test 2: Boolean trigger with multiple actions - Switch triggers fan and light
        (
            "switch_trigger_multiple_actions",
            "Switch Control Multiple Devices",
            [
                {
                    "id": "switch-on",
                    "device": "switch",
                    "param": "Power",
                    "operator": "eq",
                    "value": True,
                }
            ],
            [None],
            [
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Power",
                    "value": False,
                },
                {
                    "node": "{thing_name}",
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "value": 20.0,
                },
            ],
            [
                {"device": "light", "param": "Power", "value": True},
                {"device": "temp_sensor", "param": "Temperature", "value": 25.0},
            ],
        ),
        # Test 3: Integer less-than trigger - Brightness triggers temperature action
        (
            "brightness_lt_trigger",
            "Brightness Low Temperature Control",
            [{"device": "light", "param": "Brightness", "operator": "lt", "value": 30}],
            ["light_brightness_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "value": 25.5,
                }
            ],
            [{"device": "temp_sensor", "param": "Temperature", "value": 19.0}],
        ),
        # Test 4: Greater-than-or-equal trigger - Temperature triggers multiple light actions
        (
            "temperature_gte_trigger",
            "Temperature GTE Light Control",
            [
                {
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "operator": "ge",
                    "value": 25.0,
                }
            ],
            ["temp_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Power",
                    "value": True,
                },
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Brightness",
                    "value": 60,
                },
            ],
            [
                {"device": "light", "param": "Power", "value": False},
                {"device": "light", "param": "Brightness", "value": 0},
            ],
        ),
        # Test 5: Less-than-or-equal trigger - Temperature triggers switch
        (
            "temperature_lte_trigger",
            "Temperature LTE Switch Control",
            [
                {
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "operator": "le",
                    "value": 22.0,
                }
            ],
            ["temp_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "switch",
                    "param": "Power",
                    "value": True,
                }
            ],
            [{"device": "switch", "param": "Power", "value": False}],
        ),
        # Test 6: Not-equal boolean trigger - Light power not-equal false (i.e., true)
        (
            "light_power_ne_trigger",
            "Light Power Not-Equal Control",
            [{"device": "light", "param": "Power", "operator": "ne", "value": False}],
            [None],
            [
                {
                    "node": "{thing_name}",
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "value": 28.0,
                }
            ],
            [{"device": "temp_sensor", "param": "Temperature", "value": 16.0}],
        ),
        # Test 7: Multiple triggers in single automation - AND condition
        (
            "multi_trigger_and_condition",
            "Multi-Trigger AND Control",
            [
                {"device": "light", "param": "Power", "operator": "eq", "value": True},
                {
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "operator": "gt",
                    "value": 26.0,
                },
            ],
            [None, "temp_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "switch",
                    "param": "Power",
                    "value": True,
                },
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Brightness",
                    "value": 90,
                },
            ],
            [
                {"device": "switch", "param": "Power", "value": False},
                {"device": "light", "param": "Brightness", "value": 0},
            ],
        ),
        # Test 8: Boundary condition test - Exact value match
        (
            "boundary_exact_value",
            "Brightness Exact Value Control",
            [{"device": "light", "param": "Brightness", "operator": "eq", "value": 75}],
            ["light_brightness_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "value": 24.0,
                }
            ],
            [{"device": "temp_sensor", "param": "Temperature", "value": 30.0}],
        ),
        # Test 9: Edge case - Temperature at minimum bound
        (
            "temperature_boundary_edge",
            "Temperature Boundary Control",
            [
                {
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "operator": "lt",
                    "value": "boundary_temp_plus_1",  # Special marker for boundary calculation
                }
            ],
            ["temp_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Power",
                    "value": False,
                }
            ],
            [{"device": "light", "param": "Power", "value": True}],
        ),
        # Test 10: Complex multi-device automation
        (
            "complex_multi_device",
            "Complex Multi-Device Control",
            [
                {
                    "device": "switch",
                    "param": "Power",
                    "operator": "eq",
                    "value": False,
                },
                {
                    "device": "light",
                    "param": "Brightness",
                    "operator": "ge",
                    "value": 50,
                },
            ],
            [None, "light_brightness_bounds"],
            [
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Power",
                    "value": True,
                },
                {
                    "node": "{thing_name}",
                    "device": "light",
                    "param": "Brightness",
                    "value": 100,
                },
                {
                    "node": "{thing_name}",
                    "device": "temp_sensor",
                    "param": "Temperature",
                    "value": 30.0,
                },
            ],
            [
                {"device": "light", "param": "Power", "value": False},
                {"device": "light", "param": "Brightness", "value": 0},
                {"device": "temp_sensor", "param": "Temperature", "value": 20.0},
            ],
        ),
    ],
    ids=[
        "temp_gt_light_control",
        "switch_eq_multi_device",
        "brightness_lt_temp_control",
        "temp_ge_light_multi_action",
        "temp_le_switch_control",
        "light_power_ne_temp_control",
        "multi_trigger_and_condition",
        "brightness_eq_boundary",
        "temp_lt_boundary_edge",
        "complex_multi_device",
    ],
)
def test_firmware_automation_simple(
    associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected,
    test_name,
    automation_name,
    triggers,
    trigger_bounds_keys,
    actions,
    anti_actions,
):
    """
    Test the firmware automation triggers and actions, one automation at a time.
    """
    print(f"\n=== Test: {test_name} ===")

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Deepcopy parametrize-sourced containers — pytest reuses them across iterations on the same worker
    triggers = copy.deepcopy(triggers)
    actions = copy.deepcopy(actions)
    anti_actions = copy.deepcopy(anti_actions)
    trigger_bounds_keys = copy.deepcopy(trigger_bounds_keys)

    # Set automation node config
    node_config = _get_automation_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Resolve trigger bounds based on keys
    trigger_bounds = []
    for key in trigger_bounds_keys:
        if key == "temp_bounds":
            trigger_bounds.append(node_config["devices"][1]["params"][0]["bounds"])
        elif key == "light_brightness_bounds":
            trigger_bounds.append(node_config["devices"][0]["params"][1]["bounds"])
        else:  # None case
            trigger_bounds.append(None)

    # Handle special boundary temperature calculation for Test 9
    for trigger in triggers:
        if (
            isinstance(trigger.get("value"), str)
            and trigger["value"] == "boundary_temp_plus_1"
        ):
            temp_bounds = node_config["devices"][1]["params"][0]["bounds"]
            boundary_temp = temp_bounds["min"] + temp_bounds["step"]
            trigger["value"] = boundary_temp + 1.0
    resolved_triggers = triggers

    # Replace {thing_name} placeholders in actions
    for action in actions:
        if "node" in action and action["node"] == "{thing_name}":
            action["node"] = thing_name
    resolved_actions = actions

    def cleanup():
        # Cleanup - delete all automations and triggers
        try:
            user.delete_all_automations(group_id=group_id)
            print("Cleanup: Deleted all automations")
        except Exception as cleanup_error:
            print(f"Cleanup error: Failed to delete automations: {cleanup_error}")

        try:
            node_host_ctrl.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            assert node_host_ctrl.wait_on_trigger_details(10000), (
                "Node did not receive trigger details after deleting trigger"
            )
            print("Cleanup: Deleted node triggers")
        except Exception as cleanup_error:
            print(f"Cleanup error: Failed to delete triggers: {cleanup_error}")

    try:
        _automation_create_and_test(
            node_host_ctrl=node_host_ctrl,
            user=user,
            group_id=group_id,
            thing_name=thing_name,
            automation_name=automation_name,
            triggers=resolved_triggers,
            trigger_bounds=trigger_bounds,
            actions=resolved_actions,
            anti_actions=anti_actions,
        )

        print(f"=== Test {test_name} completed successfully ===")

    except Exception as e:
        print(f"Automation test {test_name} failed: {e}")
        cleanup()
        raise

    cleanup()


@pytest.mark.firmware
def test_firmware_automation_same_value_refire(
    associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    A same-value param update must still re-fire an automation.
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Configure the node and start it.
    node_config = _get_automation_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )
    start_node_host_ctrl(node_host_ctrl)
    assert group_id is not None, "Node did not receive group id"

    automation_name = "Same Value Refire"
    trigger = {
        "device": "temp_sensor",
        "param": "Temperature",
        "operator": "eq",
        "value": 30.0,
    }
    # Action target node must be set so the cloud routes the action back to this node.
    action = {"node": thing_name, "device": "light", "param": "Power", "value": True}
    trigger_value = trigger["value"]

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception as e:
            print(f"Cleanup error (automations): {e}")
        try:
            node_host_ctrl.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            node_host_ctrl.wait_on_trigger_details(10000)
        except Exception as e:
            print(f"Cleanup error (triggers): {e}")

    def drive_and_expect_fire(label):
        """Set the trigger param to the trigger value and assert the automation fires."""
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert node_host_ctrl.update_param(
            "temp_sensor", "Temperature", trigger_value
        ), f"Failed to set Temperature ({label})"
        assert node_host_ctrl.wait_on_state_reported(10000), (
            f"Node did not report trigger state ({label})"
        )
        assert node_host_ctrl.wait_on_notification_sent(10000), (
            f"Automation did not fire ({label})"
        )
        assert node_host_ctrl.wait_on_state_reported(10000), (
            f"Node did not report state after action ({label})"
        )
        assert node_host_ctrl.get_param("light", "Power").value, (
            f"Automation action was not applied ({label})"
        )

    try:
        # Put the action param "off" and the trigger param away from the trigger value so
        # the first set is a genuine change.
        node_host_ctrl.clear_on_state_reported()
        changed = False
        if node_host_ctrl.get_param("light", "Power").value:
            assert node_host_ctrl.update_param("light", "Power", False)
            changed = True
        if (
            node_host_ctrl.get_param("temp_sensor", "Temperature").value
            == trigger_value
        ):
            assert node_host_ctrl.update_param("temp_sensor", "Temperature", 25.0)
            changed = True
        if changed:
            assert node_host_ctrl.wait_on_state_reported(10000), (
                "Node did not report initial state"
            )

        # Create + wire the automation: temp_sensor.Temperature == 30.0 -> light.Power = True
        automation_id = user.create_automation(
            group_id=group_id,
            automation_data={
                "name": automation_name,
                "conditions": {"and": []},
                "actions": {"targets": []},
            },
        ).get("automation_id")
        assert automation_id is not None, "Failed to create automation"

        wire_triggers = _wire_triggers(
            [trigger], id_prefix=f"{thing_name}~{automation_id}"
        )
        and_condition = [w["id"] for w in wire_triggers]
        assert (
            user.update_automation(
                group_id=group_id,
                automation_id=automation_id,
                automation_data={
                    "name": automation_name,
                    "conditions": {"and": and_condition},
                    "actions": {"targets": _wire_actions([action])},
                },
            )
            is not None
        ), "Failed to update automation"

        node_host_ctrl.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=thing_name,
            trigger_data=json.dumps({"triggers": wire_triggers}),
        ), "Failed to set node triggers"
        assert node_host_ctrl.wait_on_trigger_details(10000), (
            "Node did not receive trigger details"
        )

        # First fire: a genuine change (25.0 -> 30.0).
        drive_and_expect_fire("initial change")

        # Reset only the action param; the trigger param stays at 30.0.
        node_host_ctrl.clear_on_state_reported()
        assert node_host_ctrl.update_param("light", "Power", False)
        assert node_host_ctrl.wait_on_state_reported(10000), (
            "Node did not report state after resetting action param"
        )

        # Same-value re-fire: set Temperature to 30.0 again, which it already equals.
        # With the fix this still re-evaluates the trigger and re-fires the automation.
        drive_and_expect_fire("same-value re-apply")
    finally:
        cleanup()


@pytest.mark.firmware
@pytest.mark.parametrize(
    "test_name,automation_configs,trigger_changes,expected_results,setup_anti_actions,reset_actions",
    [
        # Test 1: Simple concurrent triggers - Two independent automations
        (
            "simple_concurrent_triggers",
            [
                {
                    "name": "Temperature High Alert",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "gt",
                            "value": 28.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
                {
                    "name": "Switch State Monitor",
                    "triggers": [
                        {
                            "device": "switch",
                            "param": "Power",
                            "operator": "eq",
                            "value": True,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Brightness",
                            "value": 90,
                        }
                    ],
                },
            ],
            [
                {"device": "temp_sensor", "param": "Temperature", "value": 30.0},
                {"device": "switch", "param": "Power", "value": True},
            ],
            {"light.Power": True, "light.Brightness": 90},
            True,
            None,
        ),
        # Test 2: Cascading concurrent automations - One triggers another
        (
            "cascading_triggers",
            [
                {
                    "name": "Temperature Triggers Light",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "gt",
                            "value": 26.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Brightness",
                            "value": 80,
                        }
                    ],
                },
                {
                    "name": "Brightness Triggers Switch",
                    "triggers": [
                        {
                            "device": "light",
                            "param": "Brightness",
                            "operator": "ge",
                            "value": 75,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "switch",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
                {
                    "name": "Switch Triggers Temperature",
                    "triggers": [
                        {
                            "device": "switch",
                            "param": "Power",
                            "operator": "eq",
                            "value": True,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "value": 35.0,
                        }
                    ],
                },
            ],
            [{"device": "temp_sensor", "param": "Temperature", "value": 28.0}],
            {
                "light.Brightness": 80,
                "switch.Power": True,
                "temp_sensor.Temperature": 35.0,
            },
            True,
            None,
        ),
        # Test 3a: Multiple conditions on same parameter - Low temperature trigger
        (
            "multiple_conditions_low_temp",
            [
                {
                    "name": "Temperature Low Alert",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "lt",
                            "value": 22.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
                {
                    "name": "Temperature High Alert",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "gt",
                            "value": 30.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "switch",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
            ],
            [{"device": "temp_sensor", "param": "Temperature", "value": 20.0}],
            {"light.Power": True},
            True,
            None,
        ),
        # Test 3b: Multiple conditions on same parameter - High temperature trigger
        (
            "multiple_conditions_high_temp",
            [
                {
                    "name": "Temperature Low Alert",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "lt",
                            "value": 22.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
                {
                    "name": "Temperature High Alert",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "gt",
                            "value": 30.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "switch",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
            ],
            [{"device": "temp_sensor", "param": "Temperature", "value": 32.0}],
            {"switch.Power": True},
            False,
            ["reset_devices"],
        ),
        # Test 4: Cross-device dependencies - Multiple devices affecting each other
        (
            "cross_device_dependencies",
            [
                {
                    "name": "Light Power Controls Temperature",
                    "triggers": [
                        {
                            "device": "light",
                            "param": "Power",
                            "operator": "eq",
                            "value": True,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "value": 29.0,
                        }
                    ],
                },
                {
                    "name": "Switch Power Controls Light",
                    "triggers": [
                        {
                            "device": "switch",
                            "param": "Power",
                            "operator": "eq",
                            "value": True,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
                {
                    "name": "Temperature Controls Switch",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "ge",
                            "value": 28.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "switch",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
            ],
            [{"device": "switch", "param": "Power", "value": True}],
            {
                "light.Power": True,
                "temp_sensor.Temperature": 29.0,
                "switch.Power": True,
            },
            True,
            None,
        ),
        # Test 5: High-frequency concurrent triggers - Many automations triggered rapidly
        (
            "high_frequency_triggers",
            [
                {
                    "name": "Light Power On Automation",
                    "triggers": [
                        {
                            "device": "light",
                            "param": "Power",
                            "operator": "eq",
                            "value": True,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "value": 31.0,
                        }
                    ],
                },
                {
                    "name": "Light Brightness High",
                    "triggers": [
                        {
                            "device": "light",
                            "param": "Brightness",
                            "operator": "gt",
                            "value": 50,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "switch",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
                {
                    "name": "Switch Power On",
                    "triggers": [
                        {
                            "device": "switch",
                            "param": "Power",
                            "operator": "eq",
                            "value": True,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Brightness",
                            "value": 95,
                        }
                    ],
                },
                {
                    "name": "Temperature High Response",
                    "triggers": [
                        {
                            "device": "temp_sensor",
                            "param": "Temperature",
                            "operator": "gt",
                            "value": 30.0,
                        }
                    ],
                    "actions": [
                        {
                            "node": "{thing_name}",
                            "device": "light",
                            "param": "Power",
                            "value": True,
                        }
                    ],
                },
            ],
            [
                {"device": "light", "param": "Power", "value": True},
                {"device": "light", "param": "Brightness", "value": 70},
                {"device": "temp_sensor", "param": "Temperature", "value": 32.0},
            ],
            {
                "light.Power": True,
                "light.Brightness": 95,
                "switch.Power": True,
                "temp_sensor.Temperature": 31.0,
            },
            True,
            None,
        ),
    ],
    ids=[
        "simple_concurrent_independent",
        "cascading_chain_reactions",
        "multiple_conditions_low_trigger",
        "multiple_conditions_high_trigger",
        "cross_device_dependencies",
        "high_frequency_complex_triggers",
    ],
)
def test_firmware_automation_concurrent(
    associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected,
    test_name,
    automation_configs,
    trigger_changes,
    expected_results,
    setup_anti_actions,
    reset_actions,
):
    """
    Test concurrent automation triggers and actions with multiple automations running simultaneously.
    """
    print(f"\n=== Concurrent Test: {test_name} ===")

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Deepcopy parametrize-sourced containers — pytest reuses them across iterations on the same worker
    automation_configs = copy.deepcopy(automation_configs)
    trigger_changes = copy.deepcopy(trigger_changes)
    expected_results = copy.deepcopy(expected_results)

    # Replace {thing_name} placeholder in automation configs
    for config in automation_configs:
        for action in config["actions"]:
            if "node" in action and action["node"] == "{thing_name}":
                action["node"] = thing_name

    # Set automation node config
    node_config = _get_automation_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    def cleanup_concurrent():
        # Cleanup - delete all automations and triggers
        try:
            user.delete_all_automations(group_id=group_id)
            print("Cleanup: Deleted all automations")
        except Exception as cleanup_error:
            print(f"Cleanup error: Failed to delete automations: {cleanup_error}")

        try:
            node_host_ctrl.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            assert node_host_ctrl.wait_on_trigger_details(10000), (
                "Node did not receive trigger details after deleting trigger"
            )
            print("Cleanup: Deleted node triggers")
        except Exception as cleanup_error:
            print(f"Cleanup error: Failed to delete triggers: {cleanup_error}")

    # Handle reset actions for multi-part tests (like Test 3)
    if reset_actions:
        for action in reset_actions:
            if action == "reset_devices":
                node_host_ctrl.clear_on_state_reported()
                assert node_host_ctrl.update_param("light", "Power", False)
                assert node_host_ctrl.update_param("switch", "Power", False)
                node_host_ctrl.wait_on_state_reported(5000)

    try:
        _concurrent_automation_test(
            node_host_ctrl,
            user,
            group_id,
            thing_name,
            automation_configs,
            trigger_changes,
            expected_results,
            setup_anti_actions,
        )

        print(f"=== Test {test_name} completed successfully ===")

    except Exception as e:
        print(f"Concurrent automation test {test_name} failed: {e}")
        cleanup_concurrent()
        raise

    cleanup_concurrent()


@pytest.mark.firmware
def test_firmware_scheduling_persistence_no_reset(
    associated_user1_node_host_ctrl_with_user1_connected,
):
    """
    Verify that schedules persist across a stop/start cycle without resetting NVS.
    """

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Get zone info for node
    node_timezone = node_host_ctrl.get_current_timezone()
    assert node_timezone is not None, "Failed to get device timezone"
    zone_info = ZoneInfo(node_timezone)

    # Configure devices for scheduling
    node_config = _get_scheduling_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node and provision schedules
    start_node_host_ctrl(node_host_ctrl)

    zonal_schedule = {
        "name": "Zonal",
        "id": "zonal",
        "triggers": [
            {
                "m": 7 * 60 + 30,  # 7:30 AM
                "d": 0x1F,  # Monday to Friday
            },
        ],
        "action": {
            "light": {
                "Power": True,
                "Brightness": 100,
            },
            "curtain": {
                "Position": 0,
            },
            "aircon": {
                "Power": False,
            },
        },
        "anti_action": {
            "light": {
                "Power": False,
                "Brightness": 0,
            },
            "curtain": {
                "Position": 100,
            },
            "aircon": {
                "Power": True,
            },
        },
    }

    # Set baseline time and schedules
    start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)  # Monday 7:00
    node_host_ctrl.time_control_set_time(start_time)
    assert node_host_ctrl.get_current_time() == start_time, (
        "Device time does not match set time"
    )

    _scheduling_set_schedules(
        node_host_ctrl, user, group_id, thing_name, [zonal_schedule]
    )

    # Restart the node without reset and verify schedule still triggers
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    # After restart, schedule should still be present and trigger at 7:30
    trigger_time = dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info)
    _scheduling_verify_schedule(node_host_ctrl, trigger_time, zonal_schedule)

    # Cleanup schedules to avoid impacting other tests
    _scheduling_set_schedules(node_host_ctrl, user, group_id, thing_name, [])


@pytest.mark.firmware
def test_firmware_automation_persistence_no_reset(
    associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected,
):
    """
    Verify that node-side triggers for automations persist across a stop/start cycle without resetting NVS.
    """

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Configure automation devices
    node_config = _get_automation_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    def cleanup():
        user.delete_all_automations(group_id=group_id)
        try:
            node_host_ctrl.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            assert node_host_ctrl.wait_on_trigger_details(10000), (
                "Node did not receive trigger details after deleting trigger"
            )
        except Exception:
            pass

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Create a simple automation: temp_sensor.Temperature > 28.0 -> light.Power=True, light.Brightness=80
    empty_automation = {
        "name": "Temp High Lights",
        "conditions": {"and": []},
        "actions": {"targets": []},
    }
    automation_id = user.create_automation(
        group_id=group_id, automation_data=empty_automation
    ).get("automation_id")
    assert automation_id is not None, "Failed to create empty automation"

    try:
        trigger = {
            "device": "temp_sensor",
            "param": "Temperature",
            "operator": "gt",
            "value": 28.0,
        }
        wire_triggers = _wire_triggers(
            [trigger], id_prefix=f"{thing_name}~{automation_id}"
        )
        trigger_id = wire_triggers[0]["id"]

        actions = [
            {"node": thing_name, "device": "light", "param": "Power", "value": True},
            {"node": thing_name, "device": "light", "param": "Brightness", "value": 80},
        ]
        automation_data = {
            "name": "Temp High Lights",
            "description": "Persistence test automation",
            "conditions": {"and": [trigger_id]},
            "actions": {"targets": _wire_actions(actions)},
        }
        assert (
            user.update_automation(
                group_id=group_id,
                automation_id=automation_id,
                automation_data=automation_data,
            )
            is not None
        ), "Failed to update automation"

        trigger_data = {"triggers": wire_triggers}
        node_host_ctrl.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id, node_id=thing_name, trigger_data=json.dumps(trigger_data)
        ), "Failed to set node trigger"
        assert node_host_ctrl.wait_on_trigger_details(10000), (
            "Node did not receive trigger details"
        )

        # Set anti-action values
        node_host_ctrl.clear_on_state_reported()
        set_anti_action = False
        if node_host_ctrl.get_param("light", "Power").value:
            set_anti_action = True
            assert node_host_ctrl.update_param("light", "Power", False)
        if node_host_ctrl.get_param("light", "Brightness").value != 0:
            set_anti_action = True
            assert node_host_ctrl.update_param("light", "Brightness", 0)
        if set_anti_action:
            assert node_host_ctrl.wait_on_state_reported(5000), (
                "Node did not report state after setting anti-action values"
            )

        # Restart without reset; do NOT re-provision triggers
        restart_node_host_ctrl_without_reset(node_host_ctrl)

        # Drive the trigger condition again and verify actions fire
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert node_host_ctrl.update_param("temp_sensor", "Temperature", 29.0)
        assert node_host_ctrl.wait_on_state_reported(10000), (
            "Node did not report state after driving trigger"
        )
        assert node_host_ctrl.wait_on_notification_sent(10000), (
            "Node did not send notification after trigger"
        )
        assert node_host_ctrl.wait_on_state_reported(10000), (
            "Node did not report new state after sending notification"
        )

        assert node_host_ctrl.get_param("light", "Power").value is True, (
            "Automation action (Power) not executed after restart"
        )
        assert node_host_ctrl.get_param("light", "Brightness").value == 80, (
            "Automation action (Brightness) not executed after restart"
        )
    except Exception as e:
        print(f"Automation persistence test failed: {e}")
        cleanup()
        raise e

    # Cleanup
    cleanup()


@pytest.mark.firmware
@pytest.mark.parametrize(
    "version_type,config_getter,setter_func,setter_empty_func,getter_func,clear_details_func,wait_details_func",
    [
        (
            "scheduling",
            _get_scheduling_node_config,
            lambda nr, u, g, t: _scheduling_set_schedules(
                nr,
                u,
                g,
                t,
                [
                    {
                        "name": "Simple Schedule",
                        "id": "simple",
                        "triggers": [
                            {"rsec": 3600}
                        ],  # 1 hour from now so it doesn't trigger during the test
                        "action": {"Light": {"Power": True}},
                    }
                ],
            ),
            lambda nr, u, g, t: _scheduling_set_schedules(nr, u, g, t, []),
            lambda nr: nr.get_sched_version(),
            lambda nr: nr.clear_on_all_sched_events(),
            lambda nr: nr.wait_on_sched_details(10000),
        ),
        (
            "automation",
            _get_automation_node_config,
            lambda nr, u, g, t: u.set_node_trigger(
                group_id=g,
                node_id=t,
                trigger_data=json.dumps({"triggers": [{"id": "versioning-test"}]}),
            ),
            lambda nr, u, g, t: u.set_node_trigger(
                group_id=g, node_id=t, trigger_data=json.dumps({"triggers": []})
            ),
            lambda nr: nr.get_trigger_version(),
            lambda nr: nr.clear_on_all_trigger_events(),
            lambda nr: nr.wait_on_trigger_details(10000),
        ),
    ],
    ids=["scheduling_version_tracking", "automation_version_tracking"],
)
def test_firmware_versioning(
    associated_user1_node_host_ctrl_with_user1_connected,
    version_type,
    config_getter,
    setter_func,
    setter_empty_func,
    getter_func,
    clear_details_func,
    wait_details_func,
):
    """
    Verify version numbers are kept up to date with details for scheduling and automation.
    """
    print(f"\n=== Test: {version_type} versioning ===")

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Configure devices
    node_config = config_getter()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # 1. Get initial version
    initial_ver = getter_func(node_host_ctrl)
    assert initial_ver is not None, f"Failed to get initial {version_type} version"

    # 2. Wait one second
    sleep(1)

    # 3. Clear details flag
    clear_details_func(node_host_ctrl)

    # 4. Set function
    setter_func(node_host_ctrl, user, group_id, thing_name)

    # 5. Wait details flag
    wait_details_func(node_host_ctrl)

    # 6. Get new version and assert increase
    new_ver = getter_func(node_host_ctrl)
    assert new_ver is not None, f"Failed to get new {version_type} version"
    assert new_ver > initial_ver, f"{version_type} version did not increase"

    # 7. Wait one second
    sleep(1)

    # 8. Set empty
    clear_details_func(node_host_ctrl)
    setter_empty_func(node_host_ctrl, user, group_id, thing_name)
    wait_details_func(node_host_ctrl)

    # 9. Get new version and assert increase
    final_ver = getter_func(node_host_ctrl)
    assert final_ver is not None, f"Failed to get final {version_type} version"
    assert final_ver > new_ver, (
        f"{version_type} version did not increase after setting empty"
    )

    print(f"=== Test {version_type} versioning completed successfully ===")


@pytest.mark.firmware
@pytest.mark.parametrize(
    "version_type,setter_func,getter_func,updater_func,send_func,wait_version_func,wait_details_func,wait_all_func,clear_all_func",
    [
        (
            "scheduling",
            lambda nr, u, g, t: _scheduling_set_schedules(nr, u, g, t, []),
            lambda nr: nr.get_sched_version(),
            lambda nr: nr.update_sched_version(-1),
            lambda nr: nr.cloud_control_send_getSchedVer(),
            lambda nr: nr.wait_on_sched_version(10000),
            lambda nr: nr.wait_on_sched_details(10000),
            lambda nr: nr.wait_on_all_sched_events(60000),
            lambda nr: nr.clear_on_all_sched_events(),
        ),
        (
            "automation",
            lambda nr, u, g, t: u.set_node_trigger(
                group_id=g, node_id=t, trigger_data=json.dumps({"triggers": []})
            ),
            lambda nr: nr.get_trigger_version(),
            lambda nr: nr.update_trigger_version(-1),
            lambda nr: nr.cloud_control_send_getTriggerVer(),
            lambda nr: nr.wait_on_trigger_version(10000),
            lambda nr: nr.wait_on_trigger_details(10000),
            lambda nr: nr.wait_on_all_trigger_events(60000),
            lambda nr: nr.clear_on_all_trigger_events(),
        ),
    ],
    ids=["scheduling_version_error_recovery", "automation_version_error_recovery"],
)
def test_firmware_version_details_error_handling(
    associated_user1_node_host_ctrl_with_user1_connected,
    version_type,
    setter_func,
    getter_func,
    updater_func,
    send_func,
    wait_version_func,
    wait_details_func,
    wait_all_func,
    clear_all_func,
):
    """
    Verify version details error handling for scheduling and automation.
    """
    print(f"\n=== Test: {version_type} version details error handling ===")

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # Get initial version
    initial_ver = getter_func(node_host_ctrl)
    assert initial_ver is not None, f"Failed to get initial {version_type} version"

    # Wait one second before setting
    sleep(1)

    # Clear details flag, set data, then wait on details flag
    clear_all_func(node_host_ctrl)
    setter_func(node_host_ctrl, user, group_id, thing_name)
    wait_details_func(node_host_ctrl)

    # Verify version increased
    new_ver = getter_func(node_host_ctrl)
    assert new_ver is not None, f"Failed to get new {version_type} version"
    assert new_ver > initial_ver, f"{version_type} version did not increase"

    # Clear the version locally
    assert updater_func(node_host_ctrl), (
        f"Failed to update {version_type} version to -1"
    )
    assert getter_func(node_host_ctrl) == -1, (
        f"{version_type} version did not reset locally to -1"
    )

    # Manually send a cloud event to get the new version
    clear_all_func(node_host_ctrl)
    assert send_func(node_host_ctrl), f"Failed to send get{version_type}Ver cloud event"
    assert node_host_ctrl.mqtt_control_force_operations_failure(), (
        "Failed to force MQTT operations failure"
    )

    try:
        assert wait_version_func(node_host_ctrl), (
            f"Node did not receive {version_type} version"
        )

        # Make sure the details are not received
        assert not wait_details_func(node_host_ctrl), (
            f"Node received {version_type} details despite MQTT operations failure"
        )
        assert getter_func(node_host_ctrl) == -1, (
            f"{version_type} version did not reset to -1 despite MQTT operations failure"
        )

        # Restore MQTT operations
        clear_all_func(node_host_ctrl)
        assert node_host_ctrl.mqtt_control_restore_operations_default(), (
            "Failed to restore MQTT operations default"
        )
        assert wait_all_func(node_host_ctrl), (
            f"Node did not receive all {version_type} events after restoring MQTT operations"
        )
        assert getter_func(node_host_ctrl) == new_ver, (
            f"{version_type} version did not increase after restoring MQTT operations"
        )

        print(
            f"=== Test {version_type} version details error handling completed successfully ==="
        )

    except Exception as e:
        node_host_ctrl.mqtt_control_restore_operations_default()
        raise e


### Timeseries tests ###


from helpers.timeseries import (  # noqa: E402
    get_timeseries_node_config as _get_timeseries_node_config,
    timeseries_param_specs as _timeseries_param_specs,
    timeseries_random_value_in_bounds as _timeseries_random_value_in_bounds,
    timeseries_verify_with_user_api as _timeseries_verify_with_user_api,
)


def _timeseries_publish_test_data(node_host_ctrl, data_points):
    """
    Publish timeseries test data by updating all parameters for each timestamp,
    then advancing time and waiting for state reported.
    """

    # Group data points by timestamp
    timestamp_groups = {}
    for point in data_points:
        timestamp = point["timestamp"]
        if timestamp not in timestamp_groups:
            timestamp_groups[timestamp] = []
        timestamp_groups[timestamp].append(point)

    # Set base time
    base_time = min(timestamp_groups.keys())
    node_host_ctrl.time_control_set_time(dt.fromtimestamp(base_time))

    # Process each timestamp group
    sorted_timestamps = sorted(timestamp_groups.keys())

    for i, timestamp in enumerate(sorted_timestamps):
        group_points = timestamp_groups[timestamp]

        # Update all parameters in this group (testing queue functionality)
        node_host_ctrl.clear_on_timeseries_reported()
        node_host_ctrl.clear_on_state_reported()

        # Set time to the timestamp
        node_host_ctrl.time_control_set_time(dt.fromtimestamp(timestamp))
        for point in group_points:
            device_id = point["device"]
            param_id = point["param"]
            param_value = point["value"]
            assert node_host_ctrl.update_param(device_id, param_id, param_value), (
                f"Failed to update param {device_id}::{param_id} to {param_value}"
            )

        # Wait for state and timeseries reported (advancing time triggers the report)
        assert node_host_ctrl.wait_on_state_reported(5000), (
            f"Node did not report state after timestamp {i + 1}/{len(sorted_timestamps)}"
        )
        assert node_host_ctrl.wait_on_timeseries_reported(5000), (
            f"Node did not report timeseries after timestamp {i + 1}/{len(sorted_timestamps)}"
        )


@pytest.mark.firmware
def test_firmware_timeseries(
    associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected,
    test_user2,
):
    """
    Test the firmware timeseries functionality.
    Tests the complete timeseries workflow within the firmware testing framework:
    1. Node publishes timeseries data via parameter updates
    2. Tests the queue functionality of the timeseries manager
    3. Comprehensive testing of REST APIs (raw, latest)
    4. Validates topic format, data integrity, and API functionality
    5. Tests pagination and basic data retrieval
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_basic_ingest_variants_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    print("🔄 Starting firmware timeseries test...")

    # PART 1: CONFIGURE NODE
    print("1. Configuring node for timeseries...")
    node_config = _get_timeseries_node_config()
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # PART 2: PUBLISH COMPREHENSIVE TEST DATA
    print("2. Publishing test data...")

    # Base timestamp = real now. Test runs after build, so now > the build-time
    # TIMESYNC_REF_TIME floor → points accepted by timesync_epoch_ms_is_valid().
    base_ts = int(time_sec())
    param_specs = _timeseries_param_specs(node_config)
    num_ts = 25
    sorted_timestamps = [base_ts + (i * 2) for i in range(num_ts)]
    # start/end in ms — same unit as expected points and the cloud `ts`.
    start_time = min(sorted_timestamps) * 1000
    end_time = max(sorted_timestamps) * 1000

    # Get last value from device for each param (for first update we must not repeat it)
    last_value = {}
    for spec in param_specs:
        p = node_host_ctrl.get_param(spec["device"], spec["param"])
        assert p is not None, f"Param {spec['device']}::{spec['param']} not found"
        last_value[spec["key"]] = p.value

    # Build test data: for each timestamp, for each param, send a random value != last_value, within bounds
    test_data_points = []
    expected_points_by_param = {spec["key"]: [] for spec in param_specs}
    for timestamp in sorted_timestamps:
        for spec in param_specs:
            key = spec["key"]
            val = _timeseries_random_value_in_bounds(
                spec["bounds"], spec["data_type"], last_value[key]
            )
            last_value[key] = val
            test_data_points.append(
                {
                    "timestamp": timestamp,
                    "device": spec["device"],
                    "param": spec["param"],
                    "value": val,
                }
            )
            # Store expected timestamp in ms (the unit the cloud returns for
            # raw `ts`), so verification can compare directly.
            expected_points_by_param[key].append((timestamp * 1000, val))

    # Publish all test data
    _timeseries_publish_test_data(node_host_ctrl, test_data_points)

    print("✅ All test data published. Waiting for stream processor...")
    sleep(25)  # Wait for stream processor to process all data

    # PART 3: VERIFY DATA VIA USER APIs (all points in time period, using raw API with 1s buffer)
    print("3. Verifying data via user APIs (raw timeseries in time period)...")
    _timeseries_verify_with_user_api(
        user,
        group_id,
        thing_name,
        expected_points_by_param,
        start_time,
        end_time,
        param_specs,
    )

    # PART 4: TEST UNAUTHORIZED ACCESS
    print("4. Testing unauthorized access...")

    try:
        # This should fail with unauthorized error
        unauthorized_response = test_user2.get_node_timeseries_latest(
            group_id=group_id, node_id=thing_name, key="temperature", data_type="float"
        )
        assert unauthorized_response is None, (
            "Unauthorized user should not be able to access timeseries data"
        )
    except Exception as e:
        assert "unauthorized" in str(e).lower() or "403" in str(e) or "401" in str(e), (
            f"Should get unauthorized error, got: {str(e)}"
        )
        print("✅ Unauthorized access properly blocked")

    print("🎉 Firmware timeseries test completed successfully!")
    print("📋 Test Summary:")
    print("   - Node configuration ✅")
    print("   - Timeseries data publishing with queue functionality ✅")
    print("   - API Info ✅")
    print("   - Latest data APIs ✅")
    print("   - Raw data APIs with pagination ✅")
    print("   - Unauthorized access blocking ✅")


def _firmware_alexa_prepare_light_discovered(node_host_ctrl, user, group_id):
    """
    Shared setup for firmware Alexa tests: webhook/alexa client registration, lightbulb config,
    node start, Discovery, and wait until the node is Alexa-enabled.
    """
    # Ensure that at least one Alexa skill function ARN is available, then take one randomly
    assert len(RM_CONFIG["AlexaSkillFunctionArns"]) > 0, (
        "No Alexa skill function ARNs found"
    )
    invoke_region, alexa_skill_function_arn = random.choice(
        list(RM_CONFIG["AlexaSkillFunctionArns"].items())
    )

    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    webhook_creds = {
        "refresh_token": "test_refresh_token",
        "access_token": "test_access_token",
        "expires_at": int(time_sec())
        - (3600 * 24 * 30),  # 1 month before now, so force to refresh
    }
    user.register_client(
        platform_type="webhook_mock", mobile_device_token=json.dumps(webhook_creds)
    )
    user.register_client(
        platform_type="alexa", mobile_device_token=json.dumps(webhook_creds)
    )

    node_config = {
        "devices": [
            {
                "id": "Light",
                "type": "esp.device.lightbulb",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": False,
                        "properties": ["read", "write"],
                    }
                ],
            }
        ],
        "services": [],
        "tags": {},
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )
    start_node_host_ctrl(node_host_ctrl)

    node_host_ctrl.clear_on_alexa_enabled()
    user.alexa_set_lambda_arn(lambda_arn=alexa_skill_function_arn)
    discovery_response = user.alexa_discover_devices(region=invoke_region)
    assert "errorMessage" not in discovery_response, (
        f"Discovery response returned error: {discovery_response['errorMessage']}"
    )

    discovery_namespace = discovery_response["event"]["header"]["namespace"]
    discovery_name = discovery_response["event"]["header"]["name"]
    discovery_payload_version = discovery_response["event"]["header"]["payloadVersion"]
    assert discovery_namespace == "Alexa.Discovery", (
        f"Discovery namespace should be Alexa.Discovery, got {discovery_namespace}"
    )
    assert discovery_name == "Discover.Response", (
        f"Discovery name should be Discover.Response, got {discovery_name}"
    )
    assert discovery_payload_version == "3", (
        f"Discovery payload version should be 3, got {discovery_payload_version}"
    )

    discovery_endpoints = discovery_response["event"]["payload"]["endpoints"]
    assert discovery_endpoints is not None, "Discovery payload should contain endpoints"
    assert len(discovery_endpoints) > 0, "Discovery should have at least one endpoint"
    expected_endpoint_id = f"{thing_name}#Light"
    found_endpoint = False
    for discovery_endpoint in discovery_endpoints:
        if discovery_endpoint["endpointId"] == expected_endpoint_id:
            found_endpoint = True
            break
    assert found_endpoint, (
        f"Discovery should have endpoint {expected_endpoint_id}, got {', '.join(endpoint['endpointId'] for endpoint in discovery_endpoints)}"
    )

    assert node_host_ctrl.wait_on_alexa_enabled(5000), (
        "Node did not receive getAlexaEn event"
    )
    assert node_host_ctrl.get_alexa_enabled(), "Node is not Alexa enabled"
    return invoke_region, thing_name


def _firmware_webhook_validate_with_retry(
    validate_once,
    *,
    label: str = "Webhook",
    initial_sleep_s: float = 2,
    retries: int = 1,
    retry_sleep_s: float = 5,
):
    """
    Run validate_once after initial_sleep_s. On failure, retry up to `retries` additional times
    (total attempts = 1 + retries).
    """
    sleep(initial_sleep_s)
    attempts = 1 + retries
    for attempt in range(attempts):
        try:
            validate_once()
            return
        except (AssertionError, Exception) as err:
            if attempt == attempts - 1:
                raise
            print(
                f"{label} validation failed on attempt {attempt + 1}/{attempts}: {err}"
            )
            print(f"Waiting {retry_sleep_s} seconds before retry...")
            sleep(retry_sleep_s)


def _firmware_alexa_validate_change_report_webhook_once(
    user, thing_name: str, is_power_on: bool
):
    """Fetch latest Alexa notification from webhook mock and assert ChangeReport shape and powerState."""
    response = webhook_mock_validate("alexa", user.sub)

    assert response.status_code == 200, (
        f"Failed to validate Alexa notification for user {user.sub}: {response.text}"
    )

    notification_data = response.json()
    assert notification_data is not None, (
        f"No notification data received for user {user.sub}"
    )

    event = notification_data["event"]
    event_namespace = event["header"]["namespace"]
    event_name = event["header"]["name"]
    event_payload_version = event["header"]["payloadVersion"]
    assert event_namespace == "Alexa", (
        f"Event namespace should be Alexa, got {event_namespace}"
    )
    assert event_name == "ChangeReport", (
        f"Event name should be ChangeReport, got {event_name}"
    )
    assert event_payload_version == "3", (
        f"Payload version should be 3, got {event_payload_version}"
    )

    endpoint_id = event["endpoint"]["endpointId"]
    expected_endpoint_id = f"{thing_name}#Light"
    assert endpoint_id == expected_endpoint_id, (
        f"Endpoint ID should be {expected_endpoint_id}, got {endpoint_id}"
    )

    change_props = event["payload"]["change"]["properties"]
    assert len(change_props) > 0, "Should have at least one changed property"

    power_prop = next((p for p in change_props if p["name"] == "powerState"), None)
    assert power_prop is not None, "Should have powerState property change"
    assert power_prop["value"] == ("ON" if is_power_on else "OFF"), (
        f"Power state should be {'ON' if is_power_on else 'OFF'}, got {power_prop['value']}"
    )
    assert power_prop["namespace"] == "Alexa.PowerController", (
        "Power property should be in Alexa.PowerController namespace"
    )

    context_props = notification_data["context"]["properties"]
    connectivity_prop = next(
        (p for p in context_props if p["name"] == "connectivity"), None
    )
    assert connectivity_prop is not None, "Should have connectivity property in context"
    assert connectivity_prop["value"]["value"] == "OK", "Connectivity should be OK"
    assert connectivity_prop["namespace"] == "Alexa.EndpointHealth", (
        "Connectivity should be in Alexa.EndpointHealth namespace"
    )


def _firmware_alexa_validate_change_report_webhook(
    user, thing_name: str, is_power_on: bool, retries: int = 1
):
    """Fetch latest Alexa notification from webhook mock and assert ChangeReport shape and powerState."""
    _firmware_webhook_validate_with_retry(
        lambda: _firmware_alexa_validate_change_report_webhook_once(
            user, thing_name, is_power_on
        ),
        label="Alexa webhook",
        retries=retries,
    )


def _firmware_gva_prepare_light_discovered(node_host_ctrl, user, group_id):
    """
    Shared setup for firmware GVA notification tests: webhook / GVA client registration, lightbulb
    (Power only, same as Alexa notification test), node start, SYNC discovery, wait until GVA-enabled.
    """
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    webhook_creds = {
        "refresh_token": "test_refresh_token",
        "access_token": "test_access_token",
        "expires_at": int(time_sec()) - (3600 * 24 * 30),
    }
    user.register_client(
        platform_type="webhook_mock", mobile_device_token=json.dumps(webhook_creds)
    )
    user.register_client(
        platform_type="gva", mobile_device_token=json.dumps(webhook_creds)
    )

    node_config = {
        "devices": [
            {
                "id": "Light",
                "type": "esp.device.lightbulb",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": False,
                        "properties": ["read", "write"],
                    }
                ],
            }
        ],
        "services": [],
        "tags": {},
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )
    start_node_host_ctrl(node_host_ctrl)

    user.get_aws_credentials()

    node_host_ctrl.clear_on_gva_enabled()
    discovery_response = user.gva_discover_devices()

    assert "payload" in discovery_response, (
        f"GVA SYNC missing payload: {discovery_response}"
    )
    payload = discovery_response["payload"]
    assert payload.get("agentUserId") == user.sub, (
        "GVA agentUserId should match user sub"
    )

    device_id = f"{thing_name}.Light"
    devices = payload.get("devices") or []
    gva_device = next((d for d in devices if d.get("id") == device_id), None)
    assert gva_device is not None, (
        f"SYNC should include {device_id}, got ids {[d.get('id') for d in devices]}"
    )

    assert gva_device.get("type") == "action.devices.types.LIGHT"
    traits = sorted(gva_device.get("traits") or [])
    assert traits == sorted(["action.devices.traits.OnOff"]), (
        f"SYNC traits should be OnOff only for Power-only light, got {traits}"
    )
    assert gva_device.get("willReportState") is True
    custom = gva_device.get("customData") or {}
    assert custom.get("groupID") == group_id
    assert custom.get("paramMap_OnOff") == "Power"

    assert node_host_ctrl.wait_on_gva_enabled(5000), (
        "Node did not receive getGVAEn event"
    )
    assert node_host_ctrl.get_gva_enabled(), "Node is not GVA enabled"

    return thing_name


def _firmware_gva_validate_report_state_webhook_once(
    user, thing_name: str, is_power_on: bool
):
    """
    Fetch latest GVA notification from webhook mock and assert report-state shape
    (matches rmng itest: /v1/gva/validate, gva flag, payload.devices.states).
    """
    response = webhook_mock_validate("gva", user.sub)
    assert response.status_code == 200, (
        f"Failed to validate GVA notification for user {user.sub}: {response.text}"
    )

    notification_data = response.json()
    assert notification_data is not None, (
        f"No GVA notification data for user {user.sub}"
    )
    assert notification_data.get("gva") is True, (
        f"Not a GVA notification: {notification_data}"
    )

    assert "payload" in notification_data, "Missing payload in GVA notification"
    pl = notification_data["payload"]
    assert "devices" in pl, "Missing devices in GVA payload"
    assert "states" in pl["devices"], "Missing devices.states in GVA payload"
    states = pl["devices"]["states"]

    device_id = f"{thing_name}.Light"
    assert device_id in states, (
        f"Device {device_id} not in GVA states for user {user.sub}"
    )
    dev_state = states[device_id]
    assert dev_state.get("on") is is_power_on, (
        f"GVA on should be {is_power_on}, got {dev_state.get('on')}"
    )
    assert dev_state.get("online") is True, (
        f"GVA online should be True, got {dev_state.get('online')}"
    )


def _firmware_gva_validate_report_state_webhook(
    user, thing_name: str, is_power_on: bool, retries: int = 1
):
    """Fetch latest GVA notification from webhook mock and assert report-state shape."""
    _firmware_webhook_validate_with_retry(
        lambda: _firmware_gva_validate_report_state_webhook_once(
            user, thing_name, is_power_on
        ),
        label="GVA webhook",
        retries=retries,
    )


@pytest.mark.firmware
def test_firmware_alexa_notification(
    associated_user1_node_host_ctrl_with_user1_connected, webhook_mock_setup
):
    """
    Verify Alexa ChangeReport notifications: local parameter updates propagate through the shadow
    (notify.alexa), the notification service delivers to the webhook mock, and payload shape matches
    Alexa Smart Home v3 (endpoint, powerState, EndpointHealth context).
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    print("🔄 Starting firmware Alexa notification test...")
    _, thing_name = _firmware_alexa_prepare_light_discovered(
        node_host_ctrl, user, group_id
    )

    node_host_ctrl.clear_on_state_reported()
    assert node_host_ctrl.update_param("Light", "Power", True), (
        "Failed to update Power parameter"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after parameter update"
    )
    _firmware_alexa_validate_change_report_webhook(user, thing_name, is_power_on=True)

    node_host_ctrl.clear_on_state_reported()
    assert node_host_ctrl.update_param("Light", "Power", False), (
        "Failed to update Power parameter"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after parameter update"
    )
    _firmware_alexa_validate_change_report_webhook(user, thing_name, is_power_on=False)

    print("🎉 Firmware Alexa notification test completed successfully!")


@pytest.mark.firmware
def test_firmware_gva_notification(
    associated_user1_node_host_ctrl_with_user1_connected, webhook_mock_setup
):
    """
    Verify GVA report-state notifications: local parameter updates propagate through the shadow
    (notify.gva), the notification service delivers to the webhook mock, and payload shape matches
    the GVA validate API (gva flag, payload.devices.states with on/online).
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    print("🔄 Starting firmware GVA notification test...")
    thing_name = _firmware_gva_prepare_light_discovered(node_host_ctrl, user, group_id)

    node_host_ctrl.clear_on_state_reported()
    assert node_host_ctrl.update_param("Light", "Power", True), (
        "Failed to update Power parameter"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after parameter update"
    )
    _firmware_gva_validate_report_state_webhook(user, thing_name, is_power_on=True)

    node_host_ctrl.clear_on_state_reported()
    assert node_host_ctrl.update_param("Light", "Power", False), (
        "Failed to update Power parameter"
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after parameter update"
    )
    _firmware_gva_validate_report_state_webhook(user, thing_name, is_power_on=False)

    print("🎉 Firmware GVA notification test completed successfully!")


@pytest.mark.firmware
def test_firmware_alexa_control(
    associated_user1_node_host_ctrl_with_user1_connected, webhook_mock_setup
):
    """
    Verify Alexa skill control path: PowerController TurnOn/TurnOff updates the device and produces
    ChangeReports; ReportState returns powerState and connectivity in context.
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    print("🔄 Starting firmware Alexa control test...")
    invoke_region, thing_name = _firmware_alexa_prepare_light_discovered(
        node_host_ctrl, user, group_id
    )

    cookie = {"groupID": group_id, "paramMap_PowerController": "Power"}

    def validate_alexa_control(
        capability, action, payload=None, expected_response_structure=None
    ):
        response = user.alexa_control_device(
            device_thing_name=f"{thing_name}#Light",
            capability=capability,
            action=action,
            cookie=cookie,
            payload=payload,
            region=invoke_region,
        )
        print(f"Alexa control response for {action}: {response}")

        assert response is not None, f"No response received for {action}"
        assert "event" in response, f"Response should contain 'event' for {action}"
        assert "header" in response["event"], (
            f"Response event should contain 'header' for {action}"
        )
        assert response["event"]["header"]["namespace"] == "Alexa", (
            f"Response namespace should be 'Alexa' for {action}"
        )

        if expected_response_structure:
            for key in expected_response_structure:
                assert key in response, f"Response should contain '{key}' for {action}"

        return response

    # Ensure the node starts at OFF
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.update_param("Light", "Power", False)
    node_host_ctrl.wait_on_state_reported(5000)  # Clear stale state
    assert not node_host_ctrl.get_param("Light", "Power").value, (
        "Power parameter should be False after Power parameter update"
    )

    print("   - Testing PowerController TurnOn...")
    node_host_ctrl.clear_on_state_reported()
    validate_alexa_control(
        "Alexa.PowerController",
        "TurnOn",
        expected_response_structure=["event", "context"],
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after PowerController TurnOn"
    )
    assert node_host_ctrl.get_param("Light", "Power").value, (
        "Power parameter should be True after PowerController TurnOn"
    )
    _firmware_alexa_validate_change_report_webhook(user, thing_name, is_power_on=True)

    print("   - Testing PowerController TurnOff...")
    node_host_ctrl.clear_on_state_reported()
    validate_alexa_control(
        "Alexa.PowerController",
        "TurnOff",
        expected_response_structure=["event", "context"],
    )
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after PowerController TurnOff"
    )
    assert not node_host_ctrl.get_param("Light", "Power").value, (
        "Power parameter should be False after PowerController TurnOff"
    )
    _firmware_alexa_validate_change_report_webhook(user, thing_name, is_power_on=False)

    print("   - Testing ReportState...")
    report_state_response = validate_alexa_control(
        "Alexa", "ReportState", expected_response_structure=["context", "event"]
    )

    context_props = report_state_response.get("context", {}).get("properties", [])
    power_prop = next((p for p in context_props if p.get("name") == "powerState"), None)
    assert power_prop is not None, "Should have powerState in ReportState response"

    connectivity_prop = next(
        (p for p in context_props if p.get("name") == "connectivity"), None
    )
    assert connectivity_prop is not None, (
        "Should have connectivity in ReportState response"
    )
    assert connectivity_prop["value"]["value"] == "OK", "Connectivity should be OK"

    print("🎉 Firmware Alexa control test completed successfully!")


@pytest.mark.firmware
def test_firmware_gva_control(associated_user1_node_host_ctrl_with_user1_connected):
    """
    GVA firmware control: SYNC discovery, EXECUTE OnOff / BrightnessAbsolute, and QUERY for a light with Power + Brightness.
    Does not use webhook mock or firmware-side GVA enable flags (none exist).
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    user.get_aws_credentials()

    node_config = {
        "devices": [
            {
                "id": "Light",
                "type": "esp.device.lightbulb",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": False,
                        "properties": ["read", "write"],
                    },
                    {
                        "id": "Brightness",
                        "type": "esp.param.brightness",
                        "data_type": "int",
                        "value": 0,
                        "bounds": {"min": 0, "max": 100, "step": 1},
                        "properties": ["read", "write"],
                    },
                ],
            }
        ],
        "services": [],
        "tags": {},
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    start_node_host_ctrl(node_host_ctrl)
    discovery_response = user.gva_discover_devices()

    assert "payload" in discovery_response, (
        f"GVA SYNC missing payload: {discovery_response}"
    )
    payload = discovery_response["payload"]
    assert payload.get("agentUserId") == user.sub, (
        "GVA agentUserId should match user sub"
    )

    device_id = f"{thing_name}.Light"
    devices = payload.get("devices") or []
    gva_device = next((d for d in devices if d.get("id") == device_id), None)
    assert gva_device is not None, (
        f"SYNC should include {device_id}, got ids {[d.get('id') for d in devices]}"
    )

    assert gva_device.get("type") == "action.devices.types.LIGHT"
    traits = sorted(gva_device.get("traits") or [])
    assert traits == sorted(
        ["action.devices.traits.OnOff", "action.devices.traits.Brightness"]
    )
    assert gva_device.get("willReportState") is True
    custom = gva_device.get("customData") or {}
    assert custom.get("groupID") == group_id
    assert custom.get("paramMap_OnOff") == "Power"
    assert custom.get("paramMap_Brightness") == "Brightness"

    custom_data = {
        "groupID": group_id,
        "paramMap_OnOff": "Power",
        "paramMap_Brightness": "Brightness",
    }

    def _normalize_request_id(resp):
        out = json.loads(json.dumps(resp))
        out["requestId"] = "mock_request_id"
        return out

    def _expect_execute_onoff(device_id_local, on_value: bool):
        return {
            "requestId": "mock_request_id",
            "payload": {
                "commands": [
                    {
                        "ids": [device_id_local],
                        "status": "SUCCESS",
                        "states": {"on": on_value, "online": True},
                    }
                ]
            },
        }

    def _expect_execute_brightness(device_id_local, brightness_value: int):
        return {
            "requestId": "mock_request_id",
            "payload": {
                "commands": [
                    {
                        "ids": [device_id_local],
                        "status": "SUCCESS",
                        "states": {"brightness": brightness_value, "online": True},
                    }
                ]
            },
        }

    def _expect_query(device_id_local, on_value: bool, brightness_value: int):
        return {
            "requestId": "mock_request_id",
            "payload": {
                "devices": {
                    device_id_local: {
                        "online": True,
                        "status": "SUCCESS",
                        "on": on_value,
                        "brightness": brightness_value,
                    }
                }
            },
        }

    assert node_host_ctrl.update_param("Light", "Power", False), (
        "Failed to set Power False"
    )
    assert node_host_ctrl.update_param("Light", "Brightness", 0), (
        "Failed to set Brightness 0"
    )
    (node_host_ctrl.wait_on_state_reported(5000),)  # Clear stale state

    def set_on_off(on_value: bool):
        node_host_ctrl.clear_on_state_reported()
        ctrl = user.gva_control_device(
            device_id=device_id,
            custom_data=custom_data,
            command="action.devices.commands.OnOff",
            params={"on": on_value},
        )
        assert _normalize_request_id(ctrl) == _expect_execute_onoff(device_id, on_value)
        assert node_host_ctrl.wait_on_state_reported(5000), (
            "Node did not report after GVA OnOff"
        )
        assert node_host_ctrl.get_param("Light", "Power").value is on_value

    def set_brightness(brightness_value: int):
        node_host_ctrl.clear_on_state_reported()
        ctrl = user.gva_control_device(
            device_id=device_id,
            custom_data=custom_data,
            command="action.devices.commands.BrightnessAbsolute",
            params={"brightness": brightness_value},
        )
        assert _normalize_request_id(ctrl) == _expect_execute_brightness(
            device_id, brightness_value
        )
        assert node_host_ctrl.wait_on_state_reported(5000), (
            "Node did not report after GVA BrightnessAbsolute"
        )
        assert node_host_ctrl.get_param("Light", "Brightness").value == brightness_value

    def query_state(expected_on: bool, expected_brightness: int):
        node_host_ctrl.clear_on_state_reported()
        assert node_host_ctrl.update_param("Light", "Power", expected_on), (
            f"Failed to set Power {expected_on} for query"
        )
        assert node_host_ctrl.update_param(
            "Light", "Brightness", expected_brightness
        ), f"Failed to set Brightness {expected_brightness} for query"
        assert node_host_ctrl.wait_on_state_reported(5000), (
            "Node did not report before QUERY"
        )
        query = user.gva_query_device(device_id=device_id, custom_data=custom_data)
        assert _normalize_request_id(query) == _expect_query(
            device_id, expected_on, expected_brightness
        )

    set_on_off(True)
    set_on_off(False)
    set_brightness(75)
    set_brightness(25)
    query_state(True, 50)
    query_state(False, 30)


@pytest.fixture(scope="session")
def local_ctrl_controller():
    """
    A fixture that returns a LocalController instance.
    """
    local_test_controller = LocalController(logging=True)
    yield local_test_controller
    local_test_controller.close_sessions()


def _establish_session_with_retries(
    local_ctrl_controller,
    thing_name,
    pop_retrieval_fn,
    *,
    capability=CAPABILITY_LOCAL_CTRL,
    max_attempts=5,
    retry_delay_sec=2,
    username_retrieval_fn=None,
):
    """
    Call establish_session up to max_attempts times with a short delay between failures.
    """
    for attempt in range(1, max_attempts + 1):
        if local_ctrl_controller.establish_session(
            thing_name,
            pop_retrieval_fn,
            capability=capability,
            username_retrieval_fn=username_retrieval_fn,
        ):
            return True
        if attempt < max_attempts:
            sleep(retry_delay_sec)
    return False


def _make_local_ctrl_cred_fns(user, thing_name, group_id):
    """
    Build (pop_retrieval_fn, username_retrieval_fn) closures sharing a single shadow read.

    Each function triggers a fresh shadow read on first call within its own closure scope;
    callers may invoke them independently. Username retrieval supports SEC2; PoP supports
    SEC1 and SEC2.
    """

    def _read_local_ctrl_field(field_name):
        shadow_name = f"params-{group_id}"
        assert user.read_shadow(thing_name=thing_name, shadow_name=shadow_name), (
            f"Failed to request shadow for {thing_name}"
        )
        shadow_data = user.read_shadow_queue(timeout=10)
        assert shadow_data is not None, f"No shadow response received for {thing_name}"
        try:
            reported = shadow_data.get("state", {}).get("reported", {})
            local_ctrl = reported.get("Local Control", {})
            if isinstance(local_ctrl, dict) and field_name in local_ctrl:
                return local_ctrl[field_name]
            return None
        except Exception:
            return None

    def _pop_retrieval_fn():
        return _read_local_ctrl_field("POP")

    def _username_retrieval_fn():
        return _read_local_ctrl_field("Username")

    return _pop_retrieval_fn, _username_retrieval_fn


@pytest.mark.firmware
def test_firmware_local_control(
    associated_user1_node_host_ctrl_local_ctrl_with_user1_connected,
    local_ctrl_controller,
):
    """
    Test the firmware local control functionality.

    Parametrized over the SEC1 and SEC2 local control build variants; the LocalController
    auto-detects the security version via the version endpoint and runs the matching
    handshake (PoP for SEC1, PoP + SRP6a username for SEC2).

    This test verifies the complete local control workflow:
    1. Configure node with varied parameter types and local_ctrl service
    2. Establish local control session via AppSim
    3. Get node config via local control and verify correctness
    4. Get parameter state via local control and verify correctness
    5. Set randomized parameters via local control
    6. Verify changes are reflected in local state and named shadow
    """

    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_local_ctrl_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    print("🔄 Starting firmware local control test...")

    # PART 1: CONFIGURE NODE WITH VARIED PARAMETERS AND LOCAL CONTROL SERVICE
    print("1. Configuring node with varied parameters and local_ctrl service...")

    node_config = {
        "devices": [
            {
                "id": "light",
                "type": "esp.device.light",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": True,
                        "properties": ["read", "write"],
                    },
                    {
                        "id": "Brightness",
                        "type": "esp.param.brightness",
                        "data_type": "int",
                        "value": 80,
                        "bounds": {"min": 0, "max": 100, "step": 1},
                        "properties": ["read", "write"],
                    },
                ],
            },
            {
                "id": "sensor",
                "type": "esp.device.temperature",
                "params": [
                    {
                        "id": "Temperature",
                        "type": "esp.param.temperature",
                        "data_type": "float",
                        "value": 25.5,
                        "bounds": {"min": -10.0, "max": 50.0, "step": 0.1},
                        "properties": ["read", "write"],
                    }
                ],
            },
            {
                "id": "switch",
                "type": "esp.device.switch",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": False,
                        "properties": ["read", "write"],
                    }
                ],
            },
        ],
        "services": ["local_ctrl"],
    }

    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # PART 2: ESTABLISH LOCAL CONTROL SESSION VIA APPSIM
    print("2. Establishing local control session...")

    # Establish local control session
    _pop_retrieval_fn, _username_retrieval_fn = _make_local_ctrl_cred_fns(
        user, thing_name, group_id
    )

    def establish_local_ctrl_session():
        assert _establish_session_with_retries(
            local_ctrl_controller,
            thing_name,
            _pop_retrieval_fn,
            username_retrieval_fn=_username_retrieval_fn,
        ), (
            "Failed to establish local control session, check that host is also connected to the same network"
        )

    operate_with_named_shadow_connection(
        establish_local_ctrl_session, user, thing_name, group_id
    )

    # PART 3: GET NODE CONFIG VIA LOCAL CONTROL
    print("3. Getting node config via local control...")

    config_response = local_ctrl_controller.get_node_config(thing_name)
    assert config_response is not None, "Failed to get node config via local control"
    assert config_response.status == Status.Success, (
        f"Config get failed with status: {config_response.status}"
    )

    # Parse and verify config structure
    try:
        config_data = json.loads(config_response.value.decode("utf-8")).get("config")
        assert config_data is not None, "Config data is None"
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        assert False, f"Failed to parse config JSON: {e}"

    _verify_uploaded_node_config(config_data, node_config)

    print("✅ Node config retrieved and verified via local control")

    # PART 4: GET PARAMETER STATE VIA LOCAL CONTROL
    print("4. Getting parameter state via local control...")

    params_response = local_ctrl_controller.get_node_params(thing_name)
    assert params_response is not None, "Failed to get node params via local control"
    assert params_response.status == Status.Success, (
        f"Params get failed with status: {params_response.status}"
    )

    # Parse and verify params structure
    try:
        params_data = json.loads(params_response.value.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        assert False, f"Failed to parse params JSON: {e}"

    # Verify parameter values match initial config
    assert "light" in params_data, "Params should contain light device"
    assert "sensor" in params_data, "Params should contain sensor device"
    assert "switch" in params_data, "Params should contain switch device"

    # Check specific parameter values
    read_light_power = params_data["light"]["Power"]
    read_light_brightness = params_data["light"]["Brightness"]
    read_sensor_temperature = params_data["sensor"]["Temperature"]
    read_switch_power = params_data["switch"]["Power"]
    expected_light_power = node_host_ctrl.get_param("light", "Power").value
    expected_light_brightness = node_host_ctrl.get_param("light", "Brightness").value
    expected_sensor_temperature = node_host_ctrl.get_param(
        "sensor", "Temperature"
    ).value
    expected_switch_power = node_host_ctrl.get_param("switch", "Power").value
    assert read_light_power == expected_light_power, (
        f"Light Power should be {expected_light_power}, got {read_light_power}"
    )
    assert read_light_brightness == expected_light_brightness, (
        f"Light Brightness should be {expected_light_brightness}, got {read_light_brightness}"
    )
    assert abs(read_sensor_temperature - expected_sensor_temperature) < 0.001, (
        f"Sensor Temperature should be {expected_sensor_temperature}, got {read_sensor_temperature}"
    )
    assert read_switch_power == expected_switch_power, (
        f"Switch Power should be {expected_switch_power}, got {read_switch_power}"
    )

    print("✅ Parameter state retrieved and verified via local control")

    # PART 5: SET RANDOMIZED PARAMETERS VIA LOCAL CONTROL
    print("5. Setting randomized parameters via local control...")

    # Create expected node config with randomized values
    _randomize_config(node_config)

    # Convert to JSON string for local control set command
    param_json = _get_param_update_payload(node_config)

    # Clear state reported flag
    node_host_ctrl.clear_on_state_reported()

    # Use user to set parameters
    assert local_ctrl_controller.set_node_params(thing_name, param_json), (
        "Failed to set node params via local control"
    )

    # Wait for parameter updates to be processed
    assert node_host_ctrl.wait_on_state_reported(5000), (
        "Node did not report state after local control parameter update"
    )

    # PART 6: VERIFY CHANGES ACROSS ALL SOURCES
    print(
        "6. Verifying changes across local state, named shadow, and indexed shadow..."
    )

    # 6a: Verify local node state
    print("   - Verifying local node state...")
    _verify_config_with_node_state(node_host_ctrl, node_config)

    # 6b: Verify named shadow
    print("   - Verifying named shadow...")

    def verify_named_shadow():
        named_shadow_reported = read_named_shadow(user, thing_name, group_id)
        _verify_config_with_named_shadow_reported(node_config, named_shadow_reported)

    operate_with_named_shadow_connection(
        verify_named_shadow, user, thing_name, group_id
    )

    print("✅ All parameter changes verified across local state and named shadow")

    print("🎉 Firmware local control test completed successfully!")
    print("📋 Test Summary:")
    print("   - Node configuration with varied parameters and local_ctrl service ✅")
    print("   - Local control session establishment ✅")
    print("   - Node config retrieval via local control ✅")
    print("   - Parameter state retrieval via local control ✅")
    print("   - Randomized parameter setting via local control ✅")
    print("   - Verification across local state and named shadow ✅")


def test_firmware_local_control_chal_resp(
    associated_user1_node_host_ctrl_lc_chal_resp_with_user1_connected,
    local_ctrl_controller,
):
    """
    Test the firmware local control functionality with challenge-response.

    Parametrized over the SEC1 and SEC2 local control build variants; the LocalController
    auto-detects the security version and runs the matching handshake.

    This test verifies the complete local control challenge-response workflow:
    1. Configure node with local_ctrl service
    2. Establish local control session via AppSim
    3. Send challenge-response command via local control, and verify using node host_ctrl
    4. Disable challenge-response service, and verify it is disabled
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_lc_chal_resp_with_user1_connected
    )
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    print("🔄 Starting firmware local control challenge-response test...")

    # PART 1: CONFIGURE NODE WITH VARIED PARAMETERS AND LOCAL CONTROL SERVICE
    print("1. Configuring node with local_ctrl service...")
    node_config = {
        "services": ["local_ctrl"],
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Start the node
    start_node_host_ctrl(node_host_ctrl)

    # PART 2: ESTABLISH LOCAL CONTROL SESSION VIA APPSIM
    print("2. Establishing local control session...")

    # Establish local control session
    _pop_retrieval_fn, _username_retrieval_fn = _make_local_ctrl_cred_fns(
        user, thing_name, group_id
    )

    def establish_local_ctrl_session():
        assert _establish_session_with_retries(
            local_ctrl_controller,
            thing_name,
            _pop_retrieval_fn,
            username_retrieval_fn=_username_retrieval_fn,
        ), (
            "Failed to establish local control session, check that host is also connected to the same network"
        )

    operate_with_named_shadow_connection(
        establish_local_ctrl_session, user, thing_name, group_id
    )

    # PART 3: SEND CHALLENGE-RESPONSE COMMAND VIA LOCAL CONTROL
    print("3. Sending challenge-response command via local control...")

    # Send challenge-response command
    challenge_payload_str = "hello"
    challenge_payload_bytes = challenge_payload_str.encode("utf-8")
    response = local_ctrl_controller.challenge_response(
        thing_name,
        challenge_payload_bytes,
        username_retrieval_fn=_username_retrieval_fn,
    )
    assert response is not None, (
        "Failed to send challenge-response command via local control"
    )
    response_bytes = response.payload

    # Verify using instance credentials
    assert node_host_ctrl.verify_signature(challenge_payload_str, response_bytes), (
        "Challenge-response signature verification failed"
    )

    # PART 4: DISABLE CHALLENGE-RESPONSE SERVICE
    print("4. Disabling challenge-response service...")
    assert local_ctrl_controller.disable_chal_resp(thing_name), (
        "Failed to disable challenge-response service via local control"
    )

    # Verify challenge-response reports disabled (protocol status, not absence of reply)
    response = local_ctrl_controller.challenge_response(
        thing_name,
        challenge_payload_bytes,
        username_retrieval_fn=_username_retrieval_fn,
    )
    assert response is not None, (
        "Expected challenge-response reply after disabling challenge-response service"
    )
    assert response.status == ChalRespStatus.Disabled, (
        "Challenge-response should report Disabled after disabling service"
    )

    print(
        "✅ Challenge-response command sent and verified via local control and node host_ctrl"
    )

    print("🎉 Firmware local control challenge-response test completed successfully!")
    print("📋 Test Summary:")
    print("   - Node configuration with local_ctrl service ✅")
    print("   - Local control session establishment ✅")
    print("   - Challenge-response command sending and verification ✅")
    print("   - Challenge-response service disabling and verification ✅")


@pytest.mark.firmware
def test_firmware_on_network_chal_resp(
    node_host_ctrl_on_chal_resp, local_ctrl_controller
):
    """
    On-network challenge-response over mDNS/HTTP (on_network_chal_resp service).

    Same protocol path as local-control challenge-response, but:
    - Node enables only the ch_resp endpoint set (no local control endpoints), so the
      single ``_esp_rmaker_ctrl`` service advertises ``cap=["ch_resp"]``.
    - There is no MQTT user or named shadow yet, so the PoP cannot come from the cloud.
      A real device carries one from manufacturing data (printed on it); the test pushes
      a known PoP over host_ctrl to stand in for that.
    """
    node_host_ctrl = node_host_ctrl_on_chal_resp
    thing_name = node_host_ctrl.node_thing_name
    assert thing_name is not None, "Node did not receive thing name"

    print("🔄 Starting firmware on-network challenge-response test...")

    # Stand-in for the manufacturing PoP a real device carries (printed on it, so the
    # client already has it).
    #
    # Must be pushed before the service is configured, not just before the node is started:
    # adding the on_network_chal_resp service enables the local endpoints instance, and the
    # PoP is read once while that instance starts. esp_rmaker_local_ctrl_set_pop() therefore
    # refuses once it is running, and the SRP6a salt/verifier are already derived from
    # whatever PoP was in NVS.
    print("1. Setting a known local control PoP over host_ctrl...")
    assert node_host_ctrl.set_local_ctrl_pop(ON_NETWORK_CHAL_RESP_POP), (
        "Failed to set local control PoP"
    )

    print("2. Configuring node with on_network_chal_resp service...")
    node_config = {
        "services": ["on_network_chal_resp"],
    }
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    print("3. Starting node...")
    start_node_host_ctrl(node_host_ctrl)

    print("4. Sending challenge-response command via on-network endpoint...")

    # Establish session with retries. The node runs the default security version; both
    # SEC1-with-PoP and SEC2 need the PoP, and SEC2 additionally needs the SRP6a username.
    def _pop_retrieval_fn():
        return ON_NETWORK_CHAL_RESP_POP

    def _username_retrieval_fn():
        return LOCAL_CTRL_SEC2_USERNAME

    assert _establish_session_with_retries(
        local_ctrl_controller,
        thing_name,
        _pop_retrieval_fn,
        capability=CAPABILITY_CHAL_RESP,
        username_retrieval_fn=_username_retrieval_fn,
    ), (
        "Failed to establish local control session, check that host is also connected to the same network"
    )

    # Send challenge-response command
    challenge_payload_str = "hello"
    challenge_payload_bytes = challenge_payload_str.encode("utf-8")
    response = local_ctrl_controller.challenge_response(
        thing_name, challenge_payload_bytes
    )
    assert response is not None, (
        "Failed to send challenge-response command via on-network service"
    )
    response_bytes = response.payload

    assert node_host_ctrl.verify_signature(challenge_payload_str, response_bytes), (
        "Challenge-response signature verification failed"
    )

    print("5. Disabling on-network challenge-response service...")
    assert local_ctrl_controller.disable_chal_resp(thing_name), (
        "Failed to disable challenge-response service via on-network endpoint"
    )

    # On-network challenge-response service tears down the entire service and server after a delay
    # Wait for the service to be disabled
    sleep(10)
    response = local_ctrl_controller.challenge_response(
        thing_name, challenge_payload_bytes
    )
    assert response is None, "Expected challenge-response reply after disabling service"

    print(
        "✅ On-network challenge-response verified via LocalController and node host_ctrl"
    )
    print("🎉 Firmware on-network challenge-response test completed successfully!")
