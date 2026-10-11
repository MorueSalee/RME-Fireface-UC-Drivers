#ifndef SHIM_ASM_BYTEORDER_H
#define SHIM_ASM_BYTEORDER_H
/* the tests run on little-endian x86 */
#define cpu_to_le32(x) ((__le32)(x))
#endif
