/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __UK_INTERCEPT_H__
#define __UK_INTERCEPT_H__

#include <errno.h>

#include <uk/config.h>
#include <uk/init.h>

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_LIBINTERCEPT
int uk_intercept_boot_init(struct uk_init_ctx *ictx);
int uk_intercept_access(const char *path, int mode);
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
#endif /* CONFIG_LIBINTERCEPT */

#ifdef __cplusplus
}
#endif

#endif /* __UK_INTERCEPT_H__ */
