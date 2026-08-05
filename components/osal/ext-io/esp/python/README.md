# UART Multiplexer

The UART Multiplexer is a Python component that reads from a serial port and automatically separates monitor output from remote I/O output based on special markers.

## Overview

When using the same UART port for both console/monitor output and external I/O communication, it can be difficult to distinguish between the two data streams. The UART multiplexer solves this by:

1. Reading data from a serial port
2. Detecting special header/trailer markers that identify remote I/O output
3. Splitting the data into two separate streams:
   - **Monitor stream**: Regular console/monitor output
   - **Remote stream**: External I/O output (marked with headers/trailers)

## Markers

The multiplexer automatically parses the header and trailer markers from the [`osal_ext_io_packet_constants_esp.h`](../include/osal_ext_io_packet_constants_esp.h) header file.

## Usage

```python
from uart_multiplexer import UARTMultiplexer

# Create multiplexer for serial port
with UARTMultiplexer('/dev/ttyUSB0', baudrate=115200) as mux:
    while True:
        # Read from monitor stream
        monitor_data = mux.read_monitor()
        if monitor_data:
            print(f"MONITOR: {monitor_data}")

        # Read from remote stream
        remote_data = mux.read_remote()
        if remote_data:
            print(f"REMOTE: {remote_data}")

        # Alternative: Read until specific terminator
        # monitor_line = mux.read_monitor_until(b'\n')
        # remote_line = mux.read_remote_until(b'\n')
```

## API Reference

### UARTMultiplexer(port, baudrate=115200, header_file=None)

Creates a new UART multiplexer instance.

**Parameters:**
- `port`: Serial port name (e.g., '/dev/ttyUSB0', 'COM1')
- `baudrate`: Baud rate for serial communication (default: 115200)
- `header_file`: Path to osal_ext_io_packet_constants_esp.h header file (auto-detected if None)

### Methods

- `start()`: Start reading from the serial port
- `stop()`: Stop reading and close the serial port
- `get_monitor_stream()`: Get the raw monitor output BytesIO buffer
- `get_remote_stream()`: Get the raw remote output BytesIO buffer
- `read_monitor(size=1024)`: Read up to size bytes from monitor output
- `read_remote(size=1024)`: Read up to size bytes from remote output
- `read_monitor_until(terminator=b'\n', size=None)`: Read from monitor until terminator
- `read_remote_until(terminator=b'\n', size=None)`: Read from remote until terminator

### Context Manager

The multiplexer can be used as a context manager for automatic start/stop:

```python
with UARTMultiplexer(port) as mux:
    # multiplexer is running
    pass
# multiplexer is automatically stopped
```

## Dependencies

- `pyserial`: For serial port communication

Install with:
```bash
pip install pyserial
```

## Header File Detection

The multiplexer automatically searches for the `osal_ext_io_packet_constants_esp.h` header file in common locations:

1. Relative to the script directory: `../include/osal_ext_io_packet_constants_esp.h`
2. Relative to the component directory: `../../esp/include/osal_ext_io_packet_constants_esp.h`
3. Absolute path: `esp/include/osal_ext_io_packet_constants_esp.h`

You can also specify the header file path explicitly:

```python
mux = UARTMultiplexer(port, header_file='/path/to/osal_ext_io_packet_constants_esp.h')
```

## Threading

The multiplexer runs the serial reading loop in a background daemon thread. All methods are thread-safe for reading from the output buffers.
