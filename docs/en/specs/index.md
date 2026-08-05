# Firmware Specification

The complete behavioural specification for ESP RainMaker Neo node firmware: what the node does at each stage of its life, the payloads it exchanges with the cloud, and how it recovers when things go wrong.

For **setup, building, and factory provisioning**, see the [firmware guides](https://docs.neo.rainmaker.espressif.com/docs/firmware/). For the C API, see [C API Reference](../c-api-reference/index.rst).

## Core Functionality

- [Initialization and Startup Sequence](initialization.md) -- component initialization order, prerequisites, failure points, and the overall firmware flow sequence diagram. Start here to understand how the node boots and initializes.
- [Node Configuration](configuration.md) -- node configuration payload format, including device and service definitions, parameters, attributes and metadata.
- [State Management](state_management.md) -- incoming state modifications (parameter updates from cloud/apps), outgoing state reporting (shadow updates), online/offline state management, named and indexed shadows, and shadow migration on group changes.
- [Timeseries Data Collection](data_collection.md) -- timeseries data collection (publishing timestamped parameter values) and direct notifications from node to cloud.
- [Network Provisioning](provisioning.md) -- network provisioning and user-node association: the provisioning sequence, the challenge-response endpoint, and protobuf message formats.
- [Local Control Endpoint Protocol](local_ctrl_endpoint_protocol.md) -- the wire protocol for the local control
  and challenge-response endpoints served over protocomm.
- [Time Synchronization](time_sync.md) -- NTP/SNTP setup, which services require synchronised time, and failure handling.
- [Error Handling and Recovery Strategies](error_handling.md) -- the general error handling approach, retry strategies for MQTT, shadows and events, and the error recovery flows.

## Subsystems

- [Networking](networking/index.md) -- MQTT topics, connection lifecycle and the event-based cloud communication protocol.
- [Services](services/index.md) -- built-in services (schedules, automation triggers) and optional ones (timezone, system control, local HTTP API).
- [OTA](ota/index.md) -- OTA job document format and status-details reporting.
