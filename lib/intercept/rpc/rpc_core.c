/* SPDX-License-Identifier: BSD-3-Clause */
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"

/*
 * Current RPC state is intentionally single-flight: one connected transport
 * and one synchronous request/reply exchange at a time.
 */
static uint32_t rpc_xid = 1;

static int rpc_fail_and_reset(int rc)
{
	uk_intercept_transport_reset();
	return rc;
}

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
	struct rpc_decode_cursor decode;
	int rc;

	rc = uk_intercept_transport_recv_exact(&marker, sizeof(marker));
	if (rc < 0)
		return rc;

	marker = ntohl(marker);
	if (!(marker & RPC_LAST_FRAGMENT)) {
		uk_pr_err("intercept-rpc: multi-fragment replies unsupported\n");
		return rpc_fail_and_reset(-ENOTSUP);
	}

	len = marker & ~RPC_LAST_FRAGMENT;
	if (len > cap)
		return rpc_fail_and_reset(-EMSGSIZE);

	rc = uk_intercept_transport_recv_exact(buf, len);
	if (rc < 0)
		return rc;

	decode.p = buf;
	cursor->end = buf + len;
	decode.end = cursor->end;

	rc = rpc_decode_u32(&decode, &reply_xid);
	if (rc < 0)
		return rpc_fail_and_reset(rc);
	rc = rpc_decode_u32(&decode, &msg_type);
	if (rc < 0)
		return rpc_fail_and_reset(rc);
	rc = rpc_decode_u32(&decode, &reply_stat);
	if (rc < 0)
		return rpc_fail_and_reset(rc);

	if (reply_xid != xid || msg_type != RPC_REPLY ||
	    reply_stat != RPC_MSG_ACCEPTED)
		return rpc_fail_and_reset(-EPROTO);

	rc = rpc_decode_u32(&decode, &verf_flavor);
	if (rc < 0)
		return rpc_fail_and_reset(rc);
	if (verf_flavor != RPC_AUTH_NONE)
		return rpc_fail_and_reset(-EPROTO);
	rc = rpc_skip_opaque(&decode);
	if (rc < 0)
		return rpc_fail_and_reset(rc);
	rc = rpc_decode_u32(&decode, &accept_stat);
	if (rc < 0)
		return rpc_fail_and_reset(rc);
	if (accept_stat != RPC_SUCCESS)
		return rpc_fail_and_reset(-EPROTO);

	cursor->p = decode.p;

	return 0;
}

int rpc_call(uint32_t proc, rpc_encode_fn_t encode, const void *arg,
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
		return rpc_fail_and_reset(rc);

	if (resp_cursor.p != resp_cursor.end)
		return rpc_fail_and_reset(-EPROTO);

	return 0;
}
