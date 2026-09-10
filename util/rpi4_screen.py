#!/usr/bin/env python3

"""Proves the Pi 4 framebuffer console end to end, under QEMU.

Booting the image and seeing "TREALLA FRAMEBUFFER OK" on the serial line only
proves the GPU answered the mailbox. It says nothing about whether anything we
drew is where the display controller reads from - the mistake that matters
here is a cache line still sitting in the ARM's cache, or a pitch misread, and
either would leave the marker intact and the screen wrong.

So: boot, take a screenshot through QEMU's monitor, and read the pixels back
into text by matching every 8x8 cell against the font that drew them. If the
markers can be read off the screen, the whole path worked.
"""

import json
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkfont

# The mailbox line is deliberately absent: it is printed to say the GPU is
# answering, which is what has to happen before there is a screen to print it
# on. Everything after the console opens should be on both outputs.
EXPECTED = (
    "TREALLA FRAMEBUFFER OK",
    "TREALLA FREESTANDING BOOT",
    "TREALLA PROLOG OK",
    "TREALLA GPIO OK",
    "TREALLA FB OK",
    "TREALLA ALLOCATION FAILURE CONTROLLED",
    "TREALLA HEAP PEAK ",
    "TREALLA FREESTANDING COMPLETE",
)

DONE = "TREALLA FREESTANDING COMPLETE"

# Stands in for a cell no glyph explains. Not "?", which is a glyph.
UNREADABLE = "\ufffd"
BOOT_TIMEOUT = 60


def glyph_table():
    """Maps an 8x8 bitmap back to the character that draws it."""

    table = {}

    for character, art in mkfont.GLYPHS.items():
        key = tuple(mkfont.byte(row) for row in mkfont.rows(art))
        table.setdefault(key, character)

    return table


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()

    fields, offset = [], 0

    while len(fields) < 4:
        while offset < len(data) and data[offset : offset + 1].isspace():
            offset += 1

        if data[offset : offset + 1] == b"#":
            while data[offset : offset + 1] not in (b"\n", b""):
                offset += 1

            continue

        start = offset

        while offset < len(data) and not data[offset : offset + 1].isspace():
            offset += 1

        fields.append(data[start:offset])

    magic, width, height, maxval = fields

    if magic != b"P6" or maxval != b"255":
        raise ValueError(f"unexpected screenshot format {magic!r}/{maxval!r}")

    return int(width), int(height), data[offset + 1 :]


def screen_text(path):
    width, height, pixels = read_ppm(path)
    table = glyph_table()
    lines = []

    for cell_y in range(height // 8):
        line = ""

        for cell_x in range(width // 8):
            rows = []

            for y in range(8):
                bits = 0

                for x in range(8):
                    at = ((cell_y * 8 + y) * width + cell_x * 8 + x) * 3

                    if any(pixels[at : at + 3]):
                        bits |= 0x80 >> x

                rows.append(bits)

            line += table.get(tuple(rows), UNREADABLE)

        lines.append(line.rstrip())

    return "\n".join(lines).rstrip("\n")


def qmp(path, commands):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.connect(path)
        stream = connection.makefile("rwb")
        stream.readline()					# greeting

        for command in commands:
            stream.write(json.dumps(command).encode() + b"\n")
            stream.flush()

            while True:
                reply = json.loads(stream.readline())

                if "event" in reply:
                    continue

                if "error" in reply:
                    raise RuntimeError(f"{command['execute']}: {reply['error']}")

                break


def main():
    if len(sys.argv) != 4:
        print("usage: rpi4_screen.py <qemu-system-aarch64> <firmware.elf> <workdir>",
            file=sys.stderr)
        return 2

    qemu, firmware, workdir = sys.argv[1:]
    os.makedirs(workdir, exist_ok=True)
    serial = os.path.join(workdir, "serial.log")
    monitor = os.path.join(workdir, "qmp.sock")
    shot = os.path.abspath(os.path.join(workdir, "screen.ppm"))

    for stale in (serial, monitor, shot):
        if os.path.exists(stale):
            os.remove(stale)

    # The image without semihosting, so it parks in wfe when it finishes
    # rather than taking QEMU down before the screen can be captured.
    machine = subprocess.Popen(
        [qemu, "-machine", "raspi4b", "-kernel", firmware, "-display", "none",
         "-serial", f"file:{serial}", "-qmp", f"unix:{monitor},server,nowait"],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE)

    try:
        deadline = time.time() + BOOT_TIMEOUT

        while time.time() < deadline:
            if machine.poll() is not None:
                print(machine.stderr.read().decode(), file=sys.stderr)
                print("QEMU exited before the screen could be captured",
                    file=sys.stderr)
                return 1

            if os.path.exists(serial):
                with open(serial) as f:
                    if DONE in f.read():
                        break

            time.sleep(0.2)
        else:
            print(f"the image did not reach {DONE!r} in {BOOT_TIMEOUT}s",
                file=sys.stderr)
            return 1

        qmp(monitor, [
            {"execute": "qmp_capabilities"},
            {"execute": "screendump", "arguments": {"filename": shot}},
        ])
    finally:
        machine.kill()
        machine.wait()

    text = screen_text(shot)
    print(text)
    position = 0

    for marker in EXPECTED:
        found = text.find(marker, position)

        if found < 0:
            print(f"not on screen: {marker}", file=sys.stderr)
            return 1

        position = found + len(marker)

    if UNREADABLE in text:
        print("the screen holds pixels no glyph explains", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
