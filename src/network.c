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
#include <openssl/x509v3.h>
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

// Before 1.0.2 OpenSSL can't check a certificate names the host, so
// verify(true) is refused there rather than half done.

#if OPENSSL_VERSION_NUMBER >= 0x10002000L
#define TLS_CHECKS_HOST 1
#else
#define TLS_CHECKS_HOST 0
#define SSL_is_server(s) ((s)->server)
#endif
#endif

#include "history.h"
#include "network.h"
#include "query.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
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

// Windows sockets are made non-overlapped, since only those read and write through the C runtime once
// _open_osfhandle() makes them descriptors, which is how every stream holds a socket. Each Winsock call gets
// the SOCKET back from the descriptor, and a Winsock error is turned into the errno the callers read. Reads
// through the C runtime block whatever FIONBIO says, so a Windows socket stays blocking.

#if defined(_WIN32)
typedef SOCKET tpl_sock;
#define TPL_NO_SOCK INVALID_SOCKET
#define SOCK_OF(fd) ((SOCKET)_get_osfhandle(fd))
#define TPL_SOCK_ERRNO() (errno = tpl_wsa_errno())

static bool tpl_wsa_start(void)
{
	static bool started = false;
	WSADATA wsa_data;

	if (!started && !WSAStartup(MAKEWORD(2, 2), &wsa_data))
		started = true;

	return started;
}

static int tpl_wsa_errno(void)
{
	switch (WSAGetLastError()) {
	case WSAEADDRINUSE: return EADDRINUSE;
	case WSAEADDRNOTAVAIL: return EADDRNOTAVAIL;
	case WSAEAFNOSUPPORT: return EAFNOSUPPORT;
	case WSAEACCES: return EACCES;
	case WSAECONNREFUSED: return ECONNREFUSED;
	case WSAECONNRESET: return ECONNRESET;
	case WSAECONNABORTED: return ECONNABORTED;
	case WSAEHOSTUNREACH: return EHOSTUNREACH;
	case WSAENETUNREACH: return ENETUNREACH;
	case WSAETIMEDOUT: return ETIMEDOUT;
	case WSAEWOULDBLOCK: return EAGAIN;
	case WSAEINTR: return EINTR;
	case WSAEINVAL: return EINVAL;
	case WSAEMFILE: return EMFILE;
	case WSAEISCONN: return EISCONN;
	case WSAHOST_NOT_FOUND: return ENOENT;
	default: return EIO;
	}
}

static tpl_sock tpl_sock_new(int family, int type, int protocol)
{
	if (!tpl_wsa_start())
		return INVALID_SOCKET;

	return WSASocketW(family, type, protocol, NULL, 0, 0);
}

#define tpl_sock_close(s) closesocket(s)
#define TPL_SOCK_START() do { if (!tpl_wsa_start()) { errno = EIO; return -1; } } while (0)

static int tpl_sock_fd(tpl_sock s)
{
	int fd = _open_osfhandle((intptr_t)s, 0);

	if (fd < 0)
		closesocket(s);

	return fd;
}
#elif !defined(__wasi__)
typedef int tpl_sock;
#define TPL_NO_SOCK (-1)
#define SOCK_OF(fd) (fd)
#define TPL_SOCK_ERRNO() ((void)0)
#define tpl_sock_new socket
#define tpl_sock_close close
#define tpl_sock_fd(s) (s)
#define TPL_SOCK_START() do { } while (0)
#endif


int get_local_port(int clientSock) {
#if !defined(__wasi__)
	// Room for an IPv6 address, which localhost may bind: POSIX truncates a short one, but Winsock refuses it.
	struct sockaddr_storage ss;
	socklen_t addrlen = sizeof(ss);

	if (getsockname(SOCK_OF(clientSock), (struct sockaddr *)&ss, &addrlen) == 0) {
		if (ss.ss_family == AF_INET6)
			return ntohs(((struct sockaddr_in6*)&ss)->sin6_port);

		return ntohs(((struct sockaddr_in*)&ss)->sin_port);
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
#elif defined(_WIN32)
	if (!tpl_wsa_start() || gethostname(hostname_buffer, (int)buffer_size))
		return NULL;

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
#if !defined(__wasi__)
	NET_CHECK_METAL();
	TPL_SOCK_START();
	struct addrinfo hints, *result, *rp;
	tpl_sock fd = TPL_NO_SOCK;
	int status, save_errno = ECONNREFUSED;

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
		fd = tpl_sock_new(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (fd == TPL_NO_SOCK) {
			perror("socket");
			continue;
		}

		int flag = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag));
		//setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));

		if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;

		// Saved before the cleanup below, which may clobber it.

		TPL_SOCK_ERRNO();
		save_errno = errno;
		tpl_sock_close(fd);
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
	return tpl_sock_fd(fd);
#else
	return -1;
#endif
}

int tpl_server(const char *hostname, unsigned port, bool udp, const char *keyfile, const char *certfile)
{
#if !defined(__wasi__)
	NET_CHECK_METAL();
	TPL_SOCK_START();
	struct addrinfo hints, *result, *rp;
	tpl_sock fd = TPL_NO_SOCK;
	int status, save_errno = EADDRNOTAVAIL;

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
		fd = tpl_sock_new(rp->ai_family, rp->ai_socktype, rp->ai_protocol);

		if (fd == TPL_NO_SOCK) {
			perror("socket");
			continue;
		}

		int flag = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag));
		//setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));

#if defined(_WIN32)
		// An IPv6 socket takes IPv4 peers too, as it does by default on Linux and macOS; Windows makes it IPv6 only.
		if (rp->ai_family == AF_INET6) {
			flag = 0;
			setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&flag, sizeof(flag));
		}
#endif

		if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;

		// Kept so the caller can report *why*. freeaddrinfo and close
		// below are both free to clobber errno, so it is saved here
		// rather than read at the return.

		TPL_SOCK_ERRNO();
		save_errno = errno;
		tpl_sock_close(fd);
	}

	freeaddrinfo(result);

	if (rp == NULL) {
		errno = save_errno;
		return -1;
	}

	if (udp)
		return tpl_sock_fd(fd);

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
			tpl_sock_close(fd);
			return 0;
		}

		if (!SSL_CTX_use_certificate_file(g_ctx, !certfile?keyfile:certfile, SSL_FILETYPE_PEM)) {
			printf("SSL load certificate failed: %s\n", !certfile?keyfile:certfile);
			ERR_print_errors_fp(stderr);
			tpl_sock_close(fd);
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

	return tpl_sock_fd(fd);
#else
	return -1;
#endif
}

int tpl_accept(stream *str, char **addr, int *port)
{
#if !defined(__wasi__)
	NET_CHECK_METAL();
	struct sockaddr_storage ss = {0};
	socklen_t len = sizeof(ss);
	tpl_sock fd = accept(SOCK_OF(fileno(str->fp_in)), (struct sockaddr*)&ss, &len);

	// FIX 9: any accept() failure leaves fd == -1; bail before touching it so
	// setsockopt() is never called on an invalid descriptor.
	if (fd == TPL_NO_SOCK) {
		TPL_SOCK_ERRNO();
		return -1;
	}

	// A dual-stack listener is IPv6: give its IPv4 peers, which arrive as
	// ::ffff:a.b.c.d, in their plain IPv4 form.

	char buf[INET6_ADDRSTRLEN] = "";
	unsigned peer_port = 0;

	if (ss.ss_family == AF_INET6) {
		const struct sockaddr_in6 *sa6 = (const struct sockaddr_in6*)&ss;
		peer_port = ntohs(sa6->sin6_port);

		if (IN6_IS_ADDR_V4MAPPED(&sa6->sin6_addr))
			inet_ntop(AF_INET, &sa6->sin6_addr.s6_addr[12], buf, sizeof(buf));
		else
			inet_ntop(AF_INET6, &sa6->sin6_addr, buf, sizeof(buf));
	} else if (ss.ss_family == AF_INET) {
		const struct sockaddr_in *sa = (const struct sockaddr_in*)&ss;
		peer_port = ntohs(sa->sin_port);
		inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf));
	}

	if (addr)
		*addr = TPL_strdup(buf);

	if (port)
		*port = peer_port;

	struct linger l;
	l.l_onoff = 0;
	l.l_linger = 0;
	setsockopt(fd, SOL_SOCKET, SO_LINGER, (char*)&l, sizeof(l));
	int flag = 1;
	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (char*)&flag, sizeof(flag));
	flag = str->nodelay;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag));
	return tpl_sock_fd(fd);
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

// A TLS socket stays blocking under SSL_read(), so its timeout(_) is the
// kernel's receive timeout rather than tpl_wait_fd()'s deadline. There a
// zero means none, so a timeout of 0 becomes the shortest it can say.

void tpl_set_read_timeout(stream *str)
{
#if USE_OPENSSL && !defined(__wasi__)
	if (!str->ssl || !str->fp_in)
		return;

	int ms = str->timeout_ms;
#if defined(_WIN32)
	DWORD tv = ms < 0 ? 0 : ms ? (DWORD)ms : 1;
	setsockopt(SOCK_OF(fileno(str->fp_in)), SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
	struct timeval tv = { 0, 0 };

	if (ms > 0) {
		tv.tv_sec = ms / 1000;
		tv.tv_usec = (ms % 1000) * 1000;
	} else if (!ms)
		tv.tv_usec = 1;

	setsockopt(fileno(str->fp_in), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
#else
	(void)str;
#endif
}

#if USE_OPENSSL
// A client gets its own context, so verification settings never leak into
// the server's shared one, and the SSL keeps it alive until SSL_free().

static SSL *client_ssl(int fd, const char *hostname, bool verify, const char *cafile, const char **reason)
{
	if (verify && !TLS_CHECKS_HOST) {
		*reason = "unavailable";
		return NULL;
	}

	SSL_CTX *ctx = SSL_CTX_new(TLS_CLIENT_METHOD_FUNC());

	if (!ctx) {
		*reason = "handshake_failed";
		return NULL;
	}

	if (verify) {
		if (cafile ? !SSL_CTX_load_verify_locations(ctx, cafile, NULL) : !SSL_CTX_set_default_verify_paths(ctx)) {
			SSL_CTX_free(ctx);
			ERR_clear_error();
			*reason = "cacert_load_failed";
			return NULL;
		}

		SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
	}

	SSL *ssl = SSL_new(ctx);
	SSL_CTX_free(ctx);

	if (!ssl) {
		*reason = "handshake_failed";
		return NULL;
	}

	// A numeric host is checked against the certificate's IP addresses and,
	// per RFC 6066, never sent as SNI.

	ASN1_OCTET_STRING *ip = a2i_IPADDRESS(hostname);	// OpenSSL's parser, so no socket headers
	bool is_ip = ip != NULL;
	ASN1_OCTET_STRING_free(ip);

#if TLS_CHECKS_HOST
	if (verify) {
		if (is_ip)
			X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl), hostname);
		else
			X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), hostname, 0);
	}
#endif

	if (!is_ip)
		SSL_set_tlsext_host_name(ssl, hostname);

	SSL_set_fd(ssl, (int)SOCK_OF(fd));

	if (SSL_connect(ssl) <= 0) {
		long v = SSL_get_verify_result(ssl);
		*reason = v != X509_V_OK ? "certificate_verify_failed" : "handshake_failed";
#if TLS_CHECKS_HOST
		*reason = v == X509_V_ERR_HOSTNAME_MISMATCH ? "hostname_mismatch"
			: v == X509_V_ERR_IP_ADDRESS_MISMATCH ? "ip_address_mismatch"
			: *reason;
#endif
		SSL_free(ssl);
		ERR_clear_error();
		return NULL;
	}

	return ssl;
}
#endif

void *tpl_enable_ssl(int fd, const char *hostname, bool is_server, bool verify, const char *cafile, const char **reason)
{
	*reason = NULL;

#if USE_OPENSSL
	if (!is_server)
		return client_ssl(fd, hostname, verify, cafile, reason);

	if (!g_ctx_use_cnt++) {
		SSL_load_error_strings();
		g_ctx = SSL_CTX_new(TLS_SERVER_METHOD_FUNC());
	}

	SSL *ssl = SSL_new(g_ctx);
	SSL_set_fd(ssl, (int)SOCK_OF(fd));

	if (SSL_accept(ssl) == -1) {
		fprintf(stderr, "SSL_accept failed\n");
		ERR_print_errors_fp(stderr);
		SSL_free(ssl);
		return NULL;
	}

	return ssl;
#else
	(void) fd;
	(void) hostname;
	(void) is_server;
	(void) verify;
	(void) cafile;
	*reason = "unavailable";
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

	size_t len = fwrite(ptr, 1, nbytes, str->fp_out?str->fp_out:str->fp);

	if (str->is_pipe)
		fflush(str->fp_out);

	return len;
}

#if USE_OPENSSL
// An SSL_read() that the receive timeout above cut short reports as a
// timed-out wait does: errno = EINTR, with str->timed_out marked.

static void ssl_check_timeout(stream *str, int rlen)
{
	if (str->timeout_ms < 0)
		return;

	int err = SSL_get_error((SSL*)str->sslptr, rlen);

	if ((err != SSL_ERROR_WANT_READ) && (err != SSL_ERROR_SYSCALL))
		return;

#if defined(_WIN32)
	int wsa = WSAGetLastError();

	if ((wsa != WSAETIMEDOUT) && (wsa != WSAEWOULDBLOCK))
		return;
#else
	if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
		return;
#endif

	str->timed_out = true;
	errno = EINTR;
}
#endif

int tpl_getc(stream *str)
{
	if (str->is_string)
		return string_getc(str);

	errno = 0;	// FIX: reset so a stale EINTR from an earlier call isn't misread as an interrupt
	str->timed_out = false;
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
			ssl_check_timeout(str, rlen);

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
	str->timed_out = false;
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

		if (ok <= 0)
			ssl_check_timeout(str, ok);

		// Nothing read, not EOF, which as a size_t would be a huge length.

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return 0;
		}

		return ok < 0 ? 0 : (size_t)ok;			// avoid returning a huge size_t on error
	}
#endif

	if (str->is_socket && str->fp_out)
		fflush(str->fp_out);

	size_t ok = fread(ptr, 1, len, str->fp_in);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		ok = 0;
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
// A read also gives up once str's timeout(_), if it has one, runs out,
// and marks str->timed_out for throw_read_timeout() to report.

// The next slice, cut short by an alarm or the deadline; false when the deadline has passed.

static bool next_wait_ms(query *q, uint64_t deadline, int *wait_ms)
{
	unsigned ms;
	*wait_ms = next_alarm_delay(q, &ms) ? (int)ms : 250;

	if (*wait_ms > 250)
		*wait_ms = 250;

	if (!deadline)
		return true;

	uint64_t now = monotonic_time_in_usec();

	if (now >= deadline)
		return false;

	uint64_t left_ms = (deadline - now + 999) / 1000;

	if (left_ms < (uint64_t)*wait_ms)
		*wait_ms = (int)left_ms;

	return true;
}

static uint64_t read_deadline(stream *str)
{
	if (!str)
		return 0;

	str->timed_out = false;

	if (str->timeout_ms < 0)
		return 0;

	return monotonic_time_in_usec() + (uint64_t)str->timeout_ms * 1000;
}

#if defined(_WIN32)
// select() rather than WSAPoll(), on the SOCKET under the stream's descriptor (see tpl_sock_fd()).
static bool tpl_wait_fd(query *q, int fd, bool for_write, stream *str)
{
	if (fd < 0)
		return true;

	SOCKET sock = SOCK_OF(fd);
	uint64_t deadline = read_deadline(str);

	for (;;) {
		if (interrupt_pending(q) || cancel_pending(q) || alert_pending(q)) {
			errno = EINTR;
			return false;
		}

		int wait_ms;

		if (!next_wait_ms(q, deadline, &wait_ms)) {
			str->timed_out = true;
			errno = EINTR;
			return false;
		}

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

bool tpl_wait_fd_readable(query *q, int fd, stream *str) { return tpl_wait_fd(q, fd, false, str); }
bool tpl_wait_fd_writable(query *q, int fd) { return tpl_wait_fd(q, fd, true, NULL); }
#elif !defined(__wasi__)
static bool tpl_wait_fd(query *q, int fd, bool for_write, stream *str)
{
	if (fd < 0)
		return true;

	uint64_t deadline = read_deadline(str);

	for (;;) {
		if (interrupt_pending(q) || cancel_pending(q) || alert_pending(q)) {
			errno = EINTR;
			return false;
		}

		int wait_ms;

		if (!next_wait_ms(q, deadline, &wait_ms)) {
			str->timed_out = true;
			errno = EINTR;
			return false;
		}

		struct pollfd pfd = { .fd = fd, .events = for_write ? POLLOUT : POLLIN };
		int n = poll(&pfd, 1, wait_ms);

		if ((n < 0) && (errno == EINTR))
			continue;		// a signal: round again, where an alert or interrupt is seen

		if (n != 0)
			return true;	// ready, or poll() itself errored - let the real call report it
	}
}

bool tpl_wait_fd_readable(query *q, int fd, stream *str) { return tpl_wait_fd(q, fd, false, str); }
bool tpl_wait_fd_writable(query *q, int fd) { return tpl_wait_fd(q, fd, true, NULL); }
#else
bool tpl_wait_fd_readable(query *q, int fd, stream *str)
{
	(void) q; (void) fd; (void) str;
	return true;
}

bool tpl_wait_fd_writable(query *q, int fd)
{
	(void) q; (void) fd;
	return true;
}
#endif

// A task's line from its socket. Its scheduler, not this, waits for more
// (see do_yield_on_read()), so a line cut short by EAGAIN is kept on the
// stream rather than passed off as whole, and taken up again when the
// task comes back to the read. The stream's error stays set for the
// caller to yield on. An interrupted read reports failure, as ever.

static int tpl_getline_task(char **lineptr, size_t *n, stream *str)
{
	FILE *fp = str->fp_in;
	size_t pos = str->line_part_len;

	if (!*lineptr || (*n < pos + 128)) {
		char *ptr = TPL_realloc(*lineptr, pos + 128);
		ENSURE(ptr);
		*lineptr = ptr;
		*n = pos + 128;
	}

	if (pos)
		memcpy(*lineptr, str->line_part, pos);

	TPL_free(str->line_part);
	str->line_part = NULL;
	str->line_part_len = 0;

	for (;;) {
		errno = 0;
		int ch = fgetc(fp);

		if (ch == EOF) {
			if (errno == EINTR) {
				clearerr(fp);
				return EOF;
			}

			if (pos && ferror(fp) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
				str->line_part = TPL_malloc(pos);
				ENSURE(str->line_part);
				memcpy(str->line_part, *lineptr, pos);
				str->line_part_len = pos;
				return EOF;
			}

			break;
		}

		if ((pos + 1) >= *n) {
			size_t new_size = *n + (*n >> 1);
			char *ptr = TPL_realloc(*lineptr, new_size);
			ENSURE(ptr);
			*lineptr = ptr;
			*n = new_size;
		}

		(*lineptr)[pos++] = (char)ch;

		if (ch == '\n')
			break;
	}

	if (!pos)
		return EOF;

	(*lineptr)[pos] = '\0';
	return (int)pos;
}

int tpl_getline(char **lineptr, size_t *n, query *q, stream *str)
{
	if (str->is_string)
		return string_getline(lineptr, n, str);

	errno = 0;	// FIX: reset so a stale EINTR from an earlier call isn't misread as an interrupt
	str->timed_out = false;
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
					ssl_check_timeout(str, rlen);

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

	if (q->is_task && str->is_socket)
		return tpl_getline_task(lineptr, n, str);

	if (!str->is_socket) {
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
	return tpl_getline_nb(lineptr, n, q, str->fp_in, str->fp_out, str);
}

// The EAGAIN-aware line read above, over a plain FILE* so the tokenizer
// can use it too: once a term crosses the end of the first line, every
// further line of it comes from the parser's own getline_interruptible()
// (parser.c) rather than from here, and that path must not mistake a
// mid-line EAGAIN for end of line either. fp_flush is the write side to
// push before each read attempt, as tpl_getc() does, or NULL for none.
// str, if any, is the stream whose timeout(_) bounds each wait.

int tpl_getline_nb(char **lineptr, size_t *n, query *q, FILE *fp, FILE *fp_flush, stream *str)
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

		if (!tpl_wait_fd_readable(q, fileno(fp), str))
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
		// Only server-side streams hold a count on the shared server context.
		bool is_client = str->sslptr && !SSL_is_server((SSL*)str->sslptr);
		SSL_shutdown((SSL*)str->sslptr);
		SSL_free((SSL*)str->sslptr);

		if (!is_client && !--g_ctx_use_cnt) {
			SSL_CTX_free(g_ctx);
			g_ctx = NULL;
		}
	}
#endif

	int ok = 1;

	if (!str->is_popen && !str->is_string) {
		if (str->is_socket) {
			// A non-blocking socket's fflush() can return early with
			// whatever write_all() (bif_streams.c) left buffered in
			// stdio still unsent, if the send buffer happens to be full
			// at this exact moment - write_all() only guarantees every
			// tpl_write() call succeeded, not that stdio's own buffer
			// was ever actually drained to the kernel. This fd is going
			// away regardless, so there is no cost to blocking here
			// instead of dropping the tail of the stream.
			str->is_closing = true;
			tpl_set_blocking(str);
			fflush(str->fp_out);
#if defined(_WIN32)
			// Winsock knows the SOCKET only by its first handle, not fp_out's _dup() of it.
			shutdown(SOCK_OF(fileno(str->fp_in)), SD_BOTH);
#elif !defined(__wasi__)
			shutdown(fileno(str->fp_in), SHUT_RD);
			shutdown(fileno(str->fp_out), SHUT_WR);
#endif
		}

		ok = fclose(str->fp_in);

		if (str->fp_out != str->fp_in)
			fclose(str->fp_out);
	}

	if (str->is_string) {
		SB_free(str->sb);
		ok = 0;
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
#if !defined(__wasi__)
	struct sockaddr_storage from;
	socklen_t fromlen = sizeof(from);
	ssize_t len = recvfrom(SOCK_OF(fileno(str->fp_in)), (char*)buf, buflen, 0, (struct sockaddr*)&from, &fromlen);

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
#elif defined(_WIN32)
	fd_set fds;
	FD_ZERO(&fds);
	FD_SET(SOCK_OF(fileno(str->fp_in)), &fds);
	struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
	int rc = select(0, &fds, NULL, NULL, timeout_ms < 0 ? NULL : &tv);
	return rc > 0 ? 1 : !rc ? 0 : -1;
#else
	(void)str; (void)timeout_ms;
	return -1;
#endif
}

// Addressed datagram write. Resolves per call rather than caching: a UDP
// socket may send to many peers, so there is no one address to cache.

ssize_t tpl_udp_send(stream *str, const void *buf, size_t len, const char *host, int port)
{
#if !defined(__wasi__)
	tpl_sock fd = SOCK_OF(fileno(str->fp_in));	// the same socket as fp_out's, and on Windows the handle Winsock knows

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
		sent = sendto(fd, (const char*)buf, len, 0, rp->ai_addr, rp->ai_addrlen);

		if (sent >= 0)
			break;

		TPL_SOCK_ERRNO();

		// '$client' with udp(true) calls connect(), and BSD/macOS reject
		// sendto() with an explicit destination on a connected socket.
		// A connected datagram socket can only reach its peer anyway, so
		// the destination is either that peer or unreachable - send() is
		// the right call and gives the same result.

		if (errno == EISCONN) {
			sent = send(fd, (const char*)buf, len, 0);

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
#if !defined(__wasi__)
#if defined(_WIN32)
	if (!tpl_wsa_start())
		return false;
#endif
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

// Every address the resolver gives for hostname, as numeric strings and each
// once, so a caller can vet them all before connecting to one of them.

unsigned tpl_host_addresses(const char *hostname, char ips[][64], unsigned max)
{
#if !defined(__wasi__)
#if defined(_WIN32)
	if (!tpl_wsa_start())
		return 0;
#endif
	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(hostname, NULL, &hints, &result) != 0)
		return 0;

	unsigned n = 0;

	for (struct addrinfo *rp = result; rp && (n < max); rp = rp->ai_next) {
		if (getnameinfo(rp->ai_addr, rp->ai_addrlen, ips[n], 64, NULL, 0, NI_NUMERICHOST))
			continue;

		bool seen = false;

		for (unsigned i = 0; (i < n) && !seen; i++)
			seen = !strcmp(ips[i], ips[n]);

		if (!seen)
			n++;
	}

	freeaddrinfo(result);
	return n;
#else
	(void) hostname;
	(void) ips;
	(void) max;
	return 0;
#endif
}

// Maps errno onto the lowercase symbol SWI's socket_error/2 reports, so
// bif_net.c can name a failure without knowing the platform's constants.
// Anything unrecognised comes back as `unknown`.

const char *tpl_socket_errname(int err)
{
	switch (err) {
#if !defined(__wasi__)
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
