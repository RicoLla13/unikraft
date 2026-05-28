/* SPDX-License-Identifier: BSD-3-Clause */
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include <uk/init.h>
#include <uk/posix-fdtab.h>
#include <uk/print.h>
#include <uk/intercept.h>

#include "intercept_internal.h"

#define UK_INTERCEPT_REMOTE_FD_MAX CONFIG_LIBPOSIX_FDTAB_MAXFDS

/* Set once the transport side has been initialized during boot. */
static int intercept_ready;
static struct uk_intercept_fd_entry
	intercept_fd_entries[UK_INTERCEPT_REMOTE_FD_MAX];

static int uk_intercept_fd_in_range(int fd)
{
	return fd >= 0 && fd < UK_INTERCEPT_REMOTE_FD_MAX;
}

static enum uk_intercept_fd_backend uk_intercept_classify_backend(int flags)
{
	return (flags & O_DIRECTORY) ? UK_INTERCEPT_FD_REMOTE_DIR
				     : UK_INTERCEPT_FD_REMOTE_FILE;
}

static enum uk_intercept_fd_backend
uk_intercept_classify_backend_from_mode(mode_t st_mode)
{
	return S_ISDIR(st_mode) ? UK_INTERCEPT_FD_REMOTE_DIR
				: UK_INTERCEPT_FD_REMOTE_FILE;
}

static bool uk_intercept_local_fd_in_use(int fd)
{
	struct uk_ofile *of;

	of = uk_fdtab_get(fd);
	if (!of)
		return false;

	uk_ofile_release(of);
	return true;
}

static int uk_intercept_fdtab_alloc_guest_fd(void)
{
	int fd;

	for (fd = 0; fd < UK_INTERCEPT_REMOTE_FD_MAX; ++fd) {
		if (uk_intercept_fdtab_contains(fd))
			continue;
		if (uk_intercept_local_fd_in_use(fd))
			continue;

		return fd;
	}

	return -EMFILE;
}

void uk_intercept_fdtab_init(void)
{
	memset(intercept_fd_entries, 0, sizeof(intercept_fd_entries));
}

void uk_intercept_fdtab_reset(void)
{
	memset(intercept_fd_entries, 0, sizeof(intercept_fd_entries));
}

struct uk_intercept_fd_entry *uk_intercept_fdtab_get(int fd)
{
	if (!uk_intercept_fd_in_range(fd))
		return NULL;
	if (!intercept_fd_entries[fd].used)
		return NULL;

	return &intercept_fd_entries[fd];
}

const struct uk_intercept_fd_entry *uk_intercept_fdtab_get_const(int fd)
{
	return uk_intercept_fdtab_get(fd);
}

bool uk_intercept_fdtab_contains(int fd)
{
	return uk_intercept_fdtab_get_const(fd) != NULL;
}

bool uk_intercept_fdtab_is_remote_dir(int fd)
{
	const struct uk_intercept_fd_entry *entry =
		uk_intercept_fdtab_get_const(fd);

	return entry && entry->backend == UK_INTERCEPT_FD_REMOTE_DIR;
}

int uk_intercept_fdtab_register(int guest_fd, int remote_fd, int flags,
				mode_t mode)
{
	struct uk_intercept_fd_entry *entry;

	if (!uk_intercept_fd_in_range(guest_fd))
		return -EMFILE;

	entry = &intercept_fd_entries[guest_fd];
	if (entry->used)
		return -EBUSY;

	entry->used = true;
	entry->backend = uk_intercept_classify_backend(flags);
	entry->remote_fd = remote_fd;
	entry->flags = flags;
	entry->mode = mode;
	entry->cached_offset = 0;

	return 0;
}

int uk_intercept_fdtab_set_backend(int guest_fd,
				   enum uk_intercept_fd_backend backend)
{
	struct uk_intercept_fd_entry *entry;

	entry = uk_intercept_fdtab_get(guest_fd);
	if (!entry)
		return -EBADF;

	entry->backend = backend;
	return 0;
}

void uk_intercept_fdtab_unregister(int guest_fd)
{
	if (!uk_intercept_fd_in_range(guest_fd))
		return;

	memset(&intercept_fd_entries[guest_fd], 0,
	       sizeof(intercept_fd_entries[guest_fd]));
}

static int uk_intercept_resolve_remote_dfd(int dfd, const char *path)
{
	const struct uk_intercept_fd_entry *entry;

	/*
	 * Absolute paths do not consult dirfd. Relative paths may only use
	 * AT_FDCWD or a tracked remote directory fd.
	 */
	if (path[0] == '/')
		return AT_FDCWD;
	if (dfd == AT_FDCWD)
		return AT_FDCWD;

	entry = uk_intercept_fdtab_get_const(dfd);
	if (!entry)
		return -EBADF;

	if (entry->backend != UK_INTERCEPT_FD_REMOTE_DIR)
		return -ENOTDIR;

	return entry->remote_fd;
}

int uk_intercept_boot_init(struct uk_init_ctx *ictx __unused)
{
#if CONFIG_LIBINTERCEPT_CONNECT_BOOT_BEST_EFFORT || \
	CONFIG_LIBINTERCEPT_CONNECT_BOOT_REQUIRED
	int rc;
#endif

	uk_intercept_fdtab_init();
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
	uk_intercept_fdtab_reset();
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
	struct stat statbuf;
	int guest_fd;
	int saved_errno;
	int remote_dfd;
	int ret;
	int rc;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!path)
		return -EFAULT;

	remote_dfd = uk_intercept_resolve_remote_dfd(dfd, path);
	if (remote_dfd < 0 && remote_dfd != AT_FDCWD)
		return remote_dfd;

	saved_errno = errno;
	ret = uk_intercept_rpc_openat(remote_dfd, path, flags, mode);
	if (ret < 0)
		return ret;

	guest_fd = uk_intercept_fdtab_alloc_guest_fd();
	if (guest_fd < 0) {
		(void) uk_intercept_rpc_close(ret);
		return guest_fd;
	}

	rc = uk_intercept_fdtab_register(guest_fd, ret, flags, mode);
	if (rc < 0) {
		(void) uk_intercept_rpc_close(ret);
		return rc;
	}

	rc = uk_intercept_rpc_fstat(ret, &statbuf);
	if (rc < 0) {
		uk_intercept_fdtab_unregister(guest_fd);
		(void) uk_intercept_rpc_close(ret);
		return rc;
	}

	rc = uk_intercept_fdtab_set_backend(
		guest_fd, uk_intercept_classify_backend_from_mode(statbuf.st_mode));
	if (rc < 0) {
		uk_intercept_fdtab_unregister(guest_fd);
		(void) uk_intercept_rpc_close(ret);
		return rc;
	}

	errno = saved_errno;
	return guest_fd;
}

int uk_intercept_close(int fd)
{
	struct uk_intercept_fd_entry *entry;
	int saved_errno;
	int ret;

	if (!intercept_ready)
		return -ENOTSUP;

	entry = uk_intercept_fdtab_get(fd);
	if (!entry)
		return -ENOTSUP;

	saved_errno = errno;
	ret = uk_intercept_rpc_close(entry->remote_fd);
	if (ret < 0)
		return ret;

	uk_intercept_fdtab_unregister(fd);
	errno = saved_errno;
	return ret;
}

int uk_intercept_fstat(int fd, struct stat *statbuf)
{
	const struct uk_intercept_fd_entry *entry;
	int saved_errno;
	int ret;

	if (!intercept_ready)
		return -ENOTSUP;

	entry = uk_intercept_fdtab_get_const(fd);
	if (!entry)
		return -ENOTSUP;

	if (!statbuf)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_fstat(entry->remote_fd, statbuf);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

int uk_intercept_newfstatat(int dfd, const char *path, struct stat *statbuf,
			    int flags)
{
	int saved_errno;
	int remote_dfd;
	int ret;

	if (!intercept_ready)
		return -ENOTSUP;

	if (!path || !statbuf)
		return -EFAULT;

	remote_dfd = uk_intercept_resolve_remote_dfd(dfd, path);
	if (remote_dfd < 0 && remote_dfd != AT_FDCWD)
		return remote_dfd;

	saved_errno = errno;
	ret = uk_intercept_rpc_newfstatat(remote_dfd, path, statbuf, flags);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

off_t uk_intercept_lseek(int fd, off_t offset, int whence)
{
	struct uk_intercept_fd_entry *entry;
	int saved_errno;
	off_t ret;

	if (!intercept_ready)
		return -ENOTSUP;

	entry = uk_intercept_fdtab_get(fd);
	if (!entry)
		return -ENOTSUP;

	saved_errno = errno;
	ret = uk_intercept_rpc_lseek(entry->remote_fd, offset, whence);
	if (ret >= 0) {
		entry->cached_offset = ret;
		errno = saved_errno;
	}

	return ret;
}

ssize_t uk_intercept_pread(int fd, void *buf, size_t count, off_t offset)
{
	const struct uk_intercept_fd_entry *entry;
	int saved_errno;
	ssize_t ret;

	if (!intercept_ready)
		return -ENOTSUP;

	entry = uk_intercept_fdtab_get_const(fd);
	if (!entry)
		return -ENOTSUP;

	if (!buf && count)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_pread(entry->remote_fd, buf, count, offset);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

ssize_t uk_intercept_read(int fd, void *buf, size_t count)
{
	const struct uk_intercept_fd_entry *entry;
	int saved_errno;
	ssize_t ret;

	if (!intercept_ready)
		return -ENOTSUP;

	entry = uk_intercept_fdtab_get_const(fd);
	if (!entry)
		return -ENOTSUP;

	if (!buf && count)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_read(entry->remote_fd, buf, count);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

ssize_t uk_intercept_write(int fd, const void *buf, size_t count)
{
	const struct uk_intercept_fd_entry *entry;
	int saved_errno;
	ssize_t ret;

	if (!intercept_ready)
		return -ENOTSUP;

	entry = uk_intercept_fdtab_get_const(fd);
	if (!entry)
		return -ENOTSUP;

	if (!buf && count)
		return -EFAULT;

	saved_errno = errno;
	ret = uk_intercept_rpc_write(entry->remote_fd, buf, count);
	if (ret >= 0)
		errno = saved_errno;

	return ret;
}

uk_late_initcall(uk_intercept_boot_init, uk_intercept_boot_term);
