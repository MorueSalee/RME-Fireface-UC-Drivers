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
