# app_button

Button helper used by the examples: short- and long-press callbacks on the BOOT button by
default, with hold-to-reset provided by the `app_reset` component.

On **ESP-IDF** it wraps the [`espressif/button`](https://components.espressif.com/components/espressif/button)
component; on **POSIX** there is no physical button, so a stub backend accepts the
same configuration and does nothing.

## Required configuration

Set via `idf.py menuconfig` → *ESP RainMaker App Button Configuration*:

| Option | Default | Purpose |
|---|---|---|
| `APP_BUTTON_GPIO_NUM` | BOOT button (`0`; `9` on C2/C3/C6/H2, `28` on C5) | Button input GPIO |
| `APP_BUTTON_IS_ACTIVE_HIGH` | `n` | Button polarity |
| `APP_BUTTON_SHORT_PRESS_TIME_MS` | `50` | Press time before the short-press action |
| `APP_BUTTON_LONG_PRESS_TIME_MS` | `1000` | Hold time before the long-press action |

If your board's button is on a different pin or wired active-high, change these or
the button will do nothing.

## Reset functions

Hold-to-reset lives in the separate [`app_reset`](../app_reset/README.md) component, which
this one registers on the button for you. It is **enabled by default**: holding ~5 s performs
a network reset and ~10 s a factory reset. See that README for the thresholds, the
`APP_RESET_*` options and how to turn it off.

A long-press action you register here is skipped when the hold has already crossed a reset
threshold, so holding for a reset does not also fire the long press on release.

## Usage

```c
#include <app_button.h>

app_button_config_t config = {
    .callbacks = {
        .on_short_press = my_short_press_cb, /* NULL if not needed */
        .on_long_press  = my_long_press_cb,  /* NULL if not needed */
    },
};
app_button_init(&config);
```

Callbacks match `espressif/button`'s `button_cb_t` shape
(`void (*)(void *button_handle, void *usr_data)`).
