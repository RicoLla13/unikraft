/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_fstat_request {
	int fd;
};

struct rpc_fstat_response {
	struct rpc_stat_payload stat;
};

static int rpc_encode_fstat_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_fstat_request *req = arg;

	return rpc_encode_u32(cursor, (uint32_t)req->fd);
}

static int rpc_decode_fstat_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_fstat_response *fstat_resp = resp;

	return rpc_decode_stat_payload(cursor, &fstat_resp->stat);
}

int uk_intercept_rpc_fstat(int fd, struct stat *statbuf)
{
	const struct rpc_fstat_request req = {
		.fd = fd,
	};
	struct rpc_fstat_response resp;
	int rc;

	uk_pr_info("intercept-rpc: fstat(%d)\n", fd);

	rc = rpc_call(SYSCALL_FSTAT, rpc_encode_fstat_request, &req,
		      rpc_decode_fstat_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.stat.result < 0)
		return resp.stat.err ? -resp.stat.err : -EIO;

	rpc_apply_stat_payload(statbuf, &resp.stat);
	return resp.stat.result;
}
