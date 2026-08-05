#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Real-time UART Stream Monitor

This script monitors both monitor and remote streams from a UART multiplexer in real-time.
Useful for testing UART multiplexing with actual hardware.

Usage: python monitor_streams.py --port <serial_port> --baudrate <baudrate>

Example: python monitor_streams.py --port /dev/ttyUSB0 --baudrate 115200
"""

import sys
import os
import time
import importlib.util
import threading
import argparse
import signal

# Global lock for synchronizing output between threads
output_lock = threading.Lock()

# Add the current directory to path so we can import uart_multiplexer
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# Check for required modules (pyserial must exist before local multiplexer import)
HAS_SERIAL = False
try:
    if importlib.util.find_spec("serial") is None:
        raise ImportError
    from uart_multiplexer import UARTMultiplexer

    HAS_SERIAL = True
except ImportError:
    pass


class StreamMonitor:
    """Monitors a UART stream in real-time."""

    def __init__(self, stream_name, read_func, color_prefix):
        self.stream_name = stream_name
        self.read_func = read_func
        self.color_prefix = color_prefix
        self.running = False
        self.thread = None
        self.message_count = 0

    def start(self):
        """Start monitoring the stream."""
        if self.running:
            return

        self.running = True
        self.thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.thread.start()
        with output_lock:
            print(f"{self.color_prefix} Started monitoring {self.stream_name} stream")

    def stop(self):
        """Stop monitoring the stream."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)
        with output_lock:
            print(
                f"{self.color_prefix} Stopped monitoring {self.stream_name} stream (received {self.message_count} messages)"
            )

    def _monitor_loop(self):
        """Main monitoring loop."""
        while self.running:
            try:
                data = self.read_func(1024)  # Read up to 1KB at a time
                if data:
                    self.message_count += 1
                    # Format the data for display
                    if isinstance(data, bytes):
                        # Try to decode as UTF-8, fallback to repr
                        try:
                            text = data.decode("utf-8", errors="replace")
                            # Clean up control characters for display
                            display_text = "".join(
                                c
                                if c.isprintable() or c in "\n\r\t"
                                else f"\\x{ord(c):02x}"
                                for c in text
                            )
                        except Exception:
                            display_text = repr(data)
                    else:
                        display_text = str(data)

                    # Print with stream identification
                    # Ensure each message ends with a newline for proper separation
                    with output_lock:
                        print(
                            f"{self.color_prefix} [{self.message_count:4d}] {display_text.rstrip()}"
                        )
                    self.message_count += 1

            except Exception as e:
                if self.running:  # Only print errors if still running
                    with output_lock:
                        print(
                            f"{self.color_prefix} Error reading {self.stream_name}: {e}"
                        )
                break

            time.sleep(0.01)  # Small delay to prevent busy waiting


def signal_handler(signum, frame):
    """Handle shutdown signals."""
    global running
    print("\nReceived shutdown signal, stopping monitors...")
    running = False


def main():
    parser = argparse.ArgumentParser(description="Real-time UART Stream Monitor")
    parser.add_argument(
        "--port",
        "-p",
        type=str,
        help="Serial port to monitor (e.g., /dev/ttyUSB0, COM1)",
    )
    parser.add_argument(
        "--baudrate",
        "-b",
        type=int,
        default=115200,
        help="Baudrate for serial communication (default: 115200)",
    )
    parser.add_argument(
        "--interactive",
        "-i",
        action="store_true",
        help="Enable interactive command mode to send commands to ESP32",
    )

    args = parser.parse_args()

    # Check if port is required but not provided
    if not HAS_SERIAL and not args.port:
        parser.print_help()
        print("\nNote: pyserial not installed. Install with: pip install pyserial")
        return

    if not args.port:
        parser.print_help()
        print("\nError: --port is required when pyserial is available")
        sys.exit(1)

    global running
    running = True

    # Set up signal handlers for graceful shutdown
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    print("=" * 60)
    print("UART Stream Monitor")
    print("=" * 60)
    print(f"Port: {args.port}")
    print(f"Baudrate: {args.baudrate}")
    print(f"Interactive mode: {'Enabled' if args.interactive else 'Disabled'}")
    print()

    if not HAS_SERIAL:
        print("Error: pyserial is required for monitoring streams.")
        print("Install with: pip install pyserial")
        return

    try:
        # Create and start UART multiplexer
        print("Initializing UART multiplexer...")
        mux = UARTMultiplexer(args.port, args.baudrate, timeout=0.1)

        print("Starting multiplexer...")
        mux.start()

        # Create stream monitors
        monitor_monitor = StreamMonitor(
            "MONITOR",
            lambda size: mux.read_monitor(size),
            "\033[92m[MON]\033[0m",  # Green
        )

        remote_monitor = StreamMonitor(
            "REMOTE",
            lambda size: mux.read_remote(size),
            "\033[94m[REM]\033[0m",  # Blue
        )

        # Start monitoring threads
        monitor_monitor.start()
        remote_monitor.start()

        print("\n" + "=" * 60)
        if args.interactive:
            print("Monitoring active with interactive command mode!")
            print("Green [MON] = ESP32 monitor output")
            print("Blue  [REM] = Remote control data")
            print("Yellow[CMD] = Your command input")
            print("Yellow[SENT]= Commands sent to ESP32")
            print("=" * 60)

            # Start interactive command thread
            import threading

            cmd_thread = threading.Thread(
                target=interactive_command_sender, args=(mux,), daemon=True
            )
            cmd_thread.start()
        else:
            print("Monitoring active! Press Ctrl+C to stop.")
            print("Green [MON] = ESP32 monitor output")
            print("Blue  [REM] = Remote control data")
            print("=" * 60)
            print()

        # Keep running until interrupted
        while running:
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nInterrupted by user")
    except Exception as e:
        print(f"Error: {e}")
        import traceback

        traceback.print_exc()
    finally:
        # Clean up
        print("\nShutting down...")

        if "remote_monitor" in locals():
            remote_monitor.stop()
        if "monitor_monitor" in locals():
            monitor_monitor.stop()
        if "mux" in locals():
            mux.stop()

        print("Shutdown complete.")


def interactive_command_sender(mux):
    """Interactive command input thread."""
    print("\n" + "=" * 60)
    print("Interactive Command Mode")
    print("Type commands to send to ESP32, or 'quit' to exit")
    print("Commands will be sent with proper end characters")
    print("=" * 60)

    # Check if connection is available
    conn = mux.get_connection()
    if conn is None:
        print("\033[91m[ERROR]\033[0m No serial connection available")
        return
    if not conn.is_open:
        print("\033[91m[ERROR]\033[0m Serial connection is not open")
        return
    print(f"\033[92m[INFO]\033[0m Connected to {conn.port} at {conn.baudrate} baud")

    try:
        while running:
            # Use non-blocking input check
            try:
                import select
                import sys

                # Check if there's input available
                if select.select([sys.stdin], [], [], 0.1)[0]:
                    line = input("\033[93m[CMD]\033[0m ").strip()
                    if line.lower() in ["quit", "exit", "q"]:
                        break
                    elif line:
                        # Send command with proper end character
                        command_bytes = f"{line}\r".encode("ascii")
                        try:
                            bytes_written = mux.write(command_bytes)
                            mux.flush()
                            print(
                                f"\033[93m[SENT]\033[0m {command_bytes} ({bytes_written} bytes)"
                            )
                        except Exception as e:
                            print(f"\033[91m[ERROR]\033[0m Failed to send command: {e}")
            except (EOFError, KeyboardInterrupt):
                break
    except Exception as e:
        print(f"Command input error: {e}")


if __name__ == "__main__":
    main()
