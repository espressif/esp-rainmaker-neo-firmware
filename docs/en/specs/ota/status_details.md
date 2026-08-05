# ESP RainMaker Neo OTA Status Details JSON Formats

This document describes the JSON formats used for status details in ESP RainMaker Neo OTA job executions. These formats are used when devices report progress and status information back to AWS IoT Jobs.

## Overview

Status details provide additional context about the current state of an OTA update. The ESP RainMaker Neo SDK converts internal status structures to JSON format before reporting to AWS IoT. The JSON format varies depending on the status type.

Four status types produce status details: `IN_PROGRESS`, `SUCCEEDED`, `FAILED` and `REJECTED`. The SDK also tracks two internal types, `STARTING` and `DELAYED`, that carry no JSON — a job update for those reports the AWS status with **no** `statusDetails`.

## Status Types and JSON Formats

### IN_PROGRESS Status

Reports download progress during firmware transfer. The progress percentage can be derived by `(downloaded_bytes / total_bytes * 100)%`.

**JSON Format:**

```javascript
{
  "downloaded_bytes": <bytes already downloaded by firmware>,
  "total_bytes": <total byte size of firmware>
}
```

**Fields:**

- `downloaded_bytes` (number): Number of bytes downloaded so far
- `total_bytes` (number): Total size of the firmware file in bytes

**Example:**

```json
{
  "downloaded_bytes": 262144,
  "total_bytes": 1048576
}
```

### SUCCEEDED Status

Reports successful completion of the OTA update.

**JSON Format:**

```javascript
{
  "fw_version": <firmware version of successful update>
}
```

**Fields:**

- `fw_version` (string): The firmware version that was successfully installed

**Example:**

```json
{
  "fw_version": "2.1.0"
}
```

### FAILED Status

Reports failure of the OTA update with reason.

**JSON Format:**

```javascript
{
  "reason": <reason for failure>
}
```

**Fields:**

- `reason` (string): Description of why the update failed

The complete set of SDK-generated failure reasons:

```{list-table}
:header-rows: 1
:widths: auto

* - Reason
  - Meaning

* - ``"Image downloader setup failed"``
  - The transport-specific downloader could not be initialized

* - ``"MQTT stream subscription failed"``
  - Could not subscribe to the AWS IoT stream topics (MQTT transport)

* - ``"Image header invalid"``
  - The binary's embedded project name / firmware version did not match

* - ``"Image MD5 mismatch"``
  - The downloaded image's MD5 did not match the ``file_md5`` declared in the job
    document (see [Auto-resume](job_document.md#auto-resume)). Reported
    for both fresh and resumed downloads when ``file_md5`` is present

* - ``"Image signature invalid"``
  - Signature verification failed (only reachable when
    ``CONFIG_RMNG_OTA_SIGNATURE_VERIFY_ENABLE`` is set)

* - ``"Post download checks failed"``
  - A post-download check other than the ones above failed, e.g. the filetype
    handler's integration check

* - ``"Reboot check failed"``
  - The post-reboot validation of the new image failed

* - ``"Timed out waiting for final status"``
  - The filetype handler did not call ``esp_rmaker_ota_report_final_status()``
    within the 10-second window

* - ``"Custom filetype handler does not have a post reboot handler even though it
    was instructed to reboot post-download"``
  - The handler requested a reboot but provides no ``on_post_reboot``, so the job
    would otherwise never terminate

* - ``"Failed to process custom job document"``
  - The custom job-document callback reported failure (requires
    ``CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT``)

* - ``"Unknown error"``
  - Fallback when no more specific reason is available
```

Applications may also report their own reason strings via `esp_rmaker_ota_report_final_status()`.

**Example:**

```json
{
  "reason": "Image signature invalid"
}
```

### REJECTED Status

Reports rejection of the OTA update with reason.

**JSON Format:**

```javascript
{
  "reason": <reason for rejection>
}
```

**Fields:**

- `reason` (string): Description of why the update was rejected

**Rejection Reasons:**

```{list-table}
:header-rows: 1
:widths: auto

* - Reason
  - Meaning

* - ``"Firmware version too low"``
  - The running firmware is below the job's ``min_fw_version``

* - ``"Unsupported firmware version"``
  - The job's ``fw_version`` could not be converted to a comparable version number

* - ``"Firmware version is required for filetype"``
  - The selected filetype handler tracks versions, but the job did not declare
    ``fw_version``

* - ``"Missing RMNG-specific fields in job document"``
  - No ``rmng_ota`` section (see [ESP RainMaker Neo OTA Job Document Format](job_document.md))

* - ``"Missing AFR-OTA-specific fields in job document"``
  - The ``afr_ota`` section is missing or incomplete

* - ``"Missing signature in job document"``
  - Signature verification is enabled but no ``sig-*`` field was supplied

* - ``"Invalid base64 signature in job document"``
  - The ``sig-*`` value is not valid base64

* - ``"Image reference is invalid"``
  - The ``streamname`` (MQTT) parsed from the job document was rejected by the
    transport validator. The most common cause is a streamname that is too long; see
    [Streamname length limit](job_document.md#streamname-length-limit-mqtt)

* - ``"Filetype too long"``
  - The ``filetype`` string exceeds the internal limit

* - ``"No custom filetype implementation"``
  - The job declared a ``filetype`` but no lookup function was registered

* - ``"Filetype not supported"``
  - The lookup function returned no handler for that ``filetype``

* - ``"Filetype handler is invalid"``
  - The handler context is missing a required callback, or implements only one of
    the ``get_version``/``version_to_uint32`` pair

* - ``"Invalid custom job document"``
  - The custom job-document callback rejected the document (requires
    ``CONFIG_RMNG_OTA_CUSTOM_JOB_SUPPORT``)
```

**Example:**

```json
{
  "reason": "Firmware version too low"
}
```

## Implementation Details

### AWS IoT Job Execution Status Details

When reported to AWS IoT, these JSON strings are included in the job execution's `statusDetails.detailsMap` field. This information can be retrieved using the AWS IoT Jobs API `DescribeJobExecution` operation.

### API Details

**API Operation:** `DescribeJobExecution`

**Parameters:**

- `jobId` (string): The unique identifier you assigned to this job when it was created
- `thingName` (string): The name of the thing, or \* if the job was not created with a thing name

**Response Structure:**

```javascript
{
  "execution": {
    "jobId": "string",
    "thingName": "string",
    "status": "IN_PROGRESS | SUCCEEDED | FAILED | REJECTED | CANCELED | TIMED_OUT | REMOVED",
    "statusDetails": {
      // <status details are as per JSON formats described above>
      "detailsMap": <status details>
    },
    "queuedAt": "timestamp",
    "startedAt": "timestamp",
    "lastUpdatedAt": "timestamp",
    "executionNumber": number
  }
}
```

**Example API Call (AWS CLI):**

```bash
aws iot describe-job-execution --job-id "AFR_OTA-custom-1704067200-abc123" --thing-name "my-device"
```

**Example API Call (Python boto3):**

```python
import boto3

iot_client = boto3.client('iot')
response = iot_client.describe_job_execution(
    jobId='AFR_OTA-custom-1704067200-abc123',
    thingName='my-device'
)

execution = response.get('execution', {})
status_details = execution.get('statusDetails', {}).get('detailsMap', {})
```

**Note:** AWS IoT stores all values in the `detailsMap` as strings, regardless of their original JSON type.
