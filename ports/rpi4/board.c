#include <string.h>

#include "platform/platform.h"

#include "bcm2711.h"
#include "fb.h"
#include "mailbox.h"

#if RPI4_NET
#include "genet.h"
#include "net.h"
#endif

// Board bring-up that happens after the MMU is on and before main(): the
// things a device needs that Prolog should not have to ask for.
//
// Networking failing here is not fatal. A board with no cable, no link or -
// as under QEMU, which does not emulate GENET at all - no controller simply
// runs without it, and the udp_* builtins raise
// existence_error(network_interface) if a program asks.

#ifndef RPI4_IP
#define RPI4_IP {192, 168, 50, 2}
#endif
#ifndef RPI4_NETMASK
#define RPI4_NETMASK {255, 255, 255, 0}
#endif
#ifndef RPI4_GATEWAY
#define RPI4_GATEWAY {192, 168, 50, 1}
#endif

// Used only if the mailbox cannot tell us the board's own address. It is a
// locally administered one, so it cannot collide with a real assignment.
#ifndef RPI4_MAC
#define RPI4_MAC {0x02, 0x00, 0x5e, 0x00, 0x53, 0x01}
#endif

static void say(const char *s)
{
	tpl_platform_console_write(TPL_CONSOLE_OUTPUT, s, strlen(s));
}

static void say_uint(uint32_t value)
{
	char digits[10];
	unsigned used = 0;

	do {
		digits[used++] = (char)('0' + (value % 10));
		value /= 10;
	} while (value);

	while (used)
		tpl_platform_console_write(TPL_CONSOLE_OUTPUT, &digits[--used], 1);
}

// The first thing the board says, and on a machine with no debugger the only
// evidence that the MMU came up and the GPU is answering. The memory size is
// worth printing for itself: it says which Pi 4 this is.

static void mailbox_report(void)
{
	uint32_t bytes = 0;

	if (!rpi4_mbox_arm_memory(NULL, &bytes)) {
		say("TREALLA MAILBOX FAILED\n");
		return;
	}

	say("TREALLA MAILBOX OK ram=");
	say_uint(bytes >> 20);
	say("MiB\n");
}

#if RPI4_NET

extern bool net_stack_attach(netif *nif, const uint8_t ip[4],
	const uint8_t mask[4], const uint8_t gateway[4]);

static netif g_nif;

static void network_up(void)
{
	static const uint8_t ip[4] = RPI4_IP;
	static const uint8_t mask[4] = RPI4_NETMASK;
	static const uint8_t gateway[4] = RPI4_GATEWAY;
	static const uint8_t fallback[6] = RPI4_MAC;
	uint8_t mac[6];

	// The board's real address lives in OTP and only the GPU can read it.
	if (!rpi4_mbox_board_mac(mac))
		memcpy(mac, fallback, sizeof(mac));

	if (!rpi4_genet_open(&g_nif, mac))
		return;

	net_stack_attach(&g_nif, ip, mask, gateway);
}

#else

// Networking is opt-in: `make rpi4 RPI4_NET=1`. It is not in the default
// image because QEMU's raspi4b has no GENET, and the driver's first register
// read aborts there - so the image every CI run boots must not contain it.

static void network_up(void)
{
}

#endif

// The console comes up between the two: the mailbox has to answer before a
// framebuffer can be asked for, and everything after this point is visible on
// a monitor as well as on the serial line.

void rpi4_board_init(void)
{
	mailbox_report();

	const char *why = rpi4_fb_open();

	if (!why)
		say("TREALLA FRAMEBUFFER OK\n");
	else {
		say("TREALLA FRAMEBUFFER FAILED: ");
		say(why);
		say("\n");
	}

	network_up();
}
