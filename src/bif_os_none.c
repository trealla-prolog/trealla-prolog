#include "prolog.h"
#include "query.h"
#include "platform/platform.h"

// A freestanding image has no process CPU clock or civil-time clock by
// default. Their callers still need an increasing reference, so all three
// use the mandatory platform monotonic service until those capabilities are
// selected independently.

uint64_t cpu_time_in_usec(void) { return tpl_platform_monotonic_usec(); }
uint64_t wall_time_in_usec(void) { return tpl_platform_monotonic_usec(); }
uint64_t monotonic_time_in_usec(void) { return tpl_platform_monotonic_usec(); }

bool next_alarm_delay(query *q, unsigned *ms)
{
	(void)q; (void)ms;
	return false;
}

bool has_expired_alarm(query *q)
{
	(void)q;
	return false;
}

char **g_envp;

// The default for the optional platform service declared in platform.h. A
// port that cannot idle has nothing to do but spin, and spinning is what the
// caller's loop already does, so the default is empty rather than clever.
// Weak so that a port defines its own simply by having one - no port has to
// change, and nothing here needs to know which ones did.
//
// It lives with the OS services rather than in src/platform/ because "what to
// do when there is no OS to sleep in" is exactly that question.

__attribute__((weak))
void tpl_platform_idle_until(uint64_t deadline_usec)
{
	(void)deadline_usec;
}

// Waits out a deadline, letting the port idle if it can, and gives up early
// if the engine is halting. Shared by the two predicates below, which differ
// only in the unit they accept.

static void wait_until(query *q, uint64_t deadline)
{
	while (monotonic_time_in_usec() < deadline) {
		if (q->halt || q->pl->halt)
			break;

		tpl_platform_idle_until(deadline);
	}
}

// A task hands its delay to the scheduler instead, which puts it on the timer
// heap - otherwise unreachable in a freestanding image, since every other
// caller that parks a task on a timer is in bif_os.c or bif_threads.c and
// neither is linked here. Without it the scheduler can round-robin tasks but
// never wake one later.

static bool wait_usecs(query *q, uint64_t usecs)
{
	if (q->is_task)
		return do_yield(q, (int)(usecs / 1000));

	wait_until(q, monotonic_time_in_usec() + usecs);
	return true;
}

#define MAX_WAIT_USECS (60ull * 60 * 1000000)		// an hour is already absurd

// sleep/1, deliberately the same shape as the hosted bif_sleep_1() - retry
// guard and argument errors included - so a program does not have to know
// which build it is running on.

static bool bif_sleep_1(query *q)
{
	if (q->retry)
		return true;

	GET_FIRST_ARG(p1,number);

	if (is_negative(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	double seconds = is_float(p1) ? get_float(p1) : (double)get_smallint(p1);
	uint64_t usecs = seconds >= (double)MAX_WAIT_USECS / 1000000
		? MAX_WAIT_USECS : (uint64_t)(seconds * 1000000);

	return wait_usecs(q, usecs);
}

// delay_ms/1 is the same wait in the unit a pin is naturally timed in, taking
// an integer rather than a float. It began in the Raspberry Pi 4's GPIO table
// because that port had no sleep/1 to reach for; pacing is not board
// knowledge, so it belongs here where every freestanding target gets it. A
// hosted GPIO build has its own, in src/bif_gpio_linux.c.

static bool bif_delay_ms_1(query *q)
{
	if (q->retry)
		return true;

	GET_FIRST_ARG(p1,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	pl_int requested = get_smallint(p1);

	if (requested < 0)
		return throw_error(q, p1, p1_ctx, "domain_error", "not_less_than_zero");

	uint64_t usecs = (uint64_t)requested * 1000u;
	return wait_usecs(q, usecs > MAX_WAIT_USECS ? MAX_WAIT_USECS : usecs);
}

builtins g_os_bifs[] =
{
	{"sleep", 1, bif_sleep_1, "+number", false, false, BLAH},
	{"delay_ms", 1, bif_delay_ms_1, "+integer", false, false, BLAH},
	{0}
};
