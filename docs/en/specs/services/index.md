# Services

Services extend the core functionality of an ESP RainMaker Neo node. Built-in services ship as part of the firmware; optional ones are enabled by configuration.

## Built-in services

See [Built-in Services](builtin.md).

- **Details Maintenance**: the pattern for managing service data versions and updates
- **Schedules**: time-based automation
  - operation flow and timer management
  - schedule object format and payload structure
  - trigger types (one-shot, date-based, solar/sunrise-sunset)
  - upload format and API endpoints
- **Automation Triggers**: event-based automation
  - operation flow and trigger evaluation
  - trigger object format and condition operators
  - examples for different data types
  - trigger state update notifications

## Optional services

See [Optional Services](optional.md).

- **Timezone Service**: device-wide timezone management
  - IANA and POSIX timezone support
  - parameter updates and state reporting
  - examples and error handling
- **System Service**: device control operations
  - reboot, network reset and factory reset
  - configuration flags, timing and examples
- **Local Control Service**: local HTTP API for device control
  - SEC1 and SEC2 (SRP6a, default) security modes
  - mDNS discovery
  - endpoints (version, session, get_params, set_params, get_config, and optionally ch_resp)
  - supported properties (config, params)
  - endpoint flow diagrams

## Related Topics

- [Time Synchronization](../time_sync.md) -- time synchronization requirements (required for schedules)
- [Cloud Communication](../networking/cloud_communication.md) -- cloud events used by services
- [State Management](../state_management.md) -- how service parameters are reported in shadows
- [Timeseries Data Collection](../data_collection.md) -- notification publishing used by automation triggers
