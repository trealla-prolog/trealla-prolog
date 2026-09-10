#pragma once

#include "netif.h"

// The BCM2711's Gigabit Ethernet, as a netif. Polled; call the stack's
// net_poll() as often as you care to receive.

// NULL on success, else a short reason. Bring-up has two ways to fail and
// telling them apart is most of the debugging: nothing on the MDIO bus means
// the controller is absent or asleep, where no buffers means the DMA window
// is full. `phy` is filled in on success, for a boot message worth reading.
const char *rpi4_genet_open(netif *nif, const uint8_t mac[6], unsigned *phy);

// Whether the PHY reports carrier. Never called during open, so a boot
// message that omits it cannot tell a dead cable from a dead driver.
bool rpi4_genet_link(void);

// The same, but waits up to `ms` for autonegotiation to settle.
bool rpi4_genet_link_wait(unsigned ms);

// Reads one of the controller's registers by byte offset. For debugging a
// silent link from the toplevel, where the alternative is a rebuild and a
// card swap per question. Read-only, and refuses an offset outside the
// register window or one that is not word aligned.
bool rpi4_genet_peek(unsigned offset, uint32_t *value);
