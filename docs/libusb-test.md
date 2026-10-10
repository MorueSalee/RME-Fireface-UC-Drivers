# libusb Prototype (`libusb_test/`)

A userspace test program that drives the Fireface UC through **libusb-1.0**, using
the functions reimplemented in [`asm/`](../asm). Its goal is to validate the protocol
before writing a real (ALSA) driver. It currently plays audio on outputs only.

## Files

| File | Content |
| ---- | ------- |
| [`rme_uc.c`](../libusb_test/rme_uc.c) | `main()`, ISO playback loop (`send_iso_out()`, `fill_audio_data()`), SIGINT handler. A commented-out `send_iso_in()` is a first attempt at capture. |
| [`rme_uc.h`](../libusb_test/rme_uc.h) | Vendor/product IDs (`0x0424` / `0x3fc6`). |
| [`usb.c`](../libusb_test/usb.c), [`usb.h`](../libusb_test/usb.h) | `ctrl_setup` struct and `send_ctrl_setup()` wrapper around `libusb_control_transfer()`. |
| [`asm.h`](../libusb_test/asm.h) | Prototypes of the reimplemented functions that are linked in. |
| [`Makefile`](../libusb_test/Makefile) | Builds `rme_uc.c`, `usb.c` and 10 files from `asm/` into `../bin/rme_uc`. |
| `Her-Swim__44100-f32le.raw` | Test audio: raw PCM, 44100 Hz, 32-bit float LE, **mono**. |

## Build

Requirements: `gcc`, `make`, libusb-1.0 development headers
(`libusb-1.0-0-dev` on Debian/Ubuntu, `libusb` on Arch).

```sh
make            # from the repository root; output: bin/rme_uc
```

`bin/` is kept in git only through its `.gitignore`; the binary is never committed.

## Run

The device must be in **AP (Mac) mode** (`lsusb` shows `0424:3fc6`) and the program
needs write access to the USB device (root or a udev rule).

```sh
sudo ./bin/rme_uc < libusb_test/Her-Swim__44100-f32le.raw
```

Audio is read from **stdin**. Stop with `Ctrl-C` (SIGINT); the program then mutes the
outputs and closes the device. Without a device it prints
`Running Offline` and continues: control requests are only traced (handy to check
the computed `wValue`/`wIndex`), but ISO streaming cannot work.

## What `main()` does

1. `libusb_open_device_with_vid_pid(0x0424, 0x3fc6)`, claim interface 0, select
   **alternate setting 1** (32–64 kHz group).
2. `hwGetRevision()` → prints the firmware version.
3. `hwInitHardware()`.
4. `hwGetRate()` → prints the current sample rate.
5. `sUserClientReadStatus()` → prints the status word.
6. `goto SEND_AUDIO` — **skips** a block that replays the plug-in init sequence seen
   in Wireshark (`hwSetSampleRate(44100)` and the two sets of `0x1a` gain packets,
   see [sequences.md](protocol/sequences.md)).
7. `SEND_AUDIO:` `hwSetSampleRate(44100)`, checks it with `hwGetRate()`, then
   `hwMuteChannels(unmute)`.
8. Loop until EOF or SIGINT: `send_iso_out(EP 0x01)` then
   `libusb_handle_events_timeout_completed()` with a timeout computed to keep a
   1 ms period.
9. `hwMuteChannels(mute)`, close, exit.

## Building the ISO transfers

`send_iso_out()` allocates one transfer of **8 ISO packets**:

* 9 transfers out of 10: 3344 bytes (44 frames); every 10th: 3420 bytes (45 frames);
* packets alternate 456 B (`0x01c8`, 6 frames) and 380 B (`0x017c`, 5 frames);
* each frame = 19 × 32-bit words: word 0 is a counter (`unkId`, starts at `0x16`,
  +5/+6 per packet, wraps after `0xffff`), words 1–18 are the channels;
* the mono input sample is copied to channels 1 **and** 2.

See [audio-streaming.md](protocol/audio-streaming.md) for the observed format.

## Tracing

`usb.h` defines `TRACE_USB`: every `send_ctrl_setup()` then prints one line with
`bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`, the calling function and
the received data, which makes it easy to compare with Wireshark captures. Comment
out the `#define` to disable it.

`do_send_ctrl_setup()` returns `0` on success (not the byte count) to mimic the
reverse-engineered driver behaviour.

## Known limitations

* Only **one** ISO transfer is in flight at a time → gaps in the bus schedule and
  jitter (see the NOTE in `send_iso_out()`); several transfers should be queued.
* No capture (IN) stream, no feedback/synchronisation handling.
* Fixed 44100 Hz, 2 channels used out of 18.
* No MIDI, no level meters, no settings/mixer control.
