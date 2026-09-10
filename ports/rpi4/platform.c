#include <stddef.h>
#include <stdint.h>

#include "platform/platform.h"

#include "bcm2711.h"
#include "fb.h"

// The five platform services on a Raspberry Pi 4: PL011 UART0 for the
// console, the ARM generic timer for the clock, and a parked core for halt.

#define UART_DR REG(UART0_BASE + 0x00u)
#define UART_FR REG(UART0_BASE + 0x18u)
#define UART_IBRD REG(UART0_BASE + 0x24u)
#define UART_FBRD REG(UART0_BASE + 0x28u)
#define UART_LCRH REG(UART0_BASE + 0x2cu)
#define UART_CR REG(UART0_BASE + 0x30u)
#define UART_IMSC REG(UART0_BASE + 0x38u)
#define UART_ICR REG(UART0_BASE + 0x44u)

#define UART_FR_BUSY (1u << 3)
#define UART_FR_RXFE (1u << 4)
#define UART_FR_TXFF (1u << 5)

static int uart_ready;

// 115200 8N1 from the firmware's default 48 MHz UART clock: the divisor is
// 48000000 / (16 * 115200), so 26 and 3/64. QEMU ignores the baud registers.

static void uart_init(void)
{
	UART_CR = 0;

	// GPIO14/15 to ALT0 (TXD0/RXD0), pulls disabled. Both pins live in the
	// same select and pull register, so each is one read-modify-write.
	uint32_t function = GPFSEL(RPI4_CONSOLE_TX);
	function &= ~((7u << GPFSEL_SHIFT(RPI4_CONSOLE_TX))
		| (7u << GPFSEL_SHIFT(RPI4_CONSOLE_RX)));
	function |= (4u << GPFSEL_SHIFT(RPI4_CONSOLE_TX))
		| (4u << GPFSEL_SHIFT(RPI4_CONSOLE_RX));
	GPFSEL(RPI4_CONSOLE_TX) = function;

	uint32_t pull = GPPUPPDN(RPI4_CONSOLE_TX);
	pull &= ~((3u << GPPUPPDN_SHIFT(RPI4_CONSOLE_TX))
		| (3u << GPPUPPDN_SHIFT(RPI4_CONSOLE_RX)));
	GPPUPPDN(RPI4_CONSOLE_TX) = pull;

	UART_ICR = 0x7ffu;
	UART_IBRD = 26;
	UART_FBRD = 3;
	UART_LCRH = (3u << 5) | (1u << 4);	// 8 bits, FIFOs enabled
	UART_IMSC = 0;
	UART_CR = (1u << 0) | (1u << 8) | (1u << 9);

	uart_ready = 1;
}

static void uart_open(void)
{
	if (!uart_ready)
		uart_init();
}

static void uart_put(uint8_t ch)
{
	while (UART_FR & UART_FR_TXFF)
		;

	UART_DR = ch;
}

static void uart_drain(void)
{
	while (UART_FR & UART_FR_BUSY)
		;
}

// Blocks for the first byte, then takes whatever else is already waiting.
// Insisting on len would wedge anything interactive: a reader asks for a
// bufferful and a person sends a line.
//
// This is also where the two things a terminal driver would otherwise do
// happen, because on a raw serial line nothing else will. Bytes are echoed,
// or the typist sees nothing at all; and Return arrives as CR where the
// reader wants LF, so a term typed with a full stop and Return would sit
// unterminated forever. No line editing though - backspace echoes and is
// then handed to the parser like any other character.

// board.c's, for whatever the board's devices need while nothing else runs.
void rpi4_board_idle(void);

static uint8_t console_getch(void)
{
	while (UART_FR & UART_FR_RXFE)
		rpi4_board_idle();

	uint8_t ch = (uint8_t)UART_DR;

	if (ch == '\r') {
		ch = '\n';
		uart_put('\r');
	}

	uart_put(ch);
	return ch;
}

size_t tpl_platform_console_read(void *buf, size_t len)
{
	uint8_t *dst = buf;
	uart_open();

	if (!len)
		return 0;

	size_t got = 0;
	dst[got++] = console_getch();

	while ((got < len) && !(UART_FR & UART_FR_RXFE))
		dst[got++] = console_getch();

	return got;
}

// Nothing sleeps the core yet, so a wait is the board's chance to service its
// devices. The caller re-checks the clock and calls again.

void tpl_platform_idle_until(uint64_t deadline_usec)
{
	(void)deadline_usec;
	rpi4_board_idle();
}

// Output and error deliberately share the one UART. A newline goes out as
// CR LF: a terminal on the other end leaves the column where it was, so
// bare LF walks the output diagonally off the screen. The framebuffer
// console is handed the original bytes and does its own thing with them.
//
// Everything is echoed to the HDMI console too, once one exists. The serial
// line stays the primary output - it is the one that works before the MMU is
// on and the one a panic can rely on - but a board with a monitor and no
// serial adapter is the common case, and this is what makes it usable.

size_t tpl_platform_console_write(enum tpl_console_channel channel,
	const void *buf, size_t len)
{
	(void)channel;
	const uint8_t *src = buf;
	uart_open();

	for (size_t i = 0; i < len; i++) {
		if (src[i] == '\n')
			uart_put('\r');

		uart_put(src[i]);
	}

	rpi4_fb_write(buf, len);
	return len;
}

// CNTPCT_EL0 is 64 bits wide and counts at CNTFRQ_EL0 (54 MHz on a Pi 4), so
// there is no rollover to extend. The division is split because scaling the
// raw count by a million would overflow after about five hours.

uint64_t tpl_platform_monotonic_usec(void)
{
	uint64_t ticks, frequency;

	__asm__ volatile("isb" ::: "memory");
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(ticks));
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));

	if (!frequency)
		return 0;

	return (ticks / frequency) * 1000000u
		+ ((ticks % frequency) * 1000000u) / frequency;
}

void tpl_platform_halt(int status)
{
	uart_drain();

#if RPI4_SEMIHOSTING
	// Under QEMU, report the status the way the RV32 target's test
	// finisher does. On hardware with no debugger this instruction is not
	// serviced, so it is off unless the smoke build asks for it.
	volatile uint64_t block[2] = {0x20026u, (uint64_t)(unsigned)status};
	register uint64_t operation __asm__("x0") = 0x20;
	register uint64_t argument __asm__("x1") = (uint64_t)(uintptr_t)block;
	__asm__ volatile("hlt #0xf000"
		:: "r"(operation), "r"(argument) : "memory");
#else
	(void)status;
#endif

	for (;;)
		__asm__ volatile("wfe");
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
	tpl_platform_halt(1);
}
