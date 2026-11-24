# ESP RainMaker Neo WebRTC Camera: standalone mode (ESP32-P4 or ESP32-S3)

The RainMaker Neo node, KVS signaling and the media plane (capture, H.264 encode,
RTP/SRTP) all run on one chip.

- **ESP32-P4:** the on-board ESP32-C6 is a plain Wi-Fi + BLE co-processor running
  the KVS SDK's `network_adapter` over esp_hosted (SDIO for Wi-Fi and for the BLE
  provisioning HCI).
- **ESP32-S3:** native Wi-Fi and BLE, no co-processor. The ESP32-S3-EYE overlay
  uses its OV2640 over DVP at QVGA.

See the [camera overview](../README.md) for prerequisites (IDF, `KVS_SDK_PATH`,
node registration, credential endpoint).

## 1. Co-processor image (ESP32-P4 only)

Build `network_adapter` for the C6 and copy its images into
[`target-firmware/`](target-firmware/README.md). The `p4_eye` overlay embeds
them in the P4 app, and the P4 flashes the C6 in-system at boot. The build fails
if they are missing.

## 2. Build & flash

| Board | Overlay chain (`SDKCONFIG_DEFAULTS`) |
| --- | --- |
| ESP32-P4X-EYE (P4 v3 or later) | `sdkconfig.defaults;sdkconfig.defaults.p4_eye.esp32p4` |
| ESP32-P4-EYE (P4 before v3) | `sdkconfig.defaults;sdkconfig.defaults.p4_eye.esp32p4;sdkconfig.defaults.pre_v3.esp32p4` |
| ESP32-P4 board with an SC2336 sensor, such as the ESP32-P4 Function EV Board | `sdkconfig.defaults;sdkconfig.defaults.sc2336.esp32p4` |
| ESP32-S3-EYE | `sdkconfig.defaults` (with `set-target esp32s3`) |

IDF loads `sdkconfig.defaults.esp32p4` or `sdkconfig.defaults.esp32s3` after
`sdkconfig.defaults` on its own, so the chains do not name it.

```sh
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.p4_eye.esp32p4' set-target esp32p4 build
idf.py -p <PORT> flash
# only for a pre-registered node; assisted claiming needs no factory NVS.
# fctry is at 0x53A000 in partitions.16mb.csv and 0x35A000 in partitions.8mb.csv.
python -m esptool --chip esp32p4 -p <PORT> write_flash 0x53A000 fctry.bin

# ESP32-S3-EYE (8 MB, partitions.8mb.csv)
idf.py set-target esp32s3 build
idf.py -p <PORT> flash
```

Without an overlay the example builds for a generic 8 MB ESP32-P4
(`partitions.8mb.csv`, no in-system co-processor flashing) with no camera
sensor selected. Add a sensor overlay for your board.

## 3. Provisioning & viewing

1. First boot: BLE provisioning starts (RainMaker Home app pointed at the
   deployment via its client-outputs QR); with assisted claiming the app also
   supplies the node certificate.
2. After Wi-Fi + MQTT come up, the node reports the `Camera` device with a
   read-only `Channel` parameter (`rmng-v1-{node_id}`).
3. The WebRTC stack connects as master; open a viewer session from the app or
   any KVS WebRTC viewer using assumed-role credentials with `kvs` included.

The console (USB-Serial-JTAG on P4-EYE) runs the KVS `esp_cli` REPL with the
RainMaker Neo commands (`reset-network`, `reset-to-factory`, ...) registered into it.
