# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Unit tests for the host side of the ext-io frame link."""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from uart_multiplexer import UARTMultiplexer, VirtualPort  # noqa: E402

# pyserial's read(n) on a real port waits up to this long for n bytes.
SERIAL_TIMEOUT_S = 0.1


class FakeSerial:
    """Byte source with pyserial's read() timing, and a record of writes."""

    def __init__(self, data: bytes = b""):
        self._rx = bytearray(data)
        self.writes = []
        self.flushes = 0
        self.is_open = True

    @property
    def in_waiting(self) -> int:
        return len(self._rx)

    def read(self, size: int = 1) -> bytes:
        if len(self._rx) < size:
            time.sleep(SERIAL_TIMEOUT_S)
        data = bytes(self._rx[:size])
        del self._rx[:size]
        return data

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        self.flushes += 1


def _frame(mux: UARTMultiplexer, payload: bytes) -> bytes:
    return (
        mux.header_bytes + len(payload).to_bytes(2, "big") + payload + mux.trailer_bytes
    )


def test_open_after_close_reads_new_data():
    port = VirtualPort(timeout=0.2)
    port.close()
    port.open()
    port.write(b"ok\r")
    assert port.read_until(b"\r") == b"ok\r"


def test_open_drops_previous_session_data():
    port = VirtualPort(timeout=0.2)
    port.write(b"stale\r")
    port.close()
    port.open()
    port.write(b"new\r")
    assert port.read_until(b"\r") == b"new\r"


def test_read_available_returns_without_serial_timeout():
    mux = UARTMultiplexer("fake")
    mux.serial_conn = FakeSerial(b"abcdef")
    start = time.monotonic()
    assert mux._read_available() == b"abcdef"
    assert time.monotonic() - start < SERIAL_TIMEOUT_S / 2


def test_read_available_caps_one_pass():
    mux = UARTMultiplexer("fake")
    mux.serial_conn = FakeSerial(bytes(mux.MAX_READ_CHUNK + 10))
    assert len(mux._read_available()) == mux.MAX_READ_CHUNK
    assert len(mux._read_available()) == 10


def test_valid_frame_is_acked_before_it_is_published():
    mux = UARTMultiplexer("fake", timeout=0.2)
    mux.serial_conn = FakeSerial()
    events = []
    publish = mux.remote_buffer.write
    mux.serial_conn.write = lambda data: events.append(("ack", bytes(data)))
    mux.remote_buffer.write = lambda data: (
        events.append(("publish", bytes(data))),
        publish(data),
    )[1]

    mux._stream_buffer = bytearray(b"log line\n" + _frame(mux, b"o\r"))
    mux._process_buffer()

    assert events == [("ack", mux.ping_bytes), ("publish", b"o\r")]
    assert mux.read_monitor_until(b"\n") == b"log line\n"


def test_write_flush_goes_to_serial():
    mux = UARTMultiplexer("fake")
    mux.serial_conn = FakeSerial()
    assert mux.write(b"#0001p\r", flush=True) == 7
    assert mux.serial_conn.writes == [b"#0001p\r"]
    assert mux.serial_conn.flushes == 1
