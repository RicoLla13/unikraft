/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

struct rpc_fstat_request {
	int fd;
};

struct rpc_fstat_response {
	int result;
	int err;
	uint32_t dev;
	uint32_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint32_t rdev;
	uint64_t size;
	uint32_t blksize;
	uint64_t blocks;
	uint32_t atime;
	uint32_t mtime;
	uint32_t ctime;
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
	uint32_t value32;
	int rc;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	fstat_resp->result = (int)value32;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	fstat_resp->err = (int)value32;

	rc = rpc_decode_u32(cursor, &fstat_resp->dev);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->ino);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->mode);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->nlink);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->uid);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->gid);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->rdev);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u64(cursor, &fstat_resp->size);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->blksize);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u64(cursor, &fstat_resp->blocks);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->atime);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &fstat_resp->mtime);
	if (rc < 0)
		return rc;
	return rpc_decode_u32(cursor, &fstat_resp->ctime);
}

static void rpc_apply_fstat_response(struct stat *statbuf,
				     const struct rpc_fstat_response *resp)
{
	memset(statbuf, 0, sizeof(*statbuf));
	statbuf->st_dev = (dev_t)resp->dev;
	statbuf->st_ino = (ino_t)resp->ino;
	statbuf->st_mode = (mode_t)resp->mode;
	statbuf->st_nlink = (nlink_t)resp->nlink;
	statbuf->st_uid = (uid_t)resp->uid;
	statbuf->st_gid = (gid_t)resp->gid;
	statbuf->st_rdev = (dev_t)resp->rdev;
	statbuf->st_size = (off_t)resp->size;
	statbuf->st_blksize = (blksize_t)resp->blksize;
	statbuf->st_blocks = (blkcnt_t)resp->blocks;
	statbuf->st_atim.tv_sec = (time_t)resp->atime;
	statbuf->st_mtim.tv_sec = (time_t)resp->mtime;
	statbuf->st_ctim.tv_sec = (time_t)resp->ctime;
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

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	rpc_apply_fstat_response(statbuf, &resp);
	return resp.result;
}
