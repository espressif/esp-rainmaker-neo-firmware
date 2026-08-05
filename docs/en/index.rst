ESP RainMaker Neo Programming Guide
===================================

The ESP RainMaker Neo Firmware SDK provides what a device needs to run as an
ESP RainMaker Neo node: the node libraries (``esp_rmaker_neo``, ``esp_rmaker_neo_common``,
``esp_rmaker_neo_ota``) and the OS abstraction layer (``osal``) they are built on. It
builds for ESP-IDF targets, and additionally for POSIX hosts for testing.

This guide has two parts:

- :doc:`specs/index` -- what the firmware does: boot and initialization order,
  configuration and state payloads, MQTT topics and the cloud event protocol,
  services, OTA, and error recovery.
- :doc:`c-api-reference/index` -- the C API, generated from the headers. Unless a
  page says otherwise the API is identical on ESP-IDF and POSIX; where a header
  exists in per-platform variants, only the platform-neutral one is documented
  and the differences are described in prose.

This documentation covers the firmware specification and the C API reference.
Setup, build and factory-provisioning instructions are not part of this build --
see the `firmware guides <https://docs.neo.rainmaker.espressif.com/docs/firmware/>`_.
