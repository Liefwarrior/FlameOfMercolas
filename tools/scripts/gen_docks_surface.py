#!/usr/bin/env python3
"""Generate content/maps/src/docks_surface.tmx — the Docks ward surface map.

Deterministic (byte-identical on every run; no timestamps, no randomness).
Blueprint: the docks_surface authoring blueprint derived from
docs/design/DOCKS-GAZETTEER.md (192x128x16; z0-7 substrate, authored content
z8-15; Band A quayside z11 / Band B mid-slope z12 / Band C upper z13).
Convention: content/maps/README.md (z:+N groups, terrain/floor/fluids/markers
sublayers, materials.tsx gids, CSV, point markers at tile centers).

Deviations from the blueprint are marked with "DEV:" comments.
"""

import os

W, H, ZCOUNT = 192, 128, 16

# --- gids (materials.tsx tile id + 1, firstgid=1) ---
GRANITE_WALL = 1
GRANITE_FLOOR = 2
GRANITE_STAIR_UP = 3
GRANITE_STAIR_DOWN = 4
GRANITE_RAMP = 5
DIRT_WALL = 6
DIRT_FLOOR = 7
OAK_WALL = 8
OAK_FLOOR = 9
OAK_STAIR_UP = 10
OAK_STAIR_DOWN = 11
THATCH_WALL = 12
THATCH_FLOOR = 13
TRUDGEON_WALL = 14
TRUDGEON_FLOOR = 15
GETILIA_WALL = 16
STEEL_WALL = 18
BRICK_WALL = 20
BRICK_FLOOR = 21
WATER7 = 29
WATER4 = 30
WATER2 = 31
REMAN_WALL = 32
REMAN_FLOOR = 33
LEATHER_WALL = 34
CLOTH_WALL = 35
DIRT_RAMP = 36   # appended to materials.tsx (append-only, tile id 35)
BRICK_RAMP = 37  # appended to materials.tsx (append-only, tile id 36)
# DECISIONS.md Art register FIFTH revision (Eli 2026-07-15, DF-translated Kenney /
# Roman-pillared civic facades): 3 civic-only material clones (content/raws/materials/
# *_facade.json), WALL-only, appended to materials.tsx (tile ids 37-39). Used ONLY on the
# specific street-facing frontage walls tagged in section 3 below -- everywhere else,
# granite/brick/reman_concrete stay plain.
GRANITE_FACADE_WALL = 38
BRICK_FACADE_WALL = 39
REMAN_FACADE_WALL = 40

# 3D lattices: [z][y][x]
T = [[[0] * W for _ in range(H)] for _ in range(ZCOUNT)]   # terrain (fill)
F = [[[0] * W for _ in range(H)] for _ in range(ZCOUNT)]   # floor
FL = [[[0] * W for _ in range(H)] for _ in range(ZCOUNT)]  # fluids
MK = [[] for _ in range(ZCOUNT)]                           # markers per z


def frect(z, x0, y0, x1, y1, gid):
    for y in range(y0, y1 + 1):
        row = F[z][y]
        for x in range(x0, x1 + 1):
            row[x] = gid


def trect(z, x0, y0, x1, y1, gid):
    for y in range(y0, y1 + 1):
        row = T[z][y]
        for x in range(x0, x1 + 1):
            row[x] = gid


def border(z, x0, y0, x1, y1, gid, skip_sides=()):
    if "n" not in skip_sides:
        for x in range(x0, x1 + 1):
            T[z][y0][x] = gid
    if "s" not in skip_sides:
        for x in range(x0, x1 + 1):
            T[z][y1][x] = gid
    if "w" not in skip_sides:
        for y in range(y0, y1 + 1):
            T[z][y][x0] = gid
    if "e" not in skip_sides:
        for y in range(y0, y1 + 1):
            T[z][y][x1] = gid


def cells(z, pts, gid, layer=None):
    grid = T if layer is None else layer
    for (x, y) in pts:
        grid[z][y][x] = gid


def shell(z, x0, y0, x1, y1, wall, floor=None, doors=(), skip_sides=()):
    """Walled building story: border walls, full-rect floor, door gaps."""
    if floor is not None:
        frect(z, x0, y0, x1, y1, floor)
    border(z, x0, y0, x1, y1, wall, skip_sides)
    for (dx, dy) in doors:
        T[z][dy][dx] = 0
        if floor is not None:
            F[z][dy][dx] = floor


def mk(z, cls, name, tx, ty, **props):
    MK[z].append((cls, name, tx * 16 + 8, ty * 16 + 8, props))


def center(x0, y0, x1, y1):
    return (x0 + x1) // 2, (y0 + y1) // 2


# ======================================================================
# FIXTURE LIBRARY (2026-07-15 senior-level-design pass, Eli's directive to
# "create a lot of re-usable fixtures within these city blocks because it would
# make sense that things are standardized"). A layer of STANDARDIZED, repeatable
# stamps composed from the low-level primitives above (frect/trect/border/cells/
# shell) -- so the city reads as standardized, not bespoke-random. Everything
# reuses existing material gids ONLY (no new art). Furniture idioms are the ones
# already used throughout this file: bed = CLOTH_WALL, chest/trunk = LEATHER_WALL,
# hearth = GRANITE_WALL cells, counter/table/shelf/nightstand = OAK_WALL.
#
# The single most important fixture is sidewalk(): it paints GRANITE_FLOOR, which
# (SEVENTH art revision, art-mapping.json variantPatterns) resolves to the periodic
# floor_pave region -- an OBVIOUS regular laid-paver weave, visually distinct from
# the smooth homogeneous brick roadway spine (floor_tile) and the deliberately-rough
# dirt intersections (floor_earth, hash-scattered). That is exactly Eli's street
# hierarchy: "Sidewalks should be obvious and not irregularly patterned ... the
# middle of the street and intersections in the street would have less regularity."
# ======================================================================

# --- surface / street fixtures ---
def sidewalk(z, x0, y0, x1, y1):
    """A paved sidewalk band -> GRANITE_FLOOR (the periodic floor_pave paver weave).
    The one call that makes a street frontage read as an obvious laid sidewalk."""
    frect(z, x0, y0, x1, y1, GRANITE_FLOOR)


def safe_sidewalk(z, x0, y0, x1, y1):
    """Paint the periodic floor_pave weave (GRANITE_FLOOR) onto a rect, but ONLY on
    cells that are currently bare exterior dirt (terrain open, DIRT_FLOOR, no fluid).
    Walls, doors' interior thresholds, interior floors, stairs, ramps, water and
    already-paved street are all left untouched -- so a rect may span whole buildings
    to pave the dirt SEAMS between them in one call without moving any footprint."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if T[z][y][x] == 0 and F[z][y][x] == DIRT_FLOOR and FL[z][y][x] == 0:
                F[z][y][x] = GRANITE_FLOOR


def roadway(z, x0, y0, x1, y1, spine_gid=BRICK_FLOOR):
    """A smooth arterial roadway spine -> BRICK_FLOOR (the homogeneous floor_tile
    avenue). Sleek and regular, flanked by sidewalk() bands."""
    frect(z, x0, y0, x1, y1, spine_gid)


def intersection(z, x0, y0, x1, y1):
    """A junction/crossing square -> DIRT_FLOOR (rough floor_earth, hash-scattered):
    the deliberately 'less regular' middle-of-street/junction Eli reserves
    irregularity for."""
    frect(z, x0, y0, x1, y1, DIRT_FLOOR)


# --- slab-clutter fixtures (dressing pass; break up barren smooth expanses) ---
def crate_grid(z, x0, y0, x1, y1, gid=OAK_WALL, bw=2, bh=2, gx=1, gy=1):
    """Standardized grid of storage stacks: bw x bh solid crate/barrel islands (gid,
    default OAK_WALL) tiled from the NW corner with gx/gy-wide aisles between, so a
    warehouse/cargo bay reads as neatly racked goods that stay traversable. Like
    safe_sidewalk it SELF-GUARDS -- paints a cell only when it is currently open floor
    (terrain==0, floor set, no fluid) -- auto-skipping walls, doors, stairs, existing
    furniture and water. Aisles between/around the islands stay one connected component
    by construction; caller keeps the rect off anchors/through-spines."""
    y = y0
    while y <= y1:
        x = x0
        while x <= x1:
            for yy in range(y, min(y + bh, y1 + 1)):
                for xx in range(x, min(x + bw, x1 + 1)):
                    if T[z][yy][xx] == 0 and F[z][yy][xx] != 0 and FL[z][yy][xx] == 0:
                        T[z][yy][xx] = gid
            x += bw + gx
        y += bh + gy


def rug(z, x0, y0, x1, y1, gid=GRANITE_FLOOR):
    """Floor-variation 'rug'/inlaid medallion: repaint a rect of finished floor to gid
    (default GRANITE_FLOOR -> the periodic floor_pave weave, which reads as a checkered
    marble/mosaic inlay on the pale REMAN/BRICK slab). FLOOR layer only => entirely
    walkability-neutral (never traps, never blocks). Only repaints cells that already
    hold a non-fluid floor, so it hugs the room and won't spill onto walls/void/water."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if F[z][y][x] != 0 and FL[z][y][x] == 0:
                F[z][y][x] = gid


# --- atomic homey pieces (a furniture cell is a solid WALL-gid tile) ---
def bed(z, x, y):
    T[z][y][x] = CLOTH_WALL


def chest(z, x, y):
    T[z][y][x] = LEATHER_WALL


def hearth(z, x, y):
    T[z][y][x] = GRANITE_WALL


def table(z, x0, y0, x1, y1):
    trect(z, x0, y0, x1, y1, OAK_WALL)


def shelf(z, x0, y0, x1, y1):
    trect(z, x0, y0, x1, y1, OAK_WALL)


def shop_counter(z, x0, y0, x1, y1):
    trect(z, x0, y0, x1, y1, OAK_WALL)


# --- interior room fixtures ---
def partition(z, x0, y0, x1, y1, wall, door):
    """A single interior partition-wall segment (a straight run) with one door gap
    punched through it -- the standardized 'divide a shell into rooms' primitive the
    homes/compound units compose (formalizes the hand-rolled partition idiom used at
    K01/K03/K17 etc.)."""
    trect(z, x0, y0, x1, y1, wall)
    (dx, dy) = door
    T[z][dy][dx] = 0


def homey_touches(z, hearth_xy, bed_xys, chest_xy=None, table_rect=None):
    """Standardized 'lived-in home' furniture drop: a hearth, one bed per household
    member, an optional chest and table -- so every home feels lived-in without being
    hand-bespoke (Eli: "Homes should have homey touches"). Callers pass cells that sit
    against walls / off the room's walkable center + door approach + any stair, so the
    furniture ZONES the room (a sleeping corner, a hearth-and-table common area) without
    trapping the occupants."""
    (hx, hy) = hearth_xy
    hearth(z, hx, hy)
    for (bx, by) in bed_xys:
        bed(z, bx, by)
    if chest_xy is not None:
        chest(z, chest_xy[0], chest_xy[1])
    if table_rect is not None:
        table(z, table_rect[0], table_rect[1], table_rect[2], table_rect[3])


def compound_unit_interior(z, x0, y0, x1, y1, door, wall=REMAN_WALL):
    """Divide a compound dwelling unit into a common/hearth room + a sleeping room
    (one partition wall with a connecting door) and drop standardized homey touches --
    a real home circulation instead of one undivided box (Eli #5: "homes in the
    compounds should have rooms that are organized in a meaningful way", #6 homey
    touches). Parametric and standardized across every unit. Guarantees, by
    construction, that the unit's ANCHOR (its centre, where the household spawns), its
    ENTRY DOOR, its interior STAIR (always at x0+2), and the interior door between the
    two rooms all stay on OPEN floor -- furniture only ever hugs the back/front walls
    and the 3-wide sleeping alcove, never the central walkable rows/columns. Sized for
    the ~12x6-7 compound condos; a no-op-safe partition for anything narrower is the
    caller's responsibility (not called on the tiny hovels)."""
    (dx, dy) = door
    door_south = (dy == y1)
    back_y = y0 + 1 if door_south else y1 - 1   # hearth/beds hug the wall opposite the door
    front_y = y1 - 1 if door_south else y0 + 1
    mid_y = (y0 + y1) // 2
    px = x1 - 4                                  # partition -> a 3-wide sleeping alcove east
    partition(z, px, y0 + 1, px, y1 - 1, wall, (px, mid_y))
    # common/hearth room (west of the partition): hearth on the back wall + a table
    homey_touches(z, (x0 + 1, back_y), bed_xys=(),
                  table_rect=(x0 + 3, back_y, x0 + 4, back_y))
    # sleeping alcove (east of the partition): two beds along the back wall + a chest
    bed(z, px + 1, back_y)
    bed(z, px + 2, back_y)
    chest(z, x1 - 1, front_y)


def hovel_touches(z, x0, y0, x1, y1, door, anchor):
    """Squalor-tier lived-in touches for an un-partitionable shanty: hearth + up to
    two beds + a chest, all on the interior strip against the wall OPPOSITE the door,
    skipping the anchor cell and the door-aligned cell so the door->anchor spine and
    every open cell stay one connected component. NO partition (would trap a 2-deep box)."""
    ix0, iy0, ix1, iy1 = x0 + 1, y0 + 1, x1 - 1, y1 - 1
    (dx, dy) = door
    if dy == y0:      strip = [(x, iy1) for x in range(ix0, ix1 + 1)]   # door N -> back S row
    elif dy == y1:    strip = [(x, iy0) for x in range(ix0, ix1 + 1)]   # door S -> back N row
    elif dx == x0:    strip = [(ix1, y) for y in range(iy0, iy1 + 1)]   # door W -> back E col
    else:             strip = [(ix0, y) for y in range(iy0, iy1 + 1)]   # door E -> back W col
    (ax, ay) = anchor
    strip = [c for c in strip if c != (ax, ay)
             and not (dy in (y0, y1) and c[0] == dx)      # keep door column clear
             and not (dx in (x0, x1) and c[1] == dy)]     # keep door row clear
    if not strip:
        return
    hearth(z, *strip[0])
    for c in strip[1:-1][:2]:
        bed(z, *c)
    if len(strip) >= 2:
        chest(z, *strip[-1])


# ======================================================================
# 0. Bands / base ground (blueprint section 0)
# ======================================================================
RAMP1_X = set(range(0, 4)) | set(range(72, 80)) | set(range(160, 164))    # y96, z11
RAMP2_X = set(range(4, 8)) | set(range(72, 80)) | set(range(160, 164))    # y116, z12
POND_X = range(150, 162)  # timber pond: water pushed to y<=9 (DEV: pond spec y2-9
#                           vs general strand water y0-7; carved as water y0-9)

WATER, STRAND, RAMP_S, SEAWALL, A, RAMP_1, B, RAMP_2, C = range(9)


def band(x, y):
    if x <= 11:                       # west strand (wet shingle firebreak)
        if y <= 9:
            return WATER
        if y <= 24:
            return STRAND
        if y == 25:
            # DEV: x0-1/x10-11 at y25 unspecified -> Band A fill+floor.
            return RAMP_S if 2 <= x <= 9 else A
    elif 130 <= x <= 163:             # Beaching Strand / Harl's Yard shore
        wy = 9 if x in POND_X else 7
        if y <= wy:
            return WATER
        if y <= 28:
            return STRAND
        if y == 29:
            return RAMP_S
    else:
        if y <= 24:
            return WATER
        if y == 25:
            # DEV: blueprint seawall x-lists omit x72-79 (Saltgate foot);
            # extended so the harbor does not breach at y25.
            return SEAWALL
    if y <= 95:
        return A
    if y == 96:
        return RAMP_1 if x in RAMP1_X else B
    if y <= 115:
        return B
    if y == 116:
        return RAMP_2 if x in RAMP2_X else C
    return C


for y in range(H):
    for x in range(W):
        for z in range(7):
            T[z][y][x] = GRANITE_WALL     # z0-6 granite substrate
        T[7][y][x] = DIRT_WALL            # z7 dirt substrate
        b = band(x, y)
        if b == WATER:
            F[8][y][x] = DIRT_FLOOR       # harbor bed
            FL[9][y][x] = WATER7
            FL[10][y][x] = WATER7         # tick-0 = high tide
        elif b == STRAND:
            T[8][y][x] = DIRT_WALL
            T[9][y][x] = DIRT_WALL
            F[10][y][x] = DIRT_FLOOR
        elif b == RAMP_S:
            T[8][y][x] = DIRT_WALL
            T[9][y][x] = DIRT_WALL
            T[10][y][x] = DIRT_RAMP       # z11 above stays OPEN
        elif b == SEAWALL:
            T[8][y][x] = DIRT_WALL
            T[9][y][x] = GRANITE_WALL
            T[10][y][x] = GRANITE_WALL    # z11 above: no floor (seawall lip)
        elif b == A:
            for z in range(8, 11):
                T[z][y][x] = DIRT_WALL
            F[11][y][x] = DIRT_FLOOR
        elif b == RAMP_1:
            for z in range(8, 11):
                T[z][y][x] = DIRT_WALL
            T[11][y][x] = BRICK_RAMP if 72 <= x <= 79 else GRANITE_RAMP
        elif b == B:
            for z in range(8, 12):
                T[z][y][x] = DIRT_WALL
            F[12][y][x] = DIRT_FLOOR
        elif b == RAMP_2:
            for z in range(8, 12):
                T[z][y][x] = DIRT_WALL
            T[12][y][x] = BRICK_RAMP if 72 <= x <= 79 else GRANITE_RAMP
        else:  # C
            for z in range(8, 13):
                T[z][y][x] = DIRT_WALL
            F[13][y][x] = DIRT_FLOOR

# ======================================================================
# 1. Streets (blueprint section 1). Dirt lanes first, paved spines after
#    (paved wins at crossings); dirt-on-dirt lanes are cosmetic.
# ======================================================================
frect(11, 160, 30, 161, 59, DIRT_FLOOR)     # Gullet G1
frect(11, 176, 34, 177, 58, DIRT_FLOOR)     # Gullet G2
frect(11, 162, 57, 191, 59, DIRT_FLOOR)     # Gullet "Bottom" link
frect(11, 164, 66, 165, 95, DIRT_FLOOR)     # Gullet G3
frect(11, 80, 50, 131, 51, DIRT_FLOOR)      # Bilgewater Gap (placeholder)
frect(11, 4, 78, 71, 81, DIRT_FLOOR)        # Walkback path (placeholder)
frect(11, 0, 60, 3, 95, DIRT_FLOOR)         # Pitch Lane, Band A leg
frect(12, 0, 97, 7, 115, DIRT_FLOOR)        # Pitch Lane, Band B leg (y96 ramps stay OPEN above)
frect(11, 80, 94, 190, 95, DIRT_FLOOR)      # Backwall Alley (placeholder)
frect(13, 4, 120, 188, 122, DIRT_FLOOR)     # Gallows Row (placeholder)
frect(13, 98, 119, 105, 123, DIRT_FLOOR)    # Gallows Row well plaza bulge
frect(13, 132, 122, 135, 127, DIRT_FLOOR)   # Abbey lane (placeholder)

# DEV (Eli 2026-07-13 sizing pass, DOCKS-GAZETTEER.md 3.1): the long uniform-width
# quay/rise bands read as monolithic ("big squares everywhere"); narrowed the long
# middle stretches and kept 8-wide plazas only at deliberate frontages/crossings.
frect(11, 0, 28, 79, 33, GRANITE_FLOOR)     # Tarwalk A: quay apron (narrow, 6-wide)
frect(11, 56, 26, 79, 33, GRANITE_FLOOR)    # ...plaza bulge, the Weighhouse frontage
frect(11, 80, 28, 129, 33, BRICK_FLOOR)     # Tarwalk A: east half (narrow, 6-wide)
frect(11, 114, 26, 129, 33, BRICK_FLOOR)    # ...plaza bulge outside the Gilded Gull
frect(11, 130, 30, 163, 35, BRICK_FLOOR)    # Tarwalk B (bends inland)
for y in range(26, 34):                     # Tarwalk C: broken paving checker
    for x in range(164, 192):
        F[11][y][x] = BRICK_FLOOR if (x + y) % 2 == 0 else DIRT_FLOOR
frect(11, 4, 60, 147, 65, BRICK_FLOOR)      # Ropewynd, paved reach
frect(11, 30, 58, 67, 59, BRICK_FLOOR)      # ...bulge, the Ropewalk frontage
frect(11, 40, 58, 53, 59, BRICK_FLOOR)      # ...bulge, Cooper & Blockmaker's frontage
frect(11, 148, 60, 177, 65, DIRT_FLOOR)     # Ropewynd, paving gives out
frect(11, 32, 34, 35, 59, BRICK_FLOOR)      # Herring Lane
frect(12, 80, 97, 188, 100, BRICK_FLOOR)    # Terrace Walk (placeholder)
frect(11, 72, 26, 79, 65, BRICK_FLOOR)      # Saltgate Rise, Band A leg: wide near the
                                             # quay civic cluster + Ropewynd crossing (8-wide)
frect(11, 72, 66, 77, 95, BRICK_FLOOR)      # ...narrows south of Ropewynd (6-wide)
frect(12, 72, 97, 79, 115, BRICK_FLOOR)     # Saltgate Rise, Band B leg
frect(13, 72, 117, 79, 127, BRICK_FLOOR)    # Saltgate Rise, Band C leg
frect(11, 33, 26, 33, 59, DIRT_FLOOR)       # Herring Lane offal gutter (no fluid)
frect(11, 34, 54, 55, 54, DIRT_FLOOR)       # Salt Row offal-gutter link

# --- Sidewalk frontage bands (2026-07-15 senior-level-design pass, Eli directive #2
# "Sidewalks should be obvious and not irregularly patterned"). These repaint the
# OUTERMOST walkable row/column of an already-paved arterial (brick spine) as
# GRANITE_FLOOR -- which resolves to the periodic floor_pave paver weave -- so the
# smooth brick roadway centre now reads as flanked by OBVIOUS laid-paver sidewalks
# (the §5 street hierarchy: sidewalk band | smooth spine | sidewalk band). They only
# ever repaint existing STREET floor (brick -> granite), never touch a building, so
# no K-site/compound/hovel footprint and no DocksPopulation anchor moves. The Tarwalk
# A quay apron is already GRANITE (now periodic pavers); Tarwalk C keeps its
# deliberately-worn broken-paving checker as the §5 "less regular" junction texture.
sidewalk(11, 80, 33, 129, 33)               # Tarwalk east: sidewalk along the shopfronts
sidewalk(11, 4, 60, 147, 60)                # Ropewynd north kerb
sidewalk(11, 4, 65, 147, 65)                # Ropewynd south kerb (the frontage side)
sidewalk(11, 72, 34, 72, 65)               # Saltgate Rise, Band A: west kerb
sidewalk(11, 79, 34, 79, 65)               # Saltgate Rise, Band A: east kerb
sidewalk(12, 72, 97, 72, 115)              # Saltgate Rise, Band B: west kerb
sidewalk(12, 79, 97, 79, 115)              # Saltgate Rise, Band B: east kerb
sidewalk(13, 72, 117, 72, 127)             # Saltgate Rise, Band C: west kerb
sidewalk(13, 79, 117, 79, 127)             # Saltgate Rise, Band C: east kerb

# ======================================================================
# 2. Waterfront (blueprint section 2)
# ======================================================================
# The Long Quay: mooring posts + crane gantry
cells(11, [(14, 26), (29, 26), (34, 26), (49, 26), (54, 26), (69, 26)], STEEL_WALL)
trect(11, 40, 26, 41, 27, TRUDGEON_WALL)    # crane gantry (tall: z11+z12)
trect(12, 40, 26, 41, 27, TRUDGEON_WALL)
mk(11, "script_anchor", "berth_01_anchor", 21, 27)
mk(11, "script_anchor", "berth_02_anchor", 41, 28)
mk(11, "script_anchor", "berth_03_anchor", 61, 27)
mk(11, "script_anchor", "crane_longquay_anchor", 41, 29)
mk(11, "script_anchor", "muster_quay_anchor", 45, 30)

# K16 Drowned-Name Wall (niche shrine against the seawall; DEV: grown 2x1 -> 3x2,
# Eli 2026-07-13 sizing pass — a second course set into the seawall face itself)
cells(11, [(94, 25), (95, 25), (96, 25), (94, 26), (95, 26), (96, 26)], GRANITE_WALL)
mk(11, "script_anchor", "shrine_drowned_name_wall_anchor", 95, 27)
mk(11, "light_source", "lamp_shrine_candles", 95, 27, luminance=8)

# K20 Merle's Boats — boathouse over the water
frect(11, 82, 14, 93, 25, TRUDGEON_FLOOR)                     # deck over water
border(11, 82, 14, 93, 25, TRUDGEON_WALL)
for d in ((86, 25), (87, 25)):                                # south door onto Tarwalk
    T[11][d[1]][d[0]] = 0
    F[11][d[1]][d[0]] = TRUDGEON_FLOOR
for (px, py) in ((82, 14), (93, 14), (82, 20), (93, 20), (87, 17)):  # pilings
    T[9][py][px] = TRUDGEON_WALL
    T[10][py][px] = TRUDGEON_WALL
    FL[9][py][px] = 0
    FL[10][py][px] = 0
frect(12, 82, 14, 93, 25, TRUDGEON_FLOOR)                     # roof
mk(11, "script_anchor", "merle_liftfloor_anchor", 86, 20)
mk(11, "script_anchor", "dungeon_seam_merles_floor", 87, 21)  # seam stub: marker only
mk(11, "script_anchor", "business_k20_merles_anchor", 88, 18)

# Pier Row — 4 timber finger-piers (Wormwood condemned: deck holes, no lamps)
PIERS = [  # (x0, x1, y0, anchor_name, anchor_y)
    (98, 100, 6, "pier_01_anchor", 8),
    (106, 108, 6, "pier_02_anchor", 8),
    (114, 116, 8, "pier_03_anchor", 10),
    (122, 124, 4, "pier_04_wormwood_anchor", 24),
]
WORMWOOD_HOLES = {(123, 8), (122, 13), (124, 17), (123, 21)}
for (x0, x1, y0, aname, ay) in PIERS:
    for y in range(y0, 26):
        for x in range(x0, x1 + 1):
            if aname.startswith("pier_04") and (x, y) in WORMWOOD_HOLES:
                continue  # collapse hazard: open to the water below
            F[11][y][x] = TRUDGEON_FLOOR
    for py in range(y0, 26, 6):  # pilings under both deck edges every 6 rows
        for px in (x0, x1):
            T[9][py][px] = TRUDGEON_WALL
            T[10][py][px] = TRUDGEON_WALL
            FL[9][py][px] = 0
            FL[10][py][px] = 0
    mk(11, "script_anchor", aname, (x0 + x1) // 2, ay)
mk(11, "script_anchor", "hazard_wormwood_pier_anchor", 123, 12)

# K30-K33 moored/wrecked hulls (Eli 2026-07-13 sizing pass, DOCKS-GAZETTEER.md 3.1):
# the 3 Long Quay berths + the condemned Wormwood Pier were bare mooring markers with
# no hull geometry; each gets a walkable deck (no interior simulation) displacing the
# water it sits in, sized to increasing scale west->east per the Long Quay's own
# "deep-water berths" description.
# DEV (napkin-sketch redesign, 2026-07-14, docs/design/DOCKS-GAZETTEER.md "Redesigned
# topology" subsection): Saltgate Rise's own spine continues north into the water as a
# fishbone pier (spine + 3 perpendicular finger piers). K30/K31/K32 are RE-SITED into
# the fishbone's 3 slips, W x H unchanged (10x5 / 14x7 / 16x8) -- only moved. Their old
# footprints are reverted to plain water first (required cleanup, hard-lesson-driven).

# Revert old K30/K31/K32 footprints to plain open water before repainting.
for (ox0, oy0, ox1, oy1) in ((16, 20, 25, 24), (44, 18, 57, 24), (60, 17, 75, 24)):
    for y in range(oy0, oy1 + 1):
        for x in range(ox0, ox1 + 1):
            F[11][y][x] = 0
            T[11][y][x] = 0
            T[10][y][x] = 0
            FL[9][y][x] = WATER7
            FL[10][y][x] = WATER7

# The fishbone pier spine: Saltgate Rise's own N-S axis continues through the seawall
# (new bridge row at y25, since y25 was previously an unfloored SEAWALL lip) and out
# into open water as an 8-wide timber pier deck, x72-79 y1-25, pilings at the spine's
# own edges (mirrors the Pier Row piling convention, L289-294 in the pre-redesign file).
frect(11, 72, 1, 79, 25, TRUDGEON_FLOOR)
for py in (3, 9, 15, 21):
    for px in (72, 79):
        T[9][py][px] = TRUDGEON_WALL
        T[10][py][px] = TRUDGEON_WALL
        FL[9][py][px] = 0
        FL[10][py][px] = 0

# Three finger piers, 1 tile thick, crossing the spine at even intervals -- the gaps
# between them are the 3 hull slips (standard fishbone-marina geometry). East arms are
# capped at x81 where K20 Merle's Boats (x82-93 y14-25) would otherwise be crossed.
frect(11, 60, 9, 71, 9, TRUDGEON_FLOOR)      # finger 1 west arm
frect(11, 80, 9, 91, 9, TRUDGEON_FLOOR)      # finger 1 east arm (y9 < K20's y14 start)
mk(11, "script_anchor", "finger_01_anchor", 75, 9)
frect(11, 62, 17, 71, 17, TRUDGEON_FLOOR)    # finger 2 west arm
frect(11, 80, 17, 81, 17, TRUDGEON_FLOOR)    # finger 2 east arm, capped short of K20
mk(11, "script_anchor", "finger_02_anchor", 75, 17)
frect(11, 62, 23, 71, 23, TRUDGEON_FLOOR)    # finger 3 west arm
frect(11, 80, 23, 81, 23, TRUDGEON_FLOOR)    # finger 3 east arm, capped short of K20
mk(11, "script_anchor", "finger_03_anchor", 75, 23)

# K32 Long Quay Berth 3 hull -- "The Deep Keel" (16x8, largest) -- Slip 1, west arm,
# flush against the spine's own x=72 edge
frect(11, 56, 1, 71, 8, OAK_FLOOR)
border(11, 56, 1, 71, 8, OAK_WALL)
for y in range(1, 9):
    for x in range(56, 72):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "ship_k32_deepkeel_anchor", 63, 4)

# K31 Long Quay Berth 2 hull -- "Bregga's Promise" (14x7) -- Slip 2
frect(11, 58, 10, 71, 16, OAK_FLOOR)
border(11, 58, 10, 71, 16, OAK_WALL)
for y in range(10, 17):
    for x in range(58, 72):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "ship_k31_breggas_promise_anchor", 64, 13)

# K30 Long Quay Berth 1 hull -- "The Kestrel" (10x5, smallest) -- Slip 3
frect(11, 62, 18, 71, 22, OAK_FLOOR)
border(11, 62, 18, 71, 22, OAK_WALL)
for y in range(18, 23):
    for x in range(62, 72):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "ship_k30_kestrel_anchor", 66, 20)

# Bridge row: y25 crosses the seawall lip, connecting finger 3/spine down to Tarwalk
# (y26) -- already covered by the spine's own frect(11, 72, 1, 79, 25, ...) above,
# whose y1-25 range includes y25 (previously an unfloored SEAWALL-lip row with no
# z:+11 floor authored at all -- this IS the "new bridge tile", not a separate one).
# y24 stays open water (the approach gap between finger 3 and the seawall).

# K33 Wormwood Pier wreck -- "The Widow's Grief" (5x9 half-capsized derelict, oriented
# N-S along the pier's own run; a wreck reinforces the condemned-pier hazard texture
# rather than contradicting it with a pristine ship)
frect(11, 125, 10, 129, 18, OAK_FLOOR)
border(11, 125, 10, 129, 18, OAK_WALL, skip_sides=("n",))
for (hx, hy) in ((126, 12), (128, 15), (127, 17)):   # breached hull -- holes to the sea
    T[11][hy][hx] = 0
    FL[11][hy][hx] = WATER2
for y in range(10, 19):
    for x in range(125, 130):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "wreck_k33_widowsgrief_anchor", 127, 14)

# The Beaching Strand — public careening ground (west part)
mk(10, "script_anchor", "strand_beaching_anchor", 132, 18)

# K06 Harl's Yard — the shipyard
# Slipway: timber skid rails on the strand, running into the water
for rx in (137, 142):
    for y in range(8, 26):
        F[10][y][rx] = TRUDGEON_FLOOR         # strand-level skid rails
    for y in range(4, 8):
        T[9][y][rx] = TRUDGEON_WALL           # submerged skidway
        FL[9][y][rx] = 0
mk(10, "script_anchor", "slipway_anchor", 139, 20)  # DEV: z10 (strand walk plane;
#                                                     blueprint sec.9 default said z11)
# Hull-on-blocks (corners stepped in, prow taper west)
hull = []
for x in range(148, 158):
    hull += [(x, 14), (x, 22)]
for y in range(14, 23):
    hull += [(148, y), (157, y)]
hull = [c for c in hull if c not in {(148, 14), (157, 14), (148, 22), (157, 22)}]
hull += [(147, 17), (147, 18), (147, 19)]
cells(10, hull, OAK_WALL)
cells(10, [(150, 15), (155, 15), (150, 21), (155, 21)], GRANITE_WALL)  # blocks
mk(10, "script_anchor", "hull_on_blocks_anchor", 152, 18)
# Timber pond: floating-log cells (v0 cheat — trudgeon floor, fluid omitted)
for (lx, ly) in ((151, 4), (153, 6), (155, 3), (156, 7), (158, 5), (159, 8),
                 (152, 8), (160, 3)):
    F[10][ly][lx] = TRUDGEON_FLOOR
    FL[10][ly][lx] = 0
mk(10, "script_anchor", "timber_pond_anchor", 155, 5)
# Workshop with saw pit. DEV: pit shifted 1 east to x137-138 (blueprint's
# x136-137 sat on the workshop's own west shell wall); stair pair at (137,40).
shell(11, 136, 36, 147, 46, TRUDGEON_WALL, DIRT_FLOOR,
      doors=[(138, 36), (139, 36), (140, 36), (141, 36)])
PIT = [(137, 40), (138, 40), (137, 41), (138, 41)]
for (px, py) in PIT:
    T[10][py][px] = 0
    F[10][py][px] = GRANITE_FLOOR
    T[11][py][px] = 0
    F[11][py][px] = 0
T[10][40][137] = OAK_STAIR_UP
T[11][40][137] = OAK_STAIR_DOWN
mk(10, "script_anchor", "sawpit_anchor", 137, 41)
frect(12, 136, 36, 147, 46, TRUDGEON_FLOOR)   # workshop roof
# Timber store yard: fence, gate, log-stack rows
border(11, 150, 36, 159, 46, TRUDGEON_WALL)
for g in ((153, 36), (154, 36)):
    T[11][g[1]][g[0]] = 0
for ly in (39, 42, 45):
    trect(11, 151, ly, 158, ly, OAK_WALL)
mk(11, "script_anchor", "business_k06_harlsyard_anchor", 141, 41)

# The Outfall (sealed L2 seam stub) + Tarwalk storm drains
for z in (9, 10):
    cells(z, [(171, 25), (172, 25)], STEEL_WALL)   # grate in the seawall
for y in range(26, 30):
    cells(9, [(170, y), (173, y)], GRANITE_WALL)   # stub ring
    for x in (171, 172):
        T[9][y][x] = 0
        F[9][y][x] = GRANITE_FLOOR
        FL[9][y][x] = WATER4
cells(9, [(170, 30), (171, 30), (172, 30), (173, 30)], GRANITE_WALL)  # y30 cap
mk(9, "script_anchor", "dungeon_seam_outfall", 171, 27)
T[10][25][33] = STEEL_WALL                          # storm grate west
mk(11, "script_anchor", "storm_grate_west", 33, 26)
T[10][25][120] = STEEL_WALL                         # storm grate east
mk(11, "script_anchor", "storm_grate_east", 120, 26)

# East strand/mud: debris in the water row y24 (squalor texture)
for dx in (178, 181, 184, 186, 189, 191):
    T[10][24][dx] = OAK_WALL
    FL[10][24][dx] = 0

# ======================================================================
# 3. Establishments K01-K25 (blueprint section 3)
# ======================================================================
# DEV (Eli 2026-07-13 sizing pass, DOCKS-GAZETTEER.md 3.1 "Establishment sizing
# standard"): every K01-K25 footprint below is resized to the pinned combined size
# table (no two named sites share a W x H, even rotated); interiors are re-derived
# to fit the new bounds sensibly, not just cropped. K26-K35 are new sites.

# K01 The Weighhouse (grand, 2-story granite; tile roof; signal mast) -- 16x17
shell(11, 56, 34, 71, 50, GRANITE_WALL, BRICK_FLOOR,
      doors=[(63, 34), (64, 34), (71, 41), (71, 42)])
# Rome cue (DECISIONS.md Art register FIFTH revision, Eli 2026-07-15): the north edge is
# the street-facing frontage (door-bearing, faces the Tarwalk quay apron) -- overwrite it
# with the civic-facade material so it resolves to a pedimented-colonnade sprite, then
# re-punch its two doors (border painting solid over them first).
trect(11, 56, 34, 71, 34, GRANITE_FACADE_WALL)
for (dx, dy) in ((63, 34), (64, 34)):
    T[11][dy][dx] = 0
    F[11][dy][dx] = BRICK_FLOOR
trect(11, 61, 39, 62, 40, STEEL_WALL)               # tariff scale
for y in range(35, 50):                             # ledger-room partition
    T[11][y][65] = OAK_WALL
T[11][37][65] = 0
trect(11, 58, 37, 60, 37, OAK_WALL)                 # counter
T[11][48][58] = OAK_STAIR_UP
# Ledger-room library-stack racking (2026-07-15 interior-detail pass, design 3): the K29
# racking-island idiom -- two rack columns with an aisle gap, inside the ledger room (x66-70).
for rx in (67, 69):
    trect(11, rx, 39, rx, 42, OAK_WALL)
    trect(11, rx, 45, rx, 48, OAK_WALL)
shell(12, 56, 34, 71, 50, GRANITE_WALL, GRANITE_FLOOR)
T[12][48][58] = OAK_STAIR_DOWN
# The customs archive / clerks' loft (2026-07-15 interior-detail pass, design 5): the z12
# upper floor was a completely bare shell() with nothing else drawn on it -- now record-stack
# racking (K29 idiom verbatim), a clerks' rest nook, and a strongroom sealed like K15's cage.
RACK_COLS_K01 = [(59, 60), (63, 64), (67, 68)]
for (rx0, rx1) in RACK_COLS_K01:
    trect(12, rx0, 37, rx1, 46, OAK_WALL)
for aisle_y in (38, 41, 44):                        # cross-aisle breaks through every rack
    for (rx0, rx1) in RACK_COLS_K01:
        for x in range(rx0, rx1 + 1):
            T[12][aisle_y][x] = 0
for x in range(66, 71):                             # clerks' rest nook, SE corner
    T[12][46][x] = OAK_WALL
for y in range(46, 50):
    T[12][y][66] = OAK_WALL
cells(12, [(67, 48), (69, 48)], CLOTH_WALL)          # 2 beds
for x in range(57, 61):                             # strongroom, SW corner (K15 cage idiom)
    T[12][46][x] = STEEL_WALL
for y in range(46, 50):
    T[12][y][60] = STEEL_WALL
cells(12, [(58, 48)], STEEL_WALL)                    # lockbox
T[12][46][68] = 0                                    # nook door
F[12][46][68] = GRANITE_FLOOR
T[12][46][58] = 0                                    # strongroom slot (reached via the stair)
F[12][46][58] = GRANITE_FLOOR
mk(12, "script_anchor", "k01_archive_anchor", 64, 42)
frect(13, 56, 34, 71, 50, BRICK_FLOOR)              # tile roof
mk(13, "light_source", "lamp_weighhouse_mast", 64, 35, luminance=26)
mk(11, "script_anchor", "business_k01_weighhouse_anchor", 64, 41)
mk(11, "script_anchor", "clue_c2_weighhouse_ledger", 68, 37)

# K02 Impound Yard (steel spike fence, watchman shed, crates, dog) -- 13x6,
# shares the Weighhouse's new west edge and abuts its south wall
border(11, 56, 53, 68, 58, STEEL_WALL)
for g in ((62, 53), (63, 53)):
    T[11][g[1]][g[0]] = 0
shell(11, 56, 55, 59, 58, OAK_WALL, DIRT_FLOOR, doors=[(59, 56)])
cells(11, [(64, 55), (66, 57), (67, 54)], OAK_WALL)
mk(11, "script_anchor", "business_k02_impound_anchor", 63, 56)
mk(11, "script_anchor", "impound_dog_anchor", 60, 57)

# K03 The Gilded Gull (large 2-story tavern; district's grandest) -- 15x14
shell(11, 114, 34, 128, 47, GRANITE_WALL, OAK_FLOOR, doors=[(121, 34), (122, 34)])
trect(11, 117, 39, 123, 39, GRANITE_WALL)           # bar
for y in range(35, 47):                             # snug partition
    T[11][y][125] = OAK_WALL
T[11][37][125] = 0
T[11][43][125] = 0
cells(11, [(116, 37), (119, 37), (116, 42), (119, 42)], OAK_WALL)
# Classic top-down tavern convention (A Link to the Past / Secret of Mana), design 3.1: bar
# perpendicular to the door (already true), a hearth against the back wall, patrons seated.
cells(11, [(117, 45), (118, 45)], GRANITE_WALL)     # hearth, south back wall
T[11][45][127] = OAK_STAIR_UP
shell(12, 114, 34, 128, 47, OAK_WALL, OAK_FLOOR)
for y in range(35, 47):                             # guest-room cross partitions
    T[12][y][120] = OAK_WALL
for x in range(115, 128):
    T[12][40][x] = OAK_WALL
T[12][37][120] = 0
T[12][40][117] = 0
T[12][40][123] = 0
T[12][45][127] = OAK_STAIR_DOWN
# Rentable upper rooms (2026-07-15 interior-detail pass, design 5): the 4 "guest-room" quadrants
# were bare shells with zero furniture -- each now has a bed + storage piece.
cells(12, [(116, 36), (124, 36), (116, 44), (124, 44)], CLOTH_WALL)   # beds, one per quadrant
cells(12, [(118, 38), (118, 46)], LEATHER_WALL)     # NW/SW trunks
cells(12, [(126, 38), (126, 46)], OAK_WALL)         # NE/SE nightstands (127,45 stair kept clear)
frect(13, 114, 34, 128, 47, THATCH_FLOOR)
mk(11, "light_source", "lamp_gull_door", 121, 33, luminance=18)
mk(11, "light_source", "lamp_gull_bar", 120, 39, luminance=16)
mk(11, "script_anchor", "business_k03_gilded_gull_anchor", 120, 41)
mk(11, "script_anchor", "patron_seat_gull_01_anchor", 117, 37)
mk(11, "script_anchor", "patron_seat_gull_02_anchor", 118, 37)
mk(11, "script_anchor", "patron_seat_gull_03_anchor", 117, 42)
mk(11, "script_anchor", "patron_seat_gull_04_anchor", 118, 42)
mk(11, "script_anchor", "patron_seat_gull_05_anchor", 119, 40)
mk(11, "script_anchor", "patron_seat_gull_06_anchor", 121, 40)

# K04 The Bilge (mid tavern + hammock loft) -- 12x10
shell(11, 100, 34, 111, 43, TRUDGEON_WALL, OAK_FLOOR, doors=[(105, 34), (106, 34)])
trect(11, 103, 37, 107, 37, OAK_WALL)               # bar
cells(11, [(101, 40), (109, 40)], OAK_WALL)         # tables
cells(11, [(104, 41), (105, 41)], GRANITE_WALL)     # hearth, back wall (design 3.1)
T[11][41][109] = OAK_STAIR_UP
shell(12, 100, 34, 111, 43, TRUDGEON_WALL, OAK_FLOOR)
cells(12, [(102, 37), (105, 37), (108, 37)], OAK_WALL)
# Triple the hammock density (2026-07-15 interior-detail pass, design 5): 2 more post rows
# mirroring the existing y37 row -- 3 posts -> 9, a genuinely crowded dive-bar loft.
cells(12, [(102, 39), (105, 39), (108, 39)], OAK_WALL)
cells(12, [(102, 41), (105, 41), (108, 41)], OAK_WALL)
T[12][41][109] = OAK_STAIR_DOWN
frect(13, 100, 34, 111, 43, THATCH_FLOOR)
mk(11, "light_source", "lamp_bilge_door", 105, 33, luminance=14)
mk(11, "script_anchor", "business_k04_bilge_anchor", 106, 38)
mk(11, "script_anchor", "patron_seat_bilge_01_anchor", 102, 40)
mk(11, "script_anchor", "patron_seat_bilge_02_anchor", 101, 39)
mk(11, "script_anchor", "patron_seat_bilge_03_anchor", 108, 40)
mk(11, "script_anchor", "patron_seat_bilge_04_anchor", 110, 41)

# K05 The Lantern Room (crossroads tavern, neutral ground) -- 13x12
shell(11, 58, 66, 70, 77, GRANITE_WALL, OAK_FLOOR,
      doors=[(63, 66), (64, 66), (70, 71), (70, 72)])
cells(11, [(59, 72), (60, 72)], GRANITE_WALL)       # hearth
trect(11, 62, 73, 64, 73, OAK_WALL)                 # counter
cells(11, [(60, 69), (63, 70), (66, 69)], OAK_WALL)
T[11][75][68] = OAK_STAIR_UP
shell(12, 58, 66, 70, 77, OAK_WALL, OAK_FLOOR)
for x in range(59, 70):                             # landlady's rooms
    T[12][71][x] = OAK_WALL
T[12][71][64] = 0
T[12][75][68] = OAK_STAIR_DOWN
frect(13, 58, 66, 70, 77, THATCH_FLOOR)
mk(11, "light_source", "lamp_lantern_room", 63, 65, luminance=22)
mk(11, "script_anchor", "business_k05_lantern_room_anchor", 64, 71)
# Lighter tavern-convention treatment (design 3.1): already has a hearth -- just seat markers
# around the existing hearth+counter.
mk(11, "script_anchor", "patron_seat_lantern_01_anchor", 59, 71)
mk(11, "script_anchor", "patron_seat_lantern_02_anchor", 61, 72)
mk(11, "script_anchor", "patron_seat_lantern_03_anchor", 62, 72)
mk(11, "script_anchor", "patron_seat_lantern_04_anchor", 65, 73)

# K07 The Ropewalk (the district's longest sightline; fire tier High) -- 64x9,
# deliberately kept elongated (canon: the ward's one 60-tile-shed trade)
shell(11, 4, 82, 67, 90, TRUDGEON_WALL, DIRT_FLOOR,
      doors=[(4, 85), (4, 86), (67, 85), (67, 86)])
cells(11, [(12, 85), (20, 86), (28, 85), (36, 86), (44, 85), (52, 86), (60, 85)],
      OAK_WALL)                                     # rope-laying posts
frect(12, 4, 82, 67, 90, THATCH_FLOOR)
shell(11, 60, 91, 66, 94, THATCH_WALL, DIRT_FLOOR, skip_sides=("n",))  # hemp lean-to
frect(12, 60, 91, 66, 94, THATCH_FLOOR)             # DEV: lean-to roof (unspecified)
mk(11, "script_anchor", "business_k07_ropewalk_anchor", 36, 85)

# K08 Brann's Chandlery (+ the gray-ledger cellar) -- 8x9, THE flagged oversized
# "plain shop" (was 14x14, identical to K05's old footprint); brought down near
# the new shop standard, freeing the lot east of it for K27 (the Hardtack Oven)
shell(11, 24, 66, 31, 74, OAK_WALL, OAK_FLOOR, doors=[(27, 66), (28, 66)])
for x in range(25, 31):                             # stockroom partition
    T[11][71][x] = OAK_WALL
T[11][71][27] = 0
cells(11, [(25, 73), (30, 73)], OAK_WALL)
# Earthbound/Stardew Valley shop convention (design 3.2): shelving lines two walls, a counter
# sits near the door, distinct trade-flavored fixtures -- rope racks + oil-lamp shelving here.
cells(11, [(25, 68), (25, 69)], OAK_WALL)           # west-wall rope racks
cells(11, [(30, 68), (30, 69)], OAK_WALL)           # east-wall oil-lamp shelving
cells(11, [(26, 70), (29, 70)], OAK_WALL)           # sales counter (x27/28 kept clear, door-aligned)
cells(11, [(27, 73)], OAK_WALL)                     # stockroom rope-coil pile
shell(10, 26, 70, 30, 73, GRANITE_WALL, GRANITE_FLOOR)   # cellar carve (gray ledger)
T[10][72][28] = OAK_STAIR_UP
T[11][72][28] = OAK_STAIR_DOWN
frect(12, 24, 66, 31, 74, THATCH_FLOOR)
mk(11, "script_anchor", "business_k08_branns_anchor", 27, 69)
mk(10, "script_anchor", "clue_brann_grayledger_anchor", 28, 71)
mk(11, "light_source", "lamp_branns_door", 27, 65, luminance=14)

# K09 Pitchfield (tar yard, west fire-sort; getilia-soaked firebreak fence) --
# shed shrunk to 9x6; the fenced 28x23 tar yard itself is unchanged
border(11, 2, 36, 29, 58, GETILIA_WALL)
for gx in range(14, 18):
    T[11][36][gx] = 0                               # gate
shell(11, 6, 44, 14, 49, TRUDGEON_WALL, DIRT_FLOOR, skip_sides=("n",))  # cauldron shed
cells(11, [(9, 47), (12, 47)], GRANITE_WALL)        # cauldrons
frect(12, 6, 44, 14, 49, BRICK_FLOOR)               # tile roof at the tar yard
for bx in (20, 22, 24, 26, 28):                     # tar barrel grid
    for by in (40, 43, 46, 49, 52, 55):
        if (bx, by) != (24, 46):                    # aisle
            T[11][by][bx] = OAK_WALL
mk(11, "light_source", "lamp_pitchfield_cauldron", 10, 47, luminance=16)
mk(11, "script_anchor", "business_k09_pitchfield_anchor", 10, 46)

# K10 Dawnstalls (open-air fish market) -- pinned bounding box x40-50/y36-47,
# distinctness marker only; furniture unchanged (already within the box)
for sy in (38, 42, 46):
    trect(11, 40, sy, 43, sy, TRUDGEON_WALL)
    trect(11, 48, sy, 50, sy, TRUDGEON_WALL)
T[11][40][46] = GRANITE_WALL                        # auction block
# Dressing pass (slab-clutter): a fishmonger's well, extra stall counters, and produce
# crates in the empty south/margins; central x46 spine (muster->auction->anchor) stays clear.
trect(11, 37, 47, 38, 48, GRANITE_WALL)             # fishmonger's well, SW corner
cells(11, [(53, 46), (53, 47), (37, 38), (37, 39), (44, 45), (45, 45)], TRUDGEON_WALL)  # extra fish stalls
cells(11, [(40, 48), (41, 48), (48, 48), (49, 48), (51, 40), (51, 41), (40, 44), (41, 44)], OAK_WALL)  # produce crates
mk(11, "script_anchor", "business_k10_dawnstalls_anchor", 46, 41)
mk(11, "script_anchor", "muster_dawnstalls_anchor", 46, 37)

# K11 Salt Row (gutting sheds + smokehouses) -- 16x9, same lot
for (sx0, sx1) in ((38, 41), (43, 46), (48, 51)):
    shell(11, sx0, 50, sx1, 53, TRUDGEON_WALL, DIRT_FLOOR, skip_sides=("n",))
for (mx0, mx1, d, hx) in ((39, 42, (40, 54), 41), (47, 50, (48, 54), 49)):
    shell(11, mx0, 54, mx1, 58, GRANITE_WALL, DIRT_FLOOR, doors=[d])
    T[11][57][hx] = GRANITE_WALL                    # contained hearth
mk(11, "script_anchor", "business_k11_saltrow_anchor", 45, 51)

# K12 The King's Bond (bonded warehouse: brick, one double door, no windows) -- 17x13
shell(11, 82, 34, 98, 46, BRICK_WALL, BRICK_FLOOR, doors=[(89, 34), (90, 34)])
# Rome cue (DECISIONS.md Art register FIFTH revision, Eli 2026-07-15): north edge is the
# street-facing frontage (its one double door). Overwrite with the civic-facade material,
# re-punch the doors afterward.
trect(11, 82, 34, 98, 34, BRICK_FACADE_WALL)
for (dx, dy) in ((89, 34), (90, 34)):
    T[11][dy][dx] = 0
    F[11][dy][dx] = BRICK_FLOOR
trect(11, 84, 42, 85, 43, OAK_WALL)                 # crates
trect(11, 93, 38, 94, 39, OAK_WALL)
# Dressing pass (slab-clutter): bonded goods -- standardized crate stacks packing the four
# quadrants (N-S spine x89-90 and E-W cross-aisle y40-41 stay clear: door(89-90,34) ->
# anchor(90,40) -> south wall). SW grid origin x84 aligns to the existing (84-85,42-43)
# stack so no aisle seals; existing stacks are auto-skipped by crate_grid's open-floor guard.
for (qx0, qy0, qx1, qy1) in [(83, 35, 88, 39), (91, 35, 97, 39), (84, 42, 88, 45), (91, 42, 97, 45)]:
    crate_grid(11, qx0, qy0, qx1, qy1)
trect(11, 87, 44, 88, 44, LEATHER_WALL)             # a pallet of sacks in the SW bay
frect(12, 82, 34, 98, 46, BRICK_FLOOR)
mk(11, "script_anchor", "business_k12_kingsbond_anchor", 90, 40)
mk(11, "script_anchor", "watch_bond_post_anchor", 90, 33)

# K13 The Drowned Hold (condemned hulk; Gullet-only entry; no lamps) -- 12x21,
# stays sprawling on purpose, trimmed 1-2 tiles off the old 13x23
shell(11, 178, 34, 189, 54, TRUDGEON_WALL, doors=[(178, 49)])
for rot in ((183, 34), (188, 54), (178, 41)):       # rot gaps in the shell
    T[11][rot[1]][rot[0]] = 0
for y in range(35, 54):                             # oak floor, sagging NE quadrant
    for x in range(179, 189):
        if 184 <= x <= 188 and 36 <= y <= 43:
            F[11][y][x] = 0                         # truly OPEN: drops to the undercellar
        else:
            F[11][y][x] = OAK_FLOOR
for d in ((178, 49), (183, 34), (178, 41), (188, 54)):
    F[11][d[1]][d[0]] = OAK_FLOOR                   # thresholds under the gaps
cells(11, [(180, 45), (182, 51), (186, 49), (188, 46)], OAK_WALL)  # debris
T[11][53][187] = OAK_STAIR_UP
shell(12, 178, 46, 189, 54, TRUDGEON_WALL, OAK_FLOOR)  # 2nd story, south half only
F[12][48][181] = 0                                  # floor holes
F[12][52][185] = 0
T[12][53][187] = OAK_STAIR_DOWN
# (no roof: z13 stays empty — open to sky)
for y in range(36, 44):                             # undercellar stub z10
    for x in range(180, 188):
        T[10][y][x] = 0
        F[10][y][x] = GRANITE_FLOOR
        if x >= 186:
            FL[10][y][x] = WATER2                   # permanent shin-wet east rooms
mk(11, "script_anchor", "clue_c3_drowned_hold", 184, 47)
mk(10, "script_anchor", "dungeon_seam_drowned_hold", 184, 40)

# K14 Wrackhouse (salvage house) -- 10x9
shell(11, 164, 34, 173, 42, TRUDGEON_WALL, DIRT_FLOOR, doors=[(168, 34), (169, 34)])
trect(11, 165, 38, 166, 38, OAK_WALL)               # salvage racks
trect(11, 171, 38, 172, 38, OAK_WALL)
T[11][36][172] = STEEL_WALL                         # the diving bell
for x in range(165, 173):                           # stockroom partition
    T[11][39][x] = OAK_WALL
T[11][39][168] = 0
frect(12, 164, 34, 173, 42, TRUDGEON_FLOOR)
mk(11, "script_anchor", "business_k14_wrackhouse_anchor", 168, 37)
mk(11, "script_anchor", "clue_wrackhouse_salvage_anchor", 170, 40)

# K15 Fenner's Pawn (deliberately cramped; caged counter) -- 7x7, pushed BELOW
# the new shop standard per the gazetteer's own "deliberately cramped" language
shell(11, 122, 52, 128, 58, BRICK_WALL, BRICK_FLOOR, doors=[(125, 52)])
for x in range(123, 128):                           # cage partition with slot
    T[11][54][x] = STEEL_WALL
T[11][54][125] = 0
T[11][57][127] = OAK_WALL                           # strongbox
frect(12, 122, 52, 128, 58, BRICK_FLOOR)
mk(11, "script_anchor", "business_k15_fenners_anchor", 125, 56)
mk(11, "script_anchor", "fenner_sign_anchor", 125, 51)
mk(11, "light_source", "lamp_fenners_door", 125, 51, luminance=10)

# K17 Mission of the Flame (+ garden) -- 17x15
shell(11, 82, 66, 98, 80, GRANITE_WALL, BRICK_FLOOR,
      doors=[(88, 66), (89, 66), (82, 73), (82, 74)])
# Rome cue (DECISIONS.md Art register FIFTH revision, Eli 2026-07-15): north edge is the
# street-facing frontage (its main doors); the west-edge garden door (82,73)/(82,74) stays
# plain granite -- only the street face gets the colonnade treatment.
trect(11, 82, 66, 98, 66, GRANITE_FACADE_WALL)
for (dx, dy) in ((88, 66), (89, 66)):
    T[11][dy][dx] = 0
    F[11][dy][dx] = BRICK_FLOOR
for y in range(67, 80):                             # chapel partition
    T[11][y][90] = OAK_WALL
T[11][71][90] = 0
trect(11, 84, 69, 87, 69, OAK_WALL)                 # alms-hall tables
trect(11, 84, 72, 87, 72, OAK_WALL)
# Compound/civic-hall convention (Ald-ruhn "big crab" reading + Earthbound church-pew
# register, design 3.3): pew rows flanking the tables, real bunks in the dormitory.
cells(11, [(84, 68), (85, 68), (86, 68), (87, 68)], GRANITE_WALL)   # pew row, north
cells(11, [(84, 73), (85, 73), (86, 73), (87, 73)], GRANITE_WALL)   # pew row, south
for x in range(83, 90):                             # dormitory partition
    T[11][75][x] = OAK_WALL
T[11][75][86] = 0
cells(11, [(84, 77), (86, 77), (88, 77)], CLOTH_WALL)   # bunks, row 1
cells(11, [(84, 79), (86, 79), (88, 79)], CLOTH_WALL)   # bunks, row 2
for x in range(91, 98):                             # back room (the body)
    T[11][75][x] = OAK_WALL
T[11][75][94] = 0
frect(12, 82, 66, 98, 80, THATCH_FLOOR)
mk(11, "script_anchor", "business_k17_mission_anchor", 88, 71)
mk(11, "script_anchor", "mission_bunks_anchor", 85, 78)
mk(11, "script_anchor", "clue_c1_mission_backroom", 94, 78)
mk(11, "light_source", "lamp_mission_night", 88, 65, luminance=22)
mk(11, "script_anchor", "mission_garden_anchor", 90, 88)

# K18 Squall's Bathhouse (real pooled water) -- 11x13
shell(11, 102, 66, 112, 78, GRANITE_WALL, GRANITE_FLOOR, doors=[(107, 66), (108, 66)])
cells(11, [(104, 75), (106, 75)], STEEL_WALL)       # boilers
T[11][75][103] = GRANITE_WALL                       # hearth
for y in range(69, 73):
    for x in range(105, 110):
        FL[11][y][x] = WATER2                       # the pools
frect(12, 102, 66, 112, 78, BRICK_FLOOR)
mk(11, "script_anchor", "business_k18_bathhouse_anchor", 107, 73)

# K19 The Rows (flophouse) -- 20x7
shell(11, 100, 52, 119, 58, TRUDGEON_WALL, DIRT_FLOOR, doors=[(104, 52), (105, 52)])
for hx in (102, 105, 108, 111, 114, 117):           # hammock posts
    T[11][54][hx] = OAK_WALL
    T[11][57][hx] = OAK_WALL
T[11][53][101] = OAK_WALL                           # landlord counter
frect(12, 100, 52, 119, 58, THATCH_FLOOR)
mk(11, "script_anchor", "business_k19_rows_anchor", 109, 55)

# K21 Saltgate Watch-Post (Band C: ground z13, roof z14) -- 10x10, unchanged;
# becomes the Rise's HEAD garrison, paired with the new K34 at the foot
shell(13, 62, 117, 71, 126, GRANITE_WALL, GRANITE_FLOOR, doors=[(71, 120), (71, 121)])
for y in range(118, 126):                           # cell partition
    T[13][y][64] = STEEL_WALL
T[13][121][64] = 0
cells(13, [(66, 118), (66, 119)], OAK_WALL)         # bunks
frect(14, 62, 117, 71, 126, BRICK_FLOOR)
T[13][119][80] = STEEL_WALL                         # gibbet cage on the row
mk(13, "script_anchor", "business_k21_watchpost_anchor", 67, 122)
mk(13, "script_anchor", "notice_board_anchor", 73, 119)
mk(13, "script_anchor", "gibbet_anchor", 80, 119)
mk(13, "light_source", "lamp_watchpost_brazier", 73, 118, luminance=20)

# K22 Netmenders' Arcade (leaky colonnade fronting Dawnstalls) -- 15x2
cells(11, [(38, 35), (41, 35), (45, 35), (48, 35), (52, 35)], OAK_WALL)
frect(12, 38, 34, 52, 35, THATCH_FLOOR)
mk(11, "script_anchor", "business_k22_netmenders_anchor", 45, 34)

# K23 Cooper & Blockmaker (sawdust register; open double doors) -- 14x11
shell(11, 40, 66, 53, 76, TRUDGEON_WALL, DIRT_FLOOR, doors=[(46, 66), (47, 66)])
trect(11, 41, 73, 42, 74, OAK_WALL)                 # barrel stacks
trect(11, 50, 68, 51, 69, OAK_WALL)
trect(11, 44, 71, 47, 71, OAK_WALL)                 # workbench
# Earthbound/Stardew Valley shop convention (design 3.2): stave stacks + workbench tools --
# the cooper's own trade-flavored fixtures, distinct from Brann's rope/lamp register.
trect(11, 48, 73, 49, 74, OAK_WALL)                 # stave stack, mirrors the barrel stack
cells(11, [(43, 68), (45, 68)], OAK_WALL)           # stave-drying rack, north wall
cells(11, [(44, 72), (47, 72)], OAK_WALL)           # tool cells flanking the workbench
frect(12, 40, 66, 53, 76, THATCH_FLOOR)
mk(11, "script_anchor", "business_k23_coopers_anchor", 47, 70)

# K24 The Eel-Pots (lantern-lit night stalls on the Tarwalk) -- 28x3, the extra
# tile of depth is a stallfront apron
for i, sx in enumerate((84, 92, 100, 108)):
    trect(11, sx, 30, sx + 1, 31, TRUDGEON_WALL)
    mk(11, "light_source", "lamp_eelpot_%02d" % (i + 1), sx + 1, 32, luminance=18)
frect(11, 84, 32, 111, 32, DIRT_FLOOR)              # stallfront apron
mk(11, "script_anchor", "business_k24_eelpots_anchor", 97, 31)

# K25 Kennel Row (rat-catchers' yard + small shed) -- 11x8
border(11, 164, 48, 174, 55, TRUDGEON_WALL)
for g in ((167, 48), (168, 48)):
    T[11][g[1]][g[0]] = 0
shell(11, 164, 51, 168, 55, TRUDGEON_WALL, DIRT_FLOOR, skip_sides=("e",))
cells(11, [(170, 49), (172, 49), (170, 52), (172, 52), (170, 54), (172, 54)],
      STEEL_WALL)
mk(11, "script_anchor", "business_k25_kennelrow_anchor", 167, 54)
mk(11, "script_anchor", "kennel_dog_anchor_01", 165, 54)
mk(11, "script_anchor", "kennel_dog_anchor_02", 171, 50)
mk(11, "script_anchor", "kennel_dog_anchor_03", 171, 53)

# K26 Sailmaker's Loft (formalizes the previously-unkeyed sailmaker structure into
# the roster; net-mending/sail-repair, distinct trade from Brann's general
# chandlery) -- 8x8, shrunk into the same lot
shell(11, 8, 70, 15, 77, TRUDGEON_WALL, OAK_FLOOR, doors=[(11, 70), (12, 70)])
trect(11, 9, 74, 10, 75, OAK_WALL)                  # canvas-cutting table
mk(11, "script_anchor", "business_k26_sailmaker_anchor", 11, 73)
mk(11, "light_source", "lamp_sailmaker_door", 11, 69, luminance=12)

# K27 The Hardtack Oven (ship's-biscuit bakery, distinct from Brann's -- which
# only stocks biscuit, doesn't bake it) -- 7x9, sited in the lot Brann's resize
# freed; granite shell per the Salt Row smokehouse fire-safety precedent
shell(11, 32, 70, 38, 78, GRANITE_WALL, OAK_FLOOR, doors=[(35, 70)])
cells(11, [(33, 75), (34, 75)], GRANITE_WALL)       # bake oven
trect(11, 36, 73, 37, 73, OAK_WALL)                 # counter
mk(11, "script_anchor", "business_k27_hardtack_anchor", 35, 74)
mk(11, "light_source", "lamp_hardtack_oven", 33, 75, luminance=10)

# K28 The Slop-Chest (sailors' clothing/dry-goods; the plainest shop in the
# ward) -- 6x7. DEV (overlap-audit pass, Eli 2026-07-13): the sizing pass's
# lot at (92,52)-(97,58) physically overlapped Hovel #3 (93,53)-(97,59) AND
# K34 The Guardhouse (hovels/establishments were only checked against each
# other's W x H size table, never against placed coordinates) -- relocated
# to the free lot immediately south of K15 Fenner's Pawn (122-128,52-58),
# still in the same pawn/flophouse/dry-goods cluster the design intent calls
# for ("outfit near where they bunk" -- K19 The Rows is a few tiles further
# west along the same row).
shell(11, 130, 58, 135, 64, OAK_WALL, OAK_FLOOR, doors=[(133, 58)])
trect(11, 132, 61, 134, 61, OAK_WALL)               # counter + racks
mk(11, "script_anchor", "business_k28_slopchest_anchor", 133, 62)

# K29 The Long Store (general dry-goods warehouse: open floor, racking, a
# foreman's desk nook, NO bed -- no canon night-watchman posted there, unlike
# the Weighhouse clerks) -- 19x11, genuinely different in character from K12
# (sealed/bonded) and K13 (condemned/decayed). DEV (overlap-audit pass, Eli
# 2026-07-13): the sizing pass's lot at (140,48)-(158,58) fully swallowed
# Hovel #45 (144,50)-(149,56) -- relocated to the free lot along Saltgate
# Rise's narrow southern leg, south of K17 Mission of the Flame and just
# east of the Rise itself: still "near foot traffic" (the Rise + Ropewynd
# corridor), and a plausible warehouse footprint hugging the road.
shell(11, 79, 82, 97, 92, BRICK_WALL, DIRT_FLOOR, doors=[(87, 82), (88, 82)])
for rx in (83, 89, 93):
    trect(11, rx, 85, rx + 1, 88, OAK_WALL)         # racking islands, aisles between
trect(11, 81, 89, 82, 90, OAK_WALL)                 # foreman's desk nook
# Dressing pass (slab-clutter): receiving strip -- extend the 3 racking islands up into
# y83-84 (taller stacks, no gaps); dispatch bay -- two free-standing crate islands + a
# sack pallet (anchor aisle x87-88 stays clear full height). Foreman's desk untouched.
for rx in (83, 89, 93):
    trect(11, rx, 83, rx + 1, 84, OAK_WALL)
trect(11, 84, 90, 85, 91, OAK_WALL)
trect(11, 90, 90, 91, 91, OAK_WALL)
trect(11, 95, 90, 96, 90, LEATHER_WALL)
frect(12, 79, 82, 97, 92, BRICK_FLOOR)              # flat roof, single story, no bed
mk(11, "script_anchor", "business_k29_longstore_anchor", 88, 87)

# K34 Guardhouse (Militia Watch foot-garrison; pairs with K21 at the Rise's
# head per DOCKS-GAZETTEER 2.4's own stated intent -- only the head post was
# ever authored) -- 13x9. DEV (overlap-audit pass, Eli 2026-07-13): the
# sizing pass's lot at (82,53)-(94,61) overlapped Hovels #1 and #2 AND K28
# (all three only ever cross-checked by W x H, never by placed coordinates)
# -- relocated to the free lot just south of Squall's Bathhouse (K18), still
# inside the same Band-A quay-civic cluster (Weighhouse/King's Bond/Mission/
# Bathhouse) it was meant to garrison, and still the "foot" counterpart to
# K21's post at the Rise's head.
# PASS 6 (Phase-1 living-docks): the guardhouse grows south from y88 to a new south
# wall at y92, and the old 2-tile cage is replaced by a real 6-cell prison block -- a
# corridor (y89) fronting six STEEL_WALL cells (floor cells at x101/103/105/107/109/111
# on y90, steel dividers between them at even x, a steel back wall at y91). Six cells x
# MAX_OCCUPANTS_PER_CELL(2) = 12 capacity. The watch room + armory + north doors are
# unchanged; cells fill at arrest time in a later justice pass (no actors spawned in them).
shell(11, 100, 80, 112, 92, GRANITE_WALL, GRANITE_FLOOR, doors=[(106, 80), (107, 80)])
for y in range(81, 88):                             # armory partition (unchanged)
    T[11][y][109] = OAK_WALL
T[11][85][109] = 0
trect(11, 105, 86, 107, 86, OAK_WALL)               # watch-room table (unchanged)
for dx in (102, 104, 106, 108, 110):                # steel dividers between the six cells
    T[11][90][dx] = STEEL_WALL
for x in range(101, 112):                           # steel cell back wall (south)
    T[11][91][x] = STEEL_WALL
frect(12, 100, 80, 112, 92, BRICK_FLOOR)            # roof (extended over the cell block)
mk(11, "script_anchor", "business_k34_guardhouse_anchor", 106, 85)
for i, cx in enumerate((101, 103, 105, 107, 109, 111)):
    mk(11, "script_anchor", "cell_k34_%02d_anchor" % (i + 1), cx, 90)
mk(11, "light_source", "lamp_guardhouse_door", 106, 79, luminance=18)

# PASS 5 (Phase-1 living-docks) -- K36 The Royal Counting-House (the ward's bank), on the
# one empty Band-A civic lot: x150-159 y48-59 (the Gullet G1 lane at x160-161 and Ropewynd
# at y60-65 are deliberately left intact east/south; hovel 45 abuts the west wall, the K06
# timber-store fence the north). Granite civic shell, a north door + interior queue lane, a
# STEEL_WALL vault ring enclosing ONE chest cell (the future Royal COIN vault -- Phase 2
# seeds it, NOT here), a teller counter + banker stand, two flanking guard posts.
shell(11, 150, 48, 159, 59, GRANITE_WALL, GRANITE_FLOOR, doors=[(154, 48)])
trect(11, 152, 52, 155, 52, OAK_WALL)               # teller counter (both flanks left open)
for (vx, vy) in ((151, 56), (153, 56), (151, 57), (153, 57),
                 (151, 58), (152, 58), (153, 58)):  # STEEL vault ring; (152,56) stays the door
    T[11][vy][vx] = STEEL_WALL                       # -> ONE enclosed chest cell at (152,57)
frect(12, 150, 48, 159, 59, BRICK_FLOOR)            # tile roof
mk(11, "script_anchor", "business_k36_bank_anchor", 154, 53)      # banker stand (behind counter)
mk(11, "script_anchor", "bank_queue_01_anchor", 154, 51)         # queue: front (at counter) -> back
mk(11, "script_anchor", "bank_queue_02_anchor", 154, 50)
mk(11, "script_anchor", "bank_queue_03_anchor", 154, 49)
mk(11, "script_anchor", "bank_vault_chest_anchor", 152, 57)      # future Royal COIN vault (empty now)
mk(11, "script_anchor", "guard_post_bank_west_anchor", 152, 53)
mk(11, "script_anchor", "guard_post_bank_east_anchor", 156, 53)
mk(11, "light_source", "lamp_bank_door", 154, 47, luminance=18)

# ======================================================================
# 3.5 Napkin-sketch ambient features (redesign, 2026-07-14): the West Garden
# Court, the Bilgewater Gap market-lane stall posts, and Cache Row. None of
# these are K-sites -- exempt from the establishment sizing standard, same
# tier as hovels/compound courtyards (DOCKS-GAZETTEER.md 3.1/7).
# ======================================================================

# West Garden Court -- the sketch's garden blob + 2 fenced plots, sited in the
# one open pocket in the west cluster: between K26 Sailmaker's Loft (x8-15)
# and K08 Brann's Chandlery (x24-31), Ropewynd (y60-65) to the north, the
# Walkback path (y78-81) to the south.
shell(11, 16, 66, 23, 69, OAK_WALL, DIRT_FLOOR, doors=[(19, 69), (20, 69)])
frect(11, 16, 70, 23, 73, DIRT_FLOOR)               # open courtyard blob, unwalled
shell(11, 16, 74, 23, 77, OAK_WALL, DIRT_FLOOR, doors=[(19, 74), (20, 74)])
mk(11, "script_anchor", "west_garden_court_anchor", 19, 71)
mk(11, "script_anchor", "west_garden_plot_01_anchor", 19, 67)
mk(11, "script_anchor", "west_garden_plot_02_anchor", 19, 75)

# East market lane: formalizing the Bilgewater Gap (x80-131 y50-51, already
# authored above) as a covered market walk with an open gutter -- 3 stall-awning
# posts, mirroring the Eel-Pots' post style.
cells(11, [(90, 50), (105, 50), (120, 50)], TRUDGEON_WALL)

# Cache Row -- 3 unlicensed off-grid sheds in Band B's unclaimed field (the
# sketch's 3 isolated far-right rectangles, read as a smuggling-adjacent
# unlicensed pocket). Cheap dirt-shell construction, no lamps, NO door onto
# any street layer -- access is across open Band-B dirt only.
# DEV (overlap-audit pass, 2026-07-14): the plan's original lot (166,101)-(173,114)
# stacked 3 sheds vertically inside what the plan called "Band B's one large
# unclaimed field, x164-191 y101-115" -- but that field is NOT actually unclaimed:
# Hovel #11 (168,103)-(173,108) sits directly in it (the plan checked hovels #12/#13
# but missed #11). Relocated to the genuinely free strip below the whole hovel row
# and the goat pen, y112-115 (after hovel #13 ends at y111; the goat pen is x146-158,
# west of this range) -- 3 sheds side-by-side instead of stacked.
CACHE_SHEDS = [
    (165, 112, 172, 115, "cache_shed_01_anchor", 168, 113),
    (174, 112, 181, 115, "cache_shed_02_anchor", 177, 113),
    (183, 112, 190, 115, "cache_shed_03_anchor", 186, 113),
]
for (sx0, sy0, sx1, sy1, name, ax, ay) in CACHE_SHEDS:
    shell(12, sx0, sy0, sx1, sy1, DIRT_WALL, DIRT_FLOOR)
    mk(12, "script_anchor", name, ax, ay)

# ======================================================================
# 3.6 Benches (2026-07-15 interior-detail pass, design 6): existing-material cells, no new
# art -- placed off the walking spine, in already-authored ambient/plaza floor.
# ======================================================================
cells(11, [(18, 71), (21, 71)], OAK_WALL)           # West Garden Court, flanking its anchor
cells(11, [(60, 29), (62, 29), (68, 29), (70, 29)], GRANITE_WALL)   # Weighhouse frontage plaza
cells(11, [(117, 29), (119, 29), (123, 29), (125, 29)], OAK_WALL)   # outside the Gilded Gull
cells(13, [(100, 120), (103, 120)], GRANITE_WALL)   # Gallows Row well plaza

# ======================================================================
# 3.7 Horse-riders scope call (2026-07-15 interior-detail pass, design 6): a "horse" flavor of
# the existing generic AnimalActor, tied at hitching posts -- no rider-coupling mechanic (see
# DocksPopulation.java for the reasoning). This also completes a pre-existing gap: the
# gazetteer names a carter (dray horse) as one of the ward's Animal Keepers, but only 3 of
# the 4 were ever spawned.
# DEV (this pass): the design's literal hitch coordinates for K03/K04 sat ON the building's own
# border-wall row (y34, solid GRANITE_WALL/TRUDGEON_WALL) or one step inside the door threshold
# -- moved to the open plaza/street row just outside each door (y33) so the post doesn't
# silently no-op on top of an existing wall tile and the horse doesn't spawn inside the tavern.
# K19's hitch was shifted a few tiles off-axis from its door for the same reason: the given
# coordinates sat directly in one of the door's two walkable columns, and a post there (a wall
# tile) would have narrowed 18 lodgers' own commute down to a single-tile lane.
mk(11, "script_anchor", "carter_stand_anchor", 50, 30)
cells(11, [(124, 33)], OAK_WALL)                    # hitch_gull post
mk(11, "script_anchor", "hitch_gull_anchor", 125, 33)
cells(11, [(103, 33)], OAK_WALL)                    # hitch_bilge post
mk(11, "script_anchor", "hitch_bilge_anchor", 102, 33)
cells(11, [(108, 51)], OAK_WALL)                    # hitch_rows post
mk(11, "script_anchor", "hitch_rows_anchor", 109, 51)

# ======================================================================
# 4. Residential Compounds C1-C4 (blueprint section 4)
# ======================================================================
def unit_anchor(z, name, x0, y0, x1, y1):
    cx, cy = center(x0, y0, x1, y1)
    mk(z, "script_anchor", name, cx, cy)


# --- C1 The Quayward Compound (grand, Band B: ground z12/upper z13/terrace z14)
# Grand mansion (2-story + private roof terrace — the no-slum wealth signal)
shell(12, 8, 97, 31, 115, REMAN_WALL, REMAN_FLOOR, doors=[(31, 105), (31, 106)])
for y in range(98, 115):
    T[12][y][20] = REMAN_WALL
T[12][105][20] = 0
for x in range(9, 20):
    T[12][106][x] = REMAN_WALL
# DEV (interiors plan §7, 2026-07-15): the mansion household anchor cmp1_mansion_anchor =
# center(8,97,31,115) = (19,106) previously landed ON this horizontal wall (gap was at
# (14,106)) -- a pre-existing anchor-on-wall defect that fails the walkable-anchor
# invariant. Fix: punch the gap AT the anchor cell (19,106) instead of (14,106). The
# west-south room still reaches the rest via (19,106)->(19,105)/(19,107); DocksPopulation
# C1_MANSION={19,106} is unchanged (no footprint/anchor move), the anchor is now walkable.
T[12][106][19] = 0
T[12][100][12] = OAK_STAIR_UP
# Homey touches (interiors plan §7, unblocked now the anchor is walkable): a hearth+table
# common in the east hall, sleeping beds/chests hugging the outer walls of all three rooms.
# Trap-free (blind flood from (19,106) reaches all interior cells); keeps the east door
# approach (30,105)/(30,106) and the stair (12,100) clear.
hearth(12, 21, 106)
table(12, 25, 106, 26, 106)
bed(12, 21, 98); bed(12, 22, 98); bed(12, 29, 98); chest(12, 30, 114)
bed(12, 9, 98); bed(12, 10, 98); chest(12, 9, 104)
bed(12, 9, 114); bed(12, 10, 114); chest(12, 9, 107)
# Dressing pass (slab-clutter): the great east hall is the one genuinely barren mansion
# room. OAK renders as a crate, so a genteel hall gets the "rug (floor variation)" treatment
# -- two checkered floor-medallion RUGS (floor_pave weave), each with a 1x3 trestle table,
# plus LEATHER presses/wardrobe hugging the walls. y105 spine + east door(30,105/106) +
# hearth(21,106) kept clear (owner commutes anchor->courtyard through here).
rug(12, 23, 100, 28, 103)                           # north medallion
rug(12, 23, 109, 27, 112)                           # south medallion
table(12, 24, 101, 26, 101)                         # trestle on north rug
table(12, 24, 110, 26, 110)                         # trestle on south rug
trect(12, 30, 100, 30, 103, LEATHER_WALL)           # press, east wall (door 30,105/106 clear below)
trect(12, 21, 100, 21, 101, LEATHER_WALL)           # linen press, west wall (hearth 21,106 clear below)
trect(12, 30, 111, 30, 113, LEATHER_WALL)           # wardrobe beside the existing chest (30,114)
shell(13, 8, 97, 31, 115, REMAN_WALL, REMAN_FLOOR)
# Rome cue (DECISIONS.md Art register FIFTH revision, Eli 2026-07-15): the compound's one
# public gate is the mansion shell's own east border (x=31, the doors at (31,105)/(31,106))
# -- overwrite just that column with the civic-facade material, both the ground story (z12,
# where the doors live -- re-punch them) and the solid upper story above the gate (z13, no
# door there, so the whole column stays solid facade).
trect(12, 31, 97, 31, 115, REMAN_FACADE_WALL)
for (dx, dy) in ((31, 105), (31, 106)):
    T[12][dy][dx] = 0
    F[12][dy][dx] = REMAN_FLOOR
trect(13, 31, 97, 31, 115, REMAN_FACADE_WALL)
for y in range(98, 115):
    T[13][y][20] = REMAN_WALL
T[13][105][20] = 0
for x in range(9, 20):
    T[13][106][x] = REMAN_WALL
T[13][106][14] = 0
T[13][100][12] = OAK_STAIR_DOWN
T[13][100][16] = OAK_STAIR_UP
frect(14, 8, 97, 31, 115, REMAN_FLOOR)              # roof terrace
border(14, 8, 97, 31, 115, REMAN_WALL)              # parapet
T[14][100][16] = OAK_STAIR_DOWN
for (px, py) in ((18, 102), (22, 102), (26, 102), (18, 110), (22, 110), (26, 110)):
    F[14][py][px] = DIRT_FLOOR                      # planters
unit_anchor(12, "cmp1_mansion_anchor", 8, 97, 31, 115)
# Wings: 6 ground condos + 6 uppers; roofs z14
C1_N = [(32, 43), (44, 55), (56, 67)]               # y 97-103, doors south
C1_S = [(32, 43), (44, 55), (56, 67)]               # y 110-115, doors north
C1_STAIRS_N = ((34, 99), (46, 99), (58, 99))
C1_STAIRS_S = ((34, 113), (46, 113), (58, 113))
for i, (x0, x1) in enumerate(C1_N):
    d = ((x0 + 5, 103),)
    shell(12, x0, 97, x1, 103, REMAN_WALL, REMAN_FLOOR, doors=d)
    # Meaningful rooms + homey touches (design §3/§6): common/hearth room + sleeping
    # alcove, anchor/door/stair kept clear by the fixture's construction.
    compound_unit_interior(12, x0, 97, x1, 103, (x0 + 5, 103))
    T[12][C1_STAIRS_N[i][1]][C1_STAIRS_N[i][0]] = OAK_STAIR_UP
    shell(13, x0, 97, x1, 103, REMAN_WALL, REMAN_FLOOR)
    compound_unit_interior(13, x0, 97, x1, 103, (x0 + 5, 103))
    T[13][C1_STAIRS_N[i][1]][C1_STAIRS_N[i][0]] = OAK_STAIR_DOWN
    frect(14, x0, 97, x1, 103, REMAN_FLOOR)
    unit_anchor(12, "cmp1_condo_%02d_anchor" % (i + 1), x0, 97, x1, 103)
    unit_anchor(13, "cmp1_condo_%02d_anchor" % (i + 7), x0, 97, x1, 103)
for i, (x0, x1) in enumerate(C1_S):
    d = ((x0 + 5, 110),)
    shell(12, x0, 110, x1, 115, REMAN_WALL, REMAN_FLOOR, doors=d)
    compound_unit_interior(12, x0, 110, x1, 115, (x0 + 5, 110))
    T[12][C1_STAIRS_S[i][1]][C1_STAIRS_S[i][0]] = OAK_STAIR_UP
    shell(13, x0, 110, x1, 115, REMAN_WALL, REMAN_FLOOR)
    compound_unit_interior(13, x0, 110, x1, 115, (x0 + 5, 110))
    T[13][C1_STAIRS_S[i][1]][C1_STAIRS_S[i][0]] = OAK_STAIR_DOWN
    frect(14, x0, 110, x1, 115, REMAN_FLOOR)
    unit_anchor(12, "cmp1_condo_%02d_anchor" % (i + 4), x0, 110, x1, 115)
    unit_anchor(13, "cmp1_condo_%02d_anchor" % (i + 10), x0, 110, x1, 115)
# DEV: ring-completion segments (unit walls leave the NE/SE perimeter open):
for x in range(68, 72):
    T[12][97][x] = REMAN_WALL
    T[12][115][x] = REMAN_WALL
for y in range(97, 116):
    if not 104 <= y <= 107:                         # gate pierce onto Saltgate
        T[12][y][71] = REMAN_WALL
mk(12, "script_anchor", "cmp1_courtyard_anchor", 43, 107)
mk(12, "light_source", "lamp_cmp1_gate", 70, 105, luminance=18)

# --- C2 Netters' Compound (mid, precedent-scale, Band A: z11/z12/z13)
shell(11, 116, 66, 127, 93, REMAN_WALL, REMAN_FLOOR, doors=[(127, 79), (127, 80)])
frect(12, 116, 66, 127, 93, REMAN_FLOOR)
unit_anchor(11, "cmp2_mansion_anchor", 116, 66, 127, 93)
C2_GROUND = [  # (x0,y0,x1,y1,door)
    (128, 66, 135, 75, (131, 75)),                  # c01 north-A
    (144, 66, 151, 75, (147, 75)),                  # c02 north-B
    (128, 86, 147, 93, (137, 86)),                  # c03 south
    (152, 66, 163, 75, (152, 70)),                  # c04 east
    (152, 76, 163, 84, (152, 80)),                  # c05 east
    (152, 85, 163, 93, (152, 89)),                  # c06 east
]
for i, (x0, y0, x1, y1, d) in enumerate(C2_GROUND):
    shell(11, x0, y0, x1, y1, REMAN_WALL, REMAN_FLOOR, doors=[d])
    unit_anchor(11, "cmp2_condo_%02d_anchor" % (i + 1), x0, y0, x1, y1)
    if i < 3:
        frect(12, x0, y0, x1, y1, REMAN_FLOOR)      # single-story roof deck
C2_STAIRS = ((155, 70), (155, 80), (155, 90))       # east wing z11->z12
for i, (x0, y0, x1, y1, _) in enumerate(C2_GROUND[3:]):
    sx, sy = C2_STAIRS[i]
    T[11][sy][sx] = OAK_STAIR_UP
    shell(12, x0, y0, x1, y1, REMAN_WALL, REMAN_FLOOR)
    T[12][sy][sx] = OAK_STAIR_DOWN
    unit_anchor(12, "cmp2_condo_%02d_anchor" % (i + 7), x0, y0, x1, y1)
# --- C2 meaningful rooms + homey touches (interiors plan §3). Explicit primitive calls
# (not compound_unit_interior, which fits only C1's 12x7 S-door condos): one straight
# partition per unit + wall-hugging furniture => two/three connected rooms, anchor/door/
# stair always on open floor. Blind flood-verified from each anchor (verify_interiors.py).
# C2 mansion (116-127,66-93): 3 rooms, central common holds the E door + anchor (121,79).
partition(11, 117, 75, 126, 75, REMAN_WALL, (121, 75))
partition(11, 117, 84, 126, 84, REMAN_WALL, (121, 84))
bed(11, 117, 67); bed(11, 118, 67); bed(11, 119, 67); bed(11, 125, 67); bed(11, 126, 67)
chest(11, 117, 74); chest(11, 126, 74)
hearth(11, 117, 79); table(11, 118, 82, 119, 82); table(11, 123, 82, 124, 82); chest(11, 126, 82)
bed(11, 117, 92); bed(11, 118, 92); bed(11, 125, 92); bed(11, 126, 92); chest(11, 117, 85)
rug(11, 119, 77, 123, 78)                           # dressing: mid great-room medallion (anchor 121,79 clear)
# c01 (128-135,66-75): 8-wide -> horizontal split (sleeping N | common S).
partition(11, 129, 69, 134, 69, REMAN_WALL, (131, 69))
bed(11, 129, 67); bed(11, 130, 67); chest(11, 134, 67)
hearth(11, 129, 74); table(11, 133, 74, 134, 74)
# c02 (144-151,66-75): c01 shifted +16x.
partition(11, 145, 69, 150, 69, REMAN_WALL, (147, 69))
bed(11, 145, 67); bed(11, 146, 67); chest(11, 150, 67)
hearth(11, 145, 74); table(11, 149, 74, 150, 74)
# c03 (128-147,86-93): 18-wide -> three rooms (two vertical partitions); door+anchor central.
partition(11, 133, 87, 133, 92, REMAN_WALL, (133, 89))
partition(11, 142, 87, 142, 92, REMAN_WALL, (142, 89))
bed(11, 129, 87); bed(11, 130, 87); chest(11, 129, 92)
hearth(11, 134, 92); table(11, 139, 92, 140, 92)
bed(11, 145, 87); bed(11, 146, 87); chest(11, 146, 92)
# c04 (152-163,66-75): W door + interior stair (155,70) -> west common | east sleeping.
partition(11, 159, 67, 159, 74, REMAN_WALL, (159, 70))
bed(11, 161, 67); bed(11, 162, 67); chest(11, 162, 74)
hearth(11, 153, 74); table(11, 157, 74, 158, 74); chest(11, 158, 67)
# c05 (152-163,76-84).
partition(11, 159, 77, 159, 83, REMAN_WALL, (159, 80))
bed(11, 161, 77); bed(11, 162, 77); chest(11, 162, 83)
hearth(11, 153, 83); table(11, 157, 83, 158, 83); chest(11, 158, 77)
# c06 (152-163,85-93).
partition(11, 159, 86, 159, 92, REMAN_WALL, (159, 89))
bed(11, 161, 86); bed(11, 162, 86); chest(11, 162, 92)
hearth(11, 153, 86); table(11, 157, 92, 158, 92); chest(11, 158, 86)
# c04u/c05u/c06u (z12 uppers): same calls, z=12 (no wall door; stair-only access).
partition(12, 159, 67, 159, 74, REMAN_WALL, (159, 70))
bed(12, 161, 67); bed(12, 162, 67); chest(12, 162, 74)
hearth(12, 153, 74); table(12, 157, 74, 158, 74); chest(12, 158, 67)
partition(12, 159, 77, 159, 83, REMAN_WALL, (159, 80))
bed(12, 161, 77); bed(12, 162, 77); chest(12, 162, 83)
hearth(12, 153, 83); table(12, 157, 83, 158, 83); chest(12, 158, 77)
partition(12, 159, 86, 159, 92, REMAN_WALL, (159, 89))
bed(12, 161, 86); bed(12, 162, 86); chest(12, 162, 92)
hearth(12, 153, 86); table(12, 157, 92, 158, 92); chest(12, 158, 86)
# Roof slum z13 over the east wing. DEV: roof stair moved (159,80)->(154,79)
# (the blueprint cell lands inside roofhut_11's rect).
frect(13, 152, 66, 163, 93, REMAN_FLOOR)
border(13, 152, 66, 163, 93, REMAN_WALL)
T[12][79][154] = OAK_STAIR_UP
T[13][79][154] = OAK_STAIR_DOWN
C2_HUTS = [  # (x0,y0,x1,y1,wall,door) — DEV: 1-wide hut doors invented (precedent)
    (153, 68, 157, 73, LEATHER_WALL, (155, 73)),    # roofhut_10
    (156, 76, 161, 82, CLOTH_WALL, (156, 79)),      # roofhut_11
    (153, 86, 157, 90, LEATHER_WALL, (155, 86)),    # roofhut_12
]
for i, (x0, y0, x1, y1, wall, d) in enumerate(C2_HUTS):
    frect(13, x0, y0, x1, y1, DIRT_FLOOR)
    shell(13, x0, y0, x1, y1, wall, doors=[d])
    F[13][d[1]][d[0]] = DIRT_FLOOR
    unit_anchor(13, "cmp2_roofhut_%02d_anchor" % (i + 10), x0, y0, x1, y1)
# DEV: ring-completion at the south gap x148-151 (open strip inside the ring).
for x in range(148, 152):
    T[11][93][x] = REMAN_WALL
mk(11, "script_anchor", "cmp2_courtyard_anchor", 137, 80)
mk(11, "light_source", "lamp_cmp2_gate", 139, 67, luminance=18)
mk(13, "light_source", "lamp_cmp2_roof", 158, 84, luminance=14)

# --- C3 Saltgate Terrace Compound (mid-cramped, Band B: z12/z13/z14)
shell(12, 84, 101, 95, 115, REMAN_WALL, REMAN_FLOOR, doors=[(95, 107), (95, 108)])
frect(13, 84, 101, 95, 115, REMAN_FLOOR)
unit_anchor(12, "cmp3_mansion_anchor", 84, 101, 95, 115)
C3_GROUND = [
    (124, 101, 131, 107, (124, 104)),               # c01 east
    (132, 101, 139, 107, (135, 107)),               # c02 east
    (96, 108, 106, 115, (101, 108)),                # c03 south
    (107, 108, 117, 115, (112, 108)),               # c04 south
    (118, 108, 128, 115, (123, 108)),               # c05 south
    (129, 108, 139, 115, (134, 108)),               # c06 south
]
for i, (x0, y0, x1, y1, d) in enumerate(C3_GROUND):
    shell(12, x0, y0, x1, y1, REMAN_WALL, REMAN_FLOOR, doors=[d])
    unit_anchor(12, "cmp3_condo_%02d_anchor" % (i + 1), x0, y0, x1, y1)
    if i < 2:
        frect(13, x0, y0, x1, y1, REMAN_FLOOR)      # east condos: flat roof
C3_STAIRS = ((98, 113), (109, 113), (120, 113), (131, 113))
for i, (x0, y0, x1, y1, _) in enumerate(C3_GROUND[2:]):
    sx, sy = C3_STAIRS[i]
    T[12][sy][sx] = OAK_STAIR_UP
    shell(13, x0, y0, x1, y1, REMAN_WALL, REMAN_FLOOR)
    T[13][sy][sx] = OAK_STAIR_DOWN
    unit_anchor(13, "cmp3_condo_%02d_anchor" % (i + 7), x0, y0, x1, y1)
# --- C3 meaningful rooms + homey touches (interiors plan §4). Same explicit-primitive
# idiom as C2; blind flood-verified from each anchor. C3 mansion (84-95,101-115): 2 rooms,
# south common holds the E door + anchor (89,108); column 89 spine kept clear.
partition(12, 85, 106, 94, 106, REMAN_WALL, (89, 106))
bed(12, 85, 102); bed(12, 86, 102); bed(12, 93, 102); bed(12, 94, 102)
chest(12, 85, 105); chest(12, 94, 105)
hearth(12, 85, 110); table(12, 87, 113, 88, 113); table(12, 90, 113, 91, 113); chest(12, 94, 114)
rug(12, 86, 111, 91, 112)                           # dressing: south great-room medallion (anchor 89,108 clear)
# c01 (124-131,101-107): W door -> west common (door+anchor) | east sleeping.
partition(12, 128, 102, 128, 106, REMAN_WALL, (128, 104))
bed(12, 129, 102); bed(12, 130, 102); chest(12, 130, 106)
hearth(12, 125, 102); table(12, 126, 106, 127, 106)
# c02 (132-139,101-107): S door.
partition(12, 136, 102, 136, 106, REMAN_WALL, (136, 104))
bed(12, 137, 102); bed(12, 138, 102); chest(12, 138, 106)
hearth(12, 133, 102); table(12, 133, 106, 134, 106)
# c03 (96-106,108-115): N door + stair (98,113) -> west common (door+stair+anchor) | east.
partition(12, 102, 109, 102, 114, REMAN_WALL, (102, 111))
bed(12, 104, 109); bed(12, 105, 109); chest(12, 105, 114)
hearth(12, 97, 109); table(12, 100, 114, 101, 114)
# c04 (107-117,108-115): stair (109,113).
partition(12, 113, 109, 113, 114, REMAN_WALL, (113, 111))
bed(12, 115, 109); bed(12, 116, 109); chest(12, 116, 114)
hearth(12, 108, 109); table(12, 111, 114, 112, 114)
# c05 (118-128,108-115): stair (120,113).
partition(12, 124, 109, 124, 114, REMAN_WALL, (124, 111))
bed(12, 126, 109); bed(12, 127, 109); chest(12, 127, 114)
hearth(12, 119, 109); table(12, 122, 114, 123, 114)
# c06 (129-139,108-115): stair (131,113).
partition(12, 135, 109, 135, 114, REMAN_WALL, (135, 111))
bed(12, 137, 109); bed(12, 138, 109); chest(12, 138, 114)
hearth(12, 130, 109); table(12, 133, 114, 134, 114)
# c03u/c04u/c05u/c06u (z13 uppers): same calls, z=13.
partition(13, 102, 109, 102, 114, REMAN_WALL, (102, 111))
bed(13, 104, 109); bed(13, 105, 109); chest(13, 105, 114)
hearth(13, 97, 109); table(13, 100, 114, 101, 114)
partition(13, 113, 109, 113, 114, REMAN_WALL, (113, 111))
bed(13, 115, 109); bed(13, 116, 109); chest(13, 116, 114)
hearth(13, 108, 109); table(13, 111, 114, 112, 114)
partition(13, 124, 109, 124, 114, REMAN_WALL, (124, 111))
bed(13, 126, 109); bed(13, 127, 109); chest(13, 127, 114)
hearth(13, 119, 109); table(13, 122, 114, 123, 114)
partition(13, 135, 109, 135, 114, REMAN_WALL, (135, 111))
bed(13, 137, 109); bed(13, 138, 109); chest(13, 138, 114)
hearth(13, 130, 109); table(13, 133, 114, 134, 114)
frect(14, 96, 108, 139, 115, REMAN_FLOOR)           # roof slum deck
border(14, 96, 108, 139, 115, REMAN_WALL)
T[13][113][114] = OAK_STAIR_UP
T[14][113][114] = OAK_STAIR_DOWN
C3_HUTS = [
    (100, 110, 104, 114, CLOTH_WALL, (104, 112)),   # roofhut_11
    (126, 109, 131, 114, LEATHER_WALL, (126, 111)),  # roofhut_12
]
for i, (x0, y0, x1, y1, wall, d) in enumerate(C3_HUTS):
    frect(14, x0, y0, x1, y1, DIRT_FLOOR)
    shell(14, x0, y0, x1, y1, wall, doors=[d])
    F[14][d[1]][d[0]] = DIRT_FLOOR
    unit_anchor(14, "cmp3_roofhut_%02d_anchor" % (i + 11), x0, y0, x1, y1)
# DEV: ring-completion: north wall over the courtyard mouth, minus the gate.
for x in range(96, 124):
    if not 110 <= x <= 113:
        T[12][101][x] = REMAN_WALL
mk(12, "script_anchor", "cmp3_courtyard_anchor", 111, 104)
mk(12, "light_source", "lamp_cmp3_gate", 111, 102, luminance=18)

# --- C4 The Gullet Compound (decayed, Band A: z11/z12/z13; brick+oak, no lamps)
C4_GROUND = [
    (166, 66, 171, 79, (171, 72)),                  # c01 west (DEV: door cell invented)
    (166, 80, 171, 93, (171, 86)),                  # c02 west (DEV: door cell invented)
    (172, 66, 175, 75, (173, 75)),                  # c03 north
    (180, 66, 190, 75, (185, 75)),                  # c04 north
]
for i, (x0, y0, x1, y1, d) in enumerate(C4_GROUND):
    shell(11, x0, y0, x1, y1, BRICK_WALL, OAK_FLOOR, doors=[d])
    frect(12, x0, y0, x1, y1, THATCH_FLOOR)         # DEV: roof material unspecified
    unit_anchor(11, "cmp4_condo_%02d_anchor" % (i + 1), x0, y0, x1, y1)
# --- C4 meaningful rooms + homey touches (interiors plan §5; BRICK_WALL partitions to
# match the brick shells). Layouts keep the intentional rot gaps (170,66)/(166,88)/(190,80)
# reachable. Blind flood-verified from each anchor.
# c01 (166-171,66-79): 4-wide -> horizontal split (common N with door+anchor | sleeping S).
partition(11, 167, 73, 170, 73, BRICK_WALL, (168, 73))
hearth(11, 167, 67); table(11, 169, 68, 170, 68); chest(11, 167, 72)
bed(11, 167, 74); bed(11, 170, 74); bed(11, 167, 78); chest(11, 170, 78)
# c02 (166-171,80-93): beds shifted off (167,88) so the (166,88) rot gap stays reachable.
partition(11, 167, 87, 170, 87, BRICK_WALL, (168, 87))
hearth(11, 167, 81); table(11, 169, 82, 170, 82)
bed(11, 169, 88); bed(11, 170, 88); bed(11, 167, 92); chest(11, 170, 92)
# c03 (172-175,66-75): 2-wide interior -> NO partition (would trap 1-wide rooms); col 173 spine.
hearth(11, 174, 67); bed(11, 174, 69); bed(11, 174, 71); chest(11, 174, 73)
# c04 (180-190,66-75): vertical split (west common with door+anchor | east sleeping).
partition(11, 186, 67, 186, 74, BRICK_WALL, (186, 70))
bed(11, 188, 67); bed(11, 189, 67); chest(11, 189, 74)
hearth(11, 181, 67); table(11, 181, 73, 182, 73)
shell(11, 184, 76, 190, 93, BRICK_WALL, OAK_FLOOR, doors=[(184, 84)])  # c05 (2-story)
T[11][80][186] = OAK_STAIR_UP
shell(12, 184, 76, 190, 93, BRICK_WALL, OAK_FLOOR)  # c06 upper
T[12][80][186] = OAK_STAIR_DOWN
unit_anchor(11, "cmp4_condo_05_anchor", 184, 76, 190, 93)
unit_anchor(12, "cmp4_condo_06_anchor", 184, 76, 190, 93)
# c05 (184-190,76-93): 5x16 tall -> horizontal split, north room holds the stair (186,80)
# and the (190,80) rot gap; south room holds the W door + anchor (187,84).
partition(11, 185, 82, 189, 82, BRICK_WALL, (187, 82))
bed(11, 185, 77); bed(11, 189, 77); chest(11, 185, 78)
hearth(11, 189, 83); table(11, 185, 91, 186, 91); bed(11, 188, 92); bed(11, 189, 92)
# c06 (upper of c05, z12): same calls, z=12 (no wall door, no rot gap; stair-only access).
partition(12, 185, 82, 189, 82, BRICK_WALL, (187, 82))
bed(12, 185, 77); bed(12, 189, 77); chest(12, 185, 78)
hearth(12, 189, 83); table(12, 185, 91, 186, 91); bed(12, 188, 92); bed(12, 189, 92)
# Collapsed south unit: perimeter only, no roof, rubble
border(11, 172, 86, 183, 93, BRICK_WALL)
cells(11, [(175, 89), (180, 91)], OAK_WALL)
mk(11, "script_anchor", "cmp4_ruin_anchor", 177, 89)
# Courtyard gone to trash
cells(11, [(174, 78), (180, 82), (177, 84), (182, 77)], OAK_WALL)
mk(11, "script_anchor", "cmp4_courtyard_anchor", 177, 80)
# Roof slum z13 over the east wing (brick parapet, broken at (190,84)).
# DEV: roofhut_09 shrunk to x184-187 (blueprint SE corner covered the roof stair).
frect(13, 184, 76, 190, 93, BRICK_FLOOR)
border(13, 184, 76, 190, 93, BRICK_WALL)
T[13][84][190] = 0                                  # broken parapet
T[12][90][188] = OAK_STAIR_UP
T[13][90][188] = OAK_STAIR_DOWN
C4_HUTS = [
    (184, 77, 187, 81, CLOTH_WALL, (185, 81)),      # roofhut_07
    (187, 82, 190, 85, LEATHER_WALL, (187, 83)),    # roofhut_08
    (184, 86, 187, 90, CLOTH_WALL, (187, 88)),      # roofhut_09
    (188, 91, 190, 93, LEATHER_WALL, (189, 91)),    # roofhut_10
]
for i, (x0, y0, x1, y1, wall, d) in enumerate(C4_HUTS):
    frect(13, x0, y0, x1, y1, DIRT_FLOOR)
    shell(13, x0, y0, x1, y1, wall, doors=[d])
    F[13][d[1]][d[0]] = DIRT_FLOOR
    unit_anchor(13, "cmp4_roofhut_%02d_anchor" % (i + 7), x0, y0, x1, y1)

# K35 The Skyrunner's Roost (Eli 2026-07-13 sizing pass, DOCKS-GAZETTEER.md 3.1) --
# 3x5, concealed nook on the Gullet Compound's own rooftop-slum deck (z:+13), in the
# open gap beside roofhut_09 (184-187,86-90) and roofhut_08 (187-190,82-85). Per
# DECISIONS.md's Trojian Compounds ruling, Skyrunners use the rooftop-slum layer as
# their highway and present as ordinary tenants -- this must NOT read as a shopfront:
# no lamp, no sign, and NO new door -- it reuses roofhut_09's own existing door gap
# at (187,88), so the only way in is through that hut's own wall.
frect(13, 188, 86, 190, 90, DIRT_FLOOR)
border(13, 188, 86, 190, 90, BRICK_WALL)
T[13][88][188] = 0                                  # opens onto roofhut_09's door (187,88)
F[13][88][188] = DIRT_FLOOR
mk(13, "script_anchor", "lair_skyrunner_anchor", 189, 88)

# Ring rot gaps (after all walls are up)
for rot in ((170, 66), (190, 80), (166, 88)):
    T[11][rot[1]][rot[0]] = 0

# ======================================================================
# 4.5 Compound farm plots (PASS 7, Phase-1 living-docks; Feature-5 prerequisite):
# farm_tile_* script_anchors on EXISTING walkable courtyard floor -- MARKERS ONLY, no
# material/gid change (plots are anchors, not tilled-soil art), placed clear of each
# courtyard's gate spine + existing courtyard/gate anchors. A later farm-yield pass binds
# farmers to them. C1/C3 courtyards are Band B (z:+12); C2's is Band A (z:+11).
# ======================================================================
for i, (fx, fy) in enumerate(((40, 105), (56, 105), (40, 108), (56, 108))):   # C1, avoid (43,107)
    mk(12, "script_anchor", "farm_tile_c1_%02d_anchor" % (i + 1), fx, fy)
for i, (fx, fy) in enumerate(((133, 78), (147, 78), (133, 83), (147, 83))):   # C2, avoid (137,80)
    mk(11, "script_anchor", "farm_tile_c2_%02d_anchor" % (i + 1), fx, fy)
for i, (fx, fy) in enumerate(((102, 103), (118, 103), (102, 106), (118, 106))):  # C3, avoid gate/anchor
    mk(12, "script_anchor", "farm_tile_c3_%02d_anchor" % (i + 1), fx, fy)

# ======================================================================
# 5. Hovels & shanties (blueprint section 5) + extras
# ======================================================================
# (n, x0, y0, x1, y1, door, band_z). Materials rotate by (n-1) % 4:
# 0 oak+thatch, 1 trudgeon+trudgeon, 2 dirt shell+thatch, 3 cloth tent (roofless).
HOVELS = [
    (1, 82, 52, 86, 58, (84, 52), 11), (2, 88, 52, 91, 57, (89, 52), 11),
    (3, 93, 53, 97, 59, (95, 53), 11), (4, 8, 92, 12, 95, (10, 92), 11),
    (5, 20, 92, 25, 95, (22, 92), 11), (6, 182, 61, 186, 65, (182, 63), 11),
    (7, 188, 61, 190, 64, (188, 62), 11),
    (8, 144, 101, 148, 105, (146, 105), 12), (9, 151, 102, 156, 107, (153, 107), 12),
    (10, 159, 101, 163, 106, (161, 106), 12), (11, 168, 103, 173, 108, (170, 108), 12),
    (12, 178, 101, 182, 105, (180, 105), 12), (13, 184, 106, 188, 111, (184, 108), 12),
    (14, 10, 116, 14, 119, (12, 119), 13), (15, 17, 116, 22, 119, (19, 119), 13),
    (16, 26, 116, 30, 119, (28, 119), 13), (17, 34, 116, 39, 119, (36, 119), 13),
    (18, 44, 116, 49, 119, (46, 119), 13), (19, 52, 116, 57, 119, (54, 119), 13),
    (20, 84, 116, 89, 119, (86, 119), 13), (21, 92, 116, 96, 119, (94, 119), 13),
    (22, 108, 116, 113, 119, (110, 119), 13), (23, 118, 116, 123, 119, (120, 119), 13),
    (24, 128, 116, 133, 119, (130, 119), 13), (25, 138, 116, 143, 119, (140, 119), 13),
    (26, 148, 116, 153, 119, (150, 119), 13), (27, 166, 116, 171, 119, (168, 119), 13),
    (28, 176, 116, 181, 119, (178, 119), 13),
    (29, 8, 123, 13, 126, (10, 123), 13), (30, 18, 123, 23, 126, (20, 123), 13),
    (31, 28, 123, 33, 126, (30, 123), 13), (32, 40, 123, 45, 126, (42, 123), 13),
    (33, 50, 123, 55, 126, (52, 123), 13), (34, 84, 123, 89, 126, (86, 123), 13),
    (35, 92, 123, 97, 126, (94, 123), 13), (36, 106, 123, 111, 126, (108, 123), 13),
    (37, 116, 123, 121, 126, (118, 123), 13), (38, 126, 123, 131, 126, (128, 123), 13),
    (39, 136, 123, 141, 126, (138, 123), 13), (40, 146, 123, 151, 126, (148, 123), 13),
    (41, 156, 123, 161, 126, (158, 123), 13), (42, 166, 123, 171, 126, (168, 123), 13),
    (43, 176, 123, 181, 126, (178, 123), 13),
    (44, 134, 50, 139, 55, (136, 50), 11), (45, 144, 50, 149, 56, (146, 50), 11),
]
HOVEL_KITS = [  # (shell gid, roof gid or None)
    (OAK_WALL, THATCH_FLOOR),
    (TRUDGEON_WALL, TRUDGEON_FLOOR),
    (DIRT_WALL, THATCH_FLOOR),
    (CLOTH_WALL, None),
]
for (n, x0, y0, x1, y1, door, z) in HOVELS:
    wall, roof = HOVEL_KITS[(n - 1) % 4]
    shell(z, x0, y0, x1, y1, wall, DIRT_FLOOR, doors=[door])
    if roof is not None:
        frect(z + 1, x0, y0, x1, y1, roof)
    cx, cy = center(x0, y0, x1, y1)
    # Homey touches (design §1/§2): hearth + beds + chest on the back strip opposite the
    # door; NO partition (hovels are too shallow to divide without trapping). Placed
    # before the anchor marker so the anchor/door spine stays clear by construction.
    hovel_touches(z, x0, y0, x1, y1, door, (cx, cy))
    mk(z, "script_anchor", "hovel_%02d_anchor" % n, cx, cy)

# Band B east field: goat pen
border(12, 146, 109, 158, 114, TRUDGEON_WALL)
T[12][109][151] = 0                                 # gate
mk(12, "script_anchor", "pen_goats_anchor", 152, 111)
# Band C well plaza
trect(13, 101, 120, 102, 121, GRANITE_WALL)
mk(13, "script_anchor", "well_gallows_row_anchor", 101, 122)

# ======================================================================
# 5.7 Frontage & plaza paving (2026-07-15 exteriors/streetscape pass, Eli:
# "it's a city so buildings would be smashed in together"). safe_sidewalk paints
# the periodic floor_pave weave onto bare exterior dirt only (walls/interiors/water
# auto-preserved), so adjacent buildings share a continuous laid-paver sidewalk and
# read as one dense block; the seams between near-touching buildings are filled.
# Working yards are DELIBERATELY excluded (kept as bare-dirt working ground for
# grit/contrast): tar yard K09, Ropewalk K07, Impound K02, Kennel K25, Harl's yard
# K06, West Garden, C4 courtyard, Cache Row, the Gullet dirt lanes, Band-B/C fields.
# DEV (implementation audit, this pass): cluster-F west-field rect trimmed x145->x143
# so it stops one cell short of hovel 8's west wall (x144) and never repaints a hovel
# interior; every other rect was audited to touch only exterior dirt + door thresholds.
# ======================================================================

# --- CLUSTER A: Quay fish-market plaza (Dawnstalls/Salt Row open market ground) ---
safe_sidewalk(11, 36, 36, 55, 49)     # the open market square; stall posts auto-skipped
safe_sidewalk(11, 51, 50, 55, 52)     # corridor Dawnstalls -> Weighhouse

# --- CLUSTER B: East Tarwalk shop-row seams (King's Bond | Bilge | Gilded Gull) ---
safe_sidewalk(11, 99, 34, 99, 46)     # King's Bond | Bilge seam
safe_sidewalk(11, 112, 34, 113, 47)   # Bilge | Gilded Gull seam

# --- CLUSTER C: quay-back hovels 1-3 + Rows/Fenner/Slop seams & Ropewynd frontage ---
safe_sidewalk(11, 87, 52, 87, 57)     # hovel 1 | hovel 2
safe_sidewalk(11, 92, 53, 92, 57)     # hovel 2 | hovel 3
safe_sidewalk(11, 98, 52, 99, 59)     # hovel 3 | The Rows
safe_sidewalk(11, 120, 52, 121, 58)   # The Rows | Fenner's Pawn
safe_sidewalk(11, 129, 58, 129, 64)   # Fenner | Slop-Chest
safe_sidewalk(11, 82, 59, 146, 59)    # frontage strip along Ropewynd's north kerb (y60)

# --- CLUSTER D: Mission / Bathhouse / Guardhouse civic island corridors ---
safe_sidewalk(11, 80, 66, 81, 88)     # west frontage (Saltgate east kerb x79 -> Mission)
safe_sidewalk(11, 99, 66, 99, 79)     # Mission | Bathhouse seam
safe_sidewalk(11, 100, 79, 112, 79)   # Bathhouse | Guardhouse seam
safe_sidewalk(11, 98, 89, 113, 93)    # south apron (east of Long Store) toward Backwall

# --- CLUSTER E: Ropewynd south shop-row frontage + inter-shop corridors ---
safe_sidewalk(11, 4, 66, 70, 66)      # frontage line just below Ropewynd south kerb (y65)
safe_sidewalk(11, 39, 70, 39, 78)     # Hardtack | Coopers seam
safe_sidewalk(11, 54, 66, 57, 77)     # Coopers | Lantern Room seam

# --- CLUSTER F: Band B (z12) frontage south of Terrace Walk ---
safe_sidewalk(12, 80, 101, 139, 101)  # frontage strip under Terrace Walk (C3 + hovel fronts)
safe_sidewalk(12, 140, 101, 143, 111) # west edge of the east-hovel field (trimmed off hovel 8)

# --- CLUSTER G: Band C (z13) Gallows Row well plaza ---
safe_sidewalk(13, 96, 119, 107, 123)  # pave the well plaza around the existing wellhead

# --- §5.7 plaza fixtures (single solid cells on open paved plaza; verified off every
#     anchor/door/path, each with >=3 walkable 4-neighbours so it never plugs a route) ---
cells(11, [(18, 28), (26, 28), (38, 28), (58, 28), (66, 28)], STEEL_WALL)   # Long-Quay mooring bollards
T[11][29][74] = GRANITE_WALL                        # public well, Weighhouse frontage plaza
cells(11, [(76, 31), (77, 31)], OAK_WALL)           # Weighhouse-plaza market crates
cells(11, [(52, 38), (53, 38), (52, 44), (37, 45)], OAK_WALL)  # fish-market fishmonger crates
cells(13, [(104, 121)], OAK_WALL)                   # Gallows well-plaza crate
cells(13, [(99, 120)], STEEL_WALL)                  # Gallows well-plaza hitching post

# --- Dressing pass: Weighhouse civic frontage plaza centerpiece (quay-edge band y26,
#     off the y28-33 Tarwalk through-lane) + market barrels beside the existing crates ---
cells(11, [(63, 26)], GRANITE_WALL)                 # brazier plinth (centerpiece)
mk(11, "light_source", "brazier_weighhouse_plaza", 63, 27, luminance=16)
cells(11, [(57, 26), (70, 26)], GRANITE_WALL)       # planter boxes flanking the frontage
cells(11, [(75, 32), (75, 33)], OAK_WALL)           # market barrels by the crates

# --- Dressing pass: Long Quay loading apron -- cargo stacks on the south apron rows
#     y32-33 only, clear of the y26-30 mustering strip and every berth/pier/crane anchor ---
for (cx, cy) in [(15, 32), (32, 32), (52, 32)]:
    crate_grid(11, cx, cy, cx + 2, cy + 1)          # cargo stacks between berths
cells(11, [(24, 33), (25, 33), (48, 33), (49, 33)], LEATHER_WALL)  # sack pallets

# --- Dressing pass: Gallows Row well plaza top-up (mirror crate + night-market brazier) ---
cells(13, [(105, 121)], OAK_WALL)                   # mirror crate (matches 104,121)
mk(13, "light_source", "brazier_gallows_plaza", 106, 120, luminance=14)

# ======================================================================
# 5.8 District densification -- pave the inter-building dirt (streetscape pass, Eli:
# "it's a city so logically the buildings would be smashed in together"). The wide plan
# still read as buildings floating as islands in wide bare-brown-dirt streets/lots. This
# pass paves the inter-building STREETS, connective LANES and CIVIC SQUARES so the district
# reads as continuous urban fabric, while PRESERVING deliberate grit (hovel/slum yards, the
# tar-yard, working yards, offal gutters, the condemned NE quarter). Paint-only: repaints
# bare exterior DIRT_FLOOR -> paver; walls/doors/interiors/water are auto-preserved by the
# guard, grit zones + a 1-cell hovel apron by _grit(). No T/FL/marker cell is touched, so
# no footprint moves and walkability (a function of T only) is provably unchanged.
# Two-tier hierarchy (the only two FLOOR gids that render correctly, swatch-verified):
# smooth brick spine (BRICK_FLOOR->floor_tile) for ARTERIALS vs the periodic weave
# (GRANITE_FLOOR->floor_pave) for every pedestrian/civic/lane fill.
# ======================================================================
GRIT_KEEP = [  # (z, x0, y0, x1, y1) -- dirt that STAYS dirt (deliberate grit; never paved)
    (11,   2, 36,  29, 58),   # K09 Pitchfield tar yard (fenced tar/pitch fire-sort ground)
    (11,  56, 53,  68, 58),   # K02 Impound Yard (spike-fenced working yard)
    (11, 164, 48, 174, 55),   # K25 Kennel Row yard (rat-catchers' fenced dog yard)
    (11, 150, 36, 159, 46),   # K06 Harl's timber store (fenced log-stack yard)
    (11,  16, 66,  23, 77),   # West Garden Court (garden allotment -- dirt is the feature)
    (11, 172, 76, 183, 93),   # C4 Gullet courtyard/ruin (decayed compound gone to trash)
    (11, 160, 30, 191, 75),   # Gullet/Wrackhouse NE condemned/decayed quarter
    (11, 162, 57, 191, 59),   # Gullet bottom link (poor-quarter back-lane)
    (11, 164, 66, 165, 95),   # Gullet G3 lane (poor-quarter back-lane)
    (11,  33, 26,  33, 59),   # Herring/Salt offal gutter
    (11,  34, 54,  55, 54),   # Salt Row offal-gutter link
    (11,  38, 50,  51, 58),   # K11 Salt Row gutting sheds/smokehouses (working sheds)
    (11,   4, 82,  67, 90),   # K07 Ropewalk (canon: the ward's one long clear-sightline dirt shed)
    (11,  60, 91,  66, 94),   # K07 hemp lean-to (dirt working floor under the ropewalk roof)
    (11, 100, 52, 119, 58),   # K19 The Rows flophouse (explicit squalor -- packed-dirt doss floor)
    (12, 146,109, 158,114),   # goat pen yard (livestock pen)
    (12, 165,112, 190,115),   # Cache Row sheds (unlicensed smuggling-pocket dirt shells)
]


def _grit(x, y, z):
    """True if (x,y,z) is deliberate grit that must stay bare dirt: an explicit GRIT_KEEP
    zone, or within a 1-cell apron of any hovel (packed-dirt slum yards, incl. the roofless
    cloth-tent interiors which are visible and must not be paved)."""
    for (kz, kx0, ky0, kx1, ky1) in GRIT_KEEP:
        if z == kz and kx0 <= x <= kx1 and ky0 <= y <= ky1:
            return True
    for (n, hx0, hy0, hx1, hy1, door, hz) in HOVELS:   # 1-cell slum-yard apron
        if z == hz and hx0 - 1 <= x <= hx1 + 1 and hy0 - 1 <= y <= hy1 + 1:
            return True
    return False


def pave_dirt(z, x0, y0, x1, y1, gid):
    """Guarded street paver: repaint a rect's bare exterior DIRT_FLOOR (terrain open,
    no fluid, not a grit cell) to gid. Walls/interiors/water/stairs and every grit zone
    are auto-skipped, so a rect may span whole buildings/yards and paint only the dirt
    seams between them -- same guarded idiom as safe_sidewalk, with grit-awareness added."""
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if T[z][y][x] == 0 and F[z][y][x] == DIRT_FLOOR and FL[z][y][x] == 0 \
               and not _grit(x, y, z):
                F[z][y][x] = gid


# --- ARTERIAL SPINES (smooth brick floor_tile) -- painted first so they win at overlaps ---
pave_dirt(11,  80, 50, 133, 51, BRICK_FLOOR)   # Bilgewater Gap market spine (E-W)
pave_dirt(11,  80, 94, 190, 95, BRICK_FLOOR)   # Backwall Alley service arterial
pave_dirt(13,   0,120, 191,122, BRICK_FLOOR)   # Gallows Row arterial spine (Band C)

# --- CIVIC WEAVE FILL (granite floor_pave) -- squares / frontages / lanes ---
# Band A (z11): the "islands in mud" merchant/civic core
pave_dirt(11,  78, 47, 159, 95, GRANITE_FLOOR) # east merchant heart (the big lot sea)
pave_dirt(11,  80, 34, 135, 46, GRANITE_FLOOR) # north shopfront seams up to the Tarwalk
pave_dirt(11,   0, 34,  31, 35, GRANITE_FLOOR) # west quay-back strip
pave_dirt(11,   4, 59,  31, 59, GRANITE_FLOOR) # lane south of the tar-yard fence
pave_dirt(11,   0, 60,   3, 95, GRANITE_FLOOR) # Pitch Lane, Band A leg (connective spine)
pave_dirt(11,  52, 50,  71, 59, GRANITE_FLOOR) # Weighhouse|Impound|Lantern|Saltgate junction
pave_dirt(11,  66, 82,  71, 90, GRANITE_FLOOR) # Lantern|Saltgate south seam
pave_dirt(11,   0, 66,  71, 81, GRANITE_FLOOR) # west shop row + Walkback path
pave_dirt(11,   0, 91,  71, 95, GRANITE_FLOOR) # west Band-A south strip
pave_dirt(11,   0, 26,  79, 33, GRANITE_FLOOR) # quay apron residual frontage (west)
pave_dirt(11,  80, 26, 129, 33, GRANITE_FLOOR) # Tarwalk east apron residual (eel-pot fronts)
# Band B (z12): compound courts + Terrace Walk frontage + connective field lanes
pave_dirt(12,   0, 97,   7,115, GRANITE_FLOOR) # Pitch Lane, Band B leg
pave_dirt(12,   4, 96,  71,115, GRANITE_FLOOR) # C1 courtyard + west field
pave_dirt(12,  80, 96, 143,115, GRANITE_FLOOR) # Terrace Walk frontage + C3 courtyard
pave_dirt(12, 144, 96, 191,115, GRANITE_FLOOR) # Band-B east field connective lanes
# Band C (z13): Gallows well plaza + hovel-row connective lanes (slum aprons kept)
pave_dirt(13,  96,119, 107,123, GRANITE_FLOOR) # Gallows Row well plaza
pave_dirt(13,   0,116, 191,119, GRANITE_FLOOR) # N hovel-row connective lanes
pave_dirt(13,   0,123, 191,127, GRANITE_FLOOR) # S hovel-row connective lanes

# ======================================================================
# 6. Patrol, muster, exits (blueprint section 6; script_anchors only —
# the importer supports only light_source/script_anchor marker classes)
# ======================================================================
mk(13, "script_anchor", "patrol_post_rise_top", 75, 118)
mk(11, "script_anchor", "patrol_post_rise_foot", 75, 34)
mk(11, "script_anchor", "patrol_post_tarwalk_west", 30, 30)
mk(11, "script_anchor", "patrol_post_tarwalk_mid", 100, 30)

# ======================================================================
# 6.5 Job-site zones (2026-07-15 senior-level-design pass, Eli directive #7:
# "mark logical job-site locations/zones for a SEPARATE upcoming sim pass ...
# the actual job BEHAVIORS are out of scope here"). MARKERS ONLY -- no new solid
# fixtures that could block pathfinding -- placed on already-walkable street/quay
# floor, ready for a later jobs pass to bind. Behavior (sweeping, patrolling,
# loading) is explicitly NOT wired here.
# ======================================================================
# Street-sweep routes: ordered waypoint anchors a street-cleaning behavior can walk.
for i, sx in enumerate((20, 50, 95, 120)):
    mk(11, "script_anchor", "sweep_tarwalk_%02d" % (i + 1), sx, 30)   # Tarwalk sidewalk
for i, sy in enumerate((45, 72, 90)):
    mk(11, "script_anchor", "sweep_saltgate_%02d" % (i + 1), 75, sy)  # Saltgate Rise
# Dock loading zones: legible work spots on the quay apron (the berths already have
# ship anchors; these mark the shore-side muster/loading points beside them).
mk(11, "script_anchor", "dock_load_west_anchor", 25, 30)
mk(11, "script_anchor", "dock_load_east_anchor", 100, 32)
# Guard beats gain braziers (light markers only) at the two Tarwalk posts (K21/K34
# already have their brazier lamps; the open Tarwalk beats had none).
mk(11, "light_source", "brazier_tarwalk_west", 30, 30, luminance=16)
mk(11, "light_source", "brazier_tarwalk_mid", 100, 30, luminance=16)
mk(13, "script_anchor", "exit_saltgate_road", 75, 126)
mk(11, "script_anchor", "exit_coast_road_west", 2, 30)
mk(11, "script_anchor", "exit_shambles_east", 189, 30)
mk(13, "script_anchor", "exit_abbey_road", 133, 126)
# Napkin-sketch redesign (2026-07-14): the top-of-district road's off-map
# continuation, mirrored in Band C -- Gallows Row's own east-edge marker,
# alongside Band A's exit_shambles_east.
mk(13, "script_anchor", "exit_shambles_east_upper", 188, 121)

# ======================================================================
# 6.6 Guard patrol routes (PASS 8, Phase-1 living-docks; Feature-1 prerequisite):
# ordered patrol_* waypoint anchors along three SINGLE-Z (z:+11) routes -- the Tarwalk
# thoroughfare (sidewalk row y33), the west quay/berth apron (y30), and Ropewynd (the
# continuous south kerb y65, which clears the K28 Slop-Chest frontage that interrupts the
# mid-street). MARKERS ONLY; a later patrol-behavior pass walks them in order. stepToward
# never crosses z, so every route is deliberately single-band + on connected paved street.
for i, (px, py) in enumerate(((20, 33), (45, 33), (70, 33), (96, 33), (122, 33))):
    mk(11, "script_anchor", "patrol_tarwalk_wp_%02d" % (i + 1), px, py)
for i, (px, py) in enumerate(((14, 30), (28, 30), (42, 30), (56, 30), (68, 30))):
    mk(11, "script_anchor", "patrol_quay_wp_%02d" % (i + 1), px, py)
for i, (px, py) in enumerate(((10, 65), (35, 65), (55, 65), (78, 65),
                              (100, 65), (122, 65), (145, 65))):
    mk(11, "script_anchor", "patrol_ropewynd_wp_%02d" % (i + 1), px, py)

# 6.7 Shop guard posts (PASS 8, Phase-1 living-docks; Feature-2 "one guard per shop"): one
# guard_post_<shop>_anchor on the exterior sidewalk cell just outside each retail shop's
# street door (a militia_watch spawns at each -- see DocksPopulation). Markerless walkable
# sidewalk cells, verified clear of the door threshold + the shops' own lamp/sign markers.
for (key, gx, gy) in (("k08_branns", 28, 65), ("k14_wrackhouse", 168, 33),
                      ("k15_fenners", 126, 51), ("k23_coopers", 47, 65),
                      ("k26_sailmaker", 12, 69), ("k27_hardtack", 35, 69),
                      ("k28_slopchest", 133, 57)):
    mk(11, "script_anchor", "guard_post_%s_anchor" % key, gx, gy)

# ======================================================================
# XML emission (byte-deterministic; CSV rows carry exactly W tokens,
# no trailing comma before </data>)
# ======================================================================
def csv_data(grid):
    rows = [",".join(str(v) for v in row) for row in grid]
    return "\n    " + ",\n    ".join(rows) + "\n   "


def tile_layer(layer_id, name, grid):
    return ('  <layer id="%d" name="%s" width="%d" height="%d">\n'
            '   <data encoding="csv">%s</data>\n'
            '  </layer>\n' % (layer_id, name, W, H, csv_data(grid)))


def object_layer(layer_id, objects, first_object_id):
    if not objects:
        return '  <objectgroup id="%d" name="markers"/>\n' % layer_id, first_object_id
    oid = first_object_id
    parts = ['  <objectgroup id="%d" name="markers">\n' % layer_id]
    for (cls, oname, px, py, props) in objects:
        parts.append('   <object id="%d" name="%s" type="%s" x="%d" y="%d">\n'
                     % (oid, oname, cls, px, py))
        if props:
            parts.append('    <properties>\n')
            for k in sorted(props):
                v = props[k]
                if isinstance(v, int):
                    parts.append('     <property name="%s" type="int" value="%d"/>\n'
                                 % (k, v))
                else:
                    parts.append('     <property name="%s" value="%s"/>\n' % (k, v))
            parts.append('    </properties>\n')
        parts.append('    <point/>\n   </object>\n')
        oid += 1
    parts.append('  </objectgroup>\n')
    return "".join(parts), oid


def has_content(grid):
    return any(any(row) for row in grid)


layer_id = 2
object_id = 1
body = []
for z in range(ZCOUNT):
    body.append(' <group id="%d" name="z:+%d">\n' % (layer_id, z))
    layer_id += 1
    body.append(tile_layer(layer_id, "terrain", T[z]))
    layer_id += 1
    body.append(tile_layer(layer_id, "floor", F[z]))
    layer_id += 1
    if has_content(FL[z]):
        body.append(tile_layer(layer_id, "fluids", FL[z]))
        layer_id += 1
    obj_xml, object_id = object_layer(layer_id, MK[z], object_id)
    layer_id += 1
    body.append(obj_xml)
    body.append(' </group>\n')

header = ('<?xml version="1.0" encoding="UTF-8"?>\n'
          '<map version="1.10" orientation="orthogonal" renderorder="right-down" '
          'width="%d" height="%d" tilewidth="16" tileheight="16" infinite="0" '
          'nextlayerid="%d" nextobjectid="%d">\n'
          ' <tileset firstgid="1" source="materials.tsx"/>\n'
          % (W, H, layer_id, object_id))

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                         "..", ".."))
OUT = os.path.join(REPO_ROOT, "content", "maps", "src", "docks_surface.tmx")
with open(OUT, "w", newline="\n") as f:
    f.write(header)
    f.write("".join(body))
    f.write('</map>\n')

marker_count = sum(len(m) for m in MK)
print("wrote %s (%dx%dx%d, %d markers)" % (OUT, W, H, ZCOUNT, marker_count))
