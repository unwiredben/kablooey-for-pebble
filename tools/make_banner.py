#!/usr/bin/env python3
"""Draw the 720x320 appstore banner.

The Rebble store wants a wide hero image, which neither the icons nor a 200x228
screen capture can be stretched into: 228 to 320 is a 1.4x scale, so a capture
would have to be resampled off the pixel grid and would go soft exactly where
the game is one-pixel detail. So the banner is drawn, like the icons in
`make_store_icon.py`, but out of the game's own shapes at 3x: the bomber, the
bombs and the pails here are `render.c`'s sprites in game units, scaled up, so
the banner cannot drift from what the watch actually shows.

The title is centred, with the top of the wall below it and Scarry standing on
the capstone off to one side, which is the shape these store banners take. It
is set in `TITLE_FONT`, a heavy condensed 6x11 pixel face in the manner of
Impact: two-unit stems, small counters and a single unit of letterspacing, so
the word fills its line. Drawing the face rather than using a real one is not
a compromise here — a 5x7 grid cannot hold stems that thick, and an outlined
pixel face sits with the sprites instead of looking borrowed. The tagline
keeps the lighter 5x7 face, since a second line in the heavy one would fight
the title.

There is no image library in this toolchain, so the whole thing rasterises at
2x and box-filters down for the curves, and writes the PNG by hand.

    python3 tools/make_banner.py                       # store/banner-720x320.png
    python3 tools/make_banner.py out.png
"""

import os
import struct
import sys
import zlib

W, H, SS = 720, 320, 2
S = 3                              # game pixels to banner pixels

# render.c's palette.
SKY        = (0x55, 0xAA, 0xFF)
BRICK      = (0x55, 0x55, 0x55)
MORTAR     = (0xAA, 0xAA, 0xAA)
CAP        = (0xAA, 0xAA, 0xAA)
BLACK      = (0x00, 0x00, 0x00)
WHITE      = (0xFF, 0xFF, 0xFF)
DARK       = (0x55, 0x55, 0x55)
LIGHT      = (0xAA, 0xAA, 0xAA)
SKIN       = (0xFF, 0xAA, 0xAA)    # GColorMelon
SHIRT      = (0xFF, 0x00, 0x00)
PANTS      = (0x00, 0x00, 0xAA)    # GColorDukeBlue
WATER      = (0x00, 0xAA, 0xFF)    # GColorVividCerulean
FUSE       = (0xAA, 0x55, 0x00)
SPARK      = (0xFF, 0xFF, 0x00)
BLAST      = (0xFF, 0xAA, 0x00)

# Banner layout. The wall sits lower than on the watch: the sky has to hold the
# title, and the brick only has to read as ground. It is still high enough that
# a three-pail stack stands on it without poking over the capstone.
WALL_TOP = 176
CAP_H = 14
BRICK_W, BRICK_H, JOINT = 26 * S, 12 * S, 2 * S

TITLE = "KABLOOEY!"
TITLE_SCALE, TITLE_Y = 8, 32
TAGLINE = "THREE PAILS. ONE MAD BOMBER."
TAGLINE_SCALE, TAGLINE_Y = 2, 148

# Heavy condensed 6x11 face for the title, in the manner of Impact: stems two
# units wide, counters squeezed to two, and flat terminals.
TITLE_FONT = {
    "K": "##..##|##..##|##.##.|##.##.|####..|###...|####..|##.##.|##.##.|##..##|##..##",
    "A": "..##..|.####.|.####.|##..##|##..##|######|######|##..##|##..##|##..##|##..##",
    "B": "#####.|######|##..##|##..##|#####.|#####.|##..##|##..##|##..##|######|#####.",
    "L": "##....|##....|##....|##....|##....|##....|##....|##....|##....|######|######",
    "O": ".####.|######|##..##|##..##|##..##|##..##|##..##|##..##|##..##|######|.####.",
    "E": "######|######|##....|##....|#####.|#####.|##....|##....|##....|######|######",
    "Y": "##..##|##..##|##..##|##..##|.####.|..##..|..##..|..##..|..##..|..##..|..##..",
    "!": "..##..|..##..|..##..|..##..|..##..|..##..|..##..|..##..|......|..##..|..##..",
}

# Lighter 5x7 face, for the tagline only.
SMALL_FONT = {
    "A": ".###.|#...#|#...#|#####|#...#|#...#|#...#",
    "B": "####.|#...#|#...#|####.|#...#|#...#|####.",
    "C": ".###.|#...#|#....|#....|#....|#...#|.###.",
    "D": "####.|#...#|#...#|#...#|#...#|#...#|####.",
    "E": "#####|#....|#....|####.|#....|#....|#####",
    "H": "#...#|#...#|#...#|#####|#...#|#...#|#...#",
    "I": "#####|..#..|..#..|..#..|..#..|..#..|#####",
    "K": "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
    "L": "#....|#....|#....|#....|#....|#....|#####",
    "M": "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
    "N": "#...#|##..#|#.#.#|#.#.#|#..##|#...#|#...#",
    "O": ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
    "P": "####.|#...#|#...#|####.|#....|#....|#....",
    "R": "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
    "S": ".####|#....|#....|.###.|....#|....#|####.",
    "T": "#####|..#..|..#..|..#..|..#..|..#..|..#..",
    "Y": "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
    ".": ".....|.....|.....|.....|.....|.##..|.##..",
    "!": "..#..|..#..|..#..|..#..|..#..|.....|..#..",
    " ": ".....|.....|.....|.....|.....|.....|.....",
}


def glyph_size(font):
    """Cell width and height, read off any glyph in the face."""
    rows = next(iter(font.values())).split("|")
    return len(rows[0]), len(rows)


def text_width(font, s, scale):
    """One column of air between cells, which is tight on purpose."""
    gw, _ = glyph_size(font)
    return (gw * len(s) + (len(s) - 1)) * scale


class Canvas(object):
    """A supersampled RGB surface with the few primitives the banner needs.

    Coordinates are banner pixels and may be fractional; everything is stamped
    at SS and filtered down at the end, which is the only antialiasing
    available here. Rectangles and glyphs land on whole pixels anyway, so only
    the circles and the fuses actually gain from it.
    """

    def __init__(self, fill):
        self.w, self.h = W * SS, H * SS
        self.px = [[fill] * self.w for _ in range(self.h)]

    def rect(self, x, y, w, h, colour):
        x0, y0 = int(x * SS), int(y * SS)
        for yy in range(max(0, y0), min(self.h, y0 + int(h * SS))):
            row = self.px[yy]
            for xx in range(max(0, x0), min(self.w, x0 + int(w * SS))):
                row[xx] = colour

    def disc(self, cx, cy, r, colour):
        cx, cy, r = cx * SS, cy * SS, r * SS
        rr = r * r
        for yy in range(max(0, int(cy - r)), min(self.h, int(cy + r) + 1)):
            row = self.px[yy]
            dy2 = (yy - cy + 0.5) ** 2
            for xx in range(max(0, int(cx - r)), min(self.w, int(cx + r) + 1)):
                if (xx - cx + 0.5) ** 2 + dy2 <= rr:
                    row[xx] = colour

    def ring(self, cx, cy, r, width, colour):
        cx, cy, r, width = cx * SS, cy * SS, r * SS, width * SS
        outer, inner = r * r, (r - width) ** 2
        for yy in range(max(0, int(cy - r)), min(self.h, int(cy + r) + 1)):
            row = self.px[yy]
            dy2 = (yy - cy + 0.5) ** 2
            for xx in range(max(0, int(cx - r)), min(self.w, int(cx + r) + 1)):
                d = (xx - cx + 0.5) ** 2 + dy2
                if inner <= d <= outer:
                    row[xx] = colour

    def thick_line(self, x0, y0, x1, y1, width, colour):
        """Stamped with a disc, so ends and joins stay round like the SDK's."""
        steps = max(2, int(max(abs(x1 - x0), abs(y1 - y0)) * SS))
        for i in range(steps + 1):
            t = i / steps
            self.disc(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, width / 2.0, colour)

    def text(self, font, s, x, y, scale, colour, outline=None):
        """Draw `s` at x,y. An outline is stamped first in all eight
        directions, which is one pass per direction but keeps the glyph tables
        free of a second outlined copy of the face."""
        gw, _ = glyph_size(font)
        if outline:
            for dx, dy in ((-scale, 0), (scale, 0), (0, -scale), (0, scale),
                           (-scale, -scale), (scale, -scale),
                           (-scale, scale), (scale, scale)):
                self.text(font, s, x + dx, y + dy, scale, outline)
        for i, ch in enumerate(s.upper()):
            glyph = font.get(ch)
            if glyph is None:
                sys.exit("no glyph for %r in this face; add it" % ch)
            gx = x + i * (gw + 1) * scale
            for ry, row in enumerate(glyph.split("|")):
                for rx, cell in enumerate(row):
                    if cell == "#":
                        self.rect(gx + rx * scale, y + ry * scale,
                                  scale, scale, colour)

    def centred_text(self, font, s, y, scale, colour, outline=None):
        self.text(font, s, (W - text_width(font, s, scale)) // 2, y,
                  scale, colour, outline)

    def resolve(self):
        out, area = [], SS * SS
        for y in range(H):
            row = []
            for x in range(W):
                r = g = b = 0
                for sy in range(SS):
                    src = self.px[y * SS + sy]
                    for sx in range(SS):
                        c = src[x * SS + sx]
                        r += c[0]; g += c[1]; b += c[2]
                row.append((r // area, g // area, b // area))
            out.append(row)
        return out


def draw_wall(c):
    """Capstone with its lip, then running-bond brick, as in render.c."""
    c.rect(0, WALL_TOP, W, H - WALL_TOP, MORTAR)
    c.rect(0, WALL_TOP, W, CAP_H, CAP)
    c.rect(0, WALL_TOP, W, S, WHITE)                       # lit top edge
    c.rect(0, WALL_TOP + CAP_H - S, W, S, DARK)            # shadow under the lip
    course = 0
    y = WALL_TOP + CAP_H
    while y < H:
        start = -BRICK_W // 2 if course % 2 else 0
        x = start
        while x < W:
            c.rect(x, y, BRICK_W - JOINT, BRICK_H - JOINT, BRICK)
            x += BRICK_W
        y += BRICK_H
        course += 1


def draw_bomber(c, cx, feet, facing):
    """render.c's bomber, in game units scaled by S. `feet` is the capstone top."""
    top = feet - 36 * S
    face_y = top + 15 * S
    d = facing

    c.rect(cx - 7 * S, top, 14 * S, 8 * S, BLACK)              # crown
    c.rect(cx - 13 * S, top + 7 * S, 26 * S, 3 * S, BLACK)     # brim
    c.disc(cx, face_y, 8 * S, SKIN)                            # head
    for ex in (-3, 4):                                         # eyes, looking his way
        c.disc(cx + ex * S, face_y - 2 * S, 3 * S, WHITE)
        c.disc(cx + (ex + d) * S, face_y - 2 * S, 1 * S, BLACK)
    c.rect(cx - 6 * S, face_y + 4 * S, 13 * S, 3 * S, BLACK)   # mustache
    c.rect(cx - 8 * S, top + 22 * S, 16 * S, 10 * S, SHIRT)    # body
    c.thick_line(cx + 6 * S * d, top + 24 * S,                 # lobbing arm
                 cx + 12 * S * d, top + 19 * S, 3 * S, SKIN)
    c.thick_line(cx - 6 * S * d, top + 24 * S,                 # trailing arm
                 cx - 11 * S * d, top + 28 * S, 3 * S, SKIN)
    c.rect(cx - 7 * S, top + 31 * S, 5 * S, 4 * S, PANTS)      # legs, mid-stride
    c.rect(cx + 2 * S, top + 31 * S, 5 * S, 6 * S, PANTS)


def draw_bomb(c, cx, cy, big_spark=True):
    r = 5 * S
    c.thick_line(cx + 1 * S, cy - r, cx + 4 * S, cy - r - 4 * S, 2 * S, FUSE)
    c.disc(cx + 5 * S, cy - r - 5 * S, (3 if big_spark else 2) * S,
           SPARK if big_spark else BLAST)
    c.disc(cx, cy, r + S, WHITE)                               # pale halo
    c.disc(cx, cy, r, BLACK)
    c.disc(cx - 2 * S, cy - 2 * S, 1 * S, LIGHT)               # glint


def draw_pails(c, cx, bottom, count):
    w, h, pitch = 38 * S, 10 * S, 14 * S
    left = cx - w // 2
    for i in range(count):
        top = bottom - h - i * pitch
        c.rect(left, top, w, h, LIGHT)
        c.rect(left + 2 * S, top + 2 * S, w - 4 * S, h - 3 * S, WATER)
        c.rect(left, top, w, 2 * S, WHITE)                     # rim
        for x, y, ww, hh in ((left, top, w, S), (left, top + h - S, w, S),
                             (left, top, S, h), (left + w - S, top, S, h)):
            c.rect(x, y, ww, hh, BLACK)                        # outline
        if i == count - 1:                                     # shimmer, top pail
            c.rect(left + 7 * S, top + 5 * S, 9 * S, S, WHITE)
            c.rect(left + 22 * S, top + 7 * S, 8 * S, S, WHITE)


def draw():
    c = Canvas(SKY)
    draw_wall(c)

    # Scarry stands on the capstone at the right, clear of the centred title,
    # and throws back across the banner: the bombs step down the brick to the
    # pails in the far corner, so the diagonal ties the two halves together.
    # Nothing crosses a letter — a bomb over a glyph reads as a hole in the
    # word, not as a bomb.
    draw_bomber(c, 650, WALL_TOP, -1)
    for x, y, big in ((566, 212, True), (452, 248, False), (338, 284, True)):
        draw_bomb(c, x, y, big)
    draw_pails(c, 170, H - 8, 3)

    c.centred_text(TITLE_FONT, TITLE, TITLE_Y, TITLE_SCALE, WHITE, outline=BLACK)
    c.centred_text(SMALL_FONT, TAGLINE, TAGLINE_Y, TAGLINE_SCALE, BLACK)
    return c.resolve()


def write_png(path, rows):
    w, h = len(rows[0]), len(rows)
    raw = b"".join(b"\x00" + bytes(v for c in row for v in c) for row in rows)

    def chunk(tag, data):
        body = tag + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xffffffff))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    directory = os.path.dirname(path)
    if directory:
        os.makedirs(directory, exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    print("wrote %s (%dx%d)" % (path, w, h))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "store/banner-720x320.png"
    print("title %dpx wide, tagline %dpx, banner %dpx"
          % (text_width(TITLE_FONT, TITLE, TITLE_SCALE),
             text_width(SMALL_FONT, TAGLINE, TAGLINE_SCALE), W))
    write_png(out, draw())
