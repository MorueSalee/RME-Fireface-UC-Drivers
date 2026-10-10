# USB Captures (`cap/`)

Wireshark captures (`.pcapng`) of the traffic between macOS (driver v2.20, in a VM)
and the Fireface UC, recorded with **usbmon** on the Linux host. File names describe
the action performed in TotalMix FX / Fireface USB Settings during the capture.

The descriptions below are inferred from the file names and sizes; check the capture
itself when in doubt.

Very small files (~668 bytes) were filtered down to the one or two control transfers
caused by the action; large ones contain full sessions including ISO audio.

## Opening them

```sh
wireshark cap/set_sample_rate_96000Hz.pcapng
# or
tshark -r cap/pluginit_full.pcapng -Y 'usb.transfer_type == 0x02'   # control only
```

Useful display filters:

| Filter | Shows |
| ------ | ----- |
| `usb.transfer_type == 0x02` | control transfers |
| `usb.transfer_type == 0x00` | isochronous (audio) |
| `usb.transfer_type == 0x03` | bulk (meters, MIDI) |
| `usb.setup.bRequest == 0x1a` | one specific vendor request |
| `usb.endpoint_address == 0x01` | playback stream |

Decode the setup fields with [control-requests.md](protocol/control-requests.md).

## Index

### Plug-in / initialisation

| File | Content |
| ---- | ------- |
| `pluginit_full.pcapng` | Full enumeration + init when plugging the device. |
| `pluginit_full2_after_TotalMix_Reset.pcapng` | Same, after a TotalMix reset (default mixer state). |
| `pluginit_full3_outputsLevels-50-40-30-20-10-0_.pcapng` | Init with output levels set to -50/-40/-30/-20/-10/0 dB. |
| `pluginit_full3_outputsLevels-50-40-30-20-10-0+inputsGains-11-11-2-2_.pcapng` | Same + input gains 11/11/2/2. |
| `pluginit_0x1a_v1..v4.pcapng` | Only the `0x1a` gain/level packets of the init, several runs. |
| `pluginit_set_volume_IN1_unplug.pcapng` | Init, change IN1 volume, unplug. |

### Playback / recording start-stop

| File | Content |
| ---- | ------- |
| `start_stop_playinit_PB12-OUT12.pcapng` | Start/stop playback, playback 1-2 → out 1-2. |
| `start_stop_playinit_PB12-OUT34.pcapng` | Start/stop playback, playback 1-2 → out 3-4. |
| `start_stop_recinit_IN1.pcapng` | Start/stop recording on IN1. |
| `play_stop_full-SPLIT.pcapng`, `.pcapng1`, `.pcapng2` | Long play/stop session split in 3 parts (~130 MB). |
| `play_stop_full_with_SPDIF_OPT.pcapng` | Play/stop with optical SPDIF. |
| `play_stop_full_with_SPDIF_COAX_OPT.pcapng` | Play/stop with coax + optical SPDIF. |
| `tmp.pcapng` | Scratch capture. |

### `cap/play/` — audio payload analysis

| File | Content |
| ---- | ------- |
| `play_square_22050Hz_out_1.pcapng`, `_out_2`, `_out_3`, `_out_1-2`, `_out_3-4` | 22050 Hz square wave on given output(s) — locate channels in the frame. |
| `play_square_22050Hz_pb_1.pcapng`, `_pb_2` | Same, on playback channel 1 / 2. |
| `play_music_pb_1-2_out_1-2.pcapng` | Music on playback 1-2 → out 1-2. |
| `software_select_audio_IN1.pcapng` | Selecting IN1 as input in macOS. |
| `software_select_audio_output.pcapng` | Selecting the device as audio output in macOS. |
| `46080.pcapng` | Rate change to 46080 Hz. |

### Sample rate

`set_sample_rate_<rate>.pcapng` for 32000, 42336, 44056, 44100, 44144, 45937, 46080,
47952, 48000, 48048, 50000, 64000, 88200, 96000 (`set_sample_rate_96000Hz`),
128000, 176400 and 192000 (`set_sample_rate_192k`) Hz. The non-standard values show
how `hwSetSampleRate()` encodes the period; see
[controls.md](protocol/controls.md#set-sample-rate).

### Settings (Fireface USB Settings)

| Files | Setting | Doc |
| ----- | ------- | --- |
| `set_clock_source-INTERNAL / -SPDIF_IN / -OPTICAL_IN / -WC_IN` | Clock source | [settings.md](protocol/settings.md#clock-source-bits-02) |
| `set_line_in+4dbu / -10dbv / -low_gain` | Input level | [settings.md](protocol/settings.md#input-level-bits-89) |
| `set_line_out+4dbu / -10dbv / -hi_gain` | Output level | [settings.md](protocol/settings.md#output-level-bits-67) |
| `set_phones+4dbu / -10dbv / -hi_gain` | Phones level | [settings.md](protocol/settings.md#phones-level-bits-1011) |
| `set_optical_ADAT / _SPDIF` | Optical format | [settings.md](protocol/settings.md#optical-adat--spdif-bit-10-0x400) |
| `set_spdif_consumer / _professional` | Coax SPDIF / AES | [settings.md](protocol/settings.md#coax-aes--spdif-bit-7-0x80) |
| `set_wordclock_single_speed` | WC single speed | [settings.md](protocol/settings.md#single-speed-bit-6-0x40) |

## Ignored

`cap/play_stop_full.pcapng` is in `.gitignore` (too big; it was split into the
`-SPLIT` files).
