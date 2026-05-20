/* SPDX-License-Identifier: BSD-3-Clause */
#include "rpc_internal.h"

static int rpc_probe_encode(struct rpc_encode_cursor *cursor,
			    const void *arg)
{
	(void)cursor;
	(void)arg;
	return 0;
}

static int rpc_probe_decode(struct rpc_decode_cursor *cursor,
			    void *resp)
{
	(void)cursor;
	(void)resp;
	return 0;
}

int uk_intercept_rpc_probe(void)
{
	return rpc_call(0, rpc_probe_encode, NULL, rpc_probe_decode, NULL);
}
