/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __INTERCEPT_RPC_INTERNAL_H__
#define __INTERCEPT_RPC_INTERNAL_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#define UK_INTERCEPT_RPC_BUF_SIZE 8192
#define UK_INTERCEPT_MAX_PATH_LEN 4096

#define RPC_LAST_FRAGMENT 0x80000000U
#define RPC_VERSION 2U
#define RPC_CALL 0U
#define RPC_REPLY 1U
#define RPC_MSG_ACCEPTED 0U
#define RPC_ACCEPT_SUCCESS 0U
#define RPC_AUTH_NONE 0U
#define RPC_CALL_HEADER_SIZE (10U * sizeof(uint32_t))
#define RPC_ACCEPTED_REPLY_HEADER_SIZE (6U * sizeof(uint32_t))

/*
 * The current RPC implementation uses fixed 8 KiB request/reply buffers and
 * accepts only single-fragment messages. Keep variable-size I/O bounded so the
 * leaf codecs fail before issuing an impossible RPC.
 */
#define UK_INTERCEPT_RPC_MAX_READ_COUNT \
	(UK_INTERCEPT_RPC_BUF_SIZE - RPC_ACCEPTED_REPLY_HEADER_SIZE - \
	 (3U * sizeof(uint32_t)))
#define UK_INTERCEPT_RPC_MAX_WRITE_COUNT \
	(UK_INTERCEPT_RPC_BUF_SIZE - RPC_CALL_HEADER_SIZE - \
	 (2U * sizeof(uint32_t)))

struct rpc_encode_cursor {
	uint8_t *p;
	const uint8_t *end;
};

struct rpc_decode_cursor {
	const uint8_t *p;
	const uint8_t *end;
};

struct rpc_stat_payload {
	int result;
	int err;
	uint32_t dev;
	uint32_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint32_t rdev;
	uint64_t size;
	uint32_t blksize;
	uint64_t blocks;
	uint32_t atime;
	uint32_t mtime;
	uint32_t ctime;
};

typedef int (*rpc_encode_fn_t)(struct rpc_encode_cursor *cursor,
			       const void *arg);
typedef int (*rpc_decode_fn_t)(struct rpc_decode_cursor *cursor, void *resp);

int rpc_encode_u32(struct rpc_encode_cursor *cursor, uint32_t value);
int rpc_encode_u64(struct rpc_encode_cursor *cursor, uint64_t value);
int rpc_decode_u32(struct rpc_decode_cursor *cursor, uint32_t *value);
int rpc_decode_u64(struct rpc_decode_cursor *cursor, uint64_t *value);
int rpc_decode_stat_payload(struct rpc_decode_cursor *cursor,
			    struct rpc_stat_payload *payload);
void rpc_apply_stat_payload(struct stat *statbuf,
			    const struct rpc_stat_payload *payload);
int rpc_encode_opaque(struct rpc_encode_cursor *cursor, const void *data,
		      size_t len);
int rpc_skip_opaque(struct rpc_decode_cursor *cursor);
int rpc_decode_opaque(struct rpc_decode_cursor *cursor, const uint8_t **data,
		      size_t *len);
size_t rpc_path_len(const char *path);
int rpc_call(uint32_t proc, rpc_encode_fn_t encode, const void *arg,
	     rpc_decode_fn_t decode, void *resp);

#endif /* __INTERCEPT_RPC_INTERNAL_H__ */
