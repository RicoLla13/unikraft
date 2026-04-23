/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __UK_INTERCEPT_H__
#define __UK_INTERCEPT_H__

#include <stddef.h>
#include <sys/uio.h>

#include <uk/config.h>
#include <uk/init.h>
#include <uk/posix-fdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_LIBINTERCEPT
int uk_intercept_boot_init(struct uk_init_ctx *ictx);
void uk_intercept_enter_app(void);
void uk_intercept_leave_app(void);
ssize_t uk_intercept_writev(struct uk_ofile *of, int fd,
			    const struct iovec *iov, int iovcnt);
ssize_t uk_intercept_write(struct uk_ofile *of, int fd,
			   const void *buf, size_t count);
#else
static inline int uk_intercept_boot_init(struct uk_init_ctx *ictx __unused)
{
	return 0;
}

static inline void uk_intercept_enter_app(void)
{
}

static inline void uk_intercept_leave_app(void)
{
}

static inline ssize_t uk_intercept_writev(struct uk_ofile *of, int fd __unused,
					  const struct iovec *iov, int iovcnt)
{
	return uk_sys_writev(of, iov, iovcnt);
}

static inline ssize_t uk_intercept_write(struct uk_ofile *of, int fd __unused,
					 const void *buf, size_t count)
{
	return uk_sys_write(of, buf, count);
}
#endif /* CONFIG_LIBINTERCEPT */

#ifdef __cplusplus
}
#endif

#endif /* __UK_INTERCEPT_H__ */
