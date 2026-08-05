# Multi-device

A single firmware exposing four devices at once — a **light** (power +
brightness), a **fan** (power + swing + speed), a **switch** (power) and a
read-only **temperature sensor** — all controllable from the ESP RainMaker Home
app and the cloud.

## On-device behavior

One shared LED and the single BOOT button drive all four devices through a
**focus** cursor.

- **LED** (on-board RGB LED) shows the currently focused device:
  - **Light** — green, brightness follows the light.
  - **Fan** — blue; hue shifts with speed, brightness "breathes" while swinging.
  - **Switch** — red when on, off when off.
  - **Temperature sensor** — blue (cold) to red (hot) with the current reading.
- **Button** (BOOT button by default):
  - **Short press** — cycle focus (Light → Fan → Switch → Temperature Sensor).
  - **Long press** — act on the focused device:
    - **Light** — toggle power; when turning on, step brightness by 25 % (wraps).
    - **Fan** — toggle power; when turning on, cycle speed (1–5).
    - **Switch** — toggle power.
    - **Temperature sensor** — read-only; only logs the current reading.

The temperature sensor's reading is simulated (random walk within 0–50 °C) and
reported as a time series.

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
3. Control the devices from the app.
