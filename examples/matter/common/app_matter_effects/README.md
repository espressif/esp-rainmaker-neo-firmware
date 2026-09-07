# Matter Identify Effects

The Identify effect examples of the Matter spec (cluster 0x0003, `TriggerEffect`), expressed as
[`app_led`](../../../common/app_led) patterns. A Matter example asks for the pattern that belongs to an effect id and plays
it; the lengths, the colours and the spec reading live here, not in the example.

## Variants

The spec gives two examples for the effects that a colour changes, so
`app_matter_effects_get()` takes what the device can render:

| Effect          | `APP_MATTER_EFFECT_LIGHT_NON_COLORED`        | `APP_MATTER_EFFECT_LIGHT_COLORED` |
| --------------- | -------------------------------------------- | --------------------------------- |
| `Blink`         | on and off once                              | on and off once                   |
| `Breathe`       | on and off over 1 s, 15 times                | on and off over 1 s, 15 times     |
| `Okay`          | two flashes                                  | green for 1 s                     |
| `ChannelChange` | maximum brightness 0.5 s, then minimum 7.5 s | orange for 8 s                    |

`FinishEffect` and `StopEffect` name no pattern: they end the running effect, so the call returns
`OSAL_ERR_NOT_SUPPORTED` and the example calls `app_led_effect_stop_at_cycle_end()` or
`app_led_effect_stop()`. A plain `Identify` names no effect either, and blinks for `IdentifyTime`.
