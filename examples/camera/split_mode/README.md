# ESP RainMaker Neo WebRTC Camera: split mode (ESP32-C6 or ESP32-C5 + ESP32-P4)

Two firmwares on the same board:

| Chip | Firmware | Role |
| --- | --- | --- |
| ESP32-C6 or ESP32-C5 | [`rmaker_split_camera`](rmaker_split_camera/) (this repo) | RainMaker Neo node, BLE provisioning, KVS signaling; esp_hosted Wi-Fi co-processor of the P4 |
| ESP32-P4 | `streaming_only` from `${KVS_SDK_PATH}/examples/streaming_only` | Media capture, H.264 encode, RTP/SRTP straight to the viewer |

The co-processor keeps the node and the signaling channel online; signaling for
each viewer session is relayed to the P4 over `webrtc_bridge`, and media never
round-trips through the co-processor. Because the P4 is not needed for the cloud
connection, it can sleep between sessions on boards that wire a host wake-up
pin (the P4-EYE / P4X-EYE do not, so their overlays keep the P4 awake). The C5
build carries host deep sleep with co-processor light sleep, for the
ESP32-P4 + ESP32-C5 core board.

Both halves use esp_hosted 3.1.0, and their Wi-Fi init configs match. If they
differ, the co-processor re-initialises Wi-Fi each time the P4 boots or wakes.

See the [camera overview](../README.md) for prerequisites (IDF, `KVS_SDK_PATH`,
node registration, credential endpoint).

## 1. Build the co-processor image (`rmaker_split_camera`)

```sh
cd rmaker_split_camera
# C6 on an ESP32-P4-EYE / P4X-EYE: add the board's SDIO slave pinout
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.p4_eye_pair.esp32c6' set-target esp32c6 build
# C5 on the ESP32-P4 + ESP32-C5 core board
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.p4_c5_core_board.esp32c5' set-target esp32c5 build
```

IDF adds `sdkconfig.defaults.esp32c6` or `sdkconfig.defaults.esp32c5` for the
target. The partition table sits at `0xC000` and the first app slot at
`0x20000`, the offsets the `streaming_only` overlays flash the co-processor at.

## 2. Build & flash the P4 (`streaming_only`), carrying the C6 image

The P4-EYE's C6 has no USB port: `streaming_only` packs the C6 images into its
`slave` partition and flashes them in-system at boot.

```sh
DEST=${KVS_SDK_PATH}/examples/streaming_only/target-firmware
cp rmaker_split_camera/build/bootloader/bootloader.bin           "$DEST/bootloader.bin"
cp rmaker_split_camera/build/partition_table/partition-table.bin "$DEST/partition-table.bin"
cp rmaker_split_camera/build/rmaker_split_camera.bin             "$DEST/app.bin"

cd ${KVS_SDK_PATH}/examples/streaming_only
# ESP32-P4-EYE (pre-v3 P4); use sdkconfig.defaults.p4x_eye.esp32p4 for the P4X-EYE
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.p4_eye.esp32p4' set-target esp32p4 build
idf.py -p <P4_PORT> flash monitor
```

On the ESP32-P4 + ESP32-C5 core board (P4 below rev 3), build `streaming_only`
with `sdkconfig.defaults.p4_c5_core_board.esp32p4` instead, and flash the C5
image over the C5's own USB port.

The P4 + C5 core board setup is for development only. The C5 image has a
single factory slot and no OTA, and the P4 image does not carry it, so a C5 in
the field has no update path.

## 3. Node credentials

With assisted claiming (default) the phone app supplies the node certificate
during BLE provisioning, so nothing needs flashing. The example claims as a
camera (`CONFIG_ESP_RMAKER_NEO_CLAIM_VIDEOSTREAM_SUPPORT`): it requests the `kvs`
capability, and learns the credential-provider endpoint, video role alias and
region from the cloud (`getServerConfig`). For a pre-registered node
the factory NVS lives on the co-processor (`fctry` partition at `0x3C0000`);
with no C6 USB port on the P4-EYE, write it through an ESP-Prog on the C6's
programming header.

## 4. Provisioning & viewing

Same as standalone: BLE provisioning (served by the co-processor), then the node
reports the `Camera` device with its `Channel` parameter and the co-processor
connects to KVS as master. When a viewer's offer arrives, the co-processor hands
the session to the P4, which streams directly.
