# Factory Auto-Registration

Generate node identities, register them with the RainMaker admin API, and emit factory NVS artifacts in one run.

## Prerequisites

[Set up the Python environment](../README.md#environment-setup).

## What it does

1. **Sign in** once per run (super admin) directly against the admin Cognito user pool, then fetch temporary AWS credentials for API Gateway.
2. **Create credentials** — either RainMaker-only client key/cert pairs or Matter DAC/PAI flow via `esp-matter-mfg-tool`.
3. **Register** the node certificate with `POST /v1/admin/nodes` (SigV4-signed).
4. **Write** `factory_nvs_input.json`, ESP-IDF factory `.bin` (when NVS tools are available), and for non-Matter mode POSIX NVS under `posix/nvs_persistent/`.

## Config

Defaults to `tools/common/credentials_store/general/rmng-outputs.json`. Override with `--config`, which accepts either a filesystem path or a client outputs URL.

The JSON is read like `register_node` / stack tooling:

| Source | Keys used |
|--------|-----------|
| `rmng-base` | `ApiGatewayUrl`, `StackRegion`, `IoTEndpointUrl`, `StackAccountId` |
| `esp-user-base` | `EspAdminUserPoolClientId` (admin Cognito sign-in) |

## Dependencies

**Python:** `boto3`, `requests`, `cryptography`, `shortuuid`

**Tools:** `esp-idf-nvs-partition-gen` (for ESP-IDF `.bin`; if missing, the script warns and skips IDF output)

**RainMaker-only (default):** Node keys and certs are generated with `cryptography` via `tools/common/util/node_crypto.py`.

**Matter (`--matter`):** `esp-matter-mfg-tool`, `ESP_MATTER_PATH`, and Matter SDK test credentials (same expectations as `factory_nvs_gen.get_matter_idf_credentials`). The DAC subject CN is the thing name.

## Usage

```text
factory_autoreg.py [-h] [--matter] [--vendor-id VENDOR_ID] [--product-id PRODUCT_ID]
                   [--codesign-cert CODESIGN_CERT] [--config CONFIG] [-n N]
                   [--output-dir OUTPUT_DIR] [--key-type {ec,rsa}]
                   [--part-label PART_LABEL] [--namespace NAMESPACE]
                   [--thing-groups THING_GROUP [THING_GROUP ...]]
                   [--tags TAG [TAG ...]]
                   [--capabilities CAP [CAP ...]]
                   username password
```

| Argument | Description |
|----------|-------------|
| `username`, `password` | Super admin user credentials |
| `--matter` | Matter path: merged chip/RainMaker factory NVS (ESP-IDF `.bin` only); POSIX skipped |
| `--vendor-id` | Matter vendor ID for mfg tool (default `0xFFF2`, hex ok) |
| `--product-id` | Matter product ID (default `0x8001`) |
| `--codesign-cert` | Optional codesign cert path for RainMaker factory namespace (Matter) |
| `--config` | Path or client outputs URL to `rmng-outputs.json` |
| `-n` / `--count` | Number of nodes in one batch |
| `--output-dir` | Output root (default: `outputs/` next to this script) |
| `--key-type` | `ec` or `rsa` — non-Matter only (default `ec`) |
| `--part-label` | Factory partition label (default `fctry`) |
| `--namespace` | Factory NVS namespace (default `rmaker_creds`) |
| `--thing-groups` | Passed as `admin_group_names` on registration |
| `--tags` | e.g. `env:prod` — passed as `tags` on registration |
| `--capabilities` | Space-separated capability list (accepted: `s3`, `kvs`, `bridge`) — passed as `capabilities` on registration; drives per-capability server-side policy attachment |

Example (RainMaker-only, one node):

```bash
python3 factory_autoreg.py --config /path/to/rmng-outputs.json <admin_user> <admin_password>
```

Example (config fetched from a client outputs URL):

```bash
python3 factory_autoreg.py --config https://example.s3.us-east-1.amazonaws.com/ap-south-1/rmng-client-outputs.json <admin_user> <admin_password>
```

Example (batch of 5 Matter devices):

```bash
export ESP_MATTER_PATH=/path/to/esp-matter
python3 factory_autoreg.py --matter -n 5 --output-dir ./out <admin_user> <admin_password> 
```

## Attach factory partition to build image

`factory_attach.py` merges generated factory partitions into a normal ESP-IDF merged flash binary using the build folder `flash_args`.

```text
factory_attach.py <build_dir> <offset> [artifacts_dir] [--truncated N] [--binaries-dir BINARIES_DIR]
```

- `build_dir`: ESP-IDF build directory containing `flash_args`
- `offset`: factory partition offset (hex or decimal, e.g. `0x3E0000` — the
  `fctry` offset in the in-tree examples' `partitions.csv`)
- `artifacts_dir`: optional, defaults to `tools/factory_autoreg/outputs`
- `--truncated N`: optional thing-name substring length for output naming (default: full thing name)
- `--binaries-dir BINARIES_DIR`: optional merged binary output directory (default: `tools/factory_autoreg/binaries`)

Interactive flow:

1. **Loop 1**: choose stack account ID, then thing type (numbered menu).
2. **Loop 2**: choose thing(s) from numbered menu:
   - `0` = all things in selected type
   - single index (e.g. `3`)
   - range/list (e.g. `1-4,6` or `(1-4,6)`)
3. Enter `q` / `quit` in loop 2 to return to loop 1; enter `q` / `quit` in loop 1 to exit.

Merged binaries are written as:

```text
<binaries_dir>/<target>-<project_name>-<thing_name[:truncated]>.bin
```

`target` and `project_name` are read from `<build_dir>/project_description.json`.

## Output layout

Artifacts go under:

```text
<output-dir>/<StackAccountId>/<thing_type>/<thing_key>/
```

- **`thing_type`:** `rmng-only` (default) or `matter` with `--matter`.
- **`thing_key`:** 22-character Base62 string for non-Matter; path-safe DAC CN for Matter.

Per-thing directory typically includes:

| File / dir | Non-Matter | Matter |
|------------|------------|--------|
| `client.key`, `client.crt` | Yes | — |
| `dac_key.pem`, `dac_cert.pem` | — | Yes |
| `qr_link.txt` | — | Yes (CHIP QR URL) |
| `factory_nvs_input.json` | Yes | Yes |
| `esp-idf/<part-label>.bin` | If NVS gen OK | Yes (merged factory) |
| `posix/nvs_persistent/*.bin` | If NVS gen OK | No |
| `registration.json` | Yes | Yes (`matter`, `qr_link` when applicable) |

When `-n` / `--count` is greater than 1, `batch_summary.json` is written under:

```text
<output-dir>/<StackAccountId>/<thing_type>/batch_summary.json
```

Each entry lists `thing_name`, `node_id`, `output_dir`, and for Matter `qr_link`.
