# Project Layout

```
.
├── README.md                 Project overview (entry point)
├── Makefile                  Top-level: builds libusb_test/ (make -C libusb_test)
├── device_descriptor         Raw `lsusb -v -d 0424:3fc6` dump (AP mode, fw v118) + annotated notes
├── device_descriptor_2a39_3fce_pc_mode   `lsusb -v` of a recent firmware in PC mode
├── tools/                    Offline usbmon pcapng parsers (usbmon_ctl.py, usbmon_iso.py)
├── docs/                     This documentation
│   ├── README.md             Documentation index + glossary
│   ├── project-layout.md     This file
│   ├── reverse-engineering.md
│   ├── functions.md          Catalogue of asm/ reimplementations
│   ├── libusb-test.md        Prototype documentation
│   ├── captures.md           Index of cap/
│   ├── roadmap.md            Open questions / TODO
│   ├── hardware/
│   │   ├── device-descriptor.md
│   │   └── product-ids.md
│   └── protocol/
│       ├── control-requests.md
│       ├── audio-streaming.md
│       ├── mixer-routing.md
│       ├── controls.md
│       ├── settings.md
│       └── sequences.md
├── asm/
│   ├── FirefaceUSB.asm           Disassembly of FirefaceUSB.kext (kernel driver)
│   ├── FirefaceUSBSettings.asm   Disassembly of Fireface USB Settings
│   ├── TotalmixFX.asm            Disassembly of TotalMix FX
│   ├── FirefaceUSB/              C rewrites of kext functions (hw*, xRate, user client…)
│   ├── FirefaceUSBSettings/      C rewrites of settings-app functions
│   └── TotalmixFX/               C rewrites of TotalMix functions (CCardIOUC::*)
├── libusb_test/              Userspace libusb-1.0 prototype (see libusb-test.md)
│   ├── rme_uc.c / rme_uc.h   main() + ISO playback
│   ├── usb.c / usb.h         Control-transfer helper + tracing
│   ├── asm.h                 Prototypes of linked asm/ functions
│   ├── Makefile
│   └── Her-Swim__44100-f32le.raw   Test audio (mono, 44.1 kHz, f32le)
├── cap/                      Wireshark captures (see captures.md)
│   └── play/                 Captures for audio payload analysis
└── bin/                      Build output (only .gitignore is tracked)
```

## Ignored paths (`.gitignore`)

| Path | Why |
| ---- | --- |
| `/notes` | Personal notes |
| `/old` | Old material |
| `/cap/play_stop_full.pcapng` | Too large; split into `play_stop_full-SPLIT.*` |
| `bin/*` (via `bin/.gitignore`) | Build artefacts |

## File naming

Files in `asm/*/` are named after the original C++ symbol, including `::`
(e.g. `CCardIOUC::SetGain.c`). Quote or escape them in the shell:

```sh
cat 'asm/TotalmixFX/CCardIOUC::SetGain.c'
```

In Markdown links they work as-is (`::` is valid in a relative URL path).
