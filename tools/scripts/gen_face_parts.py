#!/usr/bin/env python3
"""gen_face_parts.py - emit content/art/faces/face-parts.png + face-parts-index.json.

FaceGen LITE part library, amended to tile parts (GRANADAD ART UNIFIED SPEC v1
section 4; DECISIONS.md Art register row, Eli 2026-07-13 FOURTH revision,
pillar 4: decision 4c's Warsim-style layered face generator composes from
tile/sprite parts instead of ASCII; FACES-SPEC.md composition logic retained).
All art ORIGINAL, zero copied pixels. MERCOLAS-24 palette only (+ transparent).

Canvas: 48x48 = 3x3 grid of 16px cells (spec 4.1). Every part is authored in
ABSOLUTE canvas coordinates on a 48x48 scratch canvas, then cropped to its
slot rect; the validator rejects any pixel outside the slot (the slot-rect
coherence rule, spec 4.1). Slot anchors (px from canvas top-left, pinned,
placeholder pending golden bless):

    base     3x3 @ (0,0)     scar 1x1 @ anchor table (spec 4.4)
    mouth    2x1 @ (8,30)    nose 1x1 @ (16,22)     eyes 2x1 @ (8,16)
    brow     2x1 @ (8,8)     hair 3x2 @ (0,0)       headwear 3x2 @ (0,0)

Line-art slots (eyes/brow/nose/mouth-clean/scar) are machine-restricted to
{N0, G1, B2, R1, R2} so any baked skin shows through (spec 4.5). Skin is baked
into bases; hair/headwear/beards are pre-colored per ramp, one entry per
style x color.

Deterministic: byte-identical rerun. No timestamps, no `random` module; all
stochastic texture comes from xorshift32 streams seeded by FNV-1a-32 of the
part id (the gen_custom_tiles.py discipline). PNG: RGBA, optimize=False,
compress_level=9, no ancillary chunks. JSON: indent-2 style, LF, UTF-8 no BOM,
one sprite entry per line, ascending ASCII id order (index schema, spec 2.2).

Sheet packing: ascending ASCII id order with a shelf packer - shelf height =
entry cell-height, new shelf when the current row cannot fit the entry width
or the height changes (spec 2.1).

Self-grading: `--samples DIR [--count N] [--seed HEX]` composes N faces with a
Python mirror of the Java FaceGen algorithm (pinned SplitMix64, FACEGEN_SALT,
id-ordinal weighted pools, class gating - spec 4.2/4.3) at 3x zoom into a
contact sheet for visual review. Samples are review aids, never committed.
"""

import argparse
import json
import os
from PIL import Image

# --------------------------------------------------------------------------
# paths
# --------------------------------------------------------------------------

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(REPO, "content", "art", "faces")
PNG_PATH = os.path.join(OUT_DIR, "face-parts.png")
JSON_PATH = os.path.join(OUT_DIR, "face-parts-index.json")
ARCHETYPES_PATH = os.path.join(OUT_DIR, "face-archetypes.json")

T = 16            # cell edge, px
COLS = 16         # sheet grid width, cells
CANVAS = 48       # face canvas edge, px (3x3 cells)

# --------------------------------------------------------------------------
# MERCOLAS-24 (unified spec section 1.2)
# --------------------------------------------------------------------------

PAL = {
    "N0": (0x0D, 0x0B, 0x10), "G1": (0x2B, 0x2A, 0x31), "G2": (0x3F, 0x3E, 0x47),
    "G3": (0x57, 0x56, 0x5F), "G4": (0x75, 0x74, 0x7C), "B1": (0xC9, 0xC2, 0xB0),
    "B2": (0xE4, 0xDC, 0xC6), "V1": (0x16, 0x21, 0x1A), "V2": (0x26, 0x38, 0x2C),
    "V3": (0x3C, 0x52, 0x3E), "V4": (0x5A, 0x6E, 0x4C), "E1": (0x22, 0x1B, 0x14),
    "E2": (0x38, 0x2C, 0x1F), "E3": (0x53, 0x3F, 0x2B), "E4": (0x70, 0x57, 0x3A),
    "E5": (0x8E, 0x74, 0x52), "R1": (0x49, 0x17, 0x22), "R2": (0x7A, 0x1F, 0x26),
    "R3": (0xA7, 0x2C, 0x2A), "C1": (0x18, 0x24, 0x2F), "C2": (0x2B, 0x42, 0x57),
    "C3": (0x46, 0x70, 0x8A), "C4": (0x83, 0xA7, 0xB4), "Y1": (0xB9, 0x8F, 0x42),
}

# [shadow, base, light] ramps (spec 4.5, placeholder pending bless)
SKIN = {"pale": ("E4", "B1", "B2"), "tan": ("E3", "E5", "B1"), "dark": ("E1", "E3", "E4")}
HAIR = {"black": ("N0", "G1", "G2"), "brown": ("E1", "E2", "E3"),
        "grey": ("G2", "G3", "G4"), "white": ("G4", "B1", "B2"),
        "red": ("R1", "R2", "R3")}
HAIR_COLORS = ["black", "brown", "grey", "red", "white"]   # ascii order for ids

LINE_ART_KEYS = {"N0", "G1", "B2", "R1", "R2"}

# slot rects: name -> (x, y, wCells, hCells)  (spec 4.1)
SLOTS = {
    "base": (0, 0, 3, 3), "scar": (0, 0, 1, 1), "mouth": (8, 30, 2, 1),
    "nose": (16, 22, 1, 1), "eyes": (8, 16, 2, 1), "brow": (8, 8, 2, 1),
    "hair": (0, 0, 3, 2), "headwear": (0, 0, 3, 2), "named": (0, 0, 3, 3),
}

# scar anchor table (spec 4.4, placeholder): px on the 48x48 canvas
SCAR_ANCHORS = [(4, 22), (28, 22), (16, 10), (16, 36)]

# --------------------------------------------------------------------------
# deterministic randomness (the gen_custom_tiles.py machinery)
# --------------------------------------------------------------------------

def fnv1a32(s):
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


class Rng:
    def __init__(self, seed):
        self.s = seed if seed != 0 else 0x9E3779B9

    def u32(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x

    def chance(self, pct):
        return self.u32() % 100 < pct

    def rint(self, lo, hi):
        return lo + self.u32() % (hi - lo + 1)


# --------------------------------------------------------------------------
# canvas helpers - canvases are 48x48 lists of palette keys or None
# --------------------------------------------------------------------------

def canvas():
    return [[None] * CANVAS for _ in range(CANVAS)]


def put(g, x, y, c):
    if 0 <= x < CANVAS and 0 <= y < CANVAS:
        g[y][x] = c


def putm(g, x, y, c):
    """Plot + mirror across the canvas vertical centerline (x' = 47 - x)."""
    put(g, x, y, c)
    put(g, CANVAS - 1 - x, y, c)


def hline(g, x0, x1, y, c):
    for x in range(x0, x1 + 1):
        put(g, x, y, c)


def vline(g, x, y0, y1, c):
    for y in range(y0, y1 + 1):
        put(g, x, y, c)


def in_rect(x, y, rect):
    rx, ry, wc, hc = rect
    return rx <= x < rx + wc * T and ry <= y < ry + hc * T


def crop(g, rect):
    rx, ry, wc, hc = rect
    return [[g[ry + y][rx + x] for x in range(wc * T)] for y in range(hc * T)]


def paste(dst, src_rows, x0, y0):
    """Alpha-over src (key rows) onto dst canvas at (x0, y0)."""
    for y, row in enumerate(src_rows):
        for x, c in enumerate(row):
            if c is not None:
                put(dst, x0 + x, y0 + y, c)


# --------------------------------------------------------------------------
# head masks - three shapes, shared geometry constants
# --------------------------------------------------------------------------

CX = 24  # canvas centerline (between 23 and 24; symmetric plots use 47-x)


def head_mask(shape):
    """Set of (x, y) covered by the head (skull + jaw), per shape."""
    m = set()
    if shape == 0:                                   # round
        cx, cy, rx, ry = 23.5, 22.0, 12.0, 14.0
        for y in range(CANVAS):
            for x in range(CANVAS):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    m.add((x, y))
    elif shape == 1:                                 # gaunt
        cx, cy, rx, ry = 23.5, 22.0, 10.5, 13.0
        for y in range(CANVAS):
            for x in range(CANVAS):
                if ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1.0:
                    m.add((x, y))
        # long jaw: taper from cheekbones to a narrow chin at y=38
        for y in range(28, 39):
            half = max(3, 9 - (y - 28) * 6 // 10)
            for x in range(24 - half, 24 + half):
                m.add((x, y))
    else:                                            # square / heavy
        for y in range(9, 37):
            half = 12
            if y < 12:
                half = 9 + (y - 9)
            if y > 32:
                half = 12 - (y - 32)
            for x in range(24 - half, 24 + half):
                m.add((x, y))
    return m


def draw_base(shape, skin, rng):
    sh, base, light = SKIN[skin]
    g = canvas()
    m = head_mask(shape)
    chin_y = max(y for _, y in m)

    # neck + shoulders + plain garment band (dark, neutral - same for all)
    for y in range(chin_y - 2, 44):
        for x in range(19, 29):
            m.add((x, y))
    # fill + shade the head/neck mask
    for (x, y) in m:
        edge = any((x + dx, y + dy) not in m
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if edge:
            g[y][x] = "N0"
        else:
            g[y][x] = base
    # light: upper-left inner rim + brow/cheek highlight, dithered
    for (x, y) in m:
        if g[y][x] != base:
            continue
        inner_edge = any((x + dx, y + dy) in m and g[y + dy][x + dx] == "N0"
                         for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1),
                                        (1, 1), (-1, -1), (1, -1), (-1, 1)))
        if inner_edge and (x < 22 and y < 26) and ((x + y) & 1) == 0:
            g[y][x] = light
        elif inner_edge and (x > 26 or y > 30):
            if ((x + y) & 1) == 0:
                g[y][x] = sh
    # cheek + under-chin shading
    for y in range(chin_y - 4, chin_y + 1):
        for x in range(20, 28):
            if g[y][x] == base and ((x + y) & 1) == 0:
                g[y][x] = sh
    # subtle skin texture
    for _ in range(26):
        x, y = rng.rint(14, 33), rng.rint(12, 34)
        if g[y][x] == base and rng.chance(45):
            g[y][x] = light if rng.chance(35) else sh

    # ears: small bumps at the sides, y 20..25
    ear_x = min(x for x, y in m if 19 <= y <= 25) - 1
    for dy in range(6):
        y = 20 + dy
        w = 2 if 1 <= dy <= 4 else 1
        for dx in range(w):
            putm(g, ear_x - dx, y, base if dx == 0 else sh)
        putm(g, ear_x - w, y, "N0")
    putm(g, ear_x, 19, "N0")
    putm(g, ear_x, 26, "N0")
    putm(g, ear_x - 1, 22, sh)
    putm(g, ear_x - 1, 23, sh)

    # shoulders / garment: simple dark tunic so the portrait grounds
    for y in range(41, CANVAS):
        spread = (y - 41) * 3
        x0, x1 = max(10, 19 - spread), min(37, 28 + spread)
        for x in range(x0, x1 + 1):
            if g[y][x] is None or (g[y][x] not in ("N0",) and y >= 43):
                c = "G1" if ((x + y) & 3) else "G2"
                g[y][x] = c
    hline(g, max(10, 19 - 0), 28, 41, "N0")
    for y in range(41, CANVAS):
        spread = (y - 41) * 3
        put(g, max(10, 19 - spread), y, "N0")
        put(g, min(37, 28 + spread), y, "N0")
        if 19 - spread > 10:
            put(g, 19 - spread + 1, y - 1, "N0")
            put(g, 28 + spread - 1, y - 1, "N0")
    # collar notch
    vline(g, 23, 42, 45, "N0")
    vline(g, 24, 42, 45, "G2")
    return g


# --------------------------------------------------------------------------
# feature parts (line-art: N0/G1/B2/R1/R2 only)
# --------------------------------------------------------------------------

EYE_L = 18   # left eye center x (abs); right is mirrored


def draw_eyes(style, rng):
    g = canvas()
    y = 22
    if style == "round":
        for x in (EYE_L - 1, EYE_L, EYE_L + 1):
            putm(g, x, y - 1, "N0")
            putm(g, x, y + 1, "N0")
        putm(g, EYE_L - 2, y, "N0")
        putm(g, EYE_L + 2, y, "N0")
        putm(g, EYE_L - 1, y, "B2")
        putm(g, EYE_L, y, "N0")
        putm(g, EYE_L + 1, y, "B2")
    elif style == "narrow":
        for x in range(EYE_L - 2, EYE_L + 3):
            putm(g, x, y, "N0")
        putm(g, EYE_L, y, "N0")
        for x in range(EYE_L - 1, EYE_L + 2):
            putm(g, x, y + 1, "G1")
        putm(g, EYE_L, y - 1, "G1")
    elif style == "weary":
        for x in range(EYE_L - 2, EYE_L + 3):
            putm(g, x, y, "N0")
        putm(g, EYE_L, y, "B2")
        for x in range(EYE_L - 1, EYE_L + 2):
            putm(g, x, y + 2, "G1")
        putm(g, EYE_L - 2, y + 1, "G1")
        putm(g, EYE_L + 2, y + 1, "G1")
    elif style == "glint":
        for x in range(EYE_L - 2, EYE_L + 3):
            putm(g, x, y - 1, "N0")
            putm(g, x, y, "G1")
        putm(g, EYE_L, y, "B2")
        putm(g, EYE_L - 2, y + 1, "N0")
        putm(g, EYE_L + 2, y + 1, "N0")
    elif style == "squint":
        putm(g, EYE_L - 2, y + 1, "N0")
        putm(g, EYE_L - 1, y, "N0")
        putm(g, EYE_L, y, "N0")
        putm(g, EYE_L + 1, y - 1, "N0")
        putm(g, EYE_L + 2, y - 1, "N0")
        putm(g, EYE_L - 1, y + 1, "G1")
        putm(g, EYE_L, y + 1, "G1")
        putm(g, EYE_L + 1, y, "G1")
    else:  # hard
        for x in range(EYE_L - 2, EYE_L + 3):
            putm(g, x, y - 1, "N0")
        for x in range(EYE_L - 2, EYE_L + 1):
            putm(g, x, y, "N0")
        putm(g, EYE_L + 1, y, "G1")
        putm(g, EYE_L + 2, y, "B2")
    return g


def draw_brow(style, rng):
    g = canvas()
    y = 17
    if style == "flat":
        for x in range(EYE_L - 3, EYE_L + 3):
            putm(g, x, y, "N0")
    elif style == "heavy":
        for x in range(EYE_L - 3, EYE_L + 4):
            putm(g, x, y - 1, "N0")
            putm(g, x, y, "N0")
        putm(g, EYE_L + 4, y, "G1")
    elif style == "arch":
        putm(g, EYE_L - 3, y + 1, "N0")
        putm(g, EYE_L - 2, y, "N0")
        putm(g, EYE_L - 1, y - 1, "N0")
        putm(g, EYE_L, y - 1, "N0")
        putm(g, EYE_L + 1, y - 1, "N0")
        putm(g, EYE_L + 2, y, "N0")
        putm(g, EYE_L + 3, y + 1, "G1")
    else:  # scowl - inner ends dive toward the nose bridge
        putm(g, EYE_L - 3, y - 1, "N0")
        putm(g, EYE_L - 2, y - 1, "N0")
        putm(g, EYE_L - 1, y, "N0")
        putm(g, EYE_L, y, "N0")
        putm(g, EYE_L + 1, y + 1, "N0")
        putm(g, EYE_L + 2, y + 2, "N0")
        putm(g, EYE_L + 2, y + 1, "G1")
    return g


def draw_nose(style, rng):
    g = canvas()
    if style == "blunt":
        vline(g, 22, 28, 30, "G1")
        put(g, 22, 31, "N0")
        put(g, 23, 32, "N0")
        put(g, 24, 32, "N0")
        put(g, 25, 31, "N0")
        put(g, 25, 32, "G1")
        put(g, 21, 31, "G1")
        put(g, 26, 31, "G1")
    elif style == "long":
        vline(g, 22, 25, 31, "G1")
        put(g, 22, 32, "N0")
        put(g, 23, 33, "N0")
        put(g, 24, 33, "N0")
        put(g, 25, 32, "N0")
        put(g, 21, 30, "G1")
    elif style == "broken":
        put(g, 22, 25, "G1")
        put(g, 22, 26, "G1")
        put(g, 23, 27, "N0")
        put(g, 24, 28, "G1")
        put(g, 23, 29, "G1")
        put(g, 22, 30, "G1")
        put(g, 22, 31, "N0")
        put(g, 23, 32, "N0")
        put(g, 24, 32, "N0")
        put(g, 25, 31, "N0")
    else:  # hook
        put(g, 22, 25, "G1")
        put(g, 21, 26, "G1")
        put(g, 21, 27, "N0")
        put(g, 21, 28, "N0")
        put(g, 22, 29, "G1")
        put(g, 22, 30, "G1")
        put(g, 22, 31, "N0")
        put(g, 23, 32, "N0")
        put(g, 24, 32, "N0")
        put(g, 25, 31, "N0")
    return g


MOUTH_Y = 36


def draw_mouth_clean(style, rng):
    g = canvas()
    if style == "line":
        hline(g, 20, 27, MOUTH_Y, "N0")
        hline(g, 21, 26, MOUTH_Y + 1, "R1")
    elif style == "frown":
        put(g, 19, MOUTH_Y + 1, "N0")
        put(g, 20, MOUTH_Y + 1, "N0")
        hline(g, 21, 26, MOUTH_Y, "N0")
        put(g, 27, MOUTH_Y + 1, "N0")
        put(g, 28, MOUTH_Y + 1, "N0")
        hline(g, 22, 25, MOUTH_Y + 1, "G1")
    elif style == "smirk":
        put(g, 20, MOUTH_Y + 1, "N0")
        hline(g, 21, 25, MOUTH_Y, "N0")
        put(g, 26, MOUTH_Y - 1, "N0")
        put(g, 27, MOUTH_Y - 1, "N0")
        put(g, 27, MOUTH_Y - 2, "G1")
        hline(g, 22, 25, MOUTH_Y + 1, "R1")
    elif style == "tight":
        hline(g, 21, 26, MOUTH_Y, "N0")
        put(g, 20, MOUTH_Y, "G1")
        put(g, 27, MOUTH_Y, "G1")
        hline(g, 22, 25, MOUTH_Y - 1, "G1")
    else:  # open
        hline(g, 21, 26, MOUTH_Y - 1, "N0")
        put(g, 20, MOUTH_Y, "N0")
        hline(g, 21, 26, MOUTH_Y, "R1")
        put(g, 27, MOUTH_Y, "N0")
        hline(g, 21, 26, MOUTH_Y + 1, "N0")
    return g


def hair_fill(g, x, y, ramp, rng, edge_set):
    sh, base, light = ramp
    if (x, y) in edge_set:
        g[y][x] = "N0"
    elif ((x * 3 + y * 5) % 7) == 0:
        g[y][x] = light
    elif ((x + y) & 3) == 3 and rng.chance(60):
        g[y][x] = sh
    else:
        g[y][x] = base


def region_edges(pts):
    return {(x, y) for (x, y) in pts
            if any((x + dx, y + dy) not in pts
                   for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))}


def draw_mouth_beard(style, color, rng):
    ramp = HAIR[color]
    g = canvas()
    pts = set()
    if style == "moustache":
        for x in range(17, 31):
            for y in range(33, 36):
                if y == 33 and (x < 19 or x > 28):
                    continue
                pts.add((x, y))
        pts.add((16, 36)); pts.add((17, 36)); pts.add((30, 36)); pts.add((31, 36))
        pts.discard((23, 35)); pts.discard((24, 35))
    elif style == "beard_short":
        for x in range(17, 31):
            for y in range(37, 44):
                dx = abs(x - 23.5)
                if y >= 43 and dx > 3.5:
                    continue
                if y >= 41 and dx > 5.5:
                    continue
                if y <= 38 and dx < 3.5:
                    continue
                pts.add((x, y))
    else:  # beard_full: moustache + jaw wrap merged
        for x in range(15, 33):
            for y in range(33, 46):
                dx = abs(x - 23.5)
                if y <= 35 and (dx < 1.5 or dx > 6.5):
                    continue
                if y in (36, 37) and dx < 4.5:
                    continue
                if y >= 44 and dx > 3.5:
                    continue
                if y >= 41 and dx > 6.5:
                    continue
                if y <= 34 and dx > 5.5:
                    continue
                pts.add((x, y))
    edges = region_edges(pts)
    for (x, y) in sorted(pts, key=lambda p: (p[1], p[0])):
        hair_fill(g, x, y, ramp, rng, edges)
    if style == "moustache":
        hline(g, 21, 26, 37, "N0")     # mouth line under the moustache
    elif style == "beard_full":
        hline(g, 21, 26, 37, "N0")     # dark mouth gap
    else:
        hline(g, 20, 27, MOUTH_Y, "N0")
        hline(g, 21, 26, MOUTH_Y + 1, "R1")
    return g


def draw_hair(style, color, rng):
    ramp = HAIR[color]
    g = canvas()
    cx, cy, rx, ry = 23.5, 22.0, 12.0, 14.0    # canonical skull (round head)
    pts = set()

    def skull(x, y, grow=1.5):
        return ((x - cx) / (rx + grow)) ** 2 + ((y - cy) / (ry + grow)) ** 2 <= 1.0

    if style == "crop":
        for y in range(6, 16):
            for x in range(9, 39):
                if skull(x, y, 1.0) and (y <= 11 or abs(x - cx) > 8.5):
                    pts.add((x, y))
        for x in range(16, 32):                 # jagged hairline
            if ((x * 7) % 3) == 0:
                pts.add((x, 12))
    elif style == "curls":
        for y in range(4, 17):
            for x in range(8, 40):
                if skull(x, y, 2.5) and (y <= 12 or abs(x - cx) > 8.0):
                    pts.add((x, y))
        for bx in range(9, 39, 4):              # bumpy outline
            pts.add((bx, 4 if 14 <= bx <= 33 else 8))
            pts.add((bx + 1, 5 if 14 <= bx <= 33 else 9))
    elif style == "long":
        for y in range(5, 15):
            for x in range(8, 40):
                if skull(x, y, 2.0) and (y <= 12 or abs(x - cx) > 8.0):
                    pts.add((x, y))
        for y in range(12, 32):                 # side curtains
            for x in range(9, 14):
                pts.add((x, y))
                pts.add((47 - x, y))
    elif style == "shaved":
        for y in range(7, 15):                  # stubble arc, checker dither
            for x in range(11, 37):
                if skull(x, y, 0.5) and (y <= 10 or abs(x - cx) > 9.0) \
                        and ((x + y) & 1) == 0:
                    pts.add((x, y))
    elif style == "tuft":
        for y in range(12, 22):                 # temple patches
            for x in range(9, 16):
                if skull(x, y, 1.5):
                    pts.add((x, y))
                    pts.add((47 - x, y))
        for x in (16, 19, 23, 27, 31):          # thin top wisps
            pts.add((x, 8))
            pts.add((x, 7))
    else:  # tonsure - monk ring, bald crown
        for y in range(10, 21):
            for x in range(9, 17):
                if skull(x, y, 1.5) and abs(x - cx) > 7.5:
                    pts.add((x, y))
                    pts.add((47 - x, y))
        for x in range(14, 34):                 # ring closes low across the back
            if skull(x, 18, 1.0):
                pts.add((x, 18))
            if skull(x, 19, 1.0) and (x & 1):
                pts.add((x, 19))
    edges = region_edges(pts)
    for (x, y) in sorted(pts, key=lambda p: (p[1], p[0])):
        hair_fill(g, x, y, ramp, rng, edges)
    return g


def draw_headwear(kind, variant, rng):
    g = canvas()
    steel = ("N0", "G1", "G2", "C2")
    if kind == "hood":
        ramp = ("E1", "E2", "E3") if variant == 0 else ("G1", "G2", "G3")
        pts = set()
        peak = 2 if variant == 1 else 4
        for y in range(peak, 32):
            for x in range(7, 41):
                dx = abs(x - 23.5)
                lim = 17.0 - max(0, (14 - y)) * 1.1
                if dx > lim:
                    continue
                if 14 <= y and dx < 9.0 and y >= 15:
                    continue                     # face opening
                if y < 14 and dx < 7.0 and y > 10:
                    continue
                pts.add((x, y))
        if variant == 1:
            for i in range(3):                   # pointed peak
                for x in range(22 - i, 26 + i):
                    pts.add((x, 2 + i))
        edges = region_edges(pts)
        for (x, y) in sorted(pts, key=lambda p: (p[1], p[0])):
            hair_fill(g, x, y, ramp, rng, edges)
        for y in range(11, 32):                  # opening shadow rim
            for x in range(14, 34):
                dx = abs(x - 23.5)
                if 8.0 <= dx <= 9.0 and y >= 14:
                    put(g, x, y, "N0")
        for x in range(16, 32):
            dx = abs(x - 23.5)
            if dx < 8.0:
                put(g, x, 13, "N0")
                if rng.chance(50):
                    put(g, x, 14, "G1" if variant == 1 else "E1")
    elif kind == "open_helm":
        sh, base, mid, glint = steel
        if variant == 0:                         # kettle helm
            for y in range(4, 13):
                for x in range(12, 36):
                    dx = abs(x - 23.5)
                    if ((dx / 12.0) ** 2 + ((y - 13) / 9.0) ** 2) <= 1.0:
                        g[y][x] = base
            for x in range(9, 39):               # brim
                g[13][x] = base
                g[14][x] = sh
            for x in range(9, 39):
                g[12][x] = mid if (x % 3) else base
            hline(g, 9, 38, 15, "N0")
            for (x, y) in [(15, 7), (16, 6), (17, 6), (18, 7)]:
                g[y][x] = "C2"
        else:                                    # nasal helm
            for y in range(4, 15):
                for x in range(13, 35):
                    dx = abs(x - 23.5)
                    if ((dx / 11.0) ** 2 + ((y - 14) / 10.0) ** 2) <= 1.0:
                        g[y][x] = base
            for x in range(13, 35):
                dx = abs(x - 23.5)
                if ((dx / 11.0) ** 2 + ((15 - 14) / 10.0) ** 2) <= 1.0:
                    g[15][x] = sh
            vline(g, 23, 15, 26, base)           # nasal bar
            vline(g, 24, 15, 26, sh)
            vline(g, 22, 15, 25, "N0")
            vline(g, 25, 15, 25, "N0")
            for (x, y) in [(16, 7), (17, 6), (18, 6)]:
                g[y][x] = "C2"
        # outline pass
        outline_opaque(g)
        # rivets
        for x in (14, 23, 33):
            put(g, x, 5 + (variant * 2), "C2" if g[5 + variant * 2][x] else None)
    elif kind == "closed_helm":
        sh, base, mid, glint = steel
        for y in range(3, 32):
            for x in range(12, 36):
                dx = abs(x - 23.5)
                lim = 11.5 if y > 8 else 11.5 - (8 - y) * 0.9
                if dx <= lim:
                    g[y][x] = base
        for y in range(3, 32):                   # vertical crest / plate seams
            if g[y][23] :
                g[y][23] = mid
            if variant == 1 and g[y][17]:
                g[y][17] = sh if y % 2 else base
            if variant == 1 and g[y][30]:
                g[y][30] = sh if y % 2 else base
        hline(g, 15, 22, 21, "N0")               # eye slit
        hline(g, 25, 32, 21, "N0")
        hline(g, 15, 22, 22, "N0")
        hline(g, 25, 32, 22, "N0")
        if variant == 0:
            for y in range(26, 30):              # breath holes
                for x in range(20, 28, 3):
                    put(g, x, y, "N0" if (x + y) % 2 else g[y][x])
        else:
            hline(g, 19, 28, 27, sh)
            hline(g, 19, 28, 29, sh)
        for (x, y) in [(16, 6), (17, 5), (18, 5), (15, 8)]:
            g[y][x] = "C2"
        outline_opaque(g)
    elif kind == "coif":
        sh, base, mid = "G1", "G2", "G3"
        pts = set()
        for y in range(5, 32):
            for x in range(10, 38):
                dx = abs(x - 23.5)
                lim = 13.5 if y > 12 else 13.5 - (12 - y) * 1.0
                if dx > lim:
                    continue
                if y >= 15 and dx < 8.5:
                    continue                     # face opening
                if 11 <= y < 15 and dx < 7.5:
                    continue
                pts.add((x, y))
        edges = region_edges(pts)
        for (x, y) in sorted(pts, key=lambda p: (p[1], p[0])):
            if (x, y) in edges:
                g[y][x] = "N0"
            else:                                # mail rings: dense checker
                g[y][x] = mid if ((x + y) & 1) == variant else (base if (x & 1) else sh)
    else:  # cowl - deep, face in shadow
        ramp = ("V1", "V2", "V3") if variant == 0 else ("N0", "G1", "G2")
        pts = set()
        for y in range(3, 32):
            for x in range(6, 42):
                dx = abs(x - 23.5)
                lim = 18.0 - max(0, (15 - y)) * 1.2
                if dx > lim:
                    continue
                if y >= 17 and dx < 8.5:
                    continue
                pts.add((x, y))
        edges = region_edges(pts)
        for (x, y) in sorted(pts, key=lambda p: (p[1], p[0])):
            hair_fill(g, x, y, ramp, rng, edges)
        for y in range(15, 24):                  # overhang shadow across the brow
            for x in range(16, 32):
                dx = abs(x - 23.5)
                if dx < 8.5 and y <= 18:
                    put(g, x, y, "N0")
                elif dx < 8.5 and y <= 20 and ((x + y) & 1) == 0:
                    put(g, x, y, "N0")
    return g


def outline_opaque(g):
    """1px N0 outline around every opaque region on the canvas."""
    solid = {(x, y) for y in range(CANVAS) for x in range(CANVAS) if g[y][x]}
    for (x, y) in region_edges(solid):
        g[y][x] = "N0"


def draw_scar(style, rng):
    g = canvas()
    if style == "slash":
        for i in range(8):
            put(g, 4 + i, 3 + i, "R2")
            if i in (2, 5):
                put(g, 5 + i, 3 + i, "R1")
        put(g, 5, 3, "R1")
        put(g, 11, 10, "R1")
    elif style == "cross":
        for i in range(6):
            put(g, 4 + i, 4 + i, "R2")
            put(g, 9 - i, 4 + i, "R1")
    else:  # notch
        hline(g, 4, 9, 7, "R1")
        put(g, 6, 6, "R2")
        put(g, 7, 8, "R2")
        put(g, 10, 7, "N0")
    return g


# --------------------------------------------------------------------------
# named portraits (spec 4.7; FACES-SPEC 2.1/2.2 stand as authoring references)
# --------------------------------------------------------------------------

def draw_named_devin():
    """Devin - canon: dark skin, white hair (Flame side-effect), young, earnest.
    Invented visuals (FACES-SPEC 2.1, placeholder): short tight white curls,
    wide alert eyes, apprentice robe collar with clasp."""
    rng = Rng(fnv1a32("face_named_devin#art"))
    g = draw_base(0, "dark", Rng(fnv1a32("face_named_devin#base")))
    paste(g, [row[:] for row in draw_hair("curls", "white", rng)], 0, 0)
    paste(g, draw_eyes("round", rng), 0, 0)
    paste(g, draw_brow("flat", rng), 0, 0)
    paste(g, draw_nose("blunt", rng), 0, 0)
    paste(g, draw_mouth_clean("smirk", rng), 0, 0)
    # apprentice robe collar: lighter band + center clasp (Y1, the sole warm accent)
    for y in range(42, 45):
        for x in range(13, 35):
            if g[y][x] in ("G1", "G2"):
                g[y][x] = "G3" if ((x + y) & 1) else "G2"
    put(g, 23, 43, "Y1")
    put(g, 24, 43, "Y1")
    put(g, 23, 42, "N0")
    put(g, 24, 42, "N0")
    return g


def draw_named_john():
    """Minister John - canon: old, poring over manuscripts; ALL visuals invented
    (FACES-SPEC 2.2, placeholder, flagged for Eli): bald dome, grey temple
    tufts, reading squint, long nose, heavy grey moustache over a downturned
    mouth, full beard, high Divine Light vestment collar."""
    rng = Rng(fnv1a32("face_named_john#art"))
    g = draw_base(1, "pale", Rng(fnv1a32("face_named_john#base")))
    gsh, gbase, glight = HAIR["grey"]
    # temple tufts hugging the gaunt skull sides
    tufts = set()
    for y in range(11, 17):
        for x in range(14, 19):
            if abs(x - 16) + abs(y - 13) <= 3:
                tufts.add((x, y))
                tufts.add((47 - x, y))
    edges = region_edges(tufts)
    for (x, y) in sorted(tufts, key=lambda p: (p[1], p[0])):
        hair_fill(g, x, y, HAIR["grey"], rng, edges)
    # brow wrinkles on the bald dome
    for x in (19, 22, 25, 28):
        put(g, x, 12, "E4")
        put(g, x + 1, 10, "E4")
    # reading squint: heavy angled slits + bags + crow's feet
    for dx in range(4):
        putm(g, 16 + dx, 22 - (dx // 2), "N0")
    putm(g, 16, 23, "G1")
    putm(g, 17, 23, "G1")
    putm(g, 18, 24, "G1")
    putm(g, 15, 21, "G1")
    # flat stern brow
    for x in range(15, 21):
        putm(g, x, 18, "N0")
    # long drooping nose
    paste(g, draw_nose("long", rng), 0, 0)
    # heavy grey moustache over a downturned mouth
    mo = set((x, y) for x in range(18, 30) for y in range(32, 35)
             if not (y == 32 and (x < 20 or x > 27)))
    edges = region_edges(mo)
    for (x, y) in sorted(mo, key=lambda p: (p[1], p[0])):
        hair_fill(g, x, y, HAIR["grey"], rng, edges)
    put(g, 17, 35, gbase)
    put(g, 30, 35, gbase)
    hline(g, 21, 26, 36, "N0")                   # downturned mouth
    put(g, 20, 37, "N0")
    put(g, 27, 37, "N0")
    # full beard, ending above the collar
    bd = set((x, y) for x in range(17, 31) for y in range(37, 43)
             if not (y >= 41 and abs(x - 23.5) > 4.5)
             and not (y == 37 and 20 <= x <= 27))
    edges = region_edges(bd)
    for (x, y) in sorted(bd, key=lambda p: (p[1], p[0])):
        hair_fill(g, x, y, HAIR["grey"], rng, edges)
    # high Divine Light vestment collar: bone band, dark trim, B2 studs
    for y in range(43, 47):
        for x in range(15, 33):
            g[y][x] = "B1" if 44 <= y <= 45 else "G2"
    hline(g, 15, 32, 43, "N0")
    hline(g, 15, 32, 46, "N0")
    vline(g, 15, 43, 46, "N0")
    vline(g, 32, 43, 46, "N0")
    for x in range(17, 32, 3):
        put(g, x, 44, "B2")
    return g


# --------------------------------------------------------------------------
# part inventory (spec 4.5 minima; weights are index data)
# --------------------------------------------------------------------------

def build_parts():
    """Returns list of dicts: id, slot, w, h, weight, tags, canvas."""
    parts = []

    def add(pid, slot, weight, tags, g):
        rect = SLOTS[slot]
        parts.append(dict(id=pid, slot=slot, w=rect[2], h=rect[3],
                          weight=weight, tags=tags, canvas=g))

    skin_weight = {"pale": 12, "tan": 20, "dark": 8}
    for skin in ("dark", "pale", "tan"):
        for shape in range(3):
            pid = f"face_base_{skin}_{shape}"
            add(pid, "base", skin_weight[skin], ["face_base", f"skin_{skin}"],
                draw_base(shape, skin, Rng(fnv1a32(pid))))

    for style, weight, tags in (("arch", 10, ["fine"]), ("flat", 20, []),
                                ("heavy", 12, []), ("scowl", 12, ["grim"])):
        pid = f"face_brow_{style}"
        add(pid, "brow", weight, ["face_brow"] + tags,
            draw_brow(style, Rng(fnv1a32(pid))))

    for style, weight, tags in (("glint", 8, ["grim"]), ("hard", 12, ["grim"]),
                                ("narrow", 15, []), ("round", 20, []),
                                ("squint", 10, []), ("weary", 10, [])):
        pid = f"face_eyes_{style}"
        add(pid, "eyes", weight, ["face_eyes"] + tags,
            draw_eyes(style, Rng(fnv1a32(pid))))

    hair_weight = {"crop": 20, "curls": 12, "long": 10, "shaved": 10,
                   "tonsure": 4, "tuft": 8}
    for style in ("crop", "curls", "long", "shaved", "tonsure", "tuft"):
        for color in HAIR_COLORS:
            pid = f"face_hair_{style}_{color}"
            add(pid, "hair", hair_weight[style],
                ["face_hair", f"hair_{color}"],
                draw_hair(style, color, Rng(fnv1a32(pid))))

    for kind in ("closed_helm", "coif", "cowl", "hood", "open_helm"):
        for variant in range(2):
            pid = f"face_headwear_{kind}_{variant}"
            add(pid, "headwear", 10, ["face_headwear", f"hw_{kind}"],
                draw_headwear(kind, variant, Rng(fnv1a32(pid))))

    beard_weight = {"beard_full": 10, "beard_short": 12, "moustache": 10}
    for style in ("beard_full", "beard_short"):
        for color in HAIR_COLORS:
            pid = f"face_mouth_{style}_{color}"
            add(pid, "mouth", beard_weight[style],
                ["face_mouth", f"hair_{color}"],
                draw_mouth_beard(style, color, Rng(fnv1a32(pid))))
    for style, weight, tags in (("frown", 12, ["grim"]), ("line", 20, [])):
        pid = f"face_mouth_{style}"
        add(pid, "mouth", weight, ["face_mouth"] + tags,
            draw_mouth_clean(style, Rng(fnv1a32(pid))))
    for color in HAIR_COLORS:
        pid = f"face_mouth_moustache_{color}"
        add(pid, "mouth", beard_weight["moustache"],
            ["face_mouth", f"hair_{color}"],
            draw_mouth_beard("moustache", color, Rng(fnv1a32(pid))))
    for style, weight, tags in (("open", 4, []), ("smirk", 8, ["fine"]),
                                ("tight", 12, ["grim"])):
        pid = f"face_mouth_{style}"
        add(pid, "mouth", weight, ["face_mouth"] + tags,
            draw_mouth_clean(style, Rng(fnv1a32(pid))))

    add("face_named_devin", "named", 1, ["face_named"], draw_named_devin())
    add("face_named_john", "named", 1, ["face_named"], draw_named_john())

    for style, weight, tags in (("blunt", 20, []), ("broken", 8, ["scarred"]),
                                ("hook", 10, []), ("long", 12, [])):
        pid = f"face_nose_{style}"
        add(pid, "nose", weight, ["face_nose"] + tags,
            draw_nose(style, Rng(fnv1a32(pid))))

    for style, weight in (("cross", 1), ("notch", 2), ("slash", 3)):
        pid = f"face_scar_{style}"
        add(pid, "scar", weight, ["face_scar", "scarred"],
            draw_scar(style, Rng(fnv1a32(pid))))

    parts.sort(key=lambda p: p["id"])
    return parts


# --------------------------------------------------------------------------
# validation
# --------------------------------------------------------------------------

LINE_ART_SLOTS = {"eyes", "brow", "nose", "scar"}


def validate_parts(parts):
    errors = []
    seen = set()
    for p in parts:
        pid = p["id"]
        if pid in seen:
            errors.append(f"{pid}: duplicate id")
        seen.add(pid)
        if not all(c.islower() or c.isdigit() or c == "_" for c in pid):
            errors.append(f"{pid}: id not [a-z0-9_]+")
        rect = SLOTS[p["slot"]]
        opaque = 0
        for y in range(CANVAS):
            for x in range(CANVAS):
                c = p["canvas"][y][x]
                if c is None:
                    continue
                opaque += 1
                if c not in PAL:
                    errors.append(f"{pid}: unknown palette key {c} at ({x},{y})")
                if not in_rect(x, y, rect):
                    errors.append(f"{pid}: pixel outside slot rect at ({x},{y})")
                if p["slot"] in LINE_ART_SLOTS and c not in LINE_ART_KEYS:
                    errors.append(f"{pid}: non-line-art key {c} at ({x},{y})")
                if (p["slot"] == "mouth" and "hair_" not in " ".join(p["tags"])
                        and c not in LINE_ART_KEYS):
                    errors.append(f"{pid}: clean mouth non-line-art key {c}")
        if opaque == 0:
            errors.append(f"{pid}: fully transparent")
    return errors


# --------------------------------------------------------------------------
# sheet packing + emission
# --------------------------------------------------------------------------

def pack(parts):
    """Shelf packer per spec 2.1: ascending id order (already sorted)."""
    placements = {}
    col = 0
    row = 0
    shelf_h = None
    for p in parts:
        w, h = p["w"], p["h"]
        if shelf_h is None:
            shelf_h = h
        if h != shelf_h or col + w > COLS:
            row += shelf_h
            col = 0
            shelf_h = h
        placements[p["id"]] = (col, row)
        col += w
    rows = row + (shelf_h or 0)
    return placements, rows


def emit_png(parts, placements, rows):
    img = Image.new("RGBA", (COLS * T, rows * T), (0, 0, 0, 0))
    px = img.load()
    for p in parts:
        col, row = placements[p["id"]]
        rect = SLOTS[p["slot"]]
        cropped = crop(p["canvas"], rect)
        for y, r in enumerate(cropped):
            for x, c in enumerate(r):
                if c is not None:
                    px[col * T + x, row * T + y] = PAL[c] + (255,)
    return img


def emit_json(parts, placements, rows):
    out = []
    a = out.append
    a("{")
    a('  "schemaVersion": 1,')
    a('  "provenance": "Generated by tools/scripts/gen_face_parts.py - do not hand-edit; '
      "rerun the generator (byte-identical output). GRANADAD ART UNIFIED SPEC v1 section 4 / "
      "FACES-SPEC.md (amended 2026-07-13: medium changed to tile parts, composition logic "
      "retained) / DECISIONS.md Art register row (Eli 2026-07-13, fourth revision, pillar 4). "
      'Original pixel art, MERCOLAS-24 palette, zero copied pixels.",')
    a('  "notes": "Face-part pools are tag queries over this index (spec 2.3: all-of tag '
      "match, id-ordinal order). Integration phase may merge these entries into the shared "
      "content/art/sprites/sprite-index.json; the schema is identical (spec 2.2). Weights "
      'feed FaceGen\'s archetype-multiplied weighted pick (spec 4.5/4.6).",')
    a('  "sheet": "art/faces/face-parts.png",')
    a('  "tilePx": %d,' % T)
    a('  "columns": %d, "rows": %d,' % (COLS, rows))
    a('  "sprites": [')
    for i, p in enumerate(parts):
        col, row = placements[p["id"]]
        fields = ['"id": "%s"' % p["id"], '"cell": [%d, %d]' % (col, row)]
        if p["w"] != 1 or p["h"] != 1:
            fields.append('"w": %d, "h": %d' % (p["w"], p["h"]))
        if p["weight"] != 1:
            fields.append('"weight": %d' % p["weight"])
        fields.append('"tags": [%s]' % ", ".join('"%s"' % t for t in p["tags"]))
        comma = "," if i < len(parts) - 1 else ""
        a("    { %s }%s" % (", ".join(fields), comma))
    a("  ]")
    a("}")
    a("")
    return "\n".join(out)


# --------------------------------------------------------------------------
# sample compositor - Python mirror of the Java FaceGen (spec 4.2/4.3)
# --------------------------------------------------------------------------

M64 = (1 << 64) - 1
FACEGEN_SALT = 0x4641434547454E31          # "FACEGEN1" (FACES-SPEC 4.2, retained)
HEADWEAR_CLASSES = ["BARE", "HOOD", "OPEN_HELM", "CLOSED_HELM", "COIF", "COWL"]
HAIR_CLASS_WEIGHTS = [("BLACK", 5), ("BROWN", 6), ("GREY", 4), ("WHITE", 2), ("RED", 1)]


def mix64(z):
    z &= M64
    z ^= z >> 30
    z = (z * 0xBF58476D1CE4E5B9) & M64
    z ^= z >> 27
    z = (z * 0x94D049BB133111EB) & M64
    z ^= z >> 31
    return z


def weighted_pick(pool, draw):
    """pool: [(key, weight)] in canonical order; integer cumulative pick."""
    total = sum(w for _, w in pool)
    r = draw % total
    cum = 0
    for key, w in pool:
        cum += w
        if r < cum:
            return key
    raise AssertionError("unreachable")


def effective_pool(parts_by_id, tag_query, multipliers, color_class=None):
    pool = []
    for pid in sorted(parts_by_id):
        p = parts_by_id[pid]
        if not set(tag_query) <= set(p["tags"]):
            continue
        part_colors = [t for t in p["tags"] if t.startswith("hair_")]
        if color_class is not None and part_colors:
            if f"hair_{color_class.lower()}" not in part_colors:
                continue
        w = p["weight"]
        for t in p["tags"]:
            w *= multipliers.get(t, 1)
        if w > 0:
            pool.append((pid, w))
    return pool


def compose_face(parts_by_id, archetype, world_seed, actor_id):
    base = mix64((mix64((world_seed ^ FACEGEN_SALT) & M64) + actor_id) & M64)

    def draw(k):
        return mix64((base + k) & M64)

    hw = weighted_pick([(c, archetype["headwearWeights"].get(c, 0))
                        for c in HEADWEAR_CLASSES
                        if archetype["headwearWeights"].get(c, 0) > 0], draw(0))
    color = weighted_pick(HAIR_CLASS_WEIGHTS, draw(8))
    mult = archetype.get("tagMultipliers", {})
    placed = []

    def pick(k, query, color_filtered=False, hw_tag=None):
        q = list(query)
        if hw_tag:
            q.append(hw_tag)
        pool = effective_pool(parts_by_id, q, mult,
                              color if color_filtered else None)
        return weighted_pick(pool, draw(k))

    placed.append((pick(1, ["face_base"]), 0, 0))
    n_scars_r = draw(9) % 16
    n_scars = 0 if n_scars_r < 13 else (1 if n_scars_r < 15 else 2)
    for i in range(n_scars):
        sid = pick(10 + 2 * i, ["face_scar"])
        ax, ay = SCAR_ANCHORS[draw(11 + 2 * i) % 4]
        placed.append((sid, ax, ay))
    placed.append((pick(5, ["face_mouth"], color_filtered=True), 8, 30))
    placed.append((pick(4, ["face_nose"]), 16, 22))
    placed.append((pick(2, ["face_eyes"]), 8, 16))
    placed.append((pick(3, ["face_brow"], color_filtered=True), 8, 8))
    if hw == "BARE":
        placed.append((pick(6, ["face_hair"], color_filtered=True), 0, 0))
    else:
        placed.append((pick(7, ["face_headwear"],
                            hw_tag=f"hw_{hw.lower()}"), 0, 0))
    return placed


def raster_face(parts, placed):
    parts_by_id = {p["id"]: p for p in parts}
    face = canvas()
    for pid, ax, ay in placed:
        p = parts_by_id[pid]
        paste(face, crop(p["canvas"], SLOTS[p["slot"]]), ax, ay)
    return face


def write_samples(parts, out_dir, count, world_seed):
    with open(ARCHETYPES_PATH, encoding="utf-8") as f:
        arch_doc = json.load(f)
    archetypes = arch_doc["archetypes"]
    parts_by_id = {p["id"]: p for p in parts}
    names = sorted(archetypes)
    zoom = 3
    pad = 4
    cols = 6
    rows_n = (count + cols - 1) // cols
    img = Image.new("RGBA", (cols * (CANVAS * zoom + pad) + pad,
                             rows_n * (CANVAS * zoom + pad) + pad),
                    PAL["G1"] + (255,))
    px = img.load()
    print("samples (worldSeed=0x%X):" % world_seed)
    for i in range(count):
        actor_id = 1000 + i * 37
        arch_name = names[i % len(names)]
        placed = compose_face(parts_by_id, archetypes[arch_name],
                              world_seed, actor_id)
        face = raster_face(parts, placed)
        ox = pad + (i % cols) * (CANVAS * zoom + pad)
        oy = pad + (i // cols) * (CANVAS * zoom + pad)
        for y in range(CANVAS):
            for x in range(CANVAS):
                c = face[y][x]
                rgba = (PAL[c] + (255,)) if c else (PAL["G2"] + (255,))
                for dy in range(zoom):
                    for dx in range(zoom):
                        px[ox + x * zoom + dx, oy + y * zoom + dy] = rgba
        print("  [%2d] actor=%d arch=%-8s %s"
              % (i, actor_id, arch_name, " ".join(pid for pid, _, _ in placed)))
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "face-samples.png")
    img.save(path, "PNG", optimize=False, compress_level=9)
    print("wrote " + path)


def write_part_gallery(parts, out_dir):
    """Every part at 3x on gray, one per slot row group - authoring aid."""
    zoom = 3
    pad = 4
    cols = 10
    rows_n = (len(parts) + cols - 1) // cols
    cell = CANVAS * zoom + pad
    img = Image.new("RGBA", (cols * cell + pad, rows_n * cell + pad),
                    PAL["G2"] + (255,))
    px = img.load()
    for i, p in enumerate(parts):
        ox = pad + (i % cols) * cell
        oy = pad + (i // cols) * cell
        for y in range(CANVAS):
            for x in range(CANVAS):
                c = p["canvas"][y][x]
                if c is None:
                    continue
                for dy in range(zoom):
                    for dx in range(zoom):
                        px[ox + x * zoom + dx, oy + y * zoom + dy] = PAL[c] + (255,)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "face-parts-gallery.png")
    img.save(path, "PNG", optimize=False, compress_level=9)
    order = [p["id"] for p in parts]
    print("gallery order (%d parts): %s" % (len(order), " ".join(order)))
    print("wrote " + path)


# --------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--samples", metavar="DIR", help="write a composed-face contact sheet")
    ap.add_argument("--gallery", metavar="DIR", help="write an all-parts gallery sheet")
    ap.add_argument("--count", type=int, default=12)
    ap.add_argument("--seed", default="5EEDF00D")
    args = ap.parse_args()

    parts = build_parts()
    errors = validate_parts(parts)
    if errors:
        for e in errors[:60]:
            print("FAIL " + e)
        raise SystemExit(1)
    placements, rows = pack(parts)
    img = emit_png(parts, placements, rows)

    if args.gallery:
        write_part_gallery(parts, args.gallery)
    if args.samples:
        write_samples(parts, args.samples, args.count, int(args.seed, 16))
    if args.gallery or args.samples:
        return

    os.makedirs(OUT_DIR, exist_ok=True)
    img.save(PNG_PATH, "PNG", optimize=False, compress_level=9)
    with open(JSON_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(emit_json(parts, placements, rows))
    print("ok: %d parts, %d cells, sheet %dx%d px"
          % (len(parts), sum(p["w"] * p["h"] for p in parts), COLS * T, rows * T))
    print("wrote " + PNG_PATH)
    print("wrote " + JSON_PATH)


if __name__ == "__main__":
    main()
