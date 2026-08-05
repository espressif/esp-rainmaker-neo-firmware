# OTA Helper

CLI for AWS IoT OTA: setup infrastructure, create/start/cancel/delete jobs, and cleanup. Uses the OTA manager in `tools/common/util/ota_aws.py`.

## Commands

| Command | Description |
|--------|-------------|
| `--setup` | One-time: certs (ACM), Code Signer profile, S3 bucket, IAM role |
| `--create-ota-job` | Create standard FreeRTOS OTA job (`create-ota-update` API). Requires `--files`, and `--thing-name` or `--thing-group-name`. |
| `--create-custom-job` | Create custom OTA job (`create-job` API). Optional `--job-config`. |
| `--create-rmng-ota-job` | Create RMNG OTA job. Requires `--files` and `--job-config`. |
| `--start-job` | Start job and monitor. Requires `--job-id`. |
| `--track-job-execution` | Poll and print job execution status for a thing. Requires `--job-id` and `--thing-name`. |
| `--cancel-custom-job` | Cancel custom job. Requires `--job-id`. Optional `--force`. |
| `--cancel-ota-job` | Cancel OTA update. Requires `--job-id`. Optional `--force`. |
| `--delete-job` | Delete job and associated S3 files. Requires `--job-id`. |
| `--destroy` | Tear down OTA infra (cancels running jobs, S3, certs, IAM, streams). |

## Options

- **Setup:** `--cert-type {RSA,ECDSA}` (default ECDSA), `--platform-id` (default AmazonFreeRTOS-Default).
- **Job creation:** `--files <json>`, `--job-config <json>`, `--thing-name`, `--thing-group-name`.
- **Job operations:** `--job-id`, `--force` (for cancel).

## Usage

```bash
# From tools/ota_helper (or set PYTHONPATH so common/util is reachable)
python ota_helper.py --setup
python ota_helper.py --setup --cert-type RSA --platform-id AmazonFreeRTOS-Default

python ota_helper.py --create-ota-job --files templates/ota_file_template.json --thing-name node_switch
python ota_helper.py --create-custom-job --files templates/ota_file_template.json --job-config templates/job_config_template.json --thing-name node_switch
python ota_helper.py --create-rmng-ota-job --files templates/ota_file_template.json --job-config templates/job_config_template.json --thing-name node_switch

python ota_helper.py --start-job --job-id ota-update-12345
python ota_helper.py --track-job-execution --job-id ota-update-12345 --thing-name node_switch

python ota_helper.py --cancel-custom-job --job-id AFR_OTA-custom-12345 [--force]
python ota_helper.py --cancel-ota-job --job-id ota-update-12345 [--force]
python ota_helper.py --delete-job --job-id ota-update-12345

python ota_helper.py --destroy
```

## Config

- **`rmng-outputs.json`** – the deployment's stack outputs (region, endpoints, account),
  placed in the [credentials store](../common/credentials_store/README.md) at
  `tools/common/credentials_store/general/rmng-outputs.json`. The tool fails at import
  without it.
- AWS credentials with permissions for IoT Core (jobs, OTA), S3, ACM, Code Signer, IAM, IoT streams.

## File list JSON (`--files`)

Array of file entries, e.g.:

```json
[
  { "name": "firmware.bin", "path": "path/to/firmware.bin", "signing": 0, "file_id": 0, "hash_algorithm": "SHA256" },
  { "name": "config.json", "path": "path/to/config.json", "signing": 0, "file_id": 1, "hash_algorithm": "SHA256" }
]
```

For single-file (firmware) RMNG OTA jobs, the image's MD5 is computed automatically and added to the
job document. This enables device-side [auto-resume](../../docs/en/specs/ota/job_document.md#auto-resume)
of interrupted downloads plus an end-to-end integrity check — no extra configuration needed.

## Dependencies

Python: boto3, awscrt, awsiot, cryptography, requests. See `tools/requirements.txt`.
