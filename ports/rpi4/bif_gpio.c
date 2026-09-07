#include <string.h>

#include "prolog.h"
#include "query.h"
#include "platform/platform.h"

#include "bcm2711.h"

// One of the Raspberry Pi 4's builtin tables - the GPIO block.
// ports/rpi4/port_bifs.c is what hands it, and any later ones, to the engine.
// Board knowledge stays here - src/ has no idea a GPIO pin exists, and a
// hosted build links the empty table in src/port_bifs_none.c.
//
// delay_ms/1 used to live here too, for want of anywhere better; it is in
// src/bif_os_none.c now, alongside sleep/1, where every freestanding target
// gets it.

typedef struct {
	const char *name;
	uint32_t value;
} name_map;

// BCM function select encoding. ALT0-ALT5 are deliberately not in numeric
// order in the hardware.
static const name_map s_modes[] = {
	{"input", 0}, {"output", 1},
	{"alt0", 4}, {"alt1", 5}, {"alt2", 6},
	{"alt3", 7}, {"alt4", 3}, {"alt5", 2},
	{NULL, 0}
};

// BCM2711 pull encoding. Note this is NOT the BCM2835 GPPUD encoding that
// most Pi 1-3 example code uses, where 01 is pull-down and 10 is pull-up.
// Here they are the other way round, and getting it wrong fails silently
// until an input is left floating.
static const name_map s_pulls[] = {
	{"none", 0}, {"up", 1}, {"down", 2},
	{NULL, 0}
};

// throw_error() returns TRUE when a catch/3 handler accepted the ball, so its
// result is the value the builtin owes the engine, NOT "did the check pass".
// These helpers therefore report success in their own return value and pass
// the engine's answer back through *status, which the caller returns as-is.
// Reading it the other way round quietly continues past a caught error with
// an unset pin number.

static bool map_atom(query *q, cell *p, pl_ctx p_ctx, const name_map *map,
	const char *domain, uint32_t *value, bool *status)
{
	const char *name = C_STR(q, p);

	for (const name_map *ptr = map; ptr->name; ptr++) {
		if (!strcmp(name, ptr->name)) {
			*value = ptr->value;
			return true;
		}
	}

	*status = throw_error(q, p, p_ctx, "domain_error", domain);
	return false;
}

static bool get_pin(query *q, cell *p, pl_ctx p_ctx, bool console_ok,
	unsigned *pin, bool *status)
{
	if (is_bigint(p)) {
		*status = throw_error(q, p, p_ctx, "domain_error",
			"small_integer_range");
		return false;
	}

	pl_int value = get_smallint(p);

	if ((value < 0) || (value >= (pl_int)RPI4_NUM_GPIO)) {
		*status = throw_error(q, p, p_ctx, "domain_error", "gpio_pin");
		return false;
	}

	// Repurposing the console pins would silence the board's only output,
	// so reading them is allowed but reconfiguring them is not.
	if (!console_ok
		&& (((unsigned)value == RPI4_CONSOLE_TX)
			|| ((unsigned)value == RPI4_CONSOLE_RX))) {
		*status = throw_error(q, p, p_ctx, "permission_error",
			"modify,gpio_pin");
		return false;
	}

	*pin = (unsigned)value;
	return true;
}

static bool get_level(query *q, cell *p, pl_ctx p_ctx, unsigned *level,
	bool *status)
{
	if (is_bigint(p)) {
		*status = throw_error(q, p, p_ctx, "domain_error",
			"small_integer_range");
		return false;
	}

	pl_int value = get_smallint(p);

	if ((value != 0) && (value != 1)) {
		*status = throw_error(q, p, p_ctx, "domain_error", "gpio_level");
		return false;
	}

	*level = (unsigned)value;
	return true;
}

// Function select and pull are read-modify-write. That is safe without a lock
// here: a freestanding build has no threads (NOTHREADS=1) and this port takes
// no interrupts, so nothing else can touch the register in between.

static bool bif_gpio_mode_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	unsigned pin;
	uint32_t mode;
	bool status;

	if (!get_pin(q, p1, p1_ctx, false, &pin, &status))
		return status;

	if (!map_atom(q, p2, p2_ctx, s_modes, "gpio_mode", &mode, &status))
		return status;

	uint32_t shift = GPFSEL_SHIFT(pin);
	uint32_t value = GPFSEL(pin);
	value &= ~(7u << shift);
	value |= mode << shift;
	GPFSEL(pin) = value;
	return true;
}

static bool bif_gpio_pull_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	unsigned pin;
	uint32_t pull;
	bool status;

	if (!get_pin(q, p1, p1_ctx, false, &pin, &status))
		return status;

	if (!map_atom(q, p2, p2_ctx, s_pulls, "gpio_pull", &pull, &status))
		return status;

	uint32_t shift = GPPUPPDN_SHIFT(pin);
	uint32_t value = GPPUPPDN(pin);
	value &= ~(3u << shift);
	value |= pull << shift;
	GPPUPPDN(pin) = value;
	return true;
}

static bool bif_gpio_read_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	unsigned pin;
	bool status;

	if (!get_pin(q, p1, p1_ctx, true, &pin, &status))
		return status;

	cell tmp;
	make_int(&tmp, (GPLEV(pin) & GPIO_BIT(pin)) ? 1 : 0);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

// Set and clear are separate write-1-to-act registers, which is why driving
// one output needs no read-modify-write and cannot disturb its neighbours.

static bool bif_gpio_write_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,integer);
	unsigned pin, level;
	bool status;

	if (!get_pin(q, p1, p1_ctx, false, &pin, &status))
		return status;

	if (!get_level(q, p2, p2_ctx, &level, &status))
		return status;

	if (level)
		GPSET(pin) = GPIO_BIT(pin);
	else
		GPCLR(pin) = GPIO_BIT(pin);

	return true;
}

builtins g_gpio_bifs[] =
{
	{"gpio_mode", 2, bif_gpio_mode_2, "+integer,+atom", false, false, BLAH},
	{"gpio_pull", 2, bif_gpio_pull_2, "+integer,+atom", false, false, BLAH},
	{"gpio_read", 2, bif_gpio_read_2, "+integer,?integer", false, false, BLAH},
	{"gpio_write", 2, bif_gpio_write_2, "+integer,+integer", false, false, BLAH},
	{0}
};
