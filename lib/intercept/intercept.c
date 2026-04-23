/* SPDX-License-Identifier: BSD-3-Clause */
#include <string.h>

#include <uk/alloc.h>
#include <uk/init.h>
#include <uk/print.h>
#include <uk/intercept.h>

static int intercept_ready;
static int intercept_app_active;

static int uk_intercept_should_intercept(int fd, const struct iovec *iov,
					 int iovcnt)
{
	return intercept_ready && intercept_app_active && fd >= 1 && fd <= 2 &&
	       iov && iovcnt > 0;
}

static int uk_intercept_iov_bytes(const struct iovec *iov, int iovcnt,
				  size_t *total_len)
{
	size_t len = 0;

	for (int i = 0; i < iovcnt; ++i) {
		if (iov[i].iov_len > SIZE_MAX - len)
			return -1;
		len += iov[i].iov_len;
	}

	*total_len = len;
	return 0;
}

int uk_intercept_boot_init(struct uk_init_ctx *ictx __unused)
{
	intercept_ready = 1;
	uk_pr_info("intercept: loaded\n");
	return 0;
}

void uk_intercept_enter_app(void)
{
	intercept_app_active = 1;
}

void uk_intercept_leave_app(void)
{
	intercept_app_active = 0;
}

ssize_t uk_intercept_writev(struct uk_ofile *of, int fd, const struct iovec *iov,
			    int iovcnt)
{
	static const char prefix[] = "[test]";
	struct uk_alloc *a;
	struct iovec intercepted_iov[2];
	uint8_t *payload;
	size_t total_len;
	size_t copied;
	ssize_t written;

	if (!uk_intercept_should_intercept(fd, iov, iovcnt))
		return uk_sys_writev(of, iov, iovcnt);

	if (uk_intercept_iov_bytes(iov, iovcnt, &total_len) < 0)
		return uk_sys_writev(of, iov, iovcnt);

	a = uk_alloc_get_default();
	if (!a)
		return uk_sys_writev(of, iov, iovcnt);

	payload = uk_malloc(a, total_len ? total_len : 1);
	if (!payload)
		return uk_sys_writev(of, iov, iovcnt);

	copied = 0;
	for (int i = 0; i < iovcnt; ++i) {
		memcpy(payload + copied, iov[i].iov_base, iov[i].iov_len);
		copied += iov[i].iov_len;
	}

	intercepted_iov[0].iov_base = (void *)prefix;
	intercepted_iov[0].iov_len = sizeof(prefix) - 1;
	intercepted_iov[1].iov_base = payload;
	intercepted_iov[1].iov_len = total_len;

	written = uk_sys_writev(of, intercepted_iov, 2);
	uk_free(a, payload);

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

uk_late_initcall(uk_intercept_boot_init, 0);
