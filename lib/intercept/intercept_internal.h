/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __INTERCEPT_INTERNAL_H__
#define __INTERCEPT_INTERNAL_H__

#include <stddef.h>
#include <sys/types.h>

/* Transport lifecycle and blocking I/O helpers used by the RPC layer. */
void uk_intercept_transport_init(void);
void uk_intercept_transport_term(void);
ssize_t uk_intercept_transport_send(const void *buf, size_t len);
ssize_t uk_intercept_transport_recv_exact(void *buf, size_t len);

/* Per-syscall RPC entry points. */
int uk_intercept_rpc_access(const char *path, int mode);
int uk_intercept_rpc_openat(int dfd, const char *path, int flags, mode_t mode);
int uk_intercept_rpc_close(int fd);
ssize_t uk_intercept_rpc_read(int fd, void *buf, size_t count);
ssize_t uk_intercept_rpc_write(int fd, const void *buf, size_t count);

#endif /* __INTERCEPT_INTERNAL_H__ */
