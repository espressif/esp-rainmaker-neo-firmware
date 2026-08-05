# UART Stream Monitor

A real-time monitoring tool for UART multiplexer streams.

## Overview

This script creates two background threads that continuously monitor and display data from both the monitor and remote streams of a UART multiplexer. Useful for debugging and testing UART multiplexing in real-time.

## Features

- **Real-time Monitoring**: Continuously displays data from both monitor and remote streams
- **Color-coded Output**:
  - Green `[MON]`: ESP32 monitor/console output
  - Blue `[REM]`: Remote control data
  - Yellow `[CMD]`: Interactive command input
  - Yellow `[SENT]`: Commands sent to ESP32
- **Interactive Command Mode**: Send commands to ESP32 while monitoring streams
- **Message Counting**: Tracks number of messages received on each stream
- **Graceful Shutdown**: Handles Ctrl+C and other termination signals cleanly
- **Configurable Parameters**: Customizable port, baudrate, and connection timeout

## Usage

```bash
python monitor_streams.py --port <serial_port> --baudrate <baudrate> [--timeout <seconds>] [--interactive]
```

### Arguments

- `--port`, `-p`: Serial port to monitor (required)
- `--baudrate`, `-b`: Baudrate for serial communication (default: 115200)
- `--timeout`, `-t`: Connection timeout in seconds (default: 30)
- `--interactive`, `-i`: Enable interactive command mode to send raw UART commands to ESP32 (may not work with protocol-mode firmware)

### Examples

```bash
# Monitor only mode (default)
python monitor_streams.py --port /dev/ttyUSB0

# Interactive mode with custom settings
python monitor_streams.py --port COM3 --baudrate 57600 --interactive

# Quick monitoring with defaults
python monitor_streams.py -p /dev/ttyACM0 -i
```

### Examples

```bash
# Monitor default ESP32 port
python monitor_streams.py --port /dev/ttyUSB0

# Monitor with custom baudrate
python monitor_streams.py --port COM3 --baudrate 57600

# Monitor with extended timeout
python monitor_streams.py --port /dev/ttyACM0 --timeout 60
```

## Dependencies

- `pyserial`: For serial communication

Install with:
```bash
pip install pyserial
```

## Output Format

### Monitor-Only Mode
```
============================================================
UART Stream Monitor
============================================================
Port: /dev/ttyUSB0
Baudrate: 115200
Timeout: 30s
Interactive mode: Disabled

Initializing UART multiplexer...
Starting multiplexer...
Waiting 30s for connection to stabilize...

============================================================
Monitoring active! Press Ctrl+C to stop.
Green [MON] = ESP32 monitor output
Blue  [REM] = Remote control data
============================================================

[MON] [   1] ESP-ROM:esp32c2-20220117
[MON] [   2] Build:Jan 17 2022
[REM] [   1] $SEI$READY$EEI$
[MON] [   3] rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)
```

### Interactive Mode
```
============================================================
UART Stream Monitor
============================================================
Port: /dev/ttyUSB0
Baudrate: 115200
Timeout: 30s
Interactive mode: Enabled

Initializing UART multiplexer...
Starting multiplexer...
Waiting 30s for connection to stabilize...

============================================================
Monitoring active with interactive command mode!
Green [MON] = ESP32 monitor output
Blue  [REM] = Remote control data
Yellow[CMD] = Your command input
Yellow[SENT]= Commands sent to ESP32
============================================================

**Note:** Output is thread-safe to prevent interleaving of messages from different streams.

Interactive Command Mode
Type commands to send to ESP32, or 'quit' to exit
Commands will be sent with proper end characters
============================================================

[CMD] ping
[SENT] ping (5 bytes)
[REM] [   1] $SEI$OK$EEI$
```

**Note:** Interactive mode sends raw UART commands. The ESP32 host control firmware may only respond to properly formatted protocol commands when in multiplexed mode. Use the host control script (`components/esp_rmaker_neo/src/host_ctrl/host_ctrl_python/host_ctrl.py`) for full protocol command testing.

## Operation

1. **Initialization**: Creates UART multiplexer and establishes connection
2. **Stream Setup**: Initializes monitor and remote data streams
3. **Background Monitoring**: Starts two threads that continuously read from each stream
4. **Real-time Display**: Prints received data with timestamps and stream identification
5. **Cleanup**: Handles shutdown signals and cleanly terminates all threads

## Troubleshooting

- **No data displayed**: Check that the ESP32 is running and connected to the correct port
- **Connection errors**: Verify port name and permissions (`ls /dev/tty*` on Linux)
- **Import errors**: Ensure pyserial is installed (`pip install pyserial`)
- **Corrupted/garbled output**: Output corruption has been fixed with thread-safe printing. If you still see issues, check for terminal encoding problems.
- **Interactive commands not working**: The ESP32 host control firmware may only respond to protocol-formatted commands in multiplexed mode. Use the host control script (`components/esp_rmaker_neo/src/host_ctrl/host_ctrl_python/host_ctrl.py`) for full protocol testing.

## Integration

This tool complements the UART multiplexer testing suite and can be used alongside the main host control application for comprehensive testing and debugging of UART multiplexing functionality.
