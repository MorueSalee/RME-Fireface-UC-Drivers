#ifndef SHIM_LINUX_ERRNO_H
#define SHIM_LINUX_ERRNO_H
/*
 * Not a wrapper of <errno.h>: glibc's <errno.h> itself includes <linux/errno.h>,
 * which would resolve to this file.
 */
#ifndef EINVAL
#define EINVAL 22
#endif
#endif
