/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <stdint.h>
#include <string.h>
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
	rc = rpc_put_opaque(&cursor->p, cursor->end, req->path, path_len);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, (uint32_t)req->flags);
}

static int rpc_decode_newfstatat_response(struct rpc_decode_cursor *cursor,
					  void *resp)
{
	struct rpc_newfstatat_response *stat_resp = resp;
	uint32_t value32;
	int rc;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	stat_resp->result = (int)value32;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	stat_resp->err = (int)value32;

	rc = rpc_decode_u32(cursor, &stat_resp->dev);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->ino);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->mode);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->nlink);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->uid);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->gid);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->rdev);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u64(cursor, &stat_resp->size);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->blksize);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u64(cursor, &stat_resp->blocks);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->atime);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &stat_resp->mtime);
	if (rc < 0)
		return rc;
	return rpc_decode_u32(cursor, &stat_resp->ctime);
}

static void rpc_apply_newfstatat_response(struct stat *statbuf,
					  const struct rpc_newfstatat_response *resp)
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

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	rpc_apply_newfstatat_response(statbuf, &resp);
	return resp.result;
}
