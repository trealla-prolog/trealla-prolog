#include <string.h>

#include "prolog.h"
#include "query.h"

#include "net.h"

// Prolog access to the device-agnostic IPv4/UDP stack, for a build that has
// no operating system to provide sockets. Offered through g_port_bif_tables,
// so a port lists it in its manifest alongside whatever else it exposes.
//
// The stack is polled, and nothing here runs concurrently with the engine, so
// it advances only when asked: net_udp_recv/5 drives net_poll() while it waits,
// and a port calls net_stack_service() while the board is otherwise idle. A
// program busy computing never advances it, which is the right trade for a
// single-threaded image with no interrupts.

static net_stack g_net;
static bool g_attached;

// Called by a port once its driver is up. Not a builtin: which device the
// stack sits on is a property of the board, not something Prolog chooses.

bool net_stack_attach(netif *nif, const uint8_t ip[4], const uint8_t mask[4],
	const uint8_t gateway[4])
{
	if (!net_init(&g_net, nif, ip, mask, gateway))
		return false;

	g_attached = true;
	return true;
}

// Called by a port whenever it has nothing better to do. Draining the device
// here answers ARP and ping, and queues datagrams for net_udp_recv/5, whether or
// not a program is waiting in it.

void net_stack_service(void)
{
	if (g_attached)
		while (net_poll(&g_net)) ;
}

static bool need_stack(query *q, cell *p, pl_ctx p_ctx, bool *status)
{
	if (g_attached)
		return true;

	*status = throw_error(q, p, p_ctx, "existence_error", "network_interface");
	return false;
}

// Dotted quad in, four bytes out. Deliberately strict: no hostnames, because
// there is no resolver and never will be at this layer.

static bool parse_ip(const char *text, uint8_t ip[4])
{
	unsigned octet = 0, value = 0, digits = 0;

	for (const char *p = text; ; p++) {
		if ((*p >= '0') && (*p <= '9')) {
			value = value * 10 + (unsigned)(*p - '0');
			digits++;

			if ((value > 255) || (digits > 3))
				return false;
		} else if ((*p == '.') || !*p) {
			if (!digits || (octet > 3))
				return false;

			ip[octet++] = (uint8_t)value;
			value = digits = 0;

			if (!*p)
				break;
		} else
			return false;
	}

	return octet == 4;
}

static bool get_port(query *q, cell *p, pl_ctx p_ctx, uint16_t *port,
	bool *status)
{
	if (is_bigint(p) || !is_smallint(p)) {
		*status = throw_error(q, p, p_ctx, "type_error", "integer");
		return false;
	}

	pl_int value = get_smallint(p);

	if ((value < 1) || (value > 65535)) {
		*status = throw_error(q, p, p_ctx, "domain_error", "udp_port");
		return false;
	}

	*port = (uint16_t)value;
	return true;
}

// A socket here is its port. library/freestanding/socket.pl builds
// library(socket)'s handles on top, so that library(tftp) runs unchanged.

static bool bif_net_udp_open_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	uint16_t port;
	bool status;

	if (!get_port(q, p1, p1_ctx, &port, &status))
		return status;

	if (!need_stack(q, p1, p1_ctx, &status))
		return status;

	if (!net_udp_bind(&g_net, port))
		return throw_error(q, p1, p1_ctx, "permission_error", "open,udp_port");

	cell tmp;
	make_int(&tmp, port);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_net_udp_close_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	uint16_t port;
	bool status;

	if (!get_port(q, p1, p1_ctx, &port, &status))
		return status;

	if (!need_stack(q, p1, p1_ctx, &status))
		return status;

	net_udp_close(&g_net, port);
	return true;
}

// Data is a list of byte values, as everything binary in Trealla is.

static bool collect_bytes(query *q, cell *l, pl_ctx l_ctx, uint8_t *buf,
	size_t max, size_t *len, bool *status)
{
	size_t n = 0;
	PROLOG_LIST_HANDLER(l);

	while (is_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		cell *c = deref(q, h, l_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!is_smallint(c) || (get_smallint(c) < 0) || (get_smallint(c) > 255)) {
			*status = throw_error(q, c, c_ctx, "type_error", "byte");
			return false;
		}

		if (n == max) {
			*status = throw_error(q, l, l_ctx, "resource_error", "datagram_too_long");
			return false;
		}

		buf[n++] = (uint8_t)get_smallint(c);
		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	*len = n;
	return true;
}

static bool bif_net_udp_send_4(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,integer);
	GET_NEXT_ARG(p4,list_or_nil);
	uint16_t src_port, dst_port;
	bool status;

	if (!get_port(q, p1, p1_ctx, &src_port, &status))
		return status;

	if (!get_port(q, p3, p3_ctx, &dst_port, &status))
		return status;

	if (!need_stack(q, p1, p1_ctx, &status))
		return status;

	uint8_t dst[4];

	if (!parse_ip(C_STR(q, p2), dst))
		return throw_error(q, p2, p2_ctx, "domain_error", "ip_address");

	static uint8_t payload[NET_UDP_PAYLOAD_MAX];
	size_t len;

	if (!collect_bytes(q, p4, p4_ctx, payload, sizeof(payload), &len, &status))
		return status;

	// A first send to an unknown peer fails having sent an ARP request. Pump
	// the stack briefly so the reply can arrive, then try once more - which
	// spares every caller from having to know that.
	if (!net_udp_send(&g_net, dst, dst_port, src_port, payload, len)) {
		// The first send to an unknown peer only emits an ARP request. Give
		// the reply a bounded moment to arrive rather than making every
		// caller understand that.
		uint64_t deadline = monotonic_time_in_usec() + 500000;

		do {
			while (net_poll(&g_net)) ;

			if (net_udp_send(&g_net, dst, dst_port, src_port, payload, len))
				return true;
		} while (monotonic_time_in_usec() < deadline);

		return false;
	}

	return true;
}

static bool bif_net_udp_recv_5(query *q)
{
	GET_FIRST_ARG(p1,integer);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);
	GET_NEXT_ARG(p4,any);
	GET_NEXT_ARG(p5,integer);
	uint16_t port;
	bool status;

	if (!get_port(q, p1, p1_ctx, &port, &status))
		return status;

	if (!need_stack(q, p1, p1_ctx, &status))
		return status;

	if (is_bigint(p5) || (get_smallint(p5) < 0))
		return throw_error(q, p5, p5_ctx, "domain_error", "not_less_than_zero");

	// Driving net_poll() here is the whole of the stack's scheduling: there
	// is no interrupt to do it and no thread to do it in.
	//
	// The wait is real milliseconds off the platform's monotonic clock, not a
	// spin count. It was a spin count first, which meant a "400000 ms" wait
	// finished in a fraction of a second and the caller saw a timeout that
	// had not happened.
	uint64_t deadline = monotonic_time_in_usec()
		+ (uint64_t)get_smallint(p5) * 1000;
	static uint8_t payload[NET_UDP_PAYLOAD_MAX];
	uint8_t from[4];
	uint16_t from_port = 0;
	size_t len = 0;

	for (;;) {
		while (net_poll(&g_net)) ;

		len = net_udp_recv(&g_net, port, from, &from_port, payload,
			sizeof(payload));

		if (len || (monotonic_time_in_usec() >= deadline))
			break;
	}

	if (!len)
		return false;						// timed out: a failure, not a fault

	char dotted[16];
	unsigned n = 0;

	for (unsigned i = 0; i < 4; i++) {
		unsigned v = from[i];

		if (v >= 100) dotted[n++] = (char)('0' + v / 100);
		if (v >= 10)  dotted[n++] = (char)('0' + (v / 10) % 10);
		dotted[n++] = (char)('0' + v % 10);
		dotted[n++] = (i < 3) ? '.' : '\0';
	}

	cell tmp;
	make_atom(&tmp, new_atom(q->pl, dotted));

	if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_int(&tmp, from_port);

	if (!unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx))
		return false;

	if (!init_tmp_heap(q))
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	for (size_t i = 0; i < len; i++) {
		cell b;
		make_int(&b, payload[i]);
		append_list(q, &b);
	}

	cell *l = len ? end_list(q) : make_nil();

	if (!l)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	return unify(q, p4, p4_ctx, l, q->st.cur_ctx);
}

// Link state, asked of whatever netif is attached rather than of any
// particular device - the stack knows nothing about PHYs. Worth having as a
// predicate because carrier changes while the board is running, and
// rebooting to find out is a poor way to debug a cable.

static bool bif_net_link_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	bool status;

	if (!need_stack(q, p1, p1_ctx, &status))
		return status;

	bool up = g_net.nif && g_net.nif->link_up && g_net.nif->link_up(g_net.nif);
	cell tmp;
	make_atom(&tmp, new_atom(q->pl, up ? "up" : "down"));
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

// The stack's own counters. Worth a predicate because they answer the first
// question a silent link raises - whether anything is arriving at all -
// which otherwise needs a second machine and a packet capture to guess at.

static bool bif_net_stats_5(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,any);
	GET_NEXT_ARG(p3,any);
	GET_NEXT_ARG(p4,any);
	GET_NEXT_ARG(p5,any);
	bool status;

	if (!need_stack(q, p1, p1_ctx, &status))
		return status;

	const unsigned values[5] = {
		g_net.rx_frames, g_net.rx_dropped, g_net.tx_frames,
		g_net.arp_requests, g_net.icmp_echoes
	};

	cell *args[5] = {p1, p2, p3, p4, p5};
	pl_ctx ctxs[5] = {p1_ctx, p2_ctx, p3_ctx, p4_ctx, p5_ctx};

	for (unsigned i = 0; i < 5; i++) {
		cell tmp;
		make_int(&tmp, values[i]);

		if (!unify(q, args[i], ctxs[i], &tmp, q->st.cur_ctx))
			return false;
	}

	return true;
}

builtins g_netstack_bifs[] =
{
	{"net_link", 1, bif_net_link_1, "?atom", false, false, BLAH},
	{"net_stats", 5, bif_net_stats_5, "?integer,?integer,?integer,?integer,?integer", false, false, BLAH},
	{"net_udp_open", 2, bif_net_udp_open_2, "+integer,-integer", false, false, BLAH},
	{"net_udp_close", 1, bif_net_udp_close_1, "+integer", false, false, BLAH},
	{"net_udp_send", 4, bif_net_udp_send_4, "+integer,+atom,+integer,+list", false, false, BLAH},
	{"net_udp_recv", 5, bif_net_udp_recv_5, "+integer,-atom,-integer,-list,+integer", false, false, BLAH},
	{0}
};
