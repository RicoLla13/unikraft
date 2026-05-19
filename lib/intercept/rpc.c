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
#define SYSCALL_OPENAT 2U
#define SYSCALL_CLOSE 3U
#define SYSCALL_ACCESS 14U

struct rpc_encode_cursor {
	uint8_t *p;
	const uint8_t *end;
};

struct rpc_decode_cursor {
	const uint8_t *p;
	const uint8_t *end;
};

typedef int (*rpc_encode_fn_t)(struct rpc_encode_cursor *cursor,
			       const void *arg);
typedef int (*rpc_decode_fn_t)(struct rpc_decode_cursor *cursor, void *resp);

struct rpc_access_request {
	const char *path;
	int mode;
};

struct rpc_access_response {
	int result;
	int err;
};

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

struct rpc_close_request {
	int fd;
};

struct rpc_close_response {
	int result;
	int err;
};

/*
 * Current RPC state is intentionally single-flight: one connected transport
 * and one synchronous request/reply exchange at a time.
 */
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

static int rpc_encode_u32(struct rpc_encode_cursor *cursor, uint32_t value)
{
	return rpc_put_u32(&cursor->p, cursor->end, value);
}

static int rpc_decode_u32(struct rpc_decode_cursor *cursor, uint32_t *value)
{
	return rpc_get_u32(&cursor->p, cursor->end, value);
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

/* Bound path scanning so request encoding can reject oversized strings. */
static size_t rpc_path_len(const char *path)
{
	return strnlen(path, UK_INTERCEPT_MAX_PATH_LEN + 1);
}

/* Encode the common ONC RPC call header used by all forwarded syscalls. */
static int rpc_put_call_header(struct rpc_encode_cursor *cursor,
			       uint32_t xid, uint32_t proc)
{
	int rc;

	rc = rpc_encode_u32(cursor, xid);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, RPC_CALL);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, RPC_VERSION);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, SYSCALL_PROG);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, SYSCALL_VERS);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, proc);
	if (rc < 0)
		return rc;

	/* Credentials and verifier are both AUTH_NONE with zero-length bodies. */
	rc = rpc_encode_u32(cursor, RPC_AUTH_NONE);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, 0);
	if (rc < 0)
		return rc;
	rc = rpc_encode_u32(cursor, RPC_AUTH_NONE);
	if (rc < 0)
		return rc;
	return rpc_encode_u32(cursor, 0);
}

/*
 * Read one accepted ONC RPC reply and return the procedure-specific payload.
 * Multi-fragment replies are intentionally rejected for now.
 */
static int rpc_read_accepted_reply(uint32_t xid, uint8_t *buf, size_t cap,
				   struct rpc_decode_cursor *cursor)
{
	uint32_t marker;
	uint32_t reply_xid;
	uint32_t msg_type;
	uint32_t reply_stat;
	uint32_t verf_flavor;
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
	cursor->end = buf + len;

	rc = rpc_get_u32(&p, cursor->end, &reply_xid);
	if (rc < 0)
		return rc;
	rc = rpc_get_u32(&p, cursor->end, &msg_type);
	if (rc < 0)
		return rc;
	rc = rpc_get_u32(&p, cursor->end, &reply_stat);
	if (rc < 0)
		return rc;

	if (reply_xid != xid || msg_type != RPC_REPLY ||
	    reply_stat != RPC_MSG_ACCEPTED)
		return -EPROTO;

	rc = rpc_get_u32(&p, cursor->end, &verf_flavor);
	if (rc < 0)
		return rc;

	if (verf_flavor != RPC_AUTH_NONE)
		return -EPROTO;

	rc = rpc_skip_opaque(&p, cursor->end);
	if (rc < 0)
		return rc;

	rc = rpc_get_u32(&p, cursor->end, &accept_stat);
	if (rc < 0)
		return rc;

	if (accept_stat != RPC_SUCCESS)
		return -EPROTO;

	cursor->p = p;
	return 0;
}

static int rpc_encode_access_request(struct rpc_encode_cursor *cursor,
				     const void *arg)
{
	const struct rpc_access_request *req = arg;
	size_t path_len;
	int rc;

	path_len = rpc_path_len(req->path);
	if (path_len > UK_INTERCEPT_MAX_PATH_LEN)
		return -ENAMETOOLONG;

	rc = rpc_put_opaque(&cursor->p, cursor->end, req->path, path_len);
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
	rc = rpc_put_opaque(&cursor->p, cursor->end, req->path, path_len);
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

static int rpc_encode_close_request(struct rpc_encode_cursor *cursor,
				    const void *arg)
{
	const struct rpc_close_request *req = arg;

	return rpc_encode_u32(cursor, (uint32_t)req->fd);
}

static int rpc_decode_close_response(struct rpc_decode_cursor *cursor, void *resp)
{
	struct rpc_close_response *close_resp = resp;
	uint32_t result;
	uint32_t err;
	int rc;

	rc = rpc_decode_u32(cursor, &result);
	if (rc < 0)
		return rc;
	rc = rpc_decode_u32(cursor, &err);
	if (rc < 0)
		return rc;

	close_resp->result = (int)result;
	close_resp->err = (int)err;
	return 0;
}

/*
 * Send one synchronous RPC request for a syscall procedure and decode the
 * procedure-specific payload via callbacks.
 */
static int rpc_call(uint32_t proc, rpc_encode_fn_t encode, const void *arg,
		    rpc_decode_fn_t decode, void *resp)
{
	uint8_t req[UK_INTERCEPT_RPC_BUF_SIZE];
	uint8_t res[UK_INTERCEPT_RPC_BUF_SIZE];
	struct rpc_encode_cursor req_cursor = {
		.p = req + sizeof(uint32_t),
		.end = req + sizeof(req),
	};
	struct rpc_decode_cursor resp_cursor;
	uint32_t xid = rpc_xid++;
	uint32_t marker;
	size_t body_len;
	int rc;

	rc = rpc_put_call_header(&req_cursor, xid, proc);
	if (rc < 0)
		return rc;

	rc = encode(&req_cursor, arg);
	if (rc < 0)
		return rc;

	body_len = (size_t)(req_cursor.p - (req + sizeof(uint32_t)));
	marker = htonl(RPC_LAST_FRAGMENT | (uint32_t)body_len);
	memcpy(req, &marker, sizeof(marker));

	rc = uk_intercept_transport_send(req, body_len + sizeof(uint32_t));
	if (rc < 0)
		return rc;

	rc = rpc_read_accepted_reply(xid, res, sizeof(res), &resp_cursor);
	if (rc < 0)
		return rc;

	rc = decode(&resp_cursor, resp);
	if (rc < 0)
		return rc;

	if (resp_cursor.p != resp_cursor.end)
		return -EPROTO;

	return 0;
}

/* access() is the first procedure carried over the generic RPC framing. */
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

int uk_intercept_rpc_close(int fd)
{
	const struct rpc_close_request req = {
		.fd = fd,
	};
	struct rpc_close_response resp;
	int rc;

	uk_pr_info("intercept-rpc: close(%d)\n", fd);

	rc = rpc_call(SYSCALL_CLOSE, rpc_encode_close_request, &req,
		      rpc_decode_close_response, &resp);
	if (rc < 0)
		return rc;

	if (resp.result < 0)
		return resp.err ? -resp.err : -EIO;

	return resp.result;
}
