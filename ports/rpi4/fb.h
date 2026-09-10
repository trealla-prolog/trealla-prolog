#pragma once

#include <stdbool.h>
#include <stddef.h>

// An HDMI text console over the VideoCore framebuffer. The GPU owns the
// display, so there is no driver here in the usual sense: the mailbox asks
// for a resolution and gets back a pointer, and everything after that is
// drawing into memory.

// Opens the console. NULL on success, else a short reason - a board with no
// monitor is the ordinary case and says so rather than failing silently.
const char *rpi4_fb_open(void);

// Writes text at the cursor, honouring newline, carriage return, tab and
// backspace, wrapping at the right margin and scrolling at the bottom. Does
// nothing at all if the framebuffer never opened, so the console can be wired
// in unconditionally.
void rpi4_fb_write(const void *buf, size_t len);
