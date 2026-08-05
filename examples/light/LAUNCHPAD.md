# Light

A colour smart light. Power, brightness, hue, saturation, colour temperature
(CCT) and light mode are all controllable from the ESP RainMaker Home app and
the cloud.

## On-device behavior

- **LED** (on-board RGB LED, present on most ESP dev boards) shows the current
  light state.
- **Colour spaces** — the light tracks either HSV or CCT and switches
  automatically: changing hue/saturation switches to HSV, changing CCT switches
  to CCT. Any colour change in the active mode also turns the light on.
- **Button** (BOOT button by default):
  - **Short press** — toggle power.
  - **Long press** — randomize hue and saturation.

## Before you flash

**Public ESP RainMaker Neo deployment (default):** nothing extra to flash. This
firmware obtains its cloud credentials over the provisioning session, so flash it
and provision it from the ESP RainMaker Home app.

**Private or self-hosted deployment:** that does not apply, and this firmware ships
**without** a factory partition. Also flash a factory partition binary, obtained
from your deployment's dashboard, at this example's `fctry` offset. The offsets and
step-by-step instructions are in the README shown on the Launchpad landing page.

## After flashing

1. Open the **ESP RainMaker Home** app.
2. Provision the device (Wi-Fi / Thread) by scanning its QR code.
3. Control the light from the app.
