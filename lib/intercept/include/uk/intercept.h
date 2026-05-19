/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __UK_INTERCEPT_H__
#define __UK_INTERCEPT_H__

#include <errno.h>
#include <sys/types.h>

#include <uk/config.h>
#include <uk/init.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_LIBINTERCEPT
int uk_intercept_boot_init(struct uk_init_ctx *ictx);
int uk_intercept_access(const char *path, int mode);
int uk_intercept_openat(int dfd, const char *path, int flags, mode_t mode);
int uk_intercept_close(int fd);
ssize_t uk_intercept_read(int fd, void *buf, size_t count);
#else
static inline int uk_intercept_boot_init(struct uk_init_ctx *ictx __unused)
{
	return 0;
}

static inline int uk_intercept_access(const char *path __unused,
				      int mode __unused)
{
	return -ENOTSUP;
}

static inline int uk_intercept_openat(int dfd __unused, const char *path __unused,
				      int flags __unused, mode_t mode __unused)
{
	return -ENOTSUP;
}

static inline int uk_intercept_close(int fd __unused)
{
	return -ENOTSUP;
}

static inline ssize_t uk_intercept_read(int fd __unused, void *buf __unused,
					size_t count __unused)
{
	return -ENOTSUP;
}
#endif /* CONFIG_LIBINTERCEPT */

#ifdef __cplusplus
}
#endif

#endif /* __UK_INTERCEPT_H__ */
