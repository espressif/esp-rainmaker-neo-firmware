# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

from .commands import CommunicationProtocol


class ParamData:
    """
    Represents a parameter data.
    """

    def __init__(self, protocol: CommunicationProtocol, payload: str):
        self.value = None
        self.properties = []

        payload_split = payload.split(protocol.delimiter_char)

        if len(payload_split) > 0:
            self.value = protocol.parse_param_value(payload_split[0])

        if len(payload_split) > 1:
            self.properties = protocol.parse_param_properties(payload_split[1])


class HeapStatusData:
    """
    Represents a heap status data.
    """

    def __init__(self, protocol: CommunicationProtocol, payload: str):
        self.protocol = protocol
        payload_split = payload.split(protocol.delimiter_char)
        self.total_size = -1
        self.allocated_size = -1
        self.free_size = -1
        self.largest_block_size = -1
        self.lowest_free_size = -1

        if len(payload_split) > 0:
            self.total_size = int(payload_split[0])
        if len(payload_split) > 1:
            self.allocated_size = int(payload_split[1])
        if len(payload_split) > 2:
            self.free_size = int(payload_split[2])
        if len(payload_split) > 3:
            self.largest_block_size = int(payload_split[3])
        if len(payload_split) > 4:
            self.lowest_free_size = int(payload_split[4])

    def __add__(self, other: "HeapStatusData") -> "HeapStatusData":
        heap_status_data = HeapStatusData(self.protocol, "0")
        heap_status_data.total_size = self.total_size + other.total_size
        heap_status_data.allocated_size = self.allocated_size + other.allocated_size
        heap_status_data.free_size = self.free_size + other.free_size
        heap_status_data.largest_block_size = (
            self.largest_block_size + other.largest_block_size
        )
        heap_status_data.lowest_free_size = (
            self.lowest_free_size + other.lowest_free_size
        )
        return heap_status_data

    def __sub__(self, other: "HeapStatusData") -> "HeapStatusData":
        heap_status_data = HeapStatusData(self.protocol, "0")
        heap_status_data.total_size = self.total_size - other.total_size
        heap_status_data.allocated_size = self.allocated_size - other.allocated_size
        heap_status_data.free_size = self.free_size - other.free_size
        heap_status_data.largest_block_size = (
            self.largest_block_size - other.largest_block_size
        )
        heap_status_data.lowest_free_size = (
            self.lowest_free_size - other.lowest_free_size
        )
        return heap_status_data
