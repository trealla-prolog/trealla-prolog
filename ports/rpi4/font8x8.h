#pragma once

#include <stdint.h>

// An 8x8 bitmap font covering printable ASCII, generated from the glyph art
// in util/mkfont.py. Each row is one byte, most significant bit leftmost.

#define RPI4_FONT_FIRST 32
#define RPI4_FONT_LAST 126
#define RPI4_FONT_GLYPHS (RPI4_FONT_LAST - RPI4_FONT_FIRST + 1)

extern const uint8_t rpi4_font8x8[RPI4_FONT_GLYPHS][8];
