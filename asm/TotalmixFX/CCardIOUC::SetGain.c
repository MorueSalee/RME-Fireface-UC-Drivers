
#include <stdio.h>
#include <stdint.h>

#include "../libusb_test/usb.h"


int setVolume( libusb_device_handle *dev, uint32_t channelID, int8_t channelType, uint32_t outputID, int32_t level )
{

/*
	CCardIOUC::SetGain( int,	rdi	this pointer ?
			    int, 	rsi	channelID
			    int, 	rdx	channelType
							0 = input
							1 = playback
							2 = output
			    int		rcx	outputID (0 - 17)
					r8	value
			  )

	0x100061ccc	push	rbp
	0x100061ccd	mov	rbp, rsp
	0x100061cd0	push	rbx
	0x100061cd1	sub	rsp, 0x28
*/

/*
	0x100061cd5	mov	rbx, qword [ds:_got__stack_chk_guard]
	0x100061cdc	mov	rbx, qword [ds:rbx]
	0x100061cdf	mov	qword [ss:rbp+var_10], rbx
*/
	// Stack Smashing Protector stuff ?

/*
	0x100061ce3	cmp	edx, 0x2
	0x100061ce6	jne	0x100061cf0
*/
	if ( channelType == 0x2 ) {	// OUTPUT
/*
		0x100061ce8	add	ecx, 0x3e0
		0x100061cee	jmp	0x100061cfc
*/
		outputID += 0x3e0;	// 992

	} else {			// INPUT (0x00) & PLAYBACK (0x01)
/*
		0x100061cf0	lea	eax, qword [ds:rdx+rdx*8]
		0x100061cf3	lea	eax, qword [ds:rsi+rax*2]
		0x100061cf6	lea	ecx, qword [ds:rcx+rcx*8]
		0x100061cf9	lea	ecx, qword [ds:rax+rcx*4]
*/
		uint32_t eax = channelID + (channelType * 9) * 2;	// ch + 18 * type
		outputID = eax + (outputID * 9) * 4;			// + 36 * out

		/*
			Checked against cap/pluginit_set_volume_IN1_unplug.pcapng :
			IN1 (type 0, ch 0) -> out 7 / out 8 gives wIndex 216 / 252.
		*/
	}

/*
	0x100061cfc	mov	byte [ds:rdi+0x2a0], 0x1   	; class stuff
	0x100061d03	mov	eax, r8d
	0x100061d06	sar	eax, 0x2
	0x100061d09	cdqe
	0x100061d0b	mov	qword [ss:rbp-0x20], rax	; wValue
*/
	/*
		level is a linear gain, 0x8000 = 0 dB (e.g. -10 dB = 10362).
		Its 2 low bits go to wIndex bits 14-15 (see below), the rest to wValue.
		Checked against cap/pluginit_full3_outputsLevels-*.pcapng :
		-10 dB -> wValue 0x0a1e, wIndex 0x83e8.
	*/
	uint16_t wValue = (uint16_t)(level >> 0x2);

/*
	0x100061d0f	and	ecx, 0x3fff
	0x100061d15	and	r8d, 0x3
	0x100061d19	shl	r8d, 0xe
	0x100061d1d	or	r8d, ecx
*/
	outputID &= 0x3fff;
	channelID = ((level & 0x3) << 0xe) | outputID;

/*
	0x100061d20	mov	qword [ss:rbp-0x18], r8		; wIndex
	0x100061d24	mov	dword [ss:rbp-0x24], 0x0
	0x100061d2b	mov	edi, dword [ds:rdi+0x2a4]	; argument "connection" for method
	0x100061d31	lea	rdx, qword [ss:rbp-0x20]	; argument "input"      for method
	0x100061d35	lea	r9, qword [ss:rbp-0x24]		; argument "outputCnt"  for method
	0x100061d39	mov	esi, 0x3			; argument "selector"   for method
	0x100061d3e	mov	ecx, 0x2			; argument "inputCnt"   for method
	0x100061d43	xor	r8d, r8d			; argument "output"     for method
	0x100061d46	call	imp___stubs__IOConnectCallScalarMethod
	0x100061d4b	test	eax, eax
	0x100061d4d	sete	al
	0x100061d50	cmp	rbx, qword [ss:rbp-0x10]
	0x100061d54	jne	0x100061d60

	0x100061d56	movzx	eax, al
	0x100061d59	add	rsp, 0x28
	0x100061d5d	pop	rbx
	0x100061d5e	pop	rbp
	0x100061d5f	ret

	0x100061d60	call	imp___stubs____stack_chk_fail
			; endp
	0x100061d65	nop
*/

	ctrl_setup ctrl;

	ctrl.bmRequestType = 0x40;
	ctrl.bRequest = 0x12;
	ctrl.wValue = wValue;
	ctrl.wIndex = channelID;
	ctrl.wLength = 0x0000;

	send_ctrl_setup( dev, &ctrl, NULL );

	return 0;
}
