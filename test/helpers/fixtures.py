# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Shared pytest fixture factories + heap tracker.

Lifted verbatim from test_firmware.py so test_bridge.py can construct
the same fixture chain against ``REQUEST_TYPE_BRIDGE`` builds without
re-importing test_firmware (which is large and pulls in many test-side
imports).

These are plain generators / helpers — wrapped by ``@pytest.fixture``
in each test module.
"""

import math

from host_ctrl_python.data import HeapStatusData
from rmng_backend import Group


class HeapStatusTracker:
    """
    Tracker for the heap status.
    """

    def __init__(self):
        # The smallest maximum block size seen so far.
        self.smallest_largest_block_size = -1
        self.last_status = None

    def track(self, status: HeapStatusData | None):
        """
        Track the heap status.
        """
        if status is None:
            return
        self.last_status = status
        if (
            self.smallest_largest_block_size < 0
            or self.smallest_largest_block_size > status.largest_block_size
        ):
            self.smallest_largest_block_size = status.largest_block_size

    @staticmethod
    def format_bytes(
        bytes_value: int, decimals: int = 2, add_positive_sign: bool = False
    ) -> str:
        """
        Converts a byte value into a human-readable string with appropriate units (Bytes, KB, MB, GB).
        """
        if bytes_value == 0:
            return "0 B"

        is_negative = bytes_value < 0
        positive_sign = "+" if add_positive_sign else ""
        bytes_value = abs(bytes_value)

        units = ["B", "KB", "MB", "GB"]
        power = 1024

        i = int(math.floor(math.log(bytes_value, power)))
        if i >= len(units):
            i = len(units) - 1

        return f"{'-' if is_negative else positive_sign}{bytes_value / (power**i):.{decimals}f} {units[i]}"


def _firmware_instance_request_host_ctrl_wrapper(request_fixture, add_summary_section):
    heap_status_tracker = HeapStatusTracker()
    yield request_fixture, heap_status_tracker
    # Add heap status summary
    add_summary_section(
        f"Overall Heap Status for {request_fixture.to_key()}",
        [
            f"Smallest largest block size: {HeapStatusTracker.format_bytes(heap_status_tracker.smallest_largest_block_size) if heap_status_tracker.smallest_largest_block_size >= 0 else 'N/A'}",
            f"Lowest free size: {HeapStatusTracker.format_bytes(heap_status_tracker.last_status.lowest_free_size) if heap_status_tracker.last_status is not None and heap_status_tracker.last_status.lowest_free_size >= 0 else 'N/A'}",
        ],
    )


def _firmware_instance_host_ctrl(
    firmware_instance_request_host_ctrl_wrapper, firmware_instance_manager
):
    """
    Fixture helper to create a firmware instance.
    """
    instance_request, heap_status_tracker = firmware_instance_request_host_ctrl_wrapper
    instance = firmware_instance_manager.dispatch(instance_request)
    if not instance.is_running():
        start_attempts = 0
        started = False
        while start_attempts < 3:
            started = instance.start()
            if started:
                break
            start_attempts += 1

        assert started, "Failed to start firmware instance"
    yield instance, heap_status_tracker
    firmware_instance_manager.return_instance(instance_request, instance)


def _node_host_ctrl(request, firmware_instance_host_ctrl, add_summary_section):
    """
    Fixture helper to create a node host_ctrl session.
    """
    instance, heap_status_tracker = firmware_instance_host_ctrl

    host_ctrl = instance.get_host_ctrl()
    host_ctrl.reset()

    # Get initial heap status
    initial_heap_status = host_ctrl.get_heap_status()
    heap_status_tracker.track(initial_heap_status)

    yield host_ctrl

    def get_heap_status_lines(
        initial: HeapStatusData, final: HeapStatusData
    ) -> list[str]:
        if initial is None or final is None:
            return []
        delta = final - initial

        return [
            f"--> Total size    : {HeapStatusTracker.format_bytes(initial.total_size)} -> {HeapStatusTracker.format_bytes(final.total_size)} ({HeapStatusTracker.format_bytes(delta.total_size, add_positive_sign=True)})",
            f"--> Allocated size: {HeapStatusTracker.format_bytes(initial.allocated_size)} -> {HeapStatusTracker.format_bytes(final.allocated_size)} ({HeapStatusTracker.format_bytes(delta.allocated_size, add_positive_sign=True)})",
            f"--> Free size     : {HeapStatusTracker.format_bytes(initial.free_size)} -> {HeapStatusTracker.format_bytes(final.free_size)} ({HeapStatusTracker.format_bytes(delta.free_size, add_positive_sign=True)})",
        ]

    try:
        # Get final heap status
        final_heap_status = host_ctrl.get_heap_status()
        heap_status_tracker.track(final_heap_status)

        # Add heap status summary
        add_summary_section(
            f"Heap Status for {request.node.nodeid}",
            [
                *get_heap_status_lines(initial_heap_status, final_heap_status),
            ],
        )
    finally:
        # Always tear down, even if the test failed mid-flow and left the node in a bad
        # state. The firmware instance is reused across tests, so any service the test
        # started (e.g. local control) would otherwise stay running and bound to its port.
        # On the next reuse, get_host_ctrl() pushes the new HTTP port before setup's reset()
        # runs; the firmware rejects the port while local control is still up
        # (INVALID_STATE), so the service comes back on a stale port and CivetWeb fails to
        # bind. reset() triggers the firmware's disable-all-standard-services path; do it
        # best-effort so a wedged node can't skip the remaining cleanup.
        #
        # Order matters: reset() deinits the node and rejects an INVALID_STATE while
        # RainMaker is still STARTED ("stop it first"), so it must run *after* stop().
        # Both are best-effort so a wedged node can't skip the remaining cleanup.
        print("Cleaning up node host_ctrl")
        try:
            host_ctrl.stop()
        except Exception as exc:
            print(f"WARN: node host_ctrl stop during teardown failed: {exc}")
        try:
            host_ctrl.reset()
        except Exception as exc:
            print(f"WARN: node host_ctrl reset during teardown failed: {exc}")
        host_ctrl.quit()


def _associate_to_new_group(group_name, node_host_ctrl, user, group_api):
    """
    Associate the node to a newly created group.
    """

    # Do user node association
    group_id = group_api.create_group(group_name=group_name)
    result = user.do_user_node_assoc(device=node_host_ctrl, group_id=group_id)
    assert result is None, f"Association failed with error: {result}"

    # NOTE: Refresh MQTT credentials to ensure MQTT credentials and IAM policies are updated for the new group
    assert user.mqtt_refresh_credentials(), (
        "Failed to refresh MQTT credentials after associating to new group"
    )

    return group_id


def _associated_user1_node_host_ctrl(node_host_ctrl, user_pool):
    """
    Helper function to reset the node host_ctrl session associated with test_user1.
    """
    user = user_pool.acquire()
    try:
        group_id = _associate_to_new_group(
            "Test Associated Group", node_host_ctrl, user, Group(user=user)
        )
        yield node_host_ctrl, user, group_id
    finally:
        # Release the user
        user_pool.release(user)


def _associated_user1_node_host_ctrl_with_user1_connected(
    associated_user1_node_host_ctrl,
):
    """
    Helper function to create a node host_ctrl session associated with test_user1.
    """

    node_host_ctrl, user, group_id = associated_user1_node_host_ctrl
    user.mqtt_connect()
    yield node_host_ctrl, user, group_id
    user.mqtt_disconnect_and_wait()
