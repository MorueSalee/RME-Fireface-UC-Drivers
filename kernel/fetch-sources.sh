#!/bin/sh
# Fetch sound/usb (and scripts/ for checkpatch) of the running zen kernel into
# kernel/build/, as a git repository so that our changes can be exported as a
# patch, then apply kernel/patches/*.patch if any.
set -eu

TAG=${TAG:-v7.2.9-zen1}
HERE=$(cd "$(dirname "$0")" && pwd)
ZEN=$HERE/build/zen
SRC=$HERE/build/sound-usb

rm -rf "$ZEN" "$SRC"
mkdir -p "$HERE/build"
git clone --quiet --depth 1 --branch "$TAG" --filter=blob:none --sparse \
	https://github.com/zen-kernel/zen-kernel.git "$ZEN"
git -C "$ZEN" sparse-checkout set sound/usb scripts

cp -r "$ZEN/sound/usb" "$SRC"
git -C "$SRC" init --quiet
printf 'rme_uc.c\nrme_uc.h\n*.o\n*.ko\n*.mod*\n.*.cmd\nmodules.order\nModule.symvers\n' \
	> "$SRC/.git/info/exclude"
git -C "$SRC" add -A
git -C "$SRC" -c user.name=fetch -c user.email=fetch@localhost \
	commit --quiet -m "upstream sound/usb $TAG"

for p in "$HERE"/patches/*.patch; do
	[ -e "$p" ] || continue
	git -C "$SRC" apply "$p"
	echo "applied $(basename "$p")"
done
