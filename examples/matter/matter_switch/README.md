# Matter Switch

A dimming switch that is a plain RainMaker Neo node to the cloud and a **Mounted
Dimmable Load Control** (device type `0x0110`) to a Matter fabric.

The application code is [`examples/switch`](../../switch) — the same power param, plus a
dim level, through the same bulk write callback — and one call:

```c
esp_rmaker_matter_mirror_enable(node, NULL);
```

[`esp_rmaker_neo_matter_mirror`](../../../components/esp_rmaker_neo_matter_mirror)
lowers the node's devices/params into the endpoint/cluster tree from its mapping
table and keeps both sides in sync: cloud/app/schedule changes appear on the fabric,
and controller commands land in the same write callback the cloud uses.

The switch-side dim level is `esp.param.dim`
(`esp_rmaker_dim_param_create()`), the counterpart of a light's brightness: it maps to
LevelControl, and its presence is what selects the dimmable rule. The mapping's rules are
param-subset ordered, so the endpoint follows the data model — drop the dim param and the
same build lowers the device to a **Mounted On/Off Control** (`0x010F`), no other
application change.

## Behaviour

The standard power, dim and identify parameters. Writes arrive together in the bulk
callback, which drives the on-board LED as the load: power switches it, and the dim level
sets its brightness. The boot button toggles power on a short press and steps the dim
level on a long press, reporting the new value to both sides. A dim level change also
switches power on, from the button, the app or a controller, as `MoveToLevelWithOnOff` does.

## Build and Run

Standard flow for every Matter example (esp-matter checkout, factory partition,
onboarding modes) — see the [Matter examples README](../README.md).

```sh
idf.py set-target esp32c3
idf.py build flash monitor
```

## Trying it out

Flash the [factory partition](../README.md#factory-data-and-credentials), then onboard
per the configured mode — the default build is Matter-first: two commissionings, run
back to back as one flow by RainMaker Home, or first-fabric-elsewhere plus a manual
pairing window when another Matter app starts it; see
[Onboarding](../README.md#onboarding).

Verify afterwards that Power and the dim level drive the LED from both the RainMaker app
and the Matter controller, and that the boot button's changes are reported back to both.

## On-network onboarding (external Matter controllers)

[`sdkconfig.ci.onnetwork`](../common/ci-sdkconfig/sdkconfig.ci.onnetwork) switches the example to
[on-network onboarding](../README.md#onboarding): RainMaker BLE provisioning brings up
Wi-Fi, and the controller commissions the node over IP.

```sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;../common/ci-sdkconfig/sdkconfig.ci.onnetwork" \
  set-target esp32c3
idf.py build flash monitor
```

Then:

1. Provision the device with the RainMaker phone app (BLE) and verify remote control of
   Power and the dim level.
2. Commission the on-network node from the controller of your choice, using the Matter
   QR code associated with the factory partition (the attestation caveat under
   [Matter-first](../README.md#matter-first-default) applies here too).
3. Cross-side sync checks. Each one is a controller action and its RainMaker-side
   effect (or the reverse); the chip-tool equivalents are given for node `0x11`,
   endpoint 1:
   - Toggle the switch from the controller — the LED changes and the RainMaker app
     updates (`chip-tool onoff toggle 0x11 1`).
   - Toggle it from the RainMaker app — the controller's reported state follows
     (`chip-tool onoff read on-off 0x11 1`).
   - Dim with a transition time from the controller — the LED fades and the cloud
     receives the settled value alone, intermediate steps coalesced
     (`chip-tool levelcontrol move-to-level-with-on-off 200 20 0 0 0x11 1`).
   - Press the boot button — both sides update.
   - Ask the controller to identify the switch — the LED blinks for the requested
     duration, and each TriggerEffect renders the spec example for a non-colored
     light: blink once, breathe 15 times, two flashes, maximum then minimum
     brightness over 8 s
     (`chip-tool identify identify 30 0x11 1`,
     `chip-tool identify trigger-effect 1 0 0x11 1`, `trigger-effect 255 0`).
