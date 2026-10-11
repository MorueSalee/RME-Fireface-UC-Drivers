#!/bin/sh
# Replace the running snd-usb-audio by the one built in kernel/build/sound-usb.
# Run as root, after stopping PipeWire (see kernel/README.md). Undo: unload.sh.
set -eu

KO=$(cd "$(dirname "$0")" && pwd)/build/sound-usb/snd-usb-audio.ko

[ -f "$KO" ] || { echo "build first: kernel/build.sh" >&2; exit 1; }
modinfo -F vermagic "$KO" | grep -q "^$(uname -r) " ||
	{ echo "vermagic does not match $(uname -r)" >&2; exit 1; }

modprobe -r snd_usb_audio
for dep in $(modinfo -F depends "$KO" | tr ',' ' '); do
	modprobe "$dep"
done
insmod "$KO"
echo "loaded $KO"
grep -i "usb-audio" /proc/asound/cards || true
