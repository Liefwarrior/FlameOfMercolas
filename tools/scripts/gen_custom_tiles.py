#!/usr/bin/env python3
"""gen_custom_tiles.py - emit content/art/custom/tiles.png + art-mapping.json.

The "Granadad register" original pixel-art pack (GRANADAD ART UNIFIED SPEC v1
2026-07-13; DECISIONS.md Art register row, Eli 2026-07-13 FOURTH revision:
flat per-z-slice rendering RETAINED, the third revision's faux-3D pillar
RESCINDED — no composite/projection features anywhere in this pack). Pillars,
inspiration only, zero copied pixels:
  1. MERCOLAS-24 palette in the He Is Coming register (muted grays, dark
     greens, blood-red accents, ominous minimalism).
  2. Earthbound-bar per-tile texture: no flat fills, 2x2-checker dither
     between adjacent ramp shades, hash-scatter drift, characterful variants.
Region roles under the flat view model: floor (plan), face (WALL TEXTURE —
the lit rim + N0 contact-shadow framing is retained purely so a wall cell
reads as solid mass in flat top-down; explicitly not a projection feature,
no composite pass ever draws face+top together), ramp (north-light gradient
+ chevrons), stair (4 treads). `<mat>.top` regions stay on the sheet as
reserved/unmapped inventory (unreferenced regions are legal, TILE-ART-SPEC
section 7.2).

Deterministic: byte-identical rerun. No timestamps, no `random` module. All
stochastic texture decisions come from xorshift32 streams seeded by FNV-1a-32:
per-variant details from "{regionName}#{variantIndex}" (the spec's per-cell
stream), plus one variant-SHARED stream per region, "{regionName}#structure",
for the structural layer (seams / courses / joints). Sharing the structural
layer across a region's variants is what guarantees cross-variant seamless
tiling (any 2x2 arrangement of variants shows no seam); it is a deliberate,
flagged refinement of the spec's single-stream wording.

Both output files are emitted by this one script so the sheet and the mapping
can never drift. PNG: optimize=False, compress_level=9, no ancillary chunks.
JSON: indent-2 style, LF newlines, UTF-8 no BOM, fixed template order
(regions serialized compactly, one region per line, for reviewability).
"""

import os
from PIL import Image

# --------------------------------------------------------------------------
# paths
# --------------------------------------------------------------------------

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(REPO, "content", "art", "custom")
PNG_PATH = os.path.join(OUT_DIR, "tiles.png")
JSON_PATH = os.path.join(OUT_DIR, "art-mapping.json")

T = 16                     # tile edge, px
COLS, ROWS = 16, 20        # sheet grid (256 x 320 px)

# --------------------------------------------------------------------------
# MERCOLAS-24 (UNIFIED SPEC section 1.2). Sole off-palette exception, forever:
# the reserved `missing` checker.
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
MISSING_A = (0xFF, 0x00, 0xFF)
MISSING_B = (0x00, 0x00, 0x00)

# [shadow, base, mid, light] per material key (UNIFIED SPEC section 1.3).
# Bucket ramps are keyed "<mat>#a<bucket>".
RAMPS = {
    "granite":                    ("N0", "G1", "G2", "G3"),
    "reman_concrete":             ("G2", "G3", "G4", "B1"),
    "steel":                      ("N0", "G1", "G2", "C2"),
    "brick":                      ("N0", "R1", "R2", "G2"),
    "dirt":                       ("N0", "E1", "E2", "E3"),
    "ash":                        ("G1", "G2", "G3", "B1"),
    "oak":                        ("E1", "E2", "E3", "E4"),
    "trudgeon_wood":              ("N0", "E1", "E2", "R1"),
    "trudgeon_wood@getilia_soak": ("V1", "E1", "E2", "V3"),
    "thatch":                     ("E3", "E4", "E5", "B1"),
    "leather":                    ("N0", "E1", "E2", "E4"),
    "cloth":                      ("E2", "E3", "E5", "G3"),
    "glowstone":                  ("N0", "R1", "R2", "R3"),
    "chromatis":                  ("C2", "G3", "C4", "B1"),
    "chromatis#a1":               ("E4", "B1", "B2", "Y1"),
    "chromatis#a2":               ("E3", "E4", "Y1", "B2"),
    "chromatis_melt":             ("E3", "E4", "Y1", "B2"),
    "phorys":                     ("G1", "G2", "C3", "C4"),
    "lightstone":                 ("V1", "V2", "V3", "G3"),
    "lightstone#a1":              ("V1", "V2", "V3", "V4"),
    "lightstone#a2":              ("V2", "V3", "V4", "B2"),
    "lightstone_shards":          ("N0", "V2", "V4", "B2"),
    "ice":                        ("C2", "C3", "C4", "B2"),
    "water":                      ("C1", "C2", "C3", "C4"),
}

MATS = [
    "ash", "brick", "chromatis", "chromatis_melt", "cloth", "dirt", "glowstone",
    "granite", "ice", "leather", "lightstone", "lightstone_shards", "oak",
    "phorys", "reman_concrete", "steel", "thatch", "trudgeon_wood",
    "trudgeon_wood@getilia_soak",
]

ROLE_VARIANTS = {"floor": 4, "face": 3, "top": 3, "ramp": 2, "stair": 2}

# --------------------------------------------------------------------------
# deterministic randomness
# --------------------------------------------------------------------------

def fnv1a32(s):
    h = 0x811C9DC5
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


class Rng:
    """xorshift32; seed 0 promoted so the stream never sticks."""

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

    def pick(self, seq):
        return seq[self.u32() % len(seq)]


# --------------------------------------------------------------------------
# grid helpers (tiles are 16x16 arrays of ramp indices 0..3)
# --------------------------------------------------------------------------

def grid(fill=1):
    return [[fill] * T for _ in range(T)]


def scatter(g, r, pct, frm, to, x0=0, y0=0, x1=15, y1=15):
    """Hash-scatter noise between ADJACENT ramp shades (rule 3), raster order."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            hit = r.chance(pct)          # consume uniformly for determinism
            if hit and g[y][x] == frm:
                g[y][x] = to


def dline_h(g, r, y, idx, pct=100, x0=0, x1=15):
    for x in range(x0, x1 + 1):
        if r.chance(pct):
            g[y % T][x % T] = idx


def dline_v(g, r, x, idx, pct=100, y0=0, y1=15):
    for y in range(y0, y1 + 1):
        if r.chance(pct):
            g[y % T][x % T] = idx


def checker_band(g, x0, y0, x1, y1, idx, phase=0):
    """2x2-block checkerboard overlay (rule 3's dither convention)."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if ((x // 2 + y // 2) & 1) == phase:
                g[y % T][x % T] = idx


def partition(r, total, lo, hi):
    sizes = []
    left = total
    while left > 0:
        s = r.rint(lo, hi)
        if 0 < left - s < lo:
            s = left
        s = min(s, left)
        sizes.append(s)
        left -= s
    return sizes


def blob(g, r, cx, cy, w, h, idx):
    """Rough dithered ellipse-ish blob centred at (cx, cy), clipped to tile."""
    for dy in range(-(h // 2), h - h // 2):
        for dx in range(-(w // 2), w - w // 2):
            x, y = cx + dx, cy + dy
            if not (0 <= x < T and 0 <= y < T):
                continue
            edge = abs(dx) * 2 >= w - 1 or abs(dy) * 2 >= h - 1
            if edge and not r.chance(55):
                continue
            g[y][x] = idx


def crack(g, r, idx, length, x=None, y=None):
    """Short 1-px random-walk crack, interior only."""
    x = x if x is not None else r.rint(2, 13)
    y = y if y is not None else r.rint(2, 13)
    for _ in range(length):
        g[y][x] = idx
        x = min(14, max(1, x + r.rint(-1, 1)))
        y = min(14, max(1, y + r.rint(-1, 1)))


def wobble16(r, amp=1):
    """Per-column offset table, |W| <= amp, W[0] = 0 (wrap-safe: the boundary
    jump equals |W[15]| <= amp, no worse than an interior step)."""
    w = [0] * T
    for i in range(1, T):
        w[i] = min(amp, max(-amp, w[i - 1] + r.rint(-1, 1)))
    return w


def bump(idx):
    return idx + 1 if idx < 3 else 2


def lower(idx):
    return idx - 1 if idx > 0 else 1


# --------------------------------------------------------------------------
# structural motifs (variant-shared; drawn from the "#structure" stream)
# Floors/tops wrap on BOTH axes; faces (rows 2..14) wrap horizontally only.
# --------------------------------------------------------------------------

def st_flagstones(g, r, band_lo=4, band_hi=6, stone_lo=4, stone_hi=6,
                  mid_pct=35, seam_pct=70, seam_idx=0):
    y = 0
    for bh in partition(r, T, band_lo, band_hi):
        off = r.rint(0, 15)
        x = off
        for sw in partition(r, T, stone_lo, stone_hi):
            shade = 2 if r.chance(mid_pct) else 1
            for dy in range(bh - 1):
                for dx in range(1, sw):
                    g[(y + dy) % T][(x + dx) % T] = shade
            dline_v(g, r, x % T, seam_idx, seam_pct, y, y + bh - 2)
            x += sw
        dline_h(g, r, (y + bh - 1) % T, seam_idx, seam_pct)
        y += bh


def st_slabs(g, r):
    """reman_concrete floor: clean 8x8 slabs, hairline dashed seams."""
    ox, oy = r.rint(0, 7), r.rint(0, 7)
    for k in range(2):
        dline_v(g, r, (ox + 8 * k) % T, 0, 45)
        dline_h(g, r, (oy + 8 * k) % T, 0, 45)
    # faint corner shading inside each slab
    for by in range(2):
        for bx in range(2):
            x0, y0 = (ox + 8 * bx + 1) % T, (oy + 8 * by + 1) % T
            for d in range(3):
                if r.chance(60):
                    g[y0 % T][(x0 + d) % T] = 2
                if r.chance(60):
                    g[(y0 + d) % T][x0 % T] = 2


def st_plates(g, r):
    """steel floor: riveted 8x8 plates; rivets = light pixels near seams."""
    ox, oy = r.rint(0, 7), r.rint(0, 7)
    for k in range(2):
        dline_v(g, r, (ox + 8 * k) % T, 0, 85)
        dline_h(g, r, (oy + 8 * k) % T, 0, 85)
    for by in range(2):
        for bx in range(2):
            rx, ry = (ox + 8 * bx + 2) % T, (oy + 8 * by + 2) % T
            g[ry][rx] = 3
            g[(ry + 4) % T][(rx + 4) % T] = 3
    # brushed streaks
    for _ in range(4):
        yy = r.rint(0, 15)
        xx = r.rint(0, 15)
        for d in range(r.rint(3, 6)):
            if g[yy][(xx + d) % T] == 1 and r.chance(70):
                g[yy][(xx + d) % T] = 2


def st_basket(g, r):
    """brick floor: 4x4 woven paver blocks (herringbone read at 16 px;
    true 4x2 herringbone does not tile a 16-torus cleanly - flagged
    deviation). Mortar = light slot (G2), shadows at intersections."""
    for by in range(4):
        for bx in range(4):
            x0, y0 = bx * 4, by * 4
            horiz = (bx + by) % 2 == 0
            for d in range(2):
                shade = 2 if r.chance(35) else 1
                for a in range(4):
                    for b in range(2):
                        if horiz:
                            g[y0 + d * 2 + b][x0 + a] = shade
                        else:
                            g[y0 + a][x0 + d * 2 + b] = shade
            # mortar seams: block border + the mid seam
            if horiz:
                dline_h(g, r, y0 + 2, 3, 75, x0, x0 + 3)
                dline_h(g, r, y0, 3, 75, x0, x0 + 3)
                dline_v(g, r, x0, 3, 55, y0, y0 + 3)
            else:
                dline_v(g, r, x0 + 2, 3, 75, y0, y0 + 3)
                dline_v(g, r, x0, 3, 75, y0, y0 + 3)
                dline_h(g, r, y0, 3, 55, x0, x0 + 3)
            g[y0][x0] = 0


def st_planks_h(g, r, widths=(4, 4, 4, 4), grain_pct=25, end_pct=70):
    """oak/trudgeon floor: horizontal planks, shadow gaps, staggered ends."""
    y = 0
    for i, wdt in enumerate(widths):
        dline_h(g, r, (y + wdt - 1) % T, 0, 78)
        ex = (i * 7 + r.rint(0, 8)) % T
        dline_v(g, r, ex, 0, end_pct, y, y + wdt - 2)
        for dy in range(wdt - 1):
            xx = r.rint(0, 15)
            for d in range(r.rint(3, 7)):
                if r.chance(grain_pct + 40):
                    yy = (y + dy) % T
                    if g[yy][(xx + d) % T] == 1:
                        g[yy][(xx + d) % T] = 2
        y += wdt


def st_boards_v(g, r, w=4, y0=2, y1=14, grain=True):
    """oak/trudgeon face: vertical boards with wobbly grain."""
    off = r.rint(0, w - 1)
    for x in range(off % w, T, w):
        dline_v(g, r, x, 0, 85, y0, y1)
    if grain:
        for x in range(T):
            if x % w == off % w:
                continue
            yy = y0 + r.rint(0, 4)
            while yy <= y1:
                if r.chance(45) and g[yy][x] == 1:
                    g[yy][x] = 2
                yy += r.rint(2, 4)


def st_courses(g, r, course_h=4, y0=2, y1=14, joint_period=8, joint_pct=80,
               line_idx=0, joint_idx=0, mid_pct=30):
    """granite/reman face: horizontal courses + staggered vertical joints."""
    yy = y0
    ci = 0
    while yy <= y1:
        ye = min(yy + course_h - 1, y1)
        dline_h(g, r, ye, line_idx, 95)
        joff = (ci * (joint_period // 2) + r.rint(0, 2)) % joint_period
        for x in range(joff, T, joint_period):
            dline_v(g, r, x, joint_idx, joint_pct, yy, ye - 1)
            # per-block shade
        x = joff
        while x < joff + T:
            if r.chance(mid_pct):
                for bx in range(x + 1, min(x + joint_period, joff + T)):
                    for by in range(yy, ye):
                        if g[by][bx % T] == 1:
                            g[by][bx % T] = 2
            x += joint_period
        yy = ye + 1
        ci += 1


def st_running_bond(g, r, y0=2, y1=14, course_h=3):
    """brick face: running-bond courses, mortar (light G2) lines."""
    yy = y0
    ci = 0
    while yy <= y1:
        ye = min(yy + course_h - 1, y1)
        dline_h(g, r, ye, 3, 80)
        joff = (4 if ci % 2 else 0) + r.rint(0, 1)
        for x in range(joff % 8, T, 8):
            dline_v(g, r, x, 3, 75, yy, ye - 1)
        # occasional lighter / darker brick
        x = joff % 8
        while x < 16 + joff % 8:
            if r.chance(30):
                for bx in range(x + 1, x + 8):
                    for by in range(yy, ye):
                        if g[by][bx % T] == 1:
                            g[by][bx % T] = 2
            x += 8
        # thin shadow under each mortar line (depth)
        if ye + 1 <= y1:
            dline_h(g, r, ye + 1 if False else ye, 3, 0)  # no-op keeps stream stable
        yy = ye + 1
        ci += 1


def st_strata(g, r, y0=0, y1=15, gap=(3, 4), line_idx=0, amp=1, pct=70):
    """dirt/ash/glowstone faces: wavy horizontal strata lines."""
    w = wobble16(r, amp)
    yy = y0 + r.rint(1, gap[1])
    while yy <= y1:
        for x in range(T):
            y = yy + w[x]
            if y0 <= y <= y1 and r.chance(pct):
                g[y][x] = line_idx
        yy += r.rint(gap[0], gap[1])


def st_speckle(g, r, pct_dark=10, pct_mid=16, y0=0, y1=15):
    scatter(g, r, pct_dark, 1, 0, 0, y0, 15, y1)
    scatter(g, r, pct_mid, 1, 2, 0, y0, 15, y1)


def st_weave(g, r):
    """cloth floor: 2x2 warp/weft checker, some blocks reverted for budget."""
    for by in range(8):
        for bx in range(8):
            mid = (bx + by) % 2 == 0 and not r.chance(30)
            if mid:
                for dy in range(2):
                    for dx in range(2):
                        g[by * 2 + dy][bx * 2 + dx] = 2
    # thread shadows along block seams, sparse
    for k in range(4):
        dline_h(g, r, k * 4 + 1, 0, 12)
        dline_v(g, r, k * 4 + 3, 0, 12)


def st_folds(g, r, y0=2, y1=14):
    """cloth face: hanging vertical fold bands, checker-dithered edges."""
    x = r.rint(0, 3)
    shade = 1
    while x < T + 4:
        wdt = r.rint(2, 4)
        for dx in range(wdt):
            xx = (x + dx) % T
            for y in range(y0, y1 + 1):
                g[y][xx] = shade
        # dither the band edge
        for y in range(y0, y1 + 1):
            if (y // 2) & 1 == 0:
                g[y][x % T] = 2 if shade == 1 else 1
        if shade == 1 and r.chance(35):
            dline_v(g, r, (x + wdt) % T, 0, 60, y0 + 2, y1 - 1)
        shade = 2 if shade == 1 else 1
        x += wdt


def st_facets(g, r, block=8):
    """chromatis floor/top: X-lattice triangular facets, light edges."""
    for y in range(T):
        for x in range(T):
            lx, ly = x % block, y % block
            if lx == ly or lx + ly == block - 1:
                g[y][x] = 3
            else:
                bx, by = x // block, y // block
                # quadrant: 0 top, 1 right, 2 bottom, 3 left
                if ly < lx and ly < block - 1 - lx:
                    q = 0
                elif lx >= ly and lx >= block - 1 - ly:
                    q = 1
                elif ly > lx:
                    q = 2 if ly > block - 1 - lx else 3
                else:
                    q = 3
                g[y][x] = 1 if (q + bx + by + (1 if r.chance(0) else 0)) % 2 == 0 else 2
    # shadow points at lattice junctions
    for by in range(T // block):
        for bx in range(T // block):
            cx, cy = bx * block + block // 2, by * block + block // 2
            g[cy][cx - 1] = 0
            g[by * block][bx * block] = 0


def st_prisms(g, r, y0=2, y1=14):
    """chromatis face: vertical prism strips, light rims, diagonal breaks."""
    off = r.rint(0, 3)
    for i in range(4):
        x0 = (off + i * 4) % T
        shade = 1 if i % 2 == 0 else 2
        brk = y0 + 2 + (i * 3 + r.rint(0, 2)) % 8
        for y in range(y0, y1 + 1):
            s = shade if y < brk else (2 if shade == 1 else 1)
            for dx in range(1, 4):
                g[y][(x0 + dx) % T] = s
        dline_v(g, r, x0, 3, 90, y0, y1)
        g[brk][(x0 + 2) % T] = 3
        g[brk][(x0 + 1) % T] = 3


def st_ripples(g, r, cycle=(1, 1, 2, 1), light_every=8):
    """chromatis_melt floor/top: wavy flow bands, gold mid ripples."""
    w = wobble16(r, 2)
    for y in range(T):
        for x in range(T):
            band = (y + w[x]) % 4
            g[y][x] = cycle[band]
            if (y + w[x]) % light_every == 0 and x % 3 != 2:
                g[y][x] = 3
    scatter(g, r, 6, 1, 0)


def st_sags(g, r, y0=2, y1=14, n=2, idx=0):
    """melt/leather face: sagging catenary curves across the body."""
    for k in range(n):
        base = y0 + 2 + k * ((y1 - y0 - 2) // max(1, n))
        depth = r.rint(1, 2)
        for x in range(T):
            # crude catenary: deepest mid-span, pinned at x=0 (wrap-safe)
            d = depth if 3 <= x <= 12 else (depth - 1 if 1 <= x <= 14 else 0)
            y = base + d
            if y0 <= y <= y1 and r.chance(85):
                g[y][x] = idx
            # light stretch highlight above the curve
            if y - 1 >= y0 and r.chance(20) and g[y - 1][x] == 1:
                g[y - 1][x] = 2


def st_thatch_floor(g, r):
    for _ in range(24):
        x, y = r.rint(1, 13), r.rint(1, 14)
        ln = r.rint(2, 3)
        idx = 2 if r.chance(70) else (3 if r.chance(50) else 0)
        dy = r.pick((0, 0, 1, -1))
        for d in range(ln):
            yy = y + (dy if d == ln - 1 else 0)
            if 0 <= yy < T and 0 <= x + d < T:
                g[yy][x + d] = idx


def st_thatch_face(g, r, y0=2, y1=14):
    for x in range(T):
        yy = y0 + r.rint(0, 2)
        shade = 2 if x % 2 == 0 else 1
        while yy <= y1:
            ln = r.rint(2, 4)
            for d in range(ln):
                if yy + d <= y1:
                    g[yy + d][x] = shade
            yy += ln + 1
    # bound cord band at rows 7-8
    dline_h(g, r, 7, 0, 95)
    dline_h(g, r, 8, 0, 75)
    for x in range(0, T, 3):
        g[8][x] = 2


def st_thatch_top(g, r):
    for x in range(T):
        shade = 2 if x % 2 == 0 else 1
        yy = r.rint(0, 2)
        while yy < T:
            ln = r.rint(3, 5)
            for d in range(ln):
                if yy + d < T:
                    g[(yy + d) % T][x] = shade
            if r.chance(45) and yy < T:
                g[yy][x] = 3       # lit strand tip
            yy += ln + 1


def st_rowlock(g, r):
    """brick top: rowlock courses - tight 2-px header rows."""
    off = r.rint(0, 1)
    for y in range(T):
        if (y + off) % 3 == 2:
            dline_h(g, r, y, 3, 80)
    ci = 0
    for y in range(T):
        if (y + off) % 3 == 0:
            joff = (ci * 2) % 4
            for x in range(joff, T, 4):
                dline_v(g, r, x, 3, 70, y, min(15, y + 1))
            ci += 1
    scatter(g, r, 18, 1, 2)


def st_ice(g, r):
    c1, c2 = r.rint(0, 15), r.rint(0, 15)
    for y in range(T):
        for x in range(T):
            if (x + y) % T == c1 and x % 3 != 2:
                g[y][x] = 2
            if (x + y) % T == (c1 + 1) % T and x % 4 == 1:
                g[y][x] = 0
            if (x - y) % T == c2 and x % 3 != 1:
                g[y][x] = 2
    scatter(g, r, 8, 1, 2)
    scatter(g, r, 5, 1, 0)


def st_ice_face(g, r, y0=2, y1=14):
    off = r.rint(0, 3)
    for x in range(T):
        if (x + off) % 4 == 0:
            dline_v(g, r, x, 2, 70, y0, y1)
        elif (x + off) % 4 == 2 and r.chance(60):
            dline_v(g, r, x, 0, 35, y0 + 2, y1)
    scatter(g, r, 6, 1, 2, 0, y0, 15, y1)


def st_phorys(g, r, y0=0, y1=15):
    scatter(g, r, 10, 1, 0, 0, y0, 15, y1)
    scatter(g, r, 8, 1, 2 if False else 0, 0, y0, 15, y1)  # extra dark grain


def st_water(g, r):
    for y in range(T):
        for x in range(T):
            d = (x + y) % 8
            if d in (0, 1) and ((x // 2 + y // 2) & 1) == 0:
                g[y][x] = 0            # trough, 2x2-checker dithered
            elif d == 4 and x % 4 != 3:
                g[y][x] = 2            # crest line, dashed


# --------------------------------------------------------------------------
# per-variant detail passes (from the "{region}#{variant}" stream)
# --------------------------------------------------------------------------

def dt_noise(pct=8, frm=1, to=2):
    def f(g, r):
        scatter(g, r, pct, frm, to)
    return f


def dt_stone(chips=3, cracks=1, noise=8):
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for _ in range(cracks):
            crack(g, r, 0, r.rint(3, 5))
        for _ in range(chips):
            g[r.rint(1, 14)][r.rint(1, 14)] = 3
    return f


def dt_knots(n=2, noise=6):
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for _ in range(n):
            x, y = r.rint(2, 13), r.rint(2, 13)
            for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
                g[y + dy][x + dx] = 2
            g[y][x] = 0
    return f


def dt_wood_accent(n=2, accent_pct=4, noise=6):
    """trudgeon woods: sparse light-slot grain accent runs."""
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for _ in range(n):
            x, y = r.rint(1, 10), r.rint(1, 14)
            for d in range(r.rint(3, 5)):
                if r.chance(80) and g[y][min(14, x + d)] != 0:
                    g[y][min(14, x + d)] = 3
    return f


def dt_sheen(pct=8, noise=5):
    """getilia soak: green sheen dither along plank centres."""
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for y in range(1, 15, 2):
            for x in range(1, 15):
                if r.chance(pct) and g[y][x] == 2:
                    g[y][x] = 3
    return f


def dt_pebbles(n=3, noise=14):
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for _ in range(n):
            x, y = r.rint(1, 13), r.rint(1, 13)
            g[y][x] = 2
            g[y][x + 1] = 2
            g[y + 1][x] = 3 if r.chance(40) else 2
    return f


def dt_ash(n=2, noise=10, fleck=6):
    def f(g, r):
        scatter(g, r, 7, 1, 0)
        scatter(g, r, noise, 1, 2)
        for _ in range(n):
            blob(g, r, r.rint(3, 12), r.rint(3, 12), r.rint(4, 6), r.rint(3, 4), 2)
        for y in range(1, 15):
            for x in range(1, 15):
                if r.chance(fleck) and g[y][x] == 2:
                    g[y][x] = 3
    return f


def dt_glow(nodules=2, cores=5, noise=8):
    def f(g, r):
        scatter(g, r, noise, 1, 0)
        scatter(g, r, noise, 1, 2)
        for _ in range(nodules):
            x, y = r.rint(2, 12), r.rint(2, 12)
            blob(g, r, x, y, 3, 3, 2)
        placed = 0
        for _ in range(cores * 3):
            if placed >= cores:
                break
            x, y = r.rint(1, 14), r.rint(1, 14)
            if g[y][x] == 2:
                g[y][x] = 3
                placed += 1
    return f


def dt_facet(noise=5, glints=3):
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for _ in range(glints):
            x, y = r.rint(1, 14), r.rint(1, 14)
            if g[y][x] == 3:
                continue
            if r.chance(50):
                g[y][x] = 0
    return f


def dt_veins(n=2, sparkle=4, noise=6):
    def f(g, r):
        scatter(g, r, noise, 1, 0)
        spark = 0
        for _ in range(n):
            x = r.rint(2, 13)
            for y in range(1, 15):
                g[y][x] = 2
                if spark < sparkle and r.chance(18):
                    g[y][x] = 3
                    spark += 1
                x = min(14, max(1, x + r.rint(-1, 1)))
    return f


def dt_nodules(n=3, noise=8):
    def f(g, r):
        scatter(g, r, noise, 1, 0)
        scatter(g, r, 12, 1, 2)
        for _ in range(n):
            x, y = r.rint(2, 12), r.rint(2, 12)
            blob(g, r, x, y, 4, 4, 2)
            g[y][x] = 3
            if r.chance(60):
                g[y][x + 1] = 3
    return f


def dt_shards(n=12, noise=18):
    def f(g, r):
        scatter(g, r, noise, 1, 0)
        scatter(g, r, 10, 1, 2)
        for _ in range(n):
            x, y = r.rint(1, 14), r.rint(1, 14)
            g[y][x] = 2
            if r.chance(35):
                g[y][min(14, x + 1)] = 3
    return f


def dt_glints(n=4, noise=6):
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for _ in range(n):
            g[r.rint(1, 14)][r.rint(1, 14)] = 3
    return f


def dt_stitches(noise=7):
    """leather: light stitch dots along seam pixels."""
    def f(g, r):
        scatter(g, r, noise, 1, 2)
        for y in range(1, 15):
            for x in range(1, 15):
                if g[y][x] == 0 and (x + y) % 3 == 0 and r.chance(45):
                    g[y][x] = 3
    return f


def dt_water(sparkle=4):
    def f(g, r):
        # drift the crest dashes + place sparkle
        for _ in range(6):
            x, y = r.rint(1, 14), r.rint(1, 14)
            if g[y][x] == 2:
                g[y][x] = 1
        placed = 0
        for _ in range(sparkle * 4):
            if placed >= sparkle:
                break
            x, y = r.rint(2, 13), r.rint(2, 13)
            if g[y][x] == 1 and (x + y) % 8 in (3, 4, 5):
                g[y][x] = 3
                placed += 1
    return f


# --------------------------------------------------------------------------
# motif registry: material -> (floor_struct, floor_detail, face_struct,
#                              face_detail, top_struct(optional))
# Face structs draw rows 2..14 only (rim + contact shadow added by the role).
# --------------------------------------------------------------------------

def face_body(fn, **kw):
    def f(g, r):
        fn(g, r, **{"y0": 2, "y1": 14, **kw})
    return f


MOTIFS = {
    "granite": dict(
        floor=lambda g, r: st_flagstones(g, r, seam_pct=58),
        floor_d=dt_stone(chips=3, cracks=1, noise=8),
        face=face_body(st_courses, course_h=4),
        face_d=dt_stone(chips=2, cracks=1, noise=8),
    ),
    "reman_concrete": dict(
        floor=st_slabs,
        floor_d=dt_stone(chips=2, cracks=1, noise=12),
        face=face_body(st_courses, course_h=5, joint_pct=55, mid_pct=20),
        face_d=dt_noise(10, 1, 2),
    ),
    "steel": dict(
        floor=st_plates,
        floor_d=dt_noise(10, 1, 2),
        face=face_body(st_courses, course_h=13, joint_period=8, joint_pct=90,
                       mid_pct=25),
        face_d=lambda g, r: (scatter(g, r, 8, 1, 2),
                             [dline_v(g, r, x, 3, 60, 4, 12)
                              for x in (3, 11)] and None),
    ),
    "brick": dict(
        floor=st_basket,
        floor_d=dt_noise(7, 1, 2),
        face=face_body(st_running_bond),
        face_d=dt_noise(7, 1, 2),
        top=st_rowlock,
    ),
    "dirt": dict(
        floor=lambda g, r: st_speckle(g, r, 8, 16),
        floor_d=dt_pebbles(n=3, noise=6),
        face=face_body(st_strata, gap=(3, 4), amp=1),
        face_d=dt_noise(12, 1, 2),
    ),
    "ash": dict(
        floor=lambda g, r: None,
        floor_d=dt_ash(n=2, noise=10, fleck=6),
        face=face_body(st_strata, gap=(3, 5), amp=1, pct=60),
        face_d=dt_noise(10, 1, 2),
    ),
    "oak": dict(
        floor=lambda g, r: st_planks_h(g, r, (4, 4, 4, 4)),
        floor_d=dt_knots(n=2, noise=5),
        face=st_boards_v,
        face_d=dt_knots(n=1, noise=5),
    ),
    "trudgeon_wood": dict(
        floor=lambda g, r: st_planks_h(g, r, (5, 5, 6), grain_pct=35),
        floor_d=dt_wood_accent(n=2, noise=6),
        face=lambda g, r: st_boards_v(g, r, w=4),
        face_d=dt_wood_accent(n=2, noise=7),
    ),
    "trudgeon_wood@getilia_soak": dict(
        floor=lambda g, r: st_planks_h(g, r, (5, 5, 6), grain_pct=35),
        floor_d=dt_sheen(pct=9, noise=5),
        face=lambda g, r: st_boards_v(g, r, w=4),
        face_d=dt_sheen(pct=9, noise=5),
    ),
    "thatch": dict(
        floor=st_thatch_floor,
        floor_d=dt_noise(8, 1, 2),
        face=st_thatch_face,
        face_d=dt_noise(6, 1, 2),
        top=st_thatch_top,
    ),
    "leather": dict(
        floor=lambda g, r: st_flagstones(g, r, band_lo=5, band_hi=6,
                                         stone_lo=5, stone_hi=7, mid_pct=28,
                                         seam_pct=62),
        floor_d=dt_stitches(noise=7),
        face=face_body(st_sags, n=2),
        face_d=dt_noise(9, 1, 2),
    ),
    "cloth": dict(
        floor=st_weave,
        floor_d=dt_glints(n=2, noise=4),
        face=st_folds,
        face_d=dt_noise(6, 1, 2),
    ),
    "glowstone": dict(
        floor=lambda g, r: st_speckle(g, r, 9, 10),
        floor_d=dt_glow(nodules=2, cores=5, noise=6),
        face=face_body(st_strata, gap=(3, 4), amp=1),
        face_d=dt_glow(nodules=2, cores=6, noise=5),
    ),
    "chromatis": dict(
        floor=st_facets,
        floor_d=dt_facet(noise=5, glints=3),
        face=st_prisms,
        face_d=dt_facet(noise=4, glints=2),
    ),
    "chromatis_melt": dict(
        floor=st_ripples,
        floor_d=dt_noise(6, 1, 2),
        face=face_body(st_sags, n=3, idx=2),
        face_d=dt_noise(8, 1, 2),
    ),
    "phorys": dict(
        floor=st_phorys,
        floor_d=dt_veins(n=2, sparkle=4, noise=5),
        face=face_body(st_phorys),
        face_d=dt_veins(n=2, sparkle=3, noise=5),
    ),
    "lightstone": dict(
        floor=lambda g, r: None,
        floor_d=dt_nodules(n=3, noise=8),
        face=face_body(st_strata, gap=(4, 5), amp=1, pct=55),
        face_d=dt_nodules(n=2, noise=6),
    ),
    "lightstone_shards": dict(
        floor=lambda g, r: None,
        floor_d=dt_shards(n=12, noise=18),
        face=face_body(st_strata, gap=(4, 5), amp=1, pct=55),
        face_d=dt_shards(n=9, noise=14),
    ),
    "ice": dict(
        floor=st_ice,
        floor_d=dt_glints(n=5, noise=11),
        face=st_ice_face,
        face_d=dt_glints(n=3, noise=4),
    ),
}


# --------------------------------------------------------------------------
# role grammar (flat view model — see module docstring)
# --------------------------------------------------------------------------

def apply_face_frame(g):
    """Lit rim rows 0-1 (row 1 checker-dithered to hold the light budget) and
    the solid-N0 contact-shadow row 15 (the CT ground line)."""
    for x in range(T):
        g[0][x] = 3
        if (x // 2) & 1 == 0:
            g[1][x] = 3
        elif g[1][x] == 0:
            g[1][x] = 1
    for x in range(T):
        g[15][x] = 0


def apply_ramp(g, r, vi):
    """North-light gradient (3 dithered bands) + up-slope chevrons."""
    for y in range(T):
        for x in range(T):
            if y <= 4:
                g[y][x] = min(3, g[y][x] + 1)
            elif y == 5:
                if ((x // 2 + y // 2) & 1) == 0:
                    g[y][x] = min(3, g[y][x] + 1)
            elif y == 11:
                if ((x // 2 + y // 2) & 1) == 0:
                    g[y][x] = max(0, g[y][x] - 1)
            elif y >= 12:
                g[y][x] = max(0, g[y][x] - 1)
    for cx in (5 + vi, 11 - vi):
        pts = ((cx, 6), (cx - 1, 7), (cx + 1, 7), (cx - 2, 8), (cx + 2, 8))
        for x, y in pts:
            g[y][x % T] = 0


def apply_stair(g, r):
    """Four treads: top row light, bottom row shadow."""
    for ty in range(0, T, 4):
        for x in range(T):
            g[ty][x] = 3
            g[ty + 3][x] = 0
    # wear: break the light rows a little
    for ty in range(0, T, 4):
        for _ in range(3):
            g[ty][r.rint(1, 14)] = 2


# --------------------------------------------------------------------------
# no-flat-fill enforcement (rule 1): no monochrome axis-aligned rectangle
# larger than 5x4 (i.e. none at >=6x5 or >=5x6).
# --------------------------------------------------------------------------

def find_flat_rect(g):
    for (mw, mh) in ((6, 5), (5, 6)):
        for y0 in range(T - mh + 1):
            for x0 in range(T - mw + 1):
                c = g[y0][x0]
                if all(g[y0 + dy][x0 + dx] == c
                       for dy in range(mh) for dx in range(mw)):
                    return x0, y0, mw, mh, c
    return None


def enforce_no_flat(g, r):
    for _ in range(64):
        hit = find_flat_rect(g)
        if hit is None:
            return
        x0, y0, mw, mh, c = hit
        cx, cy = x0 + mw // 2, y0 + mh // 2
        g[cy][cx] = bump(c) if c < 3 else lower(c)
        if r.chance(50):
            g[y0 + 1][x0 + 1] = bump(c) if c < 3 else lower(c)


# --------------------------------------------------------------------------
# region inventory + rendering
# --------------------------------------------------------------------------

def region_inventory():
    """name -> (kind, material, rampKey, variantCount)."""
    inv = {}
    for m in MATS:
        for role, count in ROLE_VARIANTS.items():
            inv[f"{m}.{role}"] = (role, m, m, count)
    for m in ("chromatis", "lightstone"):
        for b in (1, 2):
            inv[f"{m}.face.a{b}"] = ("face", m, f"{m}#a{b}", 3)
            inv[f"{m}.floor.a{b}"] = ("floor", m, f"{m}#a{b}", 4)
            inv[f"{m}.top.a{b}"] = ("top", m, f"{m}#a{b}", 3)
    inv["water"] = ("water", None, "water", 4)
    inv["missing"] = ("missing", None, None, 1)
    return inv


def render_region(name, kind, mat, count):
    """Returns list of 16x16 ramp-index grids (or RGB grids for missing)."""
    if kind == "missing":
        cell = [[MISSING_A if ((x // 8 + y // 8) & 1) == 0 else MISSING_B
                 for x in range(T)] for y in range(T)]
        return [cell], True

    s = Rng(fnv1a32(name + "#structure"))
    base = grid(1)
    mo = MOTIFS.get(mat)
    if kind == "water":
        st_water(base, s)
    elif kind in ("floor", "ramp", "stair"):
        mo["floor"](base, s)
    elif kind == "top":
        (mo.get("top") or mo["floor"])(base, s)
    elif kind == "face":
        mo["face"](base, s)

    tiles = []
    for vi in range(count):
        v = Rng(fnv1a32(f"{name}#{vi}"))
        g = [row[:] for row in base]
        if kind == "water":
            dt_water()(g, v)
        elif kind in ("floor", "ramp", "stair"):
            mo["floor_d"](g, v)
        elif kind == "top":
            mo["floor_d"](g, v)
        elif kind == "face":
            mo["face_d"](g, v)

        if kind == "face":
            apply_face_frame(g)
        elif kind == "ramp":
            apply_ramp(g, v, vi)
        elif kind == "stair":
            apply_stair(g, v)

        enforce_no_flat(g, v)
        tiles.append(g)
    return tiles, False


def to_rgb(g, ramp_key, top=False):
    ramp = RAMPS[ramp_key]
    view = (ramp[0], ramp[2], ramp[3], ramp[3]) if top else ramp
    return [[PAL[view[g[y][x]]] for x in range(T)] for y in range(T)]


# --------------------------------------------------------------------------
# sheet + mapping emission
# --------------------------------------------------------------------------

def build_sheet():
    inv = region_inventory()
    names = sorted(inv)            # ascending ASCII byte order (sheet layout rule)
    img = Image.new("RGB", (COLS * T, ROWS * T), PAL["N0"])
    px = img.load()
    cells = {}                     # region -> [(col,row), ...]
    idx = 0
    rendered = {}                  # (col,row) -> (name, vi, grid or None)
    for name in names:
        kind, mat, ramp_key, count = inv[name]
        tiles, is_rgb = render_region(name, kind, mat, count)
        coords = []
        for vi, g in enumerate(tiles):
            col, row = idx % COLS, idx // COLS
            coords.append([col, row])
            rgb = g if is_rgb else to_rgb(g, ramp_key, top=(kind == "top"))
            for y in range(T):
                for x in range(T):
                    px[col * T + x, row * T + y] = rgb[y][x]
            rendered[(col, row)] = (name, vi, None if is_rgb else g)
            idx += 1
        cells[name] = coords
    assert idx == 311, f"expected 311 cells, packed {idx}"
    return img, cells, names, inv, rendered


LIGHT_TINT_Q8 = [36, 36, 36, 38, 39, 41, 44, 47, 50, 54, 58, 63, 68, 74, 80,
                 87, 94, 102, 110, 118, 127, 136, 146, 157, 167, 179, 190,
                 202, 215, 228, 242, 256]


def material_forms_json(m):
    def arr(role):
        if m in ("chromatis", "lightstone"):
            return [f"{m}.{role}", f"{m}.{role}.a1", f"{m}.{role}.a2"]
        return [f"{m}.{role}"]

    lines = []
    lines.append('        "block": { "byAppearance": %s },' % jarr(arr("face")))
    lines.append('        "wall":  { "byAppearance": %s },' % jarr(arr("face")))
    lines.append('        "floor": { "byAppearance": %s },' % jarr(arr("floor")))
    lines.append('        "ramp":  { "byAppearance": ["%s.ramp"] },' % m)
    lines.append('        "stair": { "byAppearance": ["%s.stair"] }' % m)
    return lines


def jarr(strings):
    return "[" + ", ".join('"%s"' % s for s in strings) + "]"


def write_mapping(cells, names):
    out = []
    a = out.append
    a("{")
    a('  "schemaVersion": 1,')
    a('  "provenance": "Generated by tools/scripts/gen_custom_tiles.py - do not hand-edit; '
      "rerun the generator (byte-identical output). GRANADAD ART UNIFIED SPEC v1 2026-07-13 / "
      "DECISIONS.md Art register row (Eli 2026-07-13, FOURTH revision: flat per-z-slice "
      "rendering retained, faux-3D rescinded): custom ORIGINAL pixel art, MERCOLAS-24 palette, "
      'zero pixels copied from any referenced game. Kenney pack stays in-repo as the committed fallback.",')
    a('  "notes": "Color lives in the sheet pixels: NO materials.*.tint and no fluids.water.tint '
      "anywhere in this pack. chromatis.heatGlowTint kept (canon, BLESSING-QUEUE "
      "ruling 5, an overlay tint not a sheet pixel); glowstone.minLight kept. The wall form "
      "key deliberately duplicates block: block is JsonTileArtResolver's DEFAULT_FORM "
      "fallback and the safest default for unknown future forms. <mat>.top regions are on the "
      'sheet but unmapped - reserved inventory (legal per TILE-ART-SPEC section 7.2).",')
    a('  "atlas": "art/custom/tiles.png",')
    a('  "tilePx": 16,')
    a('  "sheet": { "columns": %d, "rows": %d },' % (COLS, ROWS))
    a('  "missingRegion": "missing",')
    a('  "voidColor": "#0D0B10",')
    a('  "lightTintQ8": [%s],' % ", ".join(str(v) for v in LIGHT_TINT_Q8))
    a('  "zPeekDimQ8": [256, 168, 112, 76],')
    a('  "regions": {')
    for i, name in enumerate(names):
        pairs = ", ".join("[%d, %d]" % (c, r) for c, r in cells[name])
        comma = "," if i < len(names) - 1 else ""
        a('    "%s": [%s]%s' % (name, pairs, comma))
    a("  },")
    a('  "materials": {')
    for i, m in enumerate(MATS):
        a('    "%s": {' % m)
        if m == "chromatis":
            a('      "heatGlowTint": "#E8842A",')
        if m == "glowstone":
            a('      "minLight": 8,')
        a('      "forms": {')
        out.extend(material_forms_json(m))
        a("      }")
        a("    }" + ("," if i < len(MATS) - 1 else ""))
    a("  },")
    a('  "fluids": {')
    a('    "water": { "region": "water", "depthAlphaQ8": [0, 96, 120, 144, 168, 192, 216, 240] }')
    a("  }")
    a("}")
    a("")
    with open(JSON_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


# --------------------------------------------------------------------------
# validation (hard failures abort with nonzero exit)
# --------------------------------------------------------------------------

def validate(img, cells, names, inv, rendered):
    errors, warns = [], []
    allowed = set(PAL.values()) | {MISSING_A, MISSING_B}
    px = img.load()
    for x in range(img.width):
        for y in range(img.height):
            if px[x, y] not in allowed:
                errors.append(f"off-palette pixel at ({x},{y}): {px[x, y]}")
                break
    total = sum(len(v) for v in cells.values())
    if total != 311:
        errors.append(f"cell count {total} != 311")
    # per-cell rules
    for (col, row), (name, vi, g) in sorted(rendered.items(),
                                            key=lambda kv: (kv[0][1], kv[0][0])):
        if g is None:              # missing checker, the sole off-palette exemption
            continue
        shades = {g[y][x] for y in range(T) for x in range(T)}
        if len(shades) < 3:
            errors.append(f"{name}#{vi}: only {len(shades)} distinct shades")
        hit = find_flat_rect(g)
        if hit:
            errors.append(f"{name}#{vi}: flat {hit[2]}x{hit[3]} rect at "
                          f"({hit[0]},{hit[1]}) shade {hit[4]}")
        kind = inv[name][0]
        n = T * T
        counts = [0, 0, 0, 0]
        for y in range(T):
            for x in range(T):
                counts[g[y][x]] += 1
        if kind == "floor":
            base_pct = counts[1] * 100 // n
            if not 40 <= base_pct <= 75:
                warns.append(f"{name}#{vi}: base {base_pct}% (target 45-70)")
            if counts[3] * 100 // n > 14:
                warns.append(f"{name}#{vi}: light {counts[3] * 100 // n}% (>12)")
            if counts[0] * 100 // n > 24:
                warns.append(f"{name}#{vi}: shadow {counts[0] * 100 // n}% (>20)")
    return errors, warns


# --------------------------------------------------------------------------

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    img, cells, names, inv, rendered = build_sheet()
    errors, warns = validate(img, cells, names, inv, rendered)
    for w in warns:
        print("WARN " + w)
    if errors:
        for e in errors:
            print("FAIL " + e)
        raise SystemExit(1)
    img.save(PNG_PATH, "PNG", optimize=False, compress_level=9)
    write_mapping(cells, names)
    print(f"ok: {len(names)} regions, {sum(len(v) for v in cells.values())} cells")
    print("wrote " + PNG_PATH)
    print("wrote " + JSON_PATH)


if __name__ == "__main__":
    main()
