# Camera (ESP32-P4-EYE)

A WebRTC camera for the **ESP32-P4X-EYE** and **ESP32-P4-EYE**. Live video
streams to the ESP RainMaker Home app over Amazon Kinesis Video Streams WebRTC.
The camera, the WebRTC signaling and the media run on the ESP32-P4. The on-board
ESP32-C6 provides Wi-Fi and BLE.

Pick the image for your board:

- **ESP32-P4X-EYE** (P4 silicon v3 or later): `camera_standalone_p4x_eye`
- **ESP32-P4-EYE** (P4 silicon before v3): `camera_standalone_p4_eye`

## On-device behavior

- **Co-processor:** the image carries the C6 firmware (`network_adapter`). On the
  first boot the P4 writes it to the C6, which adds about 30 seconds. Later boots
  skip this step.
- **Camera:** the on-board OV2710 sends video over MIPI-CSI, encoded to H.264 on
  the chip.
- **Live view:** a viewer session starts when the app opens the camera. The node
  reports a read-only `Channel` parameter with its signaling channel name.
- **Build:** ESP-IDF v6.1.1 with a CSI driver fix for RAW sensors that is not in
  an ESP-IDF release yet.

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
2. Provision the camera over BLE by scanning its QR code. The P4 console (USB)
   prints the QR code.
3. Open the camera in the app to start the live view.
