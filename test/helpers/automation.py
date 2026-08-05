# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared automation/trigger helpers used by both ``test_firmware`` (self node_host_ctrl)
and ``test_bridge`` (bridge children).

The two driver helpers (``automation_create_and_test`` and
``concurrent_automation_test``) take a ``node_host_ctrl`` handle plus a separate
``parent_node_host_ctrl``:

* ``node_host_ctrl`` is the trigger/param surface — either a ``NodeHostCtrl`` (self) or
  a ``BridgeChildHostCtrl``. ``update_param`` / ``get_param`` /
  ``clear_on_trigger_details`` / ``wait_on_trigger_details`` all live here.
* ``parent_node_host_ctrl`` is the parent ``NodeHostCtrl`` (or the same handle as
  ``node_host_ctrl`` for self runs). Used for ``state_reported`` and
  ``notification_sent`` flags — both are global to the parent's MQTT session
  even for child-scoped operations (see ``bridge_host_ctrl`` notes).

For the self case callers pass ``node_host_ctrl == parent_node_host_ctrl`` so the call
sites collapse to the same NodeHostCtrl, preserving the original test_firmware
semantics.
"""

import json
from time import sleep

from .config import _get_new_param_value


def get_automation_node_config():
    """Standard node_host_ctrl config used by automation tests (light + temp_sensor + switch)."""
    return {
        "devices": [
            {
                "id": "light",
                "type": "esp.device.light",
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
                        "value": 50,
                        "bounds": {"min": 0, "max": 100, "step": 1},
                        "properties": ["read", "write"],
                    },
                ],
            },
            {
                "id": "temp_sensor",
                "type": "esp.device.temperature",
                "params": [
                    {
                        "id": "Temperature",
                        "type": "esp.param.temperature",
                        "data_type": "float",
                        "value": 25.0,
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
        "services": [],
        "tags": {},
    }


def automation_get_test_value_for_trigger(trigger, bounds=None):
    """Get the test value to set for a trigger to be met."""
    if (trigger["operator"] == "gt" or trigger["operator"] == "ge") and bounds:
        return trigger["value"] + bounds["step"]
    elif (trigger["operator"] == "lt" or trigger["operator"] == "le") and bounds:
        return trigger["value"] - bounds["step"]
    elif trigger["operator"] == "eq":
        return trigger["value"]
    elif trigger["operator"] == "ne":
        return _get_new_param_value(trigger["value"], bounds)

    raise ValueError(
        f"Invalid operator: {trigger['operator']} or operator has no bounds"
    )


def wire_triggers(triggers, id_prefix=None):
    """Build wire-format triggers (path key) from device/param triggers.

    If id_prefix given, assigns id=f"{id_prefix}~{i}" to each trigger;
    otherwise preserves any existing "id" field from the input.
    """
    out = []
    for i, trigger in enumerate(triggers):
        wire = {k: v for k, v in trigger.items() if k not in ("device", "param")}
        wire["path"] = f"{trigger['device']}.{trigger['param']}"
        if id_prefix is not None:
            wire["id"] = f"{id_prefix}~{i}"
        out.append(wire)
    return out


def wire_actions(actions):
    """Build wire-format action targets (path key) from device/param actions."""
    out = []
    for action in actions:
        wire = {k: v for k, v in action.items() if k not in ("device", "param")}
        wire["path"] = f"{action['device']}.{action['param']}"
        out.append(wire)
    return out


def automation_create_and_test(
    node_host_ctrl,
    user,
    group_id,
    thing_name,
    automation_name,
    triggers,
    trigger_bounds,
    actions,
    anti_actions,
    parent_node_host_ctrl=None,
):
    """
    Create automation with triggers and test that it executes actions correctly.

    ``node_host_ctrl`` is the trigger surface (NodeHostCtrl or BridgeChildHostCtrl).
    ``thing_name`` is the cloud-side thing the trigger is provisioned against
    (self thing for parent, child thing for a bridge child).
    ``parent_node_host_ctrl`` carries the global state_reported /
    notification_sent flags; defaults to ``node_host_ctrl`` for the self case.
    """
    if parent_node_host_ctrl is None:
        parent_node_host_ctrl = node_host_ctrl

    # Clean up any existing automations and triggers
    user.delete_all_automations(group_id=group_id)
    try:
        node_host_ctrl.clear_on_trigger_details()
        user.delete_node_trigger(group_id=group_id, node_id=thing_name)
        assert node_host_ctrl.wait_on_trigger_details(10000), (
            "Node did not receive trigger details after deleting trigger"
        )
    except Exception:
        pass  # Ignore if no triggers exist

    # Set node_host_ctrl values to anti-values of actions
    changed_any_params = False
    for action in anti_actions:
        device_id = action["device"]
        device_param = action["param"]
        device_value = action["value"]

        if node_host_ctrl.get_param(device_id, device_param).value != device_value:
            if not changed_any_params:
                parent_node_host_ctrl.clear_on_state_reported()
                changed_any_params = True
            assert node_host_ctrl.update_param(device_id, device_param, device_value), (
                f"Failed to update param {device_id}::{device_param} to {device_value}"
            )
    if changed_any_params:
        assert parent_node_host_ctrl.wait_on_state_reported(10000), (
            "Node did not report state after setting node_host_ctrl values"
        )

    # Get automation ID
    empty_automation = {
        "name": automation_name,
        "conditions": {"and": []},
        "actions": {"targets": []},
    }
    automation_id = user.create_automation(
        group_id=group_id, automation_data=empty_automation
    ).get("automation_id")
    assert automation_id is not None, (
        f"Failed to create empty automation {automation_name}"
    )
    print(f"Created empty automation with ID: {automation_id}")

    # Set node_host_ctrl triggers and conditions using the thing name and automation ID
    updated_triggers = []
    and_condition = []
    test_values = []

    w_triggers = wire_triggers(triggers, id_prefix=f"{thing_name}~{automation_id}")
    for trigger, wire, trigger_bound in zip(triggers, w_triggers, trigger_bounds):
        updated_triggers.append({**trigger, "id": wire["id"]})
        and_condition.append(wire["id"])
        test_values.append(
            automation_get_test_value_for_trigger(trigger, trigger_bound)
        )

    # Update automation with conditions and actions
    automation_data = {
        "name": automation_name,
        "description": f"Test automation for {automation_name}",
        "conditions": {"and": and_condition},
        "actions": {"targets": wire_actions(actions)},
    }
    trigger_data = {
        "triggers": w_triggers,
    }

    print(f"Updating automation: {json.dumps(automation_data, indent=2)}")
    assert (
        user.update_automation(
            group_id=group_id,
            automation_id=automation_id,
            automation_data=automation_data,
        )
        is not None
    ), "Failed to update automation"

    print(f"Setting triggers on node_host_ctrl: {json.dumps(trigger_data, indent=2)}")
    node_host_ctrl.clear_on_trigger_details()  # Clear any stale flags
    assert user.set_node_trigger(
        group_id=group_id, node_id=thing_name, trigger_data=json.dumps(trigger_data)
    ), "Failed to set node_host_ctrl triggers"

    # Wait for node_host_ctrl to receive trigger details
    assert node_host_ctrl.wait_on_trigger_details(10000), (
        "Node did not receive trigger details"
    )

    # Clear any stale flags
    parent_node_host_ctrl.clear_on_notification_sent()
    parent_node_host_ctrl.clear_on_state_reported()

    # Update parameters to trigger the automation
    for trigger, bounds, test_value in zip(
        updated_triggers, trigger_bounds, test_values
    ):
        device_id = trigger["device"]
        device_param = trigger["param"]
        if node_host_ctrl.get_param(device_id, device_param).value == test_value:
            # update to a new value to force trigger state to be updated
            new_value = _get_new_param_value(test_value, bounds)
            assert node_host_ctrl.update_param(device_id, device_param, new_value), (
                f"Failed to update param {device_id}::{device_param} to {new_value}"
            )
        assert node_host_ctrl.update_param(device_id, device_param, test_value), (
            f"Failed to update param {device_id}::{device_param} to {test_value}"
        )
    assert parent_node_host_ctrl.wait_on_state_reported(10000), (
        "Node did not report state after updating parameters"
    )

    # Wait for notifications to be sent
    assert parent_node_host_ctrl.wait_on_notification_sent(10000), (
        "Node did not send notification"
    )

    # Wait for node_host_ctrl to receive updates and report its state
    assert parent_node_host_ctrl.wait_on_state_reported(10000), (
        "Node did not report state after sending notifications"
    )

    for action in actions:
        device_id = action["device"]
        device_param = action["param"]
        device_value = action["value"]
        node_value = node_host_ctrl.get_param(device_id, device_param).value
        assert node_value == device_value, (
            f"Automation actions were not executed as expected. Expected: {device_id}::{device_param} = {device_value} but got {node_value}"
        )

    # Delete the automation and triggers
    user.delete_automation(group_id=group_id, automation_id=automation_id)
    try:
        node_host_ctrl.clear_on_trigger_details()
        user.delete_node_trigger(group_id=group_id, node_id=thing_name)
        assert node_host_ctrl.wait_on_trigger_details(10000), (
            "Node did not receive trigger details after deleting trigger"
        )
    except Exception:
        pass  # Ignore if no triggers exist


def concurrent_automation_test(
    node_host_ctrl,
    user,
    group_id,
    thing_name,
    automation_configs,
    trigger_changes,
    expected_results,
    setup_anti_actions=True,
    parent_node_host_ctrl=None,
):
    """
    Run multiple automations concurrently against ``node_host_ctrl``. See
    ``automation_create_and_test`` for parameter semantics.
    """
    if parent_node_host_ctrl is None:
        parent_node_host_ctrl = node_host_ctrl

    # Clean up any existing automations and triggers
    user.delete_all_automations(group_id=group_id)
    try:
        node_host_ctrl.clear_on_trigger_details()
        user.delete_node_trigger(group_id=group_id, node_id=thing_name)
        assert node_host_ctrl.wait_on_trigger_details(10000), (
            "Node did not receive trigger details after deleting trigger"
        )
    except Exception:
        pass

    # Create all automations and collect their IDs and trigger configs
    automation_ids = []
    all_triggers = []

    for config in automation_configs:
        # Create empty automation first
        empty_automation = {
            "name": config["name"],
            "conditions": {"and": []},
            "actions": {"targets": []},
        }
        automation_id = user.create_automation(
            group_id=group_id, automation_data=empty_automation
        ).get("automation_id")
        automation_ids.append(automation_id)

        # Build triggers with proper IDs
        automation_triggers = wire_triggers(
            config["triggers"], id_prefix=f"{thing_name}~{automation_id}"
        )
        and_conditions = [w["id"] for w in automation_triggers]
        all_triggers.extend(automation_triggers)

        # Update automation with conditions and actions
        automation_data = {
            "name": config["name"],
            "description": f"Concurrent test automation: {config['name']}",
            "conditions": {"and": and_conditions},
            "actions": {"targets": wire_actions(config["actions"])},
        }

        print(f"Creating concurrent automation: {config['name']}")
        assert (
            user.update_automation(
                group_id=group_id,
                automation_id=automation_id,
                automation_data=automation_data,
            )
            is not None
        )

    # Set all triggers on the node_host_ctrl at once
    trigger_data = {"triggers": all_triggers}
    print("Setting all concurrent triggers on node_host_ctrl")
    node_host_ctrl.clear_on_trigger_details()
    assert user.set_node_trigger(
        group_id=group_id, node_id=thing_name, trigger_data=json.dumps(trigger_data)
    ), "Failed to set concurrent triggers"

    # Wait for node_host_ctrl to receive all trigger details
    assert node_host_ctrl.wait_on_trigger_details(10000), (
        "Node did not receive concurrent trigger details"
    )

    # Set initial anti-action states if requested
    if setup_anti_actions:
        anti_actions = []
        for key, value in expected_results.items():
            device_id, param_id = key.split(".")
            if isinstance(value, bool):
                anti_value = not value
            elif isinstance(value, (int, float)):
                anti_value = value - 10 if value > 10 else value + 10
            else:
                anti_value = value
            anti_actions.append(
                {"device": device_id, "param": param_id, "value": anti_value}
            )

        changed_any_params = False
        for action in anti_actions:
            device_id = action["device"]
            device_param = action["param"]
            device_value = action["value"]

            if node_host_ctrl.get_param(device_id, device_param).value != device_value:
                if not changed_any_params:
                    parent_node_host_ctrl.clear_on_state_reported()
                    changed_any_params = True
                assert node_host_ctrl.update_param(
                    device_id, device_param, device_value
                ), (
                    f"Failed to set anti-action {device_id}::{device_param} to {device_value}"
                )

        if changed_any_params:
            assert parent_node_host_ctrl.wait_on_state_reported(10000), (
                "Node did not report state after setting anti-action values"
            )

    parent_node_host_ctrl.clear_on_notification_sent()
    parent_node_host_ctrl.clear_on_state_reported()

    print("Triggering multiple automations concurrently...")
    for change in trigger_changes:
        device_id = change["device"]
        param_id = change["param"]
        value = change["value"]
        assert node_host_ctrl.update_param(device_id, param_id, value), (
            f"Failed to update {device_id}::{param_id} to {value}"
        )

    assert parent_node_host_ctrl.wait_on_state_reported(10000), (
        "Node did not report state after concurrent triggers"
    )

    assert parent_node_host_ctrl.wait_on_notification_sent(10000), (
        "Node did not send notifications for concurrent automations"
    )

    print("Waiting for all concurrent automation actions to be executed...")
    sleep(8)

    print("Verifying concurrent automation results...")
    all_actions_executed = True
    for key, expected_value in expected_results.items():
        device_id, param_id = key.split(".")
        actual_value = node_host_ctrl.get_param(device_id, param_id).value
        if actual_value != expected_value:
            print(
                f"Concurrent automation failed: {key} = {actual_value}, expected {expected_value}"
            )
            all_actions_executed = False
        else:
            print(f"Concurrent automation success: {key} = {actual_value}")

    assert all_actions_executed, (
        "Not all concurrent automation actions were executed correctly"
    )

    for automation_id in automation_ids:
        user.delete_automation(group_id=group_id, automation_id=automation_id)
    node_host_ctrl.clear_on_trigger_details()
    user.delete_node_trigger(group_id=group_id, node_id=thing_name)
    assert node_host_ctrl.wait_on_trigger_details(10000), (
        "Node did not receive trigger details after deleting trigger"
    )

    print("Concurrent automation test completed successfully")
