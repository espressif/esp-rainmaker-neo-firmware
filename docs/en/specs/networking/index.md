# Networking

MQTT and cloud communication: the topics, the connection lifecycle, and the event protocol carried over them. Together these enable reliable communication between nodes and the cloud infrastructure.

- [Complete MQTT Topic Reference](mqtt_topics.md) -- complete reference of every MQTT topic the firmware uses: topic variables (thing name, group info string), cloud communication topics (`to_cloud`, `from_cloud`), parameter control topics, group control topics, named and indexed shadow topics (update, delete, get/accepted), timeseries topics, notification topics, bridge topics, and the subscription patterns.
- [MQTT Connection Management](mqtt_connection.md) -- connection and authentication, reconnection behaviour with exponential backoff, session management (clean vs persistent), keep-alive and connection health monitoring, reconnection task execution, and MQTT budgeting (rate limiting).
- [Cloud Communication](cloud_communication.md) -- the event communication model (get/set events), response behaviour and patterns, the version handshake, every get event (`getGroupInfo`, `getAlexaEn`, `getGVAEn`, `getSchedVer`, `getTriggerVer`, `getSchedDetails`, `getTriggerDetails`, `getTimeSync`), every set event (`setNodeConfig`), the bridge events, and the event payload formats.

## Related Topics

- [State Management](../state_management.md) -- how MQTT topics are used in state reporting
- [Initialization and Startup Sequence](../initialization.md) -- initialization sequence and connection setup
- [Error Handling and Recovery Strategies](../error_handling.md) -- error handling and recovery strategies
