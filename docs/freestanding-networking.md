# Proposal: networking for freestanding Trealla

*Status: all three layers exist and have run on a Pi 4.* `library(tftp)` is a working client and server
and the readings pattern below runs on any hosted build; the `netif` contract
and the IPv4/UDP stack are in `src/net/`, tested against a netif that is not a
device, in `make test`. Layer 1, the GENET driver, is `ports/rpi4/genet.c`: on
a board cabled to a Mac it answers ARP and ping and carries UDP both ways, and
`ports/rpi4/readings.pl` serves readings from it to the Mac's `tftp` client.
The driver remains the one part with no automated test.

Give a freestanding image a small IPv4/UDP stack that is independent of any
device, a driver underneath it, and enough of a socket surface that TFTP can be
written in Prolog rather than C. The Raspberry Pi 4 is the first board and its
GENET the first driver, but only the driver is board-specific: the stack is
meant to serve other devices on the Pi and other boards entirely.

The point is a network stack driven from Prolog on the bare metal. It is not
the cheap way to get images onto the board - the firmware already does TFTP
netboot with no code at all, and `docs/` covers serial chain-loading. Anyone
reaching for this to speed up iteration should use one of those instead.

## Shape

Three layers, deliberately handled three different ways:

| Layer | Approach | Why |
| --- | --- | --- |
| GENET driver | **Crib** an existing BSD-licensed driver | Undocumented hardware; all the risk lives here |
| ARP/IPv4/ICMP/UDP | **Hand-roll**, ~600 lines | Textbook, and smaller than integrating a stack |
| TFTP | **Prolog**, over new builtins | The reason for doing any of this |

The asymmetry is the proposal. Cribbing the driver removes the part that is
genuinely hard to get right without documentation; hand-rolling the stack
avoids vendoring forty thousand lines to use five percent of them.

## Layer 1: the GENET driver

The Pi 4's Ethernet is a Broadcom GENET v5 MAC at `0xFD580000` - memory-mapped
with DMA rings, not the USB-attached part the Pi 3 had. That is the single
biggest thing in this proposal's favour: it is an ordinary DMA device, and no
USB stack is needed.

There is **no public documentation for GENET on the BCM2711**. It is not in the
peripherals datasheet. The driver gets written by reading other people's.

**Crib from:** FreeBSD's `sys/arm64/broadcom/genet/if_genet.c`, a working Pi 4
driver under a BSD licence - permissive enough to adapt into this MIT tree with
attribution. Verify the exact terms in the file header before copying anything.

**Read but do not copy** (all GPL): u-boot's `drivers/net/bcmgenet.c` has the
clearest minimal structure - polled, one TX ring, one RX ring, which is exactly
the shape wanted here; Circle (`rsta2/circle`) proves a GENET driver works in a
bare-metal Pi environment; Linux's `bcmgenet.c` is the authority on the magic
constants and unreadable for anything else.

Two landmines others have already hit and documented on the Raspberry Pi
forums:

- **Link-change interrupts do not work.** Link state has to be found by polling
  the PHY registers. No loss here - this port takes no interrupts anyway.
- **The Pi 4 requires the smaller DMA max burst size, `0x8`.** The kind of
  undocumented constant that costs days.

Three more turned up on the board, each enough on its own to stop every frame
while the link reads up: the port mode, the destination filter and the
transmit clock delay. `ports/rpi4/README.md` has the details.

The MAC address lives in OTP and is read over the VideoCore mailbox, which
`ports/rpi4/mailbox.c` now implements. A locally administered address remains
compiled in as the fallback for a board whose GPU does not answer.

### The main risk: DMA cache coherency

`ports/rpi4/mmu.c` currently maps **all** RAM as Normal write-back,
inner-shareable, cacheable. GENET's DMA is not coherent with the ARM caches, so
descriptor rings and packet buffers must either live in non-cacheable memory or
have explicit `DC CIVAC`/`DC IVAC` maintenance around every packet.

The proposed change is the former: add a Normal Non-Cacheable attribute to
`MAIR_EL1`, carve a region out of the level-2 table, and allocate rings and
buffers from it. Perhaps forty lines on top of what is there.

This is called out first because its failure mode is the worst kind. It does
not fail cleanly - it *mostly works*, until a descriptor write is still sitting
in a dirty cache line or a receive returns the previous packet's contents.
Anyone debugging intermittent packet loss here should suspect the mapping
before the driver.

## Layer 2: the stack

Scope, and nothing beyond it:

- Ethernet II framing
- ARP, with a small cache and ageing
- IPv4, no fragmentation - 512-byte TFTP blocks make ~558-byte frames, well
  under MTU, so it never arises
- ICMP echo, for the sake of being able to ping the board
- UDP; checksums are optional over IPv4 and can be zero until everything works

No TCP, no DHCP, no DNS, no routing beyond "same subnet, or send it to the
gateway". Static address configuration to begin with.

Around 600 lines. One thing that does *not* need fighting: the IP header lands
at offset 14 in the frame, so its 32-bit fields are unaligned - and the port
already runs with `SCTLR_EL1.A` clear over Normal memory, so unaligned loads
work.

### Why not lwIP

lwIP is BSD-licensed, `NO_SYS=1` is built for exactly this kind of bare-metal
main loop, and its `sys_now()` hook maps straight onto
`tpl_platform_monotonic_usec()`. It is a good stack.

It is still the wrong choice here. The integration surface - a `lwipopts.h`
with hundreds of knobs, `pbuf` pools wanting to be wired to `pl_set_allocator`
or a static arena, TCP and DHCP and DNS to be configured off - is likely more
work than writing the five headers this needs, and it puts forty thousand lines
in the tree to use a small fraction of them.

The exception is TCP. If TCP is ever wanted, adopt lwIP at that point rather
than growing a hand-rolled stack into one; those are different projects.

Either way, the structure and constants are worth reading from lwIP and uIP -
checksum routines, ARP cache ageing, header layouts - while writing our own.

### Device independence is a design constraint

The stack must not know what kind of device is underneath it. Only the driver
is board-specific; ARP and IPv4 know nothing about a MAC. That is not a
nice-to-have, and the shape below is a requirement on the design rather than an
outcome to hope for:

```c
typedef struct netif_ {
    const char *name;                      // "genet0"
    uint8_t mac[6];
    void *device;                          // the driver's own state

    bool (*init)(struct netif_ *nif);
    bool (*link_up)(struct netif_ *nif);
    bool (*send)(struct netif_ *nif, const void *frame, size_t len);
    size_t (*poll)(struct netif_ *nif, void *frame, size_t maxlen);
} netif;
```

Five entry points, all polled, no interrupts, no callbacks upwards. A driver
fills one of these in and the stack drives it; a second driver on the same
board, or the same stack on an entirely different board, is another
implementation of the same five functions.

**Buffers are copied at the boundary, deliberately.** `send` takes a frame the
stack owns and `poll` fills one the stack owns; the driver's DMA-visible rings
stay private to the driver. A zero-copy interface would be faster and would
also drag the cache-coherency problem out of the driver and into the stack -
every driver would then need the stack's buffers to come from non-cacheable
memory, and the device independence would be a fiction. At 512-byte TFTP
blocks the copy costs nothing worth measuring. Keeping the hardest,
most device-specific problem entirely behind the interface is the point.

The layout follows from this:

| Code | Home | Tied to |
| --- | --- | --- |
| ARP, IPv4, ICMP, UDP, the `netif` contract | `src/net/` | nothing |
| The UDP builtins | `src/net/` | nothing |
| GENET driver | `ports/rpi4/genet.c` | the BCM2711 |

That mirrors `src/bif_gpio_linux.c` versus `ports/rpi4/bif_gpio.c`: the thing
tied to an ABI or to nothing lives in `src/`, and only the thing tied to a
board lives under `ports/`.

### The builtin seam is ready for this

`g_port_bif_tables` is a NULL-terminated array of tables rather than one
table, precisely so a board can expose GPIO and networking at once without
either knowing about the other. A port lists what it offers in its own
manifest - `ports/rpi4/port_bifs.c` - so adding the UDP builtins is a line
there and a new table, with no change to the GPIO file or to `src/`.

## Layer 3: TFTP in Prolog

RFC 1350 is a toy protocol and that is the appeal: five packet types (RRQ, WRQ,
DATA, ACK, ERROR), 512-byte blocks, lockstep acknowledgement, and a short block
means end of file. Lockstep request/response with timeouts and retransmission
is comfortable ground for Prolog, and `delay_ms/1` already exists for the
retransmit timer.

What was built is two layers rather than new socket builtins. The stack offers
four, in `src/net/bif_net_stack.c`, where a socket is simply its port:

```
net_udp_open(+Port, -Socket)
net_udp_send(+SrcPort, +Host, +DstPort, +Bytes)
net_udp_recv(+Port, -Host, -FromPort, -Bytes, +TimeoutMs)
net_udp_close(+Port)
```

`library/freestanding/socket.pl` builds the UDP subset of `library(socket)` on
them - `udp_socket/1`, `tcp_bind/2`, `udp_send/4`, `udp_receive/4`,
`tcp_close_socket/1`, with the hosted meanings - and a network image embeds it
under the name `socket`, together with `library(tftp)`. So the TFTP code that
runs on the board is the same file the hosted build runs, not a port of it.
The `net_` prefix is what makes that possible: the stack's builtins were first
called `udp_*`, and `udp_send/4` then meant two different things.

Payloads are lists of byte values, which is what `library(tftp)` deals in.
Strings were proposed here to save cells, but a 512-byte block is small enough
that matching the hosted library mattered more.

**Build trap:** these go in the port table, *not* `src/network.c`. A
freestanding build sets `NONETWORK=1` and links `network_none.c`, so anything
added to `network.c` needs a matching stub there or the freestanding build
breaks.

## What this is actually for: readings, not files

The endpoint worth aiming at is not a board that can fetch files. It is a board
that answers questions, over a protocol every machine already speaks.

A TFTP read request carries an arbitrary string. Nothing says it names a file,
so a server can synthesise the reply - which is precisely what procfs does, and
what Plan 9 built an operating system on. `library(tftp)`'s `tftp_serve/3`
takes a `virtual(:Closure)` option that does this, and the namespace is just a
predicate, so adding a reading is adding a clause:

```prolog
reading('sensors/temp0', Codes) :-
    read_adc(0, Celsius),
    term_codes(reading(temp0, Celsius, celsius), Codes).
```

Any client can then ask, with nothing installed:

```
$ tftp raspberrypi 69
tftp> get sensors/temp0
$ cat temp0
reading(temp0,21.44,celsius).
```

One Prolog term per reading means a Prolog client gets structure back from
`read_term/2` while `tftp` and `cat` still work for everyone else. A
`status/index` reading that lists the others makes the namespace
self-describing.

Three properties are what make it sound, and two of them are structural rather
than matters of discipline:

- **Sampled once per transfer.** The closure is called when the request
  arrives and those bytes serve every block and every retransmission. Nothing
  re-samples mid-transfer, so a client that lost an ACK cannot splice two
  readings together.
- **Read-only.** A write to a virtual name is refused. A reading is something
  to look at; making it settable turns this into unauthenticated control of
  hardware.
- **Matched before the filesystem.** Virtual names never reach the path logic,
  so they may contain `/` and build a hierarchy with no traversal risk. The
  order matters in the other direction too: an unrecognised hierarchical name
  on a server that has readings answers "no such reading" rather than falling
  through to the traversal guard and claiming "access violation", which was
  both untruthful and a hint to anyone probing.

The honest limits:

- **TFTP has no authentication and no encryption.** Anyone who can send a
  datagram can read every reading. For instrumentation on a lab network that
  is a fair trade; it is not a control plane, and the read-only rule above is
  what keeps the distinction from eroding.
- **Roughly two round trips per query**, so this is polling at human or
  second-scale rates. Not a kilohertz telemetry path.
- **Do not stream.** TFTP ends a transfer on a short block; a continuous feed
  fights the protocol rather than using it.

The reason this section matters to the rest of the document: unlike the driver,
all of it is testable today, hosted, with no board involved at all. It is the
part of the plan that is already done.

## Milestones

Each is independently verifiable, which matters because the feedback loop is
poor.

0. Debug loop first: second machine, direct cable or dumb switch, Wireshark
   running. Do not start without this.
0b. ~~The stack against a canned-frame netif, hosted, in `make test`.~~ Done:
   `tests/net/net_test.c`, 59 checks, run by `make net-test`.
1. ~~Driver brings the link up; PHY status polled and reported over the UART.~~
   Done.
2. ~~**Transmit only.**~~ Skipped: register reads from the toplevel
   (`genet_reg/2`) did the diagnostic job this was meant to.
3. ~~Receive: ARP request answered.~~ Done.
4. ~~ICMP echo - the board answers `ping`.~~ Done.
5. ~~UDP echo, in C.~~ Skipped in favour of 6.
6. ~~The three UDP builtins, exercised from Prolog.~~ Done, to and from a Mac.
7. ~~TFTP client in Prolog, fetching a file from the other machine.~~ Done the
   other way round: the board serves readings and the Mac's `tftp` fetches
   them, a three-block transfer included. The client half is the same library
   but has not been run on a board.

## Risks

| Risk | Severity | Mitigation |
| --- | --- | --- |
| DMA cache coherency | High - fails intermittently, not cleanly | Non-cacheable region up front; suspect it first |
| PHY bring-up | High - most of the schedule variance | FreeBSD driver as reference |
| No GENET documentation | Medium | Three independent drivers to read |
| Licence terms on the cribbed driver | Low, but blocking | Check the file header before copying |
| No QEMU coverage for the driver | Medium | Everything above the driver is testable hosted - see below |

### What can and cannot be tested

Everything in the Pi 4 port so far has been verifiable under QEMU's `raspi4b`
machine, and CI boots it on every push. **QEMU does not emulate GENET** - it is
explicitly listed among that machine's missing devices, along with PWM and the
PCIe root port.

Device independence is what limits the damage. With the `netif` contract above,
the stack can be driven by a netif that is not a device at all:

- a **loopback netif**, which hands back whatever it is given, exercises ARP,
  IPv4, ICMP and UDP end to end with no hardware;
- a **canned-frame netif**, fed captured or hand-built frames, covers malformed
  input, wrong checksums, truncated headers and the ARP cache;
- both run in an ordinary **hosted** build, so they belong in `make test` and
  run in CI on every platform, not just under emulation.

That leaves genuinely untestable only the driver itself - the one layer where
that is unavoidable, and the one this proposal cribs rather than invents. A
stack written against a board-specific driver interface would have surrendered
all of it.

The upshot for the milestones below: layers 2 and 3 can be developed and
regression-tested before the board is even on the desk.

## Vendoring hygiene

Follow the `src/imath/` pattern: its own directory, its own `LICENSE`,
attribution recorded, objects added to the freestanding object list. Check
whether anything vendored needs a `network_none.c`-style stub so that a
freestanding build without networking still links.

## Effort

Two to three weekends, with nearly all the variance in PHY bring-up and cache
attributes. First ARP frame on the wire within an evening or two if the PHY
cooperates, several more if it does not. The stack is one or two evenings and
mechanical. The Prolog TFTP client is an evening, and is the fun part.

## Decisions wanted before starting

1. Adapt the FreeBSD driver in place, or vendor it under its own directory?
2. ~~Where does the stack live?~~ Settled: `src/net/`, behind the `netif`
   contract, with only the driver under `ports/rpi4/`.
3. Static IP configuration only, or DHCP in scope? (DHCP is perhaps another
   150 lines and can wait.)
4. Is leaving CI coverage behind acceptable for this subsystem?
