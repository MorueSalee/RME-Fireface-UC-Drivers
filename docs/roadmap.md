# Roadmap and Open Questions

## Status

* Control protocol: mostly understood (rate, settings, gains, phantom/PAD/Hi-Z,
  mixer, mute, firmware) — see [protocol/](protocol/control-requests.md).
* Audio: playback format understood at 44.1 kHz; a libusb prototype plays a mono
  file on outputs 1-2 ([libusb-test.md](libusb-test.md)).
* **No working driver yet.**

## Open questions

### Control requests

- [ ] `0x40 0x16` × 36 at init (`hwInitHardware()`): meaning of `wIndex 0..17` and
      `0x20..0x31`.
- [ ] `0x40 0x10 0x0800 0xb800` and `0x40 0x20 0x0001 0x0000` at init.
- [ ] `0xc0 0x19` (`sUserClientReadStatus()`): meaning of the returned word.
- [ ] `0xc0 0x10` read in `hwGetRate()`: full meaning of the second register.
- [ ] Confirm `0x17` / `0x10` writes are *masked* writes (`wIndex` = mask).

### Controls

- [ ] Loopback (`0x15`): reimplement `CCardIOUC::SetLoopbackChannel()`
      (`TotalmixFX.asm` line 120194); does init send default or user values?
- [ ] Output volume: who calls `0x12` (index 992–1009) vs `0x1a` (index 4–21), and
      why TotalMix alternates them (`CCardIOUC::SetOutputVolume()`, line 120111).
- [ ] Preamp gain: find the caller of `hwWriteLedAndPga()` in TotalMix.
- [ ] PAD: condition that makes `WriteDWORD()` send `0x8008` / `0x4004`.
- [ ] Function sending the two `0x1a` sets at plug-in.
- [ ] **Fix `setVolume()`** in `asm/TotalmixFX/CCardIOUC::SetGain.c`: index formula
      should be `36·out + 18·type + ch` and `wValue = level >> 2`
      (see [mixer-routing.md](protocol/mixer-routing.md#index-encoding-on-the-wire)).

### Status / metering

- [ ] Get current clock: finish decoding the `0x11` status register.
- [ ] VU meters: decode bulk `EP 0x85` payload (324 B; `CCardIOUC::ReadStatus(LEVELDATARAW*)`).
- [ ] Role of interfaces 3 (`EP 0x88`/`0x09`) and 4 (`EP 0x8b`).

### Audio

- [ ] Capture (`EP 0x82`) format.
- [ ] Meaning of the 4-byte frame header (counter?).
- [ ] Frame layout at other rates / alternate settings 2 and 3.
- [ ] Synchronisation (implicit feedback) — study `de_RME_driver_USBAudioEngine::*`.
- [ ] Channel order inside a frame (use `cap/play/play_square_*`).

### Prototype

- [ ] Queue several ISO transfers to avoid scheduling gaps.
- [ ] Stereo / multichannel input, capture stream.
- [ ] Replay full init (currently skipped by `goto SEND_AUDIO`).

## Towards a Linux driver

1. Stable userspace prototype (playback + capture, all rates).
2. ALSA kernel driver: likely as a quirk/extension of `snd-usb-audio` (vendor-specific
   ISO interface, implicit feedback) or a dedicated module.
3. Mixer controls exposed as ALSA controls (settings, gains, phantom…).
4. Generic DSP control UI, similar to TotalMix.
5. Port to other RME USB devices sharing the protocol
   (see [product-ids.md](hardware/product-ids.md)).
