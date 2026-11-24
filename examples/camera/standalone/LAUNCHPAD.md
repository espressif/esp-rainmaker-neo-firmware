# Camera

A WebRTC camera for the **ESP32-S3-EYE**. Live video streams to the ESP RainMaker
Home app over Amazon Kinesis Video Streams WebRTC. The camera, the WebRTC
signaling and the media all run on the ESP32-S3.

## On-device behavior

- **Camera:** the on-board OV2640 sends QVGA (320x240) video, encoded to H.264 on
  the chip.
- **Live view:** a viewer session starts when the app opens the camera. The node
  reports a read-only `Channel` parameter with its signaling channel name.

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
2. Provision the camera over BLE by scanning its QR code.
3. Open the camera in the app to start the live view.
