#!/usr/bin/env python3
"""Dump ISO frames (76-byte blocks) from a usbmon (mmapped, linktype 220) pcapng."""
import struct, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from usbmon_ctl import blocks

def iso_packets(path, ep, ev='S'):
    data = open(path, 'rb').read()
    linktype = None
    for btype, body, le in blocks(data):
        if btype == 1:
            linktype = struct.unpack_from(le + 'H', body, 0)[0]
        if btype != 6:
            continue
        caplen = struct.unpack_from(le + 'I', body, 12)[0]
        pkt = body[20:20 + caplen]
        if len(pkt) < 64 or pkt[9] != 0 or pkt[10] != ep or chr(pkt[8]) != ev:
            continue
        ndesc = struct.unpack_from('<I', pkt, 60)[0]
        descs = [struct.unpack_from('<iII', pkt, 64 + 16 * i) for i in range(ndesc)]
        payload = pkt[64 + 16 * ndesc:]
        yield linktype, descs, payload

if __name__ == '__main__':
    path, ep = sys.argv[1], int(sys.argv[2], 0)
    ev = sys.argv[3] if len(sys.argv) > 3 else 'S'
    nmax = int(sys.argv[4]) if len(sys.argv) > 4 else 2
    for i, (lt, descs, payload) in enumerate(iso_packets(path, ep, ev)):
        if i >= nmax:
            break
        print(f"linktype={lt} ndesc={len(descs)} payload={len(payload)} descs={[(o, l) for _, o, l in descs]}")
        for f in range(min(6, len(payload) // 76)):
            w = struct.unpack_from('<19I', payload, f * 76)
            print('  ' + ' '.join(f'{x:08x}' for x in w[:7]))
