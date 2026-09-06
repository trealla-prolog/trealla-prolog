#pragma once

#include <stdbool.h>
#include <stdint.h>

// The VideoCore property mailbox. The GPU owns the clocks, the display and
// the board's identity, and answers questions about them over a shared
// message buffer: the ARM writes a bus address into a hardware mailbox, the
// GPU fills the buffer in place and posts the address back.

#define RPI4_MBOX_TAG_BOARD_MAC 0x00010003u
#define RPI4_MBOX_TAG_ARM_MEMORY 0x00010005u

// Sends one property message on channel 8. `tags` is the concatenated tag
// list without the message header or the terminating end tag, each tag being
// its id, its value buffer size in bytes, a zero request code, and then that
// buffer rounded up to whole words. On success the same array holds the
// replies, where a tag that was answered has bit 31 set in its third word.

bool rpi4_mbox_property(uint32_t *tags, unsigned words);

bool rpi4_mbox_board_mac(uint8_t mac[6]);
bool rpi4_mbox_arm_memory(uint32_t *base, uint32_t *size);
