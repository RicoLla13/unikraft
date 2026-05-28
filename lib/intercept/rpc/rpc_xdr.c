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
