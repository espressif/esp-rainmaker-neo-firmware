# Multi-Device

A single node that exposes four devices at once — a **light** (power +
brightness), a **fan** (power + swing + speed), a **switch** (power) and a
**temperature sensor** (read-only, time-series) — built for both ESP-IDF
hardware and the POSIX host from one source tree. It follows
the standard RainMaker example layout — [`main/app_main.c`](main/app_main.c)
(agent bring-up + one bulk write callback per device) and
[`main/app_driver.c`](main/app_driver.c) (shared LED indicator + button behind
[`main/app_priv.h`](main/app_priv.h)) — with every OS difference hidden behind
interfaced common components.

## Structure

| Concern | Where it lives | ESP-IDF | POSIX |
| --- | --- | --- | --- |
| App logic | `main/app_main.c` (`app_run`) + `main/app_driver.c` | shared | shared |
| Entry point | `examples/common/app_entry` | `void app_main(void)` | `int main(void)` + signal/pause/teardown |
| LED | `examples/common/app_led` | LEDC / WS2812 driver | no-op stub |
| Button | `examples/common/app_button` | GPIO (espressif/button) | no-op stub |
| Fan LED animation | `main/fan_sim.c` | FreeRTOS task | POSIX thread |
| Temperature simulation | `main/temp_sim.c` | FreeRTOS task | POSIX thread |
| Network / provisioning | `examples/common/app_network` | Wi-Fi + [User-Node Association](https://docs.neo.rainmaker.espressif.com/docs/product-overview/concepts/user-node-association) | no-op |
| Reboot / OTA | `components/osal/ota` | real bootloader | mock bootloader + exit code |

Each device gets its own bulk write callback. `app_run()` reads top-to-bottom
as plain application logic: initialise console → drivers → storage → network →
node → four devices → services → OTA → start. There are no OS `#ifdef` in the
example beyond the pre-existing challenge-response service toggle.

## Behaviour

One shared on-board LED and the single boot button drive all four devices via a
**focus** cursor:

- **Short press** cycles the focus (Light → Fan → Switch → Temp Sensor) and
  points the LED at the newly focused device — light is green, fan is blue
  (animated: hue shifts with speed, brightness "breathes" while swinging),
  switch is red, temperature sensor runs blue (cold) to red (hot) with the
  current reading.
- **Long press** actuates the focused device: the light toggles power and steps
  brightness, the fan toggles power and cycles speed, the switch toggles power.
  Each change is reported to the cloud. The temperature sensor is read-only, so
  a long press only logs the current reading.

The temperature sensor's **Temperature** parameter (`esp.param.temperature`,
float °C, `PROP_FLAG_READ | PROP_FLAG_TIME_SERIES`) is driven by
[`main/temp_sim.c`](main/temp_sim.c): once a second it
nudges the standing reading by a random delta in ±2.0 °C, clamps it to
0–50 °C, and the driver reports it with `esp_rmaker_param_update()`.

On POSIX the LED/button are simulated — hardware changes are logged as
`!!!<DEVICE> HARDWARE!!!` and there is no physical button.

## Build and Run

Standard flow for every example — see the [examples README](../README.md).
The POSIX binary is `./multi_device`.
