#include <stdio.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "platform/platform.h"

size_t tpl_platform_console_read(void *buf, size_t len)
{
	return fread(buf, 1, len, stdin);
}

size_t tpl_platform_console_write(enum tpl_console_channel channel,
	const void *buf, size_t len)
{
	FILE *stream = channel == TPL_CONSOLE_ERROR ? stderr : stdout;
	size_t written = fwrite(buf, 1, len, stream);
	fflush(stream);
	return written;
}

uint64_t tpl_platform_monotonic_usec(void)
{
	return (uint64_t)esp_timer_get_time();
}

// Blocks for whole ticks only; the caller spins out any sub-tick remainder.

void tpl_platform_idle_until(uint64_t deadline_usec)
{
	uint64_t now = tpl_platform_monotonic_usec();

	if (deadline_usec > now)
		vTaskDelay((TickType_t)((deadline_usec - now)
			/ (portTICK_PERIOD_MS * 1000u)));
}

void tpl_platform_halt(int status)
{
	(void)status;
	vTaskSuspend(NULL);

	for (;;)
		;
}

void tpl_platform_panic(const char *message)
{
	static const char prefix[] = "TREALLA PLATFORM PANIC: ";
	const char *end = message;

	while (*end)
		end++;

	tpl_platform_console_write(TPL_CONSOLE_ERROR, prefix, sizeof(prefix) - 1);
	tpl_platform_console_write(TPL_CONSOLE_ERROR, message,
		(size_t)(end - message));
	tpl_platform_console_write(TPL_CONSOLE_ERROR, "\n", 1);
	tpl_platform_halt(1);
}
