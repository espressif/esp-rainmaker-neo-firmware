# OTA

The ESP RainMaker Neo Over-The-Air (OTA) update system: the job documents it accepts, the status details it reports, and how custom file types extend it.

- [ESP RainMaker Neo OTA Job Document Format](job_document.md) -- the expected format for OTA job documents used in AWS IoT Jobs: AFR-OTA compatibility fields, ESP RainMaker Neo configuration options, complete examples with all supported fields, and implementation notes.
- [ESP RainMaker Neo OTA Status Details JSON Formats](status_details.md) -- the JSON formats used for status details in OTA job executions: formats per status type (`IN_PROGRESS`, `SUCCEEDED`, `FAILED`, `REJECTED`), the AWS IoT job execution status details structure, code examples, and error handling and memory management.
- [Custom OTA Filetypes](custom_filetypes.md) -- registering handlers for file types other than the firmware image.

## Key Concepts

### Job Documents

ESP RainMaker Neo OTA job documents extend the standard AWS IoT FreeRTOS OTA (AFR-OTA) format with additional fields while maintaining backward compatibility.

### Status Reporting

Devices report OTA progress and status information back to AWS IoT using structured JSON formats. Status details provide additional context about update progress, success, failure, or rejection.

### Compatibility

Job documents use the AFR-OTA schema, with RainMaker Neo-specific fields carried in an
additional `rmng_ota` section (see [ESP RainMaker Neo OTA Job Document Format](job_document.md)).

## Related Documentation

- [AWS IoT Jobs Documentation](https://docs.aws.amazon.com/iot/latest/developerguide/iot-jobs.html)
- [AWS IoT FreeRTOS OTA](https://docs.aws.amazon.com/freertos/latest/userguide/ota-update.html)
