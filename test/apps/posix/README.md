# POSIX test app

Single POSIX project to build and run all unit tests for **esp_rmaker_neo**, **esp_rmaker_neo_ota**, **osal** and the MQTT/HTTP suites.

`TEST_ESP_RMAKER_NEO` and `TEST_ESP_RMAKER_NEO_OTA` are set in this project so both test suites are built. esp_rmaker_neo_ota still uses the POSIX config stub under `components/esp_rmaker_neo_ota/config` (sdkconfig.h) for C preprocessor defines.

## Build

From this directory (`test/apps/posix`):

```bash
mkdir build
cmake -B build -GNinja && ninja -C build
```

## Run tests

```bash
# All tests
ctest --test-dir build [--verbose]

# By sub-component (examples)
ctest --test-dir build/components/esp_rmaker_neo [--verbose]
ctest --test-dir build/components/esp_rmaker_neo_ota [--verbose]
ctest --test-dir build -R osal_platform_tests [--verbose]
```

To use the esp_rmaker_neo tests from another project, pass `-DTEST_ESP_RMAKER_NEO=ON` and `-DTEST_ESP_RMAKER_NEO_OTA=ON` when configuring CMake.

## Coverage

Run in this order:

1. `esp_rmaker_neo_coverage_test` – clears coverage data and runs CTest.
2. `esp_rmaker_neo_coverage_gen` – generates report under `build/coverage/esp_rmaker_neo`.

Requires [gcovr](https://gcovr.com/en/stable/) on `PATH` (e.g. `pip install -U gcovr`).

## Setup after build (test_rmaker_neo)

For the esp_rmaker_neo test component you need a factory NVS setup:

1. Use the [factory NVS tool](../../../tools/factory_nvs_gen/) to generate the binary.
2. Copy the generated `nvs_persistent` folder to `build/components/esp_rmaker_neo/test_rmaker_neo/`.
