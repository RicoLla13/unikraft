/* SPDX-License-Identifier: BSD-3-Clause */
#include <arpa/inet.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "rpc_internal.h"
#include "../rpcgen/protocol.h"

struct rpcgen_call_ctx {
	xdrproc_t xdr_in;
	caddr_t in;
	xdrproc_t xdr_out;
	caddr_t out;
};

static CLIENT rpc_client;
static bool rpc_client_ready;

static enum clnt_stat rpcgen_map_status(int rc)
{
	switch (-rc) {
	case EINVAL:
	case EMSGSIZE:
	case ENAMETOOLONG:
	case EPROTO:
		return RPC_CANTENCODEARGS;
	case ECONNRESET:
		return RPC_CANTRECV;
	case ETIMEDOUT:
		return RPC_TIMEDOUT;
	default:
		return RPC_SYSTEMERROR;
	}
}

static int rpcgen_encode(struct rpc_encode_cursor *cursor, const void *arg)
{
	const struct rpcgen_call_ctx *ctx = arg;
	XDR xdrs;

	xdrmem_create(&xdrs, (char *)cursor->p,
		      (unsigned int)(cursor->end - cursor->p), XDR_ENCODE);

	if (!ctx->xdr_in(&xdrs, ctx->in))
		return -EINVAL;

	cursor->p = (uint8_t *)xdrs.x_private;
	return 0;
}

static int rpcgen_decode(struct rpc_decode_cursor *cursor, void *resp)
{
	const struct rpcgen_call_ctx *ctx = resp;
	XDR xdrs;

	xdrmem_create(&xdrs, (char *)cursor->p,
		      (unsigned int)(cursor->end - cursor->p), XDR_DECODE);

	if (!ctx->xdr_out(&xdrs, ctx->out))
		return -EINVAL;

	cursor->p = (const uint8_t *)xdrs.x_private;
	return 0;
}

CLIENT *clnt_vc_create(int fd, const struct netbuf *raddr __unused,
		       unsigned long prog, unsigned long vers,
		       unsigned int sendsz __unused,
		       unsigned int recvsz __unused)
{
	rpc_client.fd = fd;
	rpc_client.prog = prog;
	rpc_client.vers = vers;
	rpc_client.last_stat = RPC_SUCCESS;
	rpc_client.last_errno = 0;
	rpc_client_ready = true;
	return &rpc_client;
}

enum clnt_stat clnt_call(CLIENT *clnt, unsigned long proc, xdrproc_t inproc,
			 caddr_t in, xdrproc_t outproc, caddr_t out,
			 struct timeval tout __unused)
{
	struct rpcgen_call_ctx ctx = {
		.xdr_in = inproc,
		.in = in,
		.xdr_out = outproc,
		.out = out,
	};
	int rc;

	if (!clnt || !inproc || !outproc) {
		if (clnt) {
			clnt->last_stat = RPC_SYSTEMERROR;
			clnt->last_errno = EINVAL;
		}
		return RPC_SYSTEMERROR;
	}

	rc = rpc_call((uint32_t)proc, rpcgen_encode, &ctx, rpcgen_decode, &ctx);
	if (rc < 0) {
		clnt->last_errno = -rc;
		clnt->last_stat = rpcgen_map_status(rc);
		return clnt->last_stat;
	}

	clnt->last_errno = 0;
	clnt->last_stat = RPC_SUCCESS;
	return RPC_SUCCESS;
}

void clnt_destroy(CLIENT *clnt)
{
	if (!clnt)
		return;

	memset(clnt, 0, sizeof(*clnt));
	rpc_client_ready = false;
}

void clnt_perror(CLIENT *clnt, const char *msg)
{
	if (!clnt) {
		uk_pr_err("%s: no client\n", msg ? msg : "rpc");
		return;
	}

	uk_pr_err("%s: stat=%d errno=%d\n", msg ? msg : "rpc",
		  clnt->last_stat, clnt->last_errno);
}

void xdrmem_create(XDR *xdrs, char *addr, unsigned int size, enum xdr_op op)
{
	xdrs->x_op = op;
	xdrs->x_base = addr;
	xdrs->x_private = addr;
	xdrs->x_handy = addr + size;
}

int32_t *xdr_inline(XDR *xdrs, unsigned int len)
{
	if ((size_t)(xdrs->x_handy - xdrs->x_private) < len)
		return NULL;

	{
		int32_t *buf = (int32_t *)(void *)xdrs->x_private;
		xdrs->x_private += len;
		return buf;
	}
}

static bool_t xdr_put_u32(XDR *xdrs, uint32_t value)
{
	uint32_t be;

	if (xdrs->x_op != XDR_ENCODE)
		return FALSE;
	if ((size_t)(xdrs->x_handy - xdrs->x_private) < sizeof(be))
		return FALSE;

	be = htonl(value);
	memcpy(xdrs->x_private, &be, sizeof(be));
	xdrs->x_private += sizeof(be);
	return TRUE;
}

static bool_t xdr_get_u32(XDR *xdrs, uint32_t *value)
{
	uint32_t be;

	if (xdrs->x_op != XDR_DECODE)
		return FALSE;
	if ((size_t)(xdrs->x_handy - xdrs->x_private) < sizeof(be))
		return FALSE;

	memcpy(&be, xdrs->x_private, sizeof(be));
	xdrs->x_private += sizeof(be);
	*value = ntohl(be);
	return TRUE;
}

bool_t xdr_int(XDR *xdrs, int *ip)
{
	uint32_t v;

	if (xdrs->x_op == XDR_FREE)
		return TRUE;
	if (xdrs->x_op == XDR_ENCODE)
		return xdr_put_u32(xdrs, (uint32_t)*ip);
	if (!xdr_get_u32(xdrs, &v))
		return FALSE;
	*ip = (int32_t)v;
	return TRUE;
}

bool_t xdr_u_int(XDR *xdrs, unsigned int *up)
{
	uint32_t v;

	if (xdrs->x_op == XDR_FREE)
		return TRUE;
	if (xdrs->x_op == XDR_ENCODE)
		return xdr_put_u32(xdrs, (uint32_t)*up);
	if (!xdr_get_u32(xdrs, &v))
		return FALSE;
	*up = (unsigned int)v;
	return TRUE;
}

bool_t xdr_long(XDR *xdrs, long *lp)
{
	int v = (int)*lp;
	bool_t ok;

	if (xdrs->x_op == XDR_ENCODE)
		return xdr_int(xdrs, &v);

	ok = xdr_int(xdrs, &v);
	if (ok && xdrs->x_op == XDR_DECODE)
		*lp = (long)v;
	return ok;
}

bool_t xdr_u_long(XDR *xdrs, unsigned long *ulp)
{
	unsigned int v = (unsigned int)*ulp;
	bool_t ok;

	if (xdrs->x_op == XDR_ENCODE)
		return xdr_u_int(xdrs, &v);

	ok = xdr_u_int(xdrs, &v);
	if (ok && xdrs->x_op == XDR_DECODE)
		*ulp = (unsigned long)v;
	return ok;
}

bool_t xdr_short(XDR *xdrs, short *sp)
{
	int v = (int)*sp;
	bool_t ok;

	if (xdrs->x_op == XDR_ENCODE)
		return xdr_int(xdrs, &v);

	ok = xdr_int(xdrs, &v);
	if (ok && xdrs->x_op == XDR_DECODE)
		*sp = (short)v;
	return ok;
}

bool_t xdr_u_short(XDR *xdrs, unsigned short *usp)
{
	unsigned int v = (unsigned int)*usp;
	bool_t ok;

	if (xdrs->x_op == XDR_ENCODE)
		return xdr_u_int(xdrs, &v);

	ok = xdr_u_int(xdrs, &v);
	if (ok && xdrs->x_op == XDR_DECODE)
		*usp = (unsigned short)v;
	return ok;
}

bool_t xdr_bool(XDR *xdrs, bool_t *bp)
{
	int v = *bp ? 1 : 0;
	bool_t ok;

	if (xdrs->x_op == XDR_ENCODE)
		return xdr_int(xdrs, &v);

	ok = xdr_int(xdrs, &v);
	if (ok && xdrs->x_op == XDR_DECODE)
		*bp = v ? TRUE : FALSE;
	return ok;
}

bool_t xdr_enum(XDR *xdrs, enum_t *ep)
{
	return xdr_int(xdrs, ep);
}

bool_t xdr_quad_t(XDR *xdrs, quad_t *qp)
{
	u_quad_t v = (u_quad_t)*qp;
	bool_t ok;

	if (xdrs->x_op == XDR_ENCODE)
		return xdr_u_quad_t(xdrs, &v);

	ok = xdr_u_quad_t(xdrs, &v);
	if (ok && xdrs->x_op == XDR_DECODE)
		*qp = (quad_t)v;
	return ok;
}

bool_t xdr_u_quad_t(XDR *xdrs, u_quad_t *uqp)
{
	uint32_t hi;
	uint32_t lo;

	if (xdrs->x_op == XDR_FREE)
		return TRUE;

	if (xdrs->x_op == XDR_ENCODE) {
		hi = (uint32_t)((uint64_t)*uqp >> 32);
		lo = (uint32_t)(uint64_t)*uqp;
		return xdr_put_u32(xdrs, hi) && xdr_put_u32(xdrs, lo);
	}

	if (!xdr_get_u32(xdrs, &hi) || !xdr_get_u32(xdrs, &lo))
		return FALSE;
	*uqp = (u_quad_t)(((uint64_t)hi << 32) | lo);
	return TRUE;
}

static bool_t xdr_counted_opaque(XDR *xdrs, char **cpp, unsigned int *sizep,
				 unsigned int maxsize, bool is_string)
{
	unsigned int len;
	size_t pad;

	if (xdrs->x_op == XDR_FREE) {
		free(*cpp);
		*cpp = NULL;
		if (sizep)
			*sizep = 0;
		return TRUE;
	}

	len = *sizep;
	if (xdrs->x_op == XDR_ENCODE) {
		if (len > maxsize)
			return FALSE;
		if (!xdr_u_int(xdrs, &len))
			return FALSE;
		pad = (4U - (len & 3U)) & 3U;
		if ((size_t)(xdrs->x_handy - xdrs->x_private) < len + pad)
			return FALSE;
		memcpy(xdrs->x_private, *cpp, len);
		xdrs->x_private += len;
		memset(xdrs->x_private, 0, pad);
		xdrs->x_private += pad;
		return TRUE;
	}

	if (!xdr_u_int(xdrs, &len))
		return FALSE;
	if (len > maxsize)
		return FALSE;
	pad = (4U - (len & 3U)) & 3U;
	if ((size_t)(xdrs->x_handy - xdrs->x_private) < len + pad)
		return FALSE;

	*cpp = malloc(len + (is_string ? 1U : 0U));
	if (!*cpp && len)
		return FALSE;
	if (len)
		memcpy(*cpp, xdrs->x_private, len);
	if (is_string)
		(*cpp)[len] = '\0';
	*sizep = len;
	xdrs->x_private += len + pad;

	return TRUE;
}

bool_t xdr_bytes(XDR *xdrs, char **cpp, unsigned int *sizep,
		 unsigned int maxsize)
{
	return xdr_counted_opaque(xdrs, cpp, sizep, maxsize, false);
}

bool_t xdr_string(XDR *xdrs, char **cpp, unsigned int maxsize)
{
	unsigned int size;

	if (xdrs->x_op == XDR_FREE) {
		free(*cpp);
		*cpp = NULL;
		return TRUE;
	}

	if (xdrs->x_op == XDR_ENCODE)
		size = (unsigned int)strlen(*cpp);
	else
		size = 0;

	return xdr_counted_opaque(xdrs, cpp, &size, maxsize, true);
}

void xdr_free(xdrproc_t proc, char *objp)
{
	XDR xdrs;

	if (!proc || !objp)
		return;

	xdrmem_create(&xdrs, NULL, 0, XDR_FREE);
	(void)proc(&xdrs, (void *)objp);
}
