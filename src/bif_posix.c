#if defined(__sun)
#define _POSIX_PTHREAD_SEMANTICS
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "files.h"
#include "prolog.h"
#include "query.h"

#ifndef _WIN32
#include <signal.h>
#include <unistd.h>
#endif

#if !defined(_WIN32) && !defined(__wasi__)
#define USE_SYSLOG 1
#include <syslog.h>
#endif

#if !defined(_WIN32) && !defined(__wasi__)
#define USE_POSIX_FILES 1
#define USE_POSIX_LINKS 1
#include <sys/stat.h>
#include <sys/time.h>
#elif defined(_WIN32)
#define USE_POSIX_FILES 1
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/utime.h>
#endif

#ifdef _WIN32
#define ctime_r(p1,p2) ctime(p1)
#define gmtime_r(p1,p2) gmtime(p1)
#define localtime_r(p1,p2) localtime(p1)
#endif

static bool bif_posix_strftime_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p2,compound);

	if ((p2->val_off != g_tm_s) || (get_arity(p2) != 9)) {
		return false;
	}

	const char *format = C_STR(q, p1);
	size_t length = C_STRLEN(q, p1);

	// XXX: Is this check reasonable? May strftime() return non-empty
	// result for empty format?

	if (length == 0) {
		cell tmp;
		make_atom(&tmp, g_empty_s);
		return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	}

	struct tm tm = {0};
	cell *arg;

	arg = deref(q, p2+1, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_sec = get_smallint(arg);
	arg = deref(q, p2+2, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_min = get_smallint(arg);
	arg = deref(q, p2+3, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_hour = get_smallint(arg);
	arg = deref(q, p2+4, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_mday = get_smallint(arg);
	arg = deref(q, p2+5, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_mon = get_smallint(arg);
	arg = deref(q, p2+6, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_year = get_smallint(arg);
	arg = deref(q, p2+7, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_wday = get_smallint(arg);
	arg = deref(q, p2+8, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_yday = get_smallint(arg);
	arg = deref(q, p2+9, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_isdst = get_smallint(arg);

	char *buffer = NULL;
	int tries = 0;
	const int max_tries = 5;

	while (++tries <= max_tries) {
		// make enough space for some long formats, e.g. `%c'
		length = 128 + length * 2;
		buffer = TPL_realloc(buffer, length);

		// FIXME: `0' returned by strftime() does not always indicate
		// an error, seems there is no easy way to check that.

		if (strftime(buffer, length, format, &tm) > 0) {
			cell tmp;
			make_string(&tmp, buffer);
			TPL_free(buffer);
			bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
			unshare_cell(&tmp);
			return ok;
		}
	}

	TPL_free(buffer);
	return false;
}

#if !defined(_WIN32) && !defined(__riscos__)
static bool bif_posix_strptime_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,string);
	GET_NEXT_ARG(p3,var);

    struct tm tm = {0};

	if (strptime(C_STR(q, p2), C_STR(q, p1), &tm) == NULL)
		return false;

	cell *tmp = alloc_heap(q, 10);
	make_instr(tmp, g_tm_s, NULL, 9, 0);
	pl_idx num_cells = 1;
	make_int(tmp+num_cells++, tm.tm_sec);
	make_int(tmp+num_cells++, tm.tm_min);
	make_int(tmp+num_cells++, tm.tm_hour);
	make_int(tmp+num_cells++, tm.tm_mday);
	make_int(tmp+num_cells++, tm.tm_mon);
	make_int(tmp+num_cells++, tm.tm_year);
	make_int(tmp+num_cells++, tm.tm_wday);
	make_int(tmp+num_cells++, tm.tm_yday);
	make_int(tmp+num_cells++, tm.tm_isdst);

	return unify(q, p3, p3_ctx, tmp, q->st.cur_ctx);
}
#endif

static bool bif_posix_mktime_2(query *q)
{
	GET_FIRST_ARG(p1,compound);
	GET_NEXT_ARG(p2,var);

	if ((p1->val_off != g_tm_s) || (get_arity(p1) != 9)) {
		return false;
	}

	struct tm tm = {0};
	cell *arg;

	arg = deref(q, p1+1, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_sec = get_smallint(arg);
	arg = deref(q, p1+2, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_min = get_smallint(arg);
	arg = deref(q, p1+3, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_hour = get_smallint(arg);
	arg = deref(q, p1+4, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_mday = get_smallint(arg);
	arg = deref(q, p1+5, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_mon = get_smallint(arg);
	arg = deref(q, p1+6, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_year = get_smallint(arg);
	arg = deref(q, p1+7, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_wday = get_smallint(arg);
	arg = deref(q, p1+8, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_yday = get_smallint(arg);
	arg = deref(q, p1+9, p2_ctx); if (!is_smallint(arg)) return throw_error(q, arg, p2_ctx, "type_error", "integer"); tm.tm_isdst = get_smallint(arg);

	time_t now = mktime(&tm);
	cell tmp;
	make_int(&tmp, now);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_posix_gmtime_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,var);

	time_t t = get_smallint(p1);
	struct tm tm = {0};

	if (gmtime_r(&t, &tm) == NULL)
		return 0;

	cell *tmp = alloc_heap(q, 10);
	make_instr(tmp, g_tm_s, NULL, 9, 0);
	pl_idx num_cells = 1;
	make_int(tmp+num_cells++, tm.tm_sec);
	make_int(tmp+num_cells++, tm.tm_min);
	make_int(tmp+num_cells++, tm.tm_hour);
	make_int(tmp+num_cells++, tm.tm_mday);
	make_int(tmp+num_cells++, tm.tm_mon);
	make_int(tmp+num_cells++, tm.tm_year);
	make_int(tmp+num_cells++, tm.tm_wday);
	make_int(tmp+num_cells++, tm.tm_yday);
	make_int(tmp+num_cells++, tm.tm_isdst);

	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool bif_posix_localtime_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,var);

	time_t t = get_smallint(p1);
	struct tm tm = {0};

	if (localtime_r(&t, &tm) == NULL)
		return 0;

	cell *tmp = alloc_heap(q, 10);
	make_instr(tmp, g_tm_s, NULL, 9, 0);
	pl_idx num_cells = 1;
	make_int(tmp+num_cells++, tm.tm_sec);
	make_int(tmp+num_cells++, tm.tm_min);
	make_int(tmp+num_cells++, tm.tm_hour);
	make_int(tmp+num_cells++, tm.tm_mday);
	make_int(tmp+num_cells++, tm.tm_mon);
	make_int(tmp+num_cells++, tm.tm_year);
	make_int(tmp+num_cells++, tm.tm_wday);
	make_int(tmp+num_cells++, tm.tm_yday);
	make_int(tmp+num_cells++, tm.tm_isdst);

	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool bif_posix_ctime_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,var);
	time_t when = get_smallint(p1);
	char tmpbuf[256];
	cell tmp;
	make_cstring(&tmp, ctime_r(&when, tmpbuf));
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_posix_time_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
	make_int(&tmp, time(NULL));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_posix_getpid_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
#ifndef __wasi__
	make_int(&tmp, getpid());
#else
	make_int(&tmp, -1);
#endif
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_posix_getppid_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
#if !defined(_WIN32) && !defined(__wasi__)
	make_int(&tmp, getppid());
#else
	make_int(&tmp, -1);
#endif
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_posix_fork_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	cell tmp;
#if !defined(_WIN32) && !defined(__wasi__)
	signal(SIGCHLD, SIG_IGN);
	int pid = fork();
	make_int(&tmp, pid);
#else
	make_int(&tmp, -1);
#endif
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}


// --- syslog ------------------------------------------------------------
//
// The LOG_* values are platform-specific, so the symbolic names SWI's
// library(syslog) uses are mapped here rather than in Prolog. Anything
// the platform does not define is simply absent from the table and is
// reported as a domain_error.

#if USE_SYSLOG

typedef struct { const char *name; int val; } syslog_name;

static const syslog_name s_syslog_facilities[] = {
	{"auth", LOG_AUTH},
#ifdef LOG_AUTHPRIV
	{"authpriv", LOG_AUTHPRIV},
#endif
	{"cron", LOG_CRON},
	{"daemon", LOG_DAEMON},
#ifdef LOG_FTP
	{"ftp", LOG_FTP},
#endif
	{"kern", LOG_KERN},
	{"local0", LOG_LOCAL0}, {"local1", LOG_LOCAL1},
	{"local2", LOG_LOCAL2}, {"local3", LOG_LOCAL3},
	{"local4", LOG_LOCAL4}, {"local5", LOG_LOCAL5},
	{"local6", LOG_LOCAL6}, {"local7", LOG_LOCAL7},
	{"lpr", LOG_LPR}, {"mail", LOG_MAIL}, {"news", LOG_NEWS},
	{"syslog", LOG_SYSLOG}, {"user", LOG_USER}, {"uucp", LOG_UUCP},
	{NULL, 0}
};

static const syslog_name s_syslog_priorities[] = {
	{"emerg", LOG_EMERG}, {"alert", LOG_ALERT}, {"crit", LOG_CRIT},
	{"err", LOG_ERR}, {"warning", LOG_WARNING}, {"notice", LOG_NOTICE},
	{"info", LOG_INFO}, {"debug", LOG_DEBUG},
	{NULL, 0}
};

static const syslog_name s_syslog_options[] = {
	{"cons", LOG_CONS}, {"ndelay", LOG_NDELAY}, {"nowait", LOG_NOWAIT},
	{"odelay", LOG_ODELAY},
#ifdef LOG_PERROR
	{"perror", LOG_PERROR},
#endif
	{"pid", LOG_PID},
	{NULL, 0}
};

static bool syslog_lookup(const syslog_name *tbl, const char *name, int *val)
{
	for (const syslog_name *p = tbl; p->name; p++) {
		if (!strcmp(p->name, name)) {
			*val = p->val;
			return true;
		}
	}

	return false;
}

// openlog(3) stores the pointer it is given, it does not copy the
// string, so the ident has to outlive the call. Keeping our own copy is
// not optional: the Prolog atom's storage can move or be reclaimed, and
// syslog would then read freed memory on every message.

static char *g_syslog_ident = NULL;

#endif

static bool bif_sys_openlog_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_nil);
	GET_NEXT_ARG(p3,atom);
#if USE_SYSLOG
	int facility = 0;

	if (!syslog_lookup(s_syslog_facilities, C_STR(q, p3), &facility))
		return throw_error(q, p3, p3_ctx, "domain_error", "syslog_facility");

	int mask = 0;
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		cell *c = deref(q, h, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!is_atom(c))
			return throw_error(q, c, c_ctx, "type_error", "atom");

		int opt = 0;

		if (!syslog_lookup(s_syslog_options, C_STR(q, c), &opt))
			return throw_error(q, c, c_ctx, "domain_error", "syslog_option");

		mask |= opt;
		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	if (!is_nil(p2))
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	char *ident = TPL_strdup(C_STR(q, p1));
	CHECKED(ident);
	openlog(ident, mask, facility);

	// Freed only after the new one is in place, since the old pointer is
	// live until openlog() replaces it.

	TPL_free(g_syslog_ident);
	g_syslog_ident = ident;
	return true;
#else
	return throw_error(q, p1, p1_ctx, "resource_error", "syslog_unavailable");
#endif
}

static bool bif_sys_syslog_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,any);
#if USE_SYSLOG
	int pri = 0;

	if (!syslog_lookup(s_syslog_priorities, C_STR(q, p1), &pri))
		return throw_error(q, p1, p1_ctx, "domain_error", "syslog_priority");

	const char *src;
	char *tofree = NULL;

	if (is_atom(p2) || is_string(p2)) {
		src = C_STR(q, p2);
	} else if (is_nil(p2)) {
		src = "";
	} else if (is_iso_list(p2)) {
		if (!scan_is_chars_list(q, p2, p2_ctx, true))
			return throw_error(q, p2, p2_ctx, "type_error", "text");

		tofree = chars_list_to_string(q, p2, p2_ctx);
		CHECKED(tofree);
		src = tofree;
	} else
		return throw_error(q, p2, p2_ctx, "type_error", "text");

	// "%s" rather than passing the message as the format itself - a
	// message carrying %s or %n would otherwise read arbitrary memory.

	syslog(pri, "%s", src);

	if (tofree)
		TPL_free(tofree);

	return true;
#else
	return throw_error(q, p1, p1_ctx, "resource_error", "syslog_unavailable");
#endif
}

static bool bif_sys_closelog_0(query *q)
{
#if USE_SYSLOG
	closelog();
	TPL_free(g_syslog_ident);
	g_syslog_ident = NULL;
	return true;
#else
	return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "syslog_unavailable");
#endif
}


// --- filesystem ---------------------------------------------------------
//
// Thin wrappers over the POSIX calls that library(filesex) needs and
// Trealla did not already have. Anything that is only path arithmetic -
// directory_file_path/3, relative_file_name/3 - is done in Prolog and is
// deliberately absent here.
//
// WASI gets resource_error rather than a silent no-op, so a caller finds
// out instead of believing the link was made. Windows has all but
// posix_symlink/2 and posix_readlink/2, which need privileges and
// reparse-point parsing.

#if USE_POSIX_FILES

static bool posix_file_error(query *q, cell *c, pl_ctx c_ctx, const char *op)
{
	// The errno name is carried through so a caller can tell a missing
	// path from a permission problem, as with the socket layer.

	const char *name;

	switch (errno) {
	case EACCES: name = "eacces"; break;
	case EEXIST: name = "eexist"; break;
	case EINVAL: name = "einval"; break;
	case EISDIR: name = "eisdir"; break;
#ifdef ELOOP
	case ELOOP: name = "eloop"; break;
#endif
	case EMLINK: name = "emlink"; break;
	case ENAMETOOLONG: name = "enametoolong"; break;
	case ENOENT: name = "enoent"; break;
	case ENOSPC: name = "enospc"; break;
	case ENOTDIR: name = "enotdir"; break;
	case ENOTEMPTY: name = "enotempty"; break;
	case EPERM: name = "eperm"; break;
	case EROFS: name = "erofs"; break;
	case EXDEV: name = "exdev"; break;
	default: name = "unknown"; break;
	}

	(void) op;
	return throw_error(q, c, c_ctx, "file_error", name);
}

#ifdef _WIN32

// Win32 calls report through GetLastError(), so map it onto errno first.

static bool win_file_error(query *q, cell *c, pl_ctx c_ctx, const char *op)
{
	switch (GetLastError()) {
	case ERROR_FILE_NOT_FOUND:
	case ERROR_PATH_NOT_FOUND:
	case ERROR_INVALID_DRIVE: errno = ENOENT; break;
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION: errno = EACCES; break;
	case ERROR_FILE_EXISTS:
	case ERROR_ALREADY_EXISTS: errno = EEXIST; break;
	case ERROR_NOT_SAME_DEVICE: errno = EXDEV; break;
	case ERROR_DIR_NOT_EMPTY: errno = ENOTEMPTY; break;
	case ERROR_FILENAME_EXCED_RANGE: errno = ENAMETOOLONG; break;
	case ERROR_DISK_FULL: errno = ENOSPC; break;
	case ERROR_WRITE_PROTECT: errno = EROFS; break;
	case ERROR_TOO_MANY_LINKS: errno = EMLINK; break;
	case ERROR_INVALID_NAME: errno = EINVAL; break;
	default: errno = 0; break;
	}

	return posix_file_error(q, c, c_ctx, op);
}

// A directory symlink or junction is removed with RemoveDirectory, which
// takes the link and leaves its target alone.

static bool win_is_dir_link(const char *path)
{
	DWORD a = GetFileAttributesA(path);
	return (a != INVALID_FILE_ATTRIBUTES)
		&& (a & FILE_ATTRIBUTE_DIRECTORY) && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

#endif

#endif

#define POSIX_FILES_GUARD(q, c, c_ctx) \
	return throw_error(q, c, c_ctx, "resource_error", "posix_files_unavailable")

static bool bif_posix_rmdir_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
#if USE_POSIX_FILES
	if (rmdir(C_STR(q, p1)))
		return posix_file_error(q, p1, p1_ctx, "rmdir");

	return true;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

// delete_file/1 checks the file exists first, which a *dangling*
// symlink does not - and a recursive delete creates dangling links as it
// goes, by removing a target before the link to it. unlink(2) removes
// the link itself and does not care what it points at.

static bool bif_posix_unlink_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
#if USE_POSIX_FILES
#ifdef _WIN32
	const char *path = C_STR(q, p1);

	if (win_is_dir_link(path) ? rmdir(path) : unlink(path))
		return posix_file_error(q, p1, p1_ctx, "unlink");
#else
	if (unlink(C_STR(q, p1)))
		return posix_file_error(q, p1, p1_ctx, "unlink");
#endif

	return true;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

static bool bif_posix_link_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
#if USE_POSIX_FILES
#ifdef _WIN32
	if (!CreateHardLinkA(C_STR(q, p2), C_STR(q, p1), NULL))
		return win_file_error(q, p2, p2_ctx, "link");
#else
	if (link(C_STR(q, p1), C_STR(q, p2)))
		return posix_file_error(q, p2, p2_ctx, "link");
#endif

	return true;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

static bool bif_posix_symlink_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
#if USE_POSIX_LINKS
	if (symlink(C_STR(q, p1), C_STR(q, p2)))
		return posix_file_error(q, p2, p2_ctx, "symlink");

	return true;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

static bool bif_posix_readlink_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
#if USE_POSIX_LINKS
	char buf[4096];
	ssize_t n = readlink(C_STR(q, p1), buf, sizeof(buf) - 1);

	if (n < 0)
		return posix_file_error(q, p1, p1_ctx, "readlink");

	buf[n] = '\0';
	cell tmp;
	make_cstring(&tmp, buf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

// The mode is the permission bits only, as an integer - the symbolic
// specs chmod/2 accepts are parsed in Prolog.

// Resolves symlinks, so a recursive walk can tell it has already been
// somewhere and not spin on a link that points back up its own tree.

static bool bif_posix_realpath_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
#if USE_POSIX_FILES
#ifdef _WIN32
	// The handle's final path has links and junctions resolved.

	HANDLE h = CreateFileA(C_STR(q, p1), 0,
		FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (h == INVALID_HANDLE_VALUE)
		return win_file_error(q, p1, p1_ctx, "realpath");

	char buf[MAX_PATH*4];
	DWORD n = GetFinalPathNameByHandleA(h, buf, sizeof(buf), FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
	CloseHandle(h);

	if (!n || (n >= sizeof(buf)))
		return win_file_error(q, p1, p1_ctx, "realpath");

	const char *real = buf;

	if (!strncmp(real, "\\\\?\\UNC\\", 8)) {
		buf[6] = '\\';
		real = buf + 6;
	} else if (!strncmp(real, "\\\\?\\", 4))
		real = buf + 4;

	tpl_prolog_path(buf);	// '/' as everywhere else a path reaches Prolog
	cell tmp;
	make_cstring(&tmp, real);
#else
	char *real = realpath(C_STR(q, p1), NULL);

	if (!real)
		return posix_file_error(q, p1, p1_ctx, "realpath");

	cell tmp;
	make_cstring(&tmp, real);
	free(real);
#endif
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

static bool bif_posix_chmod_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,integer);
#if USE_POSIX_FILES
	if (chmod(C_STR(q, p1), (mode_t)(get_smallint(p2) & 07777)))
		return posix_file_error(q, p1, p1_ctx, "chmod");

	return true;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

static bool bif_posix_file_mode_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
#if USE_POSIX_FILES
	struct stat st;

	if (stat(C_STR(q, p1), &st))
		return posix_file_error(q, p1, p1_ctx, "stat");

	cell tmp;
	make_int(&tmp, st.st_mode & 07777);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

// lstat, not stat: a symlink must report as a symlink or a recursive
// delete would follow it out of the tree it was asked to remove.

static bool bif_posix_file_type_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
#if USE_POSIX_FILES
	const char *type;
#ifdef _WIN32
	// Every reparse point counts as a symlink, junctions included, so a
	// recursive delete removes the link and never empties its target.

	DWORD a = GetFileAttributesA(C_STR(q, p1));

	if (a == INVALID_FILE_ATTRIBUTES)
		return win_file_error(q, p1, p1_ctx, "lstat");

	if (a & FILE_ATTRIBUTE_REPARSE_POINT)
		type = "symlink";
	else if (a & FILE_ATTRIBUTE_DIRECTORY)
		type = "directory";
	else if (a & FILE_ATTRIBUTE_DEVICE)
		type = "char_device";
	else
		type = "regular";
#else
	struct stat st;

	if (lstat(C_STR(q, p1), &st))
		return posix_file_error(q, p1, p1_ctx, "lstat");

	switch (st.st_mode & S_IFMT) {
	case S_IFREG: type = "regular"; break;
	case S_IFDIR: type = "directory"; break;
	case S_IFLNK: type = "symlink"; break;
	case S_IFIFO: type = "fifo"; break;
	case S_IFSOCK: type = "socket"; break;
	case S_IFCHR: type = "char_device"; break;
	case S_IFBLK: type = "block_device"; break;
	default: type = "unknown"; break;
	}
#endif

	cell tmp;
	make_atom(&tmp, new_atom(q->pl, type));
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

// Access, modified and changed times as floating-point seconds, which is
// how set_time_file/3 reports them.

static bool bif_posix_file_times_4(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,var);
#if USE_POSIX_FILES
	struct stat st;

	if (stat(C_STR(q, p1), &st))
		return posix_file_error(q, p1, p1_ctx, "stat");

	cell tmp;
	make_float(&tmp, (double)st.st_atime);

	if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_float(&tmp, (double)st.st_mtime);

	if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_float(&tmp, (double)st.st_ctime);
	return unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

// There is no portable way to set the changed time - it is maintained by
// the kernel - so set_time_file/3 rejects changed() rather than pretend.

static bool bif_posix_set_file_times_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,number);
	GET_NEXT_ARG(p3,number);
#if USE_POSIX_FILES
	double atime = is_float(p2) ? get_float(p2) : (double)get_smallint(p2);
	double mtime = is_float(p3) ? get_float(p3) : (double)get_smallint(p3);
#ifdef _WIN32
	// Whole seconds only, as stat() reports them back.

	struct _utimbuf ut;
	ut.actime = (time_t)atime;
	ut.modtime = (time_t)mtime;

	if (_utime(C_STR(q, p1), &ut))
		return posix_file_error(q, p1, p1_ctx, "utimes");
#else
	struct timeval tv[2];
	tv[0].tv_sec = (time_t)atime;
	tv[0].tv_usec = (suseconds_t)((atime - (double)(time_t)atime) * 1000000);
	tv[1].tv_sec = (time_t)mtime;
	tv[1].tv_usec = (suseconds_t)((mtime - (double)(time_t)mtime) * 1000000);

	if (utimes(C_STR(q, p1), tv))
		return posix_file_error(q, p1, p1_ctx, "utimes");
#endif

	return true;
#else
	POSIX_FILES_GUARD(q, p1, p1_ctx);
#endif
}

builtins g_posix_bifs[] =
{
    {"posix_strftime", 3, bif_posix_strftime_3, "+atom,-atom,+compound", false, false, BLAH},
#if !defined(_WIN32) && !defined(__riscos__)
    {"posix_strptime", 3, bif_posix_strptime_3, "+atom,+atom,-compound", false, false, BLAH},
#endif
	{"posix_gmtime", 2, bif_posix_gmtime_2, "+integer,-compound", false, false, BLAH},
	{"posix_localtime", 2, bif_posix_localtime_2, "+integer,-compound", false, false, BLAH},
	{"posix_mktime", 2, bif_posix_mktime_2, "+compound,-integer", false, false, BLAH},
	{"posix_ctime", 2, bif_posix_ctime_2, "+integer,-atom", false, false, BLAH},
	{"posix_time", 1, bif_posix_time_1, "-integer", false, false, BLAH},

	{"posix_getppid", 1, bif_posix_getppid_1, "-integer", false, false, BLAH},
	{"posix_getpid", 1, bif_posix_getpid_1, "-integer", false, false, BLAH},
	{"posix_fork", 1, bif_posix_fork_1, "-integer", false, false, BLAH},

	{"posix_rmdir", 1, bif_posix_rmdir_1, "+atom", false, false, BLAH},
	{"posix_unlink", 1, bif_posix_unlink_1, "+atom", false, false, BLAH},
	{"posix_link", 2, bif_posix_link_2, "+atom,+atom", false, false, BLAH},
	{"posix_symlink", 2, bif_posix_symlink_2, "+atom,+atom", false, false, BLAH},
	{"posix_readlink", 2, bif_posix_readlink_2, "+atom,-atom", false, false, BLAH},
	{"posix_realpath", 2, bif_posix_realpath_2, "+atom,-atom", false, false, BLAH},
	{"posix_chmod", 2, bif_posix_chmod_2, "+atom,+integer", false, false, BLAH},
	{"posix_file_mode", 2, bif_posix_file_mode_2, "+atom,-integer", false, false, BLAH},
	{"posix_file_type", 2, bif_posix_file_type_2, "+atom,-atom", false, false, BLAH},
	{"posix_file_times", 4, bif_posix_file_times_4, "+atom,-float,-float,-float", false, false, BLAH},
	{"posix_set_file_times", 3, bif_posix_set_file_times_3, "+atom,+number,+number", false, false, BLAH},

	{"$openlog", 3, bif_sys_openlog_3, "+atom,+list,+atom", false, false, BLAH},
	{"$syslog", 2, bif_sys_syslog_2, "+atom,+term", false, false, BLAH},
	{"$closelog", 0, bif_sys_closelog_0, NULL, false, false, BLAH},

	// For Logtalk...

	{"pid", 1, bif_posix_getpid_1, "-integer", false, false, BLAH},

	{0}
};
