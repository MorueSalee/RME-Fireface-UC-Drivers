# RME Fireface UC support in snd-usb-audio — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Linux `snd-usb-audio` driver expose the RME Fireface UC (AP mode) as a standard ALSA card with 18 / 14 / 12 channels of playback and capture at 32–192 kHz.

**Architecture:** A quirk-table entry declares the vendor-specific interface 0 (one alternate setting per rate band, implicit feedback from `EP 0x82`). Two small pure functions convert between ALSA's interleaved buffer and the device format (playback: a frame-counter word before each frame; capture: 4-byte header + planar channels); they live in a new `rme_uc.c`, are unit-tested in userspace, and are called from three narrow hooks in `pcm.c` / `endpoint.c`. The sample rate is set from `snd_usb_set_format_quirk()` with the two vendor requests already reverse-engineered (`0x1b`, `0x10`). Development happens out of tree against the running kernel's `sound/usb/`; the result is kept in this repository as new files + one patch.

**Tech Stack:** C (Linux kernel, kernel coding style), Kbuild out-of-tree module build, userspace C unit tests (gcc + make), Python 3 (ctypes/libusb) test tools, ALSA utils (`aplay`, `arecord`).

**Spec:** the verified protocol in [`docs/protocol/audio-streaming.md`](../../protocol/audio-streaming.md) (format, counter, capture layout, channel counts, implicit feedback) and [`docs/protocol/controls.md#set-sample-rate`](../../protocol/controls.md#set-sample-rate) (rate requests). Upstream reference: the RME Digiface USB support in the same files (`QUIRK_RME_DIGIFACE`, `rme_digiface_set_format_quirk()`).

## Global Constraints

- Target kernel: **`7.2.9-zen1-1-zen`** (running kernel; headers in `/lib/modules/$(uname -r)/build`), sources from tag **`v7.2.9-zen1`** of `https://github.com/zen-kernel/zen-kernel.git`.
- Devices: **`2a39:3fcf`** (firmware 138, AP mode) and **`0424:3fc6`** (firmware ≤ v118, AP mode). PC mode (`2a39:3fce`) is not supported.
- Samples: **`SNDRV_PCM_FMTBIT_FLOAT_LE`**, 32 bits. Channels: **18** (alt 1, 32–48 kHz), **14** (alt 2, 64–96 kHz), **12** (alt 3, 128–192 kHz).
- `maxpacksize` must be set explicitly: playback **532 / 780 / 1304**, capture **508 / 732 / 1208** (the fixed-stream quirk reads `wMaxPacketSize` without the high-bandwidth multiplier, which would give 2700 / 2652 for alt 3).
- Playback frame counter: **modulo 8192** (`0x1fff`), in word 0 of the **first** frame of each ISO packet, **0** in word 0 of the other frames; must keep counting across silent packets.
- Capture packet: **4-byte header**, then **planar** channel blocks.
- New quirk flag: **`QUIRK_TYPE_RME_UC = 31`** (last free bit of the `u32` quirk flags).
- License of new kernel files: `GPL-2.0-or-later` (same as `sound/usb`). Kernel coding style (tabs, 80 columns where reasonable); `scripts/checkpatch.pl` clean.
- Loading the test module replaces `snd-usb-audio` for **all** USB audio devices (on this machine also a generic "USB Audio" interface, a CME Xkey37 and a RODE NT-USB); every load must be reversible with `kernel/unload.sh`. Root commands are run by the user (`! sudo …`).

## Review Focus

1. **Stream start**: the first URBs are *silent* URBs built by `prepare_silent_urb()`; without the counter in them the device crackles or misplaces the first frames — expect a clean start (pinned in Task 2 `test_pack_silent`, heard in Task 5).
2. **Ring-buffer wrap inside a packet**: ALSA's buffer end can fall in the middle of a packet in both directions — expect contiguous audio (pinned in Task 2 `test_pack_wrap` / `test_unpack_wrap`).
3. **Header-only or short capture packets** (4-byte packets at stream start, or `actual_length` < 4 on errors) — expect 0 frames, no crash, no pointer advance (pinned in Task 2 `test_unpack_short`).
4. **Opening playback and capture at different rates / rate bands** — both share interface 0: expect the second open to be refused or to follow the first, never a wrong rate (checked in Task 5 step "rate conflict").
5. **Other USB audio devices while the test module is loaded** — expect them to keep working with the rebuilt module and to come back after `unload.sh` (checked in Task 1 and Task 5).

---

## File Structure

```
kernel/
├── README.md                     how to fetch, build, test, load, unload; test procedure
├── fetch-sources.sh              sparse-clone sound/usb (+ scripts/) of v7.2.9-zen1 into kernel/build/, apply patches/
├── build.sh                      copy sound-usb/rme_uc.[ch] into the tree, build the module
├── export-patch.sh               regenerate patches/ from the modified tree
├── load.sh / unload.sh           swap the running snd-usb-audio (root)
├── sound-usb/
│   ├── rme_uc.h                  device format helpers: API + constants
│   └── rme_uc.c                  rate registers, playback packing, capture unpacking (pure functions)
├── patches/
│   └── 0001-ALSA-usb-audio-add-RME-Fireface-UC.patch   changes to existing sound/usb files
└── tests/
    ├── Makefile                  `make check`
    ├── test_rme_uc.c             userspace unit tests of rme_uc.c
    └── shim/linux/*.h, shim/asm/byteorder.h   minimal kernel headers for userspace
tools/
└── rme_ctl.py                    control requests while the kernel driver is bound (loopback, mix), test signal generation and checking
```

`kernel/build/` is generated and git-ignored.

---

### Task 1: Out-of-tree build and swap of an unmodified snd-usb-audio

**Files:**
- Create: `kernel/fetch-sources.sh`, `kernel/build.sh`, `kernel/export-patch.sh`, `kernel/load.sh`, `kernel/unload.sh`, `kernel/README.md`
- Modify: `.gitignore`

**Interfaces:**
- Produces: `kernel/build/sound-usb/` (git repo of the upstream `sound/usb`, used by later tasks), `kernel/build/zen/scripts/checkpatch.pl`, `kernel/build/sound-usb/snd-usb-audio.ko`.

- [ ] **Step 1: Ignore the build directory**

Append to `.gitignore`:

```
/kernel/build/
```

- [ ] **Step 2: Write `kernel/fetch-sources.sh`**

```sh
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
```

- [ ] **Step 3: Write `kernel/build.sh`**

```sh
#!/bin/sh
# Build snd-usb-audio.ko from kernel/build/sound-usb against the running kernel.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/build/sound-usb

[ -d "$SRC" ] || { echo "run kernel/fetch-sources.sh first" >&2; exit 1; }
[ -f "$HERE/sound-usb/rme_uc.c" ] && cp "$HERE/sound-usb/rme_uc.c" "$HERE/sound-usb/rme_uc.h" "$SRC/"
make -C "/lib/modules/$(uname -r)/build" M="$SRC" modules
modinfo -F vermagic "$SRC/snd-usb-audio.ko"
```

- [ ] **Step 4: Write `kernel/export-patch.sh`**

```sh
#!/bin/sh
# Export the changes made to the existing sound/usb files as one patch.
# rme_uc.[ch] are kept in kernel/sound-usb/ and are not part of the patch.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$HERE/patches"
git -C "$HERE/build/sound-usb" diff \
	> "$HERE/patches/0001-ALSA-usb-audio-add-RME-Fireface-UC.patch"
git -C "$HERE/build/sound-usb" diff --stat
```

- [ ] **Step 5: Write `kernel/load.sh` and `kernel/unload.sh`**

`kernel/load.sh`:

```sh
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
```

`kernel/unload.sh`:

```sh
#!/bin/sh
# Go back to the distribution snd-usb-audio. Run as root.
set -eu

modprobe -r snd_usb_audio || rmmod snd_usb_audio
modprobe snd_usb_audio
echo "stock snd-usb-audio loaded"
grep -i "usb-audio" /proc/asound/cards || true
```

Run: `chmod +x kernel/*.sh`

- [ ] **Step 6: Fetch and build the unmodified driver**

Run: `kernel/fetch-sources.sh && kernel/build.sh`
Expected: build ends without error and prints `7.2.9-zen1-1-zen SMP preempt mod_unload ` (same vermagic as `modinfo -F vermagic snd-usb-audio`).

- [ ] **Step 7: Write `kernel/README.md`**

````markdown
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

## Load / unload (root)

Loading replaces `snd-usb-audio` for **every** USB audio device until reboot or
`unload.sh`. Stop PipeWire first so that the module can be removed:

```sh
systemctl --user stop pipewire.socket pipewire-pulse.socket wireplumber pipewire pipewire-pulse
sudo kernel/load.sh        # or: sudo kernel/unload.sh
systemctl --user start pipewire.socket pipewire-pulse.socket wireplumber
```
````

- [ ] **Step 8: Swap test with the unmodified module (user, root)**

Ask the user to run, with the Fireface UC in AP mode:

```
! systemctl --user stop pipewire.socket pipewire-pulse.socket wireplumber pipewire pipewire-pulse
! sudo kernel/load.sh
```

Expected: `loaded …/snd-usb-audio.ko`; `/proc/asound/cards` still lists the generic "USB Audio", "Xkey37" and "RODE NT-USB" cards (Review Focus 5). Then:

```
! sudo kernel/unload.sh
! systemctl --user start pipewire.socket pipewire-pulse.socket wireplumber
```

Expected: `stock snd-usb-audio loaded`, same cards listed.

- [ ] **Step 9: Commit**

```bash
git add .gitignore kernel/fetch-sources.sh kernel/build.sh kernel/export-patch.sh kernel/load.sh kernel/unload.sh kernel/README.md
git commit -m "kernel: out-of-tree build and swap of snd-usb-audio"
```

---

### Task 2: Stream format helpers (`rme_uc.c`) with userspace unit tests

**Files:**
- Create: `kernel/sound-usb/rme_uc.h`, `kernel/sound-usb/rme_uc.c`
- Create: `kernel/tests/Makefile`, `kernel/tests/test_rme_uc.c`
- Create: `kernel/tests/shim/linux/types.h`, `kernel/tests/shim/linux/kernel.h`, `kernel/tests/shim/linux/string.h`, `kernel/tests/shim/linux/errno.h`, `kernel/tests/shim/linux/math64.h`, `kernel/tests/shim/asm/byteorder.h`

**Interfaces:**
- Produces (used by Task 3):
  - `#define RME_UC_COUNTER_MASK 0x1fff`, `#define RME_UC_CAPTURE_HDR 4`
  - `struct rme_uc_rate_regs { u16 period_hi; u16 period_lo; u16 family; };`
  - `int rme_uc_rate_regs(unsigned int rate, struct rme_uc_rate_regs *regs);` — 0 or `-EINVAL` (rate outside 32000..192000)
  - `void rme_uc_pack_packet(u8 *dst, const u8 *ring, unsigned int ring_bytes, unsigned int *ring_pos, unsigned int frames, unsigned int channels, unsigned int counter);` — writes `frames × (channels + 1) × 4` bytes; `ring == NULL` writes silence and ignores `ring_bytes` / `ring_pos`
  - `unsigned int rme_uc_unpack_packet(u8 *ring, unsigned int ring_bytes, unsigned int *ring_pos, const u8 *pkt, unsigned int len, unsigned int channels);` — returns frames written

- [ ] **Step 1: Write the userspace shims**

`kernel/tests/shim/linux/types.h`:

```c
/* Minimal userspace stand-in for <linux/types.h> (unit tests only). */
#ifndef SHIM_LINUX_TYPES_H
#define SHIM_LINUX_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __le32;
#endif
```

`kernel/tests/shim/linux/kernel.h`:

```c
#ifndef SHIM_LINUX_KERNEL_H
#define SHIM_LINUX_KERNEL_H
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
```

`kernel/tests/shim/linux/string.h`:

```c
#ifndef SHIM_LINUX_STRING_H
#define SHIM_LINUX_STRING_H
#include <string.h>
#endif
```

`kernel/tests/shim/linux/errno.h`:

```c
#ifndef SHIM_LINUX_ERRNO_H
#define SHIM_LINUX_ERRNO_H
#include <errno.h>
#endif
```

`kernel/tests/shim/linux/math64.h`:

```c
#ifndef SHIM_LINUX_MATH64_H
#define SHIM_LINUX_MATH64_H
#include <linux/types.h>
static inline u64 div_u64(u64 dividend, u32 divisor)
{
	return dividend / divisor;
}
#endif
```

`kernel/tests/shim/asm/byteorder.h`:

```c
#ifndef SHIM_ASM_BYTEORDER_H
#define SHIM_ASM_BYTEORDER_H
/* the tests run on little-endian x86 */
#define cpu_to_le32(x) ((__le32)(x))
#endif
```

- [ ] **Step 2: Write `kernel/sound-usb/rme_uc.h`**

```c
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RME Fireface UC (AP mode) stream format helpers.
 */
#ifndef __USBAUDIO_RME_UC_H
#define __USBAUDIO_RME_UC_H

#include <linux/types.h>

/* playback: frame counter in word 0 of the first frame of each packet */
#define RME_UC_COUNTER_MASK	0x1fff
/* capture: header bytes at the start of each packet */
#define RME_UC_CAPTURE_HDR	4

/* values of the two vendor requests that set the sample rate */
struct rme_uc_rate_regs {
	u16 period_hi;	/* bRequest 0x1b, wValue */
	u16 period_lo;	/* bRequest 0x1b, wIndex */
	u16 family;	/* bRequest 0x10, wValue (wIndex = mask 0x0030) */
};

int rme_uc_rate_regs(unsigned int rate, struct rme_uc_rate_regs *regs);

void rme_uc_pack_packet(u8 *dst, const u8 *ring, unsigned int ring_bytes,
			unsigned int *ring_pos, unsigned int frames,
			unsigned int channels, unsigned int counter);

unsigned int rme_uc_unpack_packet(u8 *ring, unsigned int ring_bytes,
				  unsigned int *ring_pos, const u8 *pkt,
				  unsigned int len, unsigned int channels);

#endif /* __USBAUDIO_RME_UC_H */
```

- [ ] **Step 3: Write the failing tests `kernel/tests/test_rme_uc.c`**

```c
// SPDX-License-Identifier: GPL-2.0-or-later
/* Userspace unit tests of sound-usb/rme_uc.c: make -C kernel/tests check */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/types.h>
#include <linux/errno.h>
#include "rme_uc.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

static u32 word(const u8 *p, unsigned int idx)
{
	u32 w;

	memcpy(&w, p + idx * 4, 4);
	return w;
}

/* values captured from the official driver (docs/protocol/controls.md) */
static void test_rate_regs(void)
{
	static const struct {
		unsigned int rate;
		u16 hi, lo, family;
	} v[] = {
		{  32000, 0xc350, 0x0000, 0x00 },
		{  44100, 0x8db9, 0x2d01, 0x10 },
		{  48000, 0x8235, 0x5502, 0x20 },
		{  64000, 0xc350, 0x0000, 0x00 },
		{  88200, 0x8db9, 0x2d01, 0x10 },
		{  96000, 0x8235, 0x5502, 0x20 },
		{ 128000, 0xc350, 0x0000, 0x00 },
		{ 176400, 0x8db9, 0x2d01, 0x10 },
		{ 192000, 0x8235, 0x5502, 0x20 },
		{  42336, 0x93a0, 0xe501, 0x10 },
		{  45937, 0x880e, 0x4f01, 0x10 },
		{  46080, 0x87a2, 0x3802, 0x20 },
		{  50000, 0x7d00, 0x0002, 0x20 },
	};
	struct rme_uc_rate_regs r;
	unsigned int i;

	for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
		CHECK(rme_uc_rate_regs(v[i].rate, &r) == 0);
		if (r.period_hi != v[i].hi || r.period_lo != v[i].lo ||
		    r.family != v[i].family) {
			fprintf(stderr, "rate %u: got %04x %04x %04x\n",
				v[i].rate, r.period_hi, r.period_lo, r.family);
			failures++;
		}
	}
	CHECK(rme_uc_rate_regs(31999, &r) == -EINVAL);
	CHECK(rme_uc_rate_regs(192001, &r) == -EINVAL);
}

/* ring of 4 frames x 2 channels, sample value = 10 * frame + channel */
static void fill_ring(u8 *ring)
{
	u32 f, c;

	for (f = 0; f < 4; f++)
		for (c = 0; c < 2; c++) {
			u32 v = 10 * f + c;

			memcpy(ring + (f * 2 + c) * 4, &v, 4);
		}
}

static void test_pack(void)
{
	u8 ring[4 * 2 * 4], dst[3 * 3 * 4];
	unsigned int pos = 0;

	fill_ring(ring);
	rme_uc_pack_packet(dst, ring, sizeof(ring), &pos, 3, 2, 0x2005);
	/* frame 0: counter (mod 8192), ch0, ch1 */
	CHECK(word(dst, 0) == 0x0005);
	CHECK(word(dst, 1) == 0 && word(dst, 2) == 1);
	/* frames 1 and 2: word 0 is 0 */
	CHECK(word(dst, 3) == 0 && word(dst, 4) == 10 && word(dst, 5) == 11);
	CHECK(word(dst, 6) == 0 && word(dst, 7) == 20 && word(dst, 8) == 21);
	CHECK(pos == 3 * 2 * 4);
}

static void test_pack_wrap(void)
{
	u8 ring[4 * 2 * 4], dst[3 * 3 * 4];
	unsigned int pos = 3 * 2 * 4;	/* last frame of the ring */

	fill_ring(ring);
	rme_uc_pack_packet(dst, ring, sizeof(ring), &pos, 3, 2, 7);
	CHECK(word(dst, 1) == 30 && word(dst, 2) == 31);	/* frame 3 */
	CHECK(word(dst, 4) == 0 && word(dst, 5) == 1);		/* frame 0 */
	CHECK(word(dst, 7) == 10 && word(dst, 8) == 11);	/* frame 1 */
	CHECK(pos == 2 * 2 * 4);
}

static void test_pack_silent(void)
{
	u8 dst[2 * 3 * 4];
	unsigned int i;

	memset(dst, 0xaa, sizeof(dst));
	rme_uc_pack_packet(dst, NULL, 0, NULL, 2, 2, 0x1fff + 3);
	CHECK(word(dst, 0) == 0x0002);
	for (i = 1; i < 6; i++)
		CHECK(word(dst, i) == 0);
}

/* capture packet: header + 2 planar channels of 3 frames */
static void make_capture(u8 *pkt)
{
	u32 hdr = 0xdeadbeef;
	u32 f, c;

	memcpy(pkt, &hdr, 4);
	for (c = 0; c < 2; c++)
		for (f = 0; f < 3; f++) {
			u32 v = 100 * c + f;

			memcpy(pkt + 4 + (c * 3 + f) * 4, &v, 4);
		}
}

static void test_unpack(void)
{
	u8 pkt[4 + 2 * 3 * 4], ring[4 * 2 * 4];
	unsigned int pos = 0;

	make_capture(pkt);
	memset(ring, 0, sizeof(ring));
	CHECK(rme_uc_unpack_packet(ring, sizeof(ring), &pos, pkt,
				   sizeof(pkt), 2) == 3);
	CHECK(word(ring, 0) == 0 && word(ring, 1) == 100);	/* frame 0 */
	CHECK(word(ring, 2) == 1 && word(ring, 3) == 101);	/* frame 1 */
	CHECK(word(ring, 4) == 2 && word(ring, 5) == 102);	/* frame 2 */
	CHECK(pos == 3 * 2 * 4);
}

static void test_unpack_wrap(void)
{
	u8 pkt[4 + 2 * 3 * 4], ring[4 * 2 * 4];
	unsigned int pos = 3 * 2 * 4;

	make_capture(pkt);
	memset(ring, 0, sizeof(ring));
	CHECK(rme_uc_unpack_packet(ring, sizeof(ring), &pos, pkt,
				   sizeof(pkt), 2) == 3);
	CHECK(word(ring, 6) == 0 && word(ring, 7) == 100);	/* in frame 3 */
	CHECK(word(ring, 0) == 1 && word(ring, 1) == 101);	/* in frame 0 */
	CHECK(word(ring, 2) == 2 && word(ring, 3) == 102);	/* in frame 1 */
	CHECK(pos == 2 * 2 * 4);
}

static void test_unpack_short(void)
{
	u8 pkt[4] = { 1, 2, 3, 4 }, ring[4 * 2 * 4];
	unsigned int pos = 8;

	memset(ring, 0x55, sizeof(ring));
	CHECK(rme_uc_unpack_packet(ring, sizeof(ring), &pos, pkt, 4, 2) == 0);
	CHECK(rme_uc_unpack_packet(ring, sizeof(ring), &pos, pkt, 2, 2) == 0);
	CHECK(rme_uc_unpack_packet(ring, sizeof(ring), &pos, pkt, 0, 2) == 0);
	CHECK(pos == 8);
	CHECK(ring[0] == 0x55 && ring[sizeof(ring) - 1] == 0x55);
}

int main(void)
{
	test_rate_regs();
	test_pack();
	test_pack_wrap();
	test_pack_silent();
	test_unpack();
	test_unpack_wrap();
	test_unpack_short();
	if (failures) {
		fprintf(stderr, "%d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	printf("all tests passed\n");
	return EXIT_SUCCESS;
}
```

`kernel/tests/Makefile`:

```make
CFLAGS = -Wall -Wextra -Werror -O2 -I shim -I ../sound-usb

test_rme_uc: test_rme_uc.c ../sound-usb/rme_uc.c ../sound-usb/rme_uc.h
	$(CC) $(CFLAGS) -o $@ test_rme_uc.c ../sound-usb/rme_uc.c

check: test_rme_uc
	./test_rme_uc

clean:
	rm -f test_rme_uc

.PHONY: check clean
```

Also add `/kernel/tests/test_rme_uc` to `.gitignore`.

- [ ] **Step 4: Run the tests to see them fail**

Run: `make -C kernel/tests check`
Expected: FAIL at link/compile time (`../sound-usb/rme_uc.c` does not exist yet).

- [ ] **Step 5: Write `kernel/sound-usb/rme_uc.c`**

```c
// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RME Fireface UC (AP mode) stream format helpers.
 *
 * Playback packets: frames of (1 + channels) little-endian float32 words; word 0
 * of the first frame of each packet is a frame counter (modulo 8192), word 0 of
 * the other frames is 0. The device needs the counter.
 *
 * Capture packets: a 4-byte header, then the samples grouped per channel
 * (n samples of channel 1, n samples of channel 2, ...).
 *
 * The sample rate is set with two vendor requests (0x1b: period of the base
 * rate, 0x10: rate family), as the official driver's hwSetSampleRate() does.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#include "rme_uc.h"

/* 0: 32 / 64 / 128 kHz, 1: 44.1 / 88.2 / 176.4 kHz, 2: 48 / 96 / 192 kHz */
static unsigned int rme_uc_rate_family(unsigned int rate)
{
	/* upper bounds of the official driver's rate snapping (xRate()) */
	static const struct {
		unsigned int max;
		unsigned int family;
	} bands[] = {
		{  38049, 0 }, {  46007, 1 }, {  56999, 2 },
		{  76099, 0 }, {  92099, 1 }, { 113999, 2 },
		{ 152199, 0 }, { 184199, 1 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bands); i++)
		if (rate <= bands[i].max)
			return bands[i].family;
	return 2;
}

int rme_uc_rate_regs(unsigned int rate, struct rme_uc_rate_regs *regs)
{
	unsigned int family;
	u64 period;
	u32 p;

	if (rate < 32000 || rate > 192000)
		return -EINVAL;

	family = rme_uc_rate_family(rate);

	/* 2^20 * 10^8 / rate, scaled back to the period of the base rate */
	period = div_u64(0x5f5e10000000ULL, rate);
	if (rate > 114000)
		period <<= 2;
	else if (rate > 57000)
		period <<= 1;
	p = (u32)period;

	regs->period_hi = p >> 16;
	regs->period_lo = (p & 0xff00) | family;
	regs->family = family << 4;
	return 0;
}

/*
 * Build one playback ISO packet of @frames frames from the interleaved ALSA ring
 * buffer @ring (@ring_bytes long, a multiple of @channels * 4), starting at
 * byte offset *@ring_pos, which is advanced with wrap-around. A NULL @ring
 * builds a silent packet (still carrying the counter).
 */
void rme_uc_pack_packet(u8 *dst, const u8 *ring, unsigned int ring_bytes,
			unsigned int *ring_pos, unsigned int frames,
			unsigned int channels, unsigned int counter)
{
	unsigned int frame_bytes = channels * 4;
	unsigned int f;

	for (f = 0; f < frames; f++) {
		__le32 word0 = cpu_to_le32(f ? 0 : counter & RME_UC_COUNTER_MASK);

		memcpy(dst, &word0, sizeof(word0));
		if (ring) {
			memcpy(dst + 4, ring + *ring_pos, frame_bytes);
			*ring_pos += frame_bytes;
			if (*ring_pos >= ring_bytes)
				*ring_pos -= ring_bytes;
		} else {
			memset(dst + 4, 0, frame_bytes);
		}
		dst += 4 + frame_bytes;
	}
}

/*
 * Convert one capture ISO packet of @len bytes into interleaved frames written
 * to the ALSA ring buffer @ring at byte offset *@ring_pos (advanced with
 * wrap-around). Returns the number of frames; packets without samples return 0.
 */
unsigned int rme_uc_unpack_packet(u8 *ring, unsigned int ring_bytes,
				  unsigned int *ring_pos, const u8 *pkt,
				  unsigned int len, unsigned int channels)
{
	unsigned int frames, f, c;

	if (!channels || len < RME_UC_CAPTURE_HDR)
		return 0;

	frames = (len - RME_UC_CAPTURE_HDR) / (channels * 4);
	pkt += RME_UC_CAPTURE_HDR;

	for (f = 0; f < frames; f++) {
		u8 *dst = ring + *ring_pos;

		for (c = 0; c < channels; c++)
			memcpy(dst + c * 4, pkt + (c * frames + f) * 4, 4);
		*ring_pos += channels * 4;
		if (*ring_pos >= ring_bytes)
			*ring_pos -= ring_bytes;
	}
	return frames;
}
```

- [ ] **Step 6: Run the tests to see them pass**

Run: `make -C kernel/tests check`
Expected: `all tests passed`

- [ ] **Step 7: checkpatch**

Run: `kernel/build/zen/scripts/checkpatch.pl --no-tree -f kernel/sound-usb/rme_uc.c kernel/sound-usb/rme_uc.h`
Expected: `total: 0 errors, 0 warnings` for both files (fix any reported style issue, re-run Step 6).

- [ ] **Step 8: Commit**

```bash
git add .gitignore kernel/sound-usb kernel/tests
git commit -m "kernel: Fireface UC stream format helpers with unit tests"
```

---

### Task 3: Declare the device and set the sample rate

**Files (in `kernel/build/sound-usb/`, exported to `kernel/patches/` at the end):**
- Modify: `Makefile` (object list), `usbaudio.h` (quirk type, flag, helper), `quirks.c` (flag name, flag table, rate quirk), `quirks-table.h` (device entry)

**Interfaces:**
- Consumes: `rme_uc_rate_regs()`, `struct rme_uc_rate_regs` (Task 2).
- Produces: `QUIRK_FLAG_RME_UC`, `static inline bool snd_usb_is_rme_uc(struct snd_usb_audio *chip)` (Task 4).

- [ ] **Step 1: Add `rme_uc.o` to the module**

In `Makefile`, after the line `			quirks.o \` add:

```make
			rme_uc.o \
```

- [ ] **Step 2: Quirk type, flag and helper in `usbaudio.h`**

After `	QUIRK_TYPE_MIXER_GET_CUR_BROKEN		= 30,` add:

```c
	QUIRK_TYPE_RME_UC			= 31,
```

After `#define QUIRK_FLAG_MIXER_GET_CUR_BROKEN		QUIRK_FLAG(MIXER_GET_CUR_BROKEN)` add:

```c
#define QUIRK_FLAG_RME_UC			QUIRK_FLAG(RME_UC)
```

At the end of the file, before the final `#endif`, add:

```c
/* RME Fireface UC: device-specific stream format, see rme_uc.c */
static inline bool snd_usb_is_rme_uc(struct snd_usb_audio *chip)
{
	return chip->quirk_flags & QUIRK_FLAG_RME_UC;
}
```

- [ ] **Step 3: Flag name and flag table in `quirks.c`**

After `	QUIRK_STRING_ENTRY(MIXER_GET_CUR_BROKEN),` add:

```c
	QUIRK_STRING_ENTRY(RME_UC),
```

Before the line `	DEVICE_FLG(0x045e, 0x083c, /* MS USB Link headset */` add:

```c
	DEVICE_FLG(0x0424, 0x3fc6, /* RME Fireface UC (AP mode, firmware <= 118) */
		   QUIRK_FLAG_RME_UC),
```

Before the line `	DEVICE_FLG(0x2a70, 0x1881, /* OnePlus Technology (Shenzhen) Co., Ltd. BE02T */` add:

```c
	DEVICE_FLG(0x2a39, 0x3fcf, /* RME Fireface UC (AP mode) */
		   QUIRK_FLAG_RME_UC),
```

- [ ] **Step 4: Rate quirk in `quirks.c`**

Add `#include "rme_uc.h"` after the last `#include "..."` line at the top of `quirks.c`.

Before `void snd_usb_set_format_quirk(struct snd_usb_substream *subs,` add:

```c
/*
 * RME Fireface UC: period of the base rate (0x1b), then rate family (0x10,
 * mask 0x0030). The alternate setting of the rate band is already selected.
 */
static void rme_uc_set_format_quirk(struct snd_usb_substream *subs)
{
	struct usb_device *dev = subs->stream->chip->dev;
	struct rme_uc_rate_regs regs;

	if (rme_uc_rate_regs(subs->data_endpoint->cur_rate, &regs) < 0)
		return;

	snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), 0x1b, 0x40,
			regs.period_hi, regs.period_lo, NULL, 0);
	snd_usb_ctl_msg(dev, usb_sndctrlpipe(dev, 0), 0x10, 0x40,
			regs.family, 0x0030, NULL, 0);
}
```

In `snd_usb_set_format_quirk()`, after the two Digiface lines

```c
		rme_digiface_set_format_quirk(subs);
		break;
```

add:

```c
	case USB_ID(0x0424, 0x3fc6): /* RME Fireface UC (firmware <= 118) */
	case USB_ID(0x2a39, 0x3fcf): /* RME Fireface UC */
		rme_uc_set_format_quirk(subs);
		break;
```

- [ ] **Step 5: Device entry in `quirks-table.h`**

After the line `QUIRK_RME_DIGIFACE(0x3fa0),` add:

```c
/*
 * RME Fireface UC in AP (Mac) mode: one alternate setting per rate band,
 * 18 / 14 / 12 channels (ADAT S/MUX), float samples, implicit feedback from
 * the capture endpoint. maxpacksize is given because the high-bandwidth
 * multiplier of alt 3 is not applied by the fixed-stream parser.
 */
#define RME_UC_PLAYBACK(alt, ch, maxpack, rates_, r0, r1, r2) \
	{ \
		QUIRK_DATA_AUDIOFORMAT(0) { \
			.formats = SNDRV_PCM_FMTBIT_FLOAT_LE, \
			.channels = ch, \
			.fmt_bits = 32, \
			.iface = 0, \
			.altsetting = alt, \
			.altset_idx = alt, \
			.endpoint = 0x01, \
			.ep_idx = 0, \
			.ep_attr = USB_ENDPOINT_XFER_ISOC | \
				   USB_ENDPOINT_SYNC_ASYNC, \
			.maxpacksize = maxpack, \
			.rates = rates_, \
			.rate_min = r0, \
			.rate_max = r2, \
			.nr_rates = 3, \
			.rate_table = (unsigned int[]) { r0, r1, r2 }, \
			.sync_ep = 0x82, \
			.sync_iface = 0, \
			.sync_altsetting = alt, \
			.sync_ep_idx = 1, \
			.implicit_fb = 1, \
		}, \
	}

#define RME_UC_CAPTURE(alt, ch, maxpack, rates_, r0, r1, r2) \
	{ \
		QUIRK_DATA_AUDIOFORMAT(0) { \
			.formats = SNDRV_PCM_FMTBIT_FLOAT_LE, \
			.channels = ch, \
			.fmt_bits = 32, \
			.iface = 0, \
			.altsetting = alt, \
			.altset_idx = alt, \
			.endpoint = 0x82, \
			.ep_idx = 1, \
			.ep_attr = USB_ENDPOINT_XFER_ISOC | \
				   USB_ENDPOINT_SYNC_ASYNC, \
			.maxpacksize = maxpack, \
			.rates = rates_, \
			.rate_min = r0, \
			.rate_max = r2, \
			.nr_rates = 3, \
			.rate_table = (unsigned int[]) { r0, r1, r2 }, \
		}, \
	}

#define RME_UC_RATES_1X \
	(SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RME_UC_RATES_2X \
	(SNDRV_PCM_RATE_64000 | SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000)
#define RME_UC_RATES_4X \
	(SNDRV_PCM_RATE_KNOT | SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000)

#define QUIRK_RME_FIREFACE_UC(vid, pid) \
{ \
	/* Only claim interface 0 */ \
	.match_flags = USB_DEVICE_ID_MATCH_VENDOR | \
		       USB_DEVICE_ID_MATCH_PRODUCT | \
		       USB_DEVICE_ID_MATCH_INT_CLASS | \
		       USB_DEVICE_ID_MATCH_INT_NUMBER, \
	.idVendor = vid, \
	.idProduct = pid, \
	.bInterfaceClass = USB_CLASS_VENDOR_SPEC, \
	.bInterfaceNumber = 0, \
	QUIRK_DRIVER_INFO { \
		.vendor_name = "RME", \
		.product_name = "Fireface UC", \
		QUIRK_DATA_COMPOSITE { \
			RME_UC_PLAYBACK(1, 18,  532, RME_UC_RATES_1X, 32000, 44100, 48000), \
			RME_UC_PLAYBACK(2, 14,  780, RME_UC_RATES_2X, 64000, 88200, 96000), \
			RME_UC_PLAYBACK(3, 12, 1304, RME_UC_RATES_4X, 128000, 176400, 192000), \
			RME_UC_CAPTURE(1, 18,  508, RME_UC_RATES_1X, 32000, 44100, 48000), \
			RME_UC_CAPTURE(2, 14,  732, RME_UC_RATES_2X, 64000, 88200, 96000), \
			RME_UC_CAPTURE(3, 12, 1208, RME_UC_RATES_4X, 128000, 176400, 192000), \
			QUIRK_COMPOSITE_END \
		} \
	} \
}

QUIRK_RME_FIREFACE_UC(0x0424, 0x3fc6),
QUIRK_RME_FIREFACE_UC(0x2a39, 0x3fcf),
```

- [ ] **Step 6: Build**

Run: `kernel/build.sh`
Expected: no error, no new warning in `quirks.c` / `rme_uc.c`.

- [ ] **Step 7: Enumeration test (user, root)**

With PipeWire stopped (README), ask the user to run `! sudo kernel/load.sh`, then run:

```sh
UC=$(awk '/Fireface UC/ {print $1; exit}' /proc/asound/cards); echo "card $UC"
cat /proc/asound/card$UC/stream0
```

Expected: a card "Fireface UC"; `stream0` lists playback and capture, each with 3 formats: `FLOAT_LE`, channels 18 / 14 / 12, altsets 1 / 2 / 3, endpoints `0x01` (playback, implicit feedback from `0x82`) and `0x82`. Then `! sudo kernel/unload.sh`. (No audio is streamed yet: playback/capture data is wrong until Task 4.)

- [ ] **Step 8: Export the patch and commit**

```bash
kernel/export-patch.sh
git add kernel/patches
git commit -m "kernel: declare the Fireface UC and set its sample rate"
```

---

### Task 4: Playback packing and capture unpacking in the stream paths

**Files (in `kernel/build/sound-usb/`, exported to `kernel/patches/`):**
- Modify: `card.h` (endpoint counter), `endpoint.c` (playback stride, max frames, silent packets), `pcm.c` (playback copy, capture copy)

**Interfaces:**
- Consumes: `snd_usb_is_rme_uc()` (Task 3), `rme_uc_pack_packet()`, `rme_uc_unpack_packet()`, `RME_UC_CAPTURE_HDR` (Task 2).
- Produces: `unsigned int rme_uc_counter;` in `struct snd_usb_endpoint`.

- [ ] **Step 1: Endpoint counter in `card.h`**

In `struct snd_usb_endpoint`, after `	unsigned int stride;` add:

```c
	unsigned int rme_uc_counter;	/* RME Fireface UC playback frame counter */
```

- [ ] **Step 2: Playback stride and counter reset in `endpoint.c`**

Add `#include "rme_uc.h"` after the last `#include "..."` line of `endpoint.c`.

In `data_ep_set_params()`, before `	ep->datainterval = fmt->datainterval;` add:

```c
	if (snd_usb_is_rme_uc(chip) && usb_pipeout(ep->pipe)) {
		/* a frame counter word precedes each playback frame */
		frame_bits += 32;
		ep->rme_uc_counter = 0;
	}
```

- [ ] **Step 3: Max frames per packet in `endpoint.c`**

In `snd_usb_endpoint_set_params()`, replace

```c
	ep->maxframesize = ep->maxpacksize / ep->cur_frame_bytes;
```

with

```c
	if (snd_usb_is_rme_uc(chip) && usb_pipeout(ep->pipe))
		ep->maxframesize = ep->maxpacksize / ep->stride;
	else
		ep->maxframesize = ep->maxpacksize / ep->cur_frame_bytes;
```

- [ ] **Step 4: Silent packets carry the counter in `endpoint.c`** (Review Focus 1)

In `prepare_silent_urb()`, replace

```c
		memset(urb->transfer_buffer + offs,
		       ep->silence_value, length);
```

with

```c
		if (snd_usb_is_rme_uc(ep->chip) && usb_pipeout(ep->pipe)) {
			unsigned int frames = length / ep->stride;

			rme_uc_pack_packet(urb->transfer_buffer + offs, NULL, 0,
					   NULL, frames, ep->cur_channels,
					   ep->rme_uc_counter);
			ep->rme_uc_counter += frames;
		} else {
			memset(urb->transfer_buffer + offs,
			       ep->silence_value, length);
		}
```

- [ ] **Step 5: Playback copy in `pcm.c`**

Add `#include "rme_uc.h"` after the last `#include "..."` line of `pcm.c`.

Before `static int prepare_playback_urb(struct snd_usb_substream *subs,` add:

```c
/*
 * RME Fireface UC: the packet descriptors are already set up with the device
 * stride (counter word + channels); copy the frames and insert the counter.
 */
static void fill_playback_urb_rme_uc(struct snd_usb_substream *subs,
				     struct urb *urb)
{
	struct snd_pcm_runtime *runtime = subs->pcm_substream->runtime;
	struct snd_usb_endpoint *ep = subs->data_endpoint;
	unsigned int pos = subs->hwptr_done;
	unsigned int frames = 0;
	int i;

	for (i = 0; i < urb->number_of_packets; i++) {
		unsigned int n = urb->iso_frame_desc[i].length / ep->stride;

		rme_uc_pack_packet(urb->transfer_buffer +
				   urb->iso_frame_desc[i].offset,
				   runtime->dma_area, subs->buffer_bytes, &pos,
				   n, ep->cur_channels, ep->rme_uc_counter);
		ep->rme_uc_counter += n;
		frames += n;
	}

	/* ALSA buffer bookkeeping, in ALSA frames (without the counter) */
	urb_ctx_queue_advance(subs, urb, frames * (runtime->frame_bits >> 3));
}
```

In `prepare_playback_urb()`, replace

```c
		} else {
			/* usual PCM */
			if (!subs->tx_length_quirk)
```

with

```c
		} else if (snd_usb_is_rme_uc(subs->stream->chip)) {
			fill_playback_urb_rme_uc(subs, urb);
		} else {
			/* usual PCM */
			if (!subs->tx_length_quirk)
```

(`bytes = frames * stride` above already is the USB size with the device stride.)

- [ ] **Step 6: Capture copy in `pcm.c`** (Review Focus 2, 3)

In `retire_capture_urb()`:

1. After `	stride = runtime->frame_bits >> 3;` add:

```c
	rme_uc = snd_usb_is_rme_uc(subs->stream->chip);
```

and add `bool rme_uc;` to the declarations at the top of the function.

2. Replace

```c
		bytes = urb->iso_frame_desc[i].actual_length;
		if (subs->stream_offset_adj > 0) {
```

with

```c
		bytes = urb->iso_frame_desc[i].actual_length;
		if (rme_uc) {
			/* header + planar channels, see rme_uc.c */
			frames = bytes >= RME_UC_CAPTURE_HDR ?
				 (bytes - RME_UC_CAPTURE_HDR) / stride : 0;
			bytes = frames * stride;
		} else if (subs->stream_offset_adj > 0) {
```

3. Replace the copy block

```c
		/* copy a data chunk */
		if (oldptr + bytes > subs->buffer_bytes) {
```

with

```c
		/* copy a data chunk */
		if (rme_uc) {
			unsigned int pos = oldptr;

			rme_uc_unpack_packet(runtime->dma_area,
					     subs->buffer_bytes, &pos, cp,
					     urb->iso_frame_desc[i].actual_length,
					     runtime->channels);
		} else if (oldptr + bytes > subs->buffer_bytes) {
```

(With `rme_uc`, `frames = bytes / stride` and the `txfr_quirk` / sample-alignment corrections that follow are no-ops because `bytes` is already a whole number of frames.)

- [ ] **Step 7: Build and re-run the unit tests**

Run: `kernel/build.sh && make -C kernel/tests check`
Expected: build without error or new warning; `all tests passed`.

- [ ] **Step 8: checkpatch on the exported patch**

Run: `kernel/export-patch.sh && kernel/build/zen/scripts/checkpatch.pl --no-tree kernel/patches/0001-ALSA-usb-audio-add-RME-Fireface-UC.patch`
Expected: 0 errors (warnings about the missing commit message / sign-off of a raw diff are acceptable at this stage; fix style warnings in the code).

- [ ] **Step 9: Commit**

```bash
git add kernel/patches
git commit -m "kernel: Fireface UC playback and capture stream format"
```

---

### Task 5: Hardware verification tools and tests

**Files:**
- Create: `tools/rme_ctl.py`
- Modify: `kernel/README.md` (test procedure), `docs/roadmap.md`, `docs/project-layout.md`

**Interfaces:**
- Consumes: the module built in Task 4; `RME_UC` card in `/proc/asound/cards`.
- Produces: `tools/rme_ctl.py` commands `loopback`, `mix-diagonal`, `gen`, `check`, `mono18`.

- [ ] **Step 1: Write `tools/rme_ctl.py`**

```python
#!/usr/bin/env python3
"""Fireface UC test helper, usable while snd-usb-audio is bound to the device.

Control requests on endpoint 0 do not need to claim an interface, so they can be
sent while the kernel driver streams audio.

  rme_ctl.py loopback <n> on|off      loopback on outputs 1..n (0x40 0x15)
  rme_ctl.py mix-diagonal             playback k -> output k at 0 dB, other
                                      playback cells -inf, output faders 0 dB
                                      (overwrites the mix stored in the device)
  rme_ctl.py gen <channels> <frames> <file>
                                      float32 interleaved test signal: channel k
                                      is a sawtooth of amplitude 0.002 * k
  rme_ctl.py check <channels> <file>  peak of each channel of a capture, as k
  rme_ctl.py mono18 <channels> <in> <out>
                                      copy a mono float32 file to channels 1 and 2
"""
import ctypes
import ctypes.util
import struct
import sys

IDS = ((0x2a39, 0x3fcf), (0x0424, 0x3fc6))


def open_device():
    lib = ctypes.CDLL(ctypes.util.find_library('usb-1.0'))
    lib.libusb_open_device_with_vid_pid.restype = ctypes.c_void_p
    if lib.libusb_init(None) != 0:
        sys.exit('libusb_init failed')
    for vid, pid in IDS:
        handle = lib.libusb_open_device_with_vid_pid(None, vid, pid)
        if handle:
            return lib, ctypes.c_void_p(handle)
    sys.exit('Fireface UC (AP mode) not found or not accessible')


def control(lib, handle, request, value, index):
    rc = lib.libusb_control_transfer(handle, 0x40, request, value, index, None, 0, 1000)
    if rc < 0:
        sys.exit(f'request 0x{request:02x} {value:#06x} {index:#06x} failed: {rc}')


def set_gain(lib, handle, index, level):
    """level: linear, 0x8000 = 0 dB (docs/protocol/mixer-routing.md)."""
    control(lib, handle, 0x12, (level >> 2) & 0xffff, ((level & 3) << 14) | (index & 0x3fff))


def cmd_loopback(n, state):
    lib, handle = open_device()
    for out in range(int(n)):
        control(lib, handle, 0x15, 1 if state == 'on' else 0, out)
    print(f'loopback {state} on outputs 1..{n}')


def cmd_mix_diagonal():
    lib, handle = open_device()
    for out in range(18):
        for ch in range(18):
            set_gain(lib, handle, 36 * out + 18 + ch, 0x8000 if ch == out else 0)
        set_gain(lib, handle, 0x3e0 + out, 0x8000)
    print('mix: playback k -> output k at 0 dB, output faders at 0 dB')


def cmd_gen(channels, frames, path):
    channels, frames = int(channels), int(frames)
    with open(path, 'wb') as f:
        for i in range(frames):
            ramp = (i % 100) / 100.0
            f.write(struct.pack(f'<{channels}f', *(0.002 * (c + 1) * ramp for c in range(channels))))


def cmd_check(channels, path):
    channels = int(channels)
    data = open(path, 'rb').read()
    frames = len(data) // (4 * channels)
    peak = [0.0] * channels
    for i in range(frames):
        for c, v in enumerate(struct.unpack_from(f'<{channels}f', data, i * 4 * channels)):
            peak[c] = max(peak[c], v)
    print(' '.join(f'{c + 1}:{peak[c] / 0.002 / 0.99:.2f}' for c in range(channels)))


def cmd_mono18(channels, src, dst):
    channels = int(channels)
    mono = open(src, 'rb').read()
    with open(dst, 'wb') as f:
        for (v,) in struct.iter_unpack('<f', mono):
            f.write(struct.pack(f'<{channels}f', v, v, *([0.0] * (channels - 2))))


if __name__ == '__main__':
    commands = {'loopback': cmd_loopback, 'mix-diagonal': cmd_mix_diagonal,
                'gen': cmd_gen, 'check': cmd_check, 'mono18': cmd_mono18}
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        sys.exit(__doc__)
    commands[sys.argv[1]](*sys.argv[2:])
```

Run: `chmod +x tools/rme_ctl.py && python3 tools/rme_ctl.py gen 18 4 /tmp/t.raw && python3 tools/rme_ctl.py check 18 /tmp/t.raw`
Expected: `1:0.03 2:0.06 3:0.09 …` (with only 4 frames the ramp reaches 0.03 of its amplitude).

- [ ] **Step 2: Load the driver (user, root)**

PipeWire stopped (README), then `! sudo kernel/load.sh`. Set `UC=$(awk '/Fireface UC/ {print $1; exit}' /proc/asound/cards)`.

- [ ] **Step 3: Playback listening test, 44.1 kHz**

```sh
python3 tools/rme_ctl.py mono18 18 libusb_test/Her-Swim__44100-f32le.raw /tmp/music18.raw
aplay -D hw:$UC -t raw -f FLOAT_LE -r 44100 -c 18 /tmp/music18.raw
```

Expected: music on outputs 1/2, **clean from the first second** (no click at start, Review Focus 1), no crackle during 60 s; `dmesg | tail` without errors. Ask the user to listen.

- [ ] **Step 4: Channel identification through the device loopback, every band**

For each `RATE CH` in `44100 18`, `96000 14`, `192000 12`:

```sh
python3 tools/rme_ctl.py mix-diagonal
python3 tools/rme_ctl.py loopback 18 on
python3 tools/rme_ctl.py gen $CH $((RATE * 6)) /tmp/id.raw
aplay -D hw:$UC -t raw -f FLOAT_LE -r $RATE -c $CH /tmp/id.raw &
sleep 1; arecord -D hw:$UC -t raw -f FLOAT_LE -r $RATE -c $CH -d 3 /tmp/cap.raw; wait
python3 tools/rme_ctl.py loopback 18 off
python3 tools/rme_ctl.py check $CH /tmp/cap.raw
```

Expected: `1:1.00 2:2.00 … CH:CH.00` (identity order, unity gain) for the three bands; `cat /proc/asound/card$UC/pcm0p/sub0/hw_params` shows the requested rate while playing.

- [ ] **Step 5: Rate conflict** (Review Focus 4)

```sh
aplay -D hw:$UC -t raw -f FLOAT_LE -r 44100 -c 18 /tmp/music18.raw &
sleep 1; arecord -D hw:$UC -t raw -f FLOAT_LE -r 96000 -c 14 -d 1 /tmp/x.raw; echo "arecord exit $?"; kill %1
```

Expected: `arecord` fails (`Device or resource busy` or `Invalid argument`), playback continues cleanly at 44.1 kHz.

- [ ] **Step 6: Other USB audio devices and unload** (Review Focus 5)

With the test module loaded, play a short sound on the generic "USB Audio" card (`aplay -D plughw:<card> /usr/share/sounds/alsa/Front_Center.wav` if present). Then `! sudo kernel/unload.sh` and `! systemctl --user start pipewire.socket pipewire-pulse.socket wireplumber`.
Expected: sound OK on the other card; after unload, `/proc/asound/cards` lists the same cards as before (without "Fireface UC").

- [ ] **Step 7: Document**

In `kernel/README.md` add a `## Test procedure` section with Steps 2–6 (commands and expected results). In `docs/roadmap.md` tick the Phase 4 items that passed and record any failure verbatim. In `docs/project-layout.md` add the `kernel/` tree and `tools/rme_ctl.py`.

- [ ] **Step 8: Commit**

```bash
git add tools/rme_ctl.py kernel/README.md docs/roadmap.md docs/project-layout.md
git commit -m "kernel: hardware test procedure and results for the Fireface UC"
```

---

## Out of scope (later plans)

- ALSA mixer controls (clock source, levels, gains, phantom, matrix) — Phase 5.
- MIDI (interface 2) and level meters (interface 1).
- Upstream submission (`alsa-devel`), DKMS packaging.
