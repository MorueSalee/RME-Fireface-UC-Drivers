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

### The 4-byte header word

Its meaning is not confirmed. The prototype in
[`libusb_test/rme_uc.c`](../../libusb_test/rme_uc.c) (`fill_audio_data()`) treats it as
a **running frame counter** (`unkId`): it starts at `0x16`, is incremented by the
number of frames in each subframe, and wraps after `0xffff`.

## Capture at 44100 Hz

**TODO** — not analysed yet.

## Synchronisation

Host/device synchronisation is very likely based on **implicit feedback**
(USB 2.0 spec, §5.12.4.3 *Implicit Feedback*), using the device → host stream
(audio inputs) as the clock reference.

Relevant functions in `FirefaceUSB.kext` (see [`asm/FirefaceUSB.asm`](../../asm/FirefaceUSB.asm)):

* `de_RME_driver_USBAudioEngine::Timer()`
* `de_RME_driver_USBAudioEngine::inIsochCallback()`
* `de_RME_driver_USBAudioEngine::outIsochCallback()`
* `de_RME_driver_USBAudioEngine::inBulkCallback()`
* `de_RME_driver_USBAudioEngine::CalcTimestamp()`
