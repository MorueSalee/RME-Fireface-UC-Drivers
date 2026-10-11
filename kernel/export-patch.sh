#!/bin/sh
# Export the changes made to the existing sound/usb files as one patch.
# rme_uc.[ch] are kept in kernel/sound-usb/ and are not part of the patch.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$HERE/patches"
git -C "$HERE/build/sound-usb" diff \
	> "$HERE/patches/0001-ALSA-usb-audio-add-RME-Fireface-UC.patch"
git -C "$HERE/build/sound-usb" diff --stat
