/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __INTERCEPT_RPC_INTERNAL_H__
#define __INTERCEPT_RPC_INTERNAL_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define UK_INTERCEPT_RPC_BUF_SIZE 8192
#define UK_INTERCEPT_MAX_PATH_LEN 4096

#define RPC_LAST_FRAGMENT 0x80000000U
#define RPC_VERSION 2U
#define RPC_CALL 0U
#define RPC_REPLY 1U
#define RPC_MSG_ACCEPTED 0U
#define RPC_SUCCESS 0U
#define RPC_AUTH_NONE 0U

#define SYSCALL_PROG 0x20000001U
#define SYSCALL_VERS 1U
#define SYSCALL_OPENAT 2U
#define SYSCALL_CLOSE 3U
#define SYSCALL_READ 4U
#define SYSCALL_WRITE 6U
#define SYSCALL_FSTAT 10U
#define SYSCALL_ACCESS 14U

struct rpc_encode_cursor {
	uint8_t *p;
	const uint8_t *end;
};

struct rpc_decode_cursor {
	const uint8_t *p;
	const uint8_t *end;
};

typedef int (*rpc_encode_fn_t)(struct rpc_encode_cursor *cursor,
			       const void *arg);
typedef int (*rpc_decode_fn_t)(struct rpc_decode_cursor *cursor, void *resp);

int rpc_encode_u32(struct rpc_encode_cursor *cursor, uint32_t value);
int rpc_decode_u32(struct rpc_decode_cursor *cursor, uint32_t *value);
int rpc_decode_u64(struct rpc_decode_cursor *cursor, uint64_t *value);
int rpc_put_opaque(uint8_t **p, const uint8_t *end, const void *data,
		   size_t len);
int rpc_skip_opaque(const uint8_t **p, const uint8_t *end);
int rpc_decode_opaque(struct rpc_decode_cursor *cursor, const uint8_t **data,
		      size_t *len);
size_t rpc_path_len(const char *path);
int rpc_call(uint32_t proc, rpc_encode_fn_t encode, const void *arg,
	     rpc_decode_fn_t decode, void *resp);

#endif /* __INTERCEPT_RPC_INTERNAL_H__ */
