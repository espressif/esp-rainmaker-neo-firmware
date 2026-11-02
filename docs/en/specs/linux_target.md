# ESP-IDF Linux Target

The SDK components also build for ESP-IDF's `linux` target (`idf.py --preview set-target linux`). An application that needs other ESP-IDF components, such as the KVS WebRTC stack, can then run on a host. The node runs on the FreeRTOS POSIX port, reads its factory credentials from emulated flash, and connects to the cloud as a chip does.

This is separate from the POSIX build, which compiles the same sources as plain CMake libraries without ESP-IDF.

## Limitations

The target has:

- No OTA. The osal OTA calls return `OSAL_ERR_NOT_SUPPORTED`, and the partition queries return NULL.
- No Wi-Fi or BLE provisioning, so no assisted claiming.
- No local control or challenge response: ESP-IDF's protocomm does not build for it. `esp_rmaker_local_ctrl_service_enable()` and `esp_rmaker_chal_resp_service_enable()` return `ESP_RMAKER_NOT_SUPPORTED`.
- No mDNS discovery.

## Application Requirements

- `CONFIG_ESP_SCHEDULE_DISABLE_DEFAULT_PORT=y` and `CONFIG_FREERTOS_TIMER_TASK_STACK_DEPTH=4096`, for example in `sdkconfig.defaults.linux`.
- `set(COMPONENTS main)` in the project `CMakeLists.txt`, so that only what `main` needs is built. Most chip drivers do not build for the target.
- A `fctry` partition, and a flash image that holds the partition table and the node's [factory NVS](https://docs.neo.rainmaker.espressif.com/docs/firmware/device-credentials/factory-nvs). Name the image in `RMNG_FLASH_IMAGE` when starting the app; the SDK loads it before `app_main()`. The app writes its NVS into the image, so run it on a copy. For example, with the partition table at `0x8000` and `fctry` at `0x10000`:

  ```sh
  esptool --chip esp32 merge-bin --pad-to-size 4MB -o flash.img 0x8000 build/partition_table/partition-table.bin 0x10000 fctry.bin
  cp flash.img flash_run.img
  RMNG_FLASH_IMAGE=flash_run.img idf.py monitor
  ```
