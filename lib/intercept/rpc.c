/* SPDX-License-Identifier: BSD-3-Clause */
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <uk/print.h>

#include "intercept_internal.h"

#define UK_INTERCEPT_RPC_BUF_SIZE 8192
#define UK_INTERCEPT_MAX_PATH_LEN 4096

#define RPC_LAST_FRAGMENT 0x80000000U
#define RPC_VERSION 2U
#define RPC_CALL 0U
#define RPC_REPLY 1U
#define RPC_MSG_ACCEPTED 0U
#define RPC_SUCCESS 0U
#define RPC_AUTH_NONE 0U

#define SYSCALL_PROG 0x20000001U
#define SYSCALL_VERS 1U
#define SYSCALL_ACCESS 14U

static uint32_t rpc_xid = 1;

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

static int rpc_put_opaque(uint8_t **p, const uint8_t *end,
			  const void *data, size_t len)
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

static int rpc_skip_opaque(const uint8_t **p, const uint8_t *end)
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

static size_t rpc_path_len(const char *path)
{
	size_t len = 0;

	while (len <= UK_INTERCEPT_MAX_PATH_LEN && path[len])
		len++;

	return len;
}

static int rpc_put_call_header(uint8_t **p, const uint8_t *end,
			       uint32_t xid, uint32_t proc)
{
	int rc;

	rc = rpc_put_u32(p, end, xid);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, RPC_CALL);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, RPC_VERSION);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, SYSCALL_PROG);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, SYSCALL_VERS);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, proc);
	if (rc < 0)
		return rc;

	/* Credentials and verifier are both AUTH_NONE with zero-length bodies. */
	rc = rpc_put_u32(p, end, RPC_AUTH_NONE);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, 0);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(p, end, RPC_AUTH_NONE);
	if (rc < 0)
		return rc;
	return rpc_put_u32(p, end, 0);
}

static int rpc_read_reply(uint32_t xid, uint8_t *buf, size_t cap,
			  const uint8_t **payload, const uint8_t **end)
{
	uint32_t marker;
	uint32_t reply_xid;
	uint32_t msg_type;
	uint32_t reply_stat;
	uint32_t accept_stat;
	size_t len;
	const uint8_t *p;
	int rc;

	rc = uk_intercept_transport_recv_exact(&marker, sizeof(marker));
	if (rc < 0)
		return rc;

	marker = ntohl(marker);
	if (!(marker & RPC_LAST_FRAGMENT)) {
		uk_pr_err("intercept-rpc: multi-fragment replies unsupported\n");
		return -ENOTSUP;
	}

	len = marker & ~RPC_LAST_FRAGMENT;
	if (len > cap)
		return -EMSGSIZE;

	rc = uk_intercept_transport_recv_exact(buf, len);
	if (rc < 0)
		return rc;

	p = buf;
	*end = buf + len;

	rc = rpc_get_u32(&p, *end, &reply_xid);
	if (rc < 0)
		return rc;
	rc = rpc_get_u32(&p, *end, &msg_type);
	if (rc < 0)
		return rc;
	rc = rpc_get_u32(&p, *end, &reply_stat);
	if (rc < 0)
		return rc;

	if (reply_xid != xid || msg_type != RPC_REPLY ||
	    reply_stat != RPC_MSG_ACCEPTED)
		return -EPROTO;

	rc = rpc_get_u32(&p, *end, &accept_stat);
	if (rc < 0)
		return rc;

	if (accept_stat != RPC_AUTH_NONE)
		return -EPROTO;

	rc = rpc_skip_opaque(&p, *end);
	if (rc < 0)
		return rc;

	rc = rpc_get_u32(&p, *end, &accept_stat);
	if (rc < 0)
		return rc;

	if (accept_stat != RPC_SUCCESS)
		return -EPROTO;

	*payload = p;
	return 0;
}

int uk_intercept_rpc_access(const char *path, int mode)
{
	uint8_t req[UK_INTERCEPT_RPC_BUF_SIZE];
	uint8_t res[UK_INTERCEPT_RPC_BUF_SIZE];
	uint8_t *p = req + sizeof(uint32_t);
	const uint8_t *payload;
	const uint8_t *end;
	uint32_t xid = rpc_xid++;
	uint32_t result;
	uint32_t err;
	uint32_t marker;
	size_t path_len;
	size_t body_len;
	int rc;

	path_len = rpc_path_len(path);
	if (path_len > UK_INTERCEPT_MAX_PATH_LEN)
		return -ENAMETOOLONG;

	rc = rpc_put_call_header(&p, req + sizeof(req), xid, SYSCALL_ACCESS);
	if (rc < 0)
		return rc;
	rc = rpc_put_opaque(&p, req + sizeof(req), path, path_len);
	if (rc < 0)
		return rc;
	rc = rpc_put_u32(&p, req + sizeof(req), (uint32_t)mode);
	if (rc < 0)
		return rc;

	body_len = (size_t)(p - (req + sizeof(uint32_t)));
	marker = htonl(RPC_LAST_FRAGMENT | (uint32_t)body_len);
	memcpy(req, &marker, sizeof(marker));

	uk_pr_info("intercept-rpc: access('%s', %d) xid=%u\n",
		   path, mode, xid);

	rc = uk_intercept_transport_send(req, body_len + sizeof(uint32_t));
	if (rc < 0)
		return rc;

	rc = rpc_read_reply(xid, res, sizeof(res), &payload, &end);
	if (rc < 0)
		return rc;

	rc = rpc_get_u32(&payload, end, &result);
	if (rc < 0)
		return rc;
	rc = rpc_get_u32(&payload, end, &err);
	if (rc < 0)
		return rc;

	if ((int32_t)result < 0)
		return err ? -(int)err : -EIO;

	return (int)result;
}
