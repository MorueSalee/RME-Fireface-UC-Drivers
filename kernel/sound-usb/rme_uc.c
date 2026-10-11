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
