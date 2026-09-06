#include <string.h>

#include "platform/platform.h"

#include "bcm2711.h"
#include "mailbox.h"

// Mailbox 0 carries VideoCore's replies to the ARM and mailbox 1 carries the
// ARM's requests to VideoCore. They share a register block, so mailbox 1's
// status - the one that says whether there is room to write - is the far
// register at 0x38, not the 0x18 most examples reach for.

#define MBOX_BASE (PERI_BASE + 0xb880u)
#define MBOX_READ REG(MBOX_BASE + 0x00u)
#define MBOX_READ_STATUS REG(MBOX_BASE + 0x18u)
#define MBOX_WRITE REG(MBOX_BASE + 0x20u)
#define MBOX_WRITE_STATUS REG(MBOX_BASE + 0x38u)

#define MBOX_EMPTY 0x40000000u
#define MBOX_FULL 0x80000000u

// The low four bits of a mailbox word are the channel; the rest is the
// message address, which is why the buffer has to be 16-byte aligned.
#define MBOX_CHANNEL_PROPERTY 8u

#define MBOX_REQUEST 0x00000000u
#define MBOX_RESPONSE_OK 0x80000000u
#define MBOX_TAG_ANSWERED 0x80000000u
#define MBOX_TAG_END 0x00000000u

// Enough for the largest message this port sends. Framebuffer setup, the
// next customer, comes to about thirty words.
#define MBOX_MAX_WORDS 48u

// A hung GPU must not hang the board silently, so every wait is bounded. A
// property call that takes a tenth of a second has already gone wrong.
#define MBOX_TIMEOUT_USEC 100000u

// VideoCore sees RAM through four aliases of the low gigabyte; 0xc0000000 is
// the one that bypasses its L2 cache. Our side of the buffer is already
// Normal non-cacheable - it lives in the DMA window mmu.c maps that way - so
// between the two nothing needs flushing, only ordering.
#define MBOX_BUS_ALIAS 0xc0000000u

static volatile uint32_t *g_message;

static bool mbox_open(void)
{
	if (!g_message)
		g_message = rpi4_dma_alloc(MBOX_MAX_WORDS * sizeof(uint32_t));

	return g_message != NULL;
}

static bool expired(uint64_t deadline)
{
	return tpl_platform_monotonic_usec() > deadline;
}

bool rpi4_mbox_property(uint32_t *tags, unsigned words)
{
	// Two header words and the end tag go around the caller's list.
	if ((words < 3) || ((words + 3) > MBOX_MAX_WORDS) || !mbox_open())
		return false;

	g_message[0] = (words + 3) * sizeof(uint32_t);
	g_message[1] = MBOX_REQUEST;

	for (unsigned i = 0; i < words; i++)
		g_message[2 + i] = tags[i];

	g_message[2 + words] = MBOX_TAG_END;

	uint32_t address = (uint32_t)(uintptr_t)g_message | MBOX_BUS_ALIAS;
	uint64_t deadline = tpl_platform_monotonic_usec() + MBOX_TIMEOUT_USEC;

	while (MBOX_WRITE_STATUS & MBOX_FULL)
		if (expired(deadline))
			return false;

	__asm__ volatile("dsb sy" ::: "memory");
	MBOX_WRITE = address | MBOX_CHANNEL_PROPERTY;

	// Replies on other channels belong to firmware we did not ask, so they
	// are read and dropped rather than mistaken for ours.
	for (;;) {
		while (MBOX_READ_STATUS & MBOX_EMPTY)
			if (expired(deadline))
				return false;

		if ((MBOX_READ & 0xfu) == MBOX_CHANNEL_PROPERTY)
			break;
	}

	__asm__ volatile("dsb sy" ::: "memory");

	if (g_message[1] != MBOX_RESPONSE_OK)
		return false;

	for (unsigned i = 0; i < words; i++)
		tags[i] = g_message[2 + i];

	return true;
}

// A tag the firmware does not implement comes back with its request code
// untouched, so the answered bit has to be checked as well as the message.

static bool answered(const uint32_t *tag)
{
	return (tag[2] & MBOX_TAG_ANSWERED) != 0;
}

bool rpi4_mbox_board_mac(uint8_t mac[6])
{
	uint32_t tags[5] = {RPI4_MBOX_TAG_BOARD_MAC, 6, 0, 0, 0};

	if (!rpi4_mbox_property(tags, 5) || !answered(tags))
		return false;

	memcpy(mac, &tags[3], 6);
	return true;
}

bool rpi4_mbox_arm_memory(uint32_t *base, uint32_t *size)
{
	uint32_t tags[5] = {RPI4_MBOX_TAG_ARM_MEMORY, 8, 0, 0, 0};

	if (!rpi4_mbox_property(tags, 5) || !answered(tags) || !tags[4])
		return false;

	if (base)
		*base = tags[3];

	if (size)
		*size = tags[4];

	return true;
}
