/* SPDX-License-Identifier: BSD-3-Clause */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <uk/config.h>
#include <uk/print.h>

#include "intercept_internal.h"

#if CONFIG_LIBINTERCEPT_TRANSPORT_SOCKET
/* Current implementation keeps one TCP connection for synchronous RPCs. */
static int intercept_transport_fd = -1;
static bool intercept_transport_connected;

static void uk_intercept_transport_reset(void)
{
	uk_pr_info("intercept: transport reset (fd=%d connected=%d)\n",
		   intercept_transport_fd, intercept_transport_connected);

	if (intercept_transport_fd >= 0)
		close(intercept_transport_fd);

	intercept_transport_fd = -1;
	intercept_transport_connected = false;
}

static int uk_intercept_transport_connect(void)
{
	struct sockaddr_in addr;
	int fd;
	int rc;

	if (intercept_transport_connected)
		return 0;

	uk_pr_info("intercept: transport connect requested to %s:%d\n",
		   CONFIG_LIBINTERCEPT_REMOTE_IPV4,
		   CONFIG_LIBINTERCEPT_REMOTE_PORT);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		uk_pr_err("intercept: socket() failed: %d\n", errno);
		return -errno;
	}

	uk_pr_info("intercept: socket() created fd=%d\n", fd);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFIG_LIBINTERCEPT_REMOTE_PORT);
	rc =
	    inet_pton(AF_INET, CONFIG_LIBINTERCEPT_REMOTE_IPV4, &addr.sin_addr);
	if (rc != 1) {
		close(fd);
		uk_pr_err("intercept: invalid remote IPv4 address: %s\n",
			  CONFIG_LIBINTERCEPT_REMOTE_IPV4);
		return -EINVAL;
	}

	rc = connect(fd, (const struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		rc = errno;
		close(fd);
		uk_pr_err("intercept: connect(%s:%d) failed: %d\n",
			  CONFIG_LIBINTERCEPT_REMOTE_IPV4,
			  CONFIG_LIBINTERCEPT_REMOTE_PORT, rc);
		return -rc;
	}

	intercept_transport_fd = fd;
	intercept_transport_connected = true;
	uk_pr_info("intercept: transport connected to %s:%d\n",
		   CONFIG_LIBINTERCEPT_REMOTE_IPV4,
		   CONFIG_LIBINTERCEPT_REMOTE_PORT);
	return 0;
}

/* Best-effort blocking send used by the request path. */
static ssize_t uk_intercept_transport_send_all(int fd, const void *buf,
					       size_t len)
{
	const char *p = buf;
	size_t remaining = len;

	while (remaining > 0) {
		ssize_t rc = send(fd, p, remaining, 0);

		if (rc < 0)
			return -errno;
		if (rc == 0)
			return -ECONNRESET;

		p += rc;
		remaining -= (size_t)rc;
	}

	return (ssize_t)len;
}

void uk_intercept_transport_init(void)
{
	intercept_transport_fd = -1;
	intercept_transport_connected = false;
	uk_pr_info("intercept: transport init remote=%s:%d\n",
		   CONFIG_LIBINTERCEPT_REMOTE_IPV4,
		   CONFIG_LIBINTERCEPT_REMOTE_PORT);
}

void uk_intercept_transport_term(void)
{
	uk_intercept_transport_reset();
}

ssize_t uk_intercept_transport_send(const void *buf, size_t len)
{
	ssize_t rc;

	if (!buf || !len)
		return -EINVAL;

	uk_pr_info("intercept: send requested len=%zu connected=%d fd=%d\n",
		   len, intercept_transport_connected, intercept_transport_fd);

	rc = uk_intercept_transport_connect();
	if (rc < 0)
		return rc;

	rc = uk_intercept_transport_send_all(intercept_transport_fd, buf, len);
	if (rc < 0) {
		uk_pr_err("intercept: send(len=%zu) failed: %d\n", len,
			  (int)-rc);
		uk_intercept_transport_reset();
		return rc;
	}

	uk_pr_info("intercept: send(len=%zu) succeeded rc=%zd\n", len, rc);

	return rc;
}

ssize_t uk_intercept_transport_recv_exact(void *buf, size_t len)
{
	char *p = buf;
	size_t remaining = len;
	ssize_t rc;

	if (!buf || !len)
		return -EINVAL;

	rc = uk_intercept_transport_connect();
	if (rc < 0)
		return rc;

	/* Replies are framed by the RPC layer; the transport just fills
	 * buffers. */
	while (remaining > 0) {
		rc = recv(intercept_transport_fd, p, remaining, 0);
		if (rc < 0) {
			rc = -errno;
			uk_pr_err("intercept: recv(len=%zu) failed: %d\n",
				  remaining, (int)-rc);
			uk_intercept_transport_reset();
			return rc;
		}
		if (rc == 0) {
			uk_pr_err("intercept: recv(len=%zu) hit EOF\n",
				  remaining);
			uk_intercept_transport_reset();
			return -ECONNRESET;
		}

		p += rc;
		remaining -= (size_t)rc;
	}

	return (ssize_t)len;
}
#else
void uk_intercept_transport_init(void) {}

void uk_intercept_transport_term(void) {}

ssize_t uk_intercept_transport_send(const void *buf __unused,
				    size_t len __unused)
{
	return -ENOTSUP;
}

ssize_t uk_intercept_transport_recv_exact(void *buf __unused,
					  size_t len __unused)
{
	return -ENOTSUP;
}
#endif /* CONFIG_LIBINTERCEPT_TRANSPORT_SOCKET */
