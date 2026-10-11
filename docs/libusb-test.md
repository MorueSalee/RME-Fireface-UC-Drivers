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

A dry run that exercises all control requests without sending audio:

```sh
./bin/rme_uc < /dev/null      # init, rate, unmute, EOF, mute
```

## First run on hardware (firmware 138, `2a39:3fcf`)

`rx_data` is printed byte by byte, i.e. little-endian; the 32-bit values are:

| Request | Raw bytes | Value | Decoded |
| ------- | --------- | ----- | ------- |
| `0xc0 0x1c` revision | `03 01 8a 00` | `0x008a0103` | firmware **138** (`>> 16`) |
| `0xc0 0x11` status | `00 ff f0 01` | `0x01f0ff00` | clock source 7 (internal), base 44.1 kHz, single speed, no input locked |
| `0xc0 0x10` | `10 4a 00 00` | `0x00004a10` | unknown |
| `0xc0 0x19` | `04 00 00 00` | `0x00000004` | unknown |

`hwGetRate()` returns 44100 Hz and the device accepts `hwSetSampleRate(44100)`
and mute / unmute. The whole control protocol of firmware v118 works unchanged.

## Playback quality on hardware

| Version | Heard on outputs 1/2 |
| ------- | -------------------- |
| 1 transfer in flight, counter wrapping at `0xffff` | music OK, light clicks + occasional high crackle, always at the same places |
| 4 transfers queued | clicks lighter |
| + counter modulo 8192 | clicks gone, crackle remains at random places |
| + implicit feedback | **clean** |

At the end of a run the prototype prints the feedback statistics and the
`in − out` frame balance; every 10 s it prints the current balance (constant when
synchronised).

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

* **Playback** (`EP 0x01`): 4 transfers of **8 ISO packets** (1 ms each) stay queued;
  `cb_xfr()` refills and resubmits each one as it completes (`fill_iso_out()`).
* **Capture** (`EP 0x82`): 4 transfers of 8 × 508 bytes stay queued (`start_iso_in()`,
  `cb_xfr_in()`); the samples are not used, only the **frame count of each packet**
  (`(length − 4) / 72`), pushed into a FIFO.
* **Implicit feedback**: each playback packet takes the size of the oldest unused
  capture packet (`next_out_packet_frames()`); until the first capture packets arrive
  it falls back to a 5 / 6 alternation.
* Each frame = 19 × 32-bit words: word 0 is the frame counter (modulo 8192) in the
  first frame of each packet and 0 in the others, words 1–18 are the channels.
* The mono input sample is copied to channels 1 **and** 2.

See [audio-streaming.md](protocol/audio-streaming.md) for the format.

## Test switches

| Variable | Effect |
| -------- | ------ |
| `RME_NO_COUNTER=1` | Send 0 instead of the frame counter (shows that the device needs it). |
| `RME_LOOPBACK=1` | Enable the device loopback on outputs 1/2 (`0x40 0x15`) during the run, disable it at the end. |
| `RME_DUMP_IN=<file>` | Record 4000 raw capture packets (after 2 s) as `uint16 length` + bytes; read them with [`tools/capture_dump.py`](../tools/capture_dump.py). |

## Tracing

`usb.h` defines `TRACE_USB`: every `send_ctrl_setup()` then prints one line with
`bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`, the calling function and
the received data, which makes it easy to compare with Wireshark captures. Comment
out the `#define` to disable it.

`do_send_ctrl_setup()` returns `0` on success (not the byte count) to mimic the
reverse-engineered driver behaviour.

## Known limitations

* Fixed 44100 Hz (alt setting 1), mono input file, 2 channels used out of 18.
* Capture samples are read but discarded.
* Latency fixed by `ISO_OUT_XFERS` (4 ms), no buffering against stdin stalls.
* No MIDI, no level meters, no settings/mixer control.
