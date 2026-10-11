#!/usr/bin/env python3
"""Read a capture dump written by the prototype (RME_DUMP_IN=<file>).

Format: for each packet, a uint16 length then the packet bytes.
Packet layout: 4-byte header, then 18 planar channel blocks of n float32 samples
(see docs/protocol/audio-streaming.md).

Usage: capture_dump.py [--channels N] <file> [channel ...]
       N = channels in the stream: 18 (alt 1), 14 (alt 2), 12 (alt 3); default 18.
       Channels to print default to 1 2.
"""
import collections
import struct
import sys

CHANNELS = 18


def packets(path):
    data = open(path, 'rb').read()
    off = 0
    while off + 2 <= len(data):
        (length,) = struct.unpack_from('<H', data, off)
        yield data[off + 2:off + 2 + length]
        off += 2 + length


def split(pkt, channels=CHANNELS):
    """Return (header, frames, [samples of channel 1 .. channels])."""
    n = (len(pkt) - 4) // (channels * 4)
    header = struct.unpack_from('<I', pkt)[0]
    samples = struct.unpack_from(f'<{channels * n}f', pkt, 4)
    return header, n, [samples[c * n:(c + 1) * n] for c in range(channels)]


def main():
    args = sys.argv[1:]
    channels = CHANNELS
    if args[:1] == ['--channels']:
        channels, args = int(args[1]), args[2:]
    path = args[0]
    wanted = [int(c) for c in args[1:]] or [1, 2]
    sizes = collections.Counter()
    for i, pkt in enumerate(packets(path)):
        header, n, chans = split(pkt, channels)
        sizes[n] += 1
        if i < 6:
            print(f'header 0x{header:08x} (ts {header & 0xffff:5d})  frames {n}  ' +
                  '  '.join(f'ch{c}: ' + ' '.join(f'{v:+.4f}' for v in chans[c - 1]) for c in wanted))
    print('frames per packet:', dict(sizes))


if __name__ == '__main__':
    main()
