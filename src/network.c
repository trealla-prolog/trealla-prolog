#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Cosmopolitan produces one binary that runs both hosted (Linux/Mac/Windows,
// with a real network stack) and booted directly with no OS at all - and
// IsMetal() is how it tells the two apart at runtime. There's no NIC driver
// in the no-OS case, so refuse up front rather than let socket() hit an
// unimplemented syscall path.
#ifdef __COSMOPOLITAN__
#define _COSMO_SOURCE	// gates dce.h below - IsMetal() is otherwise compiled out
#include <libc/dce.h>
#define NET_CHECK_METAL() do { if (IsMetal()) { errno = ENOSYS; return -1; } } while (0)
#else
#define NET_CHECK_METAL() do { } while (0)
#endif

#if USE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/err.h>
static int g_ctx_use_cnt = 0;
static SSL_CTX *g_ctx = NULL;
#if OPENSSL_VERSION_NUMBER > 0x10100000L
#define TLS_SERVER_METHOD_FUNC TLS_server_method
#define TLS_CLIENT_METHOD_FUNC TLS_client_method
#else
#warning "TLS is not available, falling back to SSL23 (deprecated)"
#define TLS_SERVER_METHOD_FUNC SSLv23_server_method
#define TLS_CLIENT_METHOD_FUNC SSLv23_client_method
#endif
#endif

#include "history.h"
#include "network.h"
#include "query.h"

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
//#define close closesocket
//#define ioctl ioctlsocket
#ifdef errno
//#undef errno
#endif
//#define errno WSAGetLastError()
#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#define SHUT_WR SD_SEND
#endif
// EWOULDBLOCK itself comes from internal.h (aliased to EAGAIN) - not
// WSAEWOULDBLOCK, which is WSAGetLastError()'s error space, not errno's,
// and errno can't be redefined to WSAGetLastError() either: this file
// assigns to it directly (errno = 0, errno = EINTR, ...) in several
// places, and that macro would not be an lvalue.
#else
#ifndef __wasi__
#include <netdb.h>
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#if defined(__sun)
#include <sys/filio.h>
#endif
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#ifndef __wasi__
#include <poll.h>
#endif
#endif


int get_local_port(int clientSock) {
#if !defined(_WIN32) && !defined(__wasi__)
    struct sockaddr_in sin;
    socklen_t addrlen = sizeof(sin);

    if (getsockname(clientSock, (struct sockaddr *)&sin, &addrlen) == 0) {
         int local_port = ntohs(sin.sin_port);
         return local_port;
	}
#endif

    return -1;
}

const char *get_local_hostname(char *hostname_buffer, size_t buffer_size) {
#if !defined(_WIN32) && !defined(__wasi__)
    // FIX 10: do not exit() from a library routine; report failure to the caller.
    if (gethostname(hostname_buffer, buffer_size) == -1) {
        perror("gethostname error");
        return NULL;
    }
    hostname_buffer[buffer_size - 1] = '\0';
    return hostname_buffer;
#else
	(void) hostname_buffer;
	(void) buffer_size;
	return NULL;
#endif
}

int tpl_domain_connect(const char *name, bool udp)
{
#if !defined(_WIN32) && !defined(__wasi__)
	NET_CHECK_METAL();
	int fd = socket(AF_UNIX, udp?SOCK_DGRAM:SOCK_STREAM, 0);

	if (fd == -1) {
		perror("socket");
		return -1;
   }

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, name, sizeof(addr.sun_path) - 1);

	if (connect(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
		int save_errno = errno;
		close(fd);
		errno = save_errno;
		return -1;
	}

	return fd;
#else
	return -1;
#endif
}

int tpl_domain_server(const char *name, bool udp)
{
#if !defined(_WIN32) && !defined(__wasi__)
	NET_CHECK_METAL();
    struct sockaddr_un server_sockaddr;
    memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
    int fd = socket(AF_UNIX, udp?SOCK_DGRAM:SOCK_STREAM, 0);

    if (fd == -1) {
		perror("socket");
		return -1;
	}

    server_sockaddr.sun_family = AF_UNIX;
    strncpy(server_sockaddr.sun_path, name, sizeof(server_sockaddr.sun_path) - 1);
    unlink(name);
    int rc = bind(fd, (struct sockaddr *) &server_sockaddr, sizeof(server_sockaddr));

    if (rc == -1) {
		int save_errno = errno;
		close(fd);
		errno = save_errno;
		return -1;
	}

	if (udp)
		return fd;

	if (listen(fd, SOMAXCONN)) {
		perror("listen");
	}

	return fd;
#else
	return -1;
#endif
}

int tpl_connect(const char *hostname, unsigned port, bool udp, bool nodelay)
{
#if !defined(_WIN32) && !defined(__wasi__)
	NET_CHECK_METAL();
	struct addrinfo hints, *result, *rp;
	int fd, status, save_errno = ECONNREFUSED;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = hostname ? 0 : AI_PASSIVE;
	char svc[20];
	snprintf(svc, sizeof(svc), "%u", port);

	if ((status = getaddrinfo(hostname, svc, &hints, &result)) != 0) {
		errno = (status == EAI_NONAME) ? ENOENT : EADDRNOTAVAIL;
		return -1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (fd == -1) {
			perror("socket");
			continue;
		}

		int flag = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
		//setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;

		// Saved before the cleanup below, which may clobber it.

		save_errno = errno;
		close(fd);
	}

	freeaddrinfo(result);

	if (rp == NULL) {
		errno = save_errno;
		return -1;
	}

	struct linger l;
	l.l_onoff = 0;
	l.l_linger = 0;
	setsockopt(fd, SOL_SOCKET, SO_LINGER, (char*)&l, sizeof(l));
	int flag = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&flag, sizeof(flag));
	flag = nodelay;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
	return fd;
#else
	return -1;
#endif
}

int tpl_server(const char *hostname, unsigned port, bool udp, const char *keyfile, const char *certfile)
{
#if !defined(_WIN32) && !defined(__wasi__)
	NET_CHECK_METAL();
	struct addrinfo hints, *result, *rp;
	int fd, status, save_errno = EADDRNOTAVAIL;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	char svc[20];
	snprintf(svc, sizeof(svc), "%u", port);

	// A NULL or empty hostname means the wildcard address, which is what
	// this always did - it used to discard the argument outright. A real
	// hostname now binds that interface only, so a server can be
	// restricted to loopback. The caller is responsible for passing NULL
	// when the user did not actually name a host: see bif_sys_server_3,
	// where "localhost" is the alias default and must not be mistaken
	// for a bind request.

	const char *bindhost = (hostname && *hostname) ? hostname : NULL;

	if ((status = getaddrinfo(bindhost, svc, &hints, &result)) != 0) {
		errno = (status == EAI_NONAME) ? ENOENT : EADDRNOTAVAIL;
		return -1;
	}

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (fd == -1) {
			perror("socket");
			continue;
		}

		int flag = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
		//setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));

		if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		// Kept so the caller can report *why*. freeaddrinfo and close
		// below are both free to clobber errno, so it is saved here
		// rather than read at the return.

		save_errno = errno;
		close(fd);
	}

	freeaddrinfo(result);

	if (rp == NULL) {
		errno = save_errno;
		return -1;
	}

	if (udp)
		return fd;

#if USE_OPENSSL
	if (keyfile) {
		if (!g_ctx_use_cnt++) {
			SSL_load_error_strings();
			g_ctx = SSL_CTX_new(TLS_SERVER_METHOD_FUNC());
			SSL_CTX_set_options(g_ctx, SSL_OP_CIPHER_SERVER_PREFERENCE);
		}

		if (!SSL_CTX_use_PrivateKey_file(g_ctx, keyfile, SSL_FILETYPE_PEM)) {
			printf("SSL load private key failed: %s\n", keyfile);
			ERR_print_errors_fp(stderr);
			close(fd);
			return 0;
		}

		if (!SSL_CTX_use_certificate_file(g_ctx, !certfile?keyfile:certfile, SSL_FILETYPE_PEM)) {
			printf("SSL load certificate failed: %s\n", !certfile?keyfile:certfile);
			ERR_print_errors_fp(stderr);
			close(fd);
			return 0;
		}

		SSL_CTX_load_verify_locations(g_ctx, !certfile?keyfile:certfile, NULL);
		SSL_CTX_set_default_verify_paths(g_ctx);
	}
#else
	(void) keyfile;
	(void) certfile;
#endif

	if (listen(fd, SOMAXCONN)) {
		perror("listen");
	}

	return fd;
#else
	return -1;
#endif
}

int tpl_accept(stream *str, char **addr, int *port)
{
#if !defined(_WIN32) && !defined(__wasi__)
	NET_CHECK_METAL();
	struct sockaddr_in sa = {0};
	socklen_t len = sizeof(sa);
	int fd = accept(fileno(str->fp_in), (struct sockaddr*)&sa, &len);

	// FIX 9: any accept() failure leaves fd == -1; bail before touching it so
	// setsockopt() is never called on an invalid descriptor.
	if (fd == -1)
		return -1;

	if (addr) {
		char buf[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
		*addr = TPL_strdup(buf);
	}

	if (port)
		*port = ntohs(sa.sin_port);

	struct linger l;
	l.l_onoff = 0;
	l.l_linger = 0;
	setsockopt(fd, SOL_SOCKET, SO_LINGER, (char*)&l, sizeof(l));
	int flag = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&flag, sizeof(flag));
	flag = str->nodelay;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
	return fd;
#else
	return -1;
#endif
}


void tpl_set_nonblocking(stream *str)
{
#if !defined(_WIN32) && !defined(__wasi__)
	unsigned long flag = 1;
	ioctl(fileno(str->fp_in), FIONBIO, &flag);
#endif
}

// FIONBIO's flag lives on the open file description, not the individual
// fd number, so this also covers a dup()'d str->fp_out - see
// tpl_set_nonblocking()'s own call sites in bif_net.c.
static void tpl_set_blocking(stream *str)
{
#if !defined(_WIN32) && !defined(__wasi__)
	unsigned long flag = 0;
	ioctl(fileno(str->fp_in), FIONBIO, &flag);
#endif
}

void *tpl_enable_ssl(int fd, const char *hostname, bool is_server, int level, const char *certfile)
{
#if USE_OPENSSL
	if (!g_ctx_use_cnt++) {
		SSL_load_error_strings();
		g_ctx = SSL_CTX_new(is_server?TLS_SERVER_METHOD_FUNC():TLS_CLIENT_METHOD_FUNC());
		//SSL_CTX_set_cipher_list(g_ctx, DEFAULT_CIPHERS);
	}

	SSL *ssl = SSL_new(g_ctx);
	SSL_set_ssl_method(ssl, is_server?TLS_SERVER_METHOD_FUNC():TLS_CLIENT_METHOD_FUNC());
	//SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
	//SSL_set_verify(ssl, SSL_VERIFY_NONE, 0);

	if (!is_server && certfile) {
		if (!SSL_CTX_use_certificate_file(g_ctx, certfile, SSL_FILETYPE_PEM)) {
			printf("SSL load certificate failed\n");
			ERR_print_errors_fp(stderr);
			close(fd);
			SSL_free(ssl);
			return NULL;
		}

		SSL_CTX_set_default_verify_paths(g_ctx);

		if (level > 0)
			SSL_set_verify(ssl, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, 0);
	}

	SSL_set_fd(ssl, fd);

	if (is_server) {
		if (SSL_accept(ssl) == -1) {
			fprintf(stderr, "SSL_accept failed\n");
			ERR_print_errors_fp(stderr);
			SSL_free(ssl);
			return NULL;
		}
	} else {
		SSL_set_tlsext_host_name(ssl, hostname);

		if (SSL_connect(ssl) <= 0) {
			fprintf(stderr, "SSL_connect failed\n");
			ERR_print_errors_fp(stderr);
			SSL_free(ssl);
			return NULL;
		}
	}
	return ssl;
#else
	(void) fd;
	(void) hostname;
	(void) is_server;
	(void) level;
	(void) certfile;
	return NULL;
#endif
}

const char *tpl_servername(stream *str)
{
	// FIX 2: guard on USE_OPENSSL (the macro the rest of this file uses and that
	// internal.h always #defines to 0/1) rather than the never-defined USE_SSL.
#if USE_OPENSSL && !defined(_WIN32) && !defined(__wasi__)
	return SSL_get_servername((SSL*)str->sslptr, TLSEXT_NAMETYPE_host_name);
#else
	(void) str;
	return NULL;
#endif
}

size_t tpl_write(const void *ptr, size_t nbytes, stream *str)
{
#if USE_OPENSSL
	if (str->ssl) {
		int ok = SSL_write((SSL*)str->sslptr, ptr, nbytes);
		return ok < 0 ? 0 : (size_t)ok;
	}
#endif

	if (str->is_memory) {
		SB_fwrite(str->sb, ptr, nbytes);
		return nbytes;
	} else {
		size_t len = fwrite(ptr, 1, nbytes, str->fp_out?str->fp_out:str->fp);

		if (str->is_pipe)
			fflush(str->fp_out);

		return len;
	}
}

int tpl_getc(stream *str)
{
	if (str->is_string)
		return string_getc(str);

	errno = 0;	// FIX: reset so a stale EINTR from an earlier call isn't misread as an interrupt
#if USE_OPENSSL
	if (str->ssl) {
		size_t len = 1;
		char ptr[2];
		char *dst = ptr;

		while (len && str->srclen) {
			*dst++ = *str->src++;
			str->srclen--;
			len--;
		}

		if (dst != ptr)
			return (unsigned char)ptr[0];		// FIX 6: don't sign-extend 0xFF into EOF

		int rlen = SSL_read((SSL*)str->sslptr, ptr, len);

		// FIX 6: 0 == clean shutdown, <0 == error; either way return EOF rather
		// than an uninitialised byte.
		if (rlen <= 0) {
			if (errno == EINTR)
				clearerr(str->fp_in);

			return EOF;
		}

		return (unsigned char)ptr[0];
	}
#endif

	if (str->is_socket && str->fp_out)
		fflush(str->fp_out);

	int ok = fgetc(str->fp_in);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		ok = EOF;
	}

	return ok;
}

size_t tpl_read(void *ptr, size_t len, stream *str)
{
	if (str->is_string)
		return string_read(ptr, len, str);

	errno = 0;	// FIX: reset so a stale EINTR from an earlier call isn't misread as an interrupt
#if USE_OPENSSL
	if (str->ssl) {
		char *dst = ptr;

		while (len && str->srclen) {
			*dst++ = *str->src++;
			str->srclen--;
			len--;
		}

		if (dst != ptr)
			return dst - (char*)ptr;

		int ok = SSL_read((SSL*)str->sslptr, ptr, len);

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return EOF;
		}

		return ok < 0 ? 0 : (size_t)ok;			// avoid returning a huge size_t on error
	}
#endif

	if (str->is_socket && str->fp_out)
		fflush(str->fp_out);

	int ok = fread(ptr, 1, len, str->fp_in);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		ok = EOF;
	}

	return ok;
}

// getc() takes the stream's lock for every character. Take it once for the line and read unlocked
// inside: a line of a .dot file went from 30-odd lock pairs to one.

#if defined(_WIN32)
#define TPL_FLOCK(fp) _lock_file(fp)
#define TPL_FUNLOCK(fp) _unlock_file(fp)
#define TPL_GETC_UNLOCKED(fp) _getc_nolock(fp)
#elif defined(__wasi__)

// wasi-libc declares flockfile()/funlockfile() but does not define them, so using them
// links against nothing. There are no threads here to lock against either.

#define TPL_FLOCK(fp) ((void)0)
#define TPL_FUNLOCK(fp) ((void)0)
#define TPL_GETC_UNLOCKED(fp) getc(fp)
#else
#define TPL_FLOCK(fp) flockfile(fp)
#define TPL_FUNLOCK(fp) funlockfile(fp)
#define TPL_GETC_UNLOCKED(fp) getc_unlocked(fp)
#endif

int tpl_getline_fp(char **lineptr, size_t *n, FILE *fp)
{
	if (!lineptr || !n || !fp) {
		errno = EINVAL;
		return -1;
	}

	size_t pos = 0;
	int ch;
	TPL_FLOCK(fp);

	while ((ch = TPL_GETC_UNLOCKED(fp)) != EOF) {
		if ((pos + 1) >= *n) {
			size_t new_size = *n ? *n + (*n >> 1) : 128;
			char *new_ptr = TPL_realloc(*lineptr, new_size);

			if (!new_ptr) {
				TPL_FUNLOCK(fp);
				errno = ENOMEM;
				return -1;
			}

			*lineptr = new_ptr;
			*n = new_size;
		}

		(*lineptr)[pos++] = (char)ch;

		if (ch == '\n')
			break;
	}

	TPL_FUNLOCK(fp);

	if (!pos)
		return -1;

	(*lineptr)[pos] = '\0';
	return (int)pos;
}

// Waits for fd in short, alarm-aware poll() slices instead of one
// uninterruptible blocking read/write (real SIGALRM used to do this via
// EINTR - see DESIGN-GUSTTO.md). Sets errno = EINTR and returns false on
// timeout/interrupt; true means the fd is ready for the direction asked.

#if defined(_WIN32)
// select() rather than WSAPoll(): the fd here came from fileno() on a
// stream fdopen()'d over a socket the way the rest of this file already
// treats Windows sockets (see tpl_connect()/tpl_server()), and select()
// needs no extra header beyond winsock2.h, already included above.
static bool tpl_wait_fd(query *q, int fd, bool for_write)
{
	if (fd < 0)
		return true;

	SOCKET sock = (SOCKET)fd;

	for (;;) {
		if (interrupt_pending(q)) {
			errno = EINTR;
			return false;
		}

		unsigned ms;
		int wait_ms = next_alarm_delay(q, &ms) ? (int)ms : 250;

		if (wait_ms > 250)
			wait_ms = 250;

		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(sock, &fds);

		struct timeval tv;
		tv.tv_sec = wait_ms / 1000;
		tv.tv_usec = (wait_ms % 1000) * 1000;

		int n = for_write
			? select(0, NULL, &fds, NULL, &tv)	// nfds ignored on Windows
			: select(0, &fds, NULL, NULL, &tv);

		if (n != 0)
			return true;	// ready, or select() itself errored - let the real call report it
	}
}

bool tpl_wait_fd_readable(query *q, int fd) { return tpl_wait_fd(q, fd, false); }
bool tpl_wait_fd_writable(query *q, int fd) { return tpl_wait_fd(q, fd, true); }
#elif !defined(__wasi__)
static bool tpl_wait_fd(query *q, int fd, bool for_write)
{
	if (fd < 0)
		return true;

	for (;;) {
		if (interrupt_pending(q)) {
			errno = EINTR;
			return false;
		}

		unsigned ms;
		int wait_ms = next_alarm_delay(q, &ms) ? (int)ms : 250;

		if (wait_ms > 250)
			wait_ms = 250;

		struct pollfd pfd = { .fd = fd, .events = for_write ? POLLOUT : POLLIN };
		int n = poll(&pfd, 1, wait_ms);

		if (n != 0)
			return true;	// ready, or poll() itself errored - let the real call report it
	}
}

bool tpl_wait_fd_readable(query *q, int fd) { return tpl_wait_fd(q, fd, false); }
bool tpl_wait_fd_writable(query *q, int fd) { return tpl_wait_fd(q, fd, true); }
#else
bool tpl_wait_fd_readable(query *q, int fd)
{
	(void) q; (void) fd;
	return true;
}

bool tpl_wait_fd_writable(query *q, int fd)
{
	(void) q; (void) fd;
	return true;
}
#endif

int tpl_getline(char **lineptr, size_t *n, query *q, stream *str)
{
	if (str->is_string)
		return string_getline(lineptr, n, str);

	errno = 0;	// FIX: reset so a stale EINTR from an earlier call isn't misread as an interrupt
#if USE_OPENSSL
	if (str->ssl) {
		if (!*lineptr) {
			*lineptr = TPL_malloc(*n=1024);
			ENSURE(*lineptr);
		}

		char *dst = *lineptr;
		size_t dstlen = *n;
		int done = 0;

		while (!done) {
			if (str->srclen <= 0) {
				// FIX 7: srcbuf is char[MAX_STREAM_BUFLEN]; read at most BUFLEN-1 so the
				// NUL terminator below never writes one byte past the end.
				int rlen = SSL_read((SSL*)str->sslptr, str->srcbuf, MAX_STREAM_BUFLEN - 1);

				if (rlen <= 0) {
					if (errno == EINTR)
						return EOF;

					return -1;
				}

				str->srcbuf[rlen] = '\0';
				str->src = str->srcbuf;
				str->srclen = rlen;
			}

			while (str->srclen-- > 0) {
				int ch = *str->src++;
				*dst++ = ch;

				if (dstlen-- <= 1) {
					size_t savelen = dst - *lineptr;
					*n *= 2;
					*lineptr = TPL_realloc(*lineptr, *n);
					ENSURE(*lineptr);
					dst = *lineptr + savelen;
					dstlen = *n - savelen;
				}

				if (ch == '\n') {
					*dst = '\0';
					done = 1;
					break;
				}
			}
		}

		return dst - *lineptr;
	}
#endif

	if (str->is_socket && str->fp_out)
		fflush(str->fp_out);

	// A task's own scheduler already retries via do_yield_on_stream() when
	// its (already non-blocking, see bif_net.c) socket comes back EAGAIN -
	// same as always, unchanged here, EINTR included: an interrupted read
	// reports failure rather than passing off a half-read line as whole.
	if (q->is_task || !str->is_socket) {
		int ok = tpl_getline_fp(lineptr, n, str->fp_in);

		if (errno == EINTR) {
			clearerr(str->fp_in);
			ok = EOF;
		}

		return ok;
	}

	// A non-task query's socket is non-blocking too now, so a mid-line
	// EAGAIN is not EOF - tpl_getline_fp() has no way to tell the two
	// apart and would silently return whatever was accumulated so far as
	// though the line were complete. Accumulate byte-by-byte instead
	// so only a genuine EAGAIN waits (alarm/interrupt-aware, via
	// tpl_wait_fd_readable()) rather than either corrupting the read or
	// blocking the OS thread the way a blocking-mode fd's getc() would.
	return tpl_getline_nb(lineptr, n, q, str->fp_in, str->fp_out);
}

// The EAGAIN-aware line read above, over a plain FILE* so the tokenizer
// can use it too: once a term crosses the end of the first line, every
// further line of it comes from the parser's own getline_interruptible()
// (parser.c) rather than from here, and that path must not mistake a
// mid-line EAGAIN for end of line either. fp_flush is the write side to
// push before each read attempt, as tpl_getc() does, or NULL for none.

int tpl_getline_nb(char **lineptr, size_t *n, query *q, FILE *fp, FILE *fp_flush)
{
	if (!*lineptr) {
		*lineptr = TPL_malloc(*n = 128);
		ENSURE(*lineptr);
	}

	size_t pos = 0;

	for (;;) {
		errno = 0;

		if (fp_flush)
			fflush(fp_flush);

		int ch = fgetc(fp);

		if (errno == EINTR) {
			clearerr(fp);
			ch = EOF;
		}

		if (ch != EOF) {
			if ((pos + 1) >= *n) {
				size_t new_size = *n ? *n + (*n >> 1) : 128;
				char *new_ptr = TPL_realloc(*lineptr, new_size);

				if (!new_ptr) {
					errno = ENOMEM;
					return -1;
				}

				*lineptr = new_ptr;
				*n = new_size;
			}

			(*lineptr)[pos++] = (char)ch;

			if (ch == '\n')
				break;

			continue;
		}

		if (errno == EINTR)
			return -1;

		if (feof(fp) || !ferror(fp))
			break;

		if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			break;

		clearerr(fp);

		if (!tpl_wait_fd_readable(q, fileno(fp)))
			return -1;	// errno == EINTR, set by tpl_wait_fd_readable()
	}

	if (!pos)
		return -1;

	(*lineptr)[pos] = '\0';
	return (int)pos;
}

int tpl_close(stream *str)
{
#if USE_OPENSSL
	if (str->ssl) {
		SSL_shutdown((SSL*)str->sslptr);
		SSL_free((SSL*)str->sslptr);

		if (!--g_ctx_use_cnt) {
			SSL_CTX_free(g_ctx);
			g_ctx = NULL;
		}
	}
#endif

	int ok = 1;

	if (!str->is_memory && !str->is_popen && !str->is_string) {
		if (str->is_socket) {
			// A non-blocking socket's fflush() can return early with
			// whatever write_all() (bif_streams.c) left buffered in
			// stdio still unsent, if the send buffer happens to be full
			// at this exact moment - write_all() only guarantees every
			// tpl_write() call succeeded, not that stdio's own buffer
			// was ever actually drained to the kernel. This fd is going
			// away regardless, so there is no cost to blocking here
			// instead of dropping the tail of the stream.
			tpl_set_blocking(str);
			fflush(str->fp_out);
#if !defined(_WIN32) && !defined(__wasi__)
			shutdown(fileno(str->fp_in), SHUT_RD);
			shutdown(fileno(str->fp_out), SHUT_WR);
#endif
		}

		ok = fclose(str->fp_in);

		if (str->fp_out != str->fp_in)
			fclose(str->fp_out);
	}

	if (str->is_memory || str->is_string)
		SB_free(str->sb);

	if (str->is_string)
		ok = 0;

	// A stream closed part-way through a with_output_to/2 still owns
	// the marks of the captures that were running on it.

	while (str->captures) {
		capture *c = str->captures;
		str->captures = c->prev;
		TPL_free(c);
	}

	return ok;
}

// --- SWI-compatible socket helpers -----------------------------------
//
// These three exist for library/socket.pl (see docs/socket-swi-design.md).
// They live here rather than in bif_net.c because this file already owns
// the platform guards and the socket headers; bif_net.c calls tpl_*
// helpers and never touches a syscall directly.

// A datagram socket read that reports WHO sent it. The udp(true) option
// on '$client'/'$server' gives a SOCK_DGRAM socket, but reading it with
// the ordinary stream predicates loses the sender - which is the whole
// point of the UDP interface. Returns bytes read, or -1.

ssize_t tpl_udp_recv(stream *str, void *buf, size_t buflen, char *host, size_t hostlen, int *port)
{
#if !defined(_WIN32) && !defined(__wasi__)
	struct sockaddr_storage from;
	socklen_t fromlen = sizeof(from);
	ssize_t len = recvfrom(fileno(str->fp_in), buf, buflen, 0, (struct sockaddr*)&from, &fromlen);

	if (len < 0)
		return -1;

	char svc[32];
	host[0] = svc[0] = '\0';

	if (getnameinfo((struct sockaddr*)&from, fromlen, host, hostlen, svc, sizeof(svc),
			NI_NUMERICHOST|NI_NUMERICSERV) != 0) {
		// The datagram is still good even if we cannot name the peer.
		snprintf(host, hostlen, "unknown");
		*port = 0;
		return len;
	}

	*port = atoi(svc);
	return len;
#else
	return -1;
#endif
}

// Wait until a datagram is readable, so a receive can give up instead of
// blocking forever. poll() rather than select(): same idea, but with no
// FD_SETSIZE ceiling to fall off when a process holds many streams. The
// socket's own flags are left alone - making the descriptor non-blocking
// would change every other operation on the stream too, and this has to
// serve callers that still want a blocking read.
//
// timeout_ms of 0 polls and returns at once, which is the non-blocking
// case. Returns 1 readable, 0 timed out, -1 error.

int tpl_udp_wait(stream *str, int timeout_ms)
{
#if !defined(_WIN32) && !defined(__wasi__)
	int fd = fileno(str->fp_in);
	uint64_t deadline = monotonic_time_in_usec() + (uint64_t)timeout_ms * 1000;

	for (;;) {
		struct pollfd pfd;
		pfd.fd = fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		int rc = poll(&pfd, 1, timeout_ms);

		if (rc > 0)
			return 1;

		if (!rc)
			return 0;

		if (errno != EINTR)
			return -1;

		// A signal is not a timeout. poll() does not report how much of
		// the interval was left, so take it from the clock; the deadline
		// is what the caller asked for, not one full interval per signal.
		uint64_t now = monotonic_time_in_usec();

		if (now >= deadline)
			return 0;

		timeout_ms = (int)((deadline - now) / 1000);
	}
#else
	(void)str; (void)timeout_ms;
	return -1;
#endif
}

// Addressed datagram write. Resolves per call rather than caching: a UDP
// socket may send to many peers, so there is no one address to cache.

ssize_t tpl_udp_send(stream *str, const void *buf, size_t len, const char *host, int port)
{
#if !defined(_WIN32) && !defined(__wasi__)
	int fd = fileno(str->fp_out);

	// Resolve in the SOCKET's address family, not whatever getaddrinfo
	// prefers. tpl_server() binds the wildcard address - it passes NULL
	// as the host to getaddrinfo - and with AF_UNSPEC that comes back
	// IPv6 first, so a server socket is typically AF_INET6. Resolving
	// the destination independently then yields AF_INET for something
	// like 127.0.0.1, and sendto() rejects the mismatch with EINVAL.
	//
	// AI_V4MAPPED|AI_ALL lets an IPv4 destination still be reached from
	// a v6 socket, as a v4-mapped address. Some systems, notably OpenBSD,
	// deliberately do not support v4-mapped addresses or these flags.

	struct sockaddr_storage me;
	socklen_t melen = sizeof(me);
	int family = AF_UNSPEC;

	if (getsockname(fd, (struct sockaddr*)&me, &melen) == 0)
		family = me.ss_family;

	struct addrinfo hints, *result, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_DGRAM;

#if defined(AI_V4MAPPED) && defined(AI_ALL)
	if (family == AF_INET6)
		hints.ai_flags = AI_V4MAPPED | AI_ALL;
#endif

	char svc[20];
	snprintf(svc, sizeof(svc), "%d", port);

	if (getaddrinfo(host, svc, &hints, &result) != 0)
		return -1;

	ssize_t sent = -1;

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sent = sendto(fd, buf, len, 0, rp->ai_addr, rp->ai_addrlen);

		if (sent >= 0)
			break;

		// '$client' with udp(true) calls connect(), and BSD/macOS reject
		// sendto() with an explicit destination on a connected socket.
		// A connected datagram socket can only reach its peer anyway, so
		// the destination is either that peer or unreachable - send() is
		// the right call and gives the same result.

		if (errno == EISCONN) {
			sent = send(fd, buf, len, 0);

			if (sent >= 0)
				break;
		}
	}

	freeaddrinfo(result);
	return sent;
#else
	return -1;
#endif
}

// Name to numeric address, without opening a connection. '$client'
// reports the hostname it was given, not a resolved one, so there was
// previously no way to resolve a name at all.

bool tpl_host_address(const char *hostname, char *ip, size_t iplen)
{
#if !defined(_WIN32) && !defined(__wasi__)
	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, NULL, &hints, &result) != 0)
		return false;

	bool ok = getnameinfo(result->ai_addr, result->ai_addrlen, ip, iplen,
			NULL, 0, NI_NUMERICHOST) == 0;
	freeaddrinfo(result);
	return ok;
#else
	return false;
#endif
}

// Maps errno onto the lowercase symbol SWI's socket_error/2 reports, so
// bif_net.c can name a failure without knowing the platform's constants.
// Anything unrecognised comes back as `unknown`.

const char *tpl_socket_errname(int err)
{
	switch (err) {
#if !defined(_WIN32) && !defined(__wasi__)
	case EADDRINUSE: return "eaddrinuse";
	case EADDRNOTAVAIL: return "eaddrnotavail";
	case EAFNOSUPPORT: return "eafnosupport";
	case EACCES: return "eacces";
	case ECONNREFUSED: return "econnrefused";
	case ECONNRESET: return "econnreset";
	case EHOSTUNREACH: return "ehostunreach";
	case ENETUNREACH: return "enetunreach";
	case ETIMEDOUT: return "etimedout";
	case EPIPE: return "epipe";
	case EAGAIN: return "eagain";
	case EINVAL: return "einval";
	case EMFILE: return "emfile";
	case ENFILE: return "enfile";
	case ENOENT: return "enoent";
	case EISCONN: return "eisconn";
#endif
	default: break;
	}

	return "unknown";
}
