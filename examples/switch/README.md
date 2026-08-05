# Switch

A single switch example that builds for both ESP-IDF hardware and the POSIX
host. It follows the standard RainMaker example layout — [`main/app_main.c`](main/app_main.c)
(agent bring-up + bulk write callback) and [`main/app_driver.c`](main/app_driver.c)
(LED indicator + button) behind [`main/app_priv.h`](main/app_priv.h) — with every OS
difference hidden behind interfaced common components.

## Structure

| Concern | Where it lives | ESP-IDF | POSIX |
| --- | --- | --- | --- |
| App logic | `main/app_main.c` (`app_run`) + `main/app_driver.c` | shared | shared |
| Entry point | `examples/common/app_entry` | `void app_main(void)` | `int main(void)` + signal/pause/teardown |
| Error signalling | `APP_RETURN_ON_ERR` (returns `os_err_t`) | discarded by `app_main` | mapped to `POSIX_EXIT_FAILURE` |
| LED | `examples/common/app_led` | LEDC / WS2812 driver | no-op stub |
| Button | `examples/common/app_button` | GPIO (espressif/button) | no-op stub |
| Network / provisioning | `examples/common/app_network` | Wi-Fi + [User-Node Association](https://docs.neo.rainmaker.espressif.com/docs/product-overview/concepts/user-node-association) | no-op |
| Reboot / OTA | `components/osal/ota` | real bootloader | mock bootloader + exit code |

`app_run()` reads top-to-bottom as plain application logic: initialise console →
LED → button → storage → network → node → services → OTA → start. There are no
OS `#ifdef` in the example beyond the pre-existing challenge-response service
toggle.

## Behaviour

LED is red when the switch is ON, off when OFF. A single button toggles power
(short and long press behave the same). On POSIX the LED/button are simulated —
power changes are logged as `!!!HARDWARE!!!`.

## Build and Run

Standard flow for every example — see the [examples README](../README.md).
The POSIX binary is `./switch`.
