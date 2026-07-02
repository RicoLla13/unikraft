/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __UK_INTERCEPT_RPC_RPC_H__
#define __UK_INTERCEPT_RPC_RPC_H__

#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/types.h>

#define BYTES_PER_XDR_UNIT 4

typedef int bool_t;
typedef int enum_t;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

enum xdr_op {
	XDR_ENCODE = 0,
	XDR_DECODE = 1,
	XDR_FREE = 2,
};

typedef struct {
	enum xdr_op x_op;
	char *x_base;
	char *x_private;
	char *x_handy;
} XDR;

typedef bool_t (*xdrproc_t)(XDR *, void *);

struct netbuf {
	unsigned int maxlen;
	unsigned int len;
	void *buf;
};

enum clnt_stat {
	CLNT_RPC_SUCCESS = 0,
	CLNT_RPC_CANTENCODEARGS,
	CLNT_RPC_CANTDECODERES,
	CLNT_RPC_CANTSEND,
	CLNT_RPC_CANTRECV,
	CLNT_RPC_TIMEDOUT,
	CLNT_RPC_SYSTEMERROR,
};

#define RPC_SUCCESS CLNT_RPC_SUCCESS
#define RPC_CANTENCODEARGS CLNT_RPC_CANTENCODEARGS
#define RPC_CANTDECODERES CLNT_RPC_CANTDECODERES
#define RPC_CANTSEND CLNT_RPC_CANTSEND
#define RPC_CANTRECV CLNT_RPC_CANTRECV
#define RPC_TIMEDOUT CLNT_RPC_TIMEDOUT
#define RPC_SYSTEMERROR CLNT_RPC_SYSTEMERROR

typedef struct {
	int fd;
	unsigned long prog;
	unsigned long vers;
	enum clnt_stat last_stat;
	int last_errno;
} CLIENT;

typedef struct SVCXPRT SVCXPRT;
struct svc_req;

CLIENT *clnt_vc_create(int fd, const struct netbuf *raddr, unsigned long prog,
		       unsigned long vers, unsigned int sendsz,
		       unsigned int recvsz);
enum clnt_stat clnt_call(CLIENT *clnt, unsigned long proc, xdrproc_t inproc,
			 caddr_t in, xdrproc_t outproc, caddr_t out,
			 struct timeval tout);
void clnt_destroy(CLIENT *clnt);
void clnt_perror(CLIENT *clnt, const char *msg);

void xdrmem_create(XDR *xdrs, char *addr, unsigned int size, enum xdr_op op);
int32_t *xdr_inline(XDR *xdrs, unsigned int len);

bool_t xdr_int(XDR *xdrs, int *ip);
bool_t xdr_u_int(XDR *xdrs, unsigned int *up);
bool_t xdr_long(XDR *xdrs, long *lp);
bool_t xdr_u_long(XDR *xdrs, unsigned long *ulp);
bool_t xdr_short(XDR *xdrs, short *sp);
bool_t xdr_u_short(XDR *xdrs, unsigned short *usp);
bool_t xdr_bool(XDR *xdrs, bool_t *bp);
bool_t xdr_enum(XDR *xdrs, enum_t *ep);
bool_t xdr_quad_t(XDR *xdrs, quad_t *qp);
bool_t xdr_u_quad_t(XDR *xdrs, u_quad_t *uqp);
bool_t xdr_bytes(XDR *xdrs, char **cpp, unsigned int *sizep,
		 unsigned int maxsize);
bool_t xdr_string(XDR *xdrs, char **cpp, unsigned int maxsize);
void xdr_free(xdrproc_t proc, char *objp);

#define XDR_INLINE(xdrs, len) xdr_inline((xdrs), (len))

#define IXDR_GET_LONG(buf) ((int32_t)ntohl((uint32_t)*((buf)++)))
#define IXDR_PUT_LONG(buf, v) (*((buf)++) = (int32_t)htonl((uint32_t)(v)))
#define IXDR_GET_U_LONG(buf) ((uint32_t)ntohl((uint32_t)*((buf)++)))
#define IXDR_PUT_U_LONG(buf, v) \
	(*((buf)++) = (int32_t)htonl((uint32_t)(v)))
#define IXDR_GET_BOOL(buf) ((bool_t)IXDR_GET_LONG(buf))
#define IXDR_PUT_BOOL(buf, v) IXDR_PUT_LONG((buf), (v) ? 1 : 0)
#define IXDR_GET_U_SHORT(buf) ((unsigned short)IXDR_GET_U_LONG(buf))
#define IXDR_PUT_U_SHORT(buf, v) IXDR_PUT_U_LONG((buf), (v))

#endif /* __UK_INTERCEPT_RPC_RPC_H__ */
