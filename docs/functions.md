# Reimplemented Functions (`asm/`)

Each `.c` file under [`asm/`](../asm) is a C rewrite of one function from the
disassembled macOS binaries, with the original assembly kept in comments next to the
matching C lines (see [reverse-engineering.md](reverse-engineering.md#conventions-used-in-asm)).

"Built" = compiled into the prototype by [`libusb_test/Makefile`](../libusb_test/Makefile).

## From `FirefaceUSB.kext` (kernel driver) — [`asm/FirefaceUSB/`](../asm/FirefaceUSB)

| Function | File | Built | USB requests | Role |
| -------- | ---- | ----- | ------------ | ---- |
| `hwGetRevision(dev, *rev)` | [hwGetRevision.c](../asm/FirefaceUSB/hwGetRevision.c) | ✅ | `0xc0 0x1c` | Firmware revision = `answer >> 16`. |
| `hwInitHardware(dev)` | [hwInitHardware.c](../asm/FirefaceUSB/hwInitHardware.c) | ✅ | 36× `0x40 0x16`, `0x40 0x10`, `0x40 0x20` | Hardware init at plug time (meaning of the requests unknown). |
| `hwGetRate(dev, unknownArg)` | [hwGetRate.c](../asm/FirefaceUSB/hwGetRate.c) | ✅ | `0xc0 0x11`, `0xc0 0x10` | Decodes the current sample rate from the status register. Covers all products; UC path ≈ line 580. |
| `hwSetSampleRate(dev, rate)` | [hwSetSampleRate.c](../asm/FirefaceUSB/hwSetSampleRate.c) | ✅ | `0x40 0x1b`, `0x40 0x10` | Sets the sample rate (period + family flag). |
| `xRate(rate)` | [xRate.c](../asm/FirefaceUSB/xRate.c) | ✅ | — | Snaps any rate to the nearest standard rate (thresholds 38049, 46007, 56999, 76099, 92099, 113999, 152199, 184199). |
| `ufx_id_to_freq(id)` | [ufx_id_to_freq.c](../asm/FirefaceUSB/ufx_id_to_freq.c) | ❌ | — | Frequency ID (0–11) → rate. |
| `hwMuteChannels(dev, unmute)` | [hwMuteChannels.c](../asm/FirefaceUSB/hwMuteChannels.c) | ✅ | `0x40 0x13` / `0x14` | Hardware mute/unmute of all outputs. |
| `hwWriteLedAndPga()` | [hwWriteLedAndPga.c](../asm/FirefaceUSB/hwWriteLedAndPga.c) | ❌ (standalone `main`) | `0x40 0x1a` | Preamp gain (ch 0–3) and output attenuator + LCD (ch 4–21) value conversion. |
| `hwGetStatusReg(*status, len)` | [hwGetStatusReg.c](../asm/FirefaceUSB/hwGetStatusReg.c) | ❌ (incomplete) | `0xc0 0x11` | Reads `len` 32-bit status words. |
| `hwGetIsoModeFlag(dev)` | [hwGetIsoModeFlag.c](../asm/FirefaceUSB/hwGetIsoModeFlag.c) | ❌ | `0xc0 0x11` (16 B) | Only for UFX+ / MADIface XT; always `0` for `0x3fc6`. |
| `Usb_vendor_device_request()` | [Usb_vendor_device_request.c](../asm/FirefaceUSB/Usb_vendor_device_request.c) | ❌ (pseudo-code) | any | Driver's single entry point for vendor requests; `bmRequestType = (dir << 7) \| 0x40`. |
| `sUserClientReadStatus(dev, *status)` | [de_RME_driver_USBAudioUserClient::sUserClientReadStatus.c](../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientReadStatus.c) | ✅ | `0xc0 0x19` | User-client call: read a status word (meaning unknown). |
| `sUserClientWriteSettings()` | [de_RME_driver_USBAudioUserClient::sUserClientWriteSettings.c](../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientWriteSettings.c) | ❌ (pseudo-code) | `0x40 0x17` (`0x12` on MADIface Pro) | User-client call relaying settings / +48 V / PAD / Hi-Z writes. |
| `sUserClientWriteVolume()` | [de_RME_driver_USBAudioUserClient::sUserClientWriteVolume.c](../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientWriteVolume.c) | ❌ (standalone `main`) | `0x40 0x12` / `0x15` | Shows which request each product uses for volume writes (`0x12` for the UC). |

## From Fireface USB Settings — [`asm/FirefaceUSBSettings/`](../asm/FirefaceUSBSettings)

| Function | File | Built | USB requests | Role |
| -------- | ---- | ----- | ------------ | ---- |
| `GetStatus(dev)` | [CHardware::GetStatus.c](../asm/FirefaceUSBSettings/CHardware::GetStatus.c) | ✅ | `0xc0 0x11` | Decodes and prints clock source, lock/sync and frequency IDs of WC, optical and coax inputs. See [settings.md](protocol/settings.md#status-register-0x11). |
| `getDeviceNameByTypeId(pid, **name)` | [getDeviceNameByTypeId.c](../asm/FirefaceUSBSettings/getDeviceNameByTypeId.c) | ✅ | — | Product ID → name. See [product-ids.md](hardware/product-ids.md). |
| `hwWriteSettings_uc()` | [hwWriteSettings_uc.c](../asm/FirefaceUSBSettings/hwWriteSettings_uc.c) | ❌ (pseudo-code) | `0x40 0x17`, `0x40 0x10` | Builds `FLAGS_1` / `FLAGS_2` from `struct FirefaceSettings`. See [settings.md](protocol/settings.md). |

## From TotalMix FX — [`asm/TotalmixFX/`](../asm/TotalmixFX)

| Function | File | Built | USB requests | Role |
| -------- | ---- | ----- | ------------ | ---- |
| `CCardIOUC::SetGain()` → `setVolume(dev, ch, type, out, level)` | [CCardIOUC::SetGain.c](../asm/TotalmixFX/CCardIOUC::SetGain.c) | ✅ | `0x40 0x12` | Mixer matrix gain. Has known discrepancies with the assembly, see [mixer-routing.md](protocol/mixer-routing.md#index-encoding-on-the-wire). |
| `CCardIOUC::WriteDWORD(val)` | [CCardIOUC::WriteDWORD.c](../asm/TotalmixFX/CCardIOUC::WriteDWORD.c) | ❌ (pseudo-code) | `0x40 0x17` | Maps TotalMix parameter IDs (`0x16`…`0x21` in the high word) to +48 V / PAD / Hi-Z bits. See [controls.md](protocol/controls.md#48-v-phantom-pad-hi-z-instrument). |

## Notes

* Most functions hard-code `productId = 0x3fc6` instead of taking it as an argument;
  the original parameters are left commented out in the signatures.
* Declarations of the built functions live in [`libusb_test/asm.h`](../libusb_test/asm.h).
* Files marked *pseudo-code* or *standalone `main`* are working notes and do not
  compile as-is.
