/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __INTERCEPT_INTERNAL_H__
#define __INTERCEPT_INTERNAL_H__

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>

enum uk_intercept_fd_backend {
	UK_INTERCEPT_FD_NONE = 0,
	UK_INTERCEPT_FD_REMOTE_FILE,
	UK_INTERCEPT_FD_REMOTE_DIR,
};

struct uk_intercept_fd_entry {
	bool used;
	enum uk_intercept_fd_backend backend;
	int remote_fd;
	int flags;
	int fdflags;
	mode_t mode;
	off_t cached_offset;
};

/* Transport lifecycle and blocking I/O helpers used by the RPC layer. */
void uk_intercept_transport_init(void);
void uk_intercept_transport_term(void);
void uk_intercept_transport_reset(void);
int uk_intercept_transport_connect(void);
ssize_t uk_intercept_transport_send(const void *buf, size_t len);
ssize_t uk_intercept_transport_recv_exact(void *buf, size_t len);

/* Remote-fd table helpers used by the syscall-facing policy layer. */
void uk_intercept_fdtab_init(void);
void uk_intercept_fdtab_reset(void);
struct uk_intercept_fd_entry *uk_intercept_fdtab_get(int fd);
const struct uk_intercept_fd_entry *uk_intercept_fdtab_get_const(int fd);
bool uk_intercept_fdtab_contains(int fd);
bool uk_intercept_fdtab_is_remote_dir(int fd);
int uk_intercept_fdtab_register(int guest_fd, int remote_fd, int flags,
				mode_t mode);
int uk_intercept_fdtab_set_backend(int guest_fd,
				   enum uk_intercept_fd_backend backend);
void uk_intercept_fdtab_unregister(int guest_fd);

/* Per-syscall RPC entry points. */
int uk_intercept_rpc_probe(void);
int uk_intercept_rpc_access(const char *path, int mode);
int uk_intercept_rpc_openat(int dfd, const char *path, int flags, mode_t mode);
int uk_intercept_rpc_close(int fd);
int uk_intercept_rpc_fcntl(int fd, int cmd, unsigned long arg,
			   unsigned long *arg_out);
int uk_intercept_rpc_fstat(int fd, struct stat *statbuf);
int uk_intercept_rpc_newfstatat(int dfd, const char *path,
				struct stat *statbuf, int flags);
off_t uk_intercept_rpc_lseek(int fd, off_t offset, int whence);
ssize_t uk_intercept_rpc_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t uk_intercept_rpc_read(int fd, void *buf, size_t count);
ssize_t uk_intercept_rpc_write(int fd, const void *buf, size_t count);

#endif /* __INTERCEPT_INTERNAL_H__ */
