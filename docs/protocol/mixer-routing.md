# Mixer and Routing (DSP matrix)

The DSP behaves like a **matrix mixer**. Each hardware input and each virtual
playback channel has its own gain towards each physical output. Setting the value
at a given index raises/lowers a level or opens/closes a route.

The **routing, pan, mute, solo and stereo-link** features of TotalMix are entirely
implemented in software by TotalMix playing with these gains — the DSP knows
nothing about them.

## Matrix indexes

```
             out_1  out_2  out_3  out_4  out_5  out_6  out_7  out_8 ...

in MIC__1       0     36     72    108    144    180    216    252  ...
in MIC__2       1     37     73    109    145    181    217    253  ...
...            ..     ..     ..    ...    ...    ...    ...    ...  ...
in ADAT_7      16     52     88    124    160    196    232    268  ...
in ADAT_8      17     53     89    125    161    197    233    269  ...

pb AN___1      18     54     90    126    162    198    234    270  ...
pb AN___2      19     55     91    127    163    199    235    271  ...
...            ..     ..     ..    ...    ...    ...    ...    ...  ...
pb ADAT_7      34     70    106    142    178    214    250    286  ...
pb ADAT_8      35     71    107    143    179    215    251    287  ... up to 647
```

18 inputs + 18 playbacks = 36 rows per output; 18 outputs → indexes `0..647`.

## Output (master) volumes

Output faders use indexes `992..1009` (`0x3e0 + output`):

```
out AN___1      992
out AN___2      993
out AN___3      994
out AN___4      995
out AN___5      996
out AN___6      997
out AN___7      998
out AN___8      999
out SPDF_L     1000
out SPDF_R     1001
out AS___1     1002
out AS___2     1003
out AD___3     1004
out AD___4     1005
out AD___5     1006
out AD___6     1007
out AD___7     1008
out AD___8     1009
```

## Index encoding on the wire

Indexes seen in USB captures are sometimes much higher than the table above. This
comes from TotalMix `CCardIOUC::SetGain(this, channelID, channelType, outputID, level)`
(`channelType`: 0 = input, 1 = playback, 2 = output), which packs the 2 low bits
of the level into the top of `wIndex`. What the disassembly does
(`0x100061cd5`–`0x100061d1d` in [`TotalmixFX.asm`](../../asm/TotalmixFX.asm)):

```c
if ( channelType == 2 )                          /* output fader       */
    index = 0x3e0 + outputID;                    /* 992 + n            */
else                                             /* input / playback   */
    index = 36 * outputID + 18 * channelType + channelID;   /* matrix cell */

wValue = level >> 2;                             /* sar eax, 2         */
wIndex = ((level & 0x3) << 14) | (index & 0x3fff);
/* -> 0x40 0x12 wValue wIndex 0 */
```

The level is thus an 18-bit value split over `wValue` (bits 2–17) and `wIndex`
(bits 14–15) — which is why captured `wIndex` values can exceed 16384.

> **Known discrepancies** in the C reimplementation `setVolume()`
> ([`CCardIOUC::SetGain.c`](../../asm/TotalmixFX/CCardIOUC::SetGain.c)) versus the
> assembly above:
>
> * it computes `outputID * 13 + channelType * 11 + channelID`, whereas the
>   `lea` sequence computes `outputID * 36 + channelType * 18 + channelID`
>   (`rdx+rdx*8` then `rsi+rax*2` → `18·type + ch`; `rcx+rcx*8` then `rax+rcx*4`
>   → `+ 36·out`), which matches the matrix table;
> * it sends `wValue = level` instead of `level >> 2`.
>
> See [roadmap.md](../roadmap.md).

See [controls.md](controls.md#set-input--playback-volume) for the control transfer.
