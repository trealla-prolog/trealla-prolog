# Proposal: a Raspberry Pi 5 freestanding port

*Status: research only. Nothing here has run on a Pi 5, and there is no Pi 5 on
the desk yet.* Every claim below is tagged with where it came from, because the
evidence is uneven and the port should be planned around that:

| Tag | Meaning |
| --- | --- |
| **datasheet** | Read directly from Raspberry Pi's RP1 peripherals datasheet (2023 draft) |
| **docs** | Raspberry Pi's official `config.txt` documentation |
| **dts** | Raspberry Pi's Linux device tree sources (`rpi-6.6.y`) |
| **third-party** | One bare-metal project or forum post; plausible, not confirmed |
| **unknown** | No source found; has to be learned on the board |

The Pi 4 port ([`ports/rpi4/`](../ports/rpi4/README.md)) is the baseline
throughout. The short version: the engine, the platform contract and the whole
network stack above the driver carry over untouched; the address map is a
smaller change than first thought; and every peripheral the Pi 4 port drives
has to be rewritten, because on the Pi 5 they have moved to a different chip.

## What changed in the hardware

The Pi 5's BCM2712 has four Cortex-A76 cores and the GPU, but not the I/O. GPIO,
the header UARTs, Ethernet, USB, SPI, I2C, PWM and the camera and display
ports are all on **RP1**, a separate southbridge reached over PCIe 2.0 x4
(**datasheet**). Nothing is memory-mapped in the SoC the way the BCM2711's
peripheral window was. The controllers themselves are ordinary, documented IP:

| Function | Pi 4 (BCM2711) | Pi 5 (via RP1) |
| --- | --- | --- |
| Console UART | PL011 in the SoC | PL011 r1p5 in RP1 - a plain `uart0`, the same IP |
| GPIO | `GPFSEL`, `GPSET`, `GPPUPPDN` | `IO_BANK0` per-pin CTRL, `SYS_RIO` set/clear, `PADS_BANK0` |
| Ethernet | Broadcom GENET, undocumented | Cadence GEM_GXL, documented, same BCM54213PE PHY |
| Framebuffer | VideoCore mailbox tags | Mailbox still answers, but see below |
| Interrupts | GICv2 | GICv2 (unused here anyway) |

## Boot

- The firmware loads `kernel_2712.img` if present, else `kernel8.img`
  (**docs**). The former exists for Linux's 16K-page build and means nothing to
  a freestanding image, so `kernel8.img` under a name of our own, as on the
  Pi 4, should do. The load address is believed to be 0x80000 as on the Pi 4,
  but no source I could open states it: **unknown**.
- Bare-metal development wants three `config.txt` options, all **docs**:
  - `pciex4_reset=0` - by default the firmware resets the PCIe controller RP1
    hangs off before starting the OS, so every RP1 access would fault. With
    this set the OS inherits a working link. Without it, a bare-metal PCIe root
    complex driver is the price of any peripheral at all.
  - `enable_rp1_uart=1` - the firmware initialises RP1's UART0 to 115200 and
    does not reset RP1 before starting the OS.
  - `os_check=0` - the firmware otherwise checks for a compatible device tree
    before booting.
- Entry is at EL2 through an armstub (**third-party**); `boot.S` already copes
  with EL2 and EL3 entry.

## Address map

The BCM2712 puts its own peripherals above 4GB, and RP1 sits behind a PCIe
window:

| What | Address | Source |
| --- | --- | --- |
| SoC peripherals (child bus `0x7c000000`) | `0x10_7c00_0000` and up, 64MB | **dts** |
| VideoCore mailbox | `0x10_7c01_3880` | **dts** |
| PCIe root complex (RP1's link) | `0x10_0012_0000` | **dts** |
| GICv2 distributor | `0x10_7fff_9000` | **dts** |
| Debug UART (`uart10`), PL011 | `0x10_7d00_1000` | **third-party**; **dts** confirms it is the console |
| RP1 peripherals window | `0x1f_0000_0000` = RP1 `0x4000_0000` | **datasheet**, **third-party** |
| RP1 shared SRAM window | `0x1f_0400_0000` = RP1 `0x2000_0000` | **third-party** |

So an RP1 register at datasheet address `A` is at `0x1f_0000_0000 + (A -
0x4000_0000)`. Every RP1 base used below is given in the datasheet's own
addresses.

That window address is not architectural: it is where the firmware's root
complex happens to put RP1's BAR1, and the datasheet says only that the host
sees peripherals as offsets from the assigned BAR (**datasheet**). Bring-up
should read the assignment back from the root complex rather than trust
`0x1f_0000_0000`, which costs little and turns a silent fault into a printable
mismatch.

One correction to an earlier estimate: this does **not** need a wider address
space. `0x1f_0000_0000` is below 2^39, so the Pi 4 port's 39-bit table and 1GB
blocks reach it. What changes in [`mmu.c`](../ports/rpi4/mmu.c) is the layout:
drop the split fourth gigabyte and its `0xfc000000` device boundary, and add
Device blocks for the SoC window and the RP1 window. Smaller than a rewrite.

## Console

Two ways to a serial line, and the second is better for us:

1. **The dedicated debug connector** - a 3-pin JST between the HDMI ports,
   wired to `uart10` in the SoC, initialised by the firmware at 115200 with no
   PCIe involved (**third-party**, **dts**). Needs a debug cable.
2. **RP1 UART0 on GPIO14/15** - the same header pins as the Pi 4, selected as
   function `a4` on those pins (**datasheet**), and set up for us by
   `enable_rp1_uart=1` (**docs**). It needs the PCIe link (`pciex4_reset=0`),
   which every other RP1 device needs too. **The existing USB-serial adapter
   and wiring work unchanged.**

RP1's UART is the same ARM PL011 as the Pi 4's, with a 48MHz `clk_uart`
(**datasheet**), so the Pi 4's 115200 divisors (26 and 3/64) should carry over
as they are. Only the base address changes: `0x4003_0000` in RP1's map, so
`0x1f_0003_0000` from the CPU.

## GPIO

`bif_gpio.c` does not carry over; the registers are a different design
(**datasheet**):

- **`IO_BANK0`** at `0x400d_0000`: per pin a `STATUS` and a `CTRL` register,
  eight bytes apart. `CTRL` holds `FUNCSEL`
  (bits 4:0, 31 meaning none), `OUTOVER` (13:12), `OEOVER` (15:14) and
  `INOVER` (17:16). Function `a5` is `SYS_RIO`, which is software control.
- **`SYS_RIO0`** at `0x400e_0000`: `RIO_OUT`, `RIO_OE`, `RIO_NOSYNC_IN` and
  `RIO_SYNC_IN`, each with atomic set, clear and XOR aliases at +0x2000,
  +0x3000 and +0x1000. `gpio_write/2` becomes a single posted write with no
  read-modify-write, which suits the PCIe link.
- **`PADS_BANK0`** at `0x400f_0000` holds pull and drive; there is no
  `GPPUPPDN`.
- Header GPIO is bank 0, pins 0-27, so `RPI4_NUM_GPIO` (58) would shrink.
- Latency: a PCIe write costs about a microsecond and a read is a round trip,
  so the datasheet recommends a write barrier before a read after toggling a
  pin. Bit-banged protocols would be slower than on the Pi 4.
- The `gpio_mode/2` names `alt0` to `alt5` map onto RP1's `a0` to `a8`, and
  the two tables do not line up; the Prolog surface would need a decision.

## Ethernet

The MAC is a Cadence GEM_GXL 1p09 (**datasheet**), so the GENET work does not
transfer, but four things carry over or repeat:

- **The PHY is at MDIO address 1** (**dts**), and is reported to be the same
  BCM54213PE as the Pi 4's (**third-party**), so the clause 22 MDIO and
  link-detection code in `genet.c` is reusable.
- **The stack above is finished.** The `netif` contract, ARP, ICMP, UDP,
  `library(socket)`, `library(tftp)` and `readings.pl` need nothing.
- **The debug method carries over:** the peek/poke builtins and the serial REPL
  helper.
- **The same trap exists.** `ETH_CFG.CLKGEN` bit 9, `TXCLKDELEN`, "adds delay
  to the rgmii_tx_clk" (**datasheet**), is this chip's version of the transmit
  clock delay that silently stopped every GENET frame. Which setting is right
  is **unknown** until a frame either arrives or does not.

What is new, and larger than GENET was:

- **Descriptors live in memory**, not in controller registers, so they and the
  packet buffers both need the non-cacheable window; the Pi 4's DMA window
  carries the idea but not the code.
- **The PHY reset is a GPIO**: RP1 GPIO32, active low (**dts**). Ethernet
  therefore depends on the GPIO work above - and on more of it than the header
  needs, since pin 32 is outside bank 0 and wants `IO_BANK1` (`0x400d_4000`)
  and `SYS_RIO1` (`0x400e_4000`).
- **Address translation**: RP1's bus masters see host memory through PCIe, and
  whether that is 1:1 is a **third-party** claim.
- **Known Linux trouble** with this MAC and PHY - stalls tied to EEE
  (**third-party**). A polled driver should disable EEE in the PHY.
- The datasheet defers the MAC's register layout to Cadence's GEM_GXL user
  guide, which is not public; Linux's `macb` driver is the practical reference.
  A bare-metal GEM driver for RP1 exists (**third-party**) but its author says
  it was written before any hardware verified it.

Interrupts would be MSI-X through RP1's PCIe endpoint; this port polls, so none
are needed.

## Display

The least certain part. The VideoCore mailbox is at `0x10_7c01_3880`, but the
firmware's framebuffer support has been reduced in favour of Linux's own
display driver, and the reports disagree (**third-party**):

- one bare-metal example works with the `GET_FB` tag at 1920x1080, 32-bit,
  configured entirely through `config.txt` `hdmi_*` settings;
- others report allocation returning an error, and the firmware ignoring the
  requested depth and reporting 32 bits when the buffer is 16;
- Circle, a mature bare-metal framework, says the resolution cannot be set from
  the application.

The safe reading is: the firmware may hand over a framebuffer configured by
`config.txt`, and the application should ask what it got rather than request a
size. `fb.c` requests a size today; it would need to read instead, check the
pixel format at run time, and keep the `dc cvac` maintenance. Whether HDMI works
at all is **unknown**, and a real display driver is out of scope, so `fb_*`
should be treated as optional for this port.

## Cores and the clock

Two details in the existing code to check, one of which would not survive:

- **Core parking may fail.** `boot.S` parks every core whose `MPIDR_EL1` low
  byte is not zero. A76 cores on this chip are reported to number through the
  affinity 1 field instead (**third-party**), so all four could see
  Aff0 == 0 and all four run as the primary core, sharing one stack. The check
  has to read both fields. It is worth confirming first on the board, before
  anything that would be confusing to debug with four cores running it.
- The generic timer's frequency is read from `CNTFRQ_EL0`, as it already is, so
  the 54MHz assumption in a comment is the only thing that might go stale.

## Testing without QEMU

QEMU's Arm documentation lists `raspi0` to `raspi4b` and no `raspi5`, so there
is nothing to run a Pi 5 image on. Consequences:

- No `make rpi5-smoke`. CI could only check that the image builds and links.
- The engine is already covered by the Pi 4 smoke test, and shares nearly all
  its code with any Pi 5 image.
- Everything board-specific is verified by hand, over the card-swap loop the
  Pi 4 needed. Host-side mocks, like the socket shim's test, help for anything
  above the driver.

## Staging

Each stage ends in something checkable, and the first is a go/no-go.

| Stage | Adds | Proves | Needs |
| --- | --- | --- | --- |
| 1 | `ports/rpi5/` boot, `mmu.c` layout, RP1 UART0 on GPIO14/15, core parking | The engine boots to the REPL on the A76 | `pciex4_reset=0`, `enable_rp1_uart=1`; the existing USB-serial adapter |
| 2 | RP1 GPIO: `IO_BANK0`, `SYS_RIO`, `PADS` | The PCIe window works from our code; `gpio_*` | Stage 1 |
| 3 | Cadence GEM driver as a `netif`, PHY reset via GPIO32 | Ping, UDP, TFTP `readings.pl` on a Pi 5 | Stage 2, a cable |
| 4 | Framebuffer, if the firmware provides one | `fb_*` | A monitor, and luck |

A separate `ports/rpi5/` directory that copies what carries over
(`boot.S`, `fault.c`, `syscalls.c`), rather than factoring shared AArch64 code
out of `rpi4/` first. The shared parts are small, and it is easier to see what
is really common once there are two ports.

## Risks

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Every core running as the primary | High, and silent | Read `MPIDR_EL1` Aff0 and Aff1; check before anything else |
| PCIe not trained at entry | High | `pciex4_reset=0`; fault handler reports the abort; a root complex driver is the fallback and a large one |
| GEM transmit clock delay wrong | Medium - looks like a dead link | Peek/poke builtins, and try `TXCLKDELEN` both ways live |
| No framebuffer | Medium | It is optional; serial and TFTP are the point |
| No QEMU coverage | Medium | Above-the-driver code is mock-tested; hardware for the rest |
| Third-party facts wrong | Medium | Each is tagged; verify against the board, not the tag |

## Sources

- [RP1 peripherals datasheet](https://pip-assets.raspberrypi.com/categories/892-raspberry-pi-5/documents/RP-008370-DS-1-rp1-peripherals.pdf)
  (draft, 2023): address map, UART, GPIO, Ethernet, PCIe endpoint.
- [Raspberry Pi `config.txt` documentation](https://www.raspberrypi.com/documentation/computers/config_txt.html):
  `pciex4_reset`, `enable_rp1_uart`, `os_check`, kernel selection.
- Raspberry Pi's Linux device trees, `rpi-6.6.y`:
  [`bcm2712.dtsi`](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi),
  [`bcm2712-rpi.dtsi`](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/bcm2712-rpi.dtsi),
  [`bcm2712-rpi-5-b.dts`](https://github.com/raspberrypi/linux/blob/1ec873f3f18c98f0dc6d51db5b75958e109a0e5c/arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts).
- [Accessing RP1 peripherals from the BCM2712](https://forums.raspberrypi.com/viewtopic.php?t=368402):
  the `0x1f_0000_0000` window and `pciex4_reset=0`.
- [HopOS, `board/rpi5`](https://pkg.go.dev/github.com/xinix00/HopOS/metal/v2/board/rpi5)
  and its [GEM driver](https://pkg.go.dev/github.com/xinix00/HopOS/metal/v2/driver/nic/gem):
  a bare-metal Go kernel; source of the debug UART and core numbering claims.
- [Circle on the Raspberry Pi 5](https://circle-rpi.readthedocs.io/en/stable/appendices/raspberry-pi-5.html):
  what a mature bare-metal framework supports there.
- [Pi 5 framebuffer thread](https://forums.raspberrypi.com/viewtopic.php?t=380434),
  [firmware issue 1904](https://github.com/raspberrypi/firmware/issues/1904) and
  [a bare-metal framebuffer example](http://main.lv/writeup/raspberry5_baremetal_framebuffer.md).
- [QEMU's Raspberry Pi boards](https://www.qemu.org/docs/master/system/arm/raspi.html):
  `raspi0` to `raspi4b`.
