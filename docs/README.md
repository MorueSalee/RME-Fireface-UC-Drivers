# Documentation

Documentation of the reverse engineering of the **RME Fireface UC** USB protocol
(AP / Mac mode, `0424:3fc6`), toward a Linux driver.

## Contents

### Project

* [Project layout](project-layout.md) — what every file and folder is.
* [Reverse-engineering method](reverse-engineering.md) — setup, binaries, conventions.
* [Roadmap and open questions](roadmap.md) — every known TODO in one place.

### Hardware

* [USB device descriptor](hardware/device-descriptor.md) — PC vs AP mode, interfaces, endpoints.
* [RME USB product IDs](hardware/product-ids.md) — whole RME USB family.

### Protocol

* [Control requests reference](protocol/control-requests.md) — table of every `bRequest`.
* [Audio streaming](protocol/audio-streaming.md) — ISO format, alternate settings, sync.
* [Mixer and routing](protocol/mixer-routing.md) — DSP matrix indexes and gain encoding.
* [Controls](protocol/controls.md) — gain, phantom, PAD, Hi-Z, mute, loopback, sample rate, firmware.
* [Settings](protocol/settings.md) — clock source, levels, digital formats, status register.
* [Sequences](protocol/sequences.md) — init at plug-in, playback start/stop.

### Code and data

* [Reimplemented functions](functions.md) — catalogue of `asm/*/*.c`.
* [libusb prototype](libusb-test.md) — build, run, internals, limitations.
* [USB captures](captures.md) — index of `cap/`.

## Glossary

| Term | Meaning |
| ---- | ------- |
| **AP mode** | Mac mode of the device (`0x3fc6`), isochronous audio. Target of this project. |
| **PC mode** | Windows mode (`0x3fc5`), bulk audio. Not targeted. |
| **Alternate setting** | USB interface variant; on interface 0 it selects the endpoint bandwidth, hence the sample-rate group (1, 2, 3). |
| **ISO** | Isochronous USB transfer (guaranteed bandwidth, no retry), used for audio. |
| **Control transfer** | Request on endpoint 0, described by `bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`. All device controls are vendor requests (`0x40` write / `0xc0` read). |
| **Frame (audio)** | One sample for each of the 18 channels plus a 4-byte header: 19 × 4 = 76 bytes. |
| **Playback (pb)** | Virtual output channel from the computer, routed by the DSP matrix to physical outputs. |
| **Matrix index** | Address of one gain cell in the DSP mixer (`0..647`), or of an output fader (`992..1009`). |
| **PGA** | Programmable-gain amplifier (mic/line preamp). |
| **WC** | Word clock (BNC sync I/O). |
| **kext** | macOS kernel extension (`FirefaceUSB.kext`, the official driver). |
| **TotalMix FX** | RME mixer application. |
| **Fireface USB Settings** | RME settings application (clock, levels, formats). |
| **usbmon** | Linux kernel USB traffic monitor, read by Wireshark. |
