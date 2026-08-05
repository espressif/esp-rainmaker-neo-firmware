# Node Configuration

## Payload format

```javascript
{
    // node ID / thing name
    "node_id": <node ID / thing name>,
    // node configuration
    "config": {
        // Data model type; always "default"
        "data_model": "default",

        // information
        "info": {
            "name": <node name>,
            "fw_version": <firmware version>,
            "type": <node type>,
            "model": <project name>
        },

        // list of node-level attributes (if any); same format as device attributes
        "attributes": [
            {
                "name": <attribute name>,
                "value": <attribute value>
            },
            // ... one entry per node-level attribute
        ],

        // list of devices
        "devices": [
            {
                "id": <device id>,                  // e.g., "Lightbulb"
                "type": <device type>,              // e.g., "esp.device.lightbulb"

                // list of attributes for this device (if any)
                "attributes": [
                    {
                        "name": <attribute name>,        // e.g., "mac"
                        "value": <attribute value>       // e.g., "xx:yy:zz:aa:bb:cc"
                    },
                    // ... one entry per attribute for this device
                ],

                "primary": <primary parameter id>,  // e.g., "Power"

                // list of parameters for this device
                "params": [
                    {
                        "id": <parameter id>,               // e.g., "Power"
                        "type": <parameter type>,           // e.g., "esp.param.power"
                        "data_type": <parameter data type>, // e.g., "bool"

                        // list of properties: zero or more of:
                        // read, write, time_series, indexed, persist
                        "properties": [
                            "read",
                            "write",
                            // ... any other properties
                        ],

                        // bounds (if any)
                        "bounds": {
                            "min": <min value>,
                            "max": <max value>,
                            "step": <minimal step>
                        },

                        "ui_type": <parameter UI type>      // e.g., "esp.ui.toggle"
                    },
                    // ... one entry per parameter for this device
                ]
            },
            // ... one entry per device on the node
        ],

        // list of services
        "services": [
            // ... same format as "devices"
        ]
    }
}
```

## Notes

- Keys are emitted in the order shown. `info` and the node-level `attributes` array precede `devices` and `services`.
- `devices` and `services` share one internal list; a device with the "service" flag is emitted under `services`, everything else under `devices`. Both arrays are always present once the node has at least one device or service.
- Optional keys are omitted rather than emitted as `null`: `type`, `attributes`, `primary`, `params`, `bounds` and `ui_type` only appear when set.
- `data_type` is one of `"bool"`, `"int"`, `"float"`, `"string"`, `"object"`, `"array"` (or `"invalid"` for an unset value).
- `properties` reflects the parameter's property flags. `time_series` is emitted for both plain and cumulative timeseries parameters (see [Timeseries Data Collection](data_collection.md)); `persist` marks parameters whose values are written back to NVS.
- `info.model` is the build's project name and `info.fw_version` the build's firmware version when the node is created with the platform defaults. `info.name`, `info.type`, `info.fw_version` and `info.model` are also mirrored into the node's `device` tags and are reserved tag names (see [Node Tags](state_management.md#shadows)).
- The whole document is hashed (SHA-256) to detect changes. The node publishes [setNodeConfig](networking/cloud_communication.md#setnodeconfig) only when the hash differs from the value persisted in NVS, and that same hash is what is reported as `ncfg_ver` in the shadows.
