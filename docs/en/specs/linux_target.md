# ESP-IDF Linux Target

The SDK components also build for ESP-IDF's `linux` target (`idf.py --preview set-target linux`). An application that needs other ESP-IDF components, such as the KVS WebRTC stack, can then run on a host. The node runs on the FreeRTOS POSIX port, reads its factory credentials from emulated flash, and connects to the cloud as a chip does.

This is separate from the POSIX build, which compiles the same sources as plain CMake libraries without ESP-IDF.

## Limitations

The target has:

- No OTA. The osal OTA calls return `OSAL_ERR_NOT_SUPPORTED`, and the partition queries return NULL.
- No Wi-Fi or BLE provisioning, so no assisted claiming.
- No local control or challenge response: ESP-IDF's protocomm does not build for it. `esp_rmaker_local_ctrl_service_enable()` and `esp_rmaker_chal_resp_service_enable()` return `ESP_RMAKER_NOT_SUPPORTED`.
- No mDNS discovery.
- No LED or button. The examples build no-op stubs of `app_led`, `app_button` and `app_reset`, as on POSIX.

## Application Requirements

- `CONFIG_ESP_SCHEDULE_DISABLE_DEFAULT_PORT=y` and `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=4096`, for example in `sdkconfig.defaults.linux`.
- From ESP-IDF v6.1, `# CONFIG_VFS_SUPPORT_IO is not set`. The VFS wraps `open()` there and does not open host files, such as the flash image below.
- On a macOS host, `CONFIG_HAL_ASSERTION_DISABLE=y` if the app sets `CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_SILENT=y`. Silent HAL assertions link with `-u abort`, which the macOS linker does not resolve.
- `set(COMPONENTS main)` in the project `CMakeLists.txt`, so that only what `main` needs is built. Most chip drivers do not build for the target.
- A `fctry` partition, and a flash image that holds the partition table and the node's [factory NVS](https://docs.neo.rainmaker.espressif.com/docs/firmware/device-credentials/factory-nvs). Name the image in `RMNG_FLASH_IMAGE` when starting the app; the SDK loads it before `app_main()`. The app writes its NVS into the image, so run it on a copy. For example, with the partition table at `0x8000` and `fctry` at `0x10000`:

  ```sh
  esptool --chip esp32 merge-bin --pad-to-size 4MB -o flash.img 0x8000 build/partition_table/partition-table.bin 0x10000 fctry.bin
  cp flash.img flash_run.img
  RMNG_FLASH_IMAGE=flash_run.img idf.py monitor
  ```

## Examples

Every example builds for the target with ESP-IDF v6.0 and v6.1, and CI builds them. Each example has an `sdkconfig.defaults.linux` with the configuration above. To run `light`, with `fctry` at `0x3E0000` as in its `partitions.csv`:

```sh
cd examples/light
idf.py --preview set-target linux
idf.py build
esptool --chip esp32 merge-bin --pad-to-size 4MB -o flash.img 0x8000 build/partition_table/partition-table.bin 0x3E0000 fctry.bin
cp flash.img flash_run.img
RMNG_FLASH_IMAGE=flash_run.img idf.py monitor
```

`fctry.bin` is the [factory NVS](https://docs.neo.rainmaker.espressif.com/docs/firmware/device-credentials/factory-nvs) that `tools/factory_nvs_gen` makes, as for a chip. The node connects with its credentials.

ESP-IDF's esp-tls connects only to the first address that DNS returns. If the host has no IPv6 route but DNS returns an IPv6 address first, the MQTT connection times out. Prefer IPv4 with `precedence ::ffff:0:0/96 100` in `/etc/gai.conf`.
