# Host Control Python Library

This is the Python module to control firmware built with host control enabled. Use the `host_ctrl` module:
- `NodeHostCtrl`: interface with the firmware
- `NodeConfig`: node configuration with auto-verification

## Configuration JSON

Node configurations are built from JSON files that defines devices, their params, and tags. The tool validates the JSON and will reject invalid inputs. This is then passed as a file path or object to `host_ctrl.NodeConfig`.

Schema:
- `devices`: array of device objects
  - `id`: string (required)
  - `type`: string (required)
  - `params`: array of param objects
    - `id`: string (required)
    - `type`: string (required)
    - `data_type`: one of `int`, `float`, `bool`, `string`, `object`, `array` (required)
    - `value`: matches `data_type` (required)
    - `bounds`: object (optional; only if needed for `int`/`float`)
      - `min`: lower bound
      - `max`: upper bound
      - `step`: step
    - `properties`: string array of zero or more of `read`, `write`, `indexed`, `persist`, `time_series`, `ts_cumulative` (optional)
        - `read`: read
        - `write`: write
        - `indexed`: indexed
        - `persist`: persistent
        - `time_series`: time series
        - `ts_cumulative`: time series (cumulative)
- `services`: string array of zero or more of the following standard services:
  - `timezone`: enable timezone service
  - `latency`: enable latency service
  - `local_ctrl`: enable local control service
  - `on_network_chal_resp`: enable on-network challenge response (must have `CONFIG_ESP_RMAKER_ON_NETWORK_CHAL_RESP_ENABLE=y`)
- `tags`: object of tag objects
  - `<tag name>`: `<tag value>`

An example is provided as `example_config.json`.
