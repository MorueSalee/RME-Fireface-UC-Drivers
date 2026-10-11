# Audio Streaming

## Transport

* Audio uses **asynchronous isochronous** transfers on **interface 0**,
  `EP 0x01 OUT` (playback) and `EP 0x82 IN` (capture).
* Audio is always **32-bit float, little endian** (`f32le`) on playback.
* The alternate setting must match the sample-rate range (endpoint bandwidth,
  `wMaxPacketSize`):

| bAlternateSetting | Sample-rate range       | OUT / IN wMaxPacketSize |
| ----------------- | ----------------------- | ----------------------- |
| 0                 | idle                    | 0 / 0                   |
| **1**             | `[32000 Hz; 64000 Hz[`  | 532 / 508               |
| **2**             | `[64000 Hz; 128000 Hz[` | 780 / 732               |
| **3**             | `[128000 Hz; 192000 Hz]`| 2×652 / 2×604           |

> **Changing the alternate setting is mandatory when changing rate group.**
> Otherwise the rate falls back to the multiple of the current group: e.g. on alt 1 at
> 44100 Hz, requesting 96000 Hz without switching to alt 2 gives 48000 Hz.
> See [controls.md – Set Sample Rate](controls.md#set-sample-rate).

## Playback at 44100 Hz (observed)

One ISO transfer every millisecond (1000 transfers/s). Each transfer carries
**8 ISO packets ("subframes")** whose sizes alternate between 456 bytes (`0x01c8`)
and 380 bytes (`0x017c`).

ISO descriptor, as shown by Wireshark:

```wireshark
0000   ee ff ff ff 00 00 00 00   c8 01 00 00 00 00 00 00  ................
0010   ee ff ff ff c8 01 00 00   7c 01 00 00 00 00 00 00  ........|.......
0020   ee ff ff ff 44 03 00 00   c8 01 00 00 00 00 00 00  ....D...........
0030   ee ff ff ff 0c 05 00 00   7c 01 00 00 00 00 00 00  ........|.......
0040   ee ff ff ff 88 06 00 00   c8 01 00 00 00 00 00 00  ................
0050   ee ff ff ff 50 08 00 00   7c 01 00 00 00 00 00 00  ....P...|.......
0060   ee ff ff ff cc 09 00 00   c8 01 00 00 00 00 00 00  ................
0070   ee ff ff ff 94 0b 00 00   7c 01 00 00 00 00 00 00  ........|.......
```

Each subframe contains 5 or 6 **blocks of 19 × 4 bytes**:

* `5 * (19 * 4) = 380` bytes
* `6 * (19 * 4) = 456` bytes

9 transfers out of 10 carry 3344 bytes (`3344 / 76 = 44` blocks), 1 out of 10
carries 3420 bytes (`3420 / 76 = 45` blocks). Over 10 ms: `9 * 44 + 45 = 441`
blocks, i.e. **44100 blocks per second**.

A block is therefore **one frame**: one 32-bit float sample per channel
(18 channels) plus a 4-byte header word:

```wireshark
0080   fe 05 00 00 00 fe 7f 3f   00 00 00 00 00 00 00 00  .......?........
      |__????_____|__ch_1_____| |__ch_2_____|__ch_3_____|

0090   00 00 00 00 00 00 00 00   00 00 00 00 00 00 00 00  ................
      |__ch_4_____|__ch_5_____| |__ch_6_____|__ch_7_____|

00a0   00 00 00 00 00 00 00 00   00 00 00 00 00 00 00 00  ................
      |__ch_8_____|__ch_9_____| |__ch_10____|__ch_11____|

00b0   00 00 00 00 00 00 00 00   00 00 00 00 00 00 00 00  ................
      |__ch_12____|__ch_13____| |__ch_14____|__ch_15____|

00c0   00 00 00 00 00 00 00 00   00 00 00 00              ............
      |__ch_16____|__ch_17____| |__ch_18____|
```

### Sample format: float32 (verified)

In `cap/play/play_square_22050Hz_pb_1.pcapng` (22050 Hz square on playback 1 at
44.1 kHz, so the sign flips every sample) channel 1 alternates `0x3f7ffe00` /
`0xbf7ffe00`: only the sign bit changes, i.e. **IEEE-754 float ±0.99997**. A signed
integer format would give `0xc0800200` for the negative value.

> This differs from the sibling RME Digiface USB, which the Linux kernel drives as
> `S32_LE` (24 bits). `snd-usb-audio` also handles `SNDRV_PCM_FMTBIT_FLOAT_LE`.

### Word 0 of each frame: per-packet frame counter (verified)

Word 0 is **non-zero only in the first frame of each ISO packet**; in the other
frames it is `0`. Its value is a running frame counter, incremented by the number
of frames of the previous packet (`play_music_pb_1-2_out_1-2.pcapng`):

```
len 456 (6 frames)  word0 = 0x1620
len 380 (5 frames)  word0 = 0x1626   (+6)
len 456 (6 frames)  word0 = 0x162b   (+5)
len 380 (5 frames)  word0 = 0x1631   (+6)
```

**It counts modulo 8192** (`0x200f + 5 → 0x0014`, `0x2018 + 6 → 0x001e` in the same
capture), which suggests a write position in an 8192-frame ring buffer on the device.

Verified on hardware (firmware 138): with a counter wrapping at `0xffff` instead,
the position jumps every 65536 frames and an audible click occurs every ~1.49 s,
always at the same place in the music. Wrapping modulo 8192 removes it. The
prototype ([`fill_iso_out()`](../../libusb_test/rme_uc.c)) now does so.

**The counter is required**: with `RME_NO_COUNTER=1` (word 0 forced to 0, everything
else unchanged, no drift, no USB error) the prototype output crackles from start to
end. A `snd-usb-audio` quirk must therefore write it in the first frame of every
playback packet; the stream cannot simply be declared as 19 channels with a silent
one.

The official driver also makes occasional deliberate jumps (`0x175 + 2 → 0x459`),
probably re-synchronisations; not needed so far with implicit feedback.

## Capture (device → host, `EP 0x82`)

Observed at 44.1 kHz in `play_music_pb_1-2_out_1-2.pcapng`:

* The host submits 8 packets of the full `wMaxPacketSize` (508 bytes on alt 1);
  the device returns packets of **436 / 364 bytes**, alternating like playback.
* `436 = 4 + 6 × 72`, `364 = 4 + 5 × 72`: 6 or 5 frames of **18 channels × 4 bytes**
  (no per-frame extra word, unlike playback) plus **one 4-byte word per packet**.
* Samples are **float32** too (noise floor values such as `0x35600000` ≈ 8e-7).
* The position and meaning of the extra word are **not determined yet**: some
  packets carry values that look like a timestamp (`0x21aac266`, `0x316ac7e6`,
  `0x414acd66`, step `0x0fc00580`) and others look like samples. Needs a known
  signal on an input (hardware loopback test).
* The first packets after start are 4 bytes long (word only, no frames).

The size of each received packet (5 or 6 frames) is what the host must mirror on
playback → **implicit feedback**.

## Analysis tools

[`tools/usbmon_ctl.py`](../../tools/usbmon_ctl.py) lists control SETUP packets and
[`tools/usbmon_iso.py`](../../tools/usbmon_iso.py) dumps ISO packets as 19-word rows,
straight from the `.pcapng` files (no Wireshark needed):

```sh
python3 tools/usbmon_ctl.py cap/set_sample_rate_96000Hz.pcapng 0x1b,0x10
python3 tools/usbmon_iso.py cap/play/play_square_22050Hz_pb_1.pcapng 0x01 S 5
```

## Synchronisation: implicit feedback (verified)

`EP 0x01` has no feedback endpoint (*Synch Type None*): synchronisation uses
**implicit feedback** (USB 2.0 spec, §5.12.4.3), the capture stream being the clock
reference. **Each playback packet must carry as many frames as a capture packet
received from the device.**

Measured with the prototype on firmware 138, 44.1 kHz:

| Playback packet sizes | `frames in − frames out`, every 10 s | Result |
| --------------------- | ------------------------------------ | ------ |
| Fixed 44 / 45 pattern (USB bus clock) | −201, −215, −229, … (−14 frames / 10 s) | device clock **−32 ppm**: its buffer overflows, audible crackle at random times |
| Mirrored from capture packets | −176, −176, −176, … (constant = 4 queued transfers) | no drift, **clean sound** |

The capture stream must therefore always run during playback, even when nothing is
recorded. `snd-usb-audio` supports this mode (`implicit_fb = 1`, as for the
Digiface USB).

Relevant functions in `FirefaceUSB.kext` (see [`asm/FirefaceUSB.asm`](../../asm/FirefaceUSB.asm)):

* `de_RME_driver_USBAudioEngine::Timer()`
* `de_RME_driver_USBAudioEngine::inIsochCallback()`
* `de_RME_driver_USBAudioEngine::outIsochCallback()`
* `de_RME_driver_USBAudioEngine::inBulkCallback()`
* `de_RME_driver_USBAudioEngine::CalcTimestamp()`
