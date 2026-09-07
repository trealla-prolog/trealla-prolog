# GPIO from Prolog

Trealla can drive GPIO pins from two quite different places: a freestanding
image running on the bare metal, and an ordinary hosted build on Linux. Both
expose the same predicates, so the same Prolog runs on either.

| Predicate | Meaning |
| --- | --- |
| `gpio_mode(+Pin, +Mode)` | `input` or `output` (bare metal also takes `alt0`-`alt5`) |
| `gpio_pull(+Pin, +Pull)` | `none`, `up`, `down` |
| `gpio_read(+Pin, ?Level)` | reads the pin level as 0 or 1 |
| `gpio_write(+Pin, +Level)` | drives an output to 0 or 1 |
| `delay_ms(+Milliseconds)` | waits, on both targets (see below) |
| `gpio_chip(?Name, ?Label, ?Lines)` | hosted only: which controller was picked |

`ports/rpi4/blink.pl` runs unchanged on both.

## How they are selected

Neither is in a default build. Both are offered through `g_port_bif_tables`,
the NULL-terminated array of extra builtin tables the engine walks alongside
its own, chosen with `PORT_BIFS_OBJECT`; a build that selects neither links the
empty array in `src/port_bifs_none.c` and has no GPIO predicates at all. The
array exists so one board can expose several subsystems at once.

`delay_ms/1` is the exception, and comes from two different places. The
hosted backend defines it in `src/bif_gpio_linux.c`, beside the pins, because
a hosted build already has `sleep/1` and this exists only so the same Prolog
runs unchanged. Bare metal gets it from `src/bif_os_none.c` instead, together
with `sleep/1`: waiting is not board knowledge, so every freestanding target
has it whether or not it has pins.

```
make rpi4          # bare metal: ports/rpi4/bif_gpio.o, BCM2711 registers
make LINUX_GPIO=1  # hosted: src/bif_gpio_linux.o, /dev/gpiochip*
```

The names say what each one is tied to. The freestanding half genuinely is a
Pi 4 driver - `ports/rpi4/bif_gpio.c` pokes BCM2711 registers at fixed
addresses and works on nothing else, which is why it lives under `ports/rpi4/`.
The hosted half is tied to *Linux*, not to a board: it wraps the kernel's GPIO
character device (`<linux/gpio.h>`, the `GPIO_V2_*` ioctls) and contains no
chip constants at all, so it serves any Linux board that has a gpiochip. That
includes a Pi 5, whose RP1 breaks every register-poking approach.

It is refused anywhere but Linux because the ABI is Linux's. The BSDs each
have their own GPIO interface - FreeBSD's `/dev/gpioc`, NetBSD and OpenBSD's
`gpio(4)` - with different ioctls, so those would be separate files beside
this one rather than `#ifdef`s inside it.

## Why the two behave differently

The mechanisms are not the same shape, and the differences show through.

**Ownership.** Bare metal, a pin is a register you poke, and nothing arbitrates.
Under Linux a line is an *exclusive reservation* held open by a file
descriptor: `gpio_mode/2` acquires it and the hosted build holds the descriptor
for the life of the process. A pin already claimed by a kernel driver or
another process raises `permission_error(acquire, gpio_line, N)`, which has no
freestanding counterpart. This is the kernel doing arbitration you do not get
on the bare metal, and it is worth having.

**Pin multiplexing.** `alt0`-`alt5` select a pin's alternate function in the
SoC's mux. The character device has no concept of that - it belongs to pinctrl
and the device tree - so the hosted build accepts only `input` and `output`.

**The console pins.** The freestanding port refuses to reconfigure GPIO14/15
because they carry its only console, panic path included. Hosted, the kernel
owns that decision and will report the line busy if something else holds it, so
no special case is needed.

**Writing to a pin left as an input.** Bare metal this silently does nothing:
`GPSET` has no effect while the pin is not an output. Hosted, `gpio_write/2`
reconfigures the line to an output first, which is the less surprising reading
of a write. A program that sets the mode before writing - as `blink.pl` does -
behaves identically on both.

## Which controller the hosted build picks

Chip numbering has moved between Raspberry Pi OS releases, and an expander can
take a low number, so the port matches the SoC's own pin controller by label
(`pinctrl-...`) rather than assuming `gpiochip0`. That heuristic is the one
Pi-flavoured thing in an otherwise board-agnostic file: boards whose gpiochips
are labelled some other way simply fall back to the lowest-numbered chip, and
`TREALLA_GPIOCHIP` names one explicitly. `gpio_chip/3` reports what it chose:

```
?- gpio_chip(Name, Label, Lines).
   Name = gpiochip0, Label = 'pinctrl-bcm2711', Lines = 58.
```

Because it goes through the character device rather than mapped registers, the
hosted build is not tied to the BCM2711 - it should work on a Pi 5's RP1, and
on non-Pi Linux boards, though only the Pi 4 has been exercised.

## What has been tested

The hosted build has been compiled and run on Linux against a real (if
emulated) GPIO controller: chip discovery, `gpio_chip/3`, line request,
`gpio_write/2`, `gpio_read/2`, `delay_ms/1` and every argument-validation
error. It has **not** been run on a Raspberry Pi's own controller.

One caveat found while testing: on an emulated PL061, writing 1 to an output
and reading it straight back returns 0. A plain C program doing the identical
ioctls behaves the same way, so that is the emulated device rather than
Trealla. On a real Pi the level register reads the pad, so an output reads back
what it drives.
