# Sequences

Request sequences observed in Wireshark (see [captures.md](../captures.md)) and in
the disassembled driver.

## Initialisation (on USB plug)

As performed by the macOS driver + TotalMix (capture `cap/pluginit_full*.pcapng`):

1. **Get firmware revision** — [`hwGetRevision()`](../../asm/FirefaceUSB/hwGetRevision.c),
   `0xc0 0x1c`.
2. **Hardware init** — [`hwInitHardware()`](../../asm/FirefaceUSB/hwInitHardware.c):
   * 36 × `0x40 0x16 wValue=0x0001`, for `wIndex = n` and `wIndex = n + 0x20`,
     `n = 0..17` (meaning unknown — one per channel?);
   * `0x40 0x10 0x0800 0xb800` (unknown);
   * `0x40 0x20 0x0001 0x0000` (unknown).
3. **Set sample rate** — the driver always starts at 44100 Hz:

   ```
   0x00007ec3  mov  rdi, qword [ds:r15+0x120]
   0x00007eca  movzx edx, word [ds:r15+0x128]
   0x00007ed2  mov  esi, 0xac44        ; 44100
   0x00007ed7  call __Z15hwSetSampleRateP11IOUSBDevicejt
   ```

4. **Gains / output levels** (`0x40 0x1a`, [`hwWriteLedAndPga()`](../../asm/FirefaceUSB/hwWriteLedAndPga.c)):
   * A first set of **22 packets** (from packet #123 in the capture), `wIndex`
     ascending `0..21`, fixed values: `0x0a` (mic 1–2), `0x00` (line 3–4),
     `0x2e` (outputs 4–21).
   * A second set of **20 packets** (from packet #186), no particular order but
     paired by two (stereo pairs); indexes 2 and 3 are missing.
   * **Conclusion**: the first set is always identical — a reset to defaults.
     The second applies the user values from TotalMix and is sent only for values
     that differ from the defaults.
5. **Faders**, then **loopback** state of every output (`0x40 0x15`).

The prototype replays steps 1–4 by hand in `main()` of
[`libusb_test/rme_uc.c`](../../libusb_test/rme_uc.c) (currently skipped by
`goto SEND_AUDIO`).

**TODO**: find the function that sends the two `0x1a` sets.

## Playback

1. Select alternate setting on interface 0 matching the rate (see
   [audio-streaming.md](audio-streaming.md)).
2. `hwSetSampleRate()` then `hwGetRate()` to check.
3. `hwMuteChannels(unmute)` — `0x40 0x14 0xffff 0xc000`.
4. Stream ISO transfers on `EP 0x01` (and `EP 0x82` for capture).
5. Every **2 ms** the driver polls the rate with `hwGetRate()`.
6. On stop: `hwMuteChannels(mute)` — `0x40 0x13 0xffff 0xc000`.

**TODO**: complete with capture start/stop (`cap/start_stop_*.pcapng`).
