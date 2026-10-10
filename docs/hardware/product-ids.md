# RME USB Product IDs

All RME USB interfaces share vendor ID `0x0424` (Standard Microsystems Corp., the
USB controller vendor). The official macOS driver (`FirefaceUSB.kext`) supports the
whole family and branches on the product ID; this is why most reimplemented
functions in [`asm/`](../../asm) start with `uint16_t productId = 0x3fc6;`.

| ProductID | RME USB Product |
| --------- | --------------- |
| 0x3fc0    | Babyface Pro    |
| 0x3fc1    | UFX+            |
| 0x3fc2    | UFX+            |
| 0x3fc3    | MADIface Pro    |
| 0x3fc4    | UFX II          |
| 0x3fc5    | *undefined*     |
| 0x3fc6    | Fireface UC     |
| 0x3fc7    | Babyface        |
| 0x3fc8    | Fireface UFX    |
| 0x3fc9    | Fireface UCX    |
| 0x3fca    | MADIface XT     |
| 0x3fcb    | MADIface XT     |
| 0x3fcc    | MADIface        |
| 0x3fcd    | Fireface 802    |
| 0x3fce    | *undefined*     |
| 0x3fcf    | Fireface UC2    |
|           |                 |
| 0x3fa0    | Digiface USB    |

Retrieved from [`getDeviceNameByTypeId()`](../../asm/FirefaceUSBSettings/getDeviceNameByTypeId.c)
(Fireface USB Settings), which switches on `productId - 0x3fc0` (range 0–15) plus a
special case for `0x3fa0`.

## Note about `0x3fc5` / `0x3fc6`

The Fireface UC exposes **two product IDs depending on its operating mode**:

* `0x3fc5` — **PC mode** (Windows): audio uses BULK endpoints.
  `getDeviceNameByTypeId()` reports it as *undefined*.
* `0x3fc6` — **AP mode** (Mac): audio uses ISOCHRONOUS endpoints.
  **This is the only mode targeted by this project.**

See [device-descriptor.md](device-descriptor.md) for how to switch modes.

## Product-ID tricks in the disassembly

The compiler turned many product checks into arithmetic; recognising them makes the
`asm/` files much easier to read:

| Expression in code                               | Meaning                                  |
| ------------------------------------------------ | ---------------------------------------- |
| `(productId + 0xffffc036) & 0xffff < 3`          | `productId` in `0x3fca..0x3fcc` (MADIface XT / MADIface) |
| `(productId + 0xffffc03f) & 0xffff < 2`          | `productId` in `0x3fc1..0x3fc2` (UFX+)   |
| `(productId + 0xffffc038) & 0xffff < 2`          | `productId` in `0x3fc8..0x3fc9` (UFX / UCX) |
| `(productId + 0xffffc040)` used as switch index  | `productId - 0x3fc0`                     |
| `(0x1a00000001 >> (productId - 0x3fa0)) & 1`     | bitmap test: `0x3fa0`, `0x3fc1`, `0x3fc3`, `0x3fc4` (bits 0, 33, 35, 36) |
