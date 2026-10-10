# RME Fireface UC Linux drivers

> This repository is currently for development purposes only and does **not**
> contain any working driver yet.

## Goal

1. Provide working Linux drivers for the **RME Fireface UC**, supporting all inputs
   and outputs.
2. Then, consider a generic DSP control interface similar to RME TotalMix.

Although this project targets the Fireface UC, the official drivers are shared with
the **UFX / UFX+**, **802**, **UCX**, **Babyface**, **Babyface Pro** and
**MADIface XT / USB / Pro**. This work can therefore serve as a base for porting
those devices too.

## Target

* Fireface UC, **firmware v118**, in **AP (Mac) mode** — USB ID `0424:3fc6`,
  isochronous audio. PC mode (`0x3fc5`, bulk audio) is not targeted.
* Reference: macOS 10.8.5 VM with the official
  [drivers v2.20](http://www.rme-audio.de/en/downloads/driver/uc.php).

Everything here comes from USB traffic analysis (**usbmon & Wireshark**) and from the
disassembly of the official [FirefaceUSB.kext](asm/FirefaceUSB.asm) driver and of the
[Fireface USB Settings](asm/FirefaceUSBSettings.asm) and
[TotalMix FX](asm/TotalmixFX.asm) applications.

## Repository

| Path | Content |
| ---- | ------- |
| [`docs/`](docs/README.md) | **Full documentation** (protocol, hardware, method, roadmap) |
| [`asm/`](asm) | Disassembly listings + C reimplementations of key functions |
| [`libusb_test/`](libusb_test) | Userspace libusb-1.0 prototype (plays audio) |
| [`cap/`](cap) | Wireshark USB captures |
| [`device_descriptor`](device_descriptor) | `lsusb -v` dump of the device |

## Quick start (prototype)

```sh
# deps: gcc, make, libusb-1.0 headers
make                                                     # -> bin/rme_uc
sudo ./bin/rme_uc < libusb_test/Her-Swim__44100-f32le.raw   # device in AP mode
```

See [docs/libusb-test.md](docs/libusb-test.md).

## Documentation

* [Documentation index & glossary](docs/README.md)
* [Control requests reference](docs/protocol/control-requests.md)
* [Audio streaming](docs/protocol/audio-streaming.md)
* [Controls](docs/protocol/controls.md) · [Settings](docs/protocol/settings.md) · [Mixer & routing](docs/protocol/mixer-routing.md)
* [Reimplemented functions](docs/functions.md)
* [Roadmap & open questions](docs/roadmap.md)
