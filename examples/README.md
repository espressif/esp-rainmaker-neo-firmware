# Examples

Every example builds for ESP-IDF hardware from one source tree, following the
same layout: `main/app_main.c` (agent bring-up + write callbacks) and
`main/app_driver.c` (hardware) behind `main/app_priv.h`, with every OS
difference hidden behind the shared [`common/`](common/) components. Each
example also builds unchanged on a POSIX host against simulated hardware —
useful for development and CI without a device.

| Example | Purpose |
| --- | --- |
| [`light`](light/) | Single lightbulb (power, hue, saturation, brightness, CCT) — the usual entry point |
| [`switch`](switch/) | Single switch with LED indicator |
| [`fan`](fan/) | Fan with speed, swing and an LED status animation |
| [`temp_sensor`](temp_sensor/) | Read-only temperature sensor with time-series reporting |
| [`multi_device`](multi_device/) | Light + fan + switch + temperature sensor in one node |
| [`advanced/`](advanced/README.md) | SDK extension points rather than device types (custom OTA filetypes, custom jobs) |

[`common/`](common/) holds the `app_*` helper components the examples share —
entry point, network/provisioning bring-up, LED, button, hold-to-reset — plus
the shared CI sdkconfig variants.

## Prerequisites

- **ESP-IDF**: an exported ESP-IDF environment, **v6.0.2 or later**
  (`. $IDF_PATH/export.sh`); no extra install steps beyond ESP-IDF's own.
- **POSIX**: CMake ≥ 3.16, GCC or Clang, Ninja or Make, and a Python virtual
  environment with `pip install -r posix_requirements.txt` run from the
  repository root (used by the Kconfig and mbedTLS scripts at configure time).
  The first configure fetches third-party sources, so it needs network access.

A node also needs credentials before it can connect — see
[Device Credentials](#device-credentials).

## Build — ESP-IDF

From the example directory:

```sh
idf.py set-target <chip>
idf.py build flash monitor
```

Per-target overrides are merged from `sdkconfig.defaults.<target>` next to the
base `sdkconfig.defaults`; `idf.py menuconfig` exposes the RMNG options.

## Build — POSIX (Host Testing)

The POSIX build runs the same example against mocked hardware; it is currently
intended for testing, not production. From the example directory:

```sh
cmake -B build
cmake --build build
```

`cmake --build build --target menuconfig` opens the same Kconfig UI. Copy the
generated `nvs_persistent/` folder ([Device Credentials](#device-credentials))
next to the executable, then run the binary (named after the example) from the
build's partitions directory — it starts the mock bootloader, which launches
the app. Send `SIGINT` (Ctrl-C) for a clean teardown.

## Device Credentials

A node's credentials and connectivity settings (certificates, private key,
MQTT host) are handled by a **factory NVS partition**. Use the
[factory_nvs_gen](../tools/factory_nvs_gen/README.md) tool to generate the
binary — [factory_autoreg](../tools/factory_autoreg/README.md) covers batch
creation and registration, and the
[dashboard registration guide](https://docs.neo.rainmaker.espressif.com/docs/firmware/device-credentials/dashboard-batch)
covers the dashboard flow. Then:

- (ESP-IDF) flash the binary to the factory NVS partition, defined in a custom
  partition table ([example](switch/partitions.csv)).
- (POSIX) copy the generated
  [`nvs_persistent`](../tools/factory_nvs_gen/README.md#posix) folder to the
  same root directory as the built executable.

Alternatively,
[**assisted claiming**](https://docs.neo.rainmaker.espressif.com/docs/firmware/device-credentials/claiming)
lets an ESP-IDF node generate its own key and obtain a certificate at first
setup via the phone app over BLE (on by default when BLE is enabled) — use one
approach or the other, not both.

## Going Further

Full walkthroughs — setup, configuration reference, provisioning, OTA and
troubleshooting — live in the
[firmware guides](https://docs.neo.rainmaker.espressif.com/docs/firmware/).
