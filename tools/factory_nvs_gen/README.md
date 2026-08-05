# Factory NVS Generation

This generates the NVS factory partition containing local configurations
(MQTT host, client certificates, node identity, and related fields). This data
must be provisioned before the node can talk to your backend.

## Usage

**Default (RainMaker factory only):**

```
python factory_nvs_gen.py <factory_partition_label> <factory_namespace> <json_input_file>
```

**Matter + RainMaker (single ESP-IDF factory partition):**

```
python factory_nvs_gen.py --matter [--vendor-id <hex>] [--product-id <hex>] <factory_partition_label> <factory_namespace> <json_input_file>
```

- `factory_partition_label`: Partition label to use
    - Kconfig option: `CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME`
- `factory_namespace`: Namespace within the partition to store factory credentials
    - Kconfig option: `CONFIG_ESP_RMAKER_FACTORY_NAMESPACE`
- `json_input_file`: path to [JSON input](#input) (requirements differ for `--matter`; see below).

### `--matter` (Matter factory merged with RainMaker)

Use this when the device needs both **chip factory data** (DAC, Matter commissioning material in NVS) and **RainMaker** credentials in the same factory image.

**Dependencies**

- `esp-matter-mfg-tool` (`python3 -m pip install esp-matter-mfg-tool`)
- `esp-idf-nvs-partition-gen` (same as the default flow, for building the final `.bin`)
- Environment:
    - **`ESP_MATTER_PATH`** must point at your ESP-Matter tree.
    - **`MATTER_SDK_PATH`** (optional): path to the connectedhomeip checkout. If unset, the tool uses `ESP_MATTER_PATH/connectedhomeip/connectedhomeip`.
- Test **PAI key/cert** and **certification declaration** for your `--vendor-id` / `--product-id` must exist under `<Matter SDK>/credentials/test/` (same layout as upstream Matter test attestation assets).

**How it works**

1. **`esp-matter-mfg-tool`** is run once (fixed manufacturing strings such as product name match the matter-sim style example in code). It emits chip-factory NVS rows (`partition.csv` under the per-device output) and DAC material.
2. **RainMaker rows** are built from your JSON (see the **Input for `--matter`** subsection under [Input](#input)): at minimum `mqtt_host`, plus auto-generated `random`. The DAC private key and certificate from the manufacturing step are written into the RainMaker namespace as **`client_key`** / **`client_cert`**, and **`node_id`** is set to the **DAC certificate subject common name** (so the AWS Thing name aligns with the device identity).
3. The tool **concatenates** the Matter chip-factory CSV rows with the RainMaker namespace rows and runs **`esp-idf-nvs-partition-gen`** to produce **one** ESP-IDF factory binary.
4. **POSIX** factory output is **not** generated in this mode.
5. Under `out/`, the run directory is renamed to `<json_basename>_<thing_name>/` (DAC CN). Next to `esp-idf/` you also get **`dac_key.pem`**, **`dac_cert.pem`**, and **`qr_link.txt`** (browser URL for the commissioning QR payload).

**Vendor / product IDs**

- `--vendor-id` and `--product-id` are passed through to the manufacturing tool (defaults `0xFFF2` / `0x8001`, same as the script). They must match PAI/CD files present under the Matter SDK test credentials tree.

## Input

The only input is a JSON file with the following keys. An example is available at `example_input.json`.

> These key values should follow [this header file](../../components/esp_rmaker_neo/priv_include/constants/nvs.h).
### Required
- (str) `"client_key"`: path to the client key,
- (str) `"client_cert"`: path to the client certificate,
- (str) `"mqtt_host"`: MQTT broker hostname,
- (str) `"node_id"`: Client identifier (AWS Thing Name)

### Auto-generated
- (bytes) `"random"`: 16-byte random value (auto-generated, do not specify in input)

### Optional
- (str) `"client_user"`: MQTT username (omit this key if not using)
- (str) `"client_pass"`: MQTT password (omit this key if not using)

### Input for `--matter`

Key names still follow [factory_part.h](../../components/esp_rmaker_neo_common/priv_include/credentials/factory_part.h) (same as the default flow). For `--matter`, **`client_key`**, **`client_cert`**, and **`node_id`** must **not** be supplied in JSON: they are taken from the manufacturing tool (DAC and DAC subject CN).

**Required**

- (str) `"mqtt_host"`: MQTT broker hostname

**Auto-generated**

- (bytes) `"random"`: 16-byte random value (auto-generated)

**Optional**

- Same optional MQTT username/password and codesign cert as in the default flow (`codesign_cert` path if you want OTA signature verification)

## Output

This generates the following (the subdirectory is named after your JSON file, without extension):
```
out/
└── <json_basename>/
    ├── esp-idf/        // for ESP-IDF
    │   └── *.bin
    └── posix/          // for POSIX
        └── nvs_persistent/*.bin
```

If your input is `node.json`, outputs will be under `out/node/`.

With **`--matter`**, the directory is **`out/<json_basename>_<thing_name>/`** (Thing name = DAC common name). There is **no** `posix/` tree. Extra artifacts at the top level of that directory: `dac_key.pem`, `dac_cert.pem`, `qr_link.txt`.

### ESP-IDF

Define an NVS partition for factory data in a
[custom partition table](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/partition-tables.html#creating-custom-tables).
Example from [`examples/light/partitions.csv`](../../examples/light/partitions.csv):

```csv
fctry,    data, nvs,     0x3E0000,  0x6000,
```

- The **name** (`fctry` here) must match the partition label passed to this
  tool and what firmware expects via `CONFIG_ESP_RMAKER_FACTORY_PARTITION_NAME`
  (see [this header file](../../components/esp_rmaker_neo/priv_include/constants/nvs.h)).
- Partition **type**: `data`, **subtype**: `nvs`.
- **Size** must be at least as large as the generated `.bin`. The examples use
  24 KB (`0x6000`): pre-flashed credentials fit in the three-sector NVS minimum
  of 12 KB (`0x3000`), but assisted claiming writes the private key,
  certificate, node ID and random bytes at **runtime**, which needs the extra
  headroom. Keep 24 KB if claiming is enabled — it is on by default when BLE
  is enabled.

Flash the output binary with `esptool` at the offset of the factory NVS
partition from your `partitions.csv` (`0x3E0000` above):

```
esptool.py [--chip <chip type>] [--port <chip port>] write_flash <partition offset> <path to output binary file>
```

#### Merged full-flash image

To produce one binary that contains everything `idf.py flash` would write
(bootloader, partition table, apps, ...) plus the per-device factory NVS blob —
flashable in a single operation at offset `0x0`:

1. `cd` into the ESP-IDF build directory for the project (the directory that
   contains `flash_args` after `idf.py build`).
2. Run `esptool.py merge_bin` with `@flash_args` (so all default images and
   offsets from the build are included), then the factory NVS partition offset
   in hex (same value as in `partitions.csv`) and that device's factory `.bin`:

```
cd /path/to/your/project/build
esptool.py --chip <target> merge_bin -o <merged_binary_output_path> @flash_args <fctry_offset_hex> <path-to-factory.bin>
```

The result is a full flash image; flash it at offset `0x0` (for example with
`esptool.py write_flash 0x0 <merged_binary_output_path>` or your production
tool).

### POSIX

Copy the `nvs_persistent` folder into the same directory where your POSIX application is.

## Security

Do not commit real private keys, factory binaries, or registration outputs from
production runs. Use test credentials in CI and secure storage for
manufacturing outputs.
