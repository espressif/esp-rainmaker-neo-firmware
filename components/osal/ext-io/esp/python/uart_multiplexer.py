#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
UART Multiplexer for separating monitor output from remote I/O output.

This module provides a UART multiplexer that reads from a serial port and
splits the incoming data into two streams:
- Monitor output: Regular console/monitor output
- Remote output: External I/O output marked with special headers/trailers

The headers and trailers are automatically parsed from the osal_ext_io_packet_constants_esp.h header file.
"""

import re
import threading
import time
from queue import Queue, Empty
from typing import Optional, Tuple
import serial
import os


class VirtualPort:
    """
    Virtual port that can be used to handle data reading.
    """

    # Sentinel object to signal port closure
    _CLOSED_SENTINEL = object()

    def __init__(self, timeout: Optional[float] = None):
        self.timeout = timeout
        self._queue = Queue[bytes]()
        self._closed = False

        # RX buffer
        self._buffer_lock = threading.Lock()
        self._buffer = bytearray()

    def _get_buffer_locked(self, size: int) -> bytes:
        """Get the buffer. Assumes the lock is already acquired."""
        data = self._buffer[:size]
        self._buffer = self._buffer[size:]
        return bytes(data)

    def read(self, size=1024) -> bytes:
        """
        Reads 'size' bytes from the stream. Returns fewer if timeout occurs.
        """
        with self._buffer_lock:
            start_time = time.time()

            while len(self._buffer) < size:
                # Check if port is closed
                if self._closed:
                    break

                # Calculate remaining time
                remaining = self._get_remaining_time(start_time)

                # Check if we timed out before even trying to get data
                if self.timeout is not None and remaining <= 0:
                    break

                try:
                    # Pull chunk from queue
                    chunk = self._queue.get(timeout=remaining)

                    # Check for close sentinel
                    if chunk is self._CLOSED_SENTINEL:
                        self._closed = True
                        break

                    self._buffer.extend(chunk)
                except Empty:
                    break  # Timeout occurred

            # Extract requested amount (or whatever is available)
            read_len = min(len(self._buffer), size)
            return self._get_buffer_locked(read_len)

    def read_until(self, expected=b"\n", size=None) -> bytes:
        """
        Read until expected sequence is found, OR size bytes captured, OR timeout.
        """
        with self._buffer_lock:
            start_time = time.time()

            while True:
                # Check if port is closed
                if self._closed:
                    break

                # 1. PRIORITY CHECK: Have we already satisfied the 'size' requirement?
                # If size is set and buffer is big enough, return 'size' bytes immediately.
                # This ignores the terminator if it appears after the size limit.
                if size is not None and len(self._buffer) >= size:
                    return self._get_buffer_locked(size)

                # 2. SEQUENCE CHECK: Is the terminator in the current buffer?
                t_index = self._buffer.find(expected)
                if t_index != -1:
                    # Calculate length including the terminator
                    split_len = t_index + len(expected)

                    # Edge case: If size is set, logic #1 usually catches it, but if the
                    # terminator is found BEFORE the size limit, we return up to terminator.
                    if size is not None and split_len > size:
                        # This theoretically hits condition #1 first, but safety check:
                        return self._get_buffer_locked(size)

                    return self._get_buffer_locked(split_len)

                # 3. TIMEOUT CHECK
                remaining = self._get_remaining_time(start_time)
                if self.timeout is not None and remaining <= 0:
                    break

                # 4. FETCH MORE DATA
                try:
                    chunk = self._queue.get(timeout=remaining)

                    # Check for close sentinel
                    if chunk is self._CLOSED_SENTINEL:
                        self._closed = True
                        break

                    self._buffer.extend(chunk)
                except Empty:
                    break

            # If we break here (Timeout or closed), return what we have, subject to size limit
            return_len = len(self._buffer)
            if size is not None:
                return_len = min(return_len, size)

            return self._get_buffer_locked(return_len)

    def write(self, b: bytes) -> int:
        """
        Write bytes to the virtual port.
        """
        if not isinstance(b, (bytes, bytearray)):
            raise TypeError("write() argument must be bytes or bytearray")
        self._queue.put(b)
        return len(b)

    def reset(self):
        """
        Discard all buffered data — both the already-extracted RX buffer and
        anything still queued by the reader thread. Used to give each command
        a clean slate so a stale/late response from a prior exchange cannot be
        read as the response to the next command.
        """
        with self._buffer_lock:
            self._buffer = bytearray()
            while True:
                try:
                    chunk = self._queue.get_nowait()
                except Empty:
                    break
                # Preserve a close sentinel if one was already queued.
                if chunk is self._CLOSED_SENTINEL:
                    self._closed = True

    def open(self):
        """Open the virtual port."""
        self._closed = False

    def close(self):
        """Close the virtual port, cancelling any pending read operations."""
        if not self._closed:
            self._closed = True
            # Put sentinel to wake up any blocked readers
            self._queue.put(self._CLOSED_SENTINEL)

    def _get_remaining_time(self, start_time):
        if self.timeout is None:
            return None  # None means wait forever in Queue.get()

        elapsed = time.time() - start_time
        remaining = self.timeout - elapsed
        return max(0, remaining)  # Ensure we don't pass negative time


class UARTMultiplexer:
    """
    Multiplexes UART data into monitor and remote streams based on special markers.
    """

    def __init__(
        self,
        port: str,
        baudrate: int = 115200,
        timeout: Optional[float] = None,
        header_file: Optional[str] = None,
    ):
        """
        Initialize the UART multiplexer.

        Args:
            port: Serial port name (e.g., '/dev/ttyUSB0', 'COM1')
            baudrate: Baud rate for serial communication
            timeout: Timeout for the monitor/remote port readings. None for infinite block, 0 for instant.
            header_file: Path to osal_ext_io_packet_constants_esp.h header file. If None, tries to find it automatically.
        """
        self.port = port
        self.baudrate = baudrate
        self.serial_conn: Optional[serial.Serial] = None

        # Find header file if not provided
        if header_file is None:
            header_file = self._find_header_file()

        # Parse header and trailer from header file
        self.header_bytes, self.trailer_bytes, self.ping_bytes = self._parse_markers(
            header_file
        )

        # Packet format constants
        self.HEADER_LENGTH = len(self.header_bytes)  # 4 bytes
        self.LENGTH_FIELD_LENGTH = 2  # uint16_t
        self.HEADER_WITH_LENGTH = (
            self.HEADER_LENGTH + self.LENGTH_FIELD_LENGTH
        )  # 6 bytes
        self.TRAILER_LENGTH = len(self.trailer_bytes)  # 4 bytes

        # Create output buffers
        self.timeout = timeout
        self.monitor_buffer = VirtualPort(timeout=timeout)
        self.remote_buffer = VirtualPort(timeout=timeout)

        # Threading control
        self.running = False
        self.thread: Optional[threading.Thread] = None

        # State for parsing
        self.in_remote_block = False
        self.expected_remote_length = 0
        self.remote_data_buffer = bytearray()

    def set_timeout(self, timeout: float):
        """Set the timeout for the serial connection."""
        self.timeout = timeout
        self.monitor_buffer.timeout = timeout
        self.remote_buffer.timeout = timeout

    def _find_header_file(self) -> str:
        """Find the osal_ext_io_packet_constants_esp.h header file automatically."""
        # Try common locations relative to this script
        script_dir = os.path.dirname(os.path.abspath(__file__))
        possible_paths = [
            os.path.join(
                script_dir, "..", "include", "osal_ext_io_packet_constants_esp.h"
            ),
            os.path.join(
                script_dir,
                "..",
                "..",
                "esp",
                "include",
                "osal_ext_io_packet_constants_esp.h",
            ),
            os.path.join(
                script_dir,
                "..",
                "..",
                "..",
                "..",
                "esp",
                "include",
                "osal_ext_io_packet_constants_esp.h",
            ),
            "esp/include/osal_ext_io_packet_constants_esp.h",
        ]

        for path in possible_paths:
            if os.path.exists(path):
                return path

        raise FileNotFoundError(
            "Could not find osal_ext_io_packet_constants_esp.h header file. Please specify header_file parameter."
        )

    def _parse_markers(self, header_file: str) -> Tuple[bytes, bytes]:
        """
        Parse OSAL_EXT_IO_HEADER and OSAL_EXT_IO_TRAILER defines from the header file.

        Returns:
            Tuple of (header_bytes, trailer_bytes)
        """
        with open(header_file, "r", encoding="utf-8") as f:
            content = f.read()

        # Find the header define
        header_match = re.search(r'#define\s+OSAL_EXT_IO_HEADER\s+"([^"]*)"', content)
        if not header_match:
            raise ValueError("OSAL_EXT_IO_HEADER define not found in header file")

        # Find the trailer define
        trailer_match = re.search(r'#define\s+OSAL_EXT_IO_TRAILER\s+"([^"]*)"', content)
        if not trailer_match:
            raise ValueError("OSAL_EXT_IO_TRAILER define not found in header file")

        # Find the ping define
        ping_match = re.search(
            r'#define\s+OSAL_EXT_IO_RECEIVED_PING\s+"([^"]*)"', content
        )
        if not ping_match:
            raise ValueError(
                "OSAL_EXT_IO_RECEIVED_PING define not found in header file"
            )

        header_value = header_match.group(1)
        trailer_value = trailer_match.group(1)
        ping_value = ping_match.group(1)

        # Convert C-style escape sequences to bytes
        header_bytes = header_value.encode().decode("unicode_escape").encode("latin-1")
        trailer_bytes = (
            trailer_value.encode().decode("unicode_escape").encode("latin-1")
        )
        ping_bytes = ping_value.encode().decode("unicode_escape").encode("latin-1")

        return header_bytes, trailer_bytes, ping_bytes

    def start(self):
        """Start the UART multiplexer."""
        if self.running:
            return

        try:
            self.serial_conn = serial.Serial(
                port=self.port, baudrate=self.baudrate, timeout=0.1
            )
        except serial.SerialException as e:
            raise RuntimeError(f"Failed to open serial port {self.port}: {e}")

        # Ensure the virtual ports are open
        if self.monitor_buffer:
            self.monitor_buffer.open()
        if self.remote_buffer:
            self.remote_buffer.open()

        # Initialize the stream buffer
        self._stream_buffer = bytearray()

        # Start the reading loop
        self.running = True
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()
        return True

    def stop(self):
        """Stop the UART multiplexer."""
        # Stop the reading loop
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)

        # Close virtual ports to cancel any pending reads
        if self.monitor_buffer:
            self.monitor_buffer.close()
        if self.remote_buffer:
            self.remote_buffer.close()

        # Close the serial connection
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()

    def _read_loop(self):
        """Main reading loop that processes incoming data."""
        # Consecutive SerialException count; break only after many failures (real disconnect)
        serial_error_count = 0
        max_serial_errors = 50

        while self.running:
            try:
                if self.serial_conn and self.serial_conn.is_open:
                    # Read available data
                    data = self.serial_conn.read(1024)
                    serial_error_count = 0  # Reset on successful read
                    if data:
                        self._stream_buffer.extend(data)
                        self._process_buffer()
                else:
                    time.sleep(0.01)
            except serial.SerialException as e:
                # Transient on both POSIX and ESP: "device reports readiness to read but
                # returned no data" (USB glitch, brief disconnect, or multiple access).
                # Retry with backoff; only exit after repeated failures.
                serial_error_count += 1
                if serial_error_count >= max_serial_errors:
                    print(
                        f"Read loop exiting after {max_serial_errors} serial errors: {e}"
                    )
                    break
                time.sleep(0.05 if serial_error_count < 5 else 0.2)
            except Exception as e:
                print(f"Error in read loop: {e}")
                break

    def _find_header_with_length(self) -> int:
        """Find complete header + length field (6 bytes total)."""
        header_index = self._stream_buffer.find(self.header_bytes)
        if header_index == -1:
            return -1

        # Check if we have enough bytes for the length field
        header_end = header_index + self.HEADER_WITH_LENGTH
        if len(self._stream_buffer) < header_end:
            return -1

        return header_index

    def _process_remote_block(self) -> bool:
        """
        Process data in remote block with length validation.

        Returns:
            True if processing should continue, False if validation failed and we should abort
        """
        trailer_index = self._stream_buffer.find(self.trailer_bytes)

        if trailer_index != -1:
            # Found trailer - validate it appears at expected position
            remaining_length = self.expected_remote_length - len(
                self.remote_data_buffer
            )
            if trailer_index == remaining_length:
                # Valid packet - send buffered data to remote
                combined_data = (
                    self.remote_data_buffer + self._stream_buffer[:trailer_index]
                )
                self.remote_buffer.write(combined_data)

                # ACK the validated frame so the firmware's writer stops
                # retransmitting. The firmware re-sends a frame until it sees
                # this ping, which is how a frame corrupted on the wire (e.g.
                # split by an esp_rom_printf/ISR log that bypasses the UART
                # write lock) is recovered instead of silently lost.
                self.serial_conn.write(self.ping_bytes)

                # Remove processed data (including trailer)
                trailer_end = trailer_index + self.TRAILER_LENGTH
                del self._stream_buffer[:trailer_end]

                # End remote block
                self.in_remote_block = False
                self.remote_data_buffer.clear()
                return True
            else:
                # Trailer at wrong position - invalid packet, treat as monitor data
                return self._abort_remote_to_monitor()
        else:
            # No trailer found - check if we've exceeded expected length
            remaining_for_remote = self.expected_remote_length - len(
                self.remote_data_buffer
            )
            if remaining_for_remote <= 0:
                # Exceeded expected length without finding trailer - invalid packet
                return self._abort_remote_to_monitor()

            # Continue buffering - send safe amount to remote buffer
            safe_len = len(self._stream_buffer) - self._get_safe_keep_length(
                self.trailer_bytes
            )
            if safe_len > 0:
                # Don't exceed the expected remaining length
                actual_len = min(safe_len, remaining_for_remote)
                self.remote_data_buffer.extend(self._stream_buffer[:actual_len])
                del self._stream_buffer[:actual_len]

            # Break and wait for more data
            return True

    def _abort_remote_to_monitor(self) -> bool:
        """Abort remote processing and send accumulated data to monitor buffer."""
        # Send header + length + buffered remote data + current buffer to monitor
        header_with_length = self.header_bytes + self.expected_remote_length.to_bytes(
            2, "big"
        )
        self.monitor_buffer.write(header_with_length)
        if self.remote_data_buffer:
            self.monitor_buffer.write(self.remote_data_buffer)
        if self._stream_buffer:
            self.monitor_buffer.write(self._stream_buffer)

        # Clear buffers and reset state
        self._stream_buffer.clear()
        self.remote_data_buffer.clear()
        self.in_remote_block = False
        return False

    def _get_safe_keep_length(self, pattern: bytes) -> int:
        """Get the safe length to keep in the buffer to avoid losing the pattern."""
        for i in range(len(pattern), 0, -1):
            if self._stream_buffer[-i:] == pattern[:i]:
                return i
        return 0

    def _process_buffer(self):
        """Process the incoming data buffer and split into monitor/remote streams."""
        while self._stream_buffer:
            if not self.in_remote_block:
                # Look for header + length (6 bytes total)
                header_index = self._find_header_with_length()
                if header_index != -1:
                    # Found complete header + length - extract data before header to monitor buffer
                    if header_index > 0:
                        monitor_data = self._stream_buffer[:header_index]
                        self.monitor_buffer.write(monitor_data)

                    # Parse the expected remote data length (big-endian uint16)
                    length_start = header_index + self.HEADER_LENGTH
                    self.expected_remote_length = (
                        self._stream_buffer[length_start] << 8
                    ) | self._stream_buffer[length_start + 1]

                    # Remove processed data from buffer (including header + length)
                    header_end = header_index + self.HEADER_WITH_LENGTH
                    del self._stream_buffer[:header_end]

                    # Start collecting remote data
                    self.in_remote_block = True
                    self.remote_data_buffer.clear()
                else:
                    # No complete header found, but header might be truncated.
                    # Return all except the last header - 1 bytes
                    safe_len = len(self._stream_buffer) - self._get_safe_keep_length(
                        self.header_bytes
                    )
                    if safe_len > 0:
                        self.monitor_buffer.write(self._stream_buffer[:safe_len])
                        del self._stream_buffer[:safe_len]

                    # Break and wait for more data
                    break
            else:
                # In remote block - look for trailer and validate length
                success = self._process_remote_block()
                if not success:
                    # Validation failed - treat as monitor data and continue
                    break

    def get_monitor_stream(self) -> VirtualPort:
        """Get the monitor output stream."""
        return self.monitor_buffer

    def get_remote_stream(self) -> VirtualPort:
        """Get the remote output stream."""
        return self.remote_buffer

    def read_monitor(self, size: int = 1024) -> Optional[bytes]:
        """
        Read data from the monitor stream.

        Args:
            size: Maximum number of bytes to read

        Returns:
            Data from monitor output, or None if no data available
        """
        return self.monitor_buffer.read(size)

    def read_remote(self, size: int = 1024) -> Optional[bytes]:
        """
        Read data from the remote stream.

        Args:
            size: Maximum number of bytes to read

        Returns:
            Data from remote output, or None if no data available
        """
        return self.remote_buffer.read(size)

    def read_monitor_until(
        self, terminator: bytes = b"\n", size: Optional[int] = None
    ) -> Optional[bytes]:
        """
        Read data from the monitor stream until a terminator or size limit.

        Matches pyserial read_until behavior for the monitor stream.
        Blocks until condition is met or serial timeout occurs.

        Args:
            terminator: Bytes sequence to read until (default: b'\n')
            size: Maximum number of bytes to read (None for unlimited)

        Returns:
            Data up to and including terminator if found within size limit,
            or size bytes if limit reached before terminator.
            With serial timeout, may return partial data if timeout occurs.
            With no serial timeout, blocks until condition met.
        """
        return self.monitor_buffer.read_until(terminator, size)

    def read_remote_until(
        self, terminator: bytes = b"\n", size: Optional[int] = None
    ) -> Optional[bytes]:
        """
        Read data from the remote stream until a terminator or size limit.

        Matches pyserial read_until behavior for the remote stream.
        Blocks until condition is met or serial timeout occurs.

        Args:
            terminator: Bytes sequence to read until (default: b'\n')
            size: Maximum number of bytes to read (None for unlimited)

        Returns:
            Data up to and including terminator if found within size limit,
            or size bytes if limit reached before terminator.
            With serial timeout, may return partial data if timeout occurs.
            With no serial timeout, blocks until condition met.
        """
        return self.remote_buffer.read_until(terminator, size)

    def get_connection(self) -> serial.Serial:
        """Get the serial connection."""
        return self.serial_conn

    def reset_remote_input(self):
        """
        Flush everything on the inbound remote (RPC) path: the raw serial OS
        buffer, the demultiplexer's partial-frame stream buffer + in-flight
        remote-block state, and the already-demuxed remote response queue.

        ``serial.reset_input_buffer()`` alone is insufficient here — the reader
        thread continuously drains the serial port into ``remote_buffer``, so a
        stale/late/duplicate response can already be sitting past the serial
        layer. Clearing only the OS buffer leaves it there to be misread as the
        next command's response. This clears the whole path.
        """
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.reset_input_buffer()
        # Drop any partially-parsed frame and reset block state so a half-read
        # frame can't merge with the next one.
        self._stream_buffer = bytearray()
        self.in_remote_block = False
        self.expected_remote_length = 0
        self.remote_data_buffer = bytearray()
        if self.remote_buffer:
            self.remote_buffer.reset()

    def write(self, data: bytes) -> int:
        """Write data to the serial connection."""
        if self.serial_conn and self.serial_conn.is_open:
            return self.serial_conn.write(data)
        return 0

    def flush(self):
        """Flush the serial connection output buffer."""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.flush()

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()

    def __getstate__(self) -> dict:
        """
        Make this object safe to pickle across processes.

        Serial ports and threads are process-local resources; attempting to
        pickle/unpickle them can result in stale file descriptors and reader
        threads running against invalid devices (e.g. macOS: Errno 6/9).
        """
        try:
            # Ensure background reader + serial FD are closed before pickling.
            # This avoids serial FD / thread state leaking across processes.
            self.stop()
        except Exception:
            # Best-effort: we still want pickling to succeed.
            pass

        state = self.__dict__.copy()

        # Never serialize process-local resources.
        state.pop("serial_conn", None)
        state.pop("thread", None)
        state.pop("monitor_buffer", None)
        state.pop("remote_buffer", None)
        state.pop("_stream_buffer", None)

        # Record whether we should restart after unpickling.
        # We never serialize the actual serial fd; instead we re-open by port name.
        state["_resume_on_unpickle"] = bool(getattr(self, "running", False))

        # running flag is process-local; always start from False and let __setstate__
        # decide whether to call start().
        state["running"] = False

        # Parsing state should not be carried across processes.
        state["in_remote_block"] = False
        state["expected_remote_length"] = 0
        state["remote_data_buffer"] = bytearray()

        return state

    def __setstate__(self, state: dict):
        self.__dict__.update(state)
        self.monitor_buffer = VirtualPort(timeout=self.timeout)
        self.remote_buffer = VirtualPort(timeout=self.timeout)

        # Initialize new state variables if not present (for backward compatibility)
        if not hasattr(self, "expected_remote_length"):
            self.expected_remote_length = 0
        if not hasattr(self, "remote_data_buffer"):
            self.remote_data_buffer = bytearray()

        # Never resurrect the old serial fd / old thread on unpickle.
        # If requested, we will re-open the port and re-start the reader thread.
        self.serial_conn = None
        self.thread = None
        self.running = False
        self._stream_buffer = bytearray()
        self.in_remote_block = False
        self.expected_remote_length = 0
        self.remote_data_buffer = bytearray()

        resume = bool(getattr(self, "_resume_on_unpickle", False))
        # Avoid keeping this flag around; it should be one-shot.
        try:
            delattr(self, "_resume_on_unpickle")
        except Exception:
            pass

        if resume:
            try:
                self.start()
            except Exception:
                # Best-effort: if the port isn't available in this process, remain inert.
                self.running = False
                self.thread = None
                self.serial_conn = None
