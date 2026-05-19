/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_read_request {
	int fd;
	size_t count;
};

struct rpc_read_response {
	void *buf;
	size_t count;
	ssize_t result;
	int err;
};

static int rpc_encode_read_request(struct rpc_encode_cursor *cursor,
				   const void *arg)
{
	const struct rpc_read_request *req = arg;
	uint32_t count;
	int rc;

	if (req->count > UINT32_MAX)
		return -EINVAL;

	count = (uint32_t)req->count;

	rc = rpc_encode_u32(cursor, (uint32_t)req->fd);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, count);
}

static int rpc_decode_read_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_read_response *read_resp = resp;
	const uint8_t *data;
	size_t data_len;
	uint32_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_opaque(cursor, &data, &data_len);
	if (rc < 0)
		return rc;

	rc = rpc_decode_u32(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	read_resp->result = (ssize_t)(int32_t)result;
	read_resp->err = (int)err;

	if (read_resp->result > 0) {
		if (data_len < (size_t)read_resp->result ||
		    read_resp->count < (size_t)read_resp->result)
			return -EPROTO;
		memcpy(read_resp->buf, data, (size_t)read_resp->result);
	}

	return 0;
}

ssize_t uk_intercept_rpc_read(int fd, void *buf, size_t count)
{
	const struct rpc_read_request req = {
		.fd = fd,
		.count = count,
	};
	struct rpc_read_response resp = {
		.buf = buf,
		.count = count,
	};
	int rc;

	uk_pr_info("intercept-rpc: read(%d, %zu)\n", fd, count);

	rc = rpc_call(SYSCALL_READ, rpc_encode_read_request, &req,
		      rpc_decode_read_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
