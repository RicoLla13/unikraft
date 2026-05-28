/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __UK_INTERCEPT_H__
#define __UK_INTERCEPT_H__

#include <errno.h>
#include <sys/stat.h>
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
int uk_intercept_fstat(int fd, struct stat *statbuf);
int uk_intercept_newfstatat(int dfd, const char *path, struct stat *statbuf,
			    int flags);
off_t uk_intercept_lseek(int fd, off_t offset, int whence);
ssize_t uk_intercept_read(int fd, void *buf, size_t count);
ssize_t uk_intercept_write(int fd, const void *buf, size_t count);
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

static inline int uk_intercept_fstat(int fd __unused,
				     struct stat *statbuf __unused)
{
	return -ENOTSUP;
}

static inline int uk_intercept_newfstatat(int dfd __unused,
					  const char *path __unused,
					  struct stat *statbuf __unused,
					  int flags __unused)
{
	return -ENOTSUP;
}

static inline off_t uk_intercept_lseek(int fd __unused, off_t offset __unused,
				       int whence __unused)
{
	return -ENOTSUP;
}

static inline ssize_t uk_intercept_read(int fd __unused, void *buf __unused,
					size_t count __unused)
{
	return -ENOTSUP;
}

static inline ssize_t uk_intercept_write(int fd __unused,
					 const void *buf __unused,
					 size_t count __unused)
{
	return -ENOTSUP;
}
#endif /* CONFIG_LIBINTERCEPT */

#ifdef __cplusplus
}
#endif

#endif /* __UK_INTERCEPT_H__ */
