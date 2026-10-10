# Reverse-Engineering Method

## Setup

| Item | Version |
| ---- | ------- |
| Device | RME Fireface UC, **firmware v118**, AP (Mac) mode |
| Host OS for the official driver | macOS 10.8.5 in a virtual machine |
| Official driver | [RME driver v2.20 for Mac](http://www.rme-audio.de/en/downloads/driver/uc.php) |
| Capture | Linux **usbmon** + **Wireshark** on the VM host |

## Sources of information

1. **USB traffic** — each action in TotalMix FX or Fireface USB Settings is captured
   and diffed. Captures: [`cap/`](../cap), index in [captures.md](captures.md).
2. **Disassembly** of the three macOS binaries (Hopper-style listings, x86-64):

   | Binary | Listing | Role |
   | ------ | ------- | ---- |
   | `FirefaceUSB.kext` | [`asm/FirefaceUSB.asm`](../asm/FirefaceUSB.asm) (~121k lines) | Kernel driver: audio engine, ISO streaming, hardware access. |
   | Fireface USB Settings | [`asm/FirefaceUSBSettings.asm`](../asm/FirefaceUSBSettings.asm) (~112k lines) | Settings app: clock, levels, digital formats, status. |
   | TotalMix FX | [`asm/TotalmixFX.asm`](../asm/TotalmixFX.asm) (~1.1M lines) | Mixer app: matrix gains, preamp, phantom, loopback. |

   The applications talk to the kext through an IOKit user client
   (`IOConnectCallScalarMethod`, class `de_RME_driver_USBAudioUserClient`), and the
   kext turns each call into a vendor control request.
3. **Reimplementation** — interesting functions are rewritten in C under
   [`asm/`](../asm) (catalogue: [functions.md](functions.md)) and exercised against the
   real device with the [libusb prototype](libusb-test.md).

The C++ symbols are not stripped (e.g. `__Z15hwSetSampleRateP11IOUSBDevicejt`), so
function names and argument types are known. The `FirefaceSettings` structure layout
comes from the settings app's `print_settings()` function.

## Conventions used in `asm/`

* One file per function, named after the original symbol
  (`Class::Method.c` keeps the `::`).
* The original assembly is pasted in `/* ... */` blocks, each followed by the
  equivalent C lines, so the translation can be checked instruction by instruction.
* Register names are often kept as variable names (`eax`, `r14d`, `r15w`…).
* The `productId` argument of the original functions is commented out in the
  signature and replaced by a local `uint16_t productId = 0x3fc6;`, so that the
  branches for other RME products stay readable but inactive.
  Product-ID arithmetic tricks are explained in
  [product-ids.md](hardware/product-ids.md#product-id-tricks-in-the-disassembly).
* USB requests are issued via `send_ctrl_setup()` from
  [`libusb_test/usb.h`](../libusb_test/usb.h) instead of the kext's
  `Usb_vendor_device_request()`.

## Searching the listings

The `.asm` files are huge; search by symbol:

```sh
grep -n "hwSetSampleRate" asm/FirefaceUSB.asm
grep -n "^\s*__ZN9CCardIOUC.*:" asm/TotalmixFX.asm     # all CCardIOUC methods
```

`CCardIOUC` is the TotalMix class that drives the Fireface UC. Its methods (line
numbers in `TotalmixFX.asm`), useful starting points for the open questions in
[roadmap.md](roadmap.md):

| Method | Line | Status |
| ------ | ---- | ------ |
| `CCardIOUC::GetVersion()` | 119814 | — |
| `CCardIOUC::ReadStatus(LEVELDATARAW*)` | 119912 | Not reimplemented — likely the level meters |
| `CCardIOUC::SetGain(int, int, int, int)` | 120054 | [Reimplemented](../asm/TotalmixFX/CCardIOUC::SetGain.c) |
| `CCardIOUC::SetOutputVolume(int, float)` | 120111 | Not reimplemented — likely the `0x1a` output path |
| `CCardIOUC::SetProperty(...)` | 120167 | — |
| `CCardIOUC::SetLoopbackChannel(int, signed char)` | 120194 | Not reimplemented — loopback (`0x15`) |
| `CCardIOUC::WriteDWORD(unsigned int)` | 120236 | [Reimplemented](../asm/TotalmixFX/CCardIOUC::WriteDWORD.c) |
| `CCardIOUC::ReadBuffer(unsigned int*, int*)` | 120364 | — |
| `CCardIOUC::TriggerUSB()` | 120719 | — |

## Scope

Only **AP mode** (`0x3fc6`, isochronous) is targeted. The same macOS driver serves
UFX/UFX+, 802, UCX, Babyface (Pro), MADIface XT/USB/Pro, so this work can serve as a
base for those devices too.
