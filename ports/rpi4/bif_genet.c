#include "prolog.h"
#include "query.h"

#include "genet.h"

// A window onto the Ethernet controller and its PHY, for debugging a link that
// is up and silent. The alternative is a rebuild and a card swap per
// question, which is a poor way to find out whether a bit is set.
//
// The writes are as sharp as they look: a wrong value can stop the link until
// the next boot. What is still refused is anything that would fault, since a
// fault here would take the board down mid-session.

static bool in_range(cell *p, pl_int max)
{
	return !is_bigint(p) && (get_smallint(p) >= 0) && (get_smallint(p) <= max);
}

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
	// that is not word aligned would fault.
	if ((offset < 0) || !rpi4_genet_peek((unsigned)offset, &value))
		return throw_error(q, p1, p1_ctx, "domain_error", "genet_offset");

	cell tmp;
	make_int(&tmp, value);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_genet_reg_set_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);

	if (!in_range(p2, 0xffffffffLL))
		return throw_error(q, p2, p2_ctx, "domain_error", "genet_value");

	if (!in_range(p1, 0xffff)
		|| !rpi4_genet_poke((unsigned)get_smallint(p1),
			(uint32_t)get_smallint(p2)))
		return throw_error(q, p1, p1_ctx, "domain_error", "genet_offset");

	return true;
}

static bool bif_genet_mdio_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);

	if (!in_range(p1, 31))
		return throw_error(q, p1, p1_ctx, "domain_error", "mdio_register");

	cell tmp;
	make_int(&tmp, rpi4_genet_mdio_read((unsigned)get_smallint(p1)));
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_genet_mdio_set_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);

	if (!in_range(p1, 31))
		return throw_error(q, p1, p1_ctx, "domain_error", "mdio_register");

	if (!in_range(p2, 0xffff))
		return throw_error(q, p2, p2_ctx, "domain_error", "mdio_value");

	rpi4_genet_mdio_write((unsigned)get_smallint(p1),
		(uint16_t)get_smallint(p2));
	return true;
}

builtins g_genet_bifs[] =
{
	{"genet_reg", 2, bif_genet_reg_2, "+integer,?integer", false, false, BLAH},
	{"genet_reg_set", 2, bif_genet_reg_set_2, "+integer,+integer", false, false, BLAH},
	{"genet_mdio", 2, bif_genet_mdio_2, "+integer,?integer", false, false, BLAH},
	{"genet_mdio_set", 2, bif_genet_mdio_set_2, "+integer,+integer", false, false, BLAH},
	{0}
};
