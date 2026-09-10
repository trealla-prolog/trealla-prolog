#include "prolog.h"
#include "query.h"

// What this port hands the engine. One entry per subsystem: the engine walks
// them all, so adding networking later is a line here and a new table, not a
// change to the GPIO file or to src/.

extern builtins g_gpio_bifs[];
extern builtins g_fb_bifs[];

#if RPI4_NET
extern builtins g_netstack_bifs[];
extern builtins g_genet_bifs[];
#endif

builtins *g_port_bif_tables[] =
{
	g_gpio_bifs,
	g_fb_bifs,
#if RPI4_NET
	g_netstack_bifs,
	g_genet_bifs,
#endif
	NULL
};
