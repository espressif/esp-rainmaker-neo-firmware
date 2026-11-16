# Matter examples

Examples where the node is on the RainMaker cloud **and** on a Matter fabric from
one data model: the application builds the usual RainMaker Neo devices/params, and the
[`esp_rmaker_neo_matter_mirror`](../../components/esp_rmaker_neo_matter_mirror)
component derives the esp-matter endpoint/cluster tree from them and keeps both
sides in sync. The RainMaker Neo param store stays the single canonical state store — there
is no esp-matter tree to construct, no attribute callbacks, and no Matter-specific
data model in the application. Start with a plain device example in
[`examples/`](../) — and come here once the product also has to speak Matter.


| Example                         | Purpose                                                                                       |
| ------------------------------- | --------------------------------------------------------------------------------------------- |
| [`matter_light`](matter_light/) | Full color light (power, brightness, CCT, hue/saturation) mirrored as an Extended Color Light |


Cloud and phone apps see a plain RainMaker Neo node; Matter controllers see the derived
tree, and their commands arrive at the same RainMaker Neo write callback the cloud uses.
The mirror's mapping tables, cluster factories and factory-reset wiring are documented in the
[component README](../../components/esp_rmaker_neo_matter_mirror/README.md).

## How these differ from the other examples

- **ESP-IDF only.** The mirror's port needs esp-matter, so there is no POSIX build.
- **A factory partition is mandatory** — see [Factory data and credentials](#factory-data-and-credentials).



## Prerequisites

- **ESP-IDF**: an exported ESP-IDF environment, **v6.0.2 or later**
(`. $IDF_PATH/export.sh`).
- **esp-matter**: a [checkout](https://github.com/espressif/esp-matter) on
**release/v1.6 or newer**, with `ESP_MATTER_PATH` exported.

## Onboarding

Set by the *Onboarding mode* choice in menuconfig. The mode decides which
ecosystem brings up the network.


| Mode                                                                         | What happens                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| ---------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **RainMaker via Matter commissioning** (default, `..._ONBOARD_MATTER_FIRST`) | Matter commissioning is the single onboarding path: Wi-Fi credentials arrive over CHIPoBLE and there is no RainMaker provisioning session. The node ends up on **two fabrics** — see [Matter-first](#matter-first-default) below. |
| **RainMaker provisioning, then on-network Matter** (`..._ONBOARD_ONNETWORK`) | The device joins Wi-Fi via standard RainMaker BLE provisioning; controllers then commission the already-on-network device over IP. |

### Matter-first (default)

Two commissionings, and the factory QR payload starts both:

- **From RainMaker Home** — one seamless flow: the app commissions its own fabric
  first, passing the Wi-Fi credentials over CHIPoBLE, then re-opens pairing and
  commissions the RainMaker fabric on-network itself, back to back. The user scans
  once and sees a single onboarding.
- **From an external Matter app** (Apple Home, Google Home, Alexa, Home Assistant,
  chip-tool) — that app takes the first fabric and brings the node onto Wi-Fi, and the
  second commissioning is a manual step: open a pairing window from it (its
  multi-admin / "add to another ecosystem" flow), then commission the RainMaker fabric
  through that window from RainMaker Home.

The user-node association is done in the RainMaker-fabric commissioning, using the
node id the mirror puts in the CSR.

> Commercial ecosystems validate the attestation chain, so a device built with test
> DAC/PAI/CD material is only accepted by controllers that allow test credentials.

## Factory data and credentials

> **Assisted claiming is not supported.** It stores the credentials during
> provisioning, which here runs after node init has already read them — and the
> Matter-first mode has no provisioning session at all. The examples set
> `CONFIG_ESP_RMAKER_NEO_ASSISTED_CLAIM=n` and fail the build otherwise. A
> pre-claimed factory partition is a prerequisite; register the nodes from the
> [dashboard](https://docs.neo.rainmaker.espressif.com/docs/dashboard/node-management/registration#generate-nodes).

One `fctry` partition carries both stacks: the `chip-factory` namespace (DAC, PAI,
CD, commissioning passcode/discriminator) and the RainMaker namespace
(`mqtt_host`, `random`).

Write it to the device:

```sh
esptool.py write_flash <'fctry' offset in partition table> <factory binary>
```

**MQTT credentials are the same DAC.** `app_main()` installs
`esp_rmaker_matter_mirror_get_dac_credentials()`: the client certificate and key
are served from `chip-factory` and the node id is the DAC subject common name —
the factory image carries no second copy of the identity.

> **The DAC is read from `fctry` only.** A DAC in the `esp_secure_cert` partition,
> and a key protected by the Digital Signature peripheral, are not supported yet.
> The partition table reserves `esp_secure_cert` for that.

## Going Further

Full walkthroughs — setup, configuration reference, provisioning, OTA and
troubleshooting — live in the
[firmware guides](https://docs.neo.rainmaker.espressif.com/docs/firmware/).
