# Matter Light

A full color light (CCT + HSV) that is a plain RainMaker Neo node to the cloud and an
**Extended Color Light** (device type `0x010D`) to a Matter fabric.

The application code is [`examples/light`](../../light) — the same
power/brightness/CCT/hue/saturation/light-mode params, the same bulk write callback —
plus one call:

```c
esp_rmaker_matter_mirror_enable(node, NULL);
```

[`esp_rmaker_neo_matter_mirror`](../../../components/esp_rmaker_neo_matter_mirror)
lowers the node's devices/params into the endpoint/cluster tree from its mapping
table (CT + HS features, with CurrentX/CurrentY derived from hue+saturation) and
keeps both sides in sync: cloud/app/schedule changes appear on the fabric, and
controller commands land in the same write callback the cloud uses.

## Behaviour

The standard power, hue, saturation, brightness, CCT, light-mode and identify
parameters. Writes arrive together in the bulk callback, which drives the LED and
auto-switches between HSV and CCT modes: changing hue/saturation switches to HSV,
changing CCT switches to CCT. The boot button toggles power on a short press and
picks a random CCT on a long press.

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

Verify afterwards that Power / Brightness / CCT / hue-saturation drive the LED from
both the RainMaker app and the Matter controller, and that the boot button's changes
are reported back to both.

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

1. Provision the device with the RainMaker phone app (BLE) and verify remote control
   of Power / Brightness / CCT.
2. Commission the on-network node from the controller of your choice, using the Matter
   QR code associated with the factory partition (the attestation caveat under
   [Matter-first](../README.md#matter-first-default) applies here too).
3. Cross-side sync checks. Each one is a controller action and its RainMaker-side
   effect (or the reverse); the chip-tool equivalents are given for node `0x11`,
   endpoint 1:
   - Toggle the light from the controller — the LED changes and the RainMaker app
     updates (`chip-tool onoff toggle 0x11 1`).
   - Set CCT from the RainMaker app — the controller shows the converted color
     temperature in mireds
     (`chip-tool colorcontrol read color-temperature-mireds 0x11 1`).
   - Dim with a transition time from the controller — the LED fades and the cloud
     receives the settled value alone, intermediate steps coalesced
     (`chip-tool levelcontrol move-to-level-with-on-off 200 20 0 0 0x11 1`).
   - Ask the controller to identify the light — the LED blinks for the requested
     duration, and each TriggerEffect renders the spec example for a colored
     light: blink once, breathe 15 times, green for 1 s, orange for 8 s
     (`chip-tool identify identify 30 0x11 1`,
     `chip-tool identify trigger-effect 1 0 0x11 1`, `trigger-effect 255 0`).
