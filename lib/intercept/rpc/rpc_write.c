/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_write_request {
	int fd;
	const void *buf;
	size_t count;
};

struct rpc_write_response {
	ssize_t result;
	int err;
};

static int rpc_encode_write_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_write_request *req = arg;
	int rc;

	if (req->count > UINT32_MAX)
		return -EINVAL;

	rc = rpc_encode_u32(cursor, (uint32_t)req->fd);
	if (rc < 0)
		return rc;
	return rpc_put_opaque(&cursor->p, cursor->end, req->buf, req->count);
}

static int rpc_decode_write_response(struct rpc_decode_cursor *cursor,
				     void *resp)
{
	struct rpc_write_response *write_resp = resp;
	uint32_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_u32(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	write_resp->result = (ssize_t)(int32_t)result;
	write_resp->err = (int)err;
	return 0;
}

ssize_t uk_intercept_rpc_write(int fd, const void *buf, size_t count)
{
	const struct rpc_write_request req = {
		.fd = fd,
		.buf = buf,
		.count = count,
	};
	struct rpc_write_response resp;
	int rc;

	uk_pr_info("intercept-rpc: write(%d, %zu)\n", fd, count);

	rc = rpc_call(SYSCALL_WRITE, rpc_encode_write_request, &req,
		      rpc_decode_write_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
