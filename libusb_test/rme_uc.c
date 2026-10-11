
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <libusb-1.0/libusb.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>	// sleep()

#include "rme_uc.h"
#include "usb.h"
#include "asm.h"



#define EP_ISO_OUT	0x01

static int do_exit = 0;
static struct libusb_device_handle *devh = NULL;

static unsigned long /*num_bytes = 0,*/ num_xfer = 0;

#define ISO_PACKETS	8	/* 1 ms of audio per transfer (8 microframes) */
#define ISO_OUT_XFERS	4	/* transfers queued ahead : ~4 ms of latency */

/*
	Stream layout per alternate setting, from the wMaxPacketSize of EP 0x01 / 0x82
	(playback frame = counter word + channels, capture packet = 4 + frames x channels) :

	  alt 1   32 -  48 kHz   18 channels    532 = 7 x 19 x 4    508 = 4 + 7 x 18 x 4
	  alt 2   64 -  96 kHz   14 channels    780 = 13 x 15 x 4   732 = 4 + 13 x 14 x 4
	  alt 3  128 - 192 kHz   12 channels   1304 >= 25 x 13 x 4 1208 = 4 + 25 x 12 x 4
*/
struct stream_cfg {
	int alt;
	int channels;
	int max_pkt_frames;
	int in_pkt_sz;
};

static const struct stream_cfg stream_cfgs[] = {
	{ 1, 18,  7,  508 },
	{ 2, 14, 13,  732 },
	{ 3, 12, 25, 1208 },
};

#define MAX_CHANNELS	18
#define MAX_PKT_FRAMES	25
#define MAX_IN_PKT_SZ	1208

static const struct stream_cfg *cfg = &stream_cfgs[0];
static uint32_t stream_rate = 44100;	/* RME_RATE=<Hz> */

static struct libusb_transfer *iso_out_xfr[ISO_OUT_XFERS];
static uint32_t iso_out_buf[ISO_OUT_XFERS][ISO_PACKETS * MAX_PKT_FRAMES * ( MAX_CHANNELS + 1 )];
static int iso_out_in_flight = 0;

static int fill_iso_out( struct libusb_transfer *xfr );

/*
	Capture stream, read only to measure the device clock against the playback
	stream (implicit feedback study). 1 ms per transfer, max packet size of alt 1.
*/
#define EP_ISO_IN	0x82
#define ISO_IN_XFERS	4

static struct libusb_transfer *iso_in_xfr[ISO_IN_XFERS];
static uint8_t iso_in_buf[ISO_IN_XFERS][ISO_PACKETS * MAX_IN_PKT_SZ];
static int iso_in_in_flight = 0;

static unsigned long long frames_out = 0;	/* frames submitted on EP 0x01 */
static unsigned long long frames_in  = 0;	/* frames received on EP 0x82  */



uint32_t unkId = 0x16;
static int no_counter = 0;	/* RME_NO_COUNTER=1 : send 0 instead of the frame counter (test) */
static int id_channels = 0;	/* RME_ID_CHANNELS=1 : channel n plays a sawtooth of amplitude 0.002 * n (test) */
static unsigned long long id_frame = 0;
static FILE *dump_in = NULL;	/* RME_DUMP_IN=<file> : raw capture packets (test) */
static unsigned long dumped_in = 0;
#define DUMP_IN_SKIP	88200	/* start dumping after 2 s */
#define DUMP_IN_PACKETS	4000	/* 0.5 s of capture packets */



/*
	Implicit feedback : the device has no feedback endpoint, its capture stream
	is the clock reference. Each capture packet tells how many frames the device
	produced during that microframe (5 or 6 at 44.1 kHz); each playback packet must
	carry the same number of frames, otherwise the device buffer drifts
	(measured : ~32 ppm, heard as crackles).

	cb_xfr_in() pushes the frame count of every capture packet here,
	next_out_packet_frames() pops them when building playback packets.
*/
#define FB_FIFO_SZ	256	/* power of 2 */

static uint8_t  fb_fifo[FB_FIFO_SZ];
static uint32_t fb_head = 0;	/* written by cb_xfr_in() */
static uint32_t fb_tail = 0;	/* read by fill_iso_out() */
static uint8_t  fb_last = 5;
static unsigned long fb_misses = 0;



static void fb_push( uint8_t frames )
{
	if ( fb_head - fb_tail < FB_FIFO_SZ )
	{
		fb_fifo[fb_head++ % FB_FIFO_SZ] = frames;
	}
}



/*
	Frames to put in the next playback packet : the size of the oldest unused
	capture packet. Until the capture stream delivers its first packets (start-up),
	or if a capture transfer was lost, fall back to the 5 / 6 alternation, which
	keeps the nominal rate without inventing a drift of its own.
*/
static uint8_t next_out_packet_frames( void )
{
	if ( fb_head != fb_tail )
	{
		fb_last = fb_fifo[fb_tail++ % FB_FIFO_SZ];
		return fb_last;
	}

	fb_misses++;

	/* alternate around the nominal frames per microframe (5 / 6 at 44.1 kHz) */
	uint8_t lo = stream_rate / 8000, hi = ( stream_rate + 7999 ) / 8000;
	fb_last = ( fb_last == lo ) ? hi : lo;

	return fb_last;
}



static void cb_xfr( struct libusb_transfer *xfr )
{

	if ( xfr->status == LIBUSB_TRANSFER_TIMED_OUT )
	{
		fprintf( stderr, "TX %li : TIMED_OUT\n", num_xfer );
	}

	num_xfer++;
	iso_out_in_flight--;

	if ( do_exit )
	{
		return;
	}

	if ( fill_iso_out( xfr ) != 0 )
	{
		do_exit = 1;
		return;
	}

	if ( libusb_submit_transfer( xfr ) == 0 )
	{
		iso_out_in_flight++;
	}
}



/*
	Reads exactly len bytes from stdin (a pipe may return less).
	Returns the number of bytes read, less than len only on EOF.
*/
static size_t read_full( void *dst, size_t len )
{
	size_t done = 0;

	while ( done < len )
	{
		ssize_t n = read( 0, (uint8_t *)dst + done, len - done );

		if ( n <= 0 )
		{
			break;
		}

		done += n;
	}

	return done;
}



/*
	Fills one ISO OUT transfer (8 packets, one per microframe) with the next
	audio read on stdin : mono f32le, copied to channels 1 and 2.

	Frame layout (19 words) : word 0 is the frame counter in the first frame of
	each packet and 0 in the others, words 1-18 are the channels.

	Returns 1 on EOF, 0 otherwise.
*/
static int fill_iso_out( struct libusb_transfer *xfr )
{
	uint32_t *buf = (uint32_t *)xfr->buffer;
	uint32_t pcm[ISO_PACKETS * MAX_PKT_FRAMES];
	int frame_words = cfg->channels + 1;
	uint8_t  frames[ISO_PACKETS];

	int p = 0, f = 0, total = 0, z = 0;

	for ( p = 0; p < ISO_PACKETS; p++ )
	{
		frames[p] = next_out_packet_frames();

		if ( frames[p] > cfg->max_pkt_frames )
		{
			frames[p] = cfg->max_pkt_frames;
		}

		total += frames[p];
	}

	memset( pcm, 0, sizeof( pcm ) );
	memset( buf, 0, total * frame_words * 4 );

	if ( total > 0 && read_full( pcm, total * 4 ) == 0 )
	{
		printf( "*** EOF ***\n" );
		return 1;
	}

	for ( p = 0; p < ISO_PACKETS; p++ )
	{
		for ( f = 0; f < frames[p]; f++ )
		{
			uint32_t *frame = buf + ( z * frame_words );

			/*
				Frame counter, modulo 8192 like the official driver
				(cap/play/play_music_pb_1-2_out_1-2.pcapng : 0x200f + 5 -> 0x0014).
				Wrapping elsewhere makes the position jump every 65536 frames (~1.5 s).
			*/
			if ( f == 0 && !no_counter )
			{
				frame[0] = unkId & 0x1fff;
			}

			if ( id_channels )
			{
				float ramp = (float)( id_frame++ % 100 ) / 100.0f;

				for ( int c = 0; c < cfg->channels; c++ )
				{
					float v = 0.002f * ( c + 1 ) * ramp;
					memcpy( &frame[1 + c], &v, 4 );
				}
			}
			else
			{
				frame[1] = pcm[z];
				frame[2] = pcm[z];
			}

			z++;
		}

		xfr->iso_packet_desc[p].length = frames[p] * frame_words * 4;
		unkId += frames[p];
	}

	xfr->length = total * frame_words * 4;
	frames_out += total;

	return 0;
}



/*
	Capture packets are 4 bytes + n frames of 18 channels (72 bytes),
	see docs/protocol/audio-streaming.md.
*/
static void cb_xfr_in( struct libusb_transfer *xfr )
{
	int p = 0;

	iso_in_in_flight--;

	for ( p = 0; p < xfr->num_iso_packets; p++ )
	{
		struct libusb_iso_packet_descriptor *d = &xfr->iso_packet_desc[p];

		if ( d->status == LIBUSB_TRANSFER_COMPLETED && d->actual_length >= 4 )
		{
			frames_in += ( d->actual_length - 4 ) / ( cfg->channels * 4 );
			fb_push( ( d->actual_length - 4 ) / ( cfg->channels * 4 ) );

			/* record : uint16 length, then the packet bytes */
			if ( dump_in && frames_in > DUMP_IN_SKIP && dumped_in < DUMP_IN_PACKETS )
			{
				uint16_t len = d->actual_length;

				fwrite( &len, sizeof( len ), 1, dump_in );
				fwrite( libusb_get_iso_packet_buffer_simple( xfr, p ), 1, len, dump_in );
				dumped_in++;
			}
		}
	}

	/* every 10 s of capture : the slope of (in - out) is the clock drift */
	static unsigned long long next_report = 0;

	if ( next_report == 0 )
	{
		next_report = 10ULL * stream_rate;
	}

	if ( frames_in >= next_report )
	{
		printf( "drift : t = %3llus   in - out = %+lld frames\n",
			next_report / stream_rate, (long long)( frames_in - frames_out ) );
		next_report += 10ULL * stream_rate;
	}

	if ( do_exit )
	{
		return;
	}

	if ( libusb_submit_transfer( xfr ) == 0 )
	{
		iso_in_in_flight++;
	}
}



static int start_iso_in( uint8_t ep )
{
	int i = 0;

	for ( i = 0; i < ISO_IN_XFERS; i++ )
	{
		iso_in_xfr[i] = libusb_alloc_transfer( ISO_PACKETS );

		if ( !iso_in_xfr[i] )
		{
			return -ENOMEM;
		}

		libusb_fill_iso_transfer( iso_in_xfr[i], devh, ep, iso_in_buf[i],
			sizeof( iso_in_buf[i] ), ISO_PACKETS, cb_xfr_in, NULL, 0 );

		libusb_set_iso_packet_lengths( iso_in_xfr[i], cfg->in_pkt_sz );

		if ( libusb_submit_transfer( iso_in_xfr[i] ) == 0 )
		{
			iso_in_in_flight++;
		}
	}

	return 0;
}



/*
	Keeps ISO_OUT_XFERS transfers queued, so that the host controller always has
	audio scheduled ahead while a completed transfer is being refilled
	(see cb_xfr()). A single transfer in flight leaves gaps in the bus schedule,
	heard as clicks.
*/
static int send_iso_out( uint8_t ep )
{
	int i = 0;

	for ( i = 0; i < ISO_OUT_XFERS; i++ )
	{
		iso_out_xfr[i] = libusb_alloc_transfer( ISO_PACKETS );

		if ( !iso_out_xfr[i] )
		{
			return -ENOMEM;
		}

		libusb_fill_iso_transfer( iso_out_xfr[i], devh, ep, (unsigned char *)iso_out_buf[i],
			sizeof( iso_out_buf[i] ), ISO_PACKETS, cb_xfr, NULL, 0 );

		if ( fill_iso_out( iso_out_xfr[i] ) != 0 )
		{
			do_exit = 1;	/* EOF : also stops the capture stream */
			break;
		}

		if ( libusb_submit_transfer( iso_out_xfr[i] ) == 0 )
		{
			iso_out_in_flight++;
		}
	}

	return 0;
}



// static int send_iso_in(uint8_t ep)
// {
// 	int buf_len = 4064;
// 	uint8_t buf[4064];
// 	// uint32_t buf[4064];
// 	memset((unsigned char *)buf, 0, buf_len);
//
// 	uint32_t pcm[45];				/* TODO: when back to read() */
// 	int pcm_len = 45;
//
// 	static struct libusb_transfer *xfr;
// 	int num_iso_pack = 8;
//
//
// 	xfr = libusb_alloc_transfer(num_iso_pack);
// 	if (!xfr)
// 		return -ENOMEM;
//
//
// 	libusb_fill_iso_transfer(xfr, devh, ep, buf,
// 		buf_len, num_iso_pack, cb_xfr, NULL, 0);
//
// /*
// 	xfr->iso_packet_desc[0].length = 0x01b4;
// 	xfr->iso_packet_desc[1].length = 0x016c;
// 	xfr->iso_packet_desc[2].length = 0x01b4;
// 	xfr->iso_packet_desc[3].length = 0x016c;
// 	xfr->iso_packet_desc[4].length = 0x01b4;
// 	xfr->iso_packet_desc[5].length = 0x016c;
// 	xfr->iso_packet_desc[6].length = 0x01b4;
// 	xfr->iso_packet_desc[7].length = 0x016c;
// */
//
// 	xfr->iso_packet_desc[0].length = 0x01fc;
// 	xfr->iso_packet_desc[1].length = 0x01fc;
// 	xfr->iso_packet_desc[2].length = 0x01fc;
// 	xfr->iso_packet_desc[3].length = 0x01fc;
// 	xfr->iso_packet_desc[4].length = 0x01fc;
// 	xfr->iso_packet_desc[5].length = 0x01fc;
// 	xfr->iso_packet_desc[6].length = 0x01fc;
// 	xfr->iso_packet_desc[7].length = 0x01fc;
//
// 	xfr->flags = LIBUSB_TRANSFER_FREE_TRANSFER;
//
// 	memset((unsigned char *)pcm, 0, pcm_len);
// 	memset((unsigned char *)buf, 0, buf_len);
//
//
// 	if ( read( 0, pcm, pcm_len ) == 0 ) {
// 		printf("*** EOF ***\n");
// 		do_exit = 1;
// 	}
// /*
// 	int i = 0;
// 	while ( i < buf_len/4 ) {
// 		printf("0x%04x  0x%04x  0x%04x  0x%04x  0x%04x  0x%04x  0x%04x  0x%04x\n",
// 			buf[i++], buf[i++], buf[i++], buf[i++], buf[i++], buf[i++], buf[i++], buf[i++]);
// //		i++;
// 	}
//
// //	return -1;
// 	exit(1);
// */
// //	printf("buf_len : %d\n", buf_len);
//
// 	fill_audio_data( (uint32_t*)buf, buf_len, pcm, xfr );
//
// 	gettimeofday(&tv_start, NULL);
//
// 	/* NOTE: To reach maximum possible performance the program must
// 	 * submit *multiple* transfers here, not just one.
// 	 *
// 	 * When only one transfer is submitted there is a gap in the bus
// 	 * schedule from when the transfer completes until a new transfer
// 	 * is submitted by the callback. This causes some jitter for
// 	 * isochronous transfers and loss of throughput for bulk transfers.
// 	 *
// 	 * This is avoided by queueing multiple transfers in advance, so
// 	 * that the host controller is always kept busy, and will schedule
// 	 * more transfers on the bus while the callback is running for
// 	 * transfers which have completed on the bus.
// 	 */
// 	printf("before libusb_submit_transfer()\n");
// 	libusb_submit_transfer(xfr);
// 	printf("after  libusb_submit_transfer()\n");
//
// 	return 0;
// }



static void sig_hdlr( int signum )
{
	switch ( signum )
	{
		case SIGINT:
			do_exit = 1;
			break;
	}
}


int main( int argc, char **argv )
{

	/* SHUT GCC UP */
	if ( argc > 0 ) argv[0] = argv[0];


	int rc = 0;



	rc = libusb_init( NULL );

	if ( rc < 0 )
	{
		fprintf( stderr, "Error initializing libusb: %s\n", libusb_error_name( rc ) );
		exit( 1 );
	}

	libusb_set_debug( NULL, LIBUSB_LOG_LEVEL_WARNING );

	devh = libusb_open_device_with_vid_pid( NULL, RME_UC_VENDOR, RME_UC_PRODUCT );

	if ( !devh )
	{
		devh = libusb_open_device_with_vid_pid( NULL, RME_UC_VENDOR_OLD, RME_UC_PRODUCT_OLD );
	}

	if ( !devh )
	{
		libusb_device_handle *pc = libusb_open_device_with_vid_pid( NULL, RME_UC_VENDOR, RME_UC_PRODUCT_PC );

		if ( pc )
		{
			fprintf( stderr, "Fireface UC found in PC (Win) mode : switch it to Mac mode\n" );
			libusb_close( pc );
		}

		fprintf( stderr, "Error finding USB device : Running Offline\n" );
	}

	if ( devh )
	{
		rc = libusb_claim_interface( devh, 0 );
	}

	if ( rc < 0 )
	{
		fprintf( stderr, "Error claiming interface: %s\n", libusb_error_name( rc ) );
		goto out;
	}

	if ( devh )
	{
		libusb_set_interface_alt_setting( devh, rc, 1 );
	}



	/*
		Get Hardware Revision (Firmware Version)
	*/

	uint32_t rev = 0;

	hwGetRevision( devh, &rev );
	printf( "Firmware Version : %d (0x%02x)\n", rev, rev );



	/*
		Init Hardware
	*/

	/* RME_SKIP_INIT=1 : do not send the hwInitHardware() requests (0x16 x 36, 0x10, 0x20) */
	if ( getenv( "RME_SKIP_INIT" ) )
	{
		printf( "hwInitHardware() skipped (RME_SKIP_INIT)\n" );
	}
	else
	{
		hwInitHardware( devh );
	}



	/*
		Get Sample Rate
	*/

	int rate = hwGetRate( devh, 1 );

	printf("Sample Rate : %d\n", rate);



	/*
		Get Status
		TODO: Test with RME UC
	*/

	uint32_t status = 0;

	sUserClientReadStatus( devh, &status );

	printf("Status : %08x\n", status);





	// goto out;

	goto SEND_AUDIO;


	/*
		call hwSetSampleRate()
		seems to always init with 44100 Hz :

		0x00007ec3	mov	rdi, qword [ds:r15+0x120]		; argument #1 for method
		0x00007eca	movzx	edx, word [ds:r15+0x128]		; argument #3 for method
		0x00007ed2	mov	esi, 0xac44				; argument #2 for method
		0x00007ed7	call	__Z15hwSetSampleRateP11IOUSBDevicejt
	*/

	hwSetSampleRate( devh, 44100 );


	/*
		do some stuff seen in wireshark.
		This was not found in original drivers yet..

		EDIT: NOW IT IS, MOTHERFUCKAAAAAAA !!!
	*/

	printf("\n");

	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x000a, 0x0000, 0, NULL );	// set MIC_1 GAIN to 10
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x000a, 0x0001, 0, NULL );	// set MIC_2 GAIN to 10
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0000, 0x0002, 0, NULL );	// set LIN_3 GAIN to  0
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0000, 0x0003, 0, NULL );	// set LIN_4 GAIN to  0

	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0004, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0005, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0006, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0007, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0008, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0009, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x000a, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x000b, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x000c, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x000d, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x000e, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x000f, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0010, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0011, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0012, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0013, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0014, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x002e, 0x0015, 0, NULL );

//	usleep(100000);

	// send bulk_in	> 11 EP 0x8b
	// GET_DESCRIPTORS

	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0014, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0015, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0012, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0013, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0010, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0011, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x000e, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x000f, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x000c, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x000d, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0008, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0009, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0006, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0007, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0004, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0006, 0x0005, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0027, 0x000a, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0027, 0x000b, 0, NULL );

	/* set true gain values for inputs 1,2,3,4 */
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0000, 0x0000, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0000, 0x0001, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0000, 0x0002, 0, NULL );
	send_ctrl_setup_alt( devh, 0x40, 0x1a, 0x0000, 0x0003, 0, NULL );

	// BULK_IN > 6 EP 0x8§
	// MIDI Stuff > 7 EP 0x07

	// set mixer (routes)

//		printf(" %d ms\n", clock());








/***********************************************************************************************************
					    P L A Y    A U D I O
***********************************************************************************************************/

SEND_AUDIO:

	if ( !devh )	/* offline : control requests were only traced, no audio stream possible */
	{
		goto out;
	}

	/*
		Set Sample Rate
	*/

	/*
		RME_RATE=<Hz> : the alternate setting must match the rate band, otherwise the
		device falls back to the base rate of the current band (see controls.md).
	*/
	if ( getenv( "RME_RATE" ) )
	{
		stream_rate = atoi( getenv( "RME_RATE" ) );
	}

	cfg = &stream_cfgs[ ( stream_rate >= 128000 ) ? 2 : ( stream_rate >= 64000 ) ? 1 : 0 ];

	printf( "stream : %u Hz, alt %d, %d channels\n", stream_rate, cfg->alt, cfg->channels );

	libusb_set_interface_alt_setting( devh, 0, cfg->alt );

	hwSetSampleRate( devh, stream_rate );

	/*
		Check Sample Rate was correctly set
	*/

	rate = hwGetRate( devh, 1 );

	printf("Sample Rate : %d\n", rate);



	/*
		unMute Channels
	*/

	hwMuteChannels( devh, 1 );





	/* Registers signals */

	struct sigaction sigact;

	sigact.sa_handler = sig_hdlr;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGINT, &sigact, NULL);




	/*
		NOTE: Test of GetStatus()
	*/

	// while ( !do_exit )
	// {
	// 	GetStatus( devh );
	// 	printf( "\n\n" );
    //
	// 	sleep(1);
	// }
    //
	// goto out;




	// int loop_cnt = 0;

	no_counter = ( getenv( "RME_NO_COUNTER" ) != NULL );
	id_channels = ( getenv( "RME_ID_CHANNELS" ) != NULL );

	if ( getenv( "RME_DUMP_IN" ) )
	{
		dump_in = fopen( getenv( "RME_DUMP_IN" ), "wb" );
	}

	/*
		RME_LOOPBACK=<n> : loopback on outputs 1 .. n (0x40 0x15 wValue 1 wIndex 0 .. n-1,
		as the official driver numbers outputs 0-17) : the device feeds what it plays
		back to the matching capture channels.
	*/
	/*
		RME_MIX_DIAGONAL=1 : playback k -> output k at 0 dB, every other
		playback -> output cell at -inf (input -> output cells are not touched).
		Overwrites the mix stored in the device.
	*/
	if ( getenv( "RME_MIX_DIAGONAL" ) )
	{
		for ( int out = 0; out < 18; out++ )
		{
			for ( int ch = 0; ch < 18; ch++ )
			{
				setVolume( devh, ch, 1 /* playback */, out, ( ch == out ) ? 0x8000 : 0 );
			}
		}
	}

	/*
		RME_OUT_FADERS_FROM=<n> : output faders of outputs n .. 18 at 0 dB
		(indexes 992 + out, channel type 2). Overwrites the device mix.
	*/
	if ( getenv( "RME_OUT_FADERS_FROM" ) )
	{
		for ( int out = atoi( getenv( "RME_OUT_FADERS_FROM" ) ) - 1; out >= 0 && out < 18; out++ )
		{
			setVolume( devh, 0, 2 /* output */, out, 0x8000 );
		}
	}

	int loopback = getenv( "RME_LOOPBACK" ) ? atoi( getenv( "RME_LOOPBACK" ) ) : 0;

	if ( loopback > cfg->channels )
	{
		loopback = cfg->channels;
	}
	ctrl_setup lb = { 0x40, 0x15, 0x0001, 0x0000, 0x0000 };

	if ( loopback )
	{
		for ( lb.wIndex = 0; lb.wIndex < loopback; lb.wIndex++ )
		{
			send_ctrl_setup( devh, &lb, NULL );
		}
	}

	if ( no_counter )
	{
		printf( "frame counter disabled (RME_NO_COUNTER)\n" );
	}

	start_iso_in( EP_ISO_IN );

	rc = send_iso_out( EP_ISO_OUT );

	/* transfers are refilled and resubmitted from cb_xfr() until EOF or SIGINT */
	while ( iso_out_in_flight > 0 || iso_in_in_flight > 0 )
	{
		rc = libusb_handle_events( NULL );

		if ( rc == LIBUSB_ERROR_INTERRUPTED )	/* SIGINT : stop refilling, let queued transfers drain */
		{
			do_exit = 1;
			rc = LIBUSB_SUCCESS;
		}

		if ( rc != LIBUSB_SUCCESS )
		{
			break;
		}
	}

	for ( int i = 0; i < ISO_OUT_XFERS; i++ )
	{
		libusb_free_transfer( iso_out_xfr[i] );
	}

	for ( int i = 0; i < ISO_IN_XFERS; i++ )
	{
		libusb_free_transfer( iso_in_xfr[i] );
	}

	if ( loopback )
	{
		lb.wValue = 0x0000;

		for ( lb.wIndex = 0; lb.wIndex < loopback; lb.wIndex++ )
		{
			send_ctrl_setup( devh, &lb, NULL );
		}
	}

	if ( dump_in )
	{
		fclose( dump_in );
		printf( "dumped %lu capture packets\n", dumped_in );
	}

	printf( "feedback : %lu playback packets built without a capture packet to mirror\n", fb_misses );

	printf( "frames out : %llu   frames in : %llu   in - out : %lld (%.1f ppm)\n",
		frames_out, frames_in, (long long)( frames_in - frames_out ),
		frames_out ? 1e6 * ( (double)frames_in - (double)frames_out ) / (double)frames_out : 0.0 );

	if ( rc != LIBUSB_SUCCESS )
	{
		printf( "LIBUSB ERROR (%d) : %s\n", rc, libusb_error_name( rc ) );
	}


	/*
		Mute Channels
	*/

	hwMuteChannels( devh, 0 );

//	libusb_release_interface(devh, 0);

out:
//	libusb_release_interface(devh, 0);

	if ( devh )
	{
		libusb_close( devh );
	}

	libusb_exit( NULL );

	return rc;
}
