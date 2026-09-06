#!/usr/bin/env python3

"""Generates ports/rpi4/font8x8.c from the glyph art below.

The art is the source: each glyph is eight rows of eight columns, '.' for
background and 'X' for ink, written left to right and top to bottom. Glyphs
are five columns wide with three of gap, except a few that need six; the
seventh row is the baseline and the eighth is left for descenders, which is
why lines have one row of leading unless a 'g' or a 'y' borrows it.

Run `python3 util/mkfont.py` after editing, and `--show` to proof it.
"""

import sys

GLYPHS = {
    ' ': "........ ........ ........ ........ ........ ........ ........ ........",
    '!': "..X..... ..X..... ..X..... ..X..... ..X..... ........ ..X..... ........",
    '"': ".X.X.... .X.X.... ........ ........ ........ ........ ........ ........",
    '#': ".X.X.... .X.X.... XXXXX... .X.X.... XXXXX... .X.X.... .X.X.... ........",
    '$': "..X..... .XXXX... X.X..... .XXX.... ..X.X... XXXX.... ..X..... ........",
    '%': "XX...X.. XX..X... ...X.... ..X..... .X...... X...XX.. X...XX.. ........",
    '&': ".XX..... X..X.... X.X..... .X...... X.X.X... X..X.... .XX.X... ........",
    "'": "..X..... ..X..... ........ ........ ........ ........ ........ ........",
    '(': "...X.... ..X..... .X...... .X...... .X...... ..X..... ...X.... ........",
    ')': ".X...... ..X..... ...X.... ...X.... ...X.... ..X..... .X...... ........",
    '*': "........ ..X..... X.X.X... .XXX.... X.X.X... ..X..... ........ ........",
    '+': "........ ..X..... ..X..... XXXXX... ..X..... ..X..... ........ ........",
    ',': "........ ........ ........ ........ ........ ..X..... ..X..... .X......",
    '-': "........ ........ ........ XXXXX... ........ ........ ........ ........",
    '.': "........ ........ ........ ........ ........ ........ ..X..... ........",
    '/': "....X... ....X... ...X.... ..X..... .X...... X....... X....... ........",
    '0': ".XXX.... X...X... X..XX... X.X.X... XX..X... X...X... .XXX.... ........",
    '1': "..X..... .XX..... ..X..... ..X..... ..X..... ..X..... .XXX.... ........",
    '2': ".XXX.... X...X... ....X... ...X.... ..X..... .X...... XXXXX... ........",
    '3': "XXXXX... ...X.... ..XX.... ....X... ....X... X...X... .XXX.... ........",
    '4': "...X.... ..XX.... .X.X.... X..X.... XXXXX... ...X.... ...X.... ........",
    '5': "XXXXX... X....... XXXX.... ....X... ....X... X...X... .XXX.... ........",
    '6': "..XX.... .X...... X....... XXXX.... X...X... X...X... .XXX.... ........",
    '7': "XXXXX... ....X... ...X.... ..X..... .X...... .X...... .X...... ........",
    '8': ".XXX.... X...X... X...X... .XXX.... X...X... X...X... .XXX.... ........",
    '9': ".XXX.... X...X... X...X... .XXXX... ....X... ...X.... .XX..... ........",
    ':': "........ ........ ..X..... ........ ........ ..X..... ........ ........",
    ';': "........ ........ ..X..... ........ ........ ..X..... ..X..... .X......",
    '<': "...X.... ..X..... .X...... X....... .X...... ..X..... ...X.... ........",
    '=': "........ ........ XXXXX... ........ XXXXX... ........ ........ ........",
    '>': ".X...... ..X..... ...X.... ....X... ...X.... ..X..... .X...... ........",
    '?': ".XXX.... X...X... ....X... ...X.... ..X..... ........ ..X..... ........",
    '@': ".XXX.... X...X... X.XXX... X.X.X... X.XXX... X....... .XXX.... ........",
    'A': "..X..... .X.X.... X...X... X...X... XXXXX... X...X... X...X... ........",
    'B': "XXXX.... X...X... X...X... XXXX.... X...X... X...X... XXXX.... ........",
    'C': ".XXX.... X...X... X....... X....... X....... X...X... .XXX.... ........",
    'D': "XXXX.... X...X... X...X... X...X... X...X... X...X... XXXX.... ........",
    'E': "XXXXX... X....... X....... XXXX.... X....... X....... XXXXX... ........",
    'F': "XXXXX... X....... X....... XXXX.... X....... X....... X....... ........",
    'G': ".XXX.... X...X... X....... X.XXX... X...X... X...X... .XXXX... ........",
    'H': "X...X... X...X... X...X... XXXXX... X...X... X...X... X...X... ........",
    'I': ".XXX.... ..X..... ..X..... ..X..... ..X..... ..X..... .XXX.... ........",
    'J': "....X... ....X... ....X... ....X... X...X... X...X... .XXX.... ........",
    'K': "X...X... X..X.... X.X..... XX...... X.X..... X..X.... X...X... ........",
    'L': "X....... X....... X....... X....... X....... X....... XXXXX... ........",
    'M': "X....X.. XX..XX.. X.XX.X.. X....X.. X....X.. X....X.. X....X.. ........",
    'N': "X...X... XX..X... X.X.X... X..XX... X...X... X...X... X...X... ........",
    'O': ".XXX.... X...X... X...X... X...X... X...X... X...X... .XXX.... ........",
    'P': "XXXX.... X...X... X...X... XXXX.... X....... X....... X....... ........",
    'Q': ".XXX.... X...X... X...X... X...X... X.X.X... X..X.... .XX.X... ........",
    'R': "XXXX.... X...X... X...X... XXXX.... X.X..... X..X.... X...X... ........",
    'S': ".XXXX... X....... X....... .XXX.... ....X... ....X... XXXX.... ........",
    'T': "XXXXX... ..X..... ..X..... ..X..... ..X..... ..X..... ..X..... ........",
    'U': "X...X... X...X... X...X... X...X... X...X... X...X... .XXX.... ........",
    'V': "X...X... X...X... X...X... X...X... X...X... .X.X.... ..X..... ........",
    'W': "X....X.. X....X.. X....X.. X.XX.X.. XX..XX.. X....X.. X....X.. ........",
    'X': "X...X... X...X... .X.X.... ..X..... .X.X.... X...X... X...X... ........",
    'Y': "X...X... X...X... .X.X.... ..X..... ..X..... ..X..... ..X..... ........",
    'Z': "XXXXX... ....X... ...X.... ..X..... .X...... X....... XXXXX... ........",
    '[': "..XXX... ..X..... ..X..... ..X..... ..X..... ..X..... ..XXX... ........",
    '\\': "X....... X....... .X...... ..X..... ...X.... ....X... ....X... ........",
    ']': ".XXX.... ...X.... ...X.... ...X.... ...X.... ...X.... .XXX.... ........",
    '^': "..X..... .X.X.... X...X... ........ ........ ........ ........ ........",
    '_': "........ ........ ........ ........ ........ ........ ........ XXXXX...",
    '`': ".X...... ..X..... ........ ........ ........ ........ ........ ........",
    'a': "........ ........ .XXX.... ....X... .XXXX... X...X... .XXXX... ........",
    'b': "X....... X....... XXXX.... X...X... X...X... X...X... XXXX.... ........",
    'c': "........ ........ .XXX.... X...X... X....... X...X... .XXX.... ........",
    'd': "....X... ....X... .XXXX... X...X... X...X... X...X... .XXXX... ........",
    'e': "........ ........ .XXX.... X...X... XXXXX... X....... .XXX.... ........",
    'f': "..XX.... .X..X... .X...... XXX..... .X...... .X...... .X...... ........",
    'g': "........ ........ .XXXX... X...X... X...X... .XXXX... ....X... .XXX....",
    'h': "X....... X....... XXXX.... X...X... X...X... X...X... X...X... ........",
    'i': "..X..... ........ .XX..... ..X..... ..X..... ..X..... .XXX.... ........",
    'j': "...X.... ........ ..XX.... ...X.... ...X.... ...X.... X..X.... .XX.....",
    'k': "X....... X....... X..X.... X.X..... XX...... X.X..... X..X.... ........",
    'l': ".XX..... ..X..... ..X..... ..X..... ..X..... ..X..... .XXX.... ........",
    'm': "........ ........ XX.X.... X.X.X... X.X.X... X.X.X... X.X.X... ........",
    'n': "........ ........ XXXX.... X...X... X...X... X...X... X...X... ........",
    'o': "........ ........ .XXX.... X...X... X...X... X...X... .XXX.... ........",
    'p': "........ ........ XXXX.... X...X... X...X... XXXX.... X....... X.......",
    'q': "........ ........ .XXXX... X...X... X...X... .XXXX... ....X... ....X...",
    'r': "........ ........ X.XXX... XX...... X....... X....... X....... ........",
    's': "........ ........ .XXXX... X....... .XXX.... ....X... XXXX.... ........",
    't': ".X...... .X...... XXXX.... .X...... .X...... .X..X... ..XX.... ........",
    'u': "........ ........ X...X... X...X... X...X... X...X... .XXXX... ........",
    'v': "........ ........ X...X... X...X... X...X... .X.X.... ..X..... ........",
    'w': "........ ........ X....X.. X....X.. X.XX.X.. XX..XX.. X....X.. ........",
    'x': "........ ........ X...X... .X.X.... ..X..... .X.X.... X...X... ........",
    'y': "........ ........ X...X... X...X... X...X... .XXXX... ....X... .XXX....",
    'z': "........ ........ XXXXX... ...X.... ..X..... .X...... XXXXX... ........",
    '{': "...XX... ..X..... ..X..... .X...... ..X..... ..X..... ...XX... ........",
    '|': "..X..... ..X..... ..X..... ..X..... ..X..... ..X..... ..X..... ........",
    '}': ".XX..... ...X.... ...X.... ....X... ...X.... ...X.... .XX..... ........",
    '~': "........ ........ ........ .XX..X.. X..XX... ........ ........ ........",
}

FIRST, LAST = 32, 126


def rows(art):
    parts = art.split()

    if len(parts) != 8 or any(len(p) != 8 for p in parts):
        raise ValueError("a glyph is eight rows of eight columns")

    if any(c not in ".X" for p in parts for c in p):
        raise ValueError("a glyph uses only '.' and 'X'")

    return parts


def byte(row):
    return sum(0x80 >> column for column, ink in enumerate(row) if ink == "X")


def main():
    missing = [chr(c) for c in range(FIRST, LAST + 1) if chr(c) not in GLYPHS]

    if missing or len(GLYPHS) != LAST - FIRST + 1:
        print(f"glyphs missing or spare: {missing}", file=sys.stderr)
        return 1

    table = {}

    for code in range(FIRST, LAST + 1):
        character = chr(code)

        try:
            table[character] = rows(GLYPHS[character])
        except ValueError as error:
            print(f"glyph {character!r}: {error}", file=sys.stderr)
            return 1

    if "--show" in sys.argv:
        sample = " ".join(sys.argv[2:]) or "Trealla Prolog 3.9 - jigsaw quiz, {vex} #42!"

        for line in range(8):
            print("".join(
                table.get(c, table[" "])[line].replace(".", " ")
                for c in sample if c in table))

        return 0

    with open("ports/rpi4/font8x8.c", "w") as out:
        out.write("// Generated by util/mkfont.py - edit the glyph art there, not here.\n\n")
        out.write('#include "font8x8.h"\n\n')
        out.write("const uint8_t rpi4_font8x8[RPI4_FONT_GLYPHS][8] = {\n")

        for code in range(FIRST, LAST + 1):
            character = chr(code)
            cells = ", ".join(f"0x{byte(r):02x}" for r in table[character])
            name = "backslash" if character == "\\" else f"'{character}'"
            out.write(f"\t{{{cells}}},\t// {name}\n")

        out.write("};\n")

    print(f"wrote ports/rpi4/font8x8.c ({LAST - FIRST + 1} glyphs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
