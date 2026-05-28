/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_pread_request {
	int fd;
	size_t count;
	off_t offset;
};

struct rpc_pread_response {
	void *buf;
	size_t count;
	ssize_t result;
	int err;
};

static int rpc_encode_pread_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_pread_request *req = arg;
	uint32_t count;
	int32_t offset32;
	int rc;

	if (req->count > UINT32_MAX)
		return -EINVAL;
	if (req->offset < 0 || req->offset > INT32_MAX)
		return -EOVERFLOW;

	count = (uint32_t)req->count;
	offset32 = (int32_t)req->offset;

	rc = rpc_encode_u32(cursor, (uint32_t)req->fd);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, (uint32_t)offset32);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, count);
}

static int rpc_decode_pread_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_pread_response *pread_resp = resp;
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

	pread_resp->result = (ssize_t)(int32_t)result;
	pread_resp->err = (int)err;

	if (pread_resp->result > 0) {
		if (data_len < (size_t)pread_resp->result ||
		    pread_resp->count < (size_t)pread_resp->result)
			return -EPROTO;
		memcpy(pread_resp->buf, data, (size_t)pread_resp->result);
	}

	return 0;
}

ssize_t uk_intercept_rpc_pread(int fd, void *buf, size_t count, off_t offset)
{
	const struct rpc_pread_request req = {
		.fd = fd,
		.count = count,
		.offset = offset,
	};
	struct rpc_pread_response resp = {
		.buf = buf,
		.count = count,
	};
	int rc;

	uk_pr_info("intercept-rpc: pread64(%d, %zu, %lld)\n", fd, count,
		   (long long)offset);

	rc = rpc_call(SYSCALL_PREAD, rpc_encode_pread_request, &req,
		      rpc_decode_pread_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
