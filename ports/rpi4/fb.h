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

// Drawing, for the builtins in bif_fb.c. A colour is 0xRRGGBB and anything
// off-screen is clipped rather than refused. False from rpi4_fb_size() means
// there is no framebuffer, which is how the builtins know to complain.

bool rpi4_fb_size(unsigned *width, unsigned *height);
void rpi4_fb_clear(uint32_t rgb);
void rpi4_fb_pixel(unsigned x, unsigned y, uint32_t rgb);
void rpi4_fb_rect(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t rgb);

// Draws only the ink, leaving the background as it was, so text can go over
// whatever is already on the screen.
void rpi4_fb_text(unsigned x, unsigned y, const char *s, size_t len, uint32_t rgb);
