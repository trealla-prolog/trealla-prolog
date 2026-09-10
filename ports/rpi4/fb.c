#include <stdint.h>

#include "platform/platform.h"

#include "bcm2711.h"
#include "fb.h"
#include "font8x8.h"
#include "mailbox.h"

// Resolution is a compile-time choice, and a low one on purpose: the GPU
// scales whatever we ask for up to the panel, so 800x600 is not a small
// picture on a television, it is a large font. RPI4_FB_SCALE draws each glyph
// pixel as a square block if that is still not large enough.

#ifndef RPI4_FB_WIDTH
#define RPI4_FB_WIDTH 800u
#endif
#ifndef RPI4_FB_HEIGHT
#define RPI4_FB_HEIGHT 600u
#endif
#ifndef RPI4_FB_SCALE
#define RPI4_FB_SCALE 1u
#endif

#define CELL_W (8u * RPI4_FB_SCALE)
#define CELL_H (8u * RPI4_FB_SCALE)

// Greyscale, which is not only restful: it is the one palette a pixel-order
// mistake cannot show up in, so the colours below say nothing about whether
// the RGB/BGR request was honoured. The helper is written for the order we
// ask for, so drawing in colour later will be right.
#define RGB(r, g, b) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16))
#define FB_BACKGROUND RGB(0x00, 0x00, 0x00)
#define FB_FOREGROUND RGB(0xc8, 0xc8, 0xc8)

// Framebuffer property tags, and where each one's answer lands in the message.
#define TAG_SET_PHYSICAL 0x00048003u
#define TAG_SET_VIRTUAL 0x00048004u
#define TAG_SET_DEPTH 0x00048005u
#define TAG_SET_PIXEL_ORDER 0x00048006u
#define TAG_SET_OFFSET 0x00048009u
#define TAG_ALLOCATE 0x00040001u
#define TAG_GET_PITCH 0x00040008u

#define PIXEL_ORDER_RGB 1u

// A VideoCore bus address; the low 30 bits are the ARM physical address.
#define BUS_ADDRESS_MASK 0x3fffffffu

// Cortex-A72. Used only to size the cache maintenance stride, where guessing
// small would cost time and guessing large would lose writes.
#define CACHE_LINE 64u

static struct {
	volatile uint32_t *pixels;
	unsigned pitch_words;
	unsigned width, height;			// the screen, in pixels
	unsigned cols, rows;			// ... and in characters
	unsigned col, row;
} g_fb;

extern char __heap_end;

// The framebuffer is mapped Normal write-back like the rest of RAM, so the
// GPU - which is not coherent with the ARM's caches - would otherwise scan
// out whatever was there before. Cleaning the lines we actually drew is much
// cheaper than making the whole framebuffer non-cacheable would be, because
// scrolling copies megabytes and would then crawl.

static void clean(const volatile void *from, size_t len)
{
	uintptr_t start = (uintptr_t)from & ~(uintptr_t)(CACHE_LINE - 1);
	uintptr_t end = (uintptr_t)from + len;

	for (uintptr_t p = start; p < end; p += CACHE_LINE)
		__asm__ volatile("dc cvac, %0" :: "r"(p) : "memory");

	__asm__ volatile("dsb sy" ::: "memory");
}

static volatile uint32_t *line_at(unsigned y)
{
	return g_fb.pixels + (size_t)y * g_fb.pitch_words;
}

static void fill(unsigned y, unsigned height, uint32_t colour)
{
	for (unsigned row = y; row < y + height; row++) {
		volatile uint32_t *pixels = line_at(row);

		for (unsigned x = 0; x < g_fb.cols * CELL_W; x++)
			pixels[x] = colour;

		clean(pixels, (size_t)g_fb.cols * CELL_W * sizeof(uint32_t));
	}
}

static void draw(unsigned char character)
{
	if ((character < RPI4_FONT_FIRST) || (character > RPI4_FONT_LAST))
		character = '?';

	const uint8_t *glyph = rpi4_font8x8[character - RPI4_FONT_FIRST];
	unsigned x0 = g_fb.col * CELL_W;
	unsigned y0 = g_fb.row * CELL_H;

	for (unsigned r = 0; r < 8; r++) {
		uint8_t bits = glyph[r];

		for (unsigned s = 0; s < RPI4_FB_SCALE; s++) {
			volatile uint32_t *pixels
				= line_at(y0 + r * RPI4_FB_SCALE + s) + x0;

			for (unsigned c = 0; c < 8 * RPI4_FB_SCALE; c++)
				pixels[c] = (bits & (0x80u >> (c / RPI4_FB_SCALE)))
					? FB_FOREGROUND : FB_BACKGROUND;

			clean(pixels, CELL_W * sizeof(uint32_t));
		}
	}
}

// Scrolling copies through the cache, which is the whole reason the
// framebuffer is left cacheable, and cleans the result afterwards.

static void scroll(void)
{
	size_t width = (size_t)g_fb.cols * CELL_W;

	for (unsigned y = 0; y < (g_fb.rows - 1) * CELL_H; y++) {
		volatile uint32_t *to = line_at(y);
		const volatile uint32_t *from = line_at(y + CELL_H);

		for (size_t x = 0; x < width; x++)
			to[x] = from[x];

		clean(to, width * sizeof(uint32_t));
	}

	fill((g_fb.rows - 1) * CELL_H, CELL_H, FB_BACKGROUND);
	g_fb.row = g_fb.rows - 1;
}

static void newline(void)
{
	g_fb.col = 0;

	if (++g_fb.row >= g_fb.rows)
		scroll();
}

const char *rpi4_fb_open(void)
{
	// Offsets of each tag in the message, so the replies can be read back
	// by name rather than by counting.
	enum {
		SET_PHYSICAL = 0, SET_VIRTUAL = 5, SET_DEPTH = 10,
		SET_PIXEL_ORDER = 14, SET_OFFSET = 18, ALLOCATE = 23,
		GET_PITCH = 28, TAG_WORDS = 32
	};

	uint32_t tags[] = {
		TAG_SET_PHYSICAL, 8, 0, RPI4_FB_WIDTH, RPI4_FB_HEIGHT,
		TAG_SET_VIRTUAL, 8, 0, RPI4_FB_WIDTH, RPI4_FB_HEIGHT,
		TAG_SET_DEPTH, 4, 0, 32,
		TAG_SET_PIXEL_ORDER, 4, 0, PIXEL_ORDER_RGB,
		TAG_SET_OFFSET, 8, 0, 0, 0,
		TAG_ALLOCATE, 8, 0, 16, 0,		// 16-byte alignment
		TAG_GET_PITCH, 4, 0, 0,
	};

	_Static_assert(sizeof(tags) / sizeof(*tags) == TAG_WORDS,
		"framebuffer tag offsets do not match the message");

	if (g_fb.pixels)
		return NULL;

	if (!rpi4_mbox_property(tags, TAG_WORDS))
		return "mailbox refused the request";

	uint32_t base = tags[ALLOCATE + 3] & BUS_ADDRESS_MASK;
	uint32_t bytes = tags[ALLOCATE + 4];
	uint32_t pitch = tags[GET_PITCH + 3];
	uint32_t width = tags[SET_PHYSICAL + 3];
	uint32_t height = tags[SET_PHYSICAL + 4];

	// The firmware allocates nothing when it has no display to allocate
	// for, which is what an unplugged HDMI socket looks like from here.
	if (!base || !bytes)
		return "firmware allocated no buffer - is a monitor connected?";

	if (!pitch || (pitch % sizeof(uint32_t)))
		return "firmware returned an unusable pitch";

	// The firmware is free to answer with a different size than we asked
	// for, so every number it returns has to be believed rather than
	// assumed - and a buffer overlapping memory we are already using would
	// be worse than having no console at all.
	if ((width < CELL_W) || (height < CELL_H))
		return "firmware returned a screen too small for one character";

	if ((pitch < width * sizeof(uint32_t)) || ((uint64_t)pitch * height > bytes))
		return "firmware returned a buffer smaller than the screen";

	if (base < (uint32_t)(uintptr_t)&__heap_end)
		return "firmware put the buffer in memory we are already using";

	g_fb.pixels = (volatile uint32_t*)(uintptr_t)base;
	g_fb.pitch_words = pitch / sizeof(uint32_t);
	g_fb.width = width;
	g_fb.height = height;
	g_fb.cols = width / CELL_W;
	g_fb.rows = height / CELL_H;
	g_fb.col = g_fb.row = 0;
	fill(0, g_fb.rows * CELL_H, FB_BACKGROUND);
	return NULL;
}

void rpi4_fb_write(const void *buf, size_t len)
{
	const unsigned char *src = buf;

	if (!g_fb.pixels)
		return;

	for (size_t i = 0; i < len; i++) {
		unsigned char character = src[i];

		switch (character) {
		case '\n':
			newline();
			continue;

		case '\r':
			g_fb.col = 0;
			continue;

		case '\b':
			if (g_fb.col)
				g_fb.col--;

			continue;

		case '\t':
			do {
				if (g_fb.col + 1 >= g_fb.cols) {
					newline();
					break;
				}

				g_fb.col++;
			} while (g_fb.col % 8);

			continue;
		}

		if (g_fb.col >= g_fb.cols)
			newline();

		draw(character);
		g_fb.col++;
	}
}

// --- drawing ------------------------------------------------------------

// A colour arrives as 0xRRGGBB, which is what anyone writing Prolog will
// expect; the framebuffer wants it in the order the firmware was asked for.

static uint32_t pixel_of(uint32_t rgb)
{
	return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

bool rpi4_fb_size(unsigned *width, unsigned *height)
{
	if (!g_fb.pixels)
		return false;

	if (width)
		*width = g_fb.width;

	if (height)
		*height = g_fb.height;

	return true;
}

void rpi4_fb_pixel(unsigned x, unsigned y, uint32_t rgb)
{
	if (!g_fb.pixels || (x >= g_fb.width) || (y >= g_fb.height))
		return;

	volatile uint32_t *at = line_at(y) + x;
	*at = pixel_of(rgb);
	clean(at, sizeof(uint32_t));
}

void rpi4_fb_rect(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t rgb)
{
	if (!g_fb.pixels || (x >= g_fb.width) || (y >= g_fb.height))
		return;

	if (w > (g_fb.width - x))
		w = g_fb.width - x;

	if (h > (g_fb.height - y))
		h = g_fb.height - y;

	uint32_t colour = pixel_of(rgb);

	for (unsigned row = y; row < y + h; row++) {
		volatile uint32_t *pixels = line_at(row) + x;

		for (unsigned i = 0; i < w; i++)
			pixels[i] = colour;

		clean(pixels, (size_t)w * sizeof(uint32_t));
	}
}

// Clearing takes the console cursor home with it: the two share a screen, and
// leaving the cursor pointing at wiped pixels would be a surprise.

void rpi4_fb_clear(uint32_t rgb)
{
	if (!g_fb.pixels)
		return;

	rpi4_fb_rect(0, 0, g_fb.width, g_fb.height, rgb);
	g_fb.col = g_fb.row = 0;
}

void rpi4_fb_text(unsigned x, unsigned y, const char *s, size_t len, uint32_t rgb)
{
	if (!g_fb.pixels)
		return;

	uint32_t colour = pixel_of(rgb);

	for (size_t i = 0; i < len; i++) {
		unsigned char character = (unsigned char)s[i];

		if ((character < RPI4_FONT_FIRST) || (character > RPI4_FONT_LAST))
			character = '?';

		const uint8_t *glyph = rpi4_font8x8[character - RPI4_FONT_FIRST];
		unsigned x0 = x + i * CELL_W;

		if (x0 >= g_fb.width)
			break;

		for (unsigned r = 0; r < 8; r++) {
			uint8_t bits = glyph[r];

			for (unsigned s2 = 0; s2 < RPI4_FB_SCALE; s2++) {
				unsigned py = y + r * RPI4_FB_SCALE + s2;

				if (py >= g_fb.height)
					break;

				volatile uint32_t *pixels = line_at(py);

				for (unsigned c = 0; c < 8 * RPI4_FB_SCALE; c++) {
					unsigned px = x0 + c;

					if (px >= g_fb.width)
						break;

					if (bits & (0x80u >> (c / RPI4_FB_SCALE)))
						pixels[px] = colour;
				}

				clean(pixels + x0, CELL_W * sizeof(uint32_t));
			}
		}
	}
}
