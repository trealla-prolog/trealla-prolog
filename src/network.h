#pragma once

#include "internal.h"

extern int tpl_server(const char *hostname, unsigned port, bool is_udp, const char *keyfile, const char *certfile);
extern int tpl_connect(const char *hostname, unsigned port, bool is_udp, bool is_nodelay);

extern int tpl_domain_server(const char *name, bool is_udp);
extern int tpl_domain_connect(const char *name, bool is_udp);

extern int tpl_accept(stream *str, char **addr, int *port);
extern void tpl_set_nonblocking(stream *str);
extern void *tpl_enable_ssl(int fd, const char *hostname, bool is_server, int level, const char *certfile);
extern const char *tpl_servername(stream *str);
extern size_t tpl_read(void *ptr, size_t len, stream *str);
extern int tpl_getline(char **lineptr, size_t *n, query *q, stream *str);
extern int tpl_getline_fp(char **lineptr, size_t *n, FILE *fp);
extern int tpl_getline_nb(char **lineptr, size_t *n, query *q, FILE *fp, FILE *fp_flush);
extern int tpl_getc(stream *str);
extern size_t tpl_write(const void *ptr, size_t nbytes, stream *str);
extern int tpl_close(stream *str);

extern int tpl_udp_wait(stream *str, int timeout_ms);
extern ssize_t tpl_udp_recv(stream *str, void *buf, size_t buflen, char *host, size_t hostlen, int *port);
extern ssize_t tpl_udp_send(stream *str, const void *buf, size_t len, const char *host, int port);
extern const char *tpl_socket_errname(int err);
extern bool tpl_host_address(const char *hostname, char *ip, size_t iplen);

extern int get_local_port(int clientSock);
extern const char *get_local_hostname(char *hostname_buffer, size_t buffer_size);

extern bool tpl_wait_fd_readable(query *q, int fd);
extern bool tpl_wait_fd_writable(query *q, int fd);

// An open_string/2 stream's reads, served from its sb.

static inline int string_getc(stream *str)
{
	if (str->str_pos < (size_t)SB_strlen(str->sb))
		return (unsigned char)str->sb_buf.buf[str->str_pos++];

	str->str_eof = true;
	return EOF;
}

static inline size_t string_read(void *ptr, size_t len, stream *str)
{
	size_t avail = SB_strlen(str->sb) - str->str_pos;

	if (len > avail) {
		len = avail;
		str->str_eof = true;
	}

	memcpy(ptr, str->sb_buf.buf + str->str_pos, len);
	str->str_pos += len;
	return len;
}

static inline int string_getline(char **lineptr, size_t *n, stream *str)
{
	const char *src = str->sb_buf.buf + str->str_pos;
	size_t avail = SB_strlen(str->sb) - str->str_pos;

	if (!avail) {
		str->str_eof = true;
		return -1;
	}

	const char *nl = memchr(src, '\n', avail);
	size_t len = nl ? (size_t)(nl - src) + 1 : avail;

	if (!*lineptr || (*n < len + 1)) {
		char *tmp = TPL_realloc(*lineptr, len + 1);

		if (!tmp) {
			errno = ENOMEM;
			return -1;
		}

		*lineptr = tmp;
		*n = len + 1;
	}

	memcpy(*lineptr, src, len);
	(*lineptr)[len] = '\0';
	str->str_pos += len;
	return (int)len;
}
