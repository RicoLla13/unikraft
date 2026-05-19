/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>

#include <uk/init.h>
#include <uk/print.h>
#include <uk/intercept.h>

#include "intercept_internal.h"

static int intercept_ready;

int uk_intercept_boot_init(struct uk_init_ctx *ictx __unused)
{
	uk_intercept_transport_init();
	intercept_ready = 1;
	uk_pr_info("intercept: loaded\n");
	return 0;
}

static void uk_intercept_boot_term(struct uk_term_ctx *ctx __unused)
{
	uk_intercept_transport_term();
}

int uk_intercept_access(const char *path, int mode)
{
	int saved_errno;
	int ret;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!path)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_access(path, mode);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

uk_late_initcall(uk_intercept_boot_init, uk_intercept_boot_term);
