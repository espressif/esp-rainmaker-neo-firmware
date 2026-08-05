# ESP RainMaker Neo OTA Job Document Format

This document describes the expected format for ESP RainMaker Neo OTA job documents used in AWS IoT Jobs.

## Overview

ESP RainMaker Neo OTA job documents extend the standard AWS IoT FreeRTOS OTA (AFR-OTA) job document format with additional ESP RainMaker Neo-specific fields. The job document maintains backward compatibility with AFR-OTA while adding ESP RainMaker Neo-specific configuration options.

## Job Document Structure

A complete ESP RainMaker Neo OTA job document contains two main sections:

1.  **``afr_ota``** - Required for AFR-OTA compatibility
2.  **``rmng_ota``** - ESP RainMaker Neo-specific configuration and metadata

### AFR-OTA Section (`afr_ota`)

This section maintains compatibility with the AWS IoT FreeRTOS OTA agent and follows the standard AFR-OTA job document format:

```javascript
{
  "afr_ota": {
    "protocols": ["MQTT"],
    "streamname": "string",
    "files": [
      {
        "filepath": "string",
        "filesize": number,
        "fileid": "string",
        "certfile": "string",
        "sig-sha256-ecdsa": "string"
      }
    ]
  }
}
```

#### AFR-OTA Fields

```{list-table}
:header-rows: 1
:widths: auto

* - Field
  - Type
  - Required
  - Description

* - ``protocols``
  - Array of strings
  - Yes
  - Supported protocols (``["MQTT"]``). ``"MQTT"`` must be the **first** entry: the
    device reads ``protocols[0]`` only, and any other value routes the document down
    the (unsupported) HTTP branch instead of reading ``streamname``. See
    [Protocol ordering (`protocols[0]`)](#protocol-ordering-protocols0).

* - ``streamname``
  - String
  - Yes (if MQTT)
  - Name of the AWS IoT stream containing the firmware files (used for MQTT). **Must be under the** [Streamname length limit](#streamname-length-limit-mqtt)**)**.

* - ``files``
  - Array of objects
  - Yes
  - Array of file objects to be downloaded
```

#### File Object Fields

```{list-table}
:header-rows: 1
:widths: auto

* - Field
  - Type
  - Required
  - Description

* - ``filepath``
  - String
  - Yes
  - Path where the file should be stored on the device

* - ``filesize``
  - Number
  - Yes
  - Size of the file in bytes

* - ``fileid``
  - String
  - Yes
  - Unique identifier for the file

* - ``certfile``
  - String
  - No
  - Path to certificate file for signature verification

* - ``sig-*``
  - String
  - Only when signature verification is enabled
  - Base64 signature for the file (algorithm-specific field name). Required, and
    must be valid base64, when ``CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE`` is set
    (default **off**); ignored otherwise.
```

### ESP RainMaker Neo OTA Section (`rmng_ota`)

This section contains ESP RainMaker Neo-specific configuration and metadata:

```javascript
{
  "rmng_ota": {
    "filetype": "string",
    "fw_version": "string",
    "min_fw_version": "string",
    "file_md5": "string",
    "metadata": {
      "key": "value"
    },
    "download_window": {
      "validity": {
        "start": number,
        "end": number
      },
      "daily": {
        "start": number,
        "end": number
      }
    }
  }
}
```

#### ESP RainMaker Neo OTA Fields

```{list-table}
:header-rows: 1
:widths: auto

* - Field
  - Type
  - Required
  - Description

* - ``filetype``
  - String
  - No
  - [Custom filetype](custom_filetypes.md)

* - ``fw_version``
  - String
  - Yes for the default filetype handler; otherwise no, unless ``min_fw_version`` is specified
  - Target firmware version to be installed. Must match the firmware version embedded in the downloaded image exactly — the device verifies this after download (before reboot) to prevent downgrade-by-lying attacks. Custom filetype handlers that set ``get_version = NULL`` may omit this; in that case the handler is responsible for its own image header verification.

* - ``min_fw_version``
  - String
  - No
  - Minimum firmware version required for update

* - ``file_md5``
  - String
  - No
  - Hex MD5 (case-insensitive) of the **entire** OTA image. When present, enables [auto-resume](#auto-resume) of an interrupted download and an end-to-end MD5 integrity check after download (before reboot). Omit to disable both.

* - ``metadata``
  - Object
  - No
  - Additional metadata for the OTA update

* - ``download_window``
  - Object
  - No
  - Time window restrictions for the update. Parsed only when
    ``CONFIG_RMNG_OTA_TIME_SUPPORT`` is set (default on); otherwise ignored
    entirely.
```

#### Download Window Object

Every field is optional and independently defaulted — a partial `download_window` is valid, and any field left out simply imposes no restriction.

```{list-table}
:header-rows: 1
:widths: auto

* - Field
  - Type
  - Required
  - Description

* - ``validity``
  - Object
  - No
  - Overall validity period for the update

* - ``validity.start``
  - Number
  - No
  - Start timestamp (Unix epoch time). Defaults to ``0`` (no lower bound)

* - ``validity.end``
  - Number
  - No
  - End timestamp (Unix epoch time). Defaults to ``0`` (no upper bound)

* - ``daily``
  - Object
  - No
  - Daily time restrictions within the validity period

* - ``daily.start``
  - Number
  - No
  - Daily start time in minutes since midnight. Defaults to ``-1`` (no daily
    restriction)

* - ``daily.end``
  - Number
  - No
  - Daily end time in minutes since midnight. Defaults to ``-1`` (no daily
    restriction)
```

## Complete Example

```json
{
  "afr_ota": {
    "protocols": ["MQTT"],
    "streamname": "AFR_OTA-stream-1234567890",
    "files": [
      {
        "filepath": "/firmware.bin",
        "filesize": 1048576,
        "fileid": "firmware_v1.2.3",
        "certfile": "/ota-test-cert",
        "sig-sha256-ecdsa": "MEQCIG...signature..."
      }
    ]
  },
  "rmng_ota": {
    "fw_version": "1.2.3",
    "min_fw_version": "1.0.0",
    "file_md5": "d41d8cd98f00b204e9800998ecf8427e",
    "metadata": {
      "release_notes": "Bug fixes and performance improvements",
      "priority": "high"
    },
    "download_window": {
      "validity": {
        "start": 1704067200,
        "end": 1706745600
      },
      "daily": {
        "start": 1200,
        "end": 1440
      }
    }
  }
}
```

## Implementation Notes

- The `afr_ota` section is required for compatibility with existing AFR-OTA implementations
- The `rmng_ota` section is required and are fields specific to ESP RainMaker Neo. A job document without it is rejected with reason `"Missing RMNG-specific fields in job document"` (or handed to the custom job-document callback, see [below](#custom-non-ota-job-documents))
- `min_fw_version` requires `fw_version`: a job that declares a minimum without a target version is rejected
- Download windows restrict when the device can perform the OTA update:
  - `validity` defines the overall time period when the update is allowed
  - `daily` defines the allowed hours within each day (in minutes since midnight)
- All timestamps are in Unix epoch time (seconds since 1970-01-01 00:00:00 UTC)
- The device requires `"MQTT"` as the first `protocols` entry and downloads from `streamname`

### Protocol ordering (`protocols[0]`)

The device parses the job document with the AWS Jobs OTA parser, which inspects **only `afr_ota.protocols[0]`** — it does not search the array for a protocol the device supports:

- `protocols[0] == "MQTT"` → `streamname` is read and the firmware is downloaded over MQTT file-streams.
- Anything else → the parser takes the HTTP branch, which requires `fileType`, `auth_scheme` and `update_data_url` fields that ESP RainMaker Neo job documents never carry. Parsing fails and the job is REJECTED with reason `"Missing AFR-OTA-specific fields in job document"`.

So `["MQTT"]` and `["MQTT", "HTTP"]` both work, while `["HTTP", "MQTT"]` is rejected even though MQTT appears in the array. Job documents generated by the tooling in `tools/` always emit `["MQTT"]`.

### Streamname length limit (MQTT)

When using the MQTT transport, the `streamname` field is [constrained by the vendored AWS MQTT file-streams library](https://github.com/aws/aws-iot-core-mqtt-file-streams-embedded-c/blob/383ffeff43a80123e07cf8ca613d12ce680527b0/source/include/MQTTFileDownloader.h#L39). Streamnames longer than this limit are **rejected** at job-document parse with reason `"Image reference is invalid"`, before any download is attempted.

AWS Jobs wraps the user-supplied `jobId` into the streamname as `AFR_OTA-<jobId>` (8-char prefix). Practically, this means **``jobId`` length must stay ≤ the limit, minus 8 characters** when generating OTA jobs.

A device that receives a job with an oversize streamname reports the job as REJECTED and resumes normal operation; the job is not re-fetched after reboot.

### Image Header Verification

After download, before reboot, the device verifies that the binary's embedded application descriptor matches both the running app's project name and the job-declared `fw_version`:

- **Project name**: the downloaded image's embedded project name must match the running app's project name exactly. Prevents cross-project flashing (e.g. a `switch` build landing on a `light` device).
- **Firmware version**: the downloaded image's embedded version must match the job's `fw_version` field byte-for-byte. Prevents downgrade-by-lying (job claims `2.0.0`, binary is actually `1.0.0`).

Both checks run before the MD5 integrity check and before signature verification, so a mismatched image is rejected even if its signature happens to be valid for some other (downgraded or cross-project) build. A mismatch fails the job with no reboot, reported as `"Image header invalid"`.

Filetype handlers that do not track versions (`get_version = NULL` on the filetype handler context) may omit `fw_version`; in that case the handler must implement its own `verify_image_header` callback (or accept the resulting downgrade exposure by setting it to `NULL`, which skips verification entirely — strongly discouraged).

### Auto-resume

When the job document declares `file_md5` (and `CONFIG_RMNG_OTA_RESUME` is enabled, default `y`), a download interrupted by reboot/power-loss/connectivity issues resumes from where it left off instead of restarting from byte 0. The partially-written bytes already sit in the inactive app partition; the device only needs to know they belong to *this* image and how much it already has.

- **Image identity.** `file_md5` is the identity token. The device persists a small descriptor (md5, file size, and MQTT block size) to NVS alongside the progress tracker. On a re-fetched in-progress job it resumes **only** when the descriptor matches the current job exactly. A different image (different md5/size) or a changed MQTT block size forces a fresh full download.
- **Progress tracker.** MQTT persists a received-block bitmap (updated on each batch boundary to bound NVS wear).
- **Integrity.** After the download completes, the full-image MD5 is recomputed over the written partition and compared against `file_md5`. A mismatch fails the job (see [status_details](status_details.md)). This is the safety net for resumed bytes, which bypass any single continuous hash, and runs independently of signature verification.
- **Best-effort.** Resume never makes a job fail that a normal OTA would have completed: any failure in the resume path (older ESP-IDF without `esp_ota_resume`, NVS read error, tracker mismatch, etc.) transparently falls back to a fresh full download.

Custom (non-firmware) filetype handlers manage their own resume — see [custom_filetypes](custom_filetypes.md).

## Custom non-OTA Job Documents

### Format

A custom non-OTA job document is defined as a job document **missing any required keys to be a valid ESP RainMaker Neo OTA document**:

- If there are *minimal required keys*, but some keys have *invalid values*, this is treated as a **malformed ESP RainMaker Neo OTA document** and will be **rejected** instead.
- To truly ensure a non-OTA job document, avoid using the `afr_ota` and `rmng_ota` keys at all.

### Handlers

If custom job document handling is enabled (`CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT=y`), then:

- The user provides a **custom job document callback** in the OTA configuration.
- The user uses the **custom job status reporting API** to report job statuses.
