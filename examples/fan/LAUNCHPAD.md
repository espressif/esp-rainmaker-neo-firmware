# Fan

A smart fan. Power, speed (1–5) and swing are controllable from the ESP
RainMaker Home app and the cloud.

## On-device behavior

- **LED** (on-board RGB LED) shows fan status:
  - **Power ON** — hue shifts gradually, faster at higher speeds.
  - **Swing ON** — brightness rises and falls ("breathing").
- **Button** (BOOT button by default):
  - **Short press** — toggle power.
  - **Long press** — toggle swing and cycle speed (1–5).

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
3. Control the fan from the app.
