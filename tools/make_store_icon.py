#!/usr/bin/env python3
"""Draw the appstore icons: the lit bomb over the wall, in colour.

The launcher icon in `make_icon.py` is 25x25 and one bit deep, which is all the
watch wants. The appstore wants bigger, full-colour squares, so this draws the
same bomb again rather than upscaling that one: everything here is in unit
coordinates and rasterised at whatever size is asked for, 4x supersampled and
box-filtered down, which is the only antialiasing available without an image
library.

Colours are the game's own, out of `render.c`.

    python3 tools/make_store_icon.py store/icon-80.png 80
    python3 tools/make_store_icon.py           # writes every store icon
"""

import os
import struct
import sys
import zlib

SS = 4                      # supersampling factor

# render.c's palette.
SKY        = (0x55, 0xAA, 0xFF)   # GColorPictonBlue
BRICK      = (0x55, 0x55, 0x55)   # GColorDarkGray
MORTAR     = (0xAA, 0xAA, 0xAA)   # GColorLightGray
CAP        = (0xAA, 0xAA, 0xAA)   # GColorLightGray
BODY       = (0x00, 0x00, 0x00)
GLINT      = (0xFF, 0xFF, 0xFF)
FUSE       = (0xAA, 0x55, 0x00)   # GColorWindsorTan
SPARK      = (0xFF, 0xFF, 0x00)   # GColorYellow
SPARK_EDGE = (0xFF, 0xAA, 0x00)   # GColorOrange

# The wall starts here; the bomb hangs in the sky above it.
WALL_TOP = 0.76
CAP_BAND = 0.035                  # capstone, as a fraction of the icon

BOMB_CX, BOMB_CY, BOMB_R = 0.44, 0.52, 0.25
FILLER = (0.385, 0.215, 0.495, 0.30)          # filler cap, straddling the body
FUSE_PATH = ((0.46, 0.235), (0.62, 0.135), (0.72, 0.185))   # quadratic control
FUSE_W = 0.045
SPARK_C, SPARK_R = (0.755, 0.155), 0.075


def bezier(p0, p1, p2, t):
    u = 1.0 - t
    return (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
            u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])


def draw(size):
    """Return `size`x`size` rows of RGB tuples, drawn at SS and filtered down."""
    n = size * SS
    px = [[SKY] * n for _ in range(n)]

    def put(x, y, colour):
        if 0 <= x < n and 0 <= y < n:
            px[y][x] = colour

    def rect(x0, y0, x1, y1, colour):
        for y in range(max(0, int(y0 * n)), min(n, int(y1 * n) + 1)):
            for x in range(max(0, int(x0 * n)), min(n, int(x1 * n) + 1)):
                put(x, y, colour)

    def disc(cx, cy, r, colour):
        rr = (r * n) ** 2
        for y in range(max(0, int((cy - r) * n)), min(n, int((cy + r) * n) + 1)):
            for x in range(max(0, int((cx - r) * n)), min(n, int((cx + r) * n) + 1)):
                if (x - cx * n) ** 2 + (y - cy * n) ** 2 <= rr:
                    put(x, y, colour)

    # The wall: capstone, then brick courses with mortar between them. Courses
    # are staggered so the joints read as brick and not as a grid.
    #
    # The brick gets coarser as the icon gets smaller, which is the one place
    # this drawing is not purely a scale of itself. At 144 the band is 35px and
    # holds three courses; at 48 it is 11px, and three courses there put a
    # 1px joint every 3px, which greys the whole band into mush instead of
    # reading as brick. Two courses of wider bricks keep it legible.
    courses, per_course = (3, 4) if size >= 96 else (2, 2)
    rect(0.0, WALL_TOP, 1.0, 1.0, BRICK)
    rect(0.0, WALL_TOP, 1.0, WALL_TOP + CAP_BAND, CAP)
    course = (1.0 - WALL_TOP - CAP_BAND) / courses
    for i in range(courses):
        top = WALL_TOP + CAP_BAND + i * course
        rect(0.0, top, 1.0, top + course * 0.09, MORTAR)           # bed joint
        for j in range(per_course):                                # head joints
            x = (j + (0.5 if i % 2 else 0.0)) / per_course
            rect(x, top, x + 0.012, top + course, MORTAR)

    # Bomb: body, filler cap, then the glint on the upper left.
    disc(BOMB_CX, BOMB_CY, BOMB_R, BODY)
    rect(*FILLER, BODY)
    disc(BOMB_CX - BOMB_R * 0.45, BOMB_CY - BOMB_R * 0.45, BOMB_R * 0.16, GLINT)

    # Fuse: the curve stamped with a disc so it keeps an even thickness and
    # rounded ends, which a polyline would not give at these sizes.
    steps = max(24, n // 4)
    for i in range(steps + 1):
        x, y = bezier(*FUSE_PATH, i / steps)
        disc(x, y, FUSE_W / 2, FUSE)

    # Spark: a four-armed burst at the fuse tip, orange under yellow so it has
    # an edge against the sky.
    cx, cy = SPARK_C
    for r, colour in ((SPARK_R, SPARK_EDGE), (SPARK_R * 0.62, SPARK)):
        disc(cx, cy, r * 0.42, colour)
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (0.7, 0.7),
                       (-0.7, 0.7), (0.7, -0.7), (-0.7, -0.7)):
            for i in range(steps // 2 + 1):
                t = i / (steps // 2)
                disc(cx + dx * r * t, cy + dy * r * t,
                     r * 0.16 * (1.0 - t * 0.7), colour)

    # Box filter down to the requested size.
    out = []
    area = SS * SS
    for y in range(size):
        row = []
        for x in range(size):
            r = g = b = 0
            for sy in range(SS):
                src = px[y * SS + sy]
                for sx in range(SS):
                    c = src[x * SS + sx]
                    r += c[0]; g += c[1]; b += c[2]
            row.append((r // area, g // area, b // area))
        out.append(row)
    return out


def write_png(path, rows):
    size = len(rows)
    raw = b"".join(b"\x00" + bytes(v for c in row for v in c) for row in rows)

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


if __name__ == "__main__":
    if len(sys.argv) == 3:
        jobs = [(sys.argv[1], int(sys.argv[2]))]
    elif len(sys.argv) == 1:
        jobs = [("store/icon-48.png", 48),        # Rebble small icon
                ("store/icon-80.png", 80),
                ("store/icon-144.png", 144)]
    else:
        sys.exit("usage: make_store_icon.py [path size]")
    for path, size in jobs:
        write_png(path, draw(size))
        print("wrote %s (%dx%d)" % (path, size, size))
