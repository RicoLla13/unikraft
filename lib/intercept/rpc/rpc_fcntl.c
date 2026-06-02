/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

enum rpc_fcntl_arg_kind {
	RPC_FCNTL_ARG_KIND_NONE = 0,
	RPC_FCNTL_ARG_KIND_INT,
	RPC_FCNTL_ARG_KIND_FLOCK,
	RPC_FCNTL_ARG_KIND_UNSUPPORTED,
};

struct rpc_fcntl_request {
	int fd;
	int cmd;
	unsigned long arg;
	enum rpc_fcntl_arg_kind arg_kind;
};

struct rpc_fcntl_response {
	int result;
	int err;
	uint32_t arg_type;
	struct flock flock;
};

static enum rpc_fcntl_arg_kind rpc_fcntl_arg_kind(int cmd)
{
	switch (cmd) {
	case F_GETFD:
	case F_GETFL:
#ifdef F_GETOWN
	case F_GETOWN:
#endif
		return RPC_FCNTL_ARG_KIND_NONE;
	case F_DUPFD:
	case F_SETFD:
	case F_SETFL:
#ifdef F_DUPFD_CLOEXEC
	case F_DUPFD_CLOEXEC:
#endif
#ifdef F_SETOWN
	case F_SETOWN:
#endif
		return RPC_FCNTL_ARG_KIND_INT;
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		return RPC_FCNTL_ARG_KIND_FLOCK;
	default:
		return RPC_FCNTL_ARG_KIND_UNSUPPORTED;
	}
}

static int rpc_encode_flock(struct rpc_encode_cursor *cursor,
			    const struct flock *flk)
{
	int32_t type;
	int32_t whence;
	int rc;

	type = (int32_t)flk->l_type;
	whence = (int32_t)flk->l_whence;

	rc = rpc_encode_u32(cursor, (uint32_t)type);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, (uint32_t)whence);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u64(cursor, (uint64_t)(int64_t)flk->l_start);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u64(cursor, (uint64_t)(int64_t)flk->l_len);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, (uint32_t)flk->l_pid);
}

static int rpc_decode_flock(struct rpc_decode_cursor *cursor, struct flock *flk)
{
	uint32_t value32;
	uint64_t value64;
	int rc;

	memset(flk, 0, sizeof(*flk));

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	flk->l_type = (short)(int32_t)value32;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	flk->l_whence = (short)(int32_t)value32;

	rc = rpc_decode_u64(cursor, &value64);
	if (rc < 0)
		return rc;
	flk->l_start = (off_t)(int64_t)value64;

	rc = rpc_decode_u64(cursor, &value64);
	if (rc < 0)
		return rc;
	flk->l_len = (off_t)(int64_t)value64;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	flk->l_pid = (pid_t)(int32_t)value32;
	return 0;
}

static int rpc_encode_fcntl_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_fcntl_request *req = arg;
	const struct flock *flk;
	uint32_t arg_type;
	int rc;

	rc = rpc_encode_u32(cursor, (uint32_t)req->fd);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, (uint32_t)req->cmd);
	if (rc < 0)
		return rc;

	switch (req->arg_kind) {
	case RPC_FCNTL_ARG_KIND_NONE:
		arg_type = FCNTL_ARG_NONE;
		rc = rpc_encode_u32(cursor, arg_type);
		break;
	case RPC_FCNTL_ARG_KIND_INT:
		arg_type = FCNTL_ARG_INT;
		rc = rpc_encode_u32(cursor, arg_type);
		if (rc < 0)
			return rc;
		rc = rpc_encode_u32(cursor, (uint32_t)(int32_t)req->arg);
		break;
	case RPC_FCNTL_ARG_KIND_FLOCK:
		flk = (const struct flock *)(uintptr_t)req->arg;
		arg_type = FCNTL_ARG_FLOCK;
		rc = rpc_encode_u32(cursor, arg_type);
		if (rc < 0)
			return rc;
		rc = rpc_encode_flock(cursor, flk);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int rpc_decode_fcntl_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_fcntl_response *fcntl_resp = resp;
	uint32_t value32;
	int rc;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	fcntl_resp->result = (int)(int32_t)value32;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	fcntl_resp->err = (int)(int32_t)value32;

	rc = rpc_decode_u32(cursor, &fcntl_resp->arg_type);
	if (rc < 0)
		return rc;

	switch (fcntl_resp->arg_type) {
	case FCNTL_ARG_NONE:
	case FCNTL_ARG_INT:
		return 0;
	case FCNTL_ARG_FLOCK:
		return rpc_decode_flock(cursor, &fcntl_resp->flock);
	default:
		return -EPROTO;
	}
}

int uk_intercept_rpc_fcntl(int fd, int cmd, unsigned long arg,
			   unsigned long *arg_out)
{
	const struct rpc_fcntl_request req = {
		.fd = fd,
		.cmd = cmd,
		.arg = arg,
		.arg_kind = rpc_fcntl_arg_kind(cmd),
	};
	struct rpc_fcntl_response resp = { 0 };
	int rc;

	if (req.arg_kind == RPC_FCNTL_ARG_KIND_UNSUPPORTED)
		return -EINVAL;

	if (req.arg_kind == RPC_FCNTL_ARG_KIND_FLOCK && !arg)
		return -EFAULT;

	uk_pr_info("intercept-rpc: fcntl(%d, %d)\n", fd, cmd);

	rc = rpc_call(SYSCALL_FCNTL, rpc_encode_fcntl_request, &req,
		      rpc_decode_fcntl_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	if (cmd == F_GETLK && arg_out) {
		struct flock *flk = (struct flock *)(uintptr_t)*arg_out;

		if (!flk)
			return -EFAULT;
		if (resp.arg_type != FCNTL_ARG_FLOCK)
			return -EPROTO;
		*flk = resp.flock;
	}

	if (arg_out && (cmd == F_GETFD || cmd == F_GETFL
#ifdef F_GETOWN
			|| cmd == F_GETOWN
#endif
		))
		*arg_out = (unsigned long)resp.result;

	return resp.result;
}
