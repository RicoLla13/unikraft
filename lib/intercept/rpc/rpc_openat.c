/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_openat_request {
	int dfd;
	const char *path;
	int flags;
	mode_t mode;
};

struct rpc_openat_response {
	int fd;
	int result;
	int err;
};

static int rpc_encode_openat_request(struct rpc_encode_cursor *cursor,
				     const void *arg)
{
	const struct rpc_openat_request *req = arg;
	size_t path_len;
	int rc;

	path_len = rpc_path_len(req->path);
	if (path_len > UK_INTERCEPT_MAX_PATH_LEN)
		return -ENAMETOOLONG;

	rc = rpc_encode_u32(cursor, (uint32_t)req->dfd);
	if (rc < 0)
		return rc;
	rc = rpc_encode_opaque(cursor, req->path, path_len);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, (uint32_t)req->flags);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, (uint32_t)req->mode);
}

static int rpc_decode_openat_response(struct rpc_decode_cursor *cursor,
				      void *resp)
{
	struct rpc_openat_response *openat_resp = resp;
	uint32_t fd;
	uint32_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_u32(cursor, &fd);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	openat_resp->fd = (int)fd;
	openat_resp->result = (int)result;
	openat_resp->err = (int)err;
	return 0;
}

int uk_intercept_rpc_openat(int dfd, const char *path, int flags, mode_t mode)
{
	const struct rpc_openat_request req = {
		.dfd = dfd,
		.path = path,
		.flags = flags,
		.mode = mode,
	};
	struct rpc_openat_response resp;
	int rc;

	uk_pr_info("intercept-rpc: openat(%d, '%s', %d, %o)\n",
		   dfd, path, flags, mode);

	rc = rpc_call(SYSCALL_OPENAT, rpc_encode_openat_request, &req,
		      rpc_decode_openat_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
