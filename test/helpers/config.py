# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared helpers for node-config-driven assertions and param wrangling.

Lifted verbatim from test_firmware.py so test_bridge.py (and any future
test module that drives a node-shaped host_ctrl) can reuse them. Everything
here is duck-typed on the host_ctrl: anything exposing
``set_config`` / ``get_param`` / ``get_tag_value`` / ``update_param``
works — including ``BridgeChildHostCtrl``.
"""

import json
from pathlib import Path
from random import randint

from host_ctrl_python.host_ctrl import NodeConfig


def _get_new_param_value(param_value, param_bounds):
    """
    Get a new random param value.
    """
    min_val, max_val, step = 0, 100, 1
    if param_bounds:
        min_val = param_bounds.get("min", 0)
        max_val = param_bounds.get("max", 100)
        step = param_bounds.get("step", 1)

    param_value_type = type(param_value)
    if param_value_type is bool:
        return not param_value
    elif param_value_type is int or param_value_type is float:
        new_val = param_value
        while new_val == param_value:
            new_val = min_val + randint(0, int((max_val - min_val) / step)) * step

        if param_value_type is int:
            new_val = int(new_val)
        elif param_value_type is float:
            new_val = round(new_val, 3)

        return new_val
    elif param_value_type is str:
        return f"{param_value}-modified"
    elif param_value_type is dict:
        param_value["new_key"] = "new_value"
    elif param_value_type is list:
        param_value.append("new_value")
    return param_value


def _are_values_equal(value1, value2) -> bool:
    """
    Check if the values are equal.
    """
    if type(value1) is not type(value2):
        return False
    if type(value1) is int or type(value1) is float:
        return abs(value1 - value2) < 0.001  # accept 3 decimal places of precision
    return value1 == value2


def _verify_config_with_named_shadow_reported(node_config, named_shadow_reported):
    """
    Verify the node config with the named shadow reported.
    """

    config_devices = node_config.get("devices", [])

    for config_device in config_devices:
        device_id = config_device.get("id")
        device_params = config_device.get("params", {})
        named_shadow_device = named_shadow_reported.get(device_id)
        assert named_shadow_device is not None, (
            f"Device {device_id} not found in named shadow"
        )

        for param in device_params:
            param_id = param.get("id")
            full_param_id = f"{device_id}::{param_id}"
            param_value = param.get("value")

            named_shadow_param_value = named_shadow_device.get(param_id)
            assert named_shadow_param_value is not None, (
                f"Param {full_param_id} not found in named shadow"
            )
            assert type(named_shadow_param_value) is type(param_value), (
                f"Param {full_param_id} value type does not match in named shadow, expected {type(param_value)} but got {type(named_shadow_param_value)}"
            )
            assert _are_values_equal(named_shadow_param_value, param_value), (
                f"Param {full_param_id} value does not match in named shadow, expected {param_value} but got {named_shadow_param_value}"
            )


# Default tags added by the firmware on node create (see node_model.c)
DEFAULT_TAG_KEYS = ("name", "type", "fw_version", "model")


def _verify_default_tags_uploaded(node_host_ctrl, indexed_shadow_reported):
    """
    Verify that the default tags (name, type, fw_version, model) are present in the
    uploaded indexed shadow and match the values reported by the node.
    """
    shadow_data = indexed_shadow_reported.get("data")
    assert shadow_data is not None, "No data in indexed shadow"
    shadow_device = shadow_data.get("device")
    assert shadow_device is not None, "No node data in indexed shadow"
    shadow_tags = shadow_device.get("t")
    assert shadow_tags is not None, "No node tags in indexed shadow"

    for tag_key in DEFAULT_TAG_KEYS:
        expected_value = node_host_ctrl.get_tag_value(tag_key)
        assert expected_value is not None, f"Default tag {tag_key} not set on node"
        uploaded_value = shadow_tags.get(tag_key)
        assert _are_values_equal(uploaded_value, expected_value), (
            f"Default tag {tag_key} value does not match in uploaded shadow, "
            f"expected {expected_value} but got {uploaded_value}"
        )


def _verify_config_with_indexed_shadow_reported(node_config, indexed_shadow_reported):
    """
    Verify the node config with the indexed shadow reported.
    """

    config_devices = node_config.get("devices", [])
    config_tags = node_config.get("tags", {})

    # Check firmware tags
    temp = indexed_shadow_reported.get("data")
    assert temp is not None, "No data in indexed shadow"
    temp = temp.get("device")
    assert temp is not None, "No node data in indexed shadow"
    temp = temp.get("t")
    assert temp is not None, "No node tags in indexed shadow"
    for config_tag_name, config_tag_value in config_tags.items():
        indexed_shadow_tag_value = temp.get(config_tag_name)
        assert _are_values_equal(indexed_shadow_tag_value, config_tag_value), (
            f"Tag {config_tag_name} value does not match in indexed shadow, expected {config_tag_value} but got {indexed_shadow_tag_value}"
        )

    # Check indexed parameters
    indexed_shadow_devices = indexed_shadow_reported.get("params", {})
    for config_device in config_devices:
        device_id = config_device.get("id")
        indexed_shadow_device = indexed_shadow_devices.get(device_id)

        for config_param in config_device.get("params", []):
            config_param_properties = config_param.get("properties", [])
            if "indexed" not in config_param_properties:
                continue

            assert indexed_shadow_device is not None, (
                f"Device {device_id} not found in indexed shadow despite having indexed parameters"
            )

            config_param_id = config_param.get("id")
            full_param_id = f"{device_id}::{config_param_id}"
            config_param_value = config_param.get("value")
            indexed_shadow_param_value = indexed_shadow_device.get(config_param_id)
            assert indexed_shadow_param_value is not None, (
                f"Param {full_param_id} not found in indexed shadow"
            )
            assert type(indexed_shadow_param_value) is type(config_param_value), (
                f"Param {full_param_id} value type does not match in indexed shadow, expected {type(config_param_value)} but got {type(indexed_shadow_param_value)}"
            )
            assert _are_values_equal(indexed_shadow_param_value, config_param_value), (
                f"Param {full_param_id} value does not match in indexed shadow, expected {config_param_value} but got {indexed_shadow_param_value}"
            )


def _update_params_local(node_host_ctrl, node_config):
    """
    Update the params locally.
    """
    for config_device in node_config.get("devices", []):
        device_id = config_device["id"]
        for config_param in config_device.get("params", []):
            param_id = config_param["id"]
            param_value = config_param["value"]
            assert node_host_ctrl.update_param(device_id, param_id, param_value), (
                f"Failed to update param {param_id} to {param_value}"
            )


def _get_param_update_payload(node_config):
    """
    Get the cloud update payload (device-name keyed, used by unicast params-to-node topic).
    """
    update_payload = {}
    for config_device in node_config.get("devices", []):
        device_id = config_device["id"]
        update_payload[device_id] = {}
        for config_param in config_device.get("params", []):
            param_id = config_param["id"]
            param_value = config_param["value"]
            update_payload[device_id][param_id] = param_value

    return update_payload


def _get_param_update_payload_typed(node_config):
    """
    Get the cloud update payload for group control topics.

    Device-type keyed envelope:
        { "<device-type>": { "params": { "<param-type>": <value>, ... } }, ... }
    """
    update_payload = {}
    for config_device in node_config.get("devices", []):
        device_type = config_device["type"]
        params_obj = update_payload.setdefault(device_type, {"params": {}})["params"]
        for config_param in config_device.get("params", []):
            params_obj[config_param["type"]] = config_param["value"]

    return update_payload


def _verify_config_with_node_state(node_host_ctrl, node_config):
    """
    Verify the node config with the node state.
    """

    config_devices = node_config.get("devices", [])
    tags = node_config.get("tags", {})

    # Verify the config devices and params are set correctly
    for config_device in config_devices:
        config_device_id = config_device.get("id")
        config_device_params = config_device.get("params", [])
        assert config_device_id is not None, "Device id is required"
        assert len(config_device_params) > 0, "Device params must be a non-empty list"

        for config_device_param in config_device_params:
            config_param_id = config_device_param.get("id")
            full_param_id = f"{config_device_id}::{config_param_id}"
            config_param_value = config_device_param.get("value")
            assert config_param_id is not None, "Param id is required"
            assert config_param_value is not None, "Param value is required"

            node_param_data = node_host_ctrl.get_param(
                config_device_id, config_param_id
            )
            assert node_param_data is not None, f"Param {full_param_id} data is None"

            # Check the param value
            node_param_value = node_param_data.value
            assert node_param_value is not None, f"Param {full_param_id} value is None"
            assert type(node_param_value) is type(config_param_value), (
                f"Param {full_param_id} value type does not match in node, expected {type(config_param_value)} but got {type(node_param_value)}"
            )
            assert _are_values_equal(node_param_value, config_param_value), (
                f"Param {full_param_id} value does not match in node, expected {config_param_value} but got {node_param_value}"
            )

            # Check the param properties
            node_param_properties = node_param_data.properties
            assert node_param_properties is not None, (
                f"Param {full_param_id} properties is None"
            )
            assert sorted(node_param_properties) == sorted(
                config_device_param.get("properties", [])
            ), (
                f"Param {full_param_id} properties does not match in node, expected {config_device_param.get('properties')} but got {node_param_properties}"
            )

    # Verify the tags are set correctly
    for config_tag_name, config_tag_value in tags.items():
        node_tag_value = node_host_ctrl.get_tag_value(config_tag_name)
        assert _are_values_equal(node_tag_value, config_tag_value), (
            f"Tag {config_tag_name} value does not match in node, expected {config_tag_value} but got {node_tag_value}"
        )


def _randomize_config(node_config):
    """
    Randomize the node config by changing the values of the parameters to new random values.
    """

    for config_device in node_config.get("devices", []):
        for config_param in config_device.get("params", []):
            param_value = config_param.get("value")
            param_bounds = config_param.get("bounds", None)
            new_param_value = _get_new_param_value(param_value, param_bounds)
            config_param["value"] = new_param_value


def _verify_uploaded_node_config(uploaded_config, node_config):
    uploaded_devices = uploaded_config.get("devices", [])
    node_devices = node_config.get("devices", [])

    # Verify all devices are present
    assert len(node_devices) == len(uploaded_devices), (
        "Number of devices does not match"
    )
    node_devices = sorted(node_devices, key=lambda x: x.get("id"))
    uploaded_devices = sorted(uploaded_devices, key=lambda x: x.get("id"))
    for node_device, uploaded_device in zip(node_devices, uploaded_devices):
        assert node_device.get("id") == uploaded_device.get("id"), (
            "Device id does not match"
        )
        assert node_device.get("type") == uploaded_device.get("type"), (
            "Device type does not match"
        )
        assert node_device.get("primary") == uploaded_device.get("primary"), (
            "Device primary does not match"
        )

        # Verify all params are present
        node_device_params = node_device.get("params", [])
        uploaded_device_params = uploaded_device.get("params", [])
        assert len(node_device_params) == len(uploaded_device_params), (
            "Number of params does not match"
        )
        node_device_params = sorted(node_device_params, key=lambda x: x.get("id"))
        uploaded_device_params = sorted(
            uploaded_device_params, key=lambda x: x.get("id")
        )
        for node_device_param, uploaded_device_param in zip(
            node_device_params, uploaded_device_params
        ):
            assert node_device_param.get("id") == uploaded_device_param.get("id"), (
                "Param id does not match"
            )
            assert node_device_param.get("type") == uploaded_device_param.get("type"), (
                "Param type does not match"
            )
            assert node_device_param.get("ui_type") == uploaded_device_param.get(
                "ui_type"
            ), "Param ui type does not match"
            assert node_device_param.get("data_type") == uploaded_device_param.get(
                "data_type"
            ), "Param data type does not match"

            # Check properties
            node_device_param_properties = node_device_param.get("properties", [])
            uploaded_device_param_properties = uploaded_device_param.get(
                "properties", []
            )
            assert sorted(node_device_param_properties) == sorted(
                uploaded_device_param_properties
            ), "Param properties do not match"

            # Check bounds
            node_device_param_bounds = node_device_param.get("bounds", {})
            uploaded_device_param_bounds = uploaded_device_param.get("bounds", {})
            assert node_device_param_bounds.get(
                "min"
            ) == uploaded_device_param_bounds.get("min"), (
                "Param bounds min does not match"
            )
            assert node_device_param_bounds.get(
                "max"
            ) == uploaded_device_param_bounds.get("max"), (
                "Param bounds max does not match"
            )
            assert node_device_param_bounds.get(
                "step"
            ) == uploaded_device_param_bounds.get("step"), (
                "Param bounds step does not match"
            )


def _set_test_node_config(
    node_host_ctrl, config_json_path: Path, tags_json_path: Path
) -> dict:
    """
    Set the test node config.
    """

    config = json.loads(config_json_path.read_text())
    tags = json.loads(tags_json_path.read_text())

    # drill into actual tags
    try:
        tags = tags["data"]["device"]["t"]
    except KeyError:
        raise ValueError(
            "tags_json_path must be a dictionary with a 'data' key and a 'device' key with a 't' key"
        )

    config_devices = config.get("devices", [])
    assert len(config_devices) > 0, "No devices in config"

    config_services = config.get("services", [])

    # Set the config
    node_config = {"devices": config_devices, "services": config_services, "tags": tags}
    assert node_host_ctrl.set_config(NodeConfig(node_config)), (
        "Failed to set node config"
    )

    # Verify the config with the node state
    _verify_config_with_node_state(node_host_ctrl, node_config)

    # All good, return the node config
    return node_config
