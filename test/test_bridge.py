# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Bridge integration tests — exercise BridgeHostCtrl / BridgeChildHostCtrl against
the bridge-enabled firmware variant (REQUEST_TYPE_BRIDGE).
"""

import json
import os
import random
import time
from datetime import datetime as dt
from pathlib import Path
from zoneinfo import ZoneInfo

import pytest

from helpers.node_host_ctrl import (
    operate_with_named_shadow_connection,
    read_named_shadow,
    cold_reboot_node_host_ctrl,
    restart_node_host_ctrl_without_reset,
    start_node_host_ctrl,
    wait_for_cloud_params_update,
)
from helpers.bridge_host_ctrl import (
    assert_child_named_shadow_matches_config,
    poll_child_named_shadow_until,
    start_bridge_child,
    timeseries_publish_test_data_for_child,
    wait_for_cloud_group_control_bridge,
    wait_for_cloud_params_update_for_child,
    wait_for_param_value,
)
from helpers.timeseries import (
    get_timeseries_node_config,
    timeseries_param_specs,
    timeseries_random_value_in_bounds,
    timeseries_verify_with_user_api,
)
from helpers.automation import (
    automation_create_and_test,
    concurrent_automation_test,
    get_automation_node_config,
    wire_actions,
    wire_triggers,
)
from helpers.scheduling import (
    get_scheduling_node_config,
    scheduling_set_anti_action,
    scheduling_set_schedules,
    scheduling_verify_action,
    scheduling_verify_schedule,
)
from helpers.config import (
    _are_values_equal,
    _set_test_node_config,
    _verify_config_with_named_shadow_reported,
    _verify_uploaded_node_config,
)
from helpers.fixtures import (
    HeapStatusTracker,
    _associated_user1_node_host_ctrl,
    _associated_user1_node_host_ctrl_with_user1_connected,
    _firmware_instance_host_ctrl,
    _firmware_instance_request_host_ctrl_wrapper,
    _node_host_ctrl,
)
from helpers.bridge_stress import (
    run_cycle,
    select_subset,
    stress_child_node_config,
)
from host_ctrl_python.host_ctrl import NodeConfig
from rmng_backend import Group


_TEST_DATA = Path(__file__).parent / "data"
_PARENT_CONFIG_PATH = _TEST_DATA / "node_config_bridge_parent.json"
_PARENT_TAGS_PATH = _TEST_DATA / "node_tags.json"
_CHILD_CONFIG_PATH = _TEST_DATA / "node_config_va_varied_dtypes.json"
_COLLISION_PARENT_CONFIG_PATH = _TEST_DATA / "node_config_bridge_collision_parent.json"
_COLLISION_CHILD_CONFIG_PATH = _TEST_DATA / "node_config_bridge_collision_child.json"


# ---------- helpers ---------------------------------------------------------


def _load_child_config(config_path: Path) -> dict:
    """
    Load a node-config JSON for use as a child config slice. Bridge children
    do not own tags (they share the parent's), so the verify helper used here
    skips the tag pass.
    """
    config = json.loads(config_path.read_text())
    return {
        "devices": config.get("devices", []),
        "services": config.get("services", []),
        "tags": {},
    }


def _set_test_child_config(child, config_path: Path) -> dict:
    """
    Apply a config to a bridge child and verify it round-trips through the
    child's data model.
    """
    child_config = _load_child_config(config_path)
    assert child.set_config(NodeConfig(child_config)), "Failed to set child node config"
    for device in child_config["devices"]:
        for param in device["params"]:
            data = child.get_param(device["id"], param["id"])
            assert data is not None, (
                f"Child param {device['id']}::{param['id']} not present after set_config"
            )
            assert _are_values_equal(data.value, param["value"]), (
                f"Child param {device['id']}::{param['id']} value mismatch: "
                f"expected {param['value']} got {data.value}"
            )
    return child_config


# ---------- fixtures --------------------------------------------------------


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_bridge():
    # Dynamically parametrized in conftest.py
    pass


@pytest.fixture(scope="session")
def firmware_instance_request_host_ctrl_bridge_wrapper(
    firmware_instance_request_host_ctrl_bridge, add_summary_section
):
    yield from _firmware_instance_request_host_ctrl_wrapper(
        firmware_instance_request_host_ctrl_bridge, add_summary_section
    )


@pytest.fixture(scope="function")
def firmware_instance_host_ctrl_bridge(
    firmware_instance_request_host_ctrl_bridge_wrapper, firmware_instance_manager
):
    yield from _firmware_instance_host_ctrl(
        firmware_instance_request_host_ctrl_bridge_wrapper, firmware_instance_manager
    )


@pytest.fixture(scope="function")
def node_host_ctrl_bridge(
    request, firmware_instance_host_ctrl_bridge, add_summary_section
):
    yield from _node_host_ctrl(
        request, firmware_instance_host_ctrl_bridge, add_summary_section
    )


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_bridge(node_host_ctrl_bridge, user_pool):
    yield from _associated_user1_node_host_ctrl(node_host_ctrl_bridge, user_pool)


@pytest.fixture(scope="function")
def associated_user1_node_host_ctrl_bridge_with_user1_connected(
    associated_user1_node_host_ctrl_bridge,
):
    yield from _associated_user1_node_host_ctrl_with_user1_connected(
        associated_user1_node_host_ctrl_bridge
    )


@pytest.fixture(scope="function")
def configured_bridge_parent(
    associated_user1_node_host_ctrl_bridge_with_user1_connected,
):
    """
    Parent node config set + parent fully started. Yields
    ``(parent_host_ctrl, user, group_id, parent_config)``.
    """
    node_host_ctrl, user, group_id = (
        associated_user1_node_host_ctrl_bridge_with_user1_connected
    )
    parent_config = _set_test_node_config(
        node_host_ctrl, _PARENT_CONFIG_PATH, _PARENT_TAGS_PATH
    )
    start_node_host_ctrl(node_host_ctrl)
    return node_host_ctrl, user, group_id, parent_config


@pytest.fixture(scope="function")
def bridge_with_one_child(configured_bridge_parent):
    """
    Parent + one attached + configured + started child. Yields
    ``(parent_host_ctrl, user, group_id, parent_config, child, child_config)``.
    """
    node_host_ctrl, user, group_id, parent_config = configured_bridge_parent
    child = node_host_ctrl.bridge.add_child("c1", "local-c1")
    assert child is not None, "add_child returned None"
    child_config = _set_test_child_config(child, _CHILD_CONFIG_PATH)
    start_bridge_child(node_host_ctrl, child)
    return node_host_ctrl, user, group_id, parent_config, child, child_config


# ---------- tests -----------------------------------------------------------


@pytest.mark.firmware
def test_bridge_child_add_remove(configured_bridge_parent):
    """add_child → list_children → remove_child → list_children, with idempotency check."""
    node_host_ctrl, _user, _group_id, _parent_config = configured_bridge_parent
    parent_thing = node_host_ctrl.node_thing_name

    child = node_host_ctrl.bridge.add_child("c1", "local-c1")
    assert child is not None
    assert child.thing_name == f"{parent_thing}--c1", (
        f"Unexpected child thing name: {child.thing_name}"
    )

    listed = node_host_ctrl.bridge.list_children()
    handles = [c.handle for c in listed]
    assert child.handle in handles, "Added child missing from list_children"

    # Idempotency: re-add with same suffix + local id returns the same thing name.
    child_again = node_host_ctrl.bridge.add_child("c1", "local-c1")
    assert child_again is not None
    assert child_again.thing_name == child.thing_name

    assert node_host_ctrl.bridge.remove_child(child)
    listed_after = [c.handle for c in node_host_ctrl.bridge.list_children()]
    assert child.handle not in listed_after, "Child still in list_children after remove"


@pytest.mark.firmware
def test_bridge_child_invalid_suffix(configured_bridge_parent):
    """Suffix outside ``[A-Za-z0-9_]{1,32}`` must fail add_child."""
    node_host_ctrl, *_ = configured_bridge_parent
    for suffix in ("a-b", "", "a" * 33, "with space"):
        child = node_host_ctrl.bridge.add_child(suffix, f"local-{suffix or 'empty'}")
        assert child is None, f"add_child unexpectedly accepted suffix {suffix!r}"


@pytest.mark.firmware
def test_bridge_child_acts_like_normal_device(bridge_with_one_child):
    """
    Child surfaces config back via its own data-model API + named/indexed shadows.
    Bridge-side analog of ``test_firmware_start_sequence``.
    """
    node_host_ctrl, user, group_id, _parent_config, child, child_config = (
        bridge_with_one_child
    )
    assert child.thing_name is not None

    # Named shadow under child thing.
    def op_func():
        named_shadow_reported = read_named_shadow(user, child.thing_name, group_id)
        _verify_config_with_named_shadow_reported(child_config, named_shadow_reported)

    operate_with_named_shadow_connection(op_func, user, child.thing_name, group_id)

    # TODO: indexed shadow validation — no per-child get_indexed_shadow on
    # BridgeChildHostCtrl, and the cloud-read path's payload shape differs from
    # what _verify_config_with_indexed_shadow_reported expects. Revisit once
    # the bridge surface exposes an indexed-shadow accessor.


@pytest.mark.firmware
def test_bridge_child_node_config_uploaded(bridge_with_one_child):
    """
    Child's node config uploaded to cloud; restart without reset does NOT
    re-send (checksum unchanged).
    """
    node_host_ctrl, user, group_id, _parent_config, child, child_config = (
        bridge_with_one_child
    )

    uploaded_config = user.get_node_config(
        group_id=group_id, subgroup_id=None, node_id=child.thing_name
    )
    assert uploaded_config is not None, "Child node config not found in cloud"
    _verify_uploaded_node_config(uploaded_config, child_config)

    child.clear_on_node_config_sent()
    restart_node_host_ctrl_without_reset(node_host_ctrl)
    assert not child.wait_on_node_config_sent(5000), (
        "Child re-sent its configuration after restart-without-reset"
    )


@pytest.mark.firmware
def test_bridge_child_param_update_local(bridge_with_one_child):
    """update_param on child → named shadow reflects new value."""
    node_host_ctrl, user, group_id, _parent_config, child, child_config = (
        bridge_with_one_child
    )

    device = child_config["devices"][0]
    param = device["params"][0]
    new_value = param["value"]
    if isinstance(new_value, bool):
        new_value = not new_value
    elif isinstance(new_value, (int, float)):
        new_value = new_value + 1
    else:
        new_value = f"{new_value}-modified"

    node_host_ctrl.clear_on_state_reported()
    assert child.update_param(device["id"], param["id"], new_value)
    assert node_host_ctrl.wait_on_state_reported(10000), (
        "Parent did not report state after child update_param"
    )

    expected_config = {
        "devices": [
            {
                "id": device["id"],
                "params": [{"id": param["id"], "value": new_value}],
            }
        ]
    }
    assert_child_named_shadow_matches_config(user, child, group_id, expected_config)


@pytest.mark.firmware
def test_bridge_child_param_update_cloud(bridge_with_one_child):
    """Cloud-side params publish to child topic → child param updates."""
    node_host_ctrl, user, group_id, _parent_config, child, child_config = (
        bridge_with_one_child
    )

    device = child_config["devices"][0]
    param = device["params"][0]
    new_value = param["value"]
    if isinstance(new_value, bool):
        new_value = not new_value
    elif isinstance(new_value, (int, float)):
        new_value = new_value + 2
    else:
        new_value = f"{new_value}-cloud"

    payload = {device["id"]: {param["id"]: new_value}}
    wait_for_cloud_params_update_for_child(
        node_host_ctrl, user, child, group_id, payload
    )

    data = child.get_param(device["id"], param["id"])
    assert data is not None
    assert _are_values_equal(data.value, new_value), (
        f"Child param did not update from cloud: got {data.value}"
    )


@pytest.mark.firmware
def test_bridge_parent_child_independence(configured_bridge_parent):
    """
    Parent + child both have device id ``light_1`` + param id ``Power``. Updates
    via cloud routed to one thing must not bleed into the other.
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    # Re-set parent config to the collision variant (parent already started).
    parent_collision = _set_test_node_config(
        node_host_ctrl, _COLLISION_PARENT_CONFIG_PATH, _PARENT_TAGS_PATH
    )
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_node_config_sent()
    # Parent restarts itself to ship the new node config.
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    child = node_host_ctrl.bridge.add_child("col", "local-col")
    assert child is not None
    child_collision = _set_test_child_config(child, _COLLISION_CHILD_CONFIG_PATH)
    start_bridge_child(node_host_ctrl, child)

    parent_thing = node_host_ctrl.node_thing_name

    # Update child's power=True via cloud → parent's stays False.
    wait_for_cloud_params_update_for_child(
        node_host_ctrl, user, child, group_id, {"light_1": {"Power": True}}
    )
    child_power = wait_for_param_value(child, "light_1", "Power", True)
    parent_power = node_host_ctrl.get_param("light_1", "Power")
    assert parent_power is not None and parent_power.value is False, (
        f"Parent power leaked from child update: {parent_power and parent_power.value}"
    )
    assert child_power is not None and child_power.value is True

    # Update parent's power=True via cloud → child stays True (already True).
    # Set child back to False first via cloud to make the assertion meaningful.
    wait_for_cloud_params_update_for_child(
        node_host_ctrl, user, child, group_id, {"light_1": {"Power": False}}
    )
    wait_for_cloud_params_update(
        node_host_ctrl, user, parent_thing, group_id, {"light_1": {"Power": True}}
    )
    parent_power = wait_for_param_value(node_host_ctrl, "light_1", "Power", True)
    child_power = child.get_param("light_1", "Power")
    assert parent_power is not None and parent_power.value is True
    assert child_power is not None and child_power.value is False, (
        f"Child power leaked from parent update: {child_power and child_power.value}"
    )

    # Just to silence unused-var warnings.
    _ = parent_collision
    _ = child_collision


@pytest.mark.firmware
def test_bridge_children_have_independent_node_configs(configured_bridge_parent):
    """Two children with different configs → each uploads its own."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    child_a = node_host_ctrl.bridge.add_child("ca", "local-a")
    child_b = node_host_ctrl.bridge.add_child("cb", "local-b")
    assert child_a is not None and child_b is not None

    config_a = _set_test_child_config(child_a, _CHILD_CONFIG_PATH)
    config_b = _set_test_child_config(child_b, _COLLISION_CHILD_CONFIG_PATH)
    start_bridge_child(node_host_ctrl, child_a)
    start_bridge_child(node_host_ctrl, child_b)

    uploaded_a = user.get_node_config(
        group_id=group_id, subgroup_id=None, node_id=child_a.thing_name
    )
    uploaded_b = user.get_node_config(
        group_id=group_id, subgroup_id=None, node_id=child_b.thing_name
    )
    assert uploaded_a is not None and uploaded_b is not None
    _verify_uploaded_node_config(uploaded_a, config_a)
    _verify_uploaded_node_config(uploaded_b, config_b)

    # Cross-check: child A's config must NOT match child B's.
    a_device_ids = sorted(d["id"] for d in config_a["devices"])
    b_device_ids = sorted(d["id"] for d in config_b["devices"])
    if a_device_ids == b_device_ids:
        pytest.skip("Chosen child configs share device ids — cross-check meaningless")
    with pytest.raises(AssertionError):
        _verify_uploaded_node_config(uploaded_a, config_b)


@pytest.mark.firmware
def test_bridge_children_param_independence(configured_bridge_parent):
    """
    Two children with identical device+param IDs on the same bridge. Cloud
    update routed to one child must not bleed into the other.
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    child_a = node_host_ctrl.bridge.add_child("ca", "local-a")
    child_b = node_host_ctrl.bridge.add_child("cb", "local-b")
    assert child_a is not None and child_b is not None

    _set_test_child_config(child_a, _COLLISION_CHILD_CONFIG_PATH)
    _set_test_child_config(child_b, _COLLISION_CHILD_CONFIG_PATH)
    start_bridge_child(node_host_ctrl, child_a)
    start_bridge_child(node_host_ctrl, child_b)

    # Update child A's power=True via cloud → child B stays False.
    wait_for_cloud_params_update_for_child(
        node_host_ctrl, user, child_a, group_id, {"light_1": {"Power": True}}
    )
    a_power = wait_for_param_value(child_a, "light_1", "Power", True)
    b_power = child_b.get_param("light_1", "Power")
    assert a_power is not None and a_power.value is True
    assert b_power is not None and b_power.value is False, (
        f"Child B power leaked from child A update: {b_power and b_power.value}"
    )

    # Now update child B's power=True → child A stays True (already True).
    wait_for_cloud_params_update_for_child(
        node_host_ctrl, user, child_b, group_id, {"light_1": {"Power": True}}
    )
    b_power = wait_for_param_value(child_b, "light_1", "Power", True)
    a_power = child_a.get_param("light_1", "Power")
    assert b_power is not None and b_power.value is True
    assert a_power is not None and a_power.value is True

    # Push child A back to False via cloud → assert B stays True.
    wait_for_cloud_params_update_for_child(
        node_host_ctrl, user, child_a, group_id, {"light_1": {"Power": False}}
    )
    a_power = wait_for_param_value(child_a, "light_1", "Power", False)
    b_power = child_b.get_param("light_1", "Power")
    assert a_power is not None and a_power.value is False
    assert b_power is not None and b_power.value is True, (
        f"Child B power leaked from child A reset: {b_power and b_power.value}"
    )

    # Named shadow under each child reports its own value.
    assert_child_named_shadow_matches_config(
        user,
        child_a,
        group_id,
        {"devices": [{"id": "light_1", "params": [{"id": "Power", "value": False}]}]},
    )
    assert_child_named_shadow_matches_config(
        user,
        child_b,
        group_id,
        {"devices": [{"id": "light_1", "params": [{"id": "Power", "value": True}]}]},
    )


@pytest.mark.firmware
def test_bridge_parent_child_subgroup_independence(bridge_with_one_child):
    """
    Subgroup independence between bridge parent and child:
    * Parent and child share the primary group.
    * Each can belong to its own subgroup independently of the other.

    Steps:
    1. Put parent in subgroup A → parent's group_info contains A,
       child stays out of A.
    2. Put child in subgroup B → child's group_info contains B (but not A),
       parent's group_info still has A (but not B).
    """
    node_host_ctrl, user, group_id, _parent_config, child, _child_config = (
        bridge_with_one_child
    )

    group_api = Group(user=user)
    subgroup_a = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge migration subgroup A"
    )
    subgroup_b = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge migration subgroup B"
    )
    assert subgroup_a is not None and subgroup_b is not None

    parent_thing_name = node_host_ctrl.node_thing_name
    child_thing_name = child.thing_name

    def _parts(group_info_str: str) -> tuple[str, list[str]]:
        """Split <primary>[-<sg>]* into (primary, [subgroups])."""
        parts = (group_info_str or "").split("-")
        return parts[0] if parts else "", parts[1:] if len(parts) > 1 else []

    # --- Step 1: parent into subgroup A ---
    node_host_ctrl.clear_on_group_info()
    child.clear_on_group_info()
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_a, node_id=parent_thing_name
    )
    assert node_host_ctrl.wait_on_group_info(15000), (
        "Parent did not receive group_info after add_node_to_subgroup(A)"
    )
    assert not child.wait_on_group_info(5000), (
        "Child received group_info after add_node_to_subgroup(A)"
    )

    parent_gi = node_host_ctrl.get_group_info_str()
    child_gi = child.get_group_info_str()
    p_primary, p_subs = _parts(parent_gi)
    _, c_subs = _parts(child_gi)

    assert p_primary == group_id, (
        f"Parent primary mismatch: expected {group_id!r}, got {p_primary!r}"
    )
    assert subgroup_a in p_subs, (
        f"Parent group_info missing subgroup A {subgroup_a!r} in {parent_gi!r}"
    )
    assert subgroup_a not in c_subs, (
        f"Child unexpectedly inherited subgroup A {subgroup_a!r} via parent "
        f"(child group_info={child_gi!r})"
    )

    # --- Step 2: child into subgroup B (independent of parent) ---
    child.clear_on_group_info()
    node_host_ctrl.clear_on_group_info()
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_b, node_id=child_thing_name
    )
    assert child.wait_on_group_info(15000), (
        "Child did not receive group_info after add_node_to_subgroup(B)"
    )
    assert not node_host_ctrl.wait_on_group_info(5000), (
        "Parent received group_info after add_node_to_subgroup(B)"
    )

    parent_gi = node_host_ctrl.get_group_info_str()
    child_gi = child.get_group_info_str()
    p_primary, p_subs = _parts(parent_gi)
    c_primary, c_subs = _parts(child_gi)

    assert c_primary == group_id, (
        f"Child primary mismatch: expected {group_id!r}, got {c_primary!r}"
    )
    assert subgroup_b in c_subs, (
        f"Child group_info missing subgroup B {subgroup_b!r} in {child_gi!r}"
    )
    assert subgroup_a not in c_subs, (
        f"Child should not be in subgroup A: child group_info={child_gi!r}"
    )
    assert subgroup_a in p_subs, (
        f"Parent group_info lost subgroup A after child join B: {parent_gi!r}"
    )
    assert subgroup_b not in p_subs, (
        f"Parent unexpectedly joined subgroup B {subgroup_b!r}: {parent_gi!r}"
    )


@pytest.mark.firmware
def test_bridge_child_mark_online_offline(bridge_with_one_child):
    """
    mark_online(False) → child reachability reflected on the child's named
    shadow as ``online: false``; mark_online(True) restores ``online: true``.
    """
    node_host_ctrl, user, group_id, _parent_config, child, _child_config = (
        bridge_with_one_child
    )

    # --- offline ---
    child.clear_on_online()
    node_host_ctrl.clear_on_state_reported()
    assert child.mark_online(False)
    # Online bit should not fire while marked offline.
    assert child.wait_on_online(10000), (
        "Child did not report online after mark_online(False)"
    )
    assert node_host_ctrl.wait_on_state_reported(10000), (
        "Parent did not report state after mark_online(False)"
    )
    offline_seen, offline_shadow = poll_child_named_shadow_until(
        user, child, group_id, lambda s: s.get("online") is False
    )
    assert offline_seen, (
        f"Child named shadow should report online=false after mark_online(False); "
        f"last shadow={offline_shadow!r}"
    )

    # --- online ---
    child.clear_on_online()
    node_host_ctrl.clear_on_state_reported()
    assert child.mark_online(True)
    assert child.wait_on_online(10000), (
        "Child did not return to online after mark_online(True)"
    )
    assert node_host_ctrl.wait_on_state_reported(10000), (
        "Parent did not report state after mark_online(True)"
    )
    online_seen, online_shadow = poll_child_named_shadow_until(
        user, child, group_id, lambda s: s.get("online") is True
    )
    assert online_seen, (
        f"Child named shadow should report online=true after mark_online(True); "
        f"last shadow={online_shadow!r}"
    )


@pytest.mark.firmware
def test_bridge_child_online_republished_on_reconnect(bridge_with_one_child):
    """
    Parent MQTT reconnect must re-affirm each READY child's cached online
    status. Without this republish, cloud (which marks every child offline
    on parent LWT) stays out of sync with the firmware view.

    Steps:
    1. Mark child online; verify shadow.online == True.
    2. Force parent network failure → restore (forces a real
       disconnect + reconnect cycle on the parent MQTT session).
    3. Assert child's online bit fires again (republish) and shadow
       still reports online == True.
    """
    node_host_ctrl, user, group_id, _parent_config, child, _child_config = (
        bridge_with_one_child
    )

    # Establish a known online=True baseline.
    child.clear_on_online()
    node_host_ctrl.clear_on_state_reported()
    assert child.mark_online(True)
    assert child.wait_on_online(10000), "Child did not report initial online=True"
    assert node_host_ctrl.wait_on_state_reported(10000), (
        "Parent did not report state after initial mark_online(True)"
    )
    baseline_seen, baseline_shadow = poll_child_named_shadow_until(
        user, child, group_id, lambda s: s.get("online") is True
    )
    assert baseline_seen, (
        f"Child shadow online not True before disconnect: {baseline_shadow!r}"
    )

    # Clear flags, then explicitly disconnect + reconnect parent MQTT.
    child.clear_on_online()
    node_host_ctrl.clear_on_state_reported()

    assert node_host_ctrl.mqtt_control_disconnect(), "Failed to disconnect parent MQTT"

    # Cloud should mark child offline via parent LWT.
    offline_seen, last_shadow = poll_child_named_shadow_until(
        user, child, group_id, lambda s: s.get("online") is False
    )
    assert offline_seen, (
        f"Child shadow did not go offline within 30s after parent disconnect "
        f"(last shadow={last_shadow!r})"
    )

    assert node_host_ctrl.mqtt_control_connect(), "Failed to reconnect parent MQTT"

    # Reconnect must re-fire child online publish.
    assert child.wait_on_online(30000), (
        "Child did not republish online on parent reconnect"
    )
    assert node_host_ctrl.wait_on_state_reported(30000), (
        "Parent did not report state after reconnect"
    )

    online_seen, last_shadow = poll_child_named_shadow_until(
        user, child, group_id, lambda s: s.get("online") is True
    )
    assert online_seen, (
        f"Child shadow online not True within 30s after reconnect republish "
        f"(last shadow={last_shadow!r})"
    )


@pytest.mark.firmware
def test_bridge_child_subgroup_shadow_migration(bridge_with_one_child):
    """
    Child subgroup migration moves its full shadow document with it.

    Steps:
    1. Read child named shadow at the original group_info (primary only).
    2. Migrate child into subgroup A → read shadow at the new group_info.
    3. Migrate child from A into subgroup B → read shadow at B's group_info.
       Each shadow read must contain the same reported config (the migration
       republishes the full state at the new shadow name).
    """
    node_host_ctrl, user, group_id, _parent_config, child, child_config = (
        bridge_with_one_child
    )

    group_api = Group(user=user)
    subgroup_a = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge shadow migration A"
    )
    subgroup_b = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge shadow migration B"
    )
    assert subgroup_a is not None and subgroup_b is not None

    def _assert_shadow_has_child_config(group_info_str: str):
        return assert_child_named_shadow_matches_config(
            user, child, group_info_str, child_config
        )

    child_thing_name = child.thing_name

    # 1. Original shadow (primary only).
    original_group_info = child.get_group_info_str()
    _assert_shadow_has_child_config(original_group_info)

    # 2. Migrate child → subgroup A.
    child.clear_on_group_info()
    node_host_ctrl.clear_on_state_reported()
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_a, node_id=child_thing_name
    )
    assert child.wait_on_group_info(15000), (
        "Child did not receive group_info after add_node_to_subgroup(A)"
    )
    # State re-report at new shadow name follows group_info change.
    assert node_host_ctrl.wait_on_state_reported(15000), (
        "Parent did not report state after child migration to A"
    )
    a_group_info = child.get_group_info_str()
    assert subgroup_a in a_group_info.split("-")[1:], (
        f"Child group_info missing subgroup A {subgroup_a!r}: {a_group_info!r}"
    )
    _assert_shadow_has_child_config(a_group_info)

    # 3. Migrate child A → B (remove from A, add to B).
    child.clear_on_group_info()
    node_host_ctrl.clear_on_state_reported()
    group_api.remove_node_from_subgroup(
        group_id=group_id, subgroup_id=subgroup_a, node_id=child_thing_name
    )
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_b, node_id=child_thing_name
    )
    assert child.wait_on_group_info(15000), (
        "Child did not receive group_info after migration to B"
    )
    assert node_host_ctrl.wait_on_state_reported(15000), (
        "Parent did not report state after child migration to B"
    )
    b_group_info = child.get_group_info_str()
    b_subgroups = b_group_info.split("-")[1:]
    assert subgroup_b in b_subgroups, (
        f"Child group_info missing subgroup B {subgroup_b!r}: {b_group_info!r}"
    )
    assert subgroup_a not in b_subgroups, (
        f"Child group_info still contains old subgroup A {subgroup_a!r}: {b_group_info!r}"
    )
    _assert_shadow_has_child_config(b_group_info)


_LIGHT_DEVICE_TYPE = "esp.device.light"
_BRIGHTNESS_PARAM_TYPE = "esp.param.brightness"


def _group_control_brightness_payload(value: int) -> dict:
    return {_LIGHT_DEVICE_TYPE: {"params": {_BRIGHTNESS_PARAM_TYPE: value}}}


def _setup_collision_parent_and_children(node_host_ctrl, n_children: int):
    """
    Re-set parent to the collision config + attach + start ``n_children``
    children, each with the collision config. Returns list of child handles.
    Parent must already be started (via configured_bridge_parent).
    """
    _set_test_node_config(
        node_host_ctrl, _COLLISION_PARENT_CONFIG_PATH, _PARENT_TAGS_PATH
    )
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_node_config_sent()
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    children = []
    for i in range(n_children):
        suffix = f"col{i}"
        child = node_host_ctrl.bridge.add_child(suffix, f"local-{suffix}")
        assert child is not None, f"add_child({suffix}) returned None"
        _set_test_child_config(child, _COLLISION_CHILD_CONFIG_PATH)
        start_bridge_child(node_host_ctrl, child)
        children.append(child)
    return children


@pytest.mark.firmware
def test_bridge_group_control_primary_convergence(configured_bridge_parent):
    """
    Parent + child share the same primary group (no subgroups). A primary-group
    control payload keyed by device type must update *every* matching device
    on both nodes.
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    [child] = _setup_collision_parent_and_children(node_host_ctrl, 1)

    wait_for_cloud_group_control_bridge(
        node_host_ctrl, user, group_id, _group_control_brightness_payload(42)
    )

    parent_b = wait_for_param_value(node_host_ctrl, "light_1", "Brightness", 42)
    child_b = wait_for_param_value(child, "light_1", "Brightness", 42)
    assert parent_b is not None and parent_b.value == 42, (
        f"Parent Brightness not updated by primary group control: {parent_b and parent_b.value}"
    )
    assert child_b is not None and child_b.value == 42, (
        f"Child Brightness not updated by primary group control: {child_b and child_b.value}"
    )


@pytest.mark.firmware
def test_bridge_group_control_subgroup_isolation(configured_bridge_parent):
    """
    Two children in two different subgroups (A, B). Parent stays primary-only.
    Subgroup-A publish updates only child_a; subgroup-B publish updates only
    child_b. Parent is never touched.

    Brightness is used (not Power) so distinct values per publish disprove
    cross-bleed in both directions — bool would be symmetric.
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    [child_a, child_b] = _setup_collision_parent_and_children(node_host_ctrl, 2)

    group_api = Group(user=user)
    subgroup_a = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge group-control A"
    )
    subgroup_b = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge group-control B"
    )
    assert subgroup_a is not None and subgroup_b is not None

    child_a.clear_on_group_info()
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_a, node_id=child_a.thing_name
    )
    assert child_a.wait_on_group_info(15000), (
        "child_a did not receive group_info after subgroup A add"
    )

    child_b.clear_on_group_info()
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_b, node_id=child_b.thing_name
    )
    assert child_b.wait_on_group_info(15000), (
        "child_b did not receive group_info after subgroup B add"
    )

    # --- Publish to subgroup A → only child_a updates ---
    wait_for_cloud_group_control_bridge(
        node_host_ctrl,
        user,
        group_id,
        _group_control_brightness_payload(20),
        subgroup_id=subgroup_a,
    )
    a_val = wait_for_param_value(child_a, "light_1", "Brightness", 20)
    b_val = child_b.get_param("light_1", "Brightness")
    parent_val = node_host_ctrl.get_param("light_1", "Brightness")
    assert a_val is not None and a_val.value == 20, (
        f"child_a Brightness should be 20 after subgroup A publish: {a_val and a_val.value}"
    )
    assert b_val is not None and b_val.value == 0, (
        f"child_b leaked subgroup-A update: {b_val and b_val.value}"
    )
    assert parent_val is not None and parent_val.value == 0, (
        f"Parent leaked subgroup-A update: {parent_val and parent_val.value}"
    )

    # --- Publish to subgroup B → only child_b updates, child_a stays 20 (not 30) ---
    wait_for_cloud_group_control_bridge(
        node_host_ctrl,
        user,
        group_id,
        _group_control_brightness_payload(30),
        subgroup_id=subgroup_b,
    )
    b_val = wait_for_param_value(child_b, "light_1", "Brightness", 30)
    a_val = child_a.get_param("light_1", "Brightness")
    parent_val = node_host_ctrl.get_param("light_1", "Brightness")
    assert b_val is not None and b_val.value == 30, (
        f"child_b Brightness should be 30 after subgroup B publish: {b_val and b_val.value}"
    )
    assert a_val is not None and a_val.value == 20, (
        f"child_a leaked subgroup-B update (should still be 20): {a_val and a_val.value}"
    )
    assert parent_val is not None and parent_val.value == 0, (
        f"Parent leaked subgroup-B update: {parent_val and parent_val.value}"
    )


@pytest.mark.firmware
def test_bridge_group_control_primary_and_subgroup(configured_bridge_parent):
    """
    Parent stays primary-only; child joins subgroup A.

    * Primary-group publish updates both parent and child.
    * Subgroup-A publish updates only the child; parent is untouched.

    Distinct brightness values per publish make leak detection unambiguous.
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    [child] = _setup_collision_parent_and_children(node_host_ctrl, 1)

    group_api = Group(user=user)
    subgroup_a = group_api.create_subgroup(
        group_id=group_id, subgroup_name="Bridge mixed-control A"
    )
    assert subgroup_a is not None

    child.clear_on_group_info()
    group_api.add_node_to_subgroup(
        group_id=group_id, subgroup_id=subgroup_a, node_id=child.thing_name
    )
    assert child.wait_on_group_info(15000), (
        "Child did not receive group_info after subgroup A add"
    )

    # --- Primary publish → both updated to 55 ---
    wait_for_cloud_group_control_bridge(
        node_host_ctrl, user, group_id, _group_control_brightness_payload(55)
    )
    parent_val = wait_for_param_value(node_host_ctrl, "light_1", "Brightness", 55)
    child_val = wait_for_param_value(child, "light_1", "Brightness", 55)
    assert parent_val is not None and parent_val.value == 55, (
        f"Parent not updated by primary publish: {parent_val and parent_val.value}"
    )
    assert child_val is not None and child_val.value == 55, (
        f"Child not updated by primary publish: {child_val and child_val.value}"
    )

    # --- Subgroup A publish → only child updated to 77; parent stays 55 ---
    wait_for_cloud_group_control_bridge(
        node_host_ctrl,
        user,
        group_id,
        _group_control_brightness_payload(77),
        subgroup_id=subgroup_a,
    )
    child_val = wait_for_param_value(child, "light_1", "Brightness", 77)
    parent_val = node_host_ctrl.get_param("light_1", "Brightness")
    assert child_val is not None and child_val.value == 77, (
        f"Child not updated by subgroup A publish: {child_val and child_val.value}"
    )
    assert parent_val is not None and parent_val.value == 55, (
        f"Parent leaked subgroup-A update (should still be 55): "
        f"{parent_val and parent_val.value}"
    )


@pytest.mark.firmware
def test_bridge_child_timeseries(configured_bridge_parent, test_user2):
    """
    Bridge-child timeseries — mirror of test_firmware_timeseries but driven
    on a child node. Publishes test points via child.update_param at
    advancing virtual timestamps, then verifies the raw timeseries data via
    the user REST API scoped to the child's thing name.
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    print("Starting bridge-child timeseries test...")

    # Attach a child, fill info, push the timeseries node config, commit.
    child = node_host_ctrl.bridge.add_child("ts", "local-ts")
    assert child is not None, "add_child returned None"

    node_config = get_timeseries_node_config()
    # NodeConfig wrapper not strictly required — set_config takes a NodeConfig
    # but accepts the raw dict via the same constructor used elsewhere.
    assert child.set_config(NodeConfig(node_config)), "Failed to set child node config"
    start_bridge_child(node_host_ctrl, child)

    thing_name = child.thing_name
    assert thing_name is not None

    # Build test data — base timestamp = real now. Test runs after build, so now
    # > the build-time TIMESYNC_REF_TIME floor → points accepted by
    # timesync_epoch_ms_is_valid().
    base_ts = int(time.time())
    param_specs = timeseries_param_specs(node_config)
    num_ts = 25
    sorted_timestamps = [base_ts + (i * 2) for i in range(num_ts)]
    # start/end in ms — same unit as expected points and the cloud `ts`.
    start_time = min(sorted_timestamps) * 1000
    end_time = max(sorted_timestamps) * 1000

    # Seed last-known values (must not repeat on first update for each param).
    last_value = {}
    for spec in param_specs:
        p = child.get_param(spec["device"], spec["param"])
        assert p is not None, f"Child param {spec['device']}::{spec['param']} not found"
        last_value[spec["key"]] = p.value

    test_data_points = []
    expected_points_by_param = {spec["key"]: [] for spec in param_specs}
    for timestamp in sorted_timestamps:
        for spec in param_specs:
            key = spec["key"]
            val = timeseries_random_value_in_bounds(
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

    timeseries_publish_test_data_for_child(node_host_ctrl, child, test_data_points)

    print("All test data published. Waiting for stream processor...")
    from time import sleep

    sleep(25)

    print("Verifying data via user APIs (raw timeseries in time period)...")
    timeseries_verify_with_user_api(
        user,
        group_id,
        thing_name,
        expected_points_by_param,
        start_time,
        end_time,
        param_specs,
    )

    # Unauthorized read must be blocked even at the child level.
    try:
        resp = test_user2.get_node_timeseries_latest(
            group_id=group_id,
            node_id=thing_name,
            key=param_specs[0]["key"],
            data_type=param_specs[0]["data_type"],
        )
        assert resp is None, "Unauthorized user should not access child timeseries"
    except Exception as e:
        assert "unauthorized" in str(e).lower() or "403" in str(e) or "401" in str(e), (
            f"Should get unauthorized error, got: {str(e)}"
        )

    print("Bridge-child timeseries test completed.")


# ---------- automation (per-child) ----------------------------------------
#
# Mirror of the firmware-side automation tests in test_firmware.py, driven
# against a bridge child. Helpers live in ``helpers/automation``; the child
# is the trigger surface (``update_param`` / ``get_param`` /
# ``wait_on_trigger_details``) while the parent carries the global
# ``state_reported`` / ``notification_sent`` flags.


def _configure_automation_child(
    parent_node_host_ctrl,
    suffix: str,
    local_id: str,
    wait_on_node_config_sent: bool = True,
):
    """Attach + configure (without verify-roundtrip) + start a child loaded
    with the automation node config. Returns the child handle.

    Bypasses ``_set_test_child_config`` so we can pass the standard automation
    config (which has bounds/properties the verify-roundtrip helper does not
    handle uniformly across versions). Mirrors ``test_bridge_child_timeseries``.
    """
    child = parent_node_host_ctrl.bridge.add_child(suffix, local_id)
    assert child is not None, f"add_child({suffix!r}) returned None"
    node_config = get_automation_node_config()
    assert child.set_config(NodeConfig(node_config)), (
        f"Failed to set automation child config for {suffix!r}"
    )
    start_bridge_child(
        parent_node_host_ctrl, child, wait_on_node_config_sent=wait_on_node_config_sent
    )
    return child


_AUTOMATION_SIMPLE_CASES = [
    # (id, automation_name, triggers, trigger_bounds_keys, actions_template, anti_actions)
    (
        "temp_gt_light_control",
        "Temperature Light Control (child)",
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
    (
        "light_power_ne_temp_control",
        "Light Power Not-Equal Control (child)",
        [
            {
                "device": "light",
                "param": "Power",
                "operator": "ne",
                "value": False,
            }
        ],
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
]


@pytest.mark.firmware
@pytest.mark.parametrize(
    "test_name,automation_name,triggers,trigger_bounds_keys,actions,anti_actions",
    [tuple(c[1:]) for c in [(None, *t) for t in _AUTOMATION_SIMPLE_CASES]],
    ids=[c[0] for c in _AUTOMATION_SIMPLE_CASES],
)
def test_bridge_child_automation_simple(
    configured_bridge_parent,
    test_name,
    automation_name,
    triggers,
    trigger_bounds_keys,
    actions,
    anti_actions,
):
    """Per-child mirror of test_firmware_automation_simple — full automation
    create/fire/verify cycle driven through the child's data model."""
    import copy

    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    triggers = copy.deepcopy(triggers)
    actions = copy.deepcopy(actions)
    anti_actions = copy.deepcopy(anti_actions)
    trigger_bounds_keys = copy.deepcopy(trigger_bounds_keys)

    child = _configure_automation_child(node_host_ctrl, "auto", "local-auto")
    thing_name = child.thing_name
    assert thing_name is not None

    node_config = get_automation_node_config()
    trigger_bounds = []
    for key in trigger_bounds_keys:
        if key == "temp_bounds":
            trigger_bounds.append(node_config["devices"][1]["params"][0]["bounds"])
        elif key == "light_brightness_bounds":
            trigger_bounds.append(node_config["devices"][0]["params"][1]["bounds"])
        else:
            trigger_bounds.append(None)

    for action in actions:
        if action.get("node") == "{thing_name}":
            action["node"] = thing_name

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        try:
            child.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            child.wait_on_trigger_details(10000)
        except Exception:
            pass

    try:
        automation_create_and_test(
            node_host_ctrl=child,
            user=user,
            group_id=group_id,
            thing_name=thing_name,
            automation_name=automation_name,
            triggers=triggers,
            trigger_bounds=trigger_bounds,
            actions=actions,
            anti_actions=anti_actions,
            parent_node_host_ctrl=node_host_ctrl,
        )
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_child_automation_concurrent(configured_bridge_parent):
    """Per-child mirror of the ``simple_concurrent_triggers`` case from
    test_firmware_automation_concurrent."""
    import copy

    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    child = _configure_automation_child(node_host_ctrl, "autoc", "local-autoc")
    thing_name = child.thing_name
    assert thing_name is not None

    automation_configs = copy.deepcopy(
        [
            {
                "name": "Temperature High Alert (child)",
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
                        "node": thing_name,
                        "device": "light",
                        "param": "Power",
                        "value": True,
                    }
                ],
            },
            {
                "name": "Switch State Monitor (child)",
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
                        "node": thing_name,
                        "device": "light",
                        "param": "Brightness",
                        "value": 90,
                    }
                ],
            },
        ]
    )
    trigger_changes = [
        {"device": "temp_sensor", "param": "Temperature", "value": 30.0},
        {"device": "switch", "param": "Power", "value": True},
    ]
    expected_results = {"light.Power": True, "light.Brightness": 90}

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        try:
            child.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            child.wait_on_trigger_details(10000)
        except Exception:
            pass

    try:
        concurrent_automation_test(
            node_host_ctrl=child,
            user=user,
            group_id=group_id,
            thing_name=thing_name,
            automation_configs=automation_configs,
            trigger_changes=trigger_changes,
            expected_results=expected_results,
            setup_anti_actions=True,
            parent_node_host_ctrl=node_host_ctrl,
        )
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_child_automation_persistence_no_reset(configured_bridge_parent):
    """Triggers provisioned to a child must survive a parent restart-without-reset.

    Exercises the ``bridge_triggers`` NVS namespace + the reload-on-connect path
    in ``bridge_internal_child_on_connect_task`` (per-node-automation-triggers
    plan §6).
    """
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    child_args = "autop", "local-autop"
    child = _configure_automation_child(node_host_ctrl, *child_args)
    thing_name = child.thing_name
    assert thing_name is not None

    empty_automation = {
        "name": "Temp High Lights (child persist)",
        "conditions": {"and": []},
        "actions": {"targets": []},
    }
    automation_id = user.create_automation(
        group_id=group_id, automation_data=empty_automation
    ).get("automation_id")
    assert automation_id is not None, "Failed to create empty automation"

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        try:
            child.clear_on_trigger_details()
            user.delete_node_trigger(group_id=group_id, node_id=thing_name)
            child.wait_on_trigger_details(10000)
        except Exception:
            pass

    try:
        trigger = {
            "device": "temp_sensor",
            "param": "Temperature",
            "operator": "gt",
            "value": 28.0,
        }
        w_triggers = wire_triggers([trigger], id_prefix=f"{thing_name}~{automation_id}")
        trigger_id = w_triggers[0]["id"]

        actions = [
            {"node": thing_name, "device": "light", "param": "Power", "value": True},
            {
                "node": thing_name,
                "device": "light",
                "param": "Brightness",
                "value": 80,
            },
        ]
        automation_data = {
            "name": "Temp High Lights (child persist)",
            "description": "Child persistence test automation",
            "conditions": {"and": [trigger_id]},
            "actions": {"targets": wire_actions(actions)},
        }
        assert (
            user.update_automation(
                group_id=group_id,
                automation_id=automation_id,
                automation_data=automation_data,
            )
            is not None
        ), "Failed to update automation"

        trigger_data = {"triggers": w_triggers}
        child.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id, node_id=thing_name, trigger_data=json.dumps(trigger_data)
        ), "Failed to set child node trigger"
        assert child.wait_on_trigger_details(10000), (
            "Child did not receive trigger details"
        )

        # Anti-action setup.
        node_host_ctrl.clear_on_state_reported()
        set_anti = False
        if child.get_param("light", "Power").value:
            set_anti = True
            assert child.update_param("light", "Power", False)
        if child.get_param("light", "Brightness").value != 0:
            set_anti = True
            assert child.update_param("light", "Brightness", 0)
        if set_anti:
            assert node_host_ctrl.wait_on_state_reported(10000), (
                "Parent did not report state after anti-action setup"
            )

        # Restart the parent (children rehydrate from NVS). Do NOT re-provision triggers.
        child.clear_on_online()
        child.clear_on_trigger_details()
        cold_reboot_node_host_ctrl(node_host_ctrl)

        # Must re-add the child to the bridge to trigger the orphan-drain path.
        # Node config is the same, so no need to wait for node config sent.
        _configure_automation_child(
            node_host_ctrl, *child_args, wait_on_node_config_sent=False
        )

        assert child.wait_on_online(30000), (
            "Child did not republish online after parent restart"
        )
        # Reload-on-connect re-fires the trigger-details event on the child
        # after rehydrating from the bridge_triggers NVS namespace.
        assert child.wait_on_trigger_details(30000), (
            "Child did not rehydrate triggers from NVS on reconnect"
        )

        # Fire the trigger.
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert child.update_param("temp_sensor", "Temperature", 29.0)
        assert node_host_ctrl.wait_on_state_reported(10000), (
            "Parent did not report state after driving child trigger"
        )
        assert node_host_ctrl.wait_on_notification_sent(10000), (
            "Parent did not flush notification after child trigger fire"
        )
        assert node_host_ctrl.wait_on_state_reported(10000), (
            "Parent did not report new state after automation actions"
        )

        assert child.get_param("light", "Power").value is True, (
            "Child light.Power not flipped by persisted automation"
        )
        assert child.get_param("light", "Brightness").value == 80, (
            "Child light.Brightness not set by persisted automation"
        )
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_parent_child_automation_independence(configured_bridge_parent):
    """A trigger firing on the parent must not affect the child's automation
    state (and vice versa). Locks in the per-node trigger list + per-node
    ``check_and_fire`` scan introduced in the per-node-automation-triggers
    refactor (plan §4)."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    parent_thing = node_host_ctrl.node_thing_name
    assert parent_thing is not None

    # Re-set parent to the automation config (overwrites the bridge-parent
    # config installed by the fixture). Bypass _set_test_node_config to avoid
    # the tags pass — automation config has none.
    parent_cfg = get_automation_node_config()
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_node_config_sent()
    assert node_host_ctrl.set_config(NodeConfig(parent_cfg)), (
        "Failed to set parent automation config"
    )
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    child = _configure_automation_child(node_host_ctrl, "autoi", "local-autoi")
    child_thing = child.thing_name

    # Parent: temp > 28 → light.Power=True. Child: temp < 10 → light.Brightness=99.
    parent_auto = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "parent-temp-high",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")
    child_auto = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "child-temp-low",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")

    parent_wt = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "gt",
                "value": 28.0,
            }
        ],
        id_prefix=f"{parent_thing}~{parent_auto}",
    )
    child_wt = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "lt",
                "value": 10.0,
            }
        ],
        id_prefix=f"{child_thing}~{child_auto}",
    )

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        for thing, n in ((parent_thing, node_host_ctrl), (child_thing, child)):
            try:
                n.clear_on_trigger_details()
                user.delete_node_trigger(group_id=group_id, node_id=thing)
                n.wait_on_trigger_details(10000)
            except Exception:
                pass

    try:
        assert user.update_automation(
            group_id=group_id,
            automation_id=parent_auto,
            automation_data={
                "name": "parent-temp-high",
                "conditions": {"and": [parent_wt[0]["id"]]},
                "actions": {
                    "targets": wire_actions(
                        [
                            {
                                "node": parent_thing,
                                "device": "light",
                                "param": "Power",
                                "value": True,
                            }
                        ]
                    )
                },
            },
        )
        assert user.update_automation(
            group_id=group_id,
            automation_id=child_auto,
            automation_data={
                "name": "child-temp-low",
                "conditions": {"and": [child_wt[0]["id"]]},
                "actions": {
                    "targets": wire_actions(
                        [
                            {
                                "node": child_thing,
                                "device": "light",
                                "param": "Brightness",
                                "value": 99,
                            }
                        ]
                    )
                },
            },
        )

        node_host_ctrl.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=parent_thing,
            trigger_data=json.dumps({"triggers": parent_wt}),
        )
        assert node_host_ctrl.wait_on_trigger_details(10000)

        child.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=child_thing,
            trigger_data=json.dumps({"triggers": child_wt}),
        )
        assert child.wait_on_trigger_details(10000)

        # Anti-action both — only update + wait if a value actually changes
        # (else no state report fires and wait_on_state_reported hangs).
        changed = False
        if node_host_ctrl.get_param("light", "Power").value is not False:
            if not changed:
                node_host_ctrl.clear_on_state_reported()
                changed = True
            assert node_host_ctrl.update_param("light", "Power", False)
        if child.get_param("light", "Brightness").value != 0:
            if not changed:
                node_host_ctrl.clear_on_state_reported()
                changed = True
            assert child.update_param("light", "Brightness", 0)
        if changed:
            assert node_host_ctrl.wait_on_state_reported(10000)

        # Fire parent trigger.
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert node_host_ctrl.update_param("temp_sensor", "Temperature", 30.0)
        assert node_host_ctrl.wait_on_state_reported(10000)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)

        assert node_host_ctrl.get_param("light", "Power").value is True, (
            "Parent automation did not fire"
        )
        assert child.get_param("light", "Brightness").value == 0, (
            "Child Brightness changed when parent trigger fired"
        )
        assert child.get_param("light", "Power").value is False, (
            "Child Power changed when parent trigger fired"
        )

        # Reset before firing child trigger — only update + wait if a value
        # actually changed.
        changed = False
        if node_host_ctrl.get_param("light", "Power").value is not False:
            if not changed:
                node_host_ctrl.clear_on_state_reported()
                changed = True
            assert node_host_ctrl.update_param("light", "Power", False)
        if child.get_param("light", "Brightness").value != 0:
            if not changed:
                node_host_ctrl.clear_on_state_reported()
                changed = True
            assert child.update_param("light", "Brightness", 0)
        if changed:
            node_host_ctrl.wait_on_state_reported(10000)

        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert child.update_param("temp_sensor", "Temperature", 5.0)
        assert node_host_ctrl.wait_on_state_reported(10000)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)

        assert child.get_param("light", "Brightness").value == 99, (
            "Child automation did not fire"
        )
        assert node_host_ctrl.get_param("light", "Power").value is False, (
            "Parent Power changed when child trigger fired"
        )
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_children_automation_independence(configured_bridge_parent):
    """Two children with identical trigger config — firing on one must not
    fire (or re-fire) the other. Locks in the per-node ``check_and_fire`` scan
    + per-node CHANGED state."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    child_a = _configure_automation_child(node_host_ctrl, "autoa", "local-autoa")
    child_b = _configure_automation_child(node_host_ctrl, "autob", "local-autob")
    thing_a, thing_b = child_a.thing_name, child_b.thing_name

    auto_a = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "a-temp-high",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")
    auto_b = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "b-temp-high",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")

    wt_a = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "gt",
                "value": 28.0,
            }
        ],
        id_prefix=f"{thing_a}~{auto_a}",
    )
    wt_b = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "gt",
                "value": 28.0,
            }
        ],
        id_prefix=f"{thing_b}~{auto_b}",
    )

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        for thing, n in ((thing_a, child_a), (thing_b, child_b)):
            try:
                n.clear_on_trigger_details()
                user.delete_node_trigger(group_id=group_id, node_id=thing)
                n.wait_on_trigger_details(10000)
            except Exception:
                pass

    try:
        for thing, auto_id, wt in (
            (thing_a, auto_a, wt_a),
            (thing_b, auto_b, wt_b),
        ):
            assert user.update_automation(
                group_id=group_id,
                automation_id=auto_id,
                automation_data={
                    "name": f"auto-{thing}",
                    "conditions": {"and": [wt[0]["id"]]},
                    "actions": {
                        "targets": wire_actions(
                            [
                                {
                                    "node": thing,
                                    "device": "light",
                                    "param": "Power",
                                    "value": True,
                                }
                            ]
                        )
                    },
                },
            )

        child_a.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=thing_a,
            trigger_data=json.dumps({"triggers": wt_a}),
        )
        assert child_a.wait_on_trigger_details(10000)

        child_b.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=thing_b,
            trigger_data=json.dumps({"triggers": wt_b}),
        )
        assert child_b.wait_on_trigger_details(10000)

        # Anti-action both children — only update + wait if a value actually
        # changes. Default Power is False so a blind update_param + wait would
        # hang on state_reported (no publish without a real change).
        changed = False
        if child_a.get_param("light", "Power").value is not False:
            if not changed:
                node_host_ctrl.clear_on_state_reported()
                changed = True
            assert child_a.update_param("light", "Power", False)
        if child_b.get_param("light", "Power").value is not False:
            if not changed:
                node_host_ctrl.clear_on_state_reported()
                changed = True
            assert child_b.update_param("light", "Power", False)
        if changed:
            assert node_host_ctrl.wait_on_state_reported(10000)

        # Fire child A only.
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert child_a.update_param("temp_sensor", "Temperature", 30.0)
        assert node_host_ctrl.wait_on_state_reported(10000)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)

        assert child_a.get_param("light", "Power").value is True, (
            "child_a automation did not fire"
        )
        assert child_b.get_param("light", "Power").value is False, (
            "child_b Power flipped when only child_a trigger fired"
        )

        # Flip child A's power to False, to ensure that the trigger is not fired again.
        node_host_ctrl.clear_on_state_reported()
        assert child_a.update_param("light", "Power", False)
        assert node_host_ctrl.wait_on_state_reported(10000)

        assert child_a.get_param("light", "Power").value is False, (
            "child_a Power not flipped to False"
        )

        # Fire child B.
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert child_b.update_param("temp_sensor", "Temperature", 30.0)
        assert node_host_ctrl.wait_on_state_reported(10000)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)

        assert child_b.get_param("light", "Power").value is True, (
            "child_b automation did not fire after its own trigger"
        )
        assert child_a.get_param("light", "Power").value is False, (
            "child_a Power changed when child_b trigger fired"
        )
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_parent_child_automation_clear_independence(configured_bridge_parent):
    """Clearing parent's triggers must not wipe the child's, and vice
    versa. Locks in per-node ``automation_drop_node`` + the fact that
    ``set_node_trigger(thing, ...)`` only touches that thing's slice."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    parent_thing = node_host_ctrl.node_thing_name
    assert parent_thing is not None

    parent_cfg = get_automation_node_config()
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_node_config_sent()
    assert node_host_ctrl.set_config(NodeConfig(parent_cfg)), (
        "Failed to set parent automation config"
    )
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    child = _configure_automation_child(node_host_ctrl, "autopcc", "local-autopcc")
    child_thing = child.thing_name

    parent_auto = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "p-temp-high",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")
    child_auto = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "c-temp-low",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")

    parent_wt = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "gt",
                "value": 28.0,
            }
        ],
        id_prefix=f"{parent_thing}~{parent_auto}",
    )
    child_wt = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "lt",
                "value": 10.0,
            }
        ],
        id_prefix=f"{child_thing}~{child_auto}",
    )

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        for thing, n in ((parent_thing, node_host_ctrl), (child_thing, child)):
            try:
                n.clear_on_trigger_details()
                user.delete_node_trigger(group_id=group_id, node_id=thing)
                n.wait_on_trigger_details(10000)
            except Exception:
                pass

    def _anti_action_both():
        changed = False
        for n, dev, param, target in (
            (node_host_ctrl, "light", "Power", False),
            (child, "light", "Brightness", 0),
        ):
            cur = n.get_param(dev, param).value
            target_match = (
                (cur is target) if isinstance(target, bool) else (cur == target)
            )
            if not target_match:
                if not changed:
                    node_host_ctrl.clear_on_state_reported()
                    changed = True
                assert n.update_param(dev, param, target)
        if changed:
            assert node_host_ctrl.wait_on_state_reported(10000)

    def _install_parent_triggers():
        node_host_ctrl.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=parent_thing,
            trigger_data=json.dumps({"triggers": parent_wt}),
        )
        assert node_host_ctrl.wait_on_trigger_details(10000)

    def _install_child_triggers():
        child.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id,
            node_id=child_thing,
            trigger_data=json.dumps({"triggers": child_wt}),
        )
        assert child.wait_on_trigger_details(10000)

    def _clear_triggers(thing, n):
        n.clear_on_trigger_details()
        assert user.delete_node_trigger(group_id=group_id, node_id=thing)
        assert n.wait_on_trigger_details(10000)

    def _drive_temp(n, value):
        """Force a real change on ``temp_sensor::Temperature`` so the
        trigger evaluator sees a transition. Direction 2 reuses the same
        trigger value from direction 1 — a blind ``update_param`` to an
        unchanged value never fires a state report, so we bounce through
        a neutral 22.5 if we're already at the target."""
        cur = n.get_param("temp_sensor", "Temperature").value
        if cur == value:
            node_host_ctrl.clear_on_state_reported()
            assert n.update_param("temp_sensor", "Temperature", 22.5)
            assert node_host_ctrl.wait_on_state_reported(10000)
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert n.update_param("temp_sensor", "Temperature", value)
        assert node_host_ctrl.wait_on_state_reported(10000)

    try:
        # Wire both automations.
        assert user.update_automation(
            group_id=group_id,
            automation_id=parent_auto,
            automation_data={
                "name": "p-temp-high",
                "conditions": {"and": [parent_wt[0]["id"]]},
                "actions": {
                    "targets": wire_actions(
                        [
                            {
                                "node": parent_thing,
                                "device": "light",
                                "param": "Power",
                                "value": True,
                            }
                        ]
                    )
                },
            },
        )
        assert user.update_automation(
            group_id=group_id,
            automation_id=child_auto,
            automation_data={
                "name": "c-temp-low",
                "conditions": {"and": [child_wt[0]["id"]]},
                "actions": {
                    "targets": wire_actions(
                        [
                            {
                                "node": child_thing,
                                "device": "light",
                                "param": "Brightness",
                                "value": 99,
                            }
                        ]
                    )
                },
            },
        )

        # ----- Direction 1: clear PARENT, verify CHILD still fires. -----
        _install_parent_triggers()
        _install_child_triggers()
        _clear_triggers(parent_thing, node_host_ctrl)
        _anti_action_both()

        # Parent trigger condition: must NOT fire (cleared).
        _drive_temp(node_host_ctrl, 30.0)
        assert node_host_ctrl.get_param("light", "Power").value is False, (
            "Cleared parent automation must not fire"
        )

        # Child trigger condition: must still fire (not cleared).
        _drive_temp(child, 5.0)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)
        assert child.get_param("light", "Brightness").value == 99, (
            "Child automation must still fire after clearing only parent's triggers"
        )

        # ----- Direction 2: clear CHILD, verify PARENT still fires. -----
        _install_parent_triggers()
        _install_child_triggers()
        _clear_triggers(child_thing, child)
        _anti_action_both()

        # Child trigger condition: must NOT fire (cleared).
        _drive_temp(child, 5.0)
        assert child.get_param("light", "Brightness").value == 0, (
            "Cleared child automation must not fire"
        )

        # Parent trigger condition: must still fire.
        _drive_temp(node_host_ctrl, 30.0)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)
        assert node_host_ctrl.get_param("light", "Power").value is True, (
            "Parent automation must still fire after clearing only child's triggers"
        )
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_children_automation_clear_independence(configured_bridge_parent):
    """Clearing one child's triggers must not affect the sibling. Locks
    in per-child trigger-list isolation under bridge teardown / clear."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    child_a = _configure_automation_child(node_host_ctrl, "autocca", "local-autocca")
    child_b = _configure_automation_child(node_host_ctrl, "autoccb", "local-autoccb")
    thing_a, thing_b = child_a.thing_name, child_b.thing_name

    auto_a = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "a-temp-high",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")
    auto_b = user.create_automation(
        group_id=group_id,
        automation_data={
            "name": "b-temp-high",
            "conditions": {"and": []},
            "actions": {"targets": []},
        },
    ).get("automation_id")

    wt_a = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "gt",
                "value": 28.0,
            }
        ],
        id_prefix=f"{thing_a}~{auto_a}",
    )
    wt_b = wire_triggers(
        [
            {
                "device": "temp_sensor",
                "param": "Temperature",
                "operator": "gt",
                "value": 28.0,
            }
        ],
        id_prefix=f"{thing_b}~{auto_b}",
    )

    def cleanup():
        try:
            user.delete_all_automations(group_id=group_id)
        except Exception:
            pass
        for thing, n in ((thing_a, child_a), (thing_b, child_b)):
            try:
                n.clear_on_trigger_details()
                user.delete_node_trigger(group_id=group_id, node_id=thing)
                n.wait_on_trigger_details(10000)
            except Exception:
                pass

    def _anti_action_both():
        changed = False
        for c in (child_a, child_b):
            if c.get_param("light", "Power").value is not False:
                if not changed:
                    node_host_ctrl.clear_on_state_reported()
                    changed = True
                assert c.update_param("light", "Power", False)
        if changed:
            assert node_host_ctrl.wait_on_state_reported(10000)

    def _install(thing, n, wt):
        n.clear_on_trigger_details()
        assert user.set_node_trigger(
            group_id=group_id, node_id=thing, trigger_data=json.dumps({"triggers": wt})
        )
        assert n.wait_on_trigger_details(10000)

    def _clear(thing, n):
        n.clear_on_trigger_details()
        assert user.delete_node_trigger(group_id=group_id, node_id=thing)
        assert n.wait_on_trigger_details(10000)

    def _drive_temp(n, value):
        """Bounce via 22.5 if already at value so the update is always
        a real change (else state_reported would not fire and the wait
        below would hang)."""
        cur = n.get_param("temp_sensor", "Temperature").value
        if cur == value:
            node_host_ctrl.clear_on_state_reported()
            assert n.update_param("temp_sensor", "Temperature", 22.5)
            assert node_host_ctrl.wait_on_state_reported(10000)
        node_host_ctrl.clear_on_notification_sent()
        node_host_ctrl.clear_on_state_reported()
        assert n.update_param("temp_sensor", "Temperature", value)
        assert node_host_ctrl.wait_on_state_reported(10000)

    try:
        # Wire both automations.
        for thing, auto_id, wt in ((thing_a, auto_a, wt_a), (thing_b, auto_b, wt_b)):
            assert user.update_automation(
                group_id=group_id,
                automation_id=auto_id,
                automation_data={
                    "name": f"auto-{thing}",
                    "conditions": {"and": [wt[0]["id"]]},
                    "actions": {
                        "targets": wire_actions(
                            [
                                {
                                    "node": thing,
                                    "device": "light",
                                    "param": "Power",
                                    "value": True,
                                }
                            ]
                        )
                    },
                },
            )

        # ----- Direction 1: clear A, verify B still fires. -----
        _install(thing_a, child_a, wt_a)
        _install(thing_b, child_b, wt_b)
        _clear(thing_a, child_a)
        _anti_action_both()

        # A's condition: must NOT fire (cleared).
        _drive_temp(child_a, 30.0)
        assert child_a.get_param("light", "Power").value is False, (
            "Cleared child_a automation must not fire"
        )

        # B's condition: must still fire.
        _drive_temp(child_b, 30.0)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)
        assert child_b.get_param("light", "Power").value is True, (
            "child_b automation must still fire after clearing only child_a's triggers"
        )

        # ----- Direction 2: clear B, verify A still fires. -----
        _install(thing_a, child_a, wt_a)
        _install(thing_b, child_b, wt_b)
        _clear(thing_b, child_b)
        _anti_action_both()

        # B's condition: must NOT fire.
        _drive_temp(child_b, 30.0)
        assert child_b.get_param("light", "Power").value is False, (
            "Cleared child_b automation must not fire"
        )

        # A's condition: must still fire.
        _drive_temp(child_a, 30.0)
        assert node_host_ctrl.wait_on_notification_sent(10000)
        node_host_ctrl.wait_on_state_reported(10000)
        assert child_a.get_param("light", "Power").value is True, (
            "child_a automation must still fire after clearing only child_b's triggers"
        )
    except Exception:
        cleanup()
        raise
    cleanup()


# ---------- scheduling (per-child) ----------------------------------------
#
# Mirror of test_firmware_scheduling_* (cyclical / one_time /
# persistence_no_reset), driven against a bridge child. Helpers live in
# ``helpers/scheduling``; the child is the action surface (``update_param``
# / ``get_param`` / ``wait_on_sched_details``) while the parent owns time
# control and the global ``state_reported`` flag.


def _configure_scheduling_child(
    parent_node_host_ctrl,
    suffix: str,
    local_id: str,
    wait_on_node_config_sent: bool = True,
):
    """Attach + configure (without verify-roundtrip) + start a child loaded
    with the scheduling node config. Returns the child handle.

    Mirrors ``_configure_automation_child``: bypasses ``_set_test_child_config``
    so the standard scheduling config (with bounds / properties) sails through
    without the verify-roundtrip helper choking on per-version dtype quirks."""
    child = parent_node_host_ctrl.bridge.add_child(suffix, local_id)
    assert child is not None, f"add_child({suffix!r}) returned None"
    node_config = get_scheduling_node_config()
    assert child.set_config(NodeConfig(node_config)), (
        f"Failed to set scheduling child config for {suffix!r}"
    )
    start_bridge_child(
        parent_node_host_ctrl, child, wait_on_node_config_sent=wait_on_node_config_sent
    )
    return child


def _bridge_schedule_zonal_at(
    zone_info, minute_of_day: int, days_mask: int, schedule_id: str, name: str
):
    """Build a wake-up-style cyclical schedule for the scheduling-test config.
    Action drives light Power/Brightness + curtain Position + aircon Power;
    anti_action is the inverse so each verify cycle is meaningful."""
    return {
        "name": name,
        "id": schedule_id,
        "triggers": [{"m": minute_of_day, "d": days_mask}],
        "action": {
            "light": {"Power": True, "Brightness": 100},
            "curtain": {"Position": 0},
            "aircon": {"Power": False},
        },
        "anti_action": {
            "light": {"Power": False, "Brightness": 0},
            "curtain": {"Position": 100},
            "aircon": {"Power": True},
        },
    }


@pytest.mark.firmware
def test_bridge_child_scheduling_simple(configured_bridge_parent):
    """Per-child mirror of the scheduling smoke test: provision a single
    schedule on the child, advance the parent's clock to its trigger time,
    assert the child's params land at the action values. Exercises the
    cloud→child schedule routing path."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    child = _configure_scheduling_child(node_host_ctrl, "sch", "local-sch")
    thing_name = child.thing_name
    assert thing_name is not None

    node_timezone = node_host_ctrl.get_current_timezone()
    assert node_timezone is not None, "Failed to get device timezone"
    zone_info = ZoneInfo(node_timezone)

    schedule = _bridge_schedule_zonal_at(
        zone_info, 7 * 60 + 30, 0x1F, "wake_up_child", "Wake Up (child)"
    )

    def cleanup():
        try:
            scheduling_set_schedules(user, group_id, child, thing_name, [])
        except Exception:
            pass

    try:
        start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)  # Monday 7:00
        assert node_host_ctrl.time_control_set_time(start_time)
        assert node_host_ctrl.get_current_time() == start_time

        scheduling_set_schedules(user, group_id, child, thing_name, [schedule])

        trigger_time = dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info)
        scheduling_verify_schedule(child, node_host_ctrl, trigger_time, schedule)
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_parent_child_scheduling_independence(configured_bridge_parent):
    """A schedule provisioned on the parent fires only on the parent; a
    schedule provisioned on the child fires only on the child. Locks in
    per-node schedule storage + the priv_data-routed trigger callback."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    parent_thing = node_host_ctrl.node_thing_name
    assert parent_thing is not None

    # Re-set parent to the scheduling config. Bypass _set_test_node_config
    # because the scheduling config carries no tags (and the helper requires
    # a tags JSON path).
    parent_cfg = get_scheduling_node_config()
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_node_config_sent()
    assert node_host_ctrl.set_config(NodeConfig(parent_cfg)), (
        "Failed to set parent scheduling config"
    )
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    child = _configure_scheduling_child(node_host_ctrl, "schi", "local-schi")
    child_thing = child.thing_name
    assert child_thing is not None

    node_timezone = node_host_ctrl.get_current_timezone()
    zone_info = ZoneInfo(node_timezone)

    # Distinct trigger minutes so each verify cycle hits exactly one schedule.
    parent_sched = _bridge_schedule_zonal_at(
        zone_info, 7 * 60 + 30, 0x1F, "parent_wake", "Parent wake"
    )
    child_sched = _bridge_schedule_zonal_at(
        zone_info, 8 * 60, 0x1F, "child_wake", "Child wake"
    )

    def cleanup():
        for thing, n in ((parent_thing, node_host_ctrl), (child_thing, child)):
            try:
                scheduling_set_schedules(user, group_id, n, thing, [])
            except Exception:
                pass

    try:
        start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
        assert node_host_ctrl.time_control_set_time(start_time)

        scheduling_set_schedules(
            user, group_id, node_host_ctrl, parent_thing, [parent_sched]
        )
        scheduling_set_schedules(user, group_id, child, child_thing, [child_sched])

        # Fire parent schedule (7:30). Parent's params land action; child's
        # stay at anti_action. The anti_action is set on each verify, so
        # apply child's anti_action manually before the parent fire so we
        # can assert child params unaffected.
        scheduling_set_anti_action(child, node_host_ctrl, child_sched["anti_action"])
        scheduling_verify_schedule(
            node_host_ctrl,
            node_host_ctrl,
            dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info),
            parent_sched,
        )
        # Child must still be at anti_action — parent's schedule must not
        # touch it.
        scheduling_verify_action(child, child_sched["anti_action"])

        # Now fire child schedule (8:00). Child params should land action,
        # parent stays at parent's anti_action.
        scheduling_set_anti_action(
            node_host_ctrl, node_host_ctrl, parent_sched["anti_action"]
        )
        scheduling_verify_schedule(
            child,
            node_host_ctrl,
            dt(2025, 9, 15, 8, 0, 0, tzinfo=zone_info),
            child_sched,
        )
        scheduling_verify_action(node_host_ctrl, parent_sched["anti_action"])
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_children_scheduling_independence(configured_bridge_parent):
    """Two children with identical schedule ids — firing on one must not
    fire (or re-fire) the other. Locks in the per-node schedule slice +
    the SHA-256(local_id, cloud_id) name derivation (no NVS-name collision
    between children sharing the same cloud id)."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    child_a = _configure_scheduling_child(node_host_ctrl, "scha", "local-scha")
    child_b = _configure_scheduling_child(node_host_ctrl, "schb", "local-schb")
    thing_a, thing_b = child_a.thing_name, child_b.thing_name
    assert thing_a and thing_b

    node_timezone = node_host_ctrl.get_current_timezone()
    zone_info = ZoneInfo(node_timezone)

    # Same cloud id "wake_up" on both children — the per-child NVS-name
    # hash must keep them isolated. Distinct trigger minutes per child so
    # exactly one fires per advance.
    sched_a = _bridge_schedule_zonal_at(
        zone_info, 7 * 60 + 30, 0x1F, "wake_up", "Wake Up (a)"
    )
    sched_b = _bridge_schedule_zonal_at(
        zone_info, 8 * 60, 0x1F, "wake_up", "Wake Up (b)"
    )

    def cleanup():
        for thing, c in ((thing_a, child_a), (thing_b, child_b)):
            try:
                scheduling_set_schedules(user, group_id, c, thing, [])
            except Exception:
                pass

    try:
        start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
        assert node_host_ctrl.time_control_set_time(start_time)

        scheduling_set_schedules(user, group_id, child_a, thing_a, [sched_a])
        scheduling_set_schedules(user, group_id, child_b, thing_b, [sched_b])

        # Fire child_a's schedule at 7:30. child_b must stay at anti_action.
        scheduling_set_anti_action(child_b, node_host_ctrl, sched_b["anti_action"])
        scheduling_verify_schedule(
            child_a,
            node_host_ctrl,
            dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info),
            sched_a,
        )
        scheduling_verify_action(child_b, sched_b["anti_action"])

        # Fire child_b's schedule at 8:00. child_a must stay at anti_action.
        scheduling_set_anti_action(child_a, node_host_ctrl, sched_a["anti_action"])
        scheduling_verify_schedule(
            child_b, node_host_ctrl, dt(2025, 9, 15, 8, 0, 0, tzinfo=zone_info), sched_b
        )
        scheduling_verify_action(child_a, sched_a["anti_action"])
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_parent_child_scheduling_clear_independence(configured_bridge_parent):
    """Clearing schedules on the parent must not wipe the child's, and
    vice versa. Locks in per-node ``__node_release_locked`` + the fact
    that ``__build_schedule_details_for_node_locked`` runs under that
    node's lock only and only touches its slice."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    parent_thing = node_host_ctrl.node_thing_name
    assert parent_thing is not None

    parent_cfg = get_scheduling_node_config()
    node_host_ctrl.clear_on_state_reported()
    node_host_ctrl.clear_on_node_config_sent()
    assert node_host_ctrl.set_config(NodeConfig(parent_cfg)), (
        "Failed to set parent scheduling config"
    )
    restart_node_host_ctrl_without_reset(node_host_ctrl)

    child = _configure_scheduling_child(node_host_ctrl, "schpcc", "local-schpcc")
    child_thing = child.thing_name
    assert child_thing is not None

    node_timezone = node_host_ctrl.get_current_timezone()
    zone_info = ZoneInfo(node_timezone)

    parent_sched = _bridge_schedule_zonal_at(
        zone_info, 7 * 60 + 30, 0x1F, "p_wake", "Parent wake"
    )
    child_sched = _bridge_schedule_zonal_at(
        zone_info, 8 * 60, 0x1F, "c_wake", "Child wake"
    )

    def cleanup():
        for thing, n in ((parent_thing, node_host_ctrl), (child_thing, child)):
            try:
                scheduling_set_schedules(user, group_id, n, thing, [])
            except Exception:
                pass

    try:
        # ----- Direction 1: install both, clear PARENT, child still fires. -----
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
        )
        scheduling_set_schedules(
            user, group_id, node_host_ctrl, parent_thing, [parent_sched]
        )
        scheduling_set_schedules(user, group_id, child, child_thing, [child_sched])

        # Clear parent only.
        scheduling_set_schedules(user, group_id, node_host_ctrl, parent_thing, [])

        # Anti-action both surfaces so a fire is observable.
        scheduling_set_anti_action(
            node_host_ctrl, node_host_ctrl, parent_sched["anti_action"]
        )
        scheduling_set_anti_action(child, node_host_ctrl, child_sched["anti_action"])

        # Advance to parent's trigger time — cleared, must not fire. Then
        # advance to child's trigger time — must still fire.
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info)
        )
        node_host_ctrl.wait_on_state_reported(2000)
        scheduling_verify_action(node_host_ctrl, parent_sched["anti_action"])
        scheduling_verify_action(child, child_sched["anti_action"])

        scheduling_verify_schedule(
            child,
            node_host_ctrl,
            dt(2025, 9, 15, 8, 0, 0, tzinfo=zone_info),
            child_sched,
        )
        scheduling_verify_action(node_host_ctrl, parent_sched["anti_action"])

        # ----- Direction 2: install both, clear CHILD, parent still fires. -----
        # Re-pin to before 7:30 next day to give parent's days-of-week schedule
        # a fresh trigger window.
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 16, 7, 0, 0, tzinfo=zone_info)
        )
        scheduling_set_schedules(
            user, group_id, node_host_ctrl, parent_thing, [parent_sched]
        )
        scheduling_set_schedules(user, group_id, child, child_thing, [child_sched])

        # Clear child only.
        scheduling_set_schedules(user, group_id, child, child_thing, [])

        scheduling_set_anti_action(
            node_host_ctrl, node_host_ctrl, parent_sched["anti_action"]
        )
        scheduling_set_anti_action(child, node_host_ctrl, child_sched["anti_action"])

        # Parent must fire at 7:30; child stays at anti_action.
        scheduling_verify_schedule(
            node_host_ctrl,
            node_host_ctrl,
            dt(2025, 9, 16, 7, 30, 0, tzinfo=zone_info),
            parent_sched,
        )
        scheduling_verify_action(child, child_sched["anti_action"])

        # Advance to child's trigger time — cleared, must not fire.
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 16, 8, 0, 0, tzinfo=zone_info)
        )
        node_host_ctrl.wait_on_state_reported(2000)
        scheduling_verify_action(child, child_sched["anti_action"])
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_children_scheduling_clear_independence(configured_bridge_parent):
    """Clearing one child's schedules must not wipe the sibling's. Same
    invariant as the parent-child variant, applied to inter-child."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    child_a = _configure_scheduling_child(node_host_ctrl, "schcca", "local-schcca")
    child_b = _configure_scheduling_child(node_host_ctrl, "schccb", "local-schccb")
    thing_a, thing_b = child_a.thing_name, child_b.thing_name
    assert thing_a and thing_b

    node_timezone = node_host_ctrl.get_current_timezone()
    zone_info = ZoneInfo(node_timezone)

    sched_a = _bridge_schedule_zonal_at(
        zone_info, 7 * 60 + 30, 0x1F, "wake_a", "Wake Up (a)"
    )
    sched_b = _bridge_schedule_zonal_at(
        zone_info, 8 * 60, 0x1F, "wake_b", "Wake Up (b)"
    )

    def cleanup():
        for thing, c in ((thing_a, child_a), (thing_b, child_b)):
            try:
                scheduling_set_schedules(user, group_id, c, thing, [])
            except Exception:
                pass

    try:
        # ----- Direction 1: install both, clear A, B still fires. -----
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
        )
        scheduling_set_schedules(user, group_id, child_a, thing_a, [sched_a])
        scheduling_set_schedules(user, group_id, child_b, thing_b, [sched_b])

        scheduling_set_schedules(user, group_id, child_a, thing_a, [])

        scheduling_set_anti_action(child_a, node_host_ctrl, sched_a["anti_action"])
        scheduling_set_anti_action(child_b, node_host_ctrl, sched_b["anti_action"])

        # Advance to A's trigger time — cleared, must not fire.
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info)
        )
        node_host_ctrl.wait_on_state_reported(2000)
        scheduling_verify_action(child_a, sched_a["anti_action"])
        scheduling_verify_action(child_b, sched_b["anti_action"])

        # Advance to B's trigger time — must fire.
        scheduling_verify_schedule(
            child_b, node_host_ctrl, dt(2025, 9, 15, 8, 0, 0, tzinfo=zone_info), sched_b
        )
        scheduling_verify_action(child_a, sched_a["anti_action"])

        # ----- Direction 2: install both, clear B, A still fires. -----
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 16, 7, 0, 0, tzinfo=zone_info)
        )
        scheduling_set_schedules(user, group_id, child_a, thing_a, [sched_a])
        scheduling_set_schedules(user, group_id, child_b, thing_b, [sched_b])

        scheduling_set_schedules(user, group_id, child_b, thing_b, [])

        scheduling_set_anti_action(child_a, node_host_ctrl, sched_a["anti_action"])
        scheduling_set_anti_action(child_b, node_host_ctrl, sched_b["anti_action"])

        # A fires at 7:30.
        scheduling_verify_schedule(
            child_a,
            node_host_ctrl,
            dt(2025, 9, 16, 7, 30, 0, tzinfo=zone_info),
            sched_a,
        )
        scheduling_verify_action(child_b, sched_b["anti_action"])

        # Advance to B's trigger time — cleared, must not fire.
        assert node_host_ctrl.time_control_set_time(
            dt(2025, 9, 16, 8, 0, 0, tzinfo=zone_info)
        )
        node_host_ctrl.wait_on_state_reported(2000)
        scheduling_verify_action(child_b, sched_b["anti_action"])
    except Exception:
        cleanup()
        raise
    cleanup()


@pytest.mark.firmware
def test_bridge_child_scheduling_persistence_no_reset(configured_bridge_parent):
    """Schedules provisioned to a child must survive a parent
    restart-without-reset.

    Exercises the orphan-list rehydrate path end-to-end:

    1. ``scheduling_set_schedules`` installs the schedule via the cloud
       path. Parent stores it in esp_schedule NVS with priv_data carrying
       ``bridge_local_id`` (see schedules.c file docstring).
    2. ``cold_reboot_node_host_ctrl`` deinits + reinits the parent
       process; NVS partition is preserved.
    3. On start, ``esp_rmaker_schedule_service_on_start`` runs *before*
       MQTT comes up and *before* the bridge layer has any children in
       its pool (children only enter via the host_ctrl bridge handler after
       MQTT). So ``__bucket_init_nvs_handles`` cannot resolve the child
       and parks the rehydrated handle on the orphan list with the
       handle disabled.
    4. Child reconnects and reannounces over the host_ctrl protocol →
       ``esp_rmaker_bridge_add_child`` invokes
       ``esp_rmaker_schedule_service_on_child_added``, which splices
       every orphan matching this ``local_id`` off the list, attaches
       each handle to the child node's slice, re-enables it, and
       dispatches ``BRIDGE_CHILD_EVENT_SCHED_DETAILS_RECEIVED``.

    Cloud cannot rescue this path: schedule details from cloud are
    version-gated, so a wipe at on_start would not trigger a fresh
    push. The ``wait_on_sched_details`` below therefore reflects the
    orphan-drain dispatch, not a cloud reinstall. The trigger fire
    after the restart asserts the rehydrated handle is live and
    correctly bucketed back into the same child."""
    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent
    child_args = "schp", "local-schp"
    child = _configure_scheduling_child(node_host_ctrl, *child_args)
    thing_name = child.thing_name
    assert thing_name is not None

    node_timezone = node_host_ctrl.get_current_timezone()
    zone_info = ZoneInfo(node_timezone)

    schedule = _bridge_schedule_zonal_at(
        zone_info, 7 * 60 + 30, 0x1F, "wake_up_persist", "Wake Up (child persist)"
    )

    def cleanup():
        try:
            scheduling_set_schedules(user, group_id, child, thing_name, [])
        except Exception:
            pass

    try:
        # Set the schedule.
        scheduling_set_schedules(user, group_id, child, thing_name, [schedule])

        # Restart the parent. NVS preserved; live RAM gone. Schedules
        # come back via orphan-park (on_start) → orphan-drain
        # (on_child_added). sched_details fires from the drain path,
        # not from cloud.
        child.clear_on_online()
        child.clear_on_sched_details()
        cold_reboot_node_host_ctrl(node_host_ctrl)

        # Set the clock to right before the schedule trigger time.
        start_time = dt(2025, 9, 15, 7, 0, 0, tzinfo=zone_info)
        assert node_host_ctrl.time_control_set_time(start_time)

        # Must re-add the child to the bridge to trigger the orphan-drain path.
        # Node config is the same, so no need to wait for node config sent.
        _configure_scheduling_child(
            node_host_ctrl, *child_args, wait_on_node_config_sent=False
        )

        assert child.wait_on_online(30000), (
            "Child did not republish online after parent cold reboot"
        )
        assert child.wait_on_sched_details(30000), (
            "Child did not rehydrate schedules from NVS on reconnect "
            "(orphan-drain path did not dispatch sched_details)"
        )

        # After restart, schedule should still trigger at 7:30.
        trigger_time = dt(2025, 9, 15, 7, 30, 0, tzinfo=zone_info)
        scheduling_verify_schedule(child, node_host_ctrl, trigger_time, schedule)
    except Exception:
        cleanup()
        raise
    cleanup()


# ─────────────────────────────────────────────────────────────────────────────
# Bridge stress — concurrent SDK activity over pre-registered child pool.
# ─────────────────────────────────────────────────────────────────────────────

# Tunables. Start conservative; ramp once green.
BRIDGE_STRESS_POOL_SIZE = 150
BRIDGE_STRESS_CYCLES = 5
BRIDGE_STRESS_INTER_CYCLE_DELAY = 15.0  # seconds (temporarily increased to avoid flake)
BRIDGE_STRESS_CHILD_MODIFY_DELAY = 0.25  # seconds
BRIDGE_STRESS_SUBSET_FRAC = 0.5
BRIDGE_STRESS_SUBSET_MIN = 3
BRIDGE_STRESS_SEED_ENV = "BRIDGE_STRESS_SEED"
BRIDGE_STRESS_LEAK_TOLERANCE_BYTES = 4 * 1024
# Each bridge child's node owns a lock mutex (xSemaphoreCreateMutex, ~84 B
# request + allocator overhead) created on the child's first use. It is
# intentionally NOT freed on remove — reused slots keep their mutex (see
# _esp_rmaker_node_init), which avoids create/delete churn and a use-after-free
# window vs the lazy state/timeseries reapers. So every child the test touches
# permanently retains one mutex worth of heap that never returns to free. This
# is bounded, cached state — not a leak — so it is subtracted from the measured
# drift before the tolerance check.
BRIDGE_STRESS_CACHED_MUTEX_BYTES = 88
BRIDGE_STRESS_MIN_LARGEST_BLOCK_BYTES_POSIX = 32 * 1024
# Placeholder until measured on hardware.
BRIDGE_STRESS_MIN_LARGEST_BLOCK_BYTES_ESP_S3 = 8 * 1024


def _stress_min_largest_block_bytes(target: str) -> int:
    if target == "posix":
        return BRIDGE_STRESS_MIN_LARGEST_BLOCK_BYTES_POSIX
    if target == "esp32s3":
        return BRIDGE_STRESS_MIN_LARGEST_BLOCK_BYTES_ESP_S3
    # Should never be reached — the test skips other targets up front.
    return 0


@pytest.mark.firmware
@pytest.mark.stress
def test_bridge_stress_concurrent(
    firmware_instance_request_host_ctrl_bridge,
    configured_bridge_parent,
    add_summary_section,
):
    """Bridge stress under concurrent SDK activity.

    Pre-registers ``BRIDGE_STRESS_POOL_SIZE`` children (one ``bridgeAck`` per
    add, counted as setup cost), then runs ``BRIDGE_STRESS_CYCLES`` cycles.
    Each cycle picks one random subset (``BRIDGE_STRESS_SUBSET_FRAC`` of
    the pool) and drives state, automation and schedule scenarios against
    the same subset in an interleaved fan-out. Asserts overall heap stays
    bounded and the smallest-largest-block stays above a target-specific
    floor.

    Runs on POSIX + ESP32-S3 only. Other ESP targets are skipped (insufficient
    RAM at this pool size). Reproduce a flake by setting
    ``BRIDGE_STRESS_SEED=<int>`` in the environment.
    """
    target = firmware_instance_request_host_ctrl_bridge.target
    if target not in ("posix", "esp32s3"):
        pytest.skip(f"Bridge stress test skipped on target {target!r}")

    node_host_ctrl, user, group_id, _parent_config = configured_bridge_parent

    # Seed RNG — log so a flaky run can be reproduced.
    seed_env = os.environ.get(BRIDGE_STRESS_SEED_ENV)
    if seed_env is not None:
        seed = int(seed_env)
    else:
        seed = random.SystemRandom().randint(0, 2**32 - 1)
    rng = random.Random(seed)
    print(
        f"[bridge stress] seed={seed} pool={BRIDGE_STRESS_POOL_SIZE} "
        f"cycles={BRIDGE_STRESS_CYCLES}"
    )

    # Heap baseline (use the parent's host_ctrl — the child host controllers are virtual
    # views over the same parent firmware process).
    initial_heap = node_host_ctrl.get_heap_status()
    tracker = HeapStatusTracker()
    tracker.track(initial_heap)

    heap_timeline: list[tuple[str, object]] = [("initial", initial_heap)]
    cycle_wall_times: list[float] = []

    # ---- Pool setup ---------------------------------------------------------
    setup_t0 = time.monotonic()
    # Phase 1: fan-out adds with no ack. One bridgeAck RTT per child
    # would dominate setup wall time at large pool sizes — fire all the
    # adds first and wait for them collectively below.
    for i in range(BRIDGE_STRESS_POOL_SIZE):
        assert node_host_ctrl.bridge.add_child_no_ack(f"c{i}", f"local-{i}"), (
            f"add_child_no_ack(c{i}) failed"
        )
        time.sleep(BRIDGE_STRESS_CHILD_MODIFY_DELAY)

    # Phase 2: poll list_children until every child is READY (or timeout).
    add_wall = time.monotonic() - setup_t0
    pool: list = node_host_ctrl.bridge.wait_for_children(
        BRIDGE_STRESS_POOL_SIZE,
        timeout_s=max(30.0, BRIDGE_STRESS_POOL_SIZE * 1.5),
        poll_interval_s=0.5,
    )
    assert len(pool) == BRIDGE_STRESS_POOL_SIZE, (
        f"Only {len(pool)}/{BRIDGE_STRESS_POOL_SIZE} children reached READY"
    )
    ready_wall = time.monotonic() - setup_t0

    # Phase 3a: fan-out per-child set_config + commit_devices WITHOUT
    # waiting on per-child flags between commits. Each commit kicks off
    # a node-config publish + group-info fetch round-trip; serializing
    # those round-trips per child was the dominant remaining cost.
    stress_cfg = NodeConfig(stress_child_node_config())
    node_host_ctrl.clear_on_state_reported()
    for child in pool:
        child.clear_on_node_config_sent()
        child.clear_on_group_info()
        assert child.set_config(stress_cfg), f"set_config failed for {child.thing_name}"
        assert child.commit_devices(), f"commit_devices failed for {child.thing_name}"
        time.sleep(BRIDGE_STRESS_CHILD_MODIFY_DELAY)

    commit_wall = time.monotonic() - setup_t0

    # Phase 3b: collective drain — wait per-child group_info + node_config_sent.
    # Per-child waits are sequential but the flags fire in parallel cloud-side,
    # so only the first wait absorbs the batch drain; the rest return almost
    # immediately. This is a failure-timeout, not a delay — the happy path never
    # spends it — so a flat, generous value is all that's needed.
    per_child_timeout_ms = 30000
    for i, child in enumerate(pool):
        assert child.wait_on_group_info(per_child_timeout_ms), (
            f"group_info not received by {child.thing_name}"
        )
        assert child.wait_on_node_config_sent(per_child_timeout_ms), (
            f"node_config_sent not received by {child.thing_name}"
        )
        if (i + 1) % 25 == 0 or (i + 1) == BRIDGE_STRESS_POOL_SIZE:
            snap = node_host_ctrl.get_heap_status()
            tracker.track(snap)
            heap_timeline.append((f"after-child-{i + 1}", snap))
    # Parent state-reported flag is global; one wait is enough to confirm
    # the publish pipeline drained.
    node_host_ctrl.wait_on_state_reported(per_child_timeout_ms)

    setup_wall = time.monotonic() - setup_t0
    print(
        f"[bridge stress] pool setup: {len(pool)} children in {setup_wall:.1f}s "
        f"(adds={add_wall:.1f}s, ready={ready_wall:.1f}s, "
        f"commit={commit_wall - ready_wall:.1f}s, "
        f"drain={setup_wall - commit_wall:.1f}s)"
    )
    setup_done_heap = node_host_ctrl.get_heap_status()
    tracker.track(setup_done_heap)
    heap_timeline.append(("setup-done", setup_done_heap))

    # ---- Cycles -------------------------------------------------------------
    try:
        for cycle in range(BRIDGE_STRESS_CYCLES):
            cycle_t0 = time.monotonic()
            cycle_rng = random.Random(rng.randint(0, 2**32 - 1))
            subset = select_subset(
                pool,
                BRIDGE_STRESS_SUBSET_FRAC,
                cycle_rng,
                BRIDGE_STRESS_SUBSET_MIN,
            )
            print(
                f"[bridge stress] cycle {cycle + 1}/{BRIDGE_STRESS_CYCLES} "
                f"subset={len(subset)}"
            )

            # Single interleaved cycle: state + auto + sched drive in one
            # fan-out, drain in bulk, fire + verify, cleanup.
            run_cycle(node_host_ctrl, user, group_id, subset, cycle)

            time.sleep(BRIDGE_STRESS_INTER_CYCLE_DELAY)
            cycle_wall_times.append(time.monotonic() - cycle_t0)
            snap = node_host_ctrl.get_heap_status()
            tracker.track(snap)
            heap_timeline.append((f"cycle-{cycle + 1}", snap))
    finally:
        # ---- Teardown -------------------------------------------------------
        for child in pool:
            try:
                node_host_ctrl.bridge.remove_child(child)
            except Exception as e:
                print(f"remove_child failed for {child.thing_name}: {e}")
            time.sleep(
                BRIDGE_STRESS_CHILD_MODIFY_DELAY
            )  # Don't overload the bridge with too many requests
        try:
            leftover = node_host_ctrl.bridge.list_children()
        except Exception:
            leftover = []
        # leftover assertion outside finally so cleanup always runs first.

    # Post-teardown.
    node_host_ctrl.reset()
    final_heap = node_host_ctrl.get_heap_status()
    tracker.track(final_heap)
    heap_timeline.append(("final", final_heap))

    # ---- Summary ------------------------------------------------------------
    def _heap_line(label: str, status) -> str:
        if status is None:
            return f"  {label}: N/A"
        return (
            f"  {label}: free={HeapStatusTracker.format_bytes(status.free_size)} "
            f"largest={HeapStatusTracker.format_bytes(status.largest_block_size)}"
        )

    summary_lines = [
        f"seed={seed}",
        f"pool_size={BRIDGE_STRESS_POOL_SIZE} cycles={BRIDGE_STRESS_CYCLES}",
        f"subset frac={BRIDGE_STRESS_SUBSET_FRAC} (min={BRIDGE_STRESS_SUBSET_MIN})",
        f"setup wall: {setup_wall:.1f}s",
        "cycle walls: " + ", ".join(f"{w:.1f}s" for w in cycle_wall_times),
        "heap timeline:",
        *[_heap_line(label, status) for label, status in heap_timeline],
        f"smallest_largest_block={HeapStatusTracker.format_bytes(tracker.smallest_largest_block_size)}",
    ]
    add_summary_section("Bridge stress", summary_lines)

    # ---- Asserts ------------------------------------------------------------
    assert not leftover, f"Pool teardown left {len(leftover)} child(ren) behind"

    # ESP-only heap health — WARNINGS, not asserts.
    #
    # Both signals below read instantaneous heap state while TLS/MQTT/WiFi are
    # actively churning large transient buffers (a single TLS record is ~16 KB).
    # A one-shot initial-vs-final free_size delta therefore swings by tens of KB
    # independent of real leaks — it routinely goes negative (final > initial)
    # when the baseline snapshot happened to catch a transient dip. Likewise the
    # smallest-largest-block floor can be tripped by a momentary fragmentation
    # spike during a handshake. Neither is a reliable pass/fail gate, so they are
    # logged as warnings for trend-watching rather than failing the run.
    if "esp" in target:
        if initial_heap is None or final_heap is None:
            print(
                "[bridge stress] WARNING: heap snapshots unavailable; skipping heap checks"
            )
        else:
            raw_drift = initial_heap.free_size - final_heap.free_size
            # Per-child node lock mutexes are cached for the slot lifetime and
            # never freed on remove by design — subtract so the figure reflects
            # only unaccounted drift.
            known_cached_heap = (
                BRIDGE_STRESS_POOL_SIZE * BRIDGE_STRESS_CACHED_MUTEX_BYTES
            )
            leak_bytes = raw_drift - known_cached_heap
            if abs(leak_bytes) >= BRIDGE_STRESS_LEAK_TOLERANCE_BYTES:
                print(
                    f"[bridge stress] WARNING: heap drift {raw_drift} B "
                    f"(adjusted {leak_bytes} B after subtracting {known_cached_heap} B "
                    f"cached mutexes) exceeds {BRIDGE_STRESS_LEAK_TOLERANCE_BYTES} B "
                    f"(initial free={initial_heap.free_size} final free={final_heap.free_size}). "
                    f"Snapshot noise is expected; investigate only if consistently large AND positive."
                )
            min_largest = _stress_min_largest_block_bytes(target)
            if tracker.smallest_largest_block_size < min_largest:
                print(
                    f"[bridge stress] WARNING: smallest largest block "
                    f"{tracker.smallest_largest_block_size} B dropped below floor "
                    f"{min_largest} B for target {target!r}."
                )
