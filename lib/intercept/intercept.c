/* SPDX-License-Identifier: BSD-3-Clause */
#include <uk/init.h>
#include <uk/print.h>
#include <uk/intercept.h>

#include "intercept_internal.h"

static int intercept_ready;
static int intercept_app_active;

static int uk_intercept_should_intercept(int fd, const struct iovec *iov,
					 int iovcnt)
{
	return intercept_ready && intercept_app_active && fd >= 1 && fd <= 2 &&
	       iov && iovcnt > 0;
}

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

void uk_intercept_enter_app(void)
{
	intercept_app_active = 1;
}

void uk_intercept_leave_app(void)
{
	intercept_app_active = 0;
}

ssize_t uk_intercept_writev(struct uk_ofile *of, int fd,
			    const struct iovec *iov, int iovcnt)
{
	static const char prefix[] = "[test]";
	struct iovec intercepted_iov[iovcnt + 1];
	ssize_t written;

	if (!uk_intercept_should_intercept(fd, iov, iovcnt))
		return uk_sys_writev(of, iov, iovcnt);

	intercepted_iov[0].iov_base = (void *)prefix;
	intercepted_iov[0].iov_len = sizeof(prefix) - 1;
	for (int i = 0; i < iovcnt; ++i)
		intercepted_iov[i + 1] = iov[i];

	written = uk_sys_writev(of, intercepted_iov, iovcnt + 1);
	if (written > 0)
		(void)uk_intercept_transport_send_string("hello");

	if (written > 0) {
		if ((size_t)written <= sizeof(prefix) - 1)
			return 0;
		return written - (ssize_t)(sizeof(prefix) - 1);
	}

	return written;
}

ssize_t uk_intercept_write(struct uk_ofile *of, int fd, const void *buf,
			   size_t count)
{
	struct iovec iov;

	iov.iov_base = (void *)buf;
	iov.iov_len = count;
	return uk_intercept_writev(of, fd, &iov, 1);
}

uk_late_initcall(uk_intercept_boot_init, uk_intercept_boot_term);
