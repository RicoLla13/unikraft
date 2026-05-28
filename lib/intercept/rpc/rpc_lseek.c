/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_lseek_request {
	int fd;
	off_t offset;
	int whence;
};

struct rpc_lseek_response {
	off_t result;
	int err;
};

static int rpc_encode_lseek_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_lseek_request *req = arg;
	int rc;

	rc = rpc_encode_u32(cursor, (uint32_t)req->fd);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u64(cursor, (uint64_t)req->offset);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, (uint32_t)req->whence);
}

static int rpc_decode_lseek_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_lseek_response *lseek_resp = resp;
	uint64_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_u64(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	lseek_resp->result = (off_t)result;
	lseek_resp->err = (int)err;
	return 0;
}

off_t uk_intercept_rpc_lseek(int fd, off_t offset, int whence)
{
	const struct rpc_lseek_request req = {
		.fd = fd,
		.offset = offset,
		.whence = whence,
	};
	struct rpc_lseek_response resp;
	int rc;

	uk_pr_info("intercept-rpc: lseek(%d, %lld, %d)\n", fd,
		   (long long)offset, whence);

	rc = rpc_call(SYSCALL_LSEEK, rpc_encode_lseek_request, &req,
		      rpc_decode_lseek_response, &resp);
	if (rc < 0)
		return (off_t)rc;

	if (resp.result == (off_t)-1)
		return resp.err ? (off_t)-resp.err : (off_t)-EIO;

	return resp.result;
}
