#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "platform.h"

// read() rather than fread(): fread waits for the full len, which wedges an
// interactive reader asking for a bufferful.

size_t tpl_platform_console_read(void *buf, size_t len)
{
	ssize_t got = read(STDIN_FILENO, buf, len);
	return got > 0 ? (size_t)got : 0;
}

size_t tpl_platform_console_write(enum tpl_console_channel channel,
	const void *buf, size_t len)
{
	FILE *fp = channel == TPL_CONSOLE_ERROR ? stderr : stdout;
	size_t written = fwrite(buf, 1, len, fp);
	fflush(fp);
	return written;
}

uint64_t tpl_platform_monotonic_usec(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		tpl_platform_panic("monotonic clock failed");

	return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}

// The optional service from platform.h. Hosted, idling is a real sleep - the
// weak default in src/bif_os_none.c would spin instead, which on a machine
// with an OS is pure waste. Returning early is allowed, so a signal cutting
// the nap short needs no handling: the caller re-checks the clock.

void tpl_platform_idle_until(uint64_t deadline_usec)
{
	uint64_t now = tpl_platform_monotonic_usec();

	if (now >= deadline_usec)
		return;

	uint64_t usecs = deadline_usec - now;
	struct timespec nap = {
		.tv_sec = (time_t)(usecs / 1000000u),
		.tv_nsec = (long)((usecs % 1000000u) * 1000u)
	};

	nanosleep(&nap, NULL);
}

void tpl_platform_halt(int status)
{
	exit(status);
}

void tpl_platform_panic(const char *message)
{
	static const char prefix[] = "TREALLA PLATFORM PANIC: ";
	const char *end = message;

	while (*end)
		end++;

	tpl_platform_console_write(TPL_CONSOLE_ERROR, prefix, sizeof(prefix) - 1);
	tpl_platform_console_write(TPL_CONSOLE_ERROR, message, (size_t)(end - message));
	tpl_platform_console_write(TPL_CONSOLE_ERROR, "\n", 1);
	abort();
}
