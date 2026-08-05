# Temperature Sensor

A smart temperature sensor. It exposes one read-only **Temperature** parameter
(°C) recorded as a time series, so the ESP RainMaker Home app plots its history.

## On-device behavior

- The reading is **simulated**: there is no temperature hardware. It starts at
  25 °C and random-walks by up to ±2 °C per update, clamped to 0–50 °C, and each
  new reading is reported to the cloud.
- **LED** (on-board RGB LED) visualizes the reading:
  - **Cold (0 °C)** — blue (hue 240°).
  - **Hot (50 °C)** — red (hue 0°).
- The BOOT button is not used; the sensor is read-only.

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
3. Watch the temperature and its history in the app.
