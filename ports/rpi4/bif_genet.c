#include "prolog.h"
#include "query.h"

#include "genet.h"

// A read-only window onto the Ethernet controller, for debugging a link that
// is up and silent. The alternative is a rebuild and a card swap per
// question, which is a poor way to find out whether a bit is set.
//
// Reads only. Nothing here can change the controller's state, so a wrong
// offset costs a domain_error rather than a wedged board.

static bool bif_genet_reg_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error",
			"small_integer_range");

	pl_int offset = get_smallint(p1);
	uint32_t value;

	// Refused rather than attempted: an offset outside the window or one
	// that is not word aligned would fault, and a fault here would take
	// the board down mid-session.
	if ((offset < 0) || !rpi4_genet_peek((unsigned)offset, &value))
		return throw_error(q, p1, p1_ctx, "domain_error", "genet_offset");

	cell tmp;
	make_int(&tmp, value);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

builtins g_genet_bifs[] =
{
	{"genet_reg", 2, bif_genet_reg_2, "+integer,?integer", false, false, BLAH},
	{0}
};
