# Raspberry Pi 4 adapter

This is the third freestanding target and the first that boots a 64-bit
application processor with no operating system underneath it. It targets the
Pi 4's BCM2711 (four Cortex-A72 cores, low-peripheral mode) and uses the Arm
GNU bare-metal toolchain and newlib for the small C-runtime layer.

Unlike the RV32 and ESP32-S3 targets, nothing here is memory constrained: the
smallest Pi 4 has 1 GB and Trealla's live-heap peak is under 6 MB. What the
target does need, and what the two earlier ports got from someone else, is
AArch64 startup — so `boot.S` and `mmu.c` are the substance of this adapter and
`platform.c` is the small part.

## Building

Install the Arm GNU toolchain for `aarch64-none-elf` (on macOS,
`brew install --cask gcc-aarch64-embedded`, or unpack the tarball from
developer.arm.com), put its `bin` on `PATH`, then:

```
make rpi4
```

That produces `ports/rpi4/trealla.elf` and the flashable
`ports/rpi4/kernel8.img`. Override `RPI4_CC`, `RPI4_AR`, `RPI4_OBJCOPY` and
`RPI4_SIZE` if the toolchain is not on `PATH` under its usual names.

To boot the same image under QEMU and check it against the acceptance markers:

```
make rpi4-smoke
```

That needs a QEMU with the `raspi4b` machine, which arrived in QEMU 9.0 — the
target checks and says so rather than failing obscurely. Ubuntu 24.04 still
ships 8.2, which is why the CI job runs in a Debian trixie container.

## Booting your own Prolog program

`make rpi4` builds the acceptance harness. To build a kernel that boots
straight into a program of your own - the bare-metal counterpart of
`make compile main=...` in the top-level README:

```
make rpi4-app main=ports/rpi4/hello.pl
```

The program is converted to bytes on the build host and consulted from memory
at boot, so its `:- initialization(main).` runs at the end of the load, exactly
as in a hosted standalone build. There is no filesystem and nothing to install
beside the image.

`samples/freestanding_app.c` is the entry point that does this, selected
through `FREESTANDING_MAIN`; `samples/freestanding.c` remains the acceptance
harness with its fixed queries. The application needs no `halt`: the C entry
point halts the board when the load finishes, using `halt/1`'s status if the
program supplied one, and 1 if the load failed or raised.

Anything the program needs beyond the core builtins has to be embedded too,
with `EMBED_LIBS`, since there is nowhere to load a library from at run time.

Note that a freestanding build has **almost no time predicates**: `g_os_bifs`
in `src/bif_os_none.c` holds only `sleep/1` and `delay_ms/1`, so `get_time/1`
and `cpu_time/1` are absent even though the platform contract supplies a
monotonic clock. `get_time/1` would be the wrong thing to add without more
thought - a Pi 4 has no battery-backed clock, so it could only report uptime
while claiming to be wall time.

Both waits behave the same way. Inside a task the delay goes to the
scheduler, so sibling tasks run while this one waits. Either way the port is
asked to idle through `tpl_platform_idle_until()`, the one optional service in
the platform contract. This port uses it to service the network, not to sleep:
the core still spins. Sleeping needs the generic timer programmed and
interrupts routed, neither of which exists here: `boot.S` sets `VBAR_EL1` for
faults, but nothing takes an IRQ.

## Running on hardware

Copy the image to the boot partition of an otherwise ordinary Raspberry Pi OS
card - under a name of its own, because 64-bit Raspberry Pi OS *is*
`kernel8.img` and overwriting that leaves the card unable to boot the OS
again:

```
cp ports/rpi4/kernel8.img /Volumes/bootfs/trealla8.img
```

Then append to `config.txt`:

```
[all]
enable_uart=1

# --- uncomment these three to boot Trealla bare metal ---
#kernel=trealla8.img
#arm_64bit=1
#device_tree_address=0x20000000
```

Commented, the card boots Raspberry Pi OS exactly as before; uncommented, it
boots this port. Three `#` characters are the whole difference and the OS
install is never touched.

`device_tree_address` matters: the port ignores the device tree, but the
linker script hands everything from the end of BSS up to 0x20000000 to the
Trealla heap, and firmware left to itself may place the tree inside that
range. `enable_uart=1` pins the UART clock the baud divisors assume.

### The serial cable

The console is PL011 UART0 on GPIO14/15 at 115200 8N1: three wires to a
USB-TTL adapter, and the fourth - the adapter's +5V - left disconnected. The
Pi runs from its own supply, and joining the two back-feeds one into the
other.

| Adapter | Pi 4 header pin |
| --- | --- |
| GND | 6 |
| RX | 8 (GPIO14, TXD0) |
| TX | 10 (GPIO15, RXD0) |

They cross, because the Pi transmits on pin 8 and the adapter has to receive
there. Getting that backwards produces silence rather than an error, and is
the first thing to suspect when a board seems dead. The adapter must be 3.3V:
the Pi's GPIO is not 5V tolerant.

With a monitor on HDMI0 - the micro-HDMI nearest the USB-C socket - the same
output appears on screen, so a board with no adapter is not mute.

## What the port owns

| File | Role |
| --- | --- |
| `boot.S` | Parks cores 1-3, drops EL3/EL2 to EL1, enables FP/SIMD, sets the stack, zeroes BSS |
| `mmu.c` | Identity-maps RAM as Normal write-back and the peripheral window as Device, then enables the MMU and caches |
| `platform.c` | The five platform services: PL011 console, generic-timer clock, halt, panic |
| `bif_gpio.c` | The board builtins - GPIO and `delay_ms/1` |
| `port_bifs.c` | The manifest: which tables this port hands the engine |
| `bcm2711.h` | Register map shared by the adapter and the builtins |
| `fault.c` | Reports an exception - class, ESR, FAR, ELR - and halts |
| `mailbox.c` | VideoCore property mailbox — asks the GPU about the board |
| `fb.c` | HDMI text console over the VideoCore framebuffer |
| `font8x8.c` | The console font, generated from `util/mkfont.py` |
| `board.c` | Device bring-up between the MMU and `main` |
| `genet.c` | GENET Ethernet driver, as a `netif` (opt-in, see below) |
| `bif_genet.c` | Register and PHY builtins for debugging it from the toplevel |
| `syscalls.c` | Newlib's bottom half — console `_read`/`_write` and a bump `_sbrk` over the linker-defined heap |
| `rpi4.ld` | Image at 0x80000, 8 MiB stack, heap up to 0x20000000 |

The MMU is not an optimisation. With it off, every access is Device-nGnRnE:
unaligned accesses fault whatever `SCTLR_EL1.A` says, and nothing is cached.

`halt` parks the core in `wfe` after draining the UART. The smoke build adds
`-DRPI4_SEMIHOSTING=1`, which exits through a semihosting call first so QEMU
can report the status the way the RV32 target's test finisher does; on hardware
with no debugger attached that call is not serviced, which is why it is not in
the image you flash.

A Pi 4 has no battery-backed clock. `_gettimeofday` therefore reports time
since boot, taken from the same monotonic counter as the platform clock — it is
honestly monotonic and honestly not wall time.

## Board builtins

The port exposes the BCM2711 GPIO block and a timing primitive to Prolog
through `g_port_bif_tables`, the array of builtin tables a freestanding port
may supply (`PORT_BIFS_OBJECT`, defaulting to the empty
`src/port_bifs_none.c`). The
engine has no board knowledge: it walks one more table, and the Makefile
decides who fills it.

| Predicate | Meaning |
| --- | --- |
| `gpio_mode(+Pin, +Mode)` | `input`, `output`, or `alt0`-`alt5` |
| `gpio_pull(+Pin, +Pull)` | `none`, `up`, `down` |
| `gpio_read(+Pin, ?Level)` | reads the pin level as 0 or 1 |
| `gpio_write(+Pin, +Level)` | drives an output to 0 or 1 |

Pacing used to live in this table as `delay_ms/1`. It is in
`src/bif_os_none.c` now, beside `sleep/1`, because waiting is not board
knowledge - every freestanding target gets both. `delay_ms/1` is the same
wait in the unit a pin is naturally timed in, taking an integer rather than a
float. Measured under QEMU at 1.51 s for `delay_ms(1500)` against a 0.33 s
no-delay control.

A hosted Linux build offers the same predicates over the GPIO character
device (`make LINUX_GPIO=1`), so the same Prolog runs either way - see
[docs/gpio.md](../../docs/gpio.md) for where the two differ.

### Drawing

The second table is the framebuffer, for a board with a monitor rather than a
serial line. A colour is `0xRRGGBB`; coordinates are pixels from the top left.

| Predicate | Meaning |
| --- | --- |
| `fb_size(?Width, ?Height)` | the screen, in pixels |
| `fb_clear(+Colour)` | fills the screen and sends the console cursor home |
| `fb_pixel(+X, +Y, +Colour)` | one pixel |
| `fb_rect(+X, +Y, +W, +H, +Colour)` | a filled rectangle |
| `fb_text(+X, +Y, +Atom, +Colour)` | text at a pixel position, ink only |

Off-screen is clipped rather than refused - drawing partly over an edge is
ordinary, where a negative coordinate is a mistake and raises
`domain_error(fb_coord, N)`. With no framebuffer, every one of them raises
`existence_error(framebuffer)`, which on a board with nothing plugged into
HDMI is what you get.

There is no frame model: each call writes and cleans its own cache lines, so
nothing tears within a call and everything tears between them. `fb_text/4`
draws only the ink, leaving the background, and uses the console's own 8x8
font - there is no font or size to choose.

**The console shares this screen.** `write/1` still goes to both the screen
and the serial line, and it owns a cursor that wraps and scrolls. Scrolling
copies the full width upward, so console output reaching the bottom row drags
anything drawn up with it. A program that draws should keep its own output on
the serial line, or expect its picture to crawl.

`ports/rpi4/blink.pl` is the worked example, and the one to reach for with a
board on the bench:

```
make rpi4-app main=ports/rpi4/blink.pl
```

It drives GPIO21 - physical pin 40 - high and low every two seconds, slowly
enough to read on a multimeter between pin 40 and a ground pin. Note that the
acceptance program's GPIO probe pulses the same pin for microseconds and then
halts, so it is deliberately not something a meter can catch; this is.

Pins are 0-57; anything else is a `domain_error(gpio_pin, N)`. GPIO14 and
GPIO15 carry the console, so they can be read but not reconfigured -
`gpio_mode/2` and friends raise `permission_error(modify, gpio_pin, N)` rather
than let a typo silence the board's only output, panic path included.

Two details in `bif_gpio.c` are worth knowing before editing it:

- The BCM2711 pull encoding is `01` = up, `10` = down. That is the reverse of
  the BCM2835 `GPPUD` encoding that most Pi 1-3 example code uses, and getting
  it backwards fails silently until an input floats.
- Output uses `GPSET`/`GPCLR`, which are write-1-to-act, so driving one pin
  needs no read-modify-write and cannot disturb its neighbours. Function
  select and pull are read-modify-write, which is safe here only because a
  freestanding build has no threads and this port takes no interrupts.

## The VideoCore mailbox

The GPU, not the ARM, owns the clocks, the display and the board's identity.
`mailbox.c` asks it: the ARM writes the bus address of a message buffer into a
hardware mailbox, the GPU fills the buffer in place and posts the address
back. Callers pass a tag list and get their answers in the same array.

Two details are easy to get wrong. Mailbox 0 carries replies to the ARM and
mailbox 1 carries requests to the GPU, so the "is there room to write" status
is the far register at `+0x38`, not the `+0x18` most examples reach for. And
the address handed over is a VideoCore bus address, `0xc0000000 | pa`, the
alias that bypasses the GPU's L2 cache; the buffer is taken from the
non-cacheable DMA window `mmu.c` maps, so neither side has to flush anything.

Every wait is bounded at 100ms. A GPU that stops answering must not take the
board down with it silently — the boot line becomes `TREALLA MAILBOX FAILED`
and the engine starts anyway.

## The console on HDMI

`fb.c` asks the mailbox for a framebuffer and turns it into a text console, so
everything written to the serial line also appears on a monitor. That is not a
luxury: until a USB-serial adapter is plugged into GPIO14/15 the board has no
output at all, and an HDMI cable is a good deal easier to come by.

There is no display driver here. The GPU owns HDMI, the modes and the timing;
we ask for a resolution and get back a pointer and a pitch, and everything
after that is writing pixels.

The resolution is deliberately low - 800x600, `RPI4_FB_WIDTH`/`_HEIGHT` to
change it. The GPU scales whatever it is given up to the panel, so a small
framebuffer is not a small picture on a television, it is a large font.
`RPI4_FB_SCALE` draws each glyph pixel as a square block if that is still not
enough. At the default that is 100 columns by 75 rows.

The framebuffer stays mapped Normal write-back like the rest of RAM, and
`fb.c` cleans the lines it draws with `dc cvac`. The alternative - mapping it
non-cacheable, as the DMA window is - would make scrolling, which copies
megabytes at a time, crawl.

Serial input is echoed here and nowhere else, a carriage return is turned
into a line feed on the way in, and Backspace (either byte a terminal might
send for it, BS or DEL) deletes. All three are what a terminal driver would
do and there isn't one: without the echo a typist sees nothing at all,
without the translation a term typed with a full stop and Return never
terminates because Return arrives as CR and the reader wants LF, and without
deletion a typo has to be shipped to the parser as a literal character it
does not understand.

Deletion is the reason a whole line is assembled in `platform.c` before any
of it reaches libc: once a byte has been handed to `_read()` there is no
taking it back, so Backspace can only erase one still held back, corrected
as typed, and released at Return. That is not the general platform
contract - see `docs/freestanding-porting.md` - which only asks for one
byte as soon as it is ready. The difference is bounded by the same thing
either way, a person's own typing, so it does not risk the wedge the
contract is written against; a port willing to give up mid-line editing can
still take the contract at its word.

There is still no line editing beyond that - no cursor movement, no history,
nothing to edit a character that already scrolled off with the next
keystroke.

The font is 95 hand-drawn glyphs in an 8x8 cell, five columns wide with a
descender row. It is generated: edit the art in `util/mkfont.py`, run it, and
`ports/rpi4/font8x8.c` is rewritten. `python3 util/mkfont.py --show 'some text'`
proofs a change without booting anything.

## Networking

The BCM2711's Gigabit Ethernet is driven by `genet.c`, which presents the
`netif` contract that `src/net`'s IPv4/UDP stack sits on. It is **opt-in**:

```
make rpi4 RPI4_NET=1
```

The default image contains no GENET code at all, and deliberately so.
**QEMU's raspi4b has no GENET**, and the driver's first register write aborts
there:

```
TREALLA EXCEPTION entry=0x4 esr=0x96000010 (data abort) far=0x00000000fd580008
```

Since QEMU is what CI boots, the acceptance image must not contain the driver.
That also means the driver is the one part of this port with no automated test
whatsoever - it can only be exercised on a board.

Addressing is compile-time, there being no DHCP: `RPI4_IP`, `RPI4_NETMASK`,
`RPI4_GATEWAY` and `RPI4_MAC` override the defaults (192.168.50.2/24 via
.50.1). The MAC is only a fallback: the board's real address lives in OTP, and
`board.c` asks the VideoCore mailbox for it first.

Packet buffers come from the non-cacheable window `mmu.c` maps at
`RPI4_DMA_BASE`; the descriptors need no such care because GENET keeps them in
its own register window rather than in memory.

The stack is polled. `net_udp_recv/5` and `net_udp_send/4` poll it, and so does the
board whenever it is idle: while the toplevel waits for a key, and while a
program sleeps or its tasks wait on a timer. So a board answers ARP and ping,
and queues datagrams for an open port, whatever the program is doing - except
computing. A long computation leaves frames in the receive ring, and once that
fills the MAC sends pause frames until something drains it.

A network image also carries four builtins for debugging the controller from
the toplevel, driven over serial with `util/rpi4_repl.py`:

| Builtin | |
| --- | --- |
| `genet_reg(+Offset, ?Value)` | read a controller register |
| `genet_reg_set(+Offset, +Value)` | write one |
| `genet_mdio(+Reg, ?Value)` | read a PHY register |
| `genet_mdio_set(+Reg, +Value)` | write one |

Offsets are from the controller base and must be word aligned. Writes can stop
the link until the next boot, which is the point: a setting can be tried
without a rebuild and a card swap.

A network image embeds `library(tftp)` too, over the UDP subset of
`library(socket)` that `library/freestanding/socket.pl` builds on the stack's
`net_udp_*` builtins. It is the same `library(tftp)` a hosted build runs.
`ports/rpi4/readings.pl` is a whole application on top of it - a board that
answers questions over TFTP, each reading a Prolog term:

```
make rpi4-app main=ports/rpi4/readings.pl RPI4_NET=1
```

```
$ tftp 192.168.50.2
tftp> get status/index
```

## Faults

`fault.c` and the vector table in `boot.S` turn a fault into a message:

```
TREALLA EXCEPTION entry=<vector> esr=<ESR_EL1> (data abort) far=<address> elr=<pc>
```

Before that existed, `VBAR_EL1` was never set, so any fault - a stray pointer,
an alignment error, a device that is not fitted - jumped to an undefined
vector and the board simply stopped, with no output and no clue. On hardware
with no debugger attached that is the difference between a five-minute fix and
an afternoon.

## Acceptance

The smoke runner requires these markers, in order, under a timeout:

```
TREALLA MAILBOX OK ram=<size>MiB
TREALLA FRAMEBUFFER OK
TREALLA FREESTANDING BOOT
TREALLA PROLOG OK
TREALLA GPIO OK
TREALLA FB OK
TREALLA ALLOCATION FAILURE CONTROLLED
TREALLA HEAP PEAK <bytes>
TREALLA FREESTANDING COMPLETE
```

The first line comes from `rpi4_board_init()`, before `main()`, and is the
port's earliest sign of life: it means the MMU is on, the console works and
the GPU is answering. QEMU's `raspi4b` reports 960MiB, being a 1GiB machine
less the GPU's split.

`make rpi4-screen` goes further and checks the *screen* rather than the serial
line. It boots the image QEMU can screenshot - the one without semihosting,
which parks rather than exiting - takes a screendump over QMP, and reads the
console back out of the pixels by matching every 8x8 cell against the font
that drew it. Seeing `TREALLA FRAMEBUFFER OK` on serial only proves the GPU
answered the mailbox; reading it off the screen proves the pitch, the drawing
and the scan-out. What it cannot prove is the cache maintenance, because QEMU
has no caches to be wrong about.

`ports/rpi4/program.pl` supplies that extra marker. Its GPIO checks assert the
argument and permission errors, which are board-independent and therefore
QEMU-provable; the level read back from an unwired pin is only required to be
0 or 1, because under emulation it means nothing.

The measured AArch64 figures (Arm GNU Toolchain 15.2.rel1, newlib) are:

| Metric | Measured bytes | CI limit |
| --- | ---: | ---: |
| ELF text | 1,496,520 | 1,750,000 |
| ELF data | 103,064 | 500,000 |
| ELF bss | 17,176 | 900,000 |
| Peak Trealla-owned heap | 2,482,530 | 2,800,000 |

Data and bss are far below their limits because two changes removed what a
build without FFI was carrying for it: `src/bif_ffi_none.c` stopped reserving
`g_ffi_bifs[MAX_FFI]` (679KB of bss), and the FFI argument arrays left
`struct builtins_` where `USE_FFI` is off, taking the builtin tables from
341KB to 52KB.

The heap figure was 5,802,432 until the engine stopped reserving fixed-size
structures it rarely used: two `MAX_TABS` arrays and 1024 stream structs in
`struct prolog`, an 8KB ignore set and 8KB of findall queues in every query,
and a 37KB `vartab` in every parser. `pl_create()` allocates about 8KB in a
freestanding build now, against 2.8MB before. Its limit is the one that has
been tightened onto the new figure, because a 7MB ceiling would no longer
notice all of that coming back.

Text and data run larger than the RV32 baseline and BSS runs smaller, which is
what 64-bit pointers and a different libc do to the same engine. As with the
other targets, a change that intentionally exceeds a limit must move the
baseline and the limit together, with a reason.

## What has run on hardware

CI boots the image under QEMU's `raspi4b` machine on every push, and that is
what the markers above check. It has also been run on a physical Raspberry Pi
4 Model B Rev 1.2, booting from an SD card by the recipe above, which
confirmed the parts emulation cannot:

- the `config.txt` contract, the 0x80000 load address and the boot to EL1;
- PL011 at 115200 8N1 in both directions, with the baud divisors as written;
- the GPIO alt-function setup, since the console pins are configured by it;
- the VideoCore mailbox, which reports 948MiB of ARM memory on a 4GB board -
  the low window less the GPU split, not the total;
- the framebuffer: allocation, the pitch, drawing and scan-out to a monitor,
  and with it the `dc cvac` cache maintenance, which QEMU has no caches to be
  wrong about;
- the engine itself, with a heap peak within 24 bytes of the emulated figure;
- the pixel order, by drawing in colour: the console is greyscale by design,
  so a red/blue swap could never have shown up in it.

A network image, cabled straight to a Mac, has since confirmed GENET:

- the PHY, a BCM54213PE answering at MDIO address 1, and the board's own MAC
  address read from OTP;
- gigabit autonegotiation, with the MAC told the speed the PHY settled on;
- ARP and ICMP echo, answered while a program waited in `net_udp_recv/5`;
- UDP in both directions, received with `net_udp_recv/5` and sent with
  `net_udp_send/4`;
- TFTP, with `readings.pl` serving the Mac's own `tftp` client: every
  reading, one of 1500 bytes spanning three blocks, and the refusals for an
  unknown name and for a write.

Three settings no emulator could have caught were each enough on their own to
stop every frame. The port mode has to select the external PHY, whose reset
value selects an internal one the BCM2711 does not have. The destination filter
has to be programmed before it accepts anything, broadcast included. And the
MAC must add no transmit clock delay, because the PHY adds both delays out of
reset.
