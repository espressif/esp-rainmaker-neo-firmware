# app_led

Status/indicator LED helper used by the examples. Drives either a **WS2812 LED
strip** (single data GPIO) or an RGB LED on three **LEDC** PWM channels.

On **POSIX** builds a stub backend accepts the same API and does nothing.

## Required configuration

Set via `idf.py menuconfig` → *ESP RainMaker App LED Configuration*. A wrong LED type or
GPIO yields a silently dark LED, so check these first:

| Option | Default | Purpose |
|---|---|---|
| `APP_LED_TYPE` | `WS2812` (`LEDC` on ESP32-C2) | LED protocol |
| `APP_LED_WS2812_GPIO_NUM` | `18` (`8` on C3/C6/H2, `48` on S3, `27` on C5) | WS2812 data GPIO |
| `APP_LED_LEDC_GPIO_NUM_R` / `_G` / `_B` | `0` / `1` / `8` | LEDC channel GPIOs |

The WS2812 defaults match the addressable LED on common Espressif devkits; boards
with a plain RGB LED (e.g. ESP32-C2 devkits) use the LEDC backend.

## Effects

`app_led_effect_start()` plays an attention pattern over the tracked state. The caller describes
the pattern: a waveform (steady, pulse, ramp), the length of one cycle, the brightness of the low
phase, the number of cycles (0 to run until stopped) and, where it wants one, a colour of its own.
It ends at `app_led_effect_stop()` (at once) or `app_led_effect_stop_at_cycle_end()`. The pattern
drives the brightness, and the colour where it carries one, and it runs whatever the tracked power
says, so it stays visible on a light the user turned off. An animation task exists only while an
effect plays.

A frame lasts 50 ms, so a cycle is 100 ms or longer, and a multiple of 50 ms renders exactly.
