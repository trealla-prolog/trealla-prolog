#include <string.h>

#include "platform/platform.h"

#include "bcm2711.h"
#include "genet.h"

/*
 * Broadcom GENET v5 Ethernet, as found on the BCM2711. Polled, one queue, no
 * interrupts, no checksum offload - enough to move frames for the IPv4/UDP
 * stack in src/net.
 *
 * There is no public documentation for this controller. The register map and
 * the bring-up sequences below are adapted from FreeBSD's driver, which is
 * BSD-2-clause:
 *
 *   sys/arm64/broadcom/genet/if_genet.c and if_genetreg.h
 *   Copyright (c) 2020 Michael J Karels
 *   Copyright (c) 2016, 2020 Jared McNeill <jmcneill@invisible.ca>
 *
 * Two facts from reading it shape everything here. The DMA descriptors live
 * in the controller's own register window rather than in memory, so they need
 * no cache maintenance at all; only the packet buffers are memory the device
 * touches, and those come from the non-cacheable window mmu.c maps. And the
 * receiver is told to pad frames by two bytes, so a received frame starts two
 * bytes into its buffer.
 */

#define GENET_BASE 0xfd580000u
#define REG32(off) (*(volatile uint32_t*)(uintptr_t)(GENET_BASE + (off)))

#define GENET_SYS_REV_CTRL 0x000
#define GENET_SYS_PORT_CTRL 0x004
#define GENET_SYS_RBUF_FLUSH_CTRL 0x008
#define  SYS_RBUF_FLUSH_RESET (1u << 1)

#define GENET_EXT_RGMII_OOB_CTRL 0x08c
#define  OOB_ID_MODE_DISABLE (1u << 16)
#define  OOB_RGMII_MODE_EN (1u << 6)
#define  OOB_DISABLE (1u << 5)
#define  OOB_RGMII_LINK (1u << 4)

#define GENET_INTRL2_CPU_SET_MASK 0x210
#define GENET_INTRL2_CPU_CLEAR_MASK 0x214

#define GENET_RBUF_CTRL 0x300
#define  RBUF_ALIGN_2B (1u << 1)
#define GENET_RBUF_TBUF_SIZE_CTRL 0x3b4

#define GENET_UMAC_CMD 0x808
#define  UMAC_CMD_LCL_LOOP_EN (1u << 15)
#define  UMAC_CMD_SW_RESET (1u << 13)
#define  UMAC_CMD_SPEED_MASK (3u << 2)
#define  UMAC_CMD_SPEED_10 (0u << 2)
#define  UMAC_CMD_SPEED_100 (1u << 2)
#define  UMAC_CMD_SPEED_1000 (2u << 2)
#define  UMAC_CMD_RXEN (1u << 1)
#define  UMAC_CMD_TXEN (1u << 0)
#define GENET_UMAC_MAC0 0x80c
#define GENET_UMAC_MAC1 0x810
#define GENET_UMAC_MAX_FRAME_LEN 0x814
#define GENET_UMAC_TX_FLUSH 0xb34
#define GENET_UMAC_MIB_CTRL 0xd80
#define  MIB_RESET_TX (1u << 2)
#define  MIB_RESET_RUNT (1u << 1)
#define  MIB_RESET_RX (1u << 0)

#define GENET_MDIO_CMD 0xe14
#define  MDIO_START_BUSY (1u << 29)
#define  MDIO_READ_FAILED (1u << 28)
#define  MDIO_READ (1u << 27)
#define  MDIO_WRITE (1u << 26)
#define  MDIO_ADDR_SHIFT 21
#define  MDIO_REG_SHIFT 16
#define  MDIO_VAL_MASK 0xffffu

#define GENET_DMA_DESC_SIZE 12
#define GENET_DMA_RING_SIZE 0x40
#define GENET_DEFAULT_QUEUE 16

#define GENET_RX_BASE 0x2000
#define GENET_TX_BASE 0x4000

#define RX_RING(q) (GENET_RX_BASE + 0xc00 + GENET_DMA_RING_SIZE * (q))
#define TX_RING(q) (GENET_TX_BASE + 0xc00 + GENET_DMA_RING_SIZE * (q))

#define RX_WRITE_PTR_LO(q) (RX_RING(q) + 0x00)
#define RX_WRITE_PTR_HI(q) (RX_RING(q) + 0x04)
#define RX_PROD_INDEX(q) (RX_RING(q) + 0x08)
#define RX_CONS_INDEX(q) (RX_RING(q) + 0x0c)
#define RX_RING_BUF_SIZE(q) (RX_RING(q) + 0x10)
#define RX_START_ADDR_LO(q) (RX_RING(q) + 0x14)
#define RX_START_ADDR_HI(q) (RX_RING(q) + 0x18)
#define RX_END_ADDR_LO(q) (RX_RING(q) + 0x1c)
#define RX_END_ADDR_HI(q) (RX_RING(q) + 0x20)
#define RX_XON_XOFF_THRES(q) (RX_RING(q) + 0x28)
#define RX_READ_PTR_LO(q) (RX_RING(q) + 0x2c)
#define RX_READ_PTR_HI(q) (RX_RING(q) + 0x30)

#define TX_READ_PTR_LO(q) (TX_RING(q) + 0x00)
#define TX_READ_PTR_HI(q) (TX_RING(q) + 0x04)
#define TX_CONS_INDEX(q) (TX_RING(q) + 0x08)
#define TX_PROD_INDEX(q) (TX_RING(q) + 0x0c)
#define TX_RING_BUF_SIZE(q) (TX_RING(q) + 0x10)
#define TX_START_ADDR_LO(q) (TX_RING(q) + 0x14)
#define TX_START_ADDR_HI(q) (TX_RING(q) + 0x18)
#define TX_END_ADDR_LO(q) (TX_RING(q) + 0x1c)
#define TX_END_ADDR_HI(q) (TX_RING(q) + 0x20)
#define TX_MBUF_DONE_THRES(q) (TX_RING(q) + 0x24)
#define TX_WRITE_PTR_LO(q) (TX_RING(q) + 0x2c)
#define TX_WRITE_PTR_HI(q) (TX_RING(q) + 0x30)

#define RX_DESC_STATUS(i) (GENET_RX_BASE + GENET_DMA_DESC_SIZE * (i) + 0x00)
#define RX_DESC_ADDR_LO(i) (GENET_RX_BASE + GENET_DMA_DESC_SIZE * (i) + 0x04)
#define RX_DESC_ADDR_HI(i) (GENET_RX_BASE + GENET_DMA_DESC_SIZE * (i) + 0x08)
#define TX_DESC_STATUS(i) (GENET_TX_BASE + GENET_DMA_DESC_SIZE * (i) + 0x00)
#define TX_DESC_ADDR_LO(i) (GENET_TX_BASE + GENET_DMA_DESC_SIZE * (i) + 0x04)
#define TX_DESC_ADDR_HI(i) (GENET_TX_BASE + GENET_DMA_DESC_SIZE * (i) + 0x08)

#define DESC_BUFLEN_SHIFT 16
#define DESC_BUFLEN_MASK 0x0fff0000u
#define DESC_EOP (1u << 14)
#define DESC_SOP (1u << 13)
#define DESC_QTAG_MASK 0x1f80u
#define DESC_CRC (1u << 6)
#define DESC_RX_ERROR (1u << 2)

#define GENET_RX_DMA_RING_CFG (GENET_RX_BASE + 0x1040)
#define GENET_RX_DMA_CTRL (GENET_RX_BASE + 0x1044)
#define GENET_RX_SCB_BURST_SIZE (GENET_RX_BASE + 0x104c)
#define GENET_TX_DMA_RING_CFG (GENET_TX_BASE + 0x1040)
#define GENET_TX_DMA_CTRL (GENET_TX_BASE + 0x1044)
#define GENET_TX_SCB_BURST_SIZE (GENET_TX_BASE + 0x104c)
#define  DMA_CTRL_EN (1u << 0)
#define  DMA_CTRL_RBUF_EN(q) (1u << ((q) + 1))

#define PROD_CONS_MASK 0xffffu

// Powers of two: the ring index is masked, not divided.
#define RX_DESC_COUNT 32
#define TX_DESC_COUNT 32
#define GENET_BUF_SIZE 2048
#define RX_PAD 2					// the receiver's two-byte alignment pad

typedef struct {
	uint8_t *rx_buf[RX_DESC_COUNT];
	uint8_t *tx_buf[TX_DESC_COUNT];
	uint32_t rx_cons, tx_prod;
	unsigned phy;
	bool link;
} genet_state;

static genet_state g_genet;

// A bump allocator over the non-cacheable window. Nothing is ever freed:
// the rings are allocated once and live for as long as the board runs.

static void udelay(unsigned us)
{
	uint64_t until = tpl_platform_monotonic_usec() + us;

	while (tpl_platform_monotonic_usec() < until)
		;
}

// --- MDIO ---------------------------------------------------------------

static uint16_t mdio_read(unsigned phy, unsigned reg)
{
	REG32(GENET_MDIO_CMD) = MDIO_READ | (phy << MDIO_ADDR_SHIFT)
		| (reg << MDIO_REG_SHIFT);
	uint32_t val = REG32(GENET_MDIO_CMD);
	REG32(GENET_MDIO_CMD) = val | MDIO_START_BUSY;

	for (unsigned i = 0; i < 1000; i++) {
		val = REG32(GENET_MDIO_CMD);

		if (!(val & MDIO_START_BUSY))
			return (val & MDIO_READ_FAILED) ? 0xffff
				: (uint16_t)(val & MDIO_VAL_MASK);

		udelay(10);
	}

	return 0xffff;
}

static void mdio_write(unsigned phy, unsigned reg, uint16_t value)
{
	REG32(GENET_MDIO_CMD) = MDIO_WRITE | (phy << MDIO_ADDR_SHIFT)
		| (reg << MDIO_REG_SHIFT) | value;
	uint32_t val = REG32(GENET_MDIO_CMD);
	REG32(GENET_MDIO_CMD) = val | MDIO_START_BUSY;

	for (unsigned i = 0; i < 1000; i++) {
		if (!(REG32(GENET_MDIO_CMD) & MDIO_START_BUSY))
			return;

		udelay(10);
	}
}

// Clause 22 registers only, so this does not depend on the BCM54213 the Pi 4
// happens to carry.
#define MII_BMCR 0x00
#define  BMCR_RESET (1u << 15)
#define  BMCR_ANEG_ENABLE (1u << 12)
#define  BMCR_ANEG_RESTART (1u << 9)
#define MII_BMSR 0x01
#define  BMSR_ANEG_DONE (1u << 5)
#define  BMSR_LINK (1u << 2)
#define MII_PHYID1 0x02
#define MII_ANLPAR 0x05
#define  ANLPAR_100 0x0180
#define MII_STAT1000 0x0a
#define  STAT1000_LP_1000 0x0c00

static bool phy_find(unsigned *addr)
{
	// The Pi 4's PHY answers at 1, but scan rather than assume: a wrong
	// address reads back 0xffff from everything and looks like dead silicon.
	for (unsigned a = 0; a < 32; a++) {
		uint16_t id = mdio_read(a, MII_PHYID1);

		if ((id != 0xffff) && (id != 0)) {
			*addr = a;
			return true;
		}
	}

	return false;
}

// BMSR's link bit latches low, so a stale reading has to be cleared by
// reading it twice before the answer means anything.

static bool phy_link_up(unsigned phy)
{
	mdio_read(phy, MII_BMSR);
	return (mdio_read(phy, MII_BMSR) & BMSR_LINK) != 0;
}

static uint32_t phy_speed(unsigned phy)
{
	if (mdio_read(phy, MII_STAT1000) & STAT1000_LP_1000)
		return UMAC_CMD_SPEED_1000;

	if (mdio_read(phy, MII_ANLPAR) & ANLPAR_100)
		return UMAC_CMD_SPEED_100;

	return UMAC_CMD_SPEED_10;
}

// --- controller ---------------------------------------------------------

static void genet_reset(void)
{
	uint32_t val = REG32(GENET_SYS_RBUF_FLUSH_CTRL);
	REG32(GENET_SYS_RBUF_FLUSH_CTRL) = val | SYS_RBUF_FLUSH_RESET;
	udelay(10);
	REG32(GENET_SYS_RBUF_FLUSH_CTRL) = val & ~SYS_RBUF_FLUSH_RESET;
	udelay(10);
	REG32(GENET_SYS_RBUF_FLUSH_CTRL) = 0;
	udelay(10);

	REG32(GENET_UMAC_CMD) = 0;
	REG32(GENET_UMAC_CMD) = UMAC_CMD_LCL_LOOP_EN | UMAC_CMD_SW_RESET;
	udelay(10);
	REG32(GENET_UMAC_CMD) = 0;

	REG32(GENET_UMAC_MIB_CTRL) = MIB_RESET_RUNT | MIB_RESET_RX | MIB_RESET_TX;
	REG32(GENET_UMAC_MIB_CTRL) = 0;
}

static void dma_disable(void)
{
	uint32_t val = REG32(GENET_TX_DMA_CTRL);
	REG32(GENET_TX_DMA_CTRL) = val
		& ~(DMA_CTRL_EN | DMA_CTRL_RBUF_EN(GENET_DEFAULT_QUEUE));
	val = REG32(GENET_RX_DMA_CTRL);
	REG32(GENET_RX_DMA_CTRL) = val
		& ~(DMA_CTRL_EN | DMA_CTRL_RBUF_EN(GENET_DEFAULT_QUEUE));

	REG32(GENET_UMAC_TX_FLUSH) = 1;
	udelay(10);
	REG32(GENET_UMAC_TX_FLUSH) = 0;
}

static bool rings_init(void)
{
	const unsigned q = GENET_DEFAULT_QUEUE;

	REG32(GENET_RX_SCB_BURST_SIZE) = 0x08;
	REG32(RX_WRITE_PTR_LO(q)) = 0;
	REG32(RX_WRITE_PTR_HI(q)) = 0;
	REG32(RX_PROD_INDEX(q)) = 0;
	REG32(RX_CONS_INDEX(q)) = 0;
	REG32(RX_RING_BUF_SIZE(q)) = (RX_DESC_COUNT << 16) | GENET_BUF_SIZE;
	REG32(RX_START_ADDR_LO(q)) = 0;
	REG32(RX_START_ADDR_HI(q)) = 0;
	REG32(RX_END_ADDR_LO(q)) = RX_DESC_COUNT * GENET_DMA_DESC_SIZE / 4 - 1;
	REG32(RX_END_ADDR_HI(q)) = 0;
	REG32(RX_XON_XOFF_THRES(q)) = (5u << 16) | (RX_DESC_COUNT >> 4);
	REG32(RX_READ_PTR_LO(q)) = 0;
	REG32(RX_READ_PTR_HI(q)) = 0;
	REG32(GENET_RX_DMA_RING_CFG) = 1u << q;

	for (unsigned i = 0; i < RX_DESC_COUNT; i++) {
		g_genet.rx_buf[i] = rpi4_dma_alloc(GENET_BUF_SIZE);

		if (!g_genet.rx_buf[i])
			return false;

		uint64_t pa = (uint64_t)(uintptr_t)g_genet.rx_buf[i];
		REG32(RX_DESC_ADDR_LO(i)) = (uint32_t)pa;
		REG32(RX_DESC_ADDR_HI(i)) = (uint32_t)(pa >> 32);
	}

	REG32(GENET_TX_SCB_BURST_SIZE) = 0x08;
	REG32(TX_READ_PTR_LO(q)) = 0;
	REG32(TX_READ_PTR_HI(q)) = 0;
	REG32(TX_CONS_INDEX(q)) = 0;
	REG32(TX_PROD_INDEX(q)) = 0;
	REG32(TX_RING_BUF_SIZE(q)) = (TX_DESC_COUNT << 16) | GENET_BUF_SIZE;
	REG32(TX_START_ADDR_LO(q)) = 0;
	REG32(TX_START_ADDR_HI(q)) = 0;
	REG32(TX_END_ADDR_LO(q)) = TX_DESC_COUNT * GENET_DMA_DESC_SIZE / 4 - 1;
	REG32(TX_END_ADDR_HI(q)) = 0;
	REG32(TX_MBUF_DONE_THRES(q)) = 1;
	REG32(TX_WRITE_PTR_LO(q)) = 0;
	REG32(TX_WRITE_PTR_HI(q)) = 0;
	REG32(GENET_TX_DMA_RING_CFG) = 1u << q;

	for (unsigned i = 0; i < TX_DESC_COUNT; i++) {
		g_genet.tx_buf[i] = rpi4_dma_alloc(GENET_BUF_SIZE);

		if (!g_genet.tx_buf[i])
			return false;
	}

	REG32(GENET_RX_DMA_CTRL) = DMA_CTRL_RBUF_EN(q) | DMA_CTRL_EN;
	REG32(GENET_TX_DMA_CTRL) = DMA_CTRL_RBUF_EN(q) | DMA_CTRL_EN;
	return true;
}

static void genet_enable(void)
{
	REG32(GENET_UMAC_MAX_FRAME_LEN) = 1536;
	REG32(GENET_RBUF_CTRL) = REG32(GENET_RBUF_CTRL) | RBUF_ALIGN_2B;
	REG32(GENET_RBUF_TBUF_SIZE_CTRL) = 1;
	REG32(GENET_UMAC_CMD) = REG32(GENET_UMAC_CMD)
		| UMAC_CMD_TXEN | UMAC_CMD_RXEN;

	// Polled, so every interrupt source stays masked.
	REG32(GENET_INTRL2_CPU_SET_MASK) = 0xffffffffu;
	REG32(GENET_INTRL2_CPU_CLEAR_MASK) = 0xffffffffu;
}

// --- netif ---------------------------------------------------------------

static bool genet_link_up(netif *nif)
{
	(void)nif;

	if (!phy_link_up(g_genet.phy)) {
		g_genet.link = false;
		return false;
	}

	if (!g_genet.link) {
		// Tell the MAC what the PHY settled on, and take the RGMII link
		// out of band signalling out of reset. Without this the link is up
		// as far as the PHY is concerned and no frame ever moves.
		uint32_t oob = REG32(GENET_EXT_RGMII_OOB_CTRL);
		oob &= ~OOB_DISABLE;
		oob |= OOB_RGMII_LINK | OOB_RGMII_MODE_EN | OOB_ID_MODE_DISABLE;
		REG32(GENET_EXT_RGMII_OOB_CTRL) = oob;

		uint32_t cmd = REG32(GENET_UMAC_CMD);
		cmd &= ~UMAC_CMD_SPEED_MASK;
		cmd |= phy_speed(g_genet.phy);
		REG32(GENET_UMAC_CMD) = cmd;
		g_genet.link = true;
	}

	return true;
}

static bool genet_send(netif *nif, const void *frame, size_t len)
{
	(void)nif;
	const unsigned q = GENET_DEFAULT_QUEUE;

	if (!len || (len > GENET_BUF_SIZE))
		return false;

	// One outstanding frame per descriptor: wait for the controller to
	// catch up rather than overwrite a buffer it has not sent yet.
	uint32_t cons = REG32(TX_CONS_INDEX(q)) & PROD_CONS_MASK;

	if (((g_genet.tx_prod - cons) & PROD_CONS_MASK) >= TX_DESC_COUNT)
		return false;

	unsigned index = g_genet.tx_prod & (TX_DESC_COUNT - 1);
	memcpy(g_genet.tx_buf[index], frame, len);

	uint64_t pa = (uint64_t)(uintptr_t)g_genet.tx_buf[index];
	REG32(TX_DESC_ADDR_LO(index)) = (uint32_t)pa;
	REG32(TX_DESC_ADDR_HI(index)) = (uint32_t)(pa >> 32);
	REG32(TX_DESC_STATUS(index)) = DESC_QTAG_MASK | DESC_SOP | DESC_EOP
		| DESC_CRC | ((uint32_t)len << DESC_BUFLEN_SHIFT);

	g_genet.tx_prod = (g_genet.tx_prod + 1) & PROD_CONS_MASK;
	REG32(TX_PROD_INDEX(q)) = g_genet.tx_prod;
	return true;
}

static size_t genet_poll(netif *nif, void *frame, size_t maxlen)
{
	(void)nif;
	const unsigned q = GENET_DEFAULT_QUEUE;
	uint32_t prod = REG32(RX_PROD_INDEX(q)) & PROD_CONS_MASK;

	if (prod == g_genet.rx_cons)
		return 0;

	unsigned index = g_genet.rx_cons & (RX_DESC_COUNT - 1);
	uint32_t status = REG32(RX_DESC_STATUS(index));
	size_t len = (status & DESC_BUFLEN_MASK) >> DESC_BUFLEN_SHIFT;
	size_t got = 0;

	// A frame the controller could not deliver whole is dropped rather than
	// passed up in pieces: the stack above has no reassembly and wants none.
	if (((status & (DESC_SOP | DESC_EOP | DESC_RX_ERROR))
		== (DESC_SOP | DESC_EOP)) && (len > RX_PAD)) {
		len -= RX_PAD;					// skip the receiver's alignment pad
		got = len < maxlen ? len : maxlen;
		memcpy(frame, g_genet.rx_buf[index] + RX_PAD, got);
	}

	g_genet.rx_cons = (g_genet.rx_cons + 1) & PROD_CONS_MASK;
	REG32(RX_CONS_INDEX(q)) = g_genet.rx_cons;
	return got;
}

static bool genet_init(netif *nif)
{
	(void)nif;
	return true;						// rpi4_genet_open did the work
}

const char *rpi4_genet_open(netif *nif, const uint8_t mac[6], unsigned *phy)
{
	memset(&g_genet, 0, sizeof(g_genet));

	genet_reset();
	dma_disable();

	if (!phy_find(&g_genet.phy))
		return "nothing answered on the MDIO bus";

	// Restart autonegotiation and let it run; link comes up in its own time
	// and genet_link_up reports when.
	mdio_write(g_genet.phy, MII_BMCR, BMCR_ANEG_ENABLE | BMCR_ANEG_RESTART);

	REG32(GENET_UMAC_MAC0) = ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16)
		| ((uint32_t)mac[2] << 8) | mac[3];
	REG32(GENET_UMAC_MAC1) = ((uint32_t)mac[4] << 8) | mac[5];

	if (!rings_init())
		return "out of DMA buffers";

	genet_enable();

	memset(nif, 0, sizeof(*nif));
	nif->name = "genet0";
	memcpy(nif->mac, mac, 6);
	nif->init = genet_init;
	nif->link_up = genet_link_up;
	nif->send = genet_send;
	nif->poll = genet_poll;

	if (phy)
		*phy = g_genet.phy;

	return NULL;
}

bool rpi4_genet_link(void)
{
	return phy_link_up(g_genet.phy);
}

// open() restarts autonegotiation, which takes a second or two, so asking
// immediately afterwards can only ever answer "down". Waiting for it is the
// difference between a boot line that means something and one that always
// says the same thing.

bool rpi4_genet_link_wait(unsigned ms)
{
	uint64_t deadline = tpl_platform_monotonic_usec() + (uint64_t)ms * 1000;

	do {
		if (phy_link_up(g_genet.phy))
			return true;
	} while (tpl_platform_monotonic_usec() < deadline);

	return false;
}
