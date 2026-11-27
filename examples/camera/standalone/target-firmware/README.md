# Co-processor firmware (standalone mode)

On the ESP32-P4-EYE / P4X-EYE the ESP32-C6 co-processor sits on the same PCB and
has no USB port of its own. The `p4_eye` overlay enables
`CONFIG_SLAVE_FLASHER_ENABLE` with `CONFIG_SLAVE_FLASHER_IMAGE_EMBEDDED`: the P4
app embeds the C6 firmware from this directory, and `flash_slave()` writes it to
the C6 over UART at boot when it differs from what the C6 has. Each app OTA
brings its own C6 image.

The build **fails fast** unless these files are present here (they are not
checked in):

| File                  | What it is                  |
|-----------------------|-----------------------------|
| `bootloader.bin`      | C6 second-stage bootloader  |
| `partition-table.bin` | C6 partition table          |
| `app.bin`             | C6 application image        |

In standalone mode the C6 runs the KVS SDK's `network_adapter` example (Wi-Fi +
BLE HCI over SDIO):

```bash
cd ${KVS_SDK_PATH}/examples/network_adapter
# p4_eye_pair sets the C6's SDIO pins for the P4-EYE PCB.
idf.py -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c6;sdkconfig.defaults.p4_eye_pair.esp32c6' set-target esp32c6
idf.py build

DEST=<this directory>
cp build/bootloader/bootloader.bin           "$DEST/bootloader.bin"
cp build/partition_table/partition-table.bin "$DEST/partition-table.bin"
cp build/network_adapter.bin                 "$DEST/app.bin"
```

`network_adapter` puts its partition table at `0xC000` and its first app at
`0x20000`, which is what the overlays' `CONFIG_SLAVE_PARTITION_TABLE_ADDRESS` /
`CONFIG_SLAVE_APPLICATION_ADDRESS` expect.
