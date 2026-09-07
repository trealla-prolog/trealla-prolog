#!/usr/bin/env python3

import re
import subprocess
import sys


MARKERS = (
    "TREALLA MAILBOX OK ram=",
    "TREALLA FRAMEBUFFER OK",
    "TREALLA FREESTANDING BOOT",
    "TREALLA PROLOG OK",
    "TREALLA GPIO OK",
    "TREALLA ALLOCATION FAILURE CONTROLLED",
    "TREALLA HEAP PEAK ",
    "TREALLA FREESTANDING COMPLETE",
)

# Measured 1,488,120 / 101,496 / 13,016 text/data/bss and a 5,802,432-byte
# live-heap peak. Data and bss fell sharply once a build without FFI stopped
# carrying FFI's baggage - the reserved g_ffi_bifs table, then the argument
# arrays inside every builtins entry. Size limits kept as they were, so they
# still catch a material regression without being re-tuned on every
# improvement.
#
# The heap limit is the exception. The peak roughly halved, to 2,984,232, when
# struct prolog stopped carrying two fixed MAX_TABS arrays and 1024 stream
# structs - so the old 7MB ceiling would no longer notice all of that coming
# back. Lowered to 3.5MB to hold the ground that was won.
MAX_TEXT_BYTES = 1_750_000
MAX_DATA_BYTES = 500_000
MAX_BSS_BYTES = 900_000
MAX_HEAP_BYTES = 3_500_000


def image_sizes(size_output: str):
    for line in size_output.splitlines():
        fields = line.split()

        if len(fields) >= 6 and all(field.isdigit() for field in fields[:4]):
            return tuple(int(field) for field in fields[:3])

    return None


def heap_peak(qemu_output: str):
    match = re.search(r"TREALLA HEAP PEAK ([0-9]+)", qemu_output)
    return int(match.group(1)) if match else None


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: rpi4_smoke.py <qemu-system-aarch64> <firmware.elf> <size-tool>",
            file=sys.stderr,
        )
        return 2

    size_result = subprocess.run(
        [sys.argv[3], sys.argv[2]],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    print(size_result.stdout, end="")
    sizes = image_sizes(size_result.stdout)

    if size_result.returncode != 0 or sizes is None:
        print("could not read Pi 4 firmware sizes", file=sys.stderr)
        return 1

    text_size, data_size, bss_size = sizes
    limits = (
        ("text", text_size, MAX_TEXT_BYTES),
        ("data", data_size, MAX_DATA_BYTES),
        ("bss", bss_size, MAX_BSS_BYTES),
    )

    for name, actual, maximum in limits:
        if actual > maximum:
            print(
                f"Pi 4 firmware {name} budget exceeded: {actual:,} > {maximum:,} bytes",
                file=sys.stderr,
            )
            return 1

    # serial0 is the PL011 the port drives; the mini UART is serial1 and is
    # left unconnected. Semihosting carries the halt status out.
    command = [
        sys.argv[1],
        "-machine", "raspi4b",
        "-kernel", sys.argv[2],
        "-serial", "stdio",
        "-display", "none",
        "-semihosting-config", "enable=on",
    ]

    try:
        result = subprocess.run(
            command,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=60,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        output = error.stdout or ""
        print(output if isinstance(output, str) else output.decode(), end="")
        print("Pi 4 smoke timed out", file=sys.stderr)
        return 1

    print(result.stdout, end="")
    position = 0

    for marker in MARKERS:
        found = result.stdout.find(marker, position)

        if found < 0:
            print(f"Pi 4 smoke missing marker: {marker}", file=sys.stderr)
            return 1

        position = found + len(marker)

    heap_size = heap_peak(result.stdout)

    if heap_size is None:
        print("Pi 4 smoke did not report a numeric heap peak", file=sys.stderr)
        return 1

    if heap_size > MAX_HEAP_BYTES:
        print(
            f"Pi 4 heap budget exceeded: {heap_size:,} > {MAX_HEAP_BYTES:,} bytes",
            file=sys.stderr,
        )
        return 1

    if result.returncode != 0:
        print(f"QEMU exited with status {result.returncode}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
