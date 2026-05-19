/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __INTERCEPT_INTERNAL_H__
#define __INTERCEPT_INTERNAL_H__

#include <stddef.h>
#include <sys/types.h>

void uk_intercept_transport_init(void);
void uk_intercept_transport_term(void);
ssize_t uk_intercept_transport_send(const void *buf, size_t len);
ssize_t uk_intercept_transport_recv_exact(void *buf, size_t len);

int uk_intercept_rpc_access(const char *path, int mode);

#endif /* __INTERCEPT_INTERNAL_H__ */
