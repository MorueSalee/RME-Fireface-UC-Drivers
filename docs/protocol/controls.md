# Controls

Per-feature description of the control transfers. All are vendor control transfers
on endpoint 0; see the summary in [control-requests.md](control-requests.md).

Transfers are written in this format:

```
bmRequestType    : 0x40
bRequest         : 0x15 (21)
wValue           : ...
wIndex           : ...
wLength          : 0x0000
```

---

## Set Loopback

* `CCardIOUC::SetLoopbackChannel(int, signed char)` (TotalMix, line 120194 of
  [`TotalmixFX.asm`](../../asm/TotalmixFX.asm), **not reimplemented yet**)

Loopback routes the signal of an output back to the matching input (for recording
what is played). The loopback state of every output is sent during device
initialisation, right after the faders are initialised.

```
bmRequestType    : 0x40
bRequest         : 0x15 (21)
wValue           : 0x0001 on / 0x0000 off
wIndex           : channelID
wLength          : 0x0000
```

**TODO**: reimplement it; find out whether init sends default or user values
(should be user values).

## Set Input / Playback Volume

* [`CCardIOUC::SetGain()`](../../asm/TotalmixFX/CCardIOUC::SetGain.c)
* [`de_RME_driver_USBAudioUserClient::sUserClientWriteVolume()`](../../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientWriteVolume.c)
* [`Usb_vendor_device_request()`](../../asm/FirefaceUSB/Usb_vendor_device_request.c)

```
bmRequestType    : 0x40
bRequest         : 0x12 (18)
wValue           : volume
wIndex           : channelID (matrix index, see mixer-routing.md)
wLength          : 0x0000
```

Index and value encoding: [mixer-routing.md](mixer-routing.md).

## Set Output Volume

Two different transfers affect output levels, and TotalMix uses both.

**1. Mixer output fader** — same request as above, indexes `992..1009`:

```
bmRequestType    : 0x40
bRequest         : 0x12 (18)
wValue           : volume
wIndex           : channelID (992 - 1009)
wLength          : 0x0000
```

**2. Output attenuator + front-panel display** —
[`hwWriteLedAndPga()`](../../asm/FirefaceUSB/hwWriteLedAndPga.c).
Accepts `[0x3f (-inf); 0x00 (+6 dB)]`, 64 steps:

```
bmRequestType    : 0x40
bRequest         : 0x1a (26)
wValue           : volume
wIndex           : channelID (4 - 21)
wLength          : 0x0000
```

Both change the output level; only the second updates the front-panel LCD.

**TODO**: find out who calls which, when, and why TotalMix alternates them.

## Set Preamp Gain

* [`hwWriteLedAndPga()`](../../asm/FirefaceUSB/hwWriteLedAndPga.c)

Inputs 1 to 4 have a gain control:

| Inputs | Range                         | Raw values         |
| ------ | ----------------------------- | ------------------ |
| 1, 2 (mic) | 0, then 10 to 65 dB, 1 dB steps | `0x00`, `0x0a..0x41` |
| 3, 4 (line/inst) | 0 to 18 dB, 0.5 dB steps | `0x00..0x24`   |

`hwWriteLedAndPga()` performs the conversion (on the Fireface UC, mic gains get `+9`
added when non-zero) but **does not clamp the upper bound**. For outputs it adds 4 to
the index and sends `0x3f - value`.

```
bmRequestType    : 0x40
bRequest         : 0x1a (26)
wValue           : gain
wIndex           : channelID (0 - 3)
wLength          : 0x0000
```

**TODO**: find the caller in TotalMix.

## +48 V Phantom, PAD, Hi-Z (Instrument)

* [`CCardIOUC::WriteDWORD()`](../../asm/TotalmixFX/CCardIOUC::WriteDWORD.c)
* [`de_RME_driver_USBAudioUserClient::sUserClientWriteSettings()`](../../asm/FirefaceUSB/de_RME_driver_USBAudioUserClient::sUserClientWriteSettings.c)
* [`Usb_vendor_device_request()`](../../asm/FirefaceUSB/Usb_vendor_device_request.c)

All use `bRequest 0x17` with a single bit set in both `wValue` and `wIndex` (bit mask)
when switching on, and `wValue = 0` when switching off:

```
bmRequestType    : 0x40
bRequest         : 0x17 (23)
wValue           : <bit> on / 0x0000 off
wIndex           : <bit>
wLength          : 0x0000
```

| Feature        | Channel | Bit (`wIndex`) | TotalMix `WriteDWORD` value |
| -------------- | ------- | -------------- | --------------------------- |
| +48 V          | 1       | `0x0001`       | `0x16000x`                  |
| +48 V          | 2       | `0x0002`       | `0x17000x`                  |
| PAD            | 3       | `0x0008`       | `0x20000x`                  |
| PAD            | 4       | `0x0004`       | `0x21000x`                  |
| Inst (Hi-Z)    | 3       | `0x0020`       | `0x1c000x`                  |
| Inst (Hi-Z)    | 4       | `0x0010`       | `0x1d000x`                  |

+48 V applies only to inputs 1–2; PAD and Hi-Z only to inputs 3–4.

**TODO (PAD)**: depending on an unknown condition, `WriteDWORD()` can send
`0x8008` / `0x4004` instead of `0x0008` / `0x0004`. Never observed in Wireshark.

## Hardware Output Mute

* [`hwMuteChannels()`](../../asm/FirefaceUSB/hwMuteChannels.c)

**Mutes all outputs in hardware.** Used only when audio playback starts (unmute)
and stops (mute). TotalMix's own mute buttons work through volumes instead.

```
bmRequestType    : 0x40
bRequest         : 0x13 (19) mute / 0x14 (20) unmute
wValue           : 0xffff
wIndex           : 0xc000
wLength          : 0x0000
```

(`wValue`/`wIndex` come from the 18-bit channel mask `0x3ffff`: `wIndex = mask << 14`,
`wValue = mask >> 2`.)

## Get Sample Rate

* [`hwGetRate()`](../../asm/FirefaceUSB/hwGetRate.c) (Fireface UC path starts around line 580)

Two reads, in this order:

```
bmRequestType    : 0xc0
bRequest         : 0x11 (17)
wValue           : 0x0000
wIndex           : 0x0000
wLength          : 0x0004
```

```
bmRequestType    : 0xc0
bRequest         : 0x10 (16)
wValue           : 0x0000
wIndex           : 0x0000
wLength          : 0x0004
```

The first word (status register) is decoded:

* bits 9–11: current clock source (`1` SPDIF, `2` optical, `3` word clock,
  `7` internal — see [settings.md](settings.md#status-register-0x11));
* when locked to an external source, the matching 4-bit **frequency ID**
  (bits 12–15 coax, 16–19 optical, 20–23 word clock) is mapped to a rate
  (`0`→32000, `1`→44100, `2/3`→48000, `4`→64000, `5`→88200, `6/7`→96000,
  `8`→128000, `9`→176400, `10/11`→192000 — same table as
  [`ufx_id_to_freq()`](../../asm/FirefaceUSB/ufx_id_to_freq.c));
* otherwise bits 24–25 give the base rate (32000 / 44100 / 48000) and bit 26
  (×2) / bit 27 (×4) the speed multiplier.

The second read (`0x10`) is only checked for a `0x40` low byte. The function is
2000 lines long because it covers every RME product; only the `0x3fc6` path
matters here.

During playback, the macOS driver polls this every 2 ms.

## Set Sample Rate

* [`hwSetSampleRate()`](../../asm/FirefaceUSB/hwSetSampleRate.c), [`xRate()`](../../asm/FirefaceUSB/xRate.c)

Two transfers, in this order. They may need to be preceded by an alternate-setting
change on interface 0 — **mandatory when changing rate group**, see
[audio-streaming.md](audio-streaming.md):

* bAlternateSetting **1**: `[32000 Hz; 64000 Hz[`
* bAlternateSetting **2**: `[64000 Hz; 128000 Hz[`
* bAlternateSetting **3**: `[128000 Hz; 192000 Hz]`

```
bmRequestType    : 0x40
bRequest         : 0x1b (27)
wValue           : var1
wIndex           : var2
wLength          : 0x0000
```

```
bmRequestType    : 0x40
bRequest         : 0x10 (16)
wValue           : var3
wIndex           : var4
wLength          : 0x0000
```

How the values are computed for the Fireface UC:

1. `xRate(rate)` snaps the requested rate to the nearest standard rate (used only to
   pick the rate family).
2. Family flag: `0` for 32k/64k/128k, `1` for 44.1k/88.2k/176.4k, `2` for
   48k/96k/192k.
3. **Packet 1 (`0x1b`)**: `period = 0x5f5e10000000 / rate`
   (= 100 000 000 × 2²⁰ / rate, using the **exact** requested rate),
   then `<< 1` if rate > 57000, `<< 2` if rate > 114000 (only the low 32 bits are kept).
   `wValue = period >> 16`, `wIndex = (period & 0xff00) | flag`.
   Because the exact rate is used, non-standard rates work too — see the captures
   at 42336, 44056, 45937 Hz … in [`cap/`](../../cap).
4. **Packet 2 (`0x10`)**: `wValue = (flag << 4) >> 1` (`0x00` / `0x08` / `0x10`),
   then `| 0x20` if 56999 < rate < 114000, or `| 0x40` if rate ≥ 114000;
   `wIndex = 0x0078` (mask of the bits written).

## Get Firmware Version

* [`hwGetRevision()`](../../asm/FirefaceUSB/hwGetRevision.c)
* [`Usb_vendor_device_request()`](../../asm/FirefaceUSB/Usb_vendor_device_request.c)

```
bmRequestType    : 0xc0
bRequest         : 0x1c (28)
wValue           : 0x0000
wIndex           : 0x0000
wLength          : 0x0004
```

```
version = (uint32_t)answer >> 0x10
```

## Not yet documented

* **Get current clock** — partly understood via `GetStatus()`, see
  [settings.md](settings.md#status-register-0x11).
* **Get VU meters** — bulk endpoint `0x85`, see
  [device-descriptor.md](../hardware/device-descriptor.md#interface-1--level-meters-observations).
