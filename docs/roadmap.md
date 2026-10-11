# Roadmap and Open Questions

## Strategy

The target is a **quirk in the Linux `snd-usb-audio` driver**, not a standalone
driver. The kernel already supports a sibling device, the **RME Digiface USB**
(`2a39:3f8c` / `2a39:3fa0`, Linux ≥ 6.13), which uses the same RME USB platform:

* status read with `0xc0 bRequest 0x11` (16 bytes), clock source `7` = internal;
* masked register writes (`wValue` = value, `wIndex` = mask);
* current rate = `104857600000000 / period`, the constant used by `hwSetSampleRate()`;
* audio on interface 0, alt settings 1/2/3 per rate band, async ISO with implicit
  feedback.

Kernel references: `sound/usb/quirks-table.h` (`QUIRK_RME_DIGIFACE`),
`sound/usb/quirks.c`, `sound/usb/mixer_quirks.c` (`snd_rme_digiface_*`).

Known differences so far: the UC uses **float32** samples (Digiface: `S32_LE`) and
has an extra per-frame word on playback (frame counter in the first frame of each
packet).

## Phases

### Phase 0 — Test bench

- [x] Support the new RME vendor ID `0x2a39` in the prototype
      ([`libusb_test/rme_uc.h`](../libusb_test/rme_uc.h)).
- [x] Save the PC-mode descriptor of a recent firmware
      ([`device_descriptor_2a39_3fce_pc_mode`](../device_descriptor_2a39_3fce_pc_mode)).
- [x] Offline pcapng tools: [`tools/usbmon_ctl.py`](../tools/usbmon_ctl.py),
      [`tools/usbmon_iso.py`](../tools/usbmon_iso.py).
- [x] Switch the UC to AP (Mac) mode (double click on the encoder, USB unplugged, display `AP`):
      ID is `2a39:3fcf`, descriptor identical to firmware v118.
- [x] udev rule for non-root access ([`tools/70-rme-fireface-uc.rules`](../tools/70-rme-fireface-uc.rules));
- [ ] `usbmon` + `tshark` for live captures.
- [x] Read the firmware version with the prototype: **138**, control protocol unchanged
      ([libusb-test.md](libusb-test.md#first-run-on-hardware-firmware-138-2a393fcf)).

### Phase 1 — Fix known-wrong code

- [x] `setVolume()`: index `36·out + 18·type + ch`, `wValue = level >> 2`, verified on
      captures ([mixer-routing.md](protocol/mixer-routing.md#verification)).
- [x] Sample-rate docs: second packet is `0x10 (family << 4) 0x0030`; speed multiple
      selected only by the alt setting ([controls.md](protocol/controls.md#set-sample-rate)).
- [x] Prototype: queue several ISO transfers (4 × 1 ms).
- [ ] Prototype: stereo input file.

### Phase 2 — Audio unknowns (needs hardware)

- [x] Sample format: **float32** (verified on `cap/play/play_square_*`).
- [x] Playback word 0: frame counter, first frame of each packet only.
- [x] Playback channel 1 is word 1 of each 19-word frame.
- [x] Playback frame counter: **modulo 8192** (wrapping elsewhere = click every ~1.5 s),
      and **required** (forced to 0 = crackle all along).
- [x] **Implicit feedback** verified: mirroring capture packet sizes removes a −32 ppm
      drift and the remaining crackle — clean playback on outputs 1/2
      ([audio-streaming.md](protocol/audio-streaming.md#synchronisation-implicit-feedback-verified)).
- [x] Capture layout: 4-byte header (timestamp, 1/32 frame units) + **planar**
      channels (verified with the hardware loopback,
      [audio-streaming.md](protocol/audio-streaming.md#packet-layout-header--planar-channels-verified)).
- [x] Channel order: word / block k = output k for k = 1..18 in both directions
      (AN 1–6, phones 7/8, S/PDIF 9/10, ADAT 11–18), verified with the loopback.
- [x] Mixer writes (`0x12`) and loopback (`0x15`) verified on hardware.
- [x] Alt settings 2 and 3: **14 and 12 channels** (ADAT S/MUX), verified at 96 and
      192 kHz ([audio-streaming.md](protocol/audio-streaming.md#transport)).
- [x] Minimal init sequence: **none needed for playback**. After a power cycle, clean
      playback at 44.1 kHz without any `hwInitHardware()` request (`RME_SKIP_INIT=1`).
      Their role (`0x16` × 36, `0x10 0x0800/0xb800`, `0x20`) is still unknown; not
      tested at 96/192 kHz nor for capture quality.

### Phase 3 — Align with the Digiface

- [ ] Register-by-register mapping: 16-byte status of `0x11` vs our
      `GetStatus()` / `hwGetRate()` decoding; control registers 16/18 vs our `0x10` / `0x17`.
- [ ] Does the rate need `0x1b` + `0x10` (as in `hwSetSampleRate()`) or only the
      control register like the Digiface?

### Phase 4 — `snd-usb-audio` quirk

- [ ] Out-of-tree build of `sound/usb/` for the running kernel.
- [ ] `quirks-table.h` entry: iface 0, alt 1/2/3 with 18 / 14 / 12 ch, `FLOAT_LE`,
      `EP 0x01` / `0x82`, implicit feedback.
- [ ] Device-specific copy routines: playback frame counter (required) in the first
      frame of each packet; capture header skipped and planar channels re-interleaved.
- [ ] Rate setting (`0x1b` / `0x10`).
- [ ] Basic mixer controls: clock source, sync status, rates, levels, digital formats
      ([settings.md](protocol/settings.md)).
- [ ] Submit to `alsa-devel`.

### Phase 5 — Mixer and advanced controls

- [ ] Matrix gains (`0x12`), preamp gains (`0x1a`), +48 V / PAD / Hi-Z (`0x17`).
- [ ] Loopback (`0x15`): reimplement `CCardIOUC::SetLoopbackChannel()`
      (`TotalmixFX.asm` line 120194); default or user values at init?
- [ ] Output volume: `0x12` (992–1009) vs `0x1a` (4–21), `CCardIOUC::SetOutputVolume()`
      (line 120111).
- [ ] Level meters: bulk `EP 0x85` (324 B), `CCardIOUC::ReadStatus(LEVELDATARAW*)`.
- [ ] MIDI (interface 2): check whether `snd-usb-audio` already handles it.
- [ ] TotalMix-like userspace UI.

## Other open questions

- [ ] `0xc0 0x19` (`sUserClientReadStatus()`): meaning of the returned word.
- [ ] `0xc0 0x10` read in `hwGetRate()`: meaning of the second register.
- [ ] Role of interfaces 3 (`EP 0x88` / `0x09`) and 4 (`EP 0x8b`).
- [ ] Preamp gain: caller of `hwWriteLedAndPga()` in TotalMix.
- [ ] PAD: condition that makes `WriteDWORD()` send `0x8008` / `0x4004`.
- [ ] Function sending the two `0x1a` sets at plug-in.
