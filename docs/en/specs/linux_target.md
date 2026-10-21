# ESP-IDF Linux Target

The SDK components also build for ESP-IDF's `linux` target (`idf.py --preview set-target linux`). An application that needs other ESP-IDF components, such as the KVS WebRTC stack, can then run on a host. The node runs on the FreeRTOS POSIX port, reads its factory credentials from emulated flash, and connects to the cloud as a chip does.

This is separate from the POSIX build, which compiles the same sources as plain CMake libraries without ESP-IDF.

## Limitations

The target has:

- No OTA.
- No Wi-Fi or BLE provisioning, so no assisted claiming.
- No local control or challenge response: ESP-IDF's protocomm does not build for it. `esp_rmaker_local_ctrl_service_enable()` and `esp_rmaker_chal_resp_service_enable()` return `ESP_RMAKER_NOT_SUPPORTED`.
- No mDNS discovery.

## Application Requirements

- `CONFIG_ESP_SCHEDULE_DISABLE_DEFAULT_PORT=y` and `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=4096`, for example in `sdkconfig.defaults.linux`.
- `esp_http_client` in the `PRIV_REQUIRES` of `main`, if the app sets `CONFIG_OSAL_INCLUDE_HTTP=y`. The linux target needs `set(COMPONENTS main)`, so osal does not find `esp_http_client` without it.
- A `fctry` partition, and a flash image that holds the partition table and the node's factory NVS. Pass the image path through `esp_partition_get_file_mmap_ctrl_input()` before NVS is initialised.
