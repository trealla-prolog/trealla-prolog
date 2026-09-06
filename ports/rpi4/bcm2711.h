#pragma once

#include <stddef.h>
#include <stdint.h>

// BCM2711 register map in low-peripheral mode, shared by the platform adapter
// and the GPIO builtins so the peripheral base is stated once.

#define PERI_BASE 0xfe000000u
#define GPIO_BASE (PERI_BASE + 0x200000u)
#define UART0_BASE (PERI_BASE + 0x201000u)

#define REG(addr) (*(volatile uint32_t*)(uintptr_t)(addr))

// Function select: 3 bits per pin, 10 pins per register.
#define GPFSEL(pin) REG(GPIO_BASE + 0x00u + ((pin) / 10u) * 4u)
#define GPFSEL_SHIFT(pin) (((pin) % 10u) * 3u)

// Output set and clear are separate write-1-to-act registers, and the level
// register is read-only: 1 bit per pin, 32 pins per register.
#define GPSET(pin) REG(GPIO_BASE + 0x1cu + ((pin) / 32u) * 4u)
#define GPCLR(pin) REG(GPIO_BASE + 0x28u + ((pin) / 32u) * 4u)
#define GPLEV(pin) REG(GPIO_BASE + 0x34u + ((pin) / 32u) * 4u)
#define GPIO_BIT(pin) (1u << ((pin) % 32u))

// Pull up/down: 2 bits per pin, 16 pins per register. This is the BCM2711
// register, not the BCM2835 GPPUD/GPPUDCLK clocking sequence, and its pull
// encoding is the reverse of that one - see bif_gpio.c.
#define GPPUPPDN(pin) REG(GPIO_BASE + 0xe4u + ((pin) / 16u) * 4u)
#define GPPUPPDN_SHIFT(pin) (((pin) % 16u) * 2u)

// A 2MiB window of Normal Non-Cacheable memory, immediately below the heap
// ceiling, for anything a device does DMA into or out of. ports/rpi4/mmu.c
// maps it; rpi4.ld stops the heap short of it so the two never overlap.
//
// GENET keeps its descriptors in its own register window, so only packet
// buffers need to live here - which is a good deal less than a DMA engine
// usually demands.

#define RPI4_DMA_BASE 0x1fe00000ull
#define RPI4_DMA_SIZE (2ull << 20)

// Hands out cache-line aligned pieces of that window. There is no free: what
// a driver takes at bring-up it keeps for the life of the board.
void *rpi4_dma_alloc(size_t len);

#define RPI4_NUM_GPIO 58u

// GPIO14/15 carry the PL011 console. Reconfiguring them silences the only
// output the board has, the panic path included.
#define RPI4_CONSOLE_TX 14u
#define RPI4_CONSOLE_RX 15u
