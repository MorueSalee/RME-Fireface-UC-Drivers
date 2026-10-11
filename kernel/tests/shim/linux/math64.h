#ifndef SHIM_LINUX_MATH64_H
#define SHIM_LINUX_MATH64_H
#include <linux/types.h>
static inline u64 div_u64(u64 dividend, u32 divisor)
{
	return dividend / divisor;
}
#endif
