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
