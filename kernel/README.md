# snd-usb-audio support for the RME Fireface UC

Work in progress: an out-of-tree build of `sound/usb` of the running kernel with
support for the Fireface UC in AP (Mac) mode. Protocol: `docs/protocol/`.

## Layout

| Path | Content |
| ---- | ------- |
| `sound-usb/rme_uc.[ch]` | New driver file: stream format helpers (pure functions). |
| `patches/` | Changes to existing `sound/usb` files. |
| `tests/` | Userspace unit tests of `rme_uc.c` (`make -C kernel/tests check`). |
| `build/` | Generated: upstream sources + build output (git-ignored). |

## Build

```sh
kernel/fetch-sources.sh     # sound/usb of v7.2.9-zen1 + patches/
kernel/build.sh             # -> kernel/build/sound-usb/snd-usb-audio.ko
```

After editing files in `kernel/build/sound-usb/`, run `kernel/export-patch.sh`.
Style check (run from `kernel/`, otherwise checkpatch treats the patch as a
tracked source file of this repository):

```sh
cd kernel && build/zen/scripts/checkpatch.pl --no-tree --no-signoff patches/*.patch
build/zen/scripts/checkpatch.pl --no-tree -f sound-usb/rme_uc.c sound-usb/rme_uc.h
```

## Load / unload (root)

Loading replaces `snd-usb-audio` for **every** USB audio device until reboot or
`unload.sh`. Stop PipeWire first so that the module can be removed:

```sh
systemctl --user stop pipewire.socket pipewire-pulse.socket wireplumber pipewire pipewire-pulse
sudo kernel/load.sh        # or: sudo kernel/unload.sh
systemctl --user start pipewire.socket pipewire-pulse.socket wireplumber
```

## Test procedure (hardware)

With the Fireface UC in AP mode, PipeWire stopped and `sudo kernel/load.sh`:

```sh
UC=$(awk '/Fireface UC/ {print $1; exit}' /proc/asound/cards)
cat /proc/asound/card$UC/stream0          # 3 playback + 3 capture formats, FLOAT_LE 18/14/12 ch

# listening test, outputs 1/2
python3 tools/rme_ctl.py mono18 18 libusb_test/Her-Swim__44100-f32le.raw /tmp/music18.raw
aplay -D hw:$UC -t raw -f FLOAT_LE -r 44100 -c 18 -d 60 /tmp/music18.raw

# channel identification through the device loopback (overwrites the device mix)
python3 tools/rme_ctl.py mix-diagonal
for spec in "44100 18" "96000 14" "192000 12"; do
	set -- $spec
	python3 tools/rme_ctl.py gen $2 $(($1 * 6)) /tmp/id.raw
	python3 tools/rme_ctl.py loopback 18 on
	aplay -q -D hw:$UC -t raw -f FLOAT_LE -r $1 -c $2 /tmp/id.raw &
	sleep 1; arecord -q -D hw:$UC -t raw -f FLOAT_LE -r $1 -c $2 -d 3 /tmp/cap.raw; wait
	python3 tools/rme_ctl.py loopback 18 off
	python3 tools/rme_ctl.py check $2 /tmp/cap.raw   # expect 1:1.00 2:2.00 ... n:n.00
done
```

### Results (2026-10-11, firmware 138, `2a39:3fcf`, kernel 7.2.9-zen1)

| Test | Result |
| ---- | ------ |
| Enumeration | card "Fireface UC": 3 playback + 3 capture formats, FLOAT_LE, 18 / 14 / 12 channels, implicit feedback from `0x82` |
| Listening, 44.1 kHz, outputs 1/2 | **clean**, no crackle (90 s) |
| Hardware mute left on (`rme_ctl.py mute on`) | silent before the unmute fix, music heard after it |
| Loopback channel identification | identity order, unity gain, at 44.1 kHz (18 ch), 96 kHz (14 ch) and 192 kHz (12 ch) |
| Capture at 96 kHz while playing at 44.1 kHz | refused (`arecord`: channel count not available), playback unaffected |
| Other USB audio devices with the test module | generic USB Audio playback and RODE NT-USB capture OK |
| `unload.sh` | stock module back, same cards, PipeWire OK |
