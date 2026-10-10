# Control Requests Reference

Every control/configuration operation is a **vendor-specific control transfer on
endpoint 0**. Only two `bmRequestType` values are used:

* `0x40` — vendor, host → device (write, `wLength = 0`, no data stage)
* `0xc0` — vendor, device → host (read, data returned in the data stage)

In the macOS driver every request goes through
[`Usb_vendor_device_request()`](../../asm/FirefaceUSB/Usb_vendor_device_request.c),
which builds `bmRequestType = (direction << 7) | 0x40`.

## Summary table

| bmRequestType | bRequest    | wValue            | wIndex             | wLength | Meaning | Source function |
| ------------- | ----------- | ----------------- | ------------------ | ------- | ------- | --------------- |
| `0x40`        | `0x10` (16) | `0x00`/`0x10`/`0x20` (rate family) | `0x0030` (mask) | 0       | Set sample rate, packet 2/2 | [`hwSetSampleRate()`](../../asm/FirefaceUSB/hwSetSampleRate.c) |
| `0x40`        | `0x10` (16) | `FLAGS_2`         | `0x46cf`           | 0       | Clock source, single speed, AES/SPDIF, optical mode | [`hwWriteSettings_uc()`](../../asm/FirefaceUSBSettings/hwWriteSettings_uc.c) |
| `0x40`        | `0x10` (16) | `0x0800`          | `0xb800`           | 0       | **Unknown** — sent once at init | [`hwInitHardware()`](../../asm/FirefaceUSB/hwInitHardware.c) |
| `0xc0`        | `0x10` (16) | 0                 | 0                  | 4       | Read register (second word used by rate detection) | [`hwGetRate()`](../../asm/FirefaceUSB/hwGetRate.c) |
| `0xc0`        | `0x11` (17) | 0                 | 0                  | 4 (×n)  | Read **status register** (clock source, sync/lock, rates) | [`hwGetRate()`](../../asm/FirefaceUSB/hwGetRate.c), [`hwGetStatusReg()`](../../asm/FirefaceUSB/hwGetStatusReg.c), [`GetStatus()`](../../asm/FirefaceUSBSettings/CHardware::GetStatus.c) |
| `0x40`        | `0x12` (18) | volume            | mixer index        | 0       | Set input / playback / output volume | [`setVolume()`](../../asm/TotalmixFX/CCardIOUC::SetGain.c) |
| `0x40`        | `0x13` (19) | `0xffff`          | `0xc000`           | 0       | Hardware **mute** all outputs | [`hwMuteChannels()`](../../asm/FirefaceUSB/hwMuteChannels.c) |
| `0x40`        | `0x14` (20) | `0xffff`          | `0xc000`           | 0       | Hardware **unmute** all outputs | [`hwMuteChannels()`](../../asm/FirefaceUSB/hwMuteChannels.c) |
| `0x40`        | `0x15` (21) | `1` on / `0` off  | channel ID         | 0       | Loopback on output | `CCardIOUC::SetLoopbackChannel()` (not reimplemented) |
| `0x40`        | `0x16` (22) | `0x0001`          | `0..0x11`, `0x20..0x31` | 0  | **Unknown** — 36 requests at init | [`hwInitHardware()`](../../asm/FirefaceUSB/hwInitHardware.c) |
| `0x40`        | `0x17` (23) | bit flag          | same bit flag      | 0       | +48 V / PAD / Hi-Z (bitmask write) | [`WriteDWORD()`](../../asm/TotalmixFX/CCardIOUC::WriteDWORD.c), [`sUserClientWriteSettings()`](../../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientWriteSettings.c) |
| `0x40`        | `0x17` (23) | `FLAGS_1`         | `0x0fc0`           | 0       | Input / output / phones reference levels | [`hwWriteSettings_uc()`](../../asm/FirefaceUSBSettings/hwWriteSettings_uc.c) |
| `0xc0`        | `0x19` (25) | 0                 | 0                  | 4       | **Unknown** — read status | [`sUserClientReadStatus()`](../../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientReadStatus.c) |
| `0x40`        | `0x1a` (26) | gain / volume     | channel `0..21`    | 0       | Preamp gain (0–3) or output volume + LCD (4–21) | [`hwWriteLedAndPga()`](../../asm/FirefaceUSB/hwWriteLedAndPga.c) |
| `0x40`        | `0x1b` (27) | period high word  | period low byte \| flag | 0  | Set sample rate, packet 1/2 | [`hwSetSampleRate()`](../../asm/FirefaceUSB/hwSetSampleRate.c) |
| `0xc0`        | `0x1c` (28) | 0                 | 0                  | 4       | Get firmware revision (`answer >> 16`) | [`hwGetRevision()`](../../asm/FirefaceUSB/hwGetRevision.c) |
| `0x40`        | `0x20` (32) | `0x0001`          | `0x0000`           | 0       | **Unknown** — sent last at init | [`hwInitHardware()`](../../asm/FirefaceUSB/hwInitHardware.c) |

## Notes

* `0x17` is a **masked register write**: `wIndex` is the mask of bits to change,
  `wValue` their new value. This explains why +48 V/PAD/Hi-Z use the same value in
  both fields when switching on, and `wValue = 0` when switching off, and why the
  settings write uses mask `0x0fc0` (bits 6–11).
  *(This interpretation is consistent with all captures but not formally proven.)*
* On MADIface Pro (`0x3fc3`) the settings write uses `bRequest 0x12` instead of
  `0x17` (see `sUserClientWriteSettings()`).
* `sUserClientWriteVolume()` chooses `0x12` (most devices) or `0x15` (MADIface,
  UFX+, UFX II, MADIface Pro, Digiface) for volume writes.
* Details for each control: [controls.md](controls.md), [settings.md](settings.md),
  [mixer-routing.md](mixer-routing.md).
