#!/bin/sh
# Build snd-usb-audio.ko from kernel/build/sound-usb against the running kernel.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/build/sound-usb

[ -d "$SRC" ] || { echo "run kernel/fetch-sources.sh first" >&2; exit 1; }
[ -f "$HERE/sound-usb/rme_uc.c" ] && cp "$HERE/sound-usb/rme_uc.c" "$HERE/sound-usb/rme_uc.h" "$SRC/"
make -C "/lib/modules/$(uname -r)/build" M="$SRC" modules
modinfo -F vermagic "$SRC/snd-usb-audio.ko"
