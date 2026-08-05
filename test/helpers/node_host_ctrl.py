# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared helpers for firmware tests: node host_ctrl lifecycle and named-shadow I/O.
"""

from time import sleep

from payload import extract_shadow_reported_state


def start_node_host_ctrl(
    node_host_ctrl,
    wait_on_node_config_sent: bool = True,
    wait_on_alexa_enabled: bool = True,
    wait_on_gva_enabled: bool = True,
):
    """
    Start the node.
    """
    # Claim any stale flags
    node_host_ctrl.clear_on_online()
    node_host_ctrl.clear_on_all_cloud_get_events()
    node_host_ctrl.clear_on_state_started_listening()
    node_host_ctrl.clear_on_state_reported()
    if wait_on_node_config_sent:
        node_host_ctrl.clear_on_node_config_sent()

    # Start the node
    node_host_ctrl.start()

    # Give a longer time (30s) for slower nodes, connections, time synchronization, etc.
    assert node_host_ctrl.wait_on_online(30000), "Node did not signal it is online"
    if wait_on_node_config_sent:
        assert node_host_ctrl.wait_on_node_config_sent(10000), (
            "Node did not send its configuration"
        )
    assert node_host_ctrl.wait_on_all_cloud_get_events(
        10000,
        add_alexa_enabled=wait_on_alexa_enabled,
        add_gva_enabled=wait_on_gva_enabled,
    ), "Node did not receive all cloud get events"
    # Wait for the node to start listening for state changes. The IAM policy may take a while to be applied, so it might reconnect once.
    assert node_host_ctrl.wait_on_state_started_listening(20000), (
        "Node did not start listening for state changes"
    )
    assert node_host_ctrl.wait_on_state_reported(10000), "Node did not report its state"
    # Wait for the node to report its state again, if any
    # e.g., the node config received event arrives after the initial state reported event
    node_host_ctrl.wait_on_state_reported(5000)


def restart_node_host_ctrl_without_reset(node_host_ctrl):
    """
    Stop and start the node WITHOUT erasing NVS, then wait until it is
    fully online.

    Note: this only restarts the RMNG start/stop layer; the underlying
    subsystem singletons (bridge child pool, services, etc.) survive
    across the restart because ``esp_rmaker_stop`` does not run
    ``esp_rmaker_deinit``. For a true cold-reboot simulation that tears
    everything in RAM down and rebuilds from NVS, use
    ::cold_reboot_node_host_ctrl.
    """
    assert node_host_ctrl.stop(), "Failed to stop node"
    # Do not wait for the node to send its configuration, as the configuration is not reset
    start_node_host_ctrl(node_host_ctrl, wait_on_node_config_sent=False)


def cold_reboot_node_host_ctrl(node_host_ctrl):
    """
    Cold-reboot the firmware WITHOUT erasing NVS, then wait until it is
    fully online again.

    Drives ``reset_keep_nvs`` on the firmware which does a full
    ``esp_rmaker_node_deinit`` + ``esp_rmaker_node_init`` cycle. Every
    RAM-only subsystem (bridge child pool, schedule per-node handles,
    automation per-node trigger state, etc.) is torn down and rebuilt
    from scratch while NVS survives. This is what exercises the
    rehydrate paths (schedule orphan-park / drain, automation reload,
    bridge child re-register).

    The programmatic device tree set up at test start does NOT survive
    this call — caller must be tolerant of the parent node coming back
    with an empty model. Tests that rely on devices surviving should
    use ::restart_node_host_ctrl_without_reset instead.
    """
    assert node_host_ctrl.stop(), "Failed to stop node before cold-reboot"
    assert node_host_ctrl.reset_keep_nvs(), "Failed to cold-reboot node (keep NVS)"
    start_node_host_ctrl(node_host_ctrl, wait_on_node_config_sent=False)


def _connect_to_named_shadow(user, thing_name, group_info_str):
    """
    Connect to the named shadow.
    """

    named_shadow_name = f"params-{group_info_str}"
    user.subscribe_to_named_shadows(
        thing_name=thing_name, named_shadows=[named_shadow_name]
    )


def _disconnect_from_named_shadow(user, thing_name, group_info_str):
    """
    Disconnect from the named shadow.
    """

    named_shadow_name = f"params-{group_info_str}"
    user.unsubscribe_from_named_shadows(
        thing_name=thing_name, named_shadows=[named_shadow_name]
    )


def read_named_shadow(user, thing_name, group_info_str, timeout_ms=5000) -> dict:
    """
    Read the named shadow.
    """

    named_shadow_name = f"params-{group_info_str}"
    user.read_shadow(thing_name=thing_name, shadow_name=named_shadow_name)
    named_shadow_data = user.read_shadow_queue(timeout=timeout_ms)
    assert named_shadow_data is not None, "No named shadow data received"
    named_shadow_reported = extract_shadow_reported_state(named_shadow_data)
    assert named_shadow_reported is not None, "No reported state in named shadow data"
    return named_shadow_reported


def operate_with_named_shadow_connection(
    op_func, user, thing_name, group_info_str, timeout_ms=5000
):
    """
    Operate with the named shadow connection.
    op_func: Function to operate with the named shadow connection.
    - Takes no arguments, returns nothing.
    - Will throw an exception if assertion fails.
    """

    _connect_to_named_shadow(user, thing_name, group_info_str)
    try:
        op_func()
    except Exception as e:
        _disconnect_from_named_shadow(user, thing_name, group_info_str)
        raise e

    _disconnect_from_named_shadow(user, thing_name, group_info_str)


def wait_for_node_state_reported(node_host_ctrl, timeout_ms=5000):
    """
    Wait for the node to report its state.
    """
    assert node_host_ctrl.wait_on_state_reported(timeout_ms), (
        "Node did not report its state"
    )
    sleep(2)  # Wait for cloud to receive the node update


def wait_for_cloud_params_update(
    node_host_ctrl, user, thing_name, group_id, cloud_payload
):
    """
    Wait for the cloud params update.
    """
    node_host_ctrl.clear_on_state_reported()
    assert user.mqtt_publish_to_topic(
        thing_name=thing_name,
        topic_name=f"params-{group_id}/params",
        data=cloud_payload,
    ), "Failed to update cloud params"
    wait_for_node_state_reported(node_host_ctrl)


def wait_for_cloud_params_update_via_group_broadcast(
    node_host_ctrl, user, group_id, cloud_payload
):
    """
    Publish a device-type keyed control payload via the group control broadcast topic;
    wait for node to report state.
    """
    node_host_ctrl.clear_on_state_reported()
    assert user.mqtt_publish_to_group_control(group_id=group_id, data=cloud_payload), (
        "Failed to update params via group control broadcast"
    )
    wait_for_node_state_reported(node_host_ctrl)


def wait_for_cloud_params_update_via_group_subgroup(
    node_host_ctrl, user, group_id, subgroup_id, cloud_payload
):
    """
    Publish a device-type keyed control payload via the subgroup control topic;
    wait for node to report state.
    """
    node_host_ctrl.clear_on_state_reported()
    assert user.mqtt_publish_to_group_control(
        group_id=group_id, data=cloud_payload, subgroup_id=subgroup_id
    ), "Failed to update params via subgroup control"
    wait_for_node_state_reported(node_host_ctrl)
