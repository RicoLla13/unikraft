/* SPDX-License-Identifier: BSD-3-Clause */
#include <fcntl.h>
#include <errno.h>

#include <uk/bitops/bitmap.h>
#include <uk/init.h>
#include <uk/print.h>
#include <uk/intercept.h>

#include "intercept_internal.h"

#define UK_INTERCEPT_REMOTE_FD_MAX CONFIG_LIBPOSIX_FDTAB_MAXFDS

/* Set once the transport side has been initialized during boot. */
static int intercept_ready;
static unsigned long intercept_remote_fds[UK_BITS_TO_LONGS(UK_INTERCEPT_REMOTE_FD_MAX)];

static int uk_intercept_fd_in_range(int fd)
{
	return fd >= 0 && fd < UK_INTERCEPT_REMOTE_FD_MAX;
}

static int uk_intercept_is_remote_fd(int fd)
{
	return uk_intercept_fd_in_range(fd) &&
	       uk_test_bit(fd, intercept_remote_fds);
}

int uk_intercept_boot_init(struct uk_init_ctx *ictx __unused)
{
#if CONFIG_LIBINTERCEPT_CONNECT_BOOT_BEST_EFFORT || \
	CONFIG_LIBINTERCEPT_CONNECT_BOOT_REQUIRED
	int rc;
#endif

	uk_bitmap_zero(intercept_remote_fds, UK_INTERCEPT_REMOTE_FD_MAX);
	uk_intercept_transport_init();

#if CONFIG_LIBINTERCEPT_CONNECT_BOOT_BEST_EFFORT || \
	CONFIG_LIBINTERCEPT_CONNECT_BOOT_REQUIRED
	rc = uk_intercept_rpc_probe();
	if (rc < 0) {
#if CONFIG_LIBINTERCEPT_CONNECT_BOOT_REQUIRED
		uk_pr_err("intercept: boot-time RPC probe required but failed: %d\n",
			  -rc);
		return rc;
#else
		uk_pr_warn("intercept: boot-time RPC probe failed, will retry on first RPC: %d\n",
			   -rc);
#endif
	}
#endif

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

	/*
	 * Preserve the caller-visible errno on successful remote execution.
	 * Error returns still follow the normal negative errno convention.
	 */
	saved_errno = errno;
	ret = uk_intercept_rpc_access(path, mode);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

int uk_intercept_openat(int dfd, const char *path, int flags, mode_t mode)
{
	int saved_errno;
	int remote_dfd;
	int ret;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!path)
		return -EFAULT;

	/*
	 * Absolute paths do not consult dirfd. Relative paths may only use
	 * AT_FDCWD or a descriptor previously returned by the intercept layer.
	 */
	remote_dfd = (path[0] == '/') ? AT_FDCWD : dfd;
	if (remote_dfd != AT_FDCWD && !uk_intercept_is_remote_fd(remote_dfd))
		return -EBADF;

	saved_errno = errno;
	ret = uk_intercept_rpc_openat(remote_dfd, path, flags, mode);
	if (ret < 0)
		return ret;

	if (!uk_intercept_fd_in_range(ret)) {
		(void) uk_intercept_rpc_close(ret);
		return -EMFILE;
	}

	uk_set_bit(ret, intercept_remote_fds);
	errno = saved_errno;
	return ret;
}

int uk_intercept_close(int fd)
{
	int saved_errno;
	int ret;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!uk_intercept_is_remote_fd(fd))
		return -ENOTSUP;

	saved_errno = errno;
	ret = uk_intercept_rpc_close(fd);
	if (ret < 0)
		return ret;

	uk_clear_bit(fd, intercept_remote_fds);
	errno = saved_errno;
	return ret;
}

ssize_t uk_intercept_read(int fd, void *buf, size_t count)
{
	int saved_errno;
	ssize_t ret;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!uk_intercept_is_remote_fd(fd))
		return -ENOTSUP;

	if (!buf && count)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_read(fd, buf, count);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

ssize_t uk_intercept_write(int fd, const void *buf, size_t count)
{
	int saved_errno;
	ssize_t ret;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!uk_intercept_is_remote_fd(fd))
		return -ENOTSUP;

	if (!buf && count)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_write(fd, buf, count);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

uk_late_initcall(uk_intercept_boot_init, uk_intercept_boot_term);
