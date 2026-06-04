/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_access_request {
	const char *path;
	int mode;
};

struct rpc_access_response {
	int result;
	int err;
};

static int rpc_encode_access_request(struct rpc_encode_cursor *cursor,
				     const void *arg)
{
	const struct rpc_access_request *req = arg;
	size_t path_len;
	int rc;

	path_len = rpc_path_len(req->path);
	if (path_len > UK_INTERCEPT_MAX_PATH_LEN)
		return -ENAMETOOLONG;

	rc = rpc_encode_opaque(cursor, req->path, path_len);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, (uint32_t)req->mode);
}

static int rpc_decode_access_response(struct rpc_decode_cursor *cursor,
				      void *resp)
{
	struct rpc_access_response *access_resp = resp;
	uint32_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_u32(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	access_resp->result = (int)result;
	access_resp->err = (int)err;
	return 0;
}

int uk_intercept_rpc_access(const char *path, int mode)
{
	const struct rpc_access_request req = {
		.path = path,
		.mode = mode,
	};
	struct rpc_access_response resp;
	int rc;

	uk_pr_info("intercept-rpc: access('%s', %d)\n", path, mode);

	rc = rpc_call(SYSCALL_ACCESS, rpc_encode_access_request, &req,
		      rpc_decode_access_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
