#include <string.h>

#include "net.h"

// Byte-wise accessors throughout. The IPv4 header starts at offset 14 in an
// Ethernet frame, so its 32-bit fields are never 4-aligned; reading them a
// byte at a time sidesteps that entirely and needs no htons/ntohs, which a
// freestanding build may not have.

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

#define ETH_HDR 14
#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806

#define ARP_HDR 28
#define ARP_REQUEST 1
#define ARP_REPLY 2

#define IP_HDR 20
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP 17

#define ICMP_ECHO_REQUEST 8
#define ICMP_ECHO_REPLY 0

#define UDP_HDR 8

static const uint8_t broadcast_mac[NETIF_MAC_LEN] =
	{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

// The internet checksum: one's complement of the one's complement sum of
// 16-bit words, with an odd trailing byte padded. RFC 1071.

uint16_t net_checksum(const void *data, size_t len)
{
	const uint8_t *p = data;
	uint32_t sum = 0;

	while (len > 1) {
		sum += rd16(p);
		p += 2;
		len -= 2;
	}

	if (len)
		sum += (uint32_t)p[0] << 8;

	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return (uint16_t)~sum;
}

static bool ip_equal(const uint8_t a[4], const uint8_t b[4])
{
	return !memcmp(a, b, 4);
}

// --- ARP ---------------------------------------------------------------

static void arp_remember(net_stack *net, const uint8_t ip[4],
	const uint8_t mac[NETIF_MAC_LEN])
{
	net_arp_entry *slot = NULL;

	for (unsigned i = 0; i < NET_ARP_ENTRIES; i++) {
		if (net->arp[i].valid && ip_equal(net->arp[i].ip, ip)) {
			slot = &net->arp[i];
			break;
		}
	}

	if (!slot) {
		for (unsigned i = 0; i < NET_ARP_ENTRIES; i++) {
			if (!net->arp[i].valid) {
				slot = &net->arp[i];
				break;
			}
		}
	}

	// Full and none of them is this address: evict the least recently used.
	if (!slot) {
		slot = &net->arp[0];

		for (unsigned i = 1; i < NET_ARP_ENTRIES; i++) {
			if (net->arp[i].age < slot->age)
				slot = &net->arp[i];
		}
	}

	memcpy(slot->ip, ip, 4);
	memcpy(slot->mac, mac, NETIF_MAC_LEN);
	slot->valid = true;
	slot->age = ++net->clock;
}

static bool arp_lookup(net_stack *net, const uint8_t ip[4],
	uint8_t mac[NETIF_MAC_LEN])
{
	for (unsigned i = 0; i < NET_ARP_ENTRIES; i++) {
		if (net->arp[i].valid && ip_equal(net->arp[i].ip, ip)) {
			net->arp[i].age = ++net->clock;
			memcpy(mac, net->arp[i].mac, NETIF_MAC_LEN);
			return true;
		}
	}

	return false;
}

static bool eth_send(net_stack *net, const uint8_t dst[NETIF_MAC_LEN],
	uint16_t type, size_t payload_len)
{
	memcpy(net->tx, dst, NETIF_MAC_LEN);
	memcpy(net->tx + 6, net->nif->mac, NETIF_MAC_LEN);
	wr16(net->tx + 12, type);

	if (!net->nif->send(net->nif, net->tx, ETH_HDR + payload_len))
		return false;

	net->tx_frames++;
	return true;
}

static bool arp_send(net_stack *net, uint16_t op, const uint8_t target_ip[4],
	const uint8_t target_mac[NETIF_MAC_LEN], const uint8_t dst[NETIF_MAC_LEN])
{
	uint8_t *a = net->tx + ETH_HDR;
	wr16(a + 0, 1);						// hardware type: Ethernet
	wr16(a + 2, ETH_TYPE_IP);			// protocol type
	a[4] = NETIF_MAC_LEN;
	a[5] = 4;
	wr16(a + 6, op);
	memcpy(a + 8, net->nif->mac, NETIF_MAC_LEN);
	memcpy(a + 14, net->ip, 4);
	memcpy(a + 18, target_mac, NETIF_MAC_LEN);
	memcpy(a + 24, target_ip, 4);
	return eth_send(net, dst, ETH_TYPE_ARP, ARP_HDR);
}

static void arp_input(net_stack *net, const uint8_t *frame, size_t len)
{
	if (len < ETH_HDR + ARP_HDR) {
		net->rx_dropped++;
		return;
	}

	const uint8_t *a = frame + ETH_HDR;

	if ((rd16(a + 0) != 1) || (rd16(a + 2) != ETH_TYPE_IP)
		|| (a[4] != NETIF_MAC_LEN) || (a[5] != 4)) {
		net->rx_dropped++;
		return;
	}

	uint16_t op = rd16(a + 6);
	const uint8_t *sender_mac = a + 8;
	const uint8_t *sender_ip = a + 14;
	const uint8_t *target_ip = a + 24;

	// Learn from anything addressed to us, request or reply: the peer is
	// about to talk to us either way.
	if (ip_equal(target_ip, net->ip))
		arp_remember(net, sender_ip, sender_mac);

	if ((op == ARP_REQUEST) && ip_equal(target_ip, net->ip)) {
		net->arp_requests++;
		arp_send(net, ARP_REPLY, sender_ip, sender_mac, sender_mac);
	}
}

// --- ICMP --------------------------------------------------------------

static void icmp_input(net_stack *net, const uint8_t *ip_hdr, size_t ip_len,
	size_t hdr_len)
{
	const uint8_t *icmp = ip_hdr + hdr_len;
	size_t icmp_len = ip_len - hdr_len;

	if ((icmp_len < 8) || (icmp[0] != ICMP_ECHO_REQUEST))
		return;

	if (net_checksum(icmp, icmp_len))	// a good packet sums to zero
		return;

	// Echo the payload back verbatim, which is what makes ping meaningful.
	uint8_t *out = net->tx + ETH_HDR;
	size_t total = IP_HDR + icmp_len;

	if (total > NETIF_MTU)
		return;

	memcpy(out + IP_HDR, icmp, icmp_len);
	out[IP_HDR + 0] = ICMP_ECHO_REPLY;
	out[IP_HDR + 2] = out[IP_HDR + 3] = 0;
	wr16(out + IP_HDR + 2, net_checksum(out + IP_HDR, icmp_len));

	uint8_t peer_mac[NETIF_MAC_LEN];

	if (!arp_lookup(net, ip_hdr + 12, peer_mac))
		return;

	out[0] = 0x45;
	out[1] = 0;
	wr16(out + 2, (uint16_t)total);
	wr16(out + 4, 0);
	wr16(out + 6, 0);
	out[8] = 64;
	out[9] = IP_PROTO_ICMP;
	wr16(out + 10, 0);
	memcpy(out + 12, net->ip, 4);
	memcpy(out + 16, ip_hdr + 12, 4);
	wr16(out + 10, net_checksum(out, IP_HDR));

	net->icmp_echoes++;
	eth_send(net, peer_mac, ETH_TYPE_IP, total);
}

// --- UDP ---------------------------------------------------------------

static net_udp_socket *udp_find(net_stack *net, uint16_t port)
{
	for (unsigned i = 0; i < NET_UDP_SOCKETS; i++) {
		if (net->sockets[i].bound && (net->sockets[i].port == port))
			return &net->sockets[i];
	}

	return NULL;
}

static void udp_input(net_stack *net, const uint8_t *ip_hdr, size_t ip_len,
	size_t hdr_len)
{
	const uint8_t *udp = ip_hdr + hdr_len;
	size_t avail = ip_len - hdr_len;

	if (avail < UDP_HDR)
		return;

	uint16_t length = rd16(udp + 4);

	if ((length < UDP_HDR) || (length > avail))
		return;

	net_udp_socket *sock = udp_find(net, rd16(udp + 2));

	if (!sock)
		return;

	size_t payload = length - UDP_HDR;

	if (payload > NET_UDP_PAYLOAD_MAX)
		return;

	// A full queue drops the newest rather than overwriting the oldest: a
	// reply that has been waiting is more likely to be the one wanted.
	if (sock->count == NET_UDP_QUEUE) {
		net->rx_dropped++;
		return;
	}

	net_datagram *d = &sock->queue[(sock->head + sock->count) % NET_UDP_QUEUE];
	memcpy(d->from_ip, ip_hdr + 12, 4);
	d->from_port = rd16(udp + 0);
	d->len = (uint16_t)payload;
	memcpy(d->data, udp + UDP_HDR, payload);
	sock->count++;

	// The sender is talking to us, so its address is worth keeping.
	arp_remember(net, ip_hdr + 12, net->rx + 6);
}

// --- IPv4 --------------------------------------------------------------

static void ip_input(net_stack *net, const uint8_t *frame, size_t len)
{
	if (len < ETH_HDR + IP_HDR) {
		net->rx_dropped++;
		return;
	}

	const uint8_t *ip = frame + ETH_HDR;

	if ((ip[0] >> 4) != 4) {
		net->rx_dropped++;
		return;
	}

	size_t hdr_len = (size_t)(ip[0] & 0x0f) * 4;
	size_t total = rd16(ip + 2);

	if ((hdr_len < IP_HDR) || (total < hdr_len) || (total > len - ETH_HDR)) {
		net->rx_dropped++;
		return;
	}

	if (net_checksum(ip, hdr_len)) {	// a good header sums to zero
		net->rx_dropped++;
		return;
	}

	// Fragments are not reassembled. Nothing this stack carries should ever
	// be fragmented, so a fragment is a sign of something unexpected.
	if (rd16(ip + 6) & 0x3fff) {
		net->rx_dropped++;
		return;
	}

	if (!ip_equal(ip + 16, net->ip))
		return;

	if (ip[9] == IP_PROTO_ICMP)
		icmp_input(net, ip, total, hdr_len);
	else if (ip[9] == IP_PROTO_UDP)
		udp_input(net, ip, total, hdr_len);
}

// --- public ------------------------------------------------------------

bool net_init(net_stack *net, netif *nif,
	const uint8_t ip[4], const uint8_t mask[4], const uint8_t gateway[4])
{
	if (!net || !nif || !nif->send || !nif->poll)
		return false;

	memset(net, 0, sizeof(*net));
	net->nif = nif;
	memcpy(net->ip, ip, 4);
	memcpy(net->mask, mask, 4);
	memcpy(net->gateway, gateway, 4);

	if (nif->init && !nif->init(nif))
		return false;

	// A device may need telling what the link negotiated before it will
	// carry a frame - GENET programs its RGMII control and MAC speed here.
	// Nothing called this until it was noticed that a Pi 4 with carrier at
	// both ends was deaf and mute: the PHY negotiates in silicon whether or
	// not the MAC has been configured, so the link looked fine from outside.
	//
	// A device whose link comes up later is reconciled by asking again,
	// which is what net_link/1 does.

	if (nif->link_up)
		nif->link_up(nif);

	return true;
}

bool net_poll(net_stack *net)
{
	size_t len = net->nif->poll(net->nif, net->rx, sizeof(net->rx));

	if (!len)
		return false;

	net->rx_frames++;

	if (len < ETH_HDR) {
		net->rx_dropped++;
		return true;
	}

	// Accept frames for us or for everyone, and ignore the rest: a real
	// device may be in promiscuous mode, and a test one certainly is.
	if (memcmp(net->rx, net->nif->mac, NETIF_MAC_LEN)
		&& memcmp(net->rx, broadcast_mac, NETIF_MAC_LEN))
		return true;

	uint16_t type = rd16(net->rx + 12);

	if (type == ETH_TYPE_ARP)
		arp_input(net, net->rx, len);
	else if (type == ETH_TYPE_IP)
		ip_input(net, net->rx, len);
	else
		net->rx_dropped++;

	return true;
}

bool net_udp_bind(net_stack *net, uint16_t port)
{
	if (!port || udp_find(net, port))
		return false;

	for (unsigned i = 0; i < NET_UDP_SOCKETS; i++) {
		if (!net->sockets[i].bound) {
			net->sockets[i].bound = true;
			net->sockets[i].port = port;
			net->sockets[i].head = net->sockets[i].count = 0;
			return true;
		}
	}

	return false;
}

void net_udp_close(net_stack *net, uint16_t port)
{
	net_udp_socket *sock = udp_find(net, port);

	if (sock)
		sock->bound = false;
}

// Off-subnet traffic goes to the gateway, which is the whole of the routing
// this stack does.

static const uint8_t *next_hop(net_stack *net, const uint8_t dst[4])
{
	for (unsigned i = 0; i < 4; i++) {
		if ((dst[i] & net->mask[i]) != (net->ip[i] & net->mask[i]))
			return net->gateway;
	}

	return dst;
}

bool net_udp_send(net_stack *net, const uint8_t dst_ip[4], uint16_t dst_port,
	uint16_t src_port, const void *data, size_t len)
{
	if (len > NET_UDP_PAYLOAD_MAX)
		return false;

	const uint8_t *hop = next_hop(net, dst_ip);
	uint8_t peer_mac[NETIF_MAC_LEN];

	if (!arp_lookup(net, hop, peer_mac)) {
		arp_send(net, ARP_REQUEST, hop, (const uint8_t[NETIF_MAC_LEN]){0},
			broadcast_mac);
		return false;
	}

	uint8_t *ip = net->tx + ETH_HDR;
	size_t total = IP_HDR + UDP_HDR + len;

	ip[0] = 0x45;
	ip[1] = 0;
	wr16(ip + 2, (uint16_t)total);
	wr16(ip + 4, 0);
	wr16(ip + 6, 0);
	ip[8] = 64;
	ip[9] = IP_PROTO_UDP;
	wr16(ip + 10, 0);
	memcpy(ip + 12, net->ip, 4);
	memcpy(ip + 16, dst_ip, 4);
	wr16(ip + 10, net_checksum(ip, IP_HDR));

	uint8_t *udp = ip + IP_HDR;
	wr16(udp + 0, src_port);
	wr16(udp + 2, dst_port);
	wr16(udp + 4, (uint16_t)(UDP_HDR + len));

	// Zero is a legal UDP checksum over IPv4 and means "not computed",
	// which spares us the pseudo-header.
	wr16(udp + 6, 0);
	memcpy(udp + UDP_HDR, data, len);
	return eth_send(net, peer_mac, ETH_TYPE_IP, total);
}

size_t net_udp_recv(net_stack *net, uint16_t port, uint8_t from_ip[4],
	uint16_t *from_port, void *buf, size_t maxlen)
{
	net_udp_socket *sock = udp_find(net, port);

	if (!sock || !sock->count)
		return 0;

	net_datagram *d = &sock->queue[sock->head];
	size_t len = d->len < maxlen ? d->len : maxlen;

	if (from_ip) memcpy(from_ip, d->from_ip, 4);
	if (from_port) *from_port = d->from_port;
	memcpy(buf, d->data, len);

	sock->head = (sock->head + 1) % NET_UDP_QUEUE;
	sock->count--;
	return len;
}
