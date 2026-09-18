#!/usr/bin/env python3
"""Draw the 25x25 launcher icon: a lit bomb.

There is no image editor in this toolchain, so the icon is drawn in code and
written straight out as an 8-bit palette PNG. The palette matches what the
Pebble launcher expects: index 0 black, index 1 white, index 2 transparent.

    python3 tools/make_icon.py resources/kablooey-icon.png
"""

import struct
import sys
import zlib

W = H = 25
BLACK, WHITE, CLEAR = 0, 1, 2


def dot(px, x, y, colour):
    if 0 <= x < W and 0 <= y < H:
        px[y][x] = colour


def draw():
    px = [[CLEAR] * W for _ in range(H)]

    # Body. The squared threshold is pulled in slightly so the circle does not
    # end in a single-pixel nub top and bottom.
    for y in range(H):
        for x in range(W):
            if (x - 11) ** 2 + (y - 15) ** 2 <= 8 * 8 - 4:
                px[y][x] = BLACK

    # Filler cap, straddling the top of the body.
    for y in range(5, 9):
        for x in range(9, 14):
            dot(px, x, y, BLACK)

    # Glint, upper left of the body.
    for x, y in ((6, 13), (7, 12), (8, 12), (6, 14)):
        dot(px, x, y, WHITE)

    # Fuse, curling up and right off the cap, two pixels thick.
    for x, y in ((14, 4), (15, 3), (16, 3), (17, 2)):
        dot(px, x, y, BLACK)
        dot(px, x, y + 1, BLACK)

    # Spark: a four-armed burst at the fuse tip.
    for x, y in ((19, 1), (19, 0), (18, 1), (20, 1), (19, 2), (21, 0)):
        dot(px, x, y, BLACK)

    return px


def write_png(path, px):
    raw = b"".join(b"\x00" + bytes(row) for row in px)
    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", bytes((0, 0, 0, 255, 255, 255, 0, 0, 0)))
    png += chunk(b"tRNS", bytes((255, 255, 0)))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def preview(px):
    art = {BLACK: "#", WHITE: "o", CLEAR: "."}
    return "\n".join("".join(art[v] for v in row) for row in px)


if __name__ == "__main__":
    pixels = draw()
    out = sys.argv[1] if len(sys.argv) > 1 else "resources/kablooey-icon.png"
    write_png(out, pixels)
    print(preview(pixels))
    print("wrote", out)
