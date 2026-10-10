# USB Device Descriptor (AP mode, `0424:3fc6`)

Raw `lsusb -v` dump: [`device_descriptor`](../../device_descriptor) (serial numbers masked).

## Operating modes

The Fireface UC can run in two modes, which change the whole USB descriptor:

| Mode     | ID (firmware ≤ v118) | ID (newer firmware) | Audio transfer type | Target OS |
| -------- | -------------------- | ------------------- | ------------------- | --------- |
| PC       | `0424:3fc5`          | `2a39:3fce`         | BULK / interrupt    | Windows   |
| **AP**   | `0424:3fc6`          | `2a39:3fc6` (?)     | **ISOCHRONOUS**     | macOS     |

### PC mode on a recent firmware (`2a39:3fce`)

Raw dump: [`device_descriptor_2a39_3fce_pc_mode`](../../device_descriptor_2a39_3fce_pc_mode).
6 interfaces; interface 0 alt 0–3 carries `EP 0x01 OUT` / `0x82 IN` (declared
*interrupt*, 64 / 576 / 896 / 768 bytes) plus `EP 0x03 OUT` / `0x84 IN` (bulk,
interrupt on alt 3); interfaces 1–4 are bulk like in AP mode; interface 5 has a
4-byte interrupt `EP 0x8b IN`. Not targeted, kept for reference.

To switch mode: with the USB cable **unplugged**, quickly double-click the control
knob on the front panel.

**This project only targets AP mode.**

## Device

| Field                | Value                         |
| -------------------- | ----------------------------- |
| bcdUSB               | 2.00 (High Speed)             |
| bDeviceClass         | 255 (Vendor Specific)         |
| idVendor / idProduct | `0x0424` / `0x3fc6`           |
| iManufacturer        | `RME`                         |
| iProduct             | `Fireface UC Mac (<serial>)`  |
| bNumConfigurations   | 1 (5 interfaces)              |
| bMaxPacketSize0      | 64                            |

## Interfaces and endpoints

| Interface | Alt | Class            | Endpoint        | Type | wMaxPacketSize        | Role |
| --------- | --- | ---------------- | --------------- | ---- | --------------------- | ---- |
| 0         | 0   | Vendor           | `0x01` OUT / `0x82` IN | ISO | 0 / 0          | Idle (no bandwidth) |
| 0         | 1   | Vendor           | `0x01` OUT / `0x82` IN | ISO | 532 / 508      | Audio, `[32 kHz; 64 kHz[` |
| 0         | 2   | Vendor           | `0x01` OUT / `0x82` IN | ISO | 780 / 732      | Audio, `[64 kHz; 128 kHz[` |
| 0         | 3   | Vendor           | `0x01` OUT / `0x82` IN | ISO | 2×652 / 2×604 (high-bandwidth) | Audio, `[128 kHz; 192 kHz]` |
| 1         | 0   | Vendor           | `0x85` IN       | BULK | 512                   | Level meters (see below) |
| 2         | 0   | Audio / MIDI Streaming | `0x07` OUT / `0x86` IN | BULK | 512 | MIDI (SysEx seen) |
| 3         | 0   | Vendor           | `0x88` IN / `0x09` OUT | BULK | 512           | Unknown |
| 4         | 0   | Vendor           | `0x8b` IN       | BULK | 512                   | Unknown (polled during init) |

`EP 0x01` = playback (host → device), `EP 0x82` = capture (device → host).
See [audio-streaming.md](../protocol/audio-streaming.md) for the payload format.

### Interface 1 — level meters (observations)

* Continuous traffic while the TotalMix mixer window is open.
* Payload: per-channel raw blocks of 64 bits, ordered like the mixer `wIndex`.
* Used to draw VU-meters, peaks and dBFS.
* The device has 18 inputs, 18 outputs, 18 playbacks; 324 bytes of payload per
  bulk transfer (`324 / 36 = 9`, `324 / 54 = 6`). Format not decoded yet.
