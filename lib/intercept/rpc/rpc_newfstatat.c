/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_newfstatat_request {
	int dfd;
	const char *path;
	int flags;
};

struct rpc_newfstatat_response {
	struct rpc_stat_payload stat;
};

static int rpc_encode_newfstatat_request(struct rpc_encode_cursor *cursor,
					 const void *arg)
{
	const struct rpc_newfstatat_request *req = arg;
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
	return rpc_encode_u32(cursor, (uint32_t)req->flags);
}

static int rpc_decode_newfstatat_response(struct rpc_decode_cursor *cursor,
					  void *resp)
{
	struct rpc_newfstatat_response *stat_resp = resp;

	return rpc_decode_stat_payload(cursor, &stat_resp->stat);
}

int uk_intercept_rpc_newfstatat(int dfd, const char *path,
				struct stat *statbuf, int flags)
{
	const struct rpc_newfstatat_request req = {
		.dfd = dfd,
		.path = path,
		.flags = flags,
	};
	struct rpc_newfstatat_response resp;
	int rc;

	uk_pr_info("intercept-rpc: newfstatat(%d, '%s', %d)\n", dfd, path, flags);

	rc = rpc_call(SYSCALL_NEWFSTATAT, rpc_encode_newfstatat_request, &req,
		      rpc_decode_newfstatat_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.stat.result < 0)
		return resp.stat.err ? -resp.stat.err : -EIO;

	rpc_apply_stat_payload(statbuf, &resp.stat);
	return resp.stat.result;
}
