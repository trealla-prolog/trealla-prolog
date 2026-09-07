#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__APPLE__)
#include <Availability.h>
#endif

#if !defined(_WIN32) && !defined(__wasi__) && !defined(__ANDROID__) && !defined(__riscos__)
#include <spawn.h>
#include <sys/wait.h>
#endif

#if !defined(_WIN32) && !defined(__wasi__)
#include <sys/ioctl.h>
#if defined(__sun)
#include <sys/termios.h>
#endif
#endif

#include "history.h"
#include "module.h"
#include "parser.h"
#include "query.h"

#ifdef _WIN32
#include <windows.h>
#define unsetenv(p1)
#define setenv(p1,p2,p3) _putenv_s(p1,p2)
#define msleep Sleep
#define localtime_r(p1,p2) localtime(p1)
#else
#include <unistd.h>
static void msleep(int ms)
{
	struct timespec tv = {0};
	tv.tv_sec = (ms) / 1000;
	tv.tv_nsec = ((ms) % 1000) * 1000 * 1000;
	nanosleep(&tv, &tv);
}
#endif

#define MAX_ARGS 128

#ifdef _WIN32

#define MS_PER_SEC      1000ULL     // MS = milliseconds
#define US_PER_MS       1000ULL     // US = microseconds
#define HNS_PER_US      10ULL       // HNS = hundred-nanoseconds (e.g., 1 hns = 100 ns)
#define NS_PER_US       1000ULL

#define HNS_PER_SEC     (MS_PER_SEC * US_PER_MS * HNS_PER_US)
#define NS_PER_HNS      (100ULL)    // NS = nanoseconds
#define NS_PER_SEC      (MS_PER_SEC * US_PER_MS * NS_PER_US)

static int clock_gettime_monotonic(struct timespec *tv)
{
	static LARGE_INTEGER ticksPerSec = {0};
	LARGE_INTEGER ticks;
	double seconds;

	if (!ticksPerSec.QuadPart) {
		QueryPerformanceFrequency(&ticksPerSec);
		if (!ticksPerSec.QuadPart) {
			errno = ENOTSUP;
			return -1;
		}
	}

	QueryPerformanceCounter(&ticks);
	seconds = (double) ticks.QuadPart / (double) ticksPerSec.QuadPart;
	tv->tv_sec = (time_t)seconds;
	tv->tv_nsec = (long)((ULONGLONG)(seconds * NS_PER_SEC) % NS_PER_SEC);
	return 0;
}

static int clock_gettime_realtime(struct timespec *tv)
{
	FILETIME ft;
	ULARGE_INTEGER hnsTime;
	GetSystemTimeAsFileTime(&ft);
	hnsTime.LowPart = ft.dwLowDateTime;
	hnsTime.HighPart = ft.dwHighDateTime;

	// To get POSIX Epoch as baseline, subtract the number of hns intervals from Jan 1, 1601 to Jan 1, 1970.
	hnsTime.QuadPart -= (11644473600ULL * HNS_PER_SEC);

	// modulus by hns intervals per second first, then convert to ns, as not to lose resolution
	tv->tv_nsec = (long) ((hnsTime.QuadPart % HNS_PER_SEC) * NS_PER_HNS);
	tv->tv_sec = (long) (hnsTime.QuadPart / HNS_PER_SEC);
	return 0;
}

#ifdef CLOCK_PROCESS_CPUTIME_ID
static int clock_gettime_process(struct timespec *tv)
{
	FILETIME creation, exit, kernel, user;
	ULARGE_INTEGER kernelTime, userTime;

	if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
		errno = EINVAL;
		return -1;
	}

	kernelTime.LowPart = kernel.dwLowDateTime;
	kernelTime.HighPart = kernel.dwHighDateTime;
	userTime.LowPart = user.dwLowDateTime;
	userTime.HighPart = user.dwHighDateTime;
	ULONGLONG hnsTime = kernelTime.QuadPart + userTime.QuadPart;
	tv->tv_sec = (time_t)(hnsTime / HNS_PER_SEC);
	tv->tv_nsec = (long)((hnsTime % HNS_PER_SEC) * NS_PER_HNS);
	return 0;
}
#endif

static int my_clock_gettime(clockid_t type, struct timespec *tp)
{
	if (type == CLOCK_MONOTONIC)
		return clock_gettime_monotonic(tp);
	else if (type == CLOCK_REALTIME)
		return clock_gettime_realtime(tp);
#ifdef CLOCK_PROCESS_CPUTIME_ID
	else if (type == CLOCK_PROCESS_CPUTIME_ID)
		return clock_gettime_process(tp);
#endif

	errno = ENOTSUP;
	return -1;
}
#else
#define my_clock_gettime clock_gettime
#endif

uint64_t cpu_time_in_usec(void)
{
	struct timespec now = {0};
#ifdef CLOCK_PROCESS_CPUTIME_ID
	my_clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
#else
	my_clock_gettime(CLOCK_MONOTONIC, &now);
#endif
	return (uint64_t)(now.tv_sec * 1000 * 1000) + (now.tv_nsec / 1000);
}

uint64_t wall_time_in_usec(void)
{
	struct timespec now = {0};
	my_clock_gettime(CLOCK_REALTIME, &now);
	return (uint64_t)(now.tv_sec * 1000 * 1000) + (now.tv_nsec / 1000);
}

uint64_t monotonic_time_in_usec(void)
{
	struct timespec now = {0};
	my_clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)(now.tv_sec * 1000 * 1000) + (now.tv_nsec / 1000);
}

#ifndef __wasi__
static bool bif_shell_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;
	GET_SOURCE_SINK(p1, p1_ctx, filename);
	int status = system(filename);
	TPL_free(filename);

	if (status == 0)
		return true;
	else
		return false;
}

static bool bif_shell_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	char *filename;
	GET_SOURCE_SINK(p1, p1_ctx, filename);
	int status = system(filename);
	TPL_free(filename);
	cell tmp;
	make_int(&tmp, status);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}
#else
static bool bif_shell_1(query *q)
{
	return false;
}

static bool bif_shell_2(query *q)
{
	return false;
}
#endif

static bool bif_getenv_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	char *filename;
	GET_SOURCE_SINK(p1, p1_ctx, filename);
	const char *value = getenv(filename);
	TPL_free(filename);

	if (!value)
		return false;

	cell tmp;

	if (is_string(p1))
		make_string(&tmp, value);
	else
		make_cstring(&tmp, value);

	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_setenv_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,source_sink);
	char *filename, *filename2;
	GET_SOURCE_SINK(p1, p1_ctx, filename);
	GET_SOURCE_SINK(p2, p2_ctx, filename2);
	setenv(filename, filename2, 1);
	TPL_free(filename2);
	TPL_free(filename);
	return true;
}

static bool bif_unsetenv_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;
	GET_SOURCE_SINK(p1, p1_ctx, filename);
	unsetenv(filename);
	TPL_free(filename);
	return true;
}

static bool bif_sleep_1(query *q)
{
	if (q->retry)
		return true;

	GET_FIRST_ARG(p1,number);

	if (is_negative(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	int ms = (is_float(p1) ? get_float(p1) : get_smallint(p1)) * 1000;

	if (q->is_task)
		return do_yield(q, ms);

	while ((ms > 0) && !q->halt && !q->pl->halt) {
		CHECK_INTERRUPT();
		msleep(10);

		if (errno == EINTR)
			return throw_timeout(q);

		ms -= 10;
	}

	return true;
}

static bool bif_now_0(query *q)
{
	pl_int secs = wall_time_in_usec() / 1000 / 1000;
	q->accum.tag = TAG_INT;
	set_smallint(&q->accum, secs);
	return true;
}

static bool bif_now_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	pl_int secs = wall_time_in_usec() / 1000 / 1000;
	cell tmp;
	make_int(&tmp, secs);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_get_time_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	pl_int us = wall_time_in_usec();
	double secs = us / 1000 / 1000;
	double v = us - (secs * 1000 * 1000);
	double frac = v / 1000 / 1000;
	cell tmp;
	make_float(&tmp, secs + frac);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_wall_time_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	pl_int us = wall_time_in_usec() - q->get_started;
	double secs = us / 1000 / 1000;
	double v = us - (secs * 1000 * 1000);
	double frac = v / 1000 / 1000;
	cell tmp;
	make_float(&tmp, secs + frac);
	return unify (q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_cpu_time_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	double v = ((double)cpu_time_in_usec() - q->st.cpu_time) / 1000 / 1000;
	cell tmp;
	make_float(&tmp, (pl_flt)v);
	return unify (q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_date_time_7(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,var);
	GET_NEXT_ARG(p5,var);
	GET_NEXT_ARG(p6,var);
	GET_NEXT_ARG(p7,var);
	struct timeval cur_time;
	gettimeofday(&cur_time, NULL);
	struct tm tm = {0};
	localtime_r((const time_t*)&cur_time.tv_sec, &tm);
	cell tmp;
	make_int(&tmp, tm.tm_year+1900);
	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_mon+1);
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_mday);
	unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_hour);
	unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_min);
	unify(q, p5, p5_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_sec);
	unify(q, p6, p6_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, cur_time.tv_usec/1000);
	unify(q, p7, p7_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_date_time_6(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,var);
	GET_NEXT_ARG(p5,var);
	GET_NEXT_ARG(p6,var);
	struct tm tm = {0};
	time_t now = time(NULL);
	localtime_r(&now, &tm);
	cell tmp;
	make_int(&tmp, tm.tm_year+1900);
	unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_mon+1);
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_mday);
	unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_hour);
	unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_min);
	unify(q, p5, p5_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, tm.tm_sec);
	unify(q, p6, p6_ctx, &tmp, q->st.cur_ctx);
	return true;
}


// Hosts with no usable POSIX per-thread timer - see USE_POLLED_ALARMS.
// Polling a monotonic deadline from the normal interrupt checks preserves
// nested timers and avoids a helper thread (WASI builds are deliberately
// threadless).

struct alarm_entry_ {
	alarm_entry *next;
	uint64_t deadline;
	bool fired;
};

// How long until the earliest alarm this thread is waiting on, so the
// scheduler can sleep exactly that long instead of waking on a timer of
// its own to go and look. False when nothing is pending.

bool next_alarm_delay(query *q, unsigned *ms)
{
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;

	if (!self->alarms)
		return false;

	uint64_t now = monotonic_time_in_usec(), best = 0;
	bool found = false;

	for (alarm_entry *e = self->alarms; e; e = e->next) {
		if (e->fired)
			continue;

		if (!found || (e->deadline < best)) {
			best = e->deadline;
			found = true;
		}
	}

	if (!found)
		return false;

	*ms = best <= now ? 0 : (unsigned)((best - now + 999) / 1000);
	return true;
}

bool has_expired_alarm(query *q)
{
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;
	uint64_t now = monotonic_time_in_usec();

	for (alarm_entry *e = self->alarms; e; e = e->next) {
		if (!e->fired && now >= e->deadline) {
			e->fired = true;
			return true;
		}
	}

	return false;
}

static bool bif_sys_alarm_2(query *q)
{
	GET_FIRST_ARG(p1,number);
	GET_NEXT_ARG(p2,integer_or_var);
	int time_ms = 0;
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "positive_integer");

	if (is_float(p1))
		time_ms = get_float(p1) * 1000;
	else
		time_ms = get_smallint(p1);

	if (time_ms < 0)
		return throw_error(q, p1, p1_ctx, "domain_error", "positive_integer");

	if (time_ms == 0) {
		if (!is_integer(p2))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "timer");

		alarm_entry *e = get_voidptr(p2);
		alarm_entry **slot = &self->alarms;

		while (*slot && (*slot != e))
			slot = &(*slot)->next;

		if (!*slot)
			return throw_error(q, p2, p2_ctx, "domain_error", "timer");

		*slot = e->next;
		TPL_free(e);
		return true;
	}

	alarm_entry *e = TPL_calloc(1, sizeof(alarm_entry));
	CHECKED(e);
	e->deadline = monotonic_time_in_usec() + ((uint64_t)time_ms * 1000);
	e->next = self->alarms;
	self->alarms = e;
	self->timedout = 0;

	cell tmp;
	make_ptr(&tmp, e);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_busy_1(query *q)
{
	GET_FIRST_ARG(p1,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	pl_int elapse = get_smallint(p1);

	if (elapse < 0)
		return true;

	// Limit to 60 seconds...

	if (elapse > (60 * 1000))
		return true;

	pl_uint started = wall_time_in_usec() / 1000;
	pl_uint end = started + elapse;

	while (((wall_time_in_usec() / 1000)  < end) && !q->halt && !q->pl->halt) {
		CHECK_INTERRUPT();
	}

	return true;
}

// The baseline is q->cpu_time, not q->st.cpu_time: the latter is part of
// the machine state, so it is restored on backtracking and a REDO would be
// timed from the original call rather than from the redo (issue #1050).
// It is also the query start time that statistics/2 and cpu_time/1 report
// against, which time/1 has no business resetting.

static bool bif_sys_timer_0(query *q)
{
	q->cpu_time = cpu_time_in_usec();
	q->total_inferences = 0;
	return true;
}

static bool bif_sys_elapsed_0(query *q)
{
	q->total_inferences--;
	uint64_t cpu_now = cpu_time_in_usec();
	uint64_t cpu_elapsed = cpu_now - q->cpu_time;
	double lips = (1.0 / ((double)cpu_elapsed/1000/1000)) * q->total_inferences;
	cell tmp;
	make_int(&tmp, q->total_inferences);
	char tmpbuf[80];
	format_integer(tmpbuf, &tmp, 3, '_', 0, 10);
	fprintf(stderr, "%% CPU elapsed %.3fs, %s inferences, %.3f MLips\n", (double)cpu_elapsed/1000/1000, tmpbuf, lips/1000/1000);
	if (q->is_redo) fprintf(stdout, "  ");
	q->total_inferences = 0;
	q->cpu_time = cpu_now;
	return true;
}

static bool bif_time_1(query *q)
{
	if (q->retry) {
		bif_sys_elapsed_0(q);
		return false;
	}

	bif_sys_timer_0(q);
	GET_FIRST_ARG(p1,callable);
	cell *tmp = prepare_call(q, CALL_NOSKIP, p1, p1_ctx, 4);
	pl_idx num_cells = p1->num_cells;
	make_instr(tmp+num_cells++, g_sys_elapsed_s, bif_sys_elapsed_0, 0, 0);
	make_instr(tmp+num_cells++, g_sys_drop_barrier_s, bif_sys_drop_barrier_1, 1, 1);
	make_uint(tmp+num_cells++, q->st.cp);
	make_call(q, tmp+num_cells);
	CHECKED(push_barrier(q));
	q->st.instr = tmp;
	return true;
}

static bool bif_get_unbuffered_code_1(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	int ch = history_getch_fd(fileno(str->fp));

	if (ch == 4)
		ch = -1;

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp);
		return do_yield(q, 1);
	}

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp);

		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	if ((ch == '\n') || (ch == EOF))
		str->did_getc = false;

	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

#if !defined(_WIN32) && !defined(__wasi__)
// The live window size, which is not the same thing as the terminfo
// 'li'/'co' numbers: those are the type's nominal size (24x80 for an
// xterm) and do not move when the window is resized.
//
// Tries each of the standard streams because any one of them may be a
// pipe while another is still the terminal - `tpl < script.pl` being the
// obvious case. Fails silently when none of them is a terminal, so a
// caller can fall back rather than having to catch.

static bool bif_sys_tty_size_2(query *q)
{
	GET_FIRST_ARG(p1,var);
	GET_NEXT_ARG(p2,var);
	static const int fds[] = {STDOUT_FILENO, STDERR_FILENO, STDIN_FILENO};
	struct winsize ws = {0};
	bool got = false;

	for (unsigned i = 0; !got && (i < sizeof(fds)/sizeof(fds[0])); i++) {
		if (!ioctl(fds[i], TIOCGWINSZ, &ws) && ws.ws_row && ws.ws_col)
			got = true;
	}

	if (!got)
		return false;

	cell tmp;
	make_int(&tmp, ws.ws_row);

	if (!unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_int(&tmp, ws.ws_col);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}
#endif

static bool bif_get_unbuffered_char_1(query *q)
{
	GET_FIRST_ARG(p1,in_character_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	int ch = history_getch_fd(fileno(str->fp));

	if (ch == 4)
		ch = -1;

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp);
		return do_yield(q, 1);
	}

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp);

		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	if ((ch == '\n') || (ch == EOF))
		str->did_getc = false;

	if (ch == -1) {
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	n = put_char_utf8(tmpbuf, ch);
	cell tmp;
	make_smalln(&tmp, tmpbuf, n);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

#if !defined(_WIN32) && !defined(__wasi__)
static bool bif_popen_4(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,list_or_nil);

	// Options are validated BEFORE new_stream() and before the filename
	// is allocated, so no error exit below has a slot or a buffer to
	// unwind. Previously both were taken first and every exit had to
	// release them by hand - see bif_threads.c for the same change.
	//
	// std_alias records alias(current_input/output/error), which needs
	// the slot number and so can only be applied after the commit.

	// Shape-check the command first, so that a bad command still wins
	// over a bad option exactly as it did before. This allocates
	// nothing - the string is only materialised after the loop.

	if (!is_atom(p1) && !is_iso_list(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "source_sink");

	if (is_iso_list(p1) && !scan_is_chars_list(q, p1, p1_ctx, true))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	cell *alias = NULL;
	int std_alias = 0;			// 0 none, 1 input, 2 output, 3 error
	bool binary = false;
	int eof_action = eof_action_eof_code;
	PROLOG_LIST_HANDLER(p4);

	while (is_list(p4)) {
		cell *h = PROLOG_LIST_HEAD(p4);
		cell *c = deref(q, h, p4_ctx);

		if (is_var(c))
			return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		if (is_compound(c) && (get_arity(c) == 1)) {
			cell *name = c + 1;
			name = deref(q, name, q->latest_ctx);

			if (get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0)
				return throw_error(q, c, q->latest_ctx, "permission_error", "open,source_sink");

			if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
				if (!CMP_STRING_TO_CSTR(q, name, "current_input"))
					std_alias = 1;
				else if (!CMP_STRING_TO_CSTR(q, name, "current_output"))
					std_alias = 2;
				else if (!CMP_STRING_TO_CSTR(q, name, "current_error"))
					std_alias = 3;
				else
					alias = name;
			} else if (!CMP_STRING_TO_CSTR(q, c, "type")) {
				if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "binary"))
					binary = true;
				else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "text"))
					binary = false;
			} else if (!CMP_STRING_TO_CSTR(q, c, "eof_action")) {
				if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "error"))
					eof_action = eof_action_error;
				else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "eof_code"))
					eof_action = eof_action_eof_code;
				else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "reset"))
					eof_action = eof_action_reset;
			}
		} else
			return throw_error(q, c, q->latest_ctx, "domain_error", "stream_option");

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;

		if (is_var(p4))
			return throw_error(q, p4, p4_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	// Now materialise the command name. Already shape-checked above,
	// so this cannot fail and nothing below has to release it early.

	char *src = is_atom(p1) ? DUP_STRING(q, p1)
	                        : chars_list_to_string(q, p1, p1_ctx);
	char *filename = src;

	// Commit. From here only popen() itself can fail, and that path
	// still has to release the slot.

	int n = new_stream(q->pl);

	if (n < 0) {
		TPL_free(src);
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");
	}

	stream *str = &q->pl->streams[n];
	str->is_pipe = true;
	str->is_popen = true;
	CHECKED(str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL));
	CHECKED(str->filename = TPL_strdup(filename));
	CHECKED(str->mode = DUP_STRING(q, p2));
	str->binary = binary;
	str->eof_action = eof_action;

	if (std_alias == 1)
		q->pl->current_input = n;
	else if (std_alias == 2)
		q->pl->current_output = n;
	else if (std_alias == 3)
		q->pl->current_error = n;
	else if (alias)
		sl_app(str->alias, DUP_STRING(q, alias), NULL);

	if (!strcmp(str->mode, "read"))
		str->fp = popen(filename, binary?"rb":"r");
	else if (!strcmp(str->mode, "write"))
		str->fp = popen(filename, binary?"wb":"w");
	else {
		TPL_free(src);
		unwind_stream(q, n);
		return throw_error(q, p2, p2_ctx, "domain_error", "io_mode");
	}

	TPL_free(src);

	if (!str->fp) {
		bool is_read = !strcmp(str->mode, "read");
		unwind_stream(q, n);

		if ((errno == EACCES) || (!is_read && (errno == EROFS)))
			return throw_error(q, p1, p1_ctx, "permission_error", "open,source_sink");
		else
			return throw_error(q, p1, p1_ctx, "existence_error", "source_sink");
	}

	str->fp_out = str->fp;
	cell tmp;
	make_int(&tmp, n);
	tmp.flags |= FLAG_INT_STREAM;
	unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_pclose_1(query *q)
{
	GET_FIRST_ARG(pstr,stream);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!str->is_pipe || !str->is_popen)
		return throw_error(q, pstr, pstr_ctx, "domain_error", "popen");

	pclose(str->fp);
	stream_close(q, n);
	return true;
}
#endif

char **g_envp = NULL;		// set by the front end, if there is one

#if !defined(_WIN32) && !defined(__wasi__) && !defined(__ANDROID__) && !defined(__riscos__)
static bool bif_process_create_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,list_or_nil);
	GET_NEXT_ARG(p3,list_or_nil);
	char *src = NULL;
	char *filename;

	if (is_atom(p1))
		filename = src = DUP_STRING(q, p1);
	else
		return throw_error(q, p1, p1_ctx, "domain_error", "source_sink");

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		src = chars_list_to_string(q, p1, p1_ctx);
		filename = src;
	}

	int args = 0, envs = 0;
	char *arguments[MAX_ARGS] = {NULL};
	char *environments[MAX_ARGS] = {NULL};
	arguments[args++] = TPL_strdup(filename);

	for (int i = 0; g_envp[i] != NULL; i++)
		environments[envs++] = TPL_strdup(g_envp[i]);

	PROLOG_LIST_HANDLER(p2);

	while (is_iso_list(p2)) {
		assert(args < MAX_ARGS);
		cell *h = PROLOG_LIST_HEAD(p2);
		cell *c = deref(q, h, p2_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!is_atom(c))
			return throw_error(q, c, c_ctx, "domain_error", "args");

		arguments[args++] = DUP_STRING(q, c);
		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	arguments[args] = NULL;
	posix_spawn_file_actions_t file_actions;
	posix_spawn_file_actions_init(&file_actions);
	posix_spawnattr_t attrp;
	posix_spawnattr_init(&attrp);
	cell *ppid = NULL;
	pl_ctx ppid_ctx = 0;
	int child_stdin_fd = -1, child_stdout_fd = -1, child_stderr_fd = -1;
	PROLOG_LIST_HANDLER(p3);

	while (is_iso_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		cell *c = deref(q, h, p3_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_compound(c) && (get_arity(c) == 1)) {
			cell *name = c + 1;
			name = deref(q, name, c_ctx);
			pl_ctx name_ctx = q->latest_ctx;

			if (!CMP_STRING_TO_CSTR(q, c, "process") || !CMP_STRING_TO_CSTR(q, c, "pid")) {
				ppid = name;
				ppid_ctx = name_ctx;
			} else if (!CMP_STRING_TO_CSTR(q, c, "detached")) {
#if (defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 26))) || !defined(POSIX_SPAWN_SETSID)
				return throw_error(q, c, c_ctx, "system_error", "posix_spawnattr_setflags");
#else
				posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETSID);
#endif
			} else if (!CMP_STRING_TO_CSTR(q, c, "cwd")) {
				// 2.29, not the 2.26 the 'detached' case above wants:
				// POSIX_SPAWN_SETSID arrived in glibc 2.26 but
				// posix_spawn_file_actions_addchdir_np only in 2.29. Sharing
				// the 2.26 test let 2.26-2.28 fall through to a call that is
				// not declared and does not link - Raspberry Pi OS Buster
				// (glibc 2.28) is one such.
				//
				// The unsupported cases exclude the CALL, not merely
				// return before reaching it. An #if around the return
				// leaves the call compiled, and while the compiler drops it
				// as unreachable - at -O0 as well as -O3, so the link is
				// fine either way - it parses it first, and warns on every
				// build.
				//
				// Apple has both spellings, split at macOS 26: the
				// unsuffixed one is __API_AVAILABLE(macos(26.0)) and _np is
				// deprecated from the same release. Selecting on __x86_64__
				// looked right only because Apple Silicon and macOS 26
				// arrived together; the axis is the OS, not the
				// architecture.
				//
				// The test is the DEPLOYMENT TARGET, not the SDK
				// (__MAC_OS_X_VERSION_MAX_ALLOWED): a newer SDK declares the
				// unsuffixed call for an older target too, but it weak-links
				// and is null at runtime on the OS actually being targeted.
#if (defined(__GLIBC__) && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 29))) \
	|| defined(__OpenBSD__)
				return throw_error(q, c, c_ctx, "system_error", "posix_spawn_file_actions_addchdir");
#else
				const char *cwd = C_STR(q, name);
#if !defined(_WIN32) && !defined(__wasi__) && !defined(__ANDROID__) && !defined(__NetBSD__) \
	&& !(defined(__APPLE__) && defined(__MAC_OS_X_VERSION_MIN_REQUIRED) \
		&& __MAC_OS_X_VERSION_MIN_REQUIRED >= 260000)
				posix_spawn_file_actions_addchdir_np(&file_actions, cwd);
#else
				posix_spawn_file_actions_addchdir(&file_actions, cwd);
#endif
#endif
			} else if (!CMP_STRING_TO_CSTR(q, c, "env") && is_list_or_nil(name)) {
				PROLOG_LIST_HANDLER(name);
				memset(environments, 0, sizeof(environments));
				envs = 0;

				while (is_iso_list(name)) {
					cell *h = PROLOG_LIST_HEAD(name);
					cell *c = deref(q, h, name_ctx);

					if (is_compound(c) && (get_arity(c) == 2) && (c->val_off == g_eq_s)) {
						cell *p1 = c + 1, *p2 = c + 2;
						SB(pr);

						if (is_atom(p1) && is_atom(p2)) {
							SB_sprintf(pr, "%s=%s", C_STR(q, p1), C_STR(q, p2));
						} else if (is_atom(p1) && is_smallint(p2)) {
							SB_sprintf(pr, "%s=%d", C_STR(q, p1), (int)get_smallint(p2));
						}

						environments[envs++] = SB_cstr(pr);
					}

					name = PROLOG_LIST_TAIL(name);
					name = deref(q, name, name_ctx);
					name_ctx = q->latest_ctx;
				}

			} else if (!CMP_STRING_TO_CSTR(q, c, "environment") && is_list_or_nil(name)) {
				PROLOG_LIST_HANDLER(name);

				while (is_iso_list(name)) {
					cell *h = PROLOG_LIST_HEAD(name);
					cell *c = deref(q, h, name_ctx);

					if (is_compound(c) && (get_arity(c) == 2) && (c->val_off == g_eq_s)) {
						cell *p1 = c + 1, *p2 = c + 2;
						SB(pr);

						if (is_atom(p1) && is_atom(p2)) {
							SB_sprintf(pr, "%s=%s", C_STR(q, p1), C_STR(q, p2));
						} else if (is_atom(p1) && is_smallint(p2)) {
							SB_sprintf(pr, "%s=%d", C_STR(q, p1), (int)get_smallint(p2));
						}

						environments[envs++] = SB_cstr(pr);
					}

					name = PROLOG_LIST_TAIL(name);
					name = deref(q, name, name_ctx);
					name_ctx = q->latest_ctx;
				}
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdin") && !CMP_STRING_TO_CSTR(q, name, "std")) {
				posix_spawn_file_actions_adddup2(&file_actions, q->pl->current_input, 0);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdin") && !CMP_STRING_TO_CSTR(q, name, "null")) {
				posix_spawn_file_actions_addopen(&file_actions, 0, "/dev/null", O_RDONLY, 0);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdin") && !CMP_STRING_TO_CSTR(q, name, "pipe")
				&& is_compound(name) && (get_arity(name) == 1) && is_var(name+1)) {
				cell *ns = deref(q, name+1, name_ctx);
				pl_ctx ns_ctx = q->latest_ctx;
				int n = new_stream(q->pl);
				int fds[2];
				if (pipe(fds)) return false;
				posix_spawn_file_actions_adddup2(&file_actions, fds[0], 0);
				child_stdin_fd = fds[0];
				q->pl->streams[n].fp = fdopen(fds[1], "w");
				q->pl->streams[n].fp_out = q->pl->streams[n].fp;
				q->pl->streams[n].is_pipe = true;
				CHECKED(q->pl->streams[n].mode = TPL_strdup("write"));
				cell tmp;
				make_int(&tmp, n);
				tmp.flags |= FLAG_INT_STREAM;
				unify(q, ns, ns_ctx, &tmp, q->st.cur_ctx);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdin") && !CMP_STRING_TO_CSTR(q, name, "stream")) {
				cell *ns = deref(q, name+1, name_ctx);
				int n = get_stream(q, ns);
				posix_spawn_file_actions_adddup2(&file_actions, fileno(q->pl->streams[n].fp_in), 0);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdout") && !CMP_STRING_TO_CSTR(q, name, "std")) {
				posix_spawn_file_actions_adddup2(&file_actions, q->pl->current_output, 1);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdout") && !CMP_STRING_TO_CSTR(q, name, "null")) {
				posix_spawn_file_actions_addopen(&file_actions, 1, "/dev/null", O_WRONLY, 0);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdout") && !CMP_STRING_TO_CSTR(q, name, "pipe")
				&& is_compound(name) && (get_arity(name) == 1) && is_var(name+1)) {
				cell *ns = deref(q, name+1, name_ctx);
				pl_ctx ns_ctx = q->latest_ctx;
				int n = new_stream(q->pl);
				int fds[2];
				if (pipe(fds)) return false;
				posix_spawn_file_actions_adddup2(&file_actions, fds[1], 1);
				child_stdout_fd = fds[1];
				q->pl->streams[n].fp = fdopen(fds[0], "r");
				q->pl->streams[n].fp_out = q->pl->streams[n].fp;
				q->pl->streams[n].is_pipe = true;
				CHECKED(q->pl->streams[n].mode = TPL_strdup("read"));
				cell tmp;
				make_int(&tmp, n);
				tmp.flags |= FLAG_INT_STREAM;
				unify(q, ns, ns_ctx, &tmp, q->st.cur_ctx);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stdout") && !CMP_STRING_TO_CSTR(q, name, "stream")) {
				cell *ns = deref(q, name+1, name_ctx);
				int n = get_stream(q, ns);
				posix_spawn_file_actions_adddup2(&file_actions, fileno(q->pl->streams[n].fp_out), 1);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stderr") && !CMP_STRING_TO_CSTR(q, name, "std")) {
				posix_spawn_file_actions_adddup2(&file_actions, q->pl->current_error, 2);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stderr") && !CMP_STRING_TO_CSTR(q, name, "null")) {
				posix_spawn_file_actions_addopen(&file_actions, 2, "/dev/null", O_WRONLY, 0);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stderr") && !CMP_STRING_TO_CSTR(q, name, "pipe")
				&& is_compound(name) && (get_arity(name) == 1) && is_var(name+1)) {
				cell *ns = deref(q, name+1, name_ctx);
				pl_ctx ns_ctx = q->latest_ctx;
				int n = new_stream(q->pl);
				int fds[2];
				if (pipe(fds)) return false;
				posix_spawn_file_actions_adddup2(&file_actions, fds[1], 2);
				child_stderr_fd = fds[1];
				q->pl->streams[n].fp = fdopen(fds[0], "r");
				q->pl->streams[n].fp_out = q->pl->streams[n].fp;
				q->pl->streams[n].is_pipe = true;
				CHECKED(q->pl->streams[n].mode = TPL_strdup("read"));
				cell tmp;
				make_int(&tmp, n);
				tmp.flags |= FLAG_INT_STREAM;
				unify(q, ns, ns_ctx, &tmp, q->st.cur_ctx);
			} else if (!CMP_STRING_TO_CSTR(q, c, "stderr") && !CMP_STRING_TO_CSTR(q, name, "stream")) {
				cell *ns = deref(q, name+1, name_ctx);
				int n = get_stream(q, ns);
				posix_spawn_file_actions_adddup2(&file_actions, fileno(q->pl->streams[n].fp_out), 2);
			} else
				return throw_error(q, c, q->latest_ctx, "domain_error", "process_create_option");
		} else
			return throw_error(q, c, q->latest_ctx, "domain_error", "process_create_option");

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;
	}

	pid_t pid;
	int ok = posix_spawnp(&pid, C_STR(q, p1), &file_actions, &attrp, (char * const*)arguments, (char * const*)environments);
	posix_spawn_file_actions_destroy(&file_actions);
	posix_spawnattr_destroy(&attrp);
	TPL_free(src);

	if (child_stdin_fd  != -1) close(child_stdin_fd);
	if (child_stdout_fd != -1) close(child_stdout_fd);
	if (child_stderr_fd != -1) close(child_stderr_fd);

	for (int i = 0; i < args; i++)
		TPL_free(arguments[i]);

	for (int i = 0; i < envs; i++)
		TPL_free(environments[i]);

	if (ok != 0)
		return throw_error(q, p1, p1_ctx, "system_error", "posix_spawnp");

	if (ppid) {
		cell tmp;
		make_uint(&tmp, pid);
		return unify(q, ppid, ppid_ctx, &tmp, q->st.cur_ctx);
	} else {
		waitpid(pid, NULL, 0);
	}

	return true;
}

static bool bif_process_wait_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,list_or_nil);
	PROLOG_LIST_HANDLER(p3);
	int secs = -1;

	while (is_iso_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		cell *c = deref(q, h, p3_ctx);

		if (is_compound(c) && (get_arity(c) == 1) && !CMP_STRING_TO_CSTR(q, c, "timeout")) {
			if (is_integer(FIRST_ARG(c)))
				secs = get_smallint(FIRST_ARG(c));
			else if (is_atom(FIRST_ARG(c)) && !CMP_STRING_TO_CSTR(q, FIRST_ARG(c), "infinite"))
				secs = -1;
		} else
			return throw_error(q, c, q->latest_ctx, "domain_error", "process_wait_option");

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;
	}

	int status = 0, pid = get_smalluint(p1);
	pid_t ok = waitpid(pid, &status, secs != -1 ? WNOHANG : 0);

	if (ok != pid)
		return false;

	cell *tmp = alloc_heap(q, 2);

	if ( WIFSIGNALED(status)) {
		int sig = WTERMSIG(status);
		make_struct(tmp+0, g_killed_s, 1, 1);
		make_uint(tmp+1, sig);
	} else {
		int code = WEXITSTATUS(status);
		make_struct(tmp+0, g_exit_s, 1, 1);
		make_uint(tmp+1, code);
	}

	return unify(q, p2, p2_ctx, tmp, q->st.cur_ctx);
}

static bool bif_process_kill_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	int pid = get_smalluint(p1), sig = get_smallint(p2);
	kill(pid, sig);
	return true;
}

static bool bif_process_kill_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	int pid = get_smalluint(p1);
	kill(pid, SIGKILL);
	return true;
}
#endif

builtins g_os_bifs[] =
{
	{"shell", 1, bif_shell_1, "+atom", false, false, BLAH},
	{"shell", 2, bif_shell_2, "+atom,-integer", false, false, BLAH},
	{"getenv", 2, bif_getenv_2, "+atom,-atom", false, false, BLAH},
	{"setenv", 2, bif_setenv_2, "+atom,+atom", false, false, BLAH},
	{"unsetenv", 1, bif_unsetenv_1, "+atom", false, false, BLAH},
	{"sleep", 1, bif_sleep_1, "+number", false, false, BLAH},
	{"now", 0, bif_now_0, NULL, false, false, BLAH},
	{"now", 1, bif_now_1, "-integer", false, false, BLAH},
	{"time", 1, bif_time_1, ":callable", false, false, BLAH},
	{"get_time", 1, bif_get_time_1, "-float", false, false, BLAH},
	{"cpu_time", 1, bif_cpu_time_1, "-integer", false, false, BLAH},
	{"wall_time", 1, bif_wall_time_1, "-integer", false, false, BLAH},
	{"date_time", 6, bif_date_time_6, "-integer,-integer,-integer,-integer,-integer,-integer", false, false, BLAH},
	{"date_time", 7, bif_date_time_7, "-integer,-integer,-integer,-integer,-integer,-integer,-integer", false, false, BLAH},
	{"busy", 1, bif_busy_1, "+integer", false, false, BLAH},
	{"get_unbuffered_code", 1, bif_get_unbuffered_code_1, "?integer", false, false, BLAH},
	{"get_unbuffered_char", 1, bif_get_unbuffered_char_1, "?character", false, false, BLAH},

#if !defined(_WIN32) && !defined(__wasi__)
	{"$tty_size", 2, bif_sys_tty_size_2, "-integer,-integer", false, false, BLAH},
#endif

#if !defined(_WIN32) && !defined(__wasi__) && !defined(__ANDROID__) && !defined(__riscos__)
	{"process_create", 3, bif_process_create_3, "+atom,+list,+list", false, false, BLAH},
	{"$process_wait", 3, bif_process_wait_3, "+integer,-term,+list", false, false, BLAH},
	{"process_kill", 2, bif_process_kill_2, "+integer,+integer", false, false, BLAH},
	{"process_kill", 1, bif_process_kill_1, "+integer", false, false, BLAH},
#endif

#if !defined(_WIN32) && !defined(__wasi__)
	{"popen", 4, bif_popen_4, "+source_sink,+atom,--stream,+list", false, false, BLAH},
	{"pclose", 1, bif_pclose_1, "+stream", false, false, BLAH},
#endif

	{"$alarm", 2, bif_sys_alarm_2, "+integer,-integer", false, false, BLAH},
	{"$timer", 0, bif_sys_timer_0, NULL, false, false, BLAH},
	{"$elapsed", 0, bif_sys_elapsed_0, NULL, false, false, BLAH},

	{0}
};
