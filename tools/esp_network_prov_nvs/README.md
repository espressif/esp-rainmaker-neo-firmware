# Network Provisioning NVS Generation

Generate network provisioning NVS data and flash it directly to an ESP chip.

## Required arguments

- `--port`: serial port (example: `/dev/cu.usbserial-0001`)
- `--offset`: partition offset (hex or decimal, example: `0x9000`)
- `--size`: partition size in bytes (hex or decimal, example: `0x6000`)

## Usage

```bash
python esp_network_prov_nvs.py --port /dev/cu.usbserial-0001 --offset 0x9000 --size 0x6000
```

The tool pulls credentials from `.env` via `tools/common/util/esp_helpers.py`:

- Wi-Fi: `ESP_WIFI_SSID` and `ESP_WIFI_PASSWORD`
- Thread: `ESP_THREAD_ACTIVE_DATASET`

In `auto` mode (default), Wi-Fi is preferred when both Wi-Fi and Thread credentials are present.

## Optional overrides

```bash
# Force Wi-Fi and override credentials
python esp_network_prov_nvs.py \
  --port /dev/cu.usbserial-0001 \
  --offset 0x9000 \
  --size 0x6000 \
  --network-type wifi \
  --wifi-ssid "MySSID" \
  --wifi-password "MyPassword"
```

```bash
# Force Thread
python esp_network_prov_nvs.py \
  --port /dev/cu.usbserial-0001 \
  --offset 0x9000 \
  --size 0x6000 \
  --network-type thread \
  --thread-active-dataset "<hex_dataset>"
```
