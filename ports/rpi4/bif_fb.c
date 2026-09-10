#include <string.h>

#include "prolog.h"
#include "query.h"

#include "fb.h"

// The Raspberry Pi 4's framebuffer table: drawing, for a program that has a
// monitor rather than a serial line. The console in fb.c shares the screen,
// so text written with write/1 and pixels drawn here land on the same
// surface - fb_clear/1 takes the cursor home for that reason.
//
// A colour is 0xRRGGBB. Coordinates are clipped, not refused: drawing partly
// off the edge is ordinary, where a negative coordinate is a mistake.

// throw_error() returns TRUE when a catch/3 handler took the ball, so these
// report their own success and hand the engine's answer back through *status.

static bool get_dimension(query *q, cell *p, pl_ctx p_ctx, const char *domain,
	unsigned *value, bool *status)
{
	if (is_bigint(p)) {
		*status = throw_error(q, p, p_ctx, "domain_error",
			"small_integer_range");
		return false;
	}

	pl_int got = get_smallint(p);

	if (got < 0) {
		*status = throw_error(q, p, p_ctx, "domain_error", domain);
		return false;
	}

	*value = (unsigned)got;
	return true;
}

static bool get_colour(query *q, cell *p, pl_ctx p_ctx, uint32_t *value,
	bool *status)
{
	if (is_bigint(p)) {
		*status = throw_error(q, p, p_ctx, "domain_error",
			"small_integer_range");
		return false;
	}

	pl_int got = get_smallint(p);

	if ((got < 0) || (got > 0xffffff)) {
		*status = throw_error(q, p, p_ctx, "domain_error", "fb_colour");
		return false;
	}

	*value = (uint32_t)got;
	return true;
}

// Every predicate here needs a screen. Saying so is better than quietly
// drawing into nothing on a board with no monitor plugged in.

static bool have_screen(query *q, bool *status)
{
	if (rpi4_fb_size(NULL, NULL))
		return true;

	*status = throw_error(q, q->st.instr, q->st.cur_ctx, "existence_error",
		"framebuffer");
	return false;
}

static bool bif_fb_size_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	unsigned width, height;
	bool status;

	if (!have_screen(q, &status))
		return status;

	rpi4_fb_size(&width, &height);
	cell tmp;
	make_int(&tmp, width);

	if (!unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_int(&tmp, height);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_fb_clear_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	uint32_t colour;
	bool status;

	if (!have_screen(q, &status))
		return status;

	if (!get_colour(q, p1, p1_ctx, &colour, &status))
		return status;

	rpi4_fb_clear(colour);
	return true;
}

static bool bif_fb_pixel_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,integer);
	unsigned x, y;
	uint32_t colour;
	bool status;

	if (!have_screen(q, &status))
		return status;

	if (!get_dimension(q, p1, p1_ctx, "fb_coord", &x, &status)
		|| !get_dimension(q, p2, p2_ctx, "fb_coord", &y, &status)
		|| !get_colour(q, p3, p3_ctx, &colour, &status))
		return status;

	rpi4_fb_pixel(x, y, colour);
	return true;
}

static bool bif_fb_rect_5(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,integer);
	GET_NEXT_ARG(p4,integer);
	GET_NEXT_ARG(p5,integer);
	unsigned x, y, w, h;
	uint32_t colour;
	bool status;

	if (!have_screen(q, &status))
		return status;

	if (!get_dimension(q, p1, p1_ctx, "fb_coord", &x, &status)
		|| !get_dimension(q, p2, p2_ctx, "fb_coord", &y, &status)
		|| !get_dimension(q, p3, p3_ctx, "fb_extent", &w, &status)
		|| !get_dimension(q, p4, p4_ctx, "fb_extent", &h, &status)
		|| !get_colour(q, p5, p5_ctx, &colour, &status))
		return status;

	rpi4_fb_rect(x, y, w, h, colour);
	return true;
}

static bool bif_fb_text_4(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	GET_NEXT_ARG(p3,atom);
	GET_NEXT_ARG(p4,integer);
	unsigned x, y;
	uint32_t colour;
	bool status;

	if (!have_screen(q, &status))
		return status;

	if (!get_dimension(q, p1, p1_ctx, "fb_coord", &x, &status)
		|| !get_dimension(q, p2, p2_ctx, "fb_coord", &y, &status)
		|| !get_colour(q, p4, p4_ctx, &colour, &status))
		return status;

	const char *text = C_STR(q, p3);
	rpi4_fb_text(x, y, text, C_STRLEN(q, p3), colour);
	return true;
}

builtins g_fb_bifs[] =
{
	{"fb_size", 2, bif_fb_size_2, "?integer,?integer", false, false, BLAH},
	{"fb_clear", 1, bif_fb_clear_1, "+integer", false, false, BLAH},
	{"fb_pixel", 3, bif_fb_pixel_3, "+integer,+integer,+integer", false, false, BLAH},
	{"fb_rect", 5, bif_fb_rect_5, "+integer,+integer,+integer,+integer,+integer", false, false, BLAH},
	{"fb_text", 4, bif_fb_text_4, "+integer,+integer,+atom,+integer", false, false, BLAH},
	{0}
};
