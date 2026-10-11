#!/bin/sh
# Go back to the distribution snd-usb-audio. Run as root.
set -eu

modprobe -r snd_usb_audio || rmmod snd_usb_audio
modprobe snd_usb_audio
echo "stock snd-usb-audio loaded"
grep -i "usb-audio" /proc/asound/cards || true
