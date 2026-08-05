# Fan

A single fan example that builds for both ESP-IDF hardware and the POSIX host.
It follows the standard RainMaker example layout — [`main/app_main.c`](main/app_main.c)
(agent bring-up + bulk write callback) and [`main/app_driver.c`](main/app_driver.c)
(LED status animation + button) behind [`main/app_priv.h`](main/app_priv.h) — with
every OS difference hidden behind interfaced common components.

## Structure

| Concern | Where it lives | ESP-IDF | POSIX |
| --- | --- | --- | --- |
| App logic | `main/app_main.c` (`app_run`) + `main/app_driver.c` | shared | shared |
| Entry point | `examples/common/app_entry` | `void app_main(void)` | `int main(void)` + signal/pause/teardown |
| LED | `examples/common/app_led` | LEDC / WS2812 driver | no-op stub |
| Button | `examples/common/app_button` | GPIO (espressif/button) | no-op stub |
| LED animation task | `osal_task_*` | FreeRTOS task | pthread |
| Network / provisioning | `examples/common/app_network` | Wi-Fi + [User-Node Association](https://docs.neo.rainmaker.espressif.com/docs/product-overview/concepts/user-node-association) | no-op |
| Reboot / OTA | `components/osal/ota` | real bootloader | mock bootloader + exit code |

`app_run()` reads top-to-bottom as plain application logic: initialise console →
driver → storage → network → node → services → OTA → start. There are no OS
`#ifdef` in the example beyond the pre-existing challenge-response service toggle.

## Behaviour

The fan exposes three writable parameters: **power**, **swing** (`esp.param.direction`)
and **speed** (1-5). A single button controls it:
- **Short press**: toggle *power*.
- **Long press**: toggle *swing* and cycle *speed* (1-5).

The on-board RGB LED shows fan status via a background animation task:
- **Power ON**: hue shifts gradually, faster at higher speeds.
- **Swing ON**: brightness "breathes" up and down.

On POSIX the LED/button are simulated — state changes are logged as `!!!HARDWARE!!!`.

## Configuration

- **LED module**: follow [these steps](../common/app_led/README.md#required-configuration).
- **Button**: follow [these steps](../common/app_button/README.md#required-configuration), if not using the BOOT button.

If using a Thread network, please [enable the DNS64 client properly](../common/app_network/README.md#connecting-to-ipv4-servers-on-thread).

You should [flash the factory NVS partition](../README.md#device-credentials) before execution.

## Build and Run

Standard flow for every example — see the [examples README](../README.md).
The POSIX binary is `./fan`.
