# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared scheduling helpers used by both ``test_firmware`` (self node) and
``test_bridge`` (bridge children). Mirrors ``automation`` in shape:

* ``node_host_ctrl`` is the schedule surface — either a ``NodeHostCtrl`` (self) or a
  ``BridgeChildHostCtrl``. ``update_param`` / ``get_param`` /
  ``clear_on_sched_details`` / ``wait_on_sched_details`` all live here.
* ``parent_node_host_ctrl`` is the parent ``NodeHostCtrl`` (or the same handle as
  ``node_host_ctrl`` for self runs). Used for ``state_reported`` and time-control
  calls — the schedule trigger callback fires against the parent's work queue
  and reports through the parent's MQTT session, and time-control is a
  parent-only operation.

For the self case callers pass ``node_host_ctrl == parent_node_host_ctrl``.
"""

from .node_host_ctrl import wait_for_node_state_reported


def get_scheduling_node_config():
    """Standard node config used by scheduling tests (curtain + light + aircon)."""
    return {
        "devices": [
            {
                "id": "curtain",
                "type": "esp.device.curtain",
                "params": [
                    {
                        "id": "Position",
                        "type": "esp.param.position",
                        "data_type": "int",
                        "value": 50,
                        "bounds": {"min": 0, "max": 100, "step": 1},
                        "properties": ["read", "write"],
                    }
                ],
            },
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
                        "properties": ["read", "write"],
                    },
                ],
            },
            {
                "id": "aircon",
                "type": "esp.device.aircon",
                "params": [
                    {
                        "id": "Power",
                        "type": "esp.param.power",
                        "data_type": "bool",
                        "value": True,
                        "properties": ["read", "write"],
                    },
                    {
                        "id": "Temperature",
                        "type": "esp.param.temperature",
                        "data_type": "float",
                        "value": 22.5,
                        "bounds": {"min": 16.0, "max": 30.0, "step": 0.5},
                        "properties": ["read", "write"],
                    },
                ],
            },
        ],
        "services": [],
        "tags": {},
    }


def scheduling_set_anti_action(
    node_host_ctrl, parent_node_host_ctrl, anti_action: dict
):
    """Drive ``anti_action`` (a ``{device: {param: value}}`` dict) onto
    ``node_host_ctrl``. Only updates params that aren't already at the target
    value — pushing the same value firmware-side is a no-op and never
    fires ``state_reported``, so a blind update + wait would hang.

    For self runs pass ``node_host_ctrl == parent_node_host_ctrl``."""
    changed = False
    for device_id, device_params in anti_action.items():
        for param_id, param_value in device_params.items():
            current = node_host_ctrl.get_param(device_id, param_id)
            assert current is not None, (
                f"Param {device_id}::{param_id} not present on node_host_ctrl"
            )
            if current.value == param_value:
                continue
            if not changed:
                parent_node_host_ctrl.clear_on_state_reported()
                changed = True
            assert node_host_ctrl.update_param(device_id, param_id, param_value), (
                f"Failed to update param {device_id}::{param_id} to {param_value}"
            )
    if changed:
        wait_for_node_state_reported(parent_node_host_ctrl)


def scheduling_verify_action(node_host_ctrl, action: dict):
    """Assert every (device, param) in ``action`` reads back the expected
    value on ``node_host_ctrl``."""
    for device_id, device_params in action.items():
        for param_id, param_value in device_params.items():
            got = node_host_ctrl.get_param(device_id, param_id)
            assert got is not None, (
                f"Param {device_id}::{param_id} not present on node_host_ctrl"
            )
            assert got.value == param_value, (
                f"Param {device_id}::{param_id} value does not match, "
                f"expected {param_value} but got {got.value}"
            )


def scheduling_verify_schedule(
    node_host_ctrl, parent_node_host_ctrl, trigger_time, schedule: dict
):
    """Drive a single schedule: install the anti_action, advance the
    parent's clock to ``trigger_time``, wait for the resulting state
    report, and assert the action landed on ``node_host_ctrl``."""
    scheduling_set_anti_action(
        node_host_ctrl, parent_node_host_ctrl, schedule["anti_action"]
    )

    parent_node_host_ctrl.time_control_set_time(trigger_time)
    assert parent_node_host_ctrl.get_current_time() == trigger_time, (
        "Device time does not match set time"
    )
    wait_for_node_state_reported(parent_node_host_ctrl)

    scheduling_verify_action(node_host_ctrl, schedule["action"])


def scheduling_set_schedules(
    user, group_id, node_host_ctrl, thing_name, schedules: list, timeout_ms: int = 5000
):
    """Push ``schedules`` to ``thing_name`` via cloud and gate on the
    per-node ``sched_details`` flag firing on ``node_host_ctrl`` (works for
    both self ``NodeHostCtrl`` and ``BridgeChildHostCtrl``)."""
    schedule_data = {
        "schedule": {
            "Schedules": [
                {
                    "name": schedule["name"],
                    "id": schedule["id"],
                    "triggers": schedule["triggers"],
                    "action": schedule["action"],
                }
                for schedule in schedules
            ]
        }
    }
    node_host_ctrl.clear_on_sched_details()
    user.set_node_schedule(
        group_id=group_id,
        subgroup_id=None,
        node_id=thing_name,
        schedule_data=schedule_data,
    )
    assert node_host_ctrl.wait_on_sched_details(timeout_ms), (
        f"Node did not receive schedule details for thing {thing_name!r}"
    )
