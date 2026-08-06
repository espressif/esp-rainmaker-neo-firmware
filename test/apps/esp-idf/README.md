# ESP-IDF test app (unified)

Single ESP-IDF test application for all on-device tests: **osal**, **esp_rmaker_neo_common**, **esp_rmaker_neo**, and **esp_rmaker_neo_ota**.

## Supported targets

| ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-S3 |
| ----- | -------- | -------- | -------- | -------- | -------- |

Every SDK-supported target, all of which have Wi-Fi. Some test suites need a network to reach their servers; the rest run offline.

## Setup

1. **Wi-Fi:** Set `TEST_APP_WIFI_SSID` and `TEST_APP_WIFI_PASSWORD` under *ESP-IDF Test App Configuration* in `idf.py menuconfig`.
2. **MQTT tests:** Follow the [MQTT test setup](../../../components/osal/mqtt/test_osal_mqtt/README.md) if you will run MQTT-related tests.
3. **esp_rmaker_neo tests:** Flash a factory NVS partition for the device. Use the [factory NVS tool](../../../tools/factory_nvs_gen/) to generate the binary, then flash it at the `fctry` offset from `partitions.csv` (e.g. via `esptool`).

## Build and run

From this directory (`test/apps/esp-idf`):

```bash
idf.py set-target <board target>
idf.py build
```

Then either:

- **Pytest (all cases):**
  ```bash
  pytest --port <device port> pytest_test_all.py
  ```
  `pytest_test_all.py` is parametrized over the targets above; `conftest.py` keeps only the one the
  build directory was configured for. Pass `--target <board target>` to override.
- **Interactive:**  
  ```bash
  idf.py -p <device port> flash monitor
  ```
  and run test cases from the Unity menu.

### ESP32-C2 (and other non-default UART baudrates)

`CONFIG_XTAL_FREQ_26=y` changes the default monitor baudrate. Use the provided `sdkconfig.defaults.esp32c2` (already sets `CONFIG_ESP_CONSOLE_UART_CUSTOM=y` and `CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200`) so `pytest` and monitor work correctly. Otherwise use `idf.py monitor` and run tests manually.
