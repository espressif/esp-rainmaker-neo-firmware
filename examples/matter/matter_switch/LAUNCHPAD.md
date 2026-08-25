# Matter Switch

A dimming switch that is a plain ESP RainMaker Neo node to the cloud and a
**Mounted Dimmable Load Control** to a Matter fabric. Power and the dim level are
controllable from the ESP RainMaker Home app, from the cloud, and from any Matter
controller on the fabric.

## On-device behavior

- **LED** (on-board RGB LED, present on most ESP dev boards) stands in for the
  switched load. Power switches it, and the dim level sets its brightness.
- **Defaults** — the load starts off, at a dim level of 50.
- **Button** (BOOT button by default):
  - **Short press** — toggle power.
  - **Long press** — step the dim level by 25, back to 25 after 100.
- **Dim** — a dim level change, from the button, the app or a controller, also
  turns the load on.
- **Identify** — a Matter controller can ask the switch to identify itself. The
  LED then blinks for the requested duration.

Both sides stay in sync. A change from the cloud, the app or a schedule appears
on the Matter fabric, and a controller command lands in the same callback the
cloud uses. A dim command with a transition time fades the LED, and the cloud
receives the settled value alone.

## Before you flash

> **This firmware does not work with the public ESP RainMaker Neo deployment.**
> The cloud credentials must be in the factory partition before commissioning,
> and the public deployment has no way to put them there. Assisted claiming is
> the only claiming this SDK provides, it runs over the BLE provisioning session,
> and this firmware has no such session. Use a private or self-hosted deployment,
> and register the nodes from its dashboard.

**A factory partition is mandatory for this firmware.** Assisted claiming does
not apply to a Matter node, so this example disables it. One `fctry` partition
carries both stacks: the Matter credentials (DAC, PAI, CD, commissioning
passcode and discriminator) and the RainMaker credentials.

Obtain the factory partition binary from your private deployment's dashboard,
then flash it at this example's `fctry` offset before you flash the firmware.
The offsets and step-by-step instructions are in the README shown on the
Launchpad landing page.

> Commercial ecosystems validate the attestation chain. A device built with test
> DAC/PAI/CD material is only accepted by a controller that allows test
> credentials.

## After flashing

This build onboards through Matter commissioning. There is no RainMaker
provisioning session — the Wi-Fi credentials arrive over Matter. The node ends
up on two fabrics, and the factory QR code starts both commissionings.

1. Open the **ESP RainMaker Home** app.
2. Scan the QR code of the factory partition. The app commissions its own fabric
   first, then the RainMaker fabric, back to back.
3. Control the switch from the app and from your Matter controller.

To commission from another Matter app first (Apple Home, Google Home, Alexa,
Home Assistant), let that app take the first fabric. Then open a pairing window
from it and commission the RainMaker fabric from ESP RainMaker Home.
