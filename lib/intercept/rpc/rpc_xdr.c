/* SPDX-License-Identifier: BSD-3-Clause */
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "rpc_internal.h"

static int rpc_put_u32(uint8_t **p, const uint8_t *end, uint32_t value)
{
	uint32_t be;

	if ((size_t)(end - *p) < sizeof(be))
		return -EMSGSIZE;

	be = htonl(value);
	memcpy(*p, &be, sizeof(be));
	*p += sizeof(be);
	return 0;
}

static int rpc_get_u32(const uint8_t **p, const uint8_t *end, uint32_t *value)
{
	uint32_t be;

	if ((size_t)(end - *p) < sizeof(be))
		return -EINVAL;

	memcpy(&be, *p, sizeof(be));
	*value = ntohl(be);
	*p += sizeof(be);
	return 0;
}

int rpc_encode_u32(struct rpc_encode_cursor *cursor, uint32_t value)
{
	return rpc_put_u32(&cursor->p, cursor->end, value);
}

int rpc_encode_u64(struct rpc_encode_cursor *cursor, uint64_t value)
{
	int rc;

	rc = rpc_put_u32(&cursor->p, cursor->end, (uint32_t)(value >> 32));
	if (rc < 0)
		return rc;

	return rpc_put_u32(&cursor->p, cursor->end, (uint32_t)value);
}

int rpc_decode_u32(struct rpc_decode_cursor *cursor, uint32_t *value)
{
	return rpc_get_u32(&cursor->p, cursor->end, value);
}

int rpc_decode_u64(struct rpc_decode_cursor *cursor, uint64_t *value)
{
	uint32_t hi;
	uint32_t lo;
	int rc;

	rc = rpc_get_u32(&cursor->p, cursor->end, &hi);
	if (rc < 0)
		return rc;
	rc = rpc_get_u32(&cursor->p, cursor->end, &lo);
	if (rc < 0)
		return rc;

	*value = ((uint64_t)hi << 32) | lo;
	return 0;
}

int rpc_decode_stat_payload(struct rpc_decode_cursor *cursor,
			    struct rpc_stat_payload *payload)
{
	uint32_t value32;
	int rc;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	payload->result = (int)value32;

	rc = rpc_decode_u32(cursor, &value32);
	if (rc < 0)
		return rc;
	payload->err = (int)value32;

	rc = rpc_decode_u32(cursor, &payload->dev);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->ino);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->mode);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->nlink);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->uid);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->gid);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->rdev);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u64(cursor, &payload->size);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->blksize);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u64(cursor, &payload->blocks);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->atime);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &payload->mtime);
	if (rc < 0)
		return rc;
	return rpc_decode_u32(cursor, &payload->ctime);
}

void rpc_apply_stat_payload(struct stat *statbuf,
			    const struct rpc_stat_payload *payload)
{
	memset(statbuf, 0, sizeof(*statbuf));
	statbuf->st_dev = (dev_t)payload->dev;
	statbuf->st_ino = (ino_t)payload->ino;
	statbuf->st_mode = (mode_t)payload->mode;
	statbuf->st_nlink = (nlink_t)payload->nlink;
	statbuf->st_uid = (uid_t)payload->uid;
	statbuf->st_gid = (gid_t)payload->gid;
	statbuf->st_rdev = (dev_t)payload->rdev;
	statbuf->st_size = (off_t)payload->size;
	statbuf->st_blksize = (blksize_t)payload->blksize;
	statbuf->st_blocks = (blkcnt_t)payload->blocks;
	statbuf->st_atim.tv_sec = (time_t)payload->atime;
	statbuf->st_mtim.tv_sec = (time_t)payload->mtime;
	statbuf->st_ctim.tv_sec = (time_t)payload->ctime;
}

int rpc_put_opaque(uint8_t **p, const uint8_t *end, const void *data, size_t len)
{
	size_t pad = (4 - (len & 3)) & 3;
	int rc;

	rc = rpc_put_u32(p, end, (uint32_t)len);
	if (rc < 0)
		return rc;

	if ((size_t)(end - *p) < len + pad)
		return -EMSGSIZE;

	memcpy(*p, data, len);
	*p += len;
	memset(*p, 0, pad);
	*p += pad;
	return 0;
}

int rpc_skip_opaque(const uint8_t **p, const uint8_t *end)
{
	uint32_t len;
	size_t total;
	int rc;

	rc = rpc_get_u32(p, end, &len);
	if (rc < 0)
		return rc;

	total = len + ((4 - (len & 3)) & 3);
	if ((size_t)(end - *p) < total)
		return -EINVAL;

	*p += total;
	return 0;
}

int rpc_decode_opaque(struct rpc_decode_cursor *cursor, const uint8_t **data,
		      size_t *len)
{
	uint32_t opaque_len;
	size_t total;
	int rc;

	rc = rpc_get_u32(&cursor->p, cursor->end, &opaque_len);
	if (rc < 0)
		return rc;

	total = opaque_len + ((4 - (opaque_len & 3)) & 3);
	if ((size_t)(cursor->end - cursor->p) < total)
		return -EINVAL;

	*data = cursor->p;
	*len = opaque_len;
	cursor->p += total;
	return 0;
}

size_t rpc_path_len(const char *path)
{
	return strnlen(path, UK_INTERCEPT_MAX_PATH_LEN + 1);
}
