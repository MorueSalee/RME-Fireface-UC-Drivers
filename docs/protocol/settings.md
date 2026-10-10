# Settings

The parameters shown in the **Fireface USB Settings** application form a separate
group: whenever one changes, **all of them** are re-sent as bit flags in the
`wValue` of two control transfers, built by
[`hwWriteSettings_uc()`](../../asm/FirefaceUSBSettings/hwWriteSettings_uc.c).

The settings structure is known from the `print_settings()` function of the
application:

```c
struct FirefaceSettings {
    unsigned int ClockSource;   /* 0 internal, 1 word clock, 2 ADAT, 3 SPDIF */
    int SingleSpeed;
    int SpdifOptical;           /* optical port in SPDIF mode instead of ADAT */
    int Professional;           /* coax in AES (professional) mode */
    signed int InputLevel;
    int OutputLevel;
    int PhonesLevel;
    int SpdifInput;
    int BypassDSP;
    int UNKNOWN;
};
```

> Bit numbers below are **0-based** (bit 0 = `0x0001`). The former README used
> 1-based numbering.

## Transfer 1 — analog reference levels (`FLAGS_1`)

```
bmRequestType    : 0x40
bRequest         : 0x17 (23)
wValue           : FLAGS_1
wIndex           : 0x0fc0          (mask: bits 6-11)
wLength          : 0x0000
```

### Input level (bits 8–9)

| Level    | bit 9 | bit 8 |
| -------- | ----- | ----- |
| +4 dBu   | 0     | 1     |
| -10 dBV  | 1     | 1     |
| Low Gain | 0     | 0     |

### Output level (bits 6–7)

| Level    | bit 7 | bit 6 | Value  |
| -------- | ----- | ----- | ------ |
| +4 dBu   | 1     | 1     | `0xc0` |
| -10 dBV  | 0     | 1     | `0x40` |
| Hi Gain  | 1     | 0     | `0x80` |

### Phones level (bits 10–11)

| Level    | bit 11 | bit 10 | Value   |
| -------- | ------ | ------ | ------- |
| +4 dBu   | 1      | 1      | `0xc00` |
| -10 dBV  | 1      | 0      | `0x800` |
| Hi Gain  | 0      | 1      | `0x400` |

## Transfer 2 — digital I/O and sync (`FLAGS_2`)

```
bmRequestType    : 0x40
bRequest         : 0x10 (16)
wValue           : FLAGS_2         (always ORed with 0x4200)
wIndex           : 0x46cf          (0x47cf on Fireface UCX)
wLength          : 0x0000
```

### Clock source (bits 0–2)

| Source        | bit 2 | bit 1 | bit 0 | Value |
| ------------- | ----- | ----- | ----- | ----- |
| Internal      | 0     | 0     | 1     | `0x1` |
| SPDIF in      | 0     | 1     | 0     | `0x2` |
| ADAT in       | 1     | 0     | 0     | `0x4` |
| Word Clock in | 1     | 1     | 0     | `0x6` |

### Single speed (bit 6, `0x40`)

The word-clock output normally runs at the sample rate. *Single speed* forces it
to the base rate: at 96 kHz or 192 kHz the WC output stays at 48 kHz.

### Coax AES / SPDIF (bit 7, `0x80`)

Selects the coaxial output format: `1` = **AES** (professional), `0` = **SPDIF**
(consumer).

### Optical ADAT / SPDIF (bit 10, `0x400`)

Selects the optical in/out format: `1` = **SPDIF**, `0` = **ADAT**.

## Status register (`0x11`)

Read with `0xc0 0x11 0 0 4`. Decoded by
[`CHardware::GetStatus()`](../../asm/FirefaceUSBSettings/CHardware::GetStatus.c)
(Fireface USB Settings) and by [`hwGetRate()`](../../asm/FirefaceUSB/hwGetRate.c).

| Bits    | Field           | Meaning |
| ------- | --------------- | ------- |
| 0 / 4   | coax lock / sync | `0` no lock, `1` lock, `2` sync (both bits) |
| 1 / 5   | optical lock / sync | idem |
| 2 / 6   | word clock lock / sync | idem |
| 1 & 8   | `ff_status_0x04` | `2` when optical is plugged in (bit 1 only), `1` if bits 1 and 8 |
| 9–11    | `clock_src`     | `7` internal, `1` SPDIF, `2` optical, `3` word clock (`5` if source = 2 and bit 8 set) |
| 12–15   | `coax_freq_id`  | frequency ID of the coax input (`12` = none) |
| 16–19   | `opti_freq_id`  | frequency ID of the optical input |
| 20–23   | `word_freq_id`  | frequency ID of the word-clock input |
| 24–25   | base rate       | `0` 32 kHz, `1` 44.1 kHz, `2/3` 48 kHz |
| 26 / 27 | multiplier      | ×2 / ×4 |

Frequency IDs: see [`ufx_id_to_freq()`](../../asm/FirefaceUSB/ufx_id_to_freq.c)
and [controls.md – Get Sample Rate](controls.md#get-sample-rate).
