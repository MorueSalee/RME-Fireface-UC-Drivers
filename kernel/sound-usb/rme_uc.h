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
