# ESP RainMaker Neo WebRTC Camera

KVS WebRTC camera on ESP RainMaker Neo. The device connects as MASTER to its
own signaling channel (`rmng-v1-{node_id}`) using temporary AWS credentials
from the AWS IoT Credential Provider (node certificate → video role alias).
Viewers connect through the phone app or dashboard, which get their own
credentials from the cloud.

Unlike the other examples these are **ESP-IDF only**: the KVS media plane has
no POSIX port.

| Mode | Where the node + signaling run | Where media runs | Use it for |
| --- | --- | --- | --- |
| [`standalone/`](standalone/README.md) | ESP32-P4 | ESP32-P4 (C6 = plain Wi-Fi/BLE co-processor) | Simplest bring-up, fastest session setup |
| [`split_mode/`](split_mode/README.md) | ESP32-C6 | ESP32-P4 (KVS `streaming_only`) | P4 can sleep while the C6 keeps the node + signaling online |

The example is not tied to a board. Board overlays are provided for the
**ESP32-P4-EYE** and the **ESP32-P4X-EYE** (`p4_eye` overlays, plus
`pre_v3` for P4 silicon before v3). Both have an
on-board ESP32-C6 without its own USB port, so their overlays make the P4 image
carry the C6 firmware and flash it in-system at boot (`slave_flasher`).

[`components/rmaker_camera`](components/rmaker_camera/) is shared by both
modes: the camera device (with its read-only `Channel` parameter), the IoT
Credential Provider client (on `espressif/rmaker_aws_sign`), and the KVS
signaling bring-up. Each mode plugs in
its own peer-connection interface — the local KVS peer connection
(standalone) or the bridge to the streaming chip (split).

## Prerequisites

- A local clone of **esp-port-for-amazon-kvs-sdk** (with submodules), at
  `v1.0.0-rc1` or later. CI builds against `v1.0.0-rc1`
  (`.gitlab/examples_camera.yml`).

  ```sh
  git clone --recursive --branch v1.0.0-rc1 https://github.com/espressif/esp-port-for-amazon-kvs-sdk.git
  export KVS_SDK_PATH=/path/to/esp-port-for-amazon-kvs-sdk
  ```

- Node credentials. By default none need to be flashed: assisted claiming
  (`CONFIG_ESP_RMAKER_NEO_ASSISTED_CLAIM`, on by default) has the phone app supply
  the node certificate during BLE provisioning. Alternatively, pre-register a
  node **with the `kvs` capability** and flash its factory NVS to the `fctry`
  partition. See [Factory NVS](https://docs.neo.rainmaker.espressif.com/docs/firmware/device-credentials/factory-nvs).

- Nothing deployment-specific at build time:
  - At claim, the examples request the `kvs` capability
    (`CONFIG_ESP_RMAKER_NEO_CLAIM_VIDEOSTREAM_SUPPORT`).
  - They read the credential endpoint, video role alias and region from the
    cloud's server config (`CONFIG_ESP_RMAKER_NEO_SERVER_CONFIG_VIDEOSTREAM_SUPPORT`,
    selected by the `rmaker_camera` component).
  - Signaling starts only after the server config arrives. The node asks for it
    again on each connection until it does.

## Notes / current limitations

- **RAW camera sensors stream only on ESP-IDF v5.5.5 for now.** The P4
  overlays here use RAW MIPI-CSI sensors. Later IDF versions reject the RAW
  input at CSI init, so video does not start. The fix is espressif/esp-idf!53677.
  Until it lands, build the P4 image with the v5.5.5 tag.
- WebRTC streaming only: no file or clip upload, no insights, no
  command-response.
