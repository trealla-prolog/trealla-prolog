#pragma once

#include <stddef.h>
#include <stdint.h>

// Internal link-time service contract for an OS-free Trealla image. A port
// supplies one adapter or adapter pair. This remains internal rather than a
// stable public ABI; hosted, reusable board-shim and RV32 implementations keep
// validating the shape while ports are still experimental.

enum tpl_console_channel {
	TPL_CONSOLE_OUTPUT,
	TPL_CONSOLE_ERROR
};

size_t tpl_platform_console_read(void *buf, size_t len);
size_t tpl_platform_console_write(enum tpl_console_channel channel,
	const void *buf, size_t len);
uint64_t tpl_platform_monotonic_usec(void);

#if defined(__GNUC__) || defined(__clang__)
#define TPL_NORETURN __attribute__((noreturn))
#else
#define TPL_NORETURN _Noreturn
#endif

TPL_NORETURN void tpl_platform_halt(int status);
TPL_NORETURN void tpl_platform_panic(const char *message);

// The one optional service. Waiting for a deadline is otherwise a spin on the
// monotonic clock above, which is all a port with no timer interrupt can do -
// so that is the default, defined weakly in src/bif_os_none.c. A port able to
// sleep the core instead (WFI against a timer, a vendor idle call) defines
// this and stops burning power while a program waits.
//
// Waking early is allowed and expected: any interrupt can cut a WFI short.
// Callers must therefore treat the return as advisory and re-check the clock,
// which also lets the default do nothing at all.

void tpl_platform_idle_until(uint64_t deadline_usec);
