# Temperature Sensor

A single temperature-sensor example that builds for both ESP-IDF hardware and
the POSIX host. It follows the standard RainMaker example layout —
[`main/app_main.c`](main/app_main.c) (agent bring-up + bulk write callback) and
[`main/app_driver.c`](main/app_driver.c) (RGB-LED temperature indicator + the
sensor simulation) behind [`main/app_priv.h`](main/app_priv.h) — with every OS
difference hidden behind interfaced common components.

## Structure

| Concern | Where it lives | ESP-IDF | POSIX |
| --- | --- | --- | --- |
| App logic | `main/app_main.c` (`app_run`) + `main/app_driver.c` | shared | shared |
| Entry point | `examples/common/app_entry` | `void app_main(void)` | `int main(void)` + signal/pause/teardown |
| LED indicator | `examples/common/app_led` | LEDC / WS2812 driver | no-op stub |
| Temperature simulation | `main/temp_sim.c` | FreeRTOS task | POSIX thread |
| Network / provisioning | `examples/common/app_network` | Wi-Fi + [User-Node Association](https://docs.neo.rainmaker.espressif.com/docs/product-overview/concepts/user-node-association) | no-op |
| Reboot / OTA | `components/osal/ota` | real bootloader | mock bootloader + exit code |

`app_run()` reads top-to-bottom as plain application logic: initialise console →
driver → storage → network → node → services → OTA → start → simulation.

## Behaviour

The node exposes one `esp.device.temperature-sensor` device with a single
**Temperature** parameter (`esp.param.temperature`, float °C,
`PROP_FLAG_READ | PROP_FLAG_TIME_SERIES`) — read-only and recorded as a time
series, so the phone app plots its history.

[`main/temp_sim.c`](main/temp_sim.c) simulates the sensor: once a second it
nudges the standing temperature by a random delta in ±2.0 °C, clamps it to
0–50 °C, and invokes the example's callback, which calls
`esp_rmaker_param_update()` and drives the on-board RGB LED (blue = cold,
red = hot). Range, delta, period and the initial reading are all
`temp_sim_config_t` fields; the example uses the defaults. On POSIX
the LED is simulated — changes are logged as `!!!HARDWARE!!!`.

## Build and Run

Standard flow for every example — see the [examples README](../README.md).
The POSIX binary is `./temp_sensor`.
