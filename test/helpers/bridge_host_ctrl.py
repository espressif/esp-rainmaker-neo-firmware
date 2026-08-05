# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Bridge-specific helpers, analogous to node_host_ctrl.py.

The per-child state-report bit lives on the parent's ``NodeHostCtrl`` global
(``wait_on_state_reported``) — the firmware publish-complete signal is not
context-scoped. The per-child bits this
module orchestrates are ``online``, ``group_info``, and
``node_config_sent``.
"""

from datetime import datetime as dt
from time import sleep, monotonic

from .node_host_ctrl import (
    read_named_shadow,
    operate_with_named_shadow_connection,
)
from .config import _are_values_equal


def wait_for_param_value(
    node_host_ctrl, device_id, param_id, expected, *, timeout_ms=10000, poll_ms=200
):
    """
    Poll ``get_param`` until it equals ``expected`` or timeout; return the last
    ParamData read (possibly None/stale on timeout, so the caller's assert still
    prints a useful value).

    Group-control and child cloud updates gate on the PARENT's state_reported
    flag, which fires on publish-complete regardless of which node the update
    routed to (see wait_for_cloud_group_control_bridge). The routed node's param
    can therefore lag that flag, so a single get_param races the apply. Polling
    closes that window without weakening the check — the value still must reach
    ``expected``.
    """
    deadline = monotonic() + timeout_ms / 1000.0
    param = node_host_ctrl.get_param(device_id, param_id)
    while (param is None or param.value != expected) and monotonic() < deadline:
        sleep(poll_ms / 1000.0)
        param = node_host_ctrl.get_param(device_id, param_id)
    return param


def start_bridge_child(
    parent_node_host_ctrl,
    child,
    timeout_ms: int = 15000,
    wait_on_node_config_sent: bool = True,
):
    """
    Bridge-child analog of ``start_node_host_ctrl``.

    Caller must have already attached the child (``parent.bridge.add_child``)
    and set its config via ``child.set_config(...)``.

    Clears flags, commits, then waits for the per-child node-config + group-info
    bits and the parent's global state-reported bit.
    """
    parent_node_host_ctrl.clear_on_state_reported()
    child.clear_on_node_config_sent()
    child.clear_on_group_info()

    assert child.commit_devices(), "commit_devices failed"

    assert child.wait_on_group_info(timeout_ms), "Child did not receive group info"
    if wait_on_node_config_sent:
        assert child.wait_on_node_config_sent(timeout_ms), (
            "Child did not send its node config"
        )
    assert parent_node_host_ctrl.wait_on_state_reported(timeout_ms), (
        "Parent did not report state after child commit"
    )


def wait_for_bridge_child_state_reported(
    parent_node_host_ctrl, timeout_ms: int = 5000
) -> bool:
    """
    Wait for the parent's global state-reported bit. Use after a per-child
    operation (param update / commit) that ends in a state publish.

    Caller is responsible for clearing the bit before triggering the op.
    """
    return parent_node_host_ctrl.wait_on_state_reported(timeout_ms)


def read_child_named_shadow(user, child, group_info_str: str, timeout_ms: int = 5000):
    """
    Read the named shadow scoped to a bridge child's thing.

    Subscribes to the child's ``/get/accepted`` topic for the read,
    then unsubscribes — without the subscribe step, the ``get`` request
    publishes but the response is never delivered and the read hangs.
    """
    result_holder = {}

    def _op():
        result_holder["value"] = read_named_shadow(
            user, child.thing_name, group_info_str, timeout_ms
        )

    operate_with_named_shadow_connection(
        _op, user, child.thing_name, group_info_str, timeout_ms
    )
    return result_holder["value"]


def poll_child_named_shadow_until(
    user,
    child,
    group_info_str: str,
    predicate,
    *,
    max_retries: int = 30,
    delay_s: float = 1.0,
    read_timeout_ms: int = 5000,
):
    """
    Poll the child's named shadow inside a single subscribe/unsubscribe cycle
    until ``predicate(shadow)`` returns truthy or attempts are exhausted.

    Avoids the per-read subscribe/unsubscribe churn of calling
    ``read_child_named_shadow`` in a loop.

    Returns ``(matched, last_shadow)``.
    """
    state = {"matched": False, "last_shadow": None}

    def _op():
        for _ in range(max_retries):
            shadow = read_named_shadow(
                user, child.thing_name, group_info_str, read_timeout_ms
            )
            state["last_shadow"] = shadow
            if predicate(shadow):
                state["matched"] = True
                return
            sleep(delay_s)

    operate_with_named_shadow_connection(
        _op, user, child.thing_name, group_info_str, read_timeout_ms
    )
    return state["matched"], state["last_shadow"]


def wait_for_cloud_group_control_bridge(
    parent_node_host_ctrl,
    user,
    group_id,
    payload,
    *,
    subgroup_id=None,
    timeout_ms=10000,
):
    """
    Publish a device-type-keyed group control payload (primary or subgroup);
    wait for the parent's global state-report pipeline to flush.

    The parent's ``state_reported`` flag fires once per publish-complete
    regardless of which node (parent / child) the update routed to, so it
    is the right gate even for child-only subgroup pushes.
    """
    parent_node_host_ctrl.clear_on_state_reported()
    assert user.mqtt_publish_to_group_control(
        group_id=group_id, data=payload, subgroup_id=subgroup_id
    ), "Failed to publish group control payload"
    assert parent_node_host_ctrl.wait_on_state_reported(timeout_ms), (
        "Parent did not report state after group control publish"
    )


def wait_for_cloud_params_update_for_child(
    parent_node_host_ctrl, user, child, group_id, cloud_payload, timeout_ms: int = 5000
):
    """
    Publish a params update addressed to the child's thing topic; wait for the
    parent's state-report pipeline to flush.
    """
    parent_node_host_ctrl.clear_on_state_reported()
    assert user.mqtt_publish_to_topic(
        thing_name=child.thing_name,
        topic_name=f"params-{group_id}/params",
        data=cloud_payload,
    ), "Failed to publish params update to child topic"
    assert parent_node_host_ctrl.wait_on_state_reported(timeout_ms), (
        "Parent did not report state after child cloud update"
    )


def timeseries_publish_test_data_for_child(parent_node_host_ctrl, child, data_points):
    """
    Bridge-child analog of test_firmware._timeseries_publish_test_data.

    Drives ``child.update_param`` instead of the parent's update_param, but
    routes flag waits through the parent (the timeseries-reported and
    state-reported flags are global to the parent's host_ctrl).
    """
    timestamp_groups = {}
    for point in data_points:
        timestamp_groups.setdefault(point["timestamp"], []).append(point)

    base_time = min(timestamp_groups.keys())
    parent_node_host_ctrl.time_control_set_time(dt.fromtimestamp(base_time))

    sorted_timestamps = sorted(timestamp_groups.keys())
    for i, timestamp in enumerate(sorted_timestamps):
        group_points = timestamp_groups[timestamp]

        parent_node_host_ctrl.clear_on_timeseries_reported()
        parent_node_host_ctrl.clear_on_state_reported()

        parent_node_host_ctrl.time_control_set_time(dt.fromtimestamp(timestamp))
        for point in group_points:
            assert child.update_param(
                point["device"], point["param"], point["value"]
            ), f"Child update_param failed for {point['device']}::{point['param']}"

        assert parent_node_host_ctrl.wait_on_state_reported(5000), (
            f"Parent did not report state after child timestamp {i + 1}/{len(sorted_timestamps)}"
        )
        assert parent_node_host_ctrl.wait_on_timeseries_reported(5000), (
            f"Parent did not report timeseries after child timestamp {i + 1}/{len(sorted_timestamps)}"
        )


def _shadow_matches_child_config(shadow: dict, child_config: dict) -> bool:
    """
    True iff every device/param in ``child_config`` appears in ``shadow`` with
    the expected value.
    """
    for device in child_config.get("devices", []):
        device_shadow = shadow.get(device["id"])
        if device_shadow is None:
            return False
        for param in device.get("params", []):
            if not _are_values_equal(device_shadow.get(param["id"]), param["value"]):
                return False
    return True


def assert_child_named_shadow_matches_config(
    user,
    child,
    group_info_str: str,
    child_config: dict,
    *,
    max_retries: int = 8,
    delay_s: float = 1.0,
):
    """
    Read + verify the child's named shadow against ``child_config`` with
    retries. Useful for any bridge test that gates on a per-child shadow
    converging after a state report.

    Rationale: ``state_reported`` fires on the parent's publish ack — for
    a child publish (e.g. on subgroup migration republish), parent ack is
    not the same instant as the child's new shadow being readable through
    the cloud shadow service. Retry absorbs that propagation window.

    Returns the matched shadow dict on success; raises ``AssertionError``
    with a detailed message after ``max_retries`` failed attempts.
    """
    matched, last_shadow = poll_child_named_shadow_until(
        user,
        child,
        group_info_str,
        lambda s: _shadow_matches_child_config(s, child_config),
        max_retries=max_retries,
        delay_s=delay_s,
    )
    if matched:
        return last_shadow

    # Final attempt failed — re-run asserts on last_shadow for a detailed error.
    for device in child_config.get("devices", []):
        device_id = device["id"]
        device_shadow = (last_shadow or {}).get(device_id)
        assert device_shadow is not None, (
            f"Shadow at group_info={group_info_str!r} missing device {device_id!r} "
            f"after {max_retries} retries (last shadow={last_shadow!r})"
        )
        for param in device.get("params", []):
            assert _are_values_equal(device_shadow.get(param["id"]), param["value"]), (
                f"Shadow at group_info={group_info_str!r} {device_id}::{param['id']} "
                f"mismatch after {max_retries} retries: "
                f"got {device_shadow.get(param['id'])!r} expected {param['value']!r}"
            )
    return last_shadow
