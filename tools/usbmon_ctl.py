#!/usr/bin/env python3
"""List control SETUP packets (URB_SUBMIT, type 'S', xfer 2) from a usbmon pcapng."""
import struct, sys

def blocks(data):
    off, le = 0, '<'
    while off + 12 <= len(data):
        btype, blen = struct.unpack_from(le + 'II', data, off)
        if btype == 0x0A0D0D0A:
            bom = data[off+8:off+12]
            le = '<' if bom == b'\x4d\x3c\x2b\x1a' else '>'
            btype, blen = struct.unpack_from(le + 'II', data, off)
        yield btype, data[off+8:off+blen-4], le
        off += blen

def main(path, req_filter=None):
    data = open(path, 'rb').read()
    n = 0
    for btype, body, le in blocks(data):
        if btype != 6:   # Enhanced Packet Block
            continue
        caplen = struct.unpack_from(le + 'I', body, 12)[0]
        pkt = body[20:20+caplen]
        if len(pkt) < 48:
            continue
        ev, xfer, epnum, devnum = pkt[8], pkt[9], pkt[10], pkt[11]
        setup_flag = pkt[14]
        n += 1
        if xfer != 2 or chr(ev) != 'S' or setup_flag != 0:
            continue
        bmrt, breq, wv, wi, wl = struct.unpack_from('<BBHHH', pkt, 40)
        if req_filter is not None and breq not in req_filter:
            continue
        print(f"#{n:6d} dev{devnum} 0x{bmrt:02x} 0x{breq:02x} ({breq:2d})  wValue=0x{wv:04x}  wIndex=0x{wi:04x} ({wi:5d})  wLength={wl}")

if __name__ == '__main__':
    f = {int(x, 0) for x in sys.argv[2].split(',')} if len(sys.argv) > 2 else None
    main(sys.argv[1], f)
