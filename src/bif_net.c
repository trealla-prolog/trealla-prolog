#define _XOPEN_SOURCE 700
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "network.h"
#include "prolog.h"
#include "query.h"

static void parse_host(const char *src, char hostname[1024], char path[4096], unsigned *port, int *ssl, int *domain)
{
	if (!strncmp(src, "https://", 8)) {
		src += 8;
		*ssl = 1;
		*port = 443;
	} else if (!strncmp(src, "http://", 7)) {
		src += 7;
		*ssl = 0;
		*port = 80;
	} else if (!strncmp(src, "unix://", 7)) {
		src += 7;
		*domain = 1;

		// The remainder is a filesystem path, not host/port/path. Take it
		// verbatim - the parsing below would split it at the first slash
		// and then strip the leading one, silently turning an absolute
		// path into a relative one.

		snprintf(path, 4096, "%s", src);
		hostname[0] = '\0';
		*port = 0;
		return;
	}

	if (*src == ':')
		sscanf(src, ":%u/%4095s", port, path);
	else {
		sscanf(src, "%1023[^/:]", hostname);
		const char *rest = src + strlen(hostname);

		if (*rest == ':')
			sscanf(rest, ":%u%4095s", port, path);
		else
			sscanf(rest, "%4095s", path);
	}

	hostname[1023] = '\0';
	path[4095] = '\0';

	if (path[0] == '/')
		memmove(path, path+1, strlen(path+1)+1);
}

// A build without OpenSSL refuses TLS before any socket exists, rather than
// open a plain one or fail obscurely later.

static bool ssl_unavailable(query *q)
{
	cell tmp;
	make_atom(&tmp, new_atom(q->pl, "ssl"));
	return throw_error(q, &tmp, q->st.cur_ctx, "existence_error", "feature");
}

static bool bif_sys_server_3(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,list_or_nil);
	char hostname[1024], path[4096];
	char *keyfile = "privkey.pem", *certfile = "fullchain.pem";
	int udp = 0, nodelay = 1, ssl = 0, domain = 0, level = 0;
	unsigned port = 80;
	// Left EMPTY rather than defaulting to "localhost", so that
	// tpl_server() can tell "no host given" (bind the wildcard, the
	// historical behaviour) from "host is localhost" (bind loopback
	// only). The alias default below restores "localhost" afterwards.

	hostname[0] = '\0';
	path[0] = '\0';
	char *filename = NULL;

	if (is_var(p1)) {
		port = 0;
		filename = TPL_strdup(":0");
	} else if (is_compound(p1) && (get_arity(p1) == 2)) {
		cell *p11 = deref(q, p1+1, p1_ctx);
		cell *p12 = deref(q, p1+2, p1_ctx);
		char tmpbuf[1024];

		if (is_atom(p11) && is_smallint(p12))
			snprintf(tmpbuf, sizeof(tmpbuf), "%s:%u", C_STR(q, p11), (unsigned)get_smalluint(p12));
		else if (is_atom(p11) && is_var(p12)) {
			p1 = deref(q, p12, p1_ctx);
			p1_ctx = q->latest_ctx;
			snprintf(tmpbuf, sizeof(tmpbuf), "%s:%u", C_STR(q, p11), port=0);
		} else
			return throw_error(q, p1, p1_ctx, "domain_error", "source_sink");

		filename = TPL_strdup(tmpbuf);
	} else if (is_atom(p1))
		filename = DUP_STRING(q, p1);
	else if (!is_iso_list(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "source_sink");
	else {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	}

	PROLOG_LIST_HANDLER(p3);

	while (is_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		cell *c = deref(q, h, p3_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_compound(c) && (get_arity(c) == 1)) {
			if (!CMP_STRING_TO_CSTR(q, c, "udp")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					udp = !CMP_STRING_TO_CSTR(q, c, "true") ? 1 : 0;
			} else if (!CMP_STRING_TO_CSTR(q, c, "nodelay")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					nodelay = !CMP_STRING_TO_CSTR(q, c, "true") ? 1 : 0;
			} else if (!CMP_STRING_TO_CSTR(q, c, "ssl")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					ssl = !CMP_STRING_TO_CSTR(q, c, "true") ? 1 : 0;
			} else if (!CMP_STRING_TO_CSTR(q, c, "keyfile")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					keyfile = C_STR(q, c);
			} else if (!CMP_STRING_TO_CSTR(q, c, "certfile")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					certfile = C_STR(q, c);
			} else if (!CMP_STRING_TO_CSTR(q, c, "hostname")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					slicecpy(hostname, sizeof(hostname), C_STR(q, c), C_STRLEN(q, c));
			} else if (!CMP_STRING_TO_CSTR(q, c, "scheme")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c)) {
					ssl = !CMP_STRING_TO_CSTR(q, c, "https") ? 1 : 0;
					port = 443;
				}
			} else if (!CMP_STRING_TO_CSTR(q, c, "port")) {
				c = deref(q, c + 1, c_ctx);

				if (is_integer(c))
					port = get_smallint(c);
			} else if (!CMP_STRING_TO_CSTR(q, c, "level")) {
				c = deref(q, c + 1, c_ctx);

				if (is_integer(c))
					level = (int)get_smallint(c);
			}
		}

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;
	}

	const char *url = filename;
	parse_host(url, hostname, path, &port, &ssl, &domain);
	TPL_free(filename);

	if (ssl && !USE_OPENSSL)
		return ssl_unavailable(q);

	int fd = domain
		? tpl_domain_server(path, udp)
		: tpl_server(hostname, port, udp, ssl?keyfile:NULL, ssl?certfile:NULL);

	// The stream alias has always been "localhost" when unspecified.

	if (!hostname[0])
		snprintf(hostname, sizeof(hostname), "localhost");

	if (fd == -1)
		return throw_error(q, p1, p1_ctx, "socket_error", tpl_socket_errname(errno));

	int n = new_stream(q->pl);

	if (n < 0) {
		close(fd);
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");
	}

	if (port == 0) {
		port = get_local_port(fd);
		cell tmp;
		make_int(&tmp, port);
		unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	stream *str = &q->pl->streams[n];
	CHECKED(str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL));
	sl_app(str->alias, TPL_strdup(hostname), NULL);
	CHECKED(str->filename = DUP_STRING(q, p1));
	CHECKED(str->mode = TPL_strdup("update"));
	str->is_socket = true;
	str->nodelay = nodelay;
	str->udp = udp;
	str->ssl = ssl;
	str->level = level;
	str->fp = str->fp_in = fdopen(fd, "r");	// FIX 11: also set fp so $server_tls's fileno(str->fp) works
	str->fp_out = str->fp_in;

	if (str->fp_in == NULL) {
		str->is_active = false;
		close(fd);
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_stream");
	}

	if (!str->ssl && q->is_task)
		tpl_set_nonblocking(str);

	cell tmp;
	make_int(&tmp, n);
	tmp.flags |= FLAG_INT_STREAM;
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_sys_accept_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,var);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *peer_addr = NULL;
	int peer_port = 0;
	int fd;

	// Wait in the interruptible slices reads use, so an alarm, an
	// interrupt or thread_cancel/1 reaches a thread blocked here. A
	// listener closed by another thread wakes this with an error, which
	// must be reported, not waited on or turned into a silent failure. A
	// client that gave up before being accepted (ECONNABORTED, or EAGAIN
	// after the wait said ready) is waited past.

	for (;;) {
		if (str->is_closing || !str->is_active || !str->fp_in) {
			fd = -1;
			break;
		}

		if (!q->is_task && !tpl_wait_fd_readable(q, fileno(str->fp_in), NULL))
			return throw_timeout(q);

		if (str->is_closing || !str->is_active || !str->fp_in) {
			fd = -1;
			break;
		}

		fd = tpl_accept(str, &peer_addr, &peer_port);

		if ((fd != -1) || str->is_closing)
			break;

		if ((errno == EINTR) || (errno == ECONNABORTED))
			continue;

		if (!q->is_task && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
			continue;

		break;
	}

	if (fd == -1) {
		if (str->is_closing || !str->is_active)
			return throw_error(q, pstr, pstr_ctx, "existence_error", "stream");

		if (q->is_task && ((errno == EAGAIN) || (errno == EWOULDBLOCK)))
			return do_yield_on_stream(q, str, false);

		return throw_error(q, pstr, pstr_ctx, "socket_error", tpl_socket_errname(errno));
	}

	n = new_stream(q->pl);

	if (n < 0) {
		close(fd);
		TPL_free(peer_addr);
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");
	}

	stream *str2 = &q->pl->streams[n];
	// FIX 13: new_stream() does not allocate the alias skiplist; create it before
	// sl_app() (matching bif_sys_server_3 / bif_sys_client_5).
	CHECKED(str2->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL));
	sl_app(str2->alias, TPL_strdup(str->filename), NULL);
	CHECKED(str2->filename = TPL_strdup(str->filename));
	CHECKED(str2->mode = TPL_strdup("update"));
	str2->addr = peer_addr;
	str2->port = peer_port;
	str2->is_socket = true;
	str2->nodelay = str->nodelay;
	str2->udp = str->udp;
	str2->ssl = str->ssl;
	str2->fp = str2->fp_in = fdopen(fd, "r");	// FIX 11: set fp as well

	if (str2->fp_in == NULL) {
		str2->is_active = false;
		close(fd);
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_stream");
	}

#ifndef __wasi__
	int fd2 = dup(fd);
	str2->fp_out = fdopen(fd2, "w");

	if (str2->fp_out == NULL) {
		close(fd2);
		fclose(str2->fp_in);
		str2->is_active = false;
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_stream");
	}
#else
	str2->fp_out = str2->fp_in;
#endif

	if (str->ssl) {
		const char *reason;
		str2->sslptr = tpl_enable_ssl(fd, str->filename, true, false, NULL, &reason);

		if (!str2->sslptr) {
			close(fd);
			str2->is_active = false;
			return false;
		}
	}

	// Always non-blocking now, TLS too once its handshake is done, not just
	// for a task: a blocking read on this fd cannot be broken out of by a
	// time limit or Ctrl-C (see DESIGN-GUSTTO.md), so a plain synchronous
	// reader needs the same EAGAIN-and-retry path a task already gets. The
	// choice point stays task-only: it exists for do_yield_on_stream()'s
	// retry, which a non-task read handles inline instead (see
	// tpl_getline()/retry_getc() in network.c/bif_streams.c).
	tpl_set_nonblocking(str2);

	if (!str->ssl && q->is_task) {
		CHECKED(push_choice(q));
	}

	cell tmp;
	make_int(&tmp, n);
	tmp.flags |= FLAG_INT_STREAM;
	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	return true;
}
static bool bif_sys_client_5(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,var);
	GET_NEXT_ARG(p5,list_or_nil);
	char hostname[1024], path[1024*4];
	char *cafile = NULL, *server_name = NULL;
	int udp = 0, nodelay = 1, ssl = 0, domain = 0, level = 0, verify = 1;
	hostname[0] = path[0] = '\0';
	unsigned port = 80;
	char *filename = NULL;

	if (is_atom(p1))
		filename = DUP_STRING(q, p1);
	else if (!is_list(p1)) {
		char host[1024];
		snprintf(host, sizeof(host), "%s", C_STR(q, deref(q, p1+1, p1_ctx)));
		port = (int)get_smallint(deref(q, p1+2, p1_ctx));
		filename = TPL_strdup(host);
	}

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	}

	PROLOG_LIST_HANDLER(p5);

	while (is_iso_list(p5)) {
		cell *h = PROLOG_LIST_HEAD(p5);
		cell *c = deref(q, h, p5_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_compound(c) && (get_arity(c) == 1)) {
			if (!CMP_STRING_TO_CSTR(q, c, "udp")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					udp = !CMP_STRING_TO_CSTR(q, c, "true") ? 1 : 0;
			} else if (!CMP_STRING_TO_CSTR(q, c, "nodelay")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					nodelay = !CMP_STRING_TO_CSTR(q, c, "true") ? 1 : 0;
			} else if (!CMP_STRING_TO_CSTR(q, c, "ssl")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					ssl = !CMP_STRING_TO_CSTR(q, c, "true") ? 1 : 0;
			} else if (!CMP_STRING_TO_CSTR(q, c, "cacert")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					cafile = C_STR(q, c);
			} else if (!CMP_STRING_TO_CSTR(q, c, "server_name")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					server_name = C_STR(q, c);
			} else if (!CMP_STRING_TO_CSTR(q, c, "verify")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c))
					verify = !CMP_STRING_TO_CSTR(q, c, "false") ? 0 : 1;
			} else if (!CMP_STRING_TO_CSTR(q, c, "scheme")) {
				c = deref(q, c + 1, c_ctx);

				if (is_atom(c)) {
					ssl = !CMP_STRING_TO_CSTR(q, c, "https") ? 1 : 0;
					if (ssl) port = 443;
				}
			} else if (!CMP_STRING_TO_CSTR(q, c, "port")) {
				c = deref(q, c + 1, c_ctx);

				if (is_integer(c))
					port = (int)get_smallint(c);
			} else if (!CMP_STRING_TO_CSTR(q, c, "level")) {
				c = deref(q, c + 1, c_ctx);

				if (is_integer(c))
					level = (int)get_smallint(c);
			}
		}

		p5 = PROLOG_LIST_TAIL(p5);
		p5 = deref(q, p5, p5_ctx);
		p5_ctx = q->latest_ctx;
	}

	const char *url = filename;
	parse_host(url, hostname, path, &port, &ssl, &domain);
	TPL_free(filename);

	if (ssl && !USE_OPENSSL)
		return ssl_unavailable(q);

	int fd = domain
		? tpl_domain_connect(path, udp)
		: tpl_connect(hostname, port, udp, nodelay);

	if (fd == -1)
		return throw_error(q, p1, p1_ctx, "socket_error", tpl_socket_errname(errno));

	int n = new_stream(q->pl);

	if (n < 0) {
		close(fd);
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");
	}

	stream *str = &q->pl->streams[n];
	CHECKED(str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL));
	sl_app(str->alias, DUP_STRING(q, p1), NULL);
	CHECKED(str->filename = DUP_STRING(q, p1));
	CHECKED(str->mode = TPL_strdup("update"));
	str->is_socket = true;
	str->nodelay = nodelay;
	str->udp = udp;
	str->ssl = ssl;
	str->level = level;
	str->fp = str->fp_in = fdopen(fd, "r");
	str->port = port;
	str->addr = TPL_strdup(hostname);

	if (!str->filename || !str->mode) {
		sl_destroy(str->alias);
		TPL_free(str->filename);
		TPL_free(str->mode);
		str->is_active = false;
		return false;
	}

	if (str->fp_in == NULL) {
		str->is_active = false;
		close(fd);
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_stream");
	}

#ifndef __wasi__
	int fd2 = dup(fd);
	str->fp_out = fdopen(fd2, "w");

	if (str->fp_out == NULL) {
		close(fd2);
		fclose(str->fp_in);
		str->is_active = false;
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_stream");
	}
#else
	str->fp_out = str->fp_in;
#endif

	if (str->ssl) {
		const char *reason;
		// TLS checks and names (SNI) the server as server_name(Host) says, if
		// given, not by the address connected to: a caller can resolve and
		// vet an address itself, connect to just that, and still verify the name.

		str->sslptr = tpl_enable_ssl(fd, server_name ? server_name : hostname, false, verify, cafile, &reason);

		if (!str->sslptr) {
			str->ssl = false;
			stream_close(q, n);

			return throw_error(q, p1, p1_ctx, "ssl_error", reason ? reason : "handshake_failed");
		}
	}

	// Always non-blocking now, TLS too, not just for a task - see the
	// matching comment in bif_sys_accept_2() above.
	tpl_set_nonblocking(str);

	cell tmp;
	make_string(&tmp, hostname);
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	make_string(&tmp, path);
	unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	cell tmp2;
	make_int(&tmp2, n);
	tmp2.flags |= FLAG_INT_STREAM;
	unify(q, p4, p4_ctx, &tmp2, q->st.cur_ctx);
	return true;
}

static bool bif_sys_server_tls_2(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,atom_or_var);

	if (!USE_OPENSSL)
		return ssl_unavailable(q);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	int fd = fileno(str->fp_in);
	const char *reason;
	str->sslptr = tpl_enable_ssl(fd, NULL, true, false, NULL, &reason);

	if (!str->sslptr)
		return false;						// FIX 1: bool, not NULL

	const char *hostname = tpl_servername(str);	// FIX 1: pass the stream, not the SSL*

	if (!hostname)
		return true;

	cell tmp;
	make_cstring(&tmp, hostname);
	// FIX 1: succeed iff the unify succeeds (was inverted).
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_sys_client_tls_4(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);				// verify: true or false
	GET_NEXT_ARG(p3,atom_or_var);		// cacert file, else the system store

	if (!USE_OPENSSL)
		return ssl_unavailable(q);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	int fd = fileno(str->fp_in);
	const char *hostname = C_STR(q, p1);
	bool verify = CMP_STRING_TO_CSTR(q, p2, "false") != 0;
	const char *cafile = is_atom(p3) ? C_STR(q, p3) : NULL;
	const char *reason;
	str->sslptr = tpl_enable_ssl(fd, hostname, false, verify, cafile, &reason);

	if (!str->sslptr)
		return throw_error(q, p1, p1_ctx, "ssl_error", reason ? reason : "handshake_failed");

	return true;
}

static bool bif_sys_current_host_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	char buffer[256];
	const char *host = get_local_hostname(buffer, sizeof(buffer));

	// FIX 12: host is NULL on non-POSIX or on gethostname() failure.
	if (!host)
		return throw_error(q, p1, p1_ctx, "resource_error", "hostname_unavailable");

	cell tmp;
	make_cstring(&tmp, host);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

// '$udp_recv'(+Stream, -Data, -Host, -Port, +Options)
//
// Options: max_message_size(+Bytes), default 4096 as SWI has it, and
// encoding(octet).
//
// Data normally comes back as a length-counted string, so an embedded NUL
// survives and library/socket.pl converts per udp_receive/4's as(Type).
// That path is UTF-8 text, though, so it cannot carry arbitrary bytes -
// under encoding(octet) the datagram is returned as a list of raw byte
// values instead, which is what a binary protocol needs.

static bool bif_sys_udp_recv_5(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,list_or_nil);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	size_t maxlen = 4096;
	bool octet = false;
	int timeout_ms = -1;			// no timeout(_): block as before

	PROLOG_LIST_HANDLER(p4);

	while (is_list(p4)) {
		cell *h = PROLOG_LIST_HEAD(p4);
		cell *c = deref(q, h, p4_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_compound(c) && (get_arity(c) == 1)
			&& !CMP_STRING_TO_CSTR(q, c, "max_message_size")) {
			cell *arg = deref(q, c+1, c_ctx);

			if (is_smallint(arg) && (get_smallint(arg) > 0))
				maxlen = (size_t)get_smallint(arg);
		}

		if (is_compound(c) && (get_arity(c) == 1)
			&& !CMP_STRING_TO_CSTR(q, c, "encoding")) {
			cell *arg = deref(q, c+1, c_ctx);

			if (is_atom(arg) && !CMP_STRING_TO_CSTR(q, arg, "octet"))
				octet = true;
		}

		if (is_compound(c) && (get_arity(c) == 1)
			&& !CMP_STRING_TO_CSTR(q, c, "timeout")) {
			cell *arg = deref(q, c+1, c_ctx);

			if (is_smallint(arg) && (get_smallint(arg) >= 0))
				timeout_ms = (int)get_smallint(arg);
		}

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;
	}

	// timeout(0) polls and returns at once, which is the non-blocking case;
	// a positive value sleeps in the kernel until then. Timing out is
	// reported as socket_error(timeout), which library(socket) turns into a
	// clean failure rather than an exception.

	if (timeout_ms >= 0) {
		int ready = tpl_udp_wait(str, timeout_ms);

		if (!ready)
			return throw_error(q, pstr, pstr_ctx, "socket_error", "timeout");

		if (ready < 0)
			return throw_error(q, pstr, pstr_ctx, "socket_error",
				tpl_socket_errname(errno));
	}

	char *buf = TPL_malloc(maxlen);
	CHECKED(buf);
	char host[256];
	int port = 0;
	ssize_t len = tpl_udp_recv(str, buf, maxlen, host, sizeof(host), &port);

	if (len < 0) {
		int save_errno = errno;
		TPL_free(buf);
		return throw_error(q, pstr, pstr_ctx, "socket_error", tpl_socket_errname(save_errno));
	}

	if (octet) {
		if (!init_tmp_heap(q)) {
			TPL_free(buf);
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
		}

		for (ssize_t i = 0; i < len; i++) {
			cell tmp;
			make_int(&tmp, (unsigned char)buf[i]);
			append_list(q, &tmp);
		}

		TPL_free(buf);
		cell *l = len ? end_list(q) : make_nil();
		CHECKED(l);

		if (!unify(q, p1, p1_ctx, l, q->st.cur_ctx))
			return false;
	} else {
		cell tmp;
		bool ok = make_stringn(&tmp, buf, (size_t)len);
		TPL_free(buf);

		if (!ok)
			return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

		ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);

		if (!ok)
			return false;
	}

	cell tmp2;
	make_cstring(&tmp2, host);
	bool ok = unify(q, p2, p2_ctx, &tmp2, q->st.cur_ctx);
	unshare_cell(&tmp2);

	if (!ok)
		return false;

	cell tmp3;
	make_int(&tmp3, port);
	return unify(q, p3, p3_ctx, &tmp3, q->st.cur_ctx);
}

// '$udp_send'(+Stream, +Data, +Host, +Port, +Options)
//
// Data may be an atom, a string, or a list of chars/codes, which go out
// as UTF-8. Under encoding(octet) it must instead be a list of byte
// values, sent verbatim - the UTF-8 path would turn byte 255 into two
// bytes on the wire. as(Type) is handled in library/socket.pl, which
// knows the term shapes.

static bool bif_sys_udp_send_5(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,any);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,integer);
	GET_NEXT_ARG(p4,list_or_nil);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	bool octet = false;

	PROLOG_LIST_HANDLER(p4);

	while (is_list(p4)) {
		cell *h = PROLOG_LIST_HEAD(p4);
		cell *c = deref(q, h, p4_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_compound(c) && (get_arity(c) == 1)
			&& !CMP_STRING_TO_CSTR(q, c, "encoding")) {
			cell *arg = deref(q, c+1, c_ctx);

			if (is_atom(arg) && !CMP_STRING_TO_CSTR(q, arg, "octet"))
				octet = true;
		}

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;
	}

	const char *src;
	size_t len;
	char *tofree = NULL;

	if (octet) {
		size_t cnt = 0, cap = 256;
		char *bytes = TPL_malloc(cap);
		CHECKED(bytes);
		cell *l = p1;
		pl_ctx l_ctx = p1_ctx;
		PROLOG_LIST_HANDLER(l);

		while (is_list(l)) {
			cell *h = deref(q, PROLOG_LIST_HEAD(l), l_ctx);

			if (!is_smallint(h) || (get_smallint(h) < 0) || (get_smallint(h) > 255)) {
				TPL_free(bytes);
				return throw_error(q, h, l_ctx, "type_error", "byte");
			}

			if (cnt == cap) {
				cap *= 2;
				char *tmp = TPL_realloc(bytes, cap);

				if (!tmp) {
					TPL_free(bytes);
					return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
				}

				bytes = tmp;
			}

			bytes[cnt++] = (char)get_smallint(h);
			l = deref(q, PROLOG_LIST_TAIL(l), l_ctx);
			l_ctx = q->latest_ctx;
		}

		if (!is_nil(l)) {
			TPL_free(bytes);
			return throw_error(q, p1, p1_ctx, "type_error", "list");
		}

		tofree = bytes;
		src = bytes;
		len = cnt;
	} else if (is_atom(p1) || is_string(p1)) {
		src = C_STR(q, p1);
		len = C_STRLEN(q, p1);
	} else if (is_iso_list(p1)) {
		if (!scan_is_chars_list(q, p1, p1_ctx, true))
			return throw_error(q, p1, p1_ctx, "type_error", "text");

		tofree = chars_list_to_string(q, p1, p1_ctx);
		CHECKED(tofree);
		src = tofree;
		len = strlen(tofree);
	} else if (is_nil(p1)) {
		src = "";
		len = 0;
	} else
		return throw_error(q, p1, p1_ctx, "type_error", "text");

	ssize_t sent = tpl_udp_send(str, src, len, C_STR(q, p2), (int)get_smallint(p3));
	int save_errno = errno;

	if (tofree)
		TPL_free(tofree);

	if (sent < 0)
		return throw_error(q, pstr, pstr_ctx, "socket_error", tpl_socket_errname(save_errno));

	return true;
}

// '$host_address'(+Host, -Address)
//
// Resolve without connecting. '$client' reports back the hostname it was
// handed, not a resolved address, so there was no way to do this before.

static bool bif_sys_host_address_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
	char ip[256];

	if (!tpl_host_address(C_STR(q, p1), ip, sizeof(ip)))
		return false;

	cell tmp;
	make_cstring(&tmp, ip);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

// '$host_addresses'(+Host, -Addresses): all of them, for host_address/3.

static bool bif_sys_host_addresses_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
	char ips[32][64];
	unsigned n = tpl_host_addresses(C_STR(q, p1), ips, 32);
	CHECKED(init_tmp_heap(q));

	for (unsigned i = 0; i < n; i++) {
		cell tmp;
		make_cstring(&tmp, ips[i]);
		append_list(q, &tmp);
	}

	cell *l = n ? end_list_made(q) : make_nil();
	CHECKED(l);
	return unify(q, p2, p2_ctx, l, q->st.cur_ctx);
}

static bool bif_sys_peer_addr_3(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	GET_NEXT_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	// FIX 12: addr is NULL for a listening/server stream.
	if (!str->addr)
		return false;

	cell tmp;
	make_cstring(&tmp, str->addr);
	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, str->port);
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	return true;
}

builtins g_net_bifs[] =
{
	{"$server", 3, bif_sys_server_3, "+source_sink,--stream,+list", false, false, BLAH},
	{"$accept", 2, bif_sys_accept_2, "+stream,--stream", false, false, BLAH},
	{"$client", 5, bif_sys_client_5, "+source_sink,-atom,-atom,--stream,+list", false, false, BLAH},
	{"$server_tls", 2, bif_sys_server_tls_2, "+stream,-atom", false, false, BLAH},
	{"$client_tls", 4, bif_sys_client_tls_4, "+stream,+atom,+atom,+source_sink", false, false, BLAH},
	{"$current_host", 1, bif_sys_current_host_1, "-atom", false, false, BLAH},
	{"$peer_addr", 3, bif_sys_peer_addr_3, "+stream,-atom,-integer", false, false, BLAH},
	{"$udp_recv", 5, bif_sys_udp_recv_5, "+stream,-string,-atom,-integer,+list", false, false, BLAH},
	{"$udp_send", 5, bif_sys_udp_send_5, "+stream,+term,+atom,+integer,+list", false, false, BLAH},
	{"$host_address", 2, bif_sys_host_address_2, "+atom,-atom", false, false, BLAH},
	{"$host_addresses", 2, bif_sys_host_addresses_2, "+atom,-list", false, false, BLAH},

	{0}
};
