# OTA Custom

A single OTA example that builds for both ESP-IDF hardware and the POSIX host.
It fetches OTA jobs, logs each OTA lifecycle event, and drives a status LED
through the OTA states. Beyond the default firmware update it demonstrates two
OTA extension points via a sample "mock" implementation:

- A **custom-filetype handler** for non-firmware OTA filetypes (`mock`,
  `mock_no_ver`), plumbed in via `esp_rmaker_ota_config_t::custom_filetype_handler_lookup`.
- A **custom job callback** for non-OTA jobs, plumbed in via
  `esp_rmaker_ota_config_t::custom_job_cb` (enabled with `CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT`).

## Behaviour — status LED

| State | Colour |
| --- | --- |
| Setup | White |
| Idle (ready) | Light blue |
| OTA in progress | Orange |
| OTA success | Green |
| OTA failed / rejected / error | Red |

On POSIX the LED is a no-op stub.

## Custom filetype file generation

The mock filetype handler downloads the file directly into a dynamically
allocated buffer, so **keep the OTA filesize small**. Generate a random binary
with the bundled helper:

```bash
# Usage: python random_bytes.py [size] [filename]
python random_bytes.py 4096 firmware_mock.bin
```

## Build and Run

Standard flow for every example — see the [examples README](../../README.md).
The POSIX binary is `./ota-custom`.

You should [flash the factory NVS partition](../../README.md#device-credentials)
before execution. If using a Thread network, [enable the DNS64 client](../../common/app_network/README.md#connecting-to-ipv4-servers-on-thread).
