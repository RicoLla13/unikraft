/* SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

#include <uk/print.h>

#include "../intercept_internal.h"
#include "../rpcgen/protocol.h"

static CLIENT *uk_intercept_rpc_get_client(void)
{
	static CLIENT *client;
	static int last_fd = -1;
	struct sockaddr_in dummy_addr = { 0 };
	struct netbuf addr = {
		.maxlen = sizeof(dummy_addr),
		.len = sizeof(dummy_addr),
		.buf = &dummy_addr,
	};
	int rc;
	int fd;

	rc = uk_intercept_transport_connect();
	if (rc < 0)
		return NULL;

	fd = uk_intercept_transport_fd();
	if (fd < 0)
		return NULL;

	if (!client || fd != last_fd) {
		client = clnt_vc_create(fd, &addr, SYSCALL_PROG, SYSCALL_VERS,
					0, 0);
		last_fd = fd;
	}

	return client;
}

static int uk_intercept_rpc_client_error(CLIENT *client)
{
	if (!client)
		return -EIO;
	if (client->last_errno > 0)
		return -client->last_errno;
	return -EIO;
}

static void uk_intercept_apply_stat(struct stat *statbuf, unsigned long dev,
				    unsigned long ino, unsigned int mode,
				    unsigned int nlink, unsigned int uid,
				    unsigned int gid, unsigned long rdev,
				    u_quad_t size, unsigned long blksize,
				    u_quad_t blocks, unsigned long atime,
				    unsigned long mtime, unsigned long ctime)
{
	memset(statbuf, 0, sizeof(*statbuf));
	statbuf->st_dev = (dev_t)dev;
	statbuf->st_ino = (ino_t)ino;
	statbuf->st_mode = (mode_t)mode;
	statbuf->st_nlink = (nlink_t)nlink;
	statbuf->st_uid = (uid_t)uid;
	statbuf->st_gid = (gid_t)gid;
	statbuf->st_rdev = (dev_t)rdev;
	statbuf->st_size = (off_t)size;
	statbuf->st_blksize = (blksize_t)blksize;
	statbuf->st_blocks = (blkcnt_t)blocks;
	statbuf->st_atim.tv_sec = (time_t)atime;
	statbuf->st_mtim.tv_sec = (time_t)mtime;
	statbuf->st_ctim.tv_sec = (time_t)ctime;
}

int uk_intercept_rpc_access(const char *path, int mode)
{
	access_request req = {
		.path = (char *)path,
		.mode = mode,
	};
	access_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: access('%s', %d)\n", path, mode);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_access_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;
	return resp->result;
}

int uk_intercept_rpc_openat(int dfd, const char *path, int flags, mode_t mode)
{
	openat_request req = {
		.dirfd = dfd,
		.path = (char *)path,
		.flags = flags,
		.mode = (u_int)mode,
	};
	openat_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: openat(%d, '%s', %d, %o)\n",
		   dfd, path, flags, mode);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_openat_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;
	return resp->result;
}

int uk_intercept_rpc_close(int fd)
{
	close_request req = {
		.fd = fd,
	};
	close_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: close(%d)\n", fd);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_close_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;
	return resp->result;
}

int uk_intercept_rpc_fstat(int fd, struct stat *statbuf)
{
	fstat_request req = {
		.fd = fd,
	};
	fstat_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: fstat(%d)\n", fd);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_fstat_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;

	uk_intercept_apply_stat(statbuf, resp->dev, resp->ino, resp->mode,
				resp->nlink, resp->uid, resp->gid, resp->rdev,
				resp->size, resp->blksize, resp->blocks,
				resp->atime, resp->mtime, resp->ctime);
	return resp->result;
}

int uk_intercept_rpc_newfstatat(int dfd, const char *path,
				struct stat *statbuf, int flags)
{
	newfstatat_request req = {
		.dirfd = dfd,
		.path = (char *)path,
		.flags = flags,
	};
	newfstatat_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: newfstatat(%d, '%s', %d)\n",
		   dfd, path, flags);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_newfstatat_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;

	uk_intercept_apply_stat(statbuf, resp->dev, resp->ino, resp->mode,
				resp->nlink, resp->uid, resp->gid, resp->rdev,
				resp->size, resp->blksize, resp->blocks,
				resp->atime, resp->mtime, resp->ctime);
	return resp->result;
}

off_t uk_intercept_rpc_lseek(int fd, off_t offset, int whence)
{
	lseek_request req = {
		.fd = fd,
		.offset = (quad_t)offset,
		.whence = whence,
	};
	lseek_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: lseek(%d, %lld, %d)\n", fd,
		   (long long)offset, whence);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_lseek_1(&req, client);
	if (!resp)
		return (off_t)uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? (off_t)-resp->err : (off_t)-EIO;
	return (off_t)resp->result;
}

ssize_t uk_intercept_rpc_pread(int fd, void *buf, size_t count, off_t offset)
{
	pread_request req = {
		.fd = fd,
		.offset = (long)offset,
		.count = (u_int)count,
	};
	pread_response *resp;
	CLIENT *client;
	size_t copy_len;

	uk_pr_info("intercept-rpcgen: pread64(%d, %zu, %lld)\n", fd, count,
		   (long long)offset);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_pread_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;

	copy_len = resp->data.data_len;
	if (copy_len > count)
		copy_len = count;
	if (copy_len > 0)
		memcpy(buf, resp->data.data_val, copy_len);
	return resp->result;
}

ssize_t uk_intercept_rpc_read(int fd, void *buf, size_t count)
{
	read_request req = {
		.fd = fd,
		.count = (u_int)count,
	};
	read_response *resp;
	CLIENT *client;
	size_t copy_len;

	uk_pr_info("intercept-rpcgen: read(%d, %zu)\n", fd, count);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_read_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;

	copy_len = resp->data.data_len;
	if (copy_len > count)
		copy_len = count;
	if (copy_len > 0)
		memcpy(buf, resp->data.data_val, copy_len);
	return resp->result;
}

ssize_t uk_intercept_rpc_write(int fd, const void *buf, size_t count)
{
	write_request req = {
		.fd = fd,
		.data = {
			.data_len = (u_int)count,
			.data_val = (char *)(uintptr_t)buf,
		},
	};
	write_response *resp;
	CLIENT *client;

	uk_pr_info("intercept-rpcgen: write(%d, %zu)\n", fd, count);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_write_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;
	return resp->result;
}

int uk_intercept_rpc_fcntl(int fd, int cmd, unsigned long arg,
			   unsigned long *arg_out)
{
	fcntl_request req;
	fcntl_response *resp;
	CLIENT *client;

	memset(&req, 0, sizeof(req));
	req.fd = fd;
	req.cmd = cmd;

	switch (cmd) {
	case F_GETFD:
	case F_GETFL:
#ifdef F_GETOWN
	case F_GETOWN:
#endif
		req.arg.type = FCNTL_ARG_NONE;
		break;
	case F_DUPFD:
	case F_SETFD:
	case F_SETFL:
#ifdef F_DUPFD_CLOEXEC
	case F_DUPFD_CLOEXEC:
#endif
#ifdef F_SETOWN
	case F_SETOWN:
#endif
		req.arg.type = FCNTL_ARG_INT;
		req.arg.fcntl_arg_u.int_arg = (int)arg;
		break;
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		if (!arg)
			return -EFAULT;
		req.arg.type = FCNTL_ARG_FLOCK;
		{
			const struct flock *flk =
				(const struct flock *)(uintptr_t)arg;

			req.arg.fcntl_arg_u.flock_arg.l_type = flk->l_type;
			req.arg.fcntl_arg_u.flock_arg.l_whence = flk->l_whence;
			req.arg.fcntl_arg_u.flock_arg.l_start = flk->l_start;
			req.arg.fcntl_arg_u.flock_arg.l_len = flk->l_len;
			req.arg.fcntl_arg_u.flock_arg.l_pid = flk->l_pid;
		}
		break;
	default:
		return -EINVAL;
	}

	uk_pr_info("intercept-rpcgen: fcntl(%d, %d)\n", fd, cmd);

	client = uk_intercept_rpc_get_client();
	if (!client)
		return -EIO;

	resp = syscall_fcntl_1(&req, client);
	if (!resp)
		return uk_intercept_rpc_client_error(client);
	if (resp->result < 0)
		return resp->err ? -resp->err : -EIO;

	if (cmd == F_GETLK && arg_out) {
		struct flock *flk = (struct flock *)(uintptr_t)*arg_out;

		if (!flk)
			return -EFAULT;
		if (resp->arg_out.type != FCNTL_ARG_FLOCK)
			return -EPROTO;

		flk->l_type = resp->arg_out.fcntl_arg_u.flock_arg.l_type;
		flk->l_whence = resp->arg_out.fcntl_arg_u.flock_arg.l_whence;
		flk->l_start = (off_t)resp->arg_out.fcntl_arg_u.flock_arg.l_start;
		flk->l_len = (off_t)resp->arg_out.fcntl_arg_u.flock_arg.l_len;
		flk->l_pid = (pid_t)resp->arg_out.fcntl_arg_u.flock_arg.l_pid;
	}

	if (arg_out && (cmd == F_GETFD || cmd == F_GETFL
#ifdef F_GETOWN
			|| cmd == F_GETOWN
#endif
		))
		*arg_out = (unsigned long)resp->result;

	return resp->result;
}
