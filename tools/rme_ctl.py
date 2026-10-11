#!/usr/bin/env python3
"""Fireface UC test helper, usable while snd-usb-audio is bound to the device.

Control requests on endpoint 0 do not need to claim an interface, so they can be
sent while the kernel driver streams audio.

  rme_ctl.py loopback <n> on|off      loopback on outputs 1..n (0x40 0x15)
  rme_ctl.py mute on|off              hardware mute of all outputs (0x13 / 0x14)
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


def cmd_mute(state):
    lib, handle = open_device()
    control(lib, handle, 0x13 if state == 'on' else 0x14, 0xffff, 0xc000)
    print(f'hardware mute {state}')


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
    commands = {'loopback': cmd_loopback, 'mute': cmd_mute, 'mix-diagonal': cmd_mix_diagonal,
                'gen': cmd_gen, 'check': cmd_check, 'mono18': cmd_mono18}
    if len(sys.argv) < 2 or sys.argv[1] not in commands:
        sys.exit(__doc__)
    commands[sys.argv[1]](*sys.argv[2:])
