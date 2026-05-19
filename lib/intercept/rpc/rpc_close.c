/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_close_request {
	int fd;
};

struct rpc_close_response {
	int result;
	int err;
};

static int rpc_encode_close_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_close_request *req = arg;

	return rpc_encode_u32(cursor, (uint32_t)req->fd);
}

static int rpc_decode_close_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_close_response *close_resp = resp;
	uint32_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_u32(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	close_resp->result = (int)result;
	close_resp->err = (int)err;
	return 0;
}

int uk_intercept_rpc_close(int fd)
{
	const struct rpc_close_request req = {
		.fd = fd,
	};
	struct rpc_close_response resp;
	int rc;

	uk_pr_info("intercept-rpc: close(%d)\n", fd);

	rc = rpc_call(SYSCALL_CLOSE, rpc_encode_close_request, &req,
		      rpc_decode_close_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
