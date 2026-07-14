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
# K30 Long Quay Berth 1 hull -- "The Kestrel" (10x5 fishing lugger, smallest craft)
frect(11, 16, 20, 25, 24, OAK_FLOOR)
border(11, 16, 20, 25, 24, OAK_WALL)
for y in range(20, 25):
    for x in range(16, 26):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "ship_k30_kestrel_anchor", 20, 22)

# K31 Long Quay Berth 2 hull -- "Bregga's Promise" (14x7 mid trade cog, flanks the crane)
frect(11, 44, 18, 57, 24, OAK_FLOOR)
border(11, 44, 18, 57, 24, OAK_WALL)
for y in range(18, 25):
    for x in range(44, 58):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "ship_k31_breggas_promise_anchor", 50, 21)

# K32 Long Quay Berth 3 hull -- "The Deep Keel" (16x8, largest, deepest-water berth)
frect(11, 60, 17, 75, 24, OAK_FLOOR)
border(11, 60, 17, 75, 24, OAK_WALL)
for y in range(17, 25):
    for x in range(60, 76):
        FL[9][y][x] = 0
        FL[10][y][x] = 0
        T[10][y][x] = OAK_WALL
mk(11, "script_anchor", "ship_k32_deepkeel_anchor", 67, 20)

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
trect(11, 61, 39, 62, 40, STEEL_WALL)               # tariff scale
for y in range(35, 50):                             # ledger-room partition
    T[11][y][65] = OAK_WALL
T[11][37][65] = 0
trect(11, 58, 37, 60, 37, OAK_WALL)                 # counter
T[11][48][58] = OAK_STAIR_UP
shell(12, 56, 34, 71, 50, GRANITE_WALL, GRANITE_FLOOR)
T[12][48][58] = OAK_STAIR_DOWN
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
frect(13, 114, 34, 128, 47, THATCH_FLOOR)
mk(11, "light_source", "lamp_gull_door", 121, 33, luminance=18)
mk(11, "light_source", "lamp_gull_bar", 120, 39, luminance=16)
mk(11, "script_anchor", "business_k03_gilded_gull_anchor", 120, 41)

# K04 The Bilge (mid tavern + hammock loft) -- 12x10
shell(11, 100, 34, 111, 43, TRUDGEON_WALL, OAK_FLOOR, doors=[(105, 34), (106, 34)])
trect(11, 103, 37, 107, 37, OAK_WALL)               # bar
cells(11, [(101, 40), (109, 40)], OAK_WALL)         # tables
T[11][41][109] = OAK_STAIR_UP
shell(12, 100, 34, 111, 43, TRUDGEON_WALL, OAK_FLOOR)
cells(12, [(102, 37), (105, 37), (108, 37)], OAK_WALL)
T[12][41][109] = OAK_STAIR_DOWN
frect(13, 100, 34, 111, 43, THATCH_FLOOR)
mk(11, "light_source", "lamp_bilge_door", 105, 33, luminance=14)
mk(11, "script_anchor", "business_k04_bilge_anchor", 106, 38)

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
trect(11, 84, 42, 85, 43, OAK_WALL)                 # crates
trect(11, 93, 38, 94, 39, OAK_WALL)
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
for y in range(67, 80):                             # chapel partition
    T[11][y][90] = OAK_WALL
T[11][71][90] = 0
trect(11, 84, 69, 87, 69, OAK_WALL)                 # alms-hall tables
trect(11, 84, 72, 87, 72, OAK_WALL)
for x in range(83, 90):                             # dormitory partition
    T[11][75][x] = OAK_WALL
T[11][75][86] = 0
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
shell(11, 100, 80, 112, 88, GRANITE_WALL, GRANITE_FLOOR, doors=[(106, 80), (107, 80)])
cells(11, [(102, 84), (103, 84)], STEEL_WALL)       # holding-cell cage
for y in range(81, 88):                             # armory partition
    T[11][y][109] = OAK_WALL
T[11][85][109] = 0
trect(11, 105, 86, 107, 86, OAK_WALL)               # watch-room table
frect(12, 100, 80, 112, 88, BRICK_FLOOR)            # roof
mk(11, "script_anchor", "business_k34_guardhouse_anchor", 106, 85)
mk(11, "light_source", "lamp_guardhouse_door", 106, 79, luminance=18)

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
T[12][106][14] = 0
T[12][100][12] = OAK_STAIR_UP
shell(13, 8, 97, 31, 115, REMAN_WALL, REMAN_FLOOR)
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
    T[12][C1_STAIRS_N[i][1]][C1_STAIRS_N[i][0]] = OAK_STAIR_UP
    shell(13, x0, 97, x1, 103, REMAN_WALL, REMAN_FLOOR)
    T[13][C1_STAIRS_N[i][1]][C1_STAIRS_N[i][0]] = OAK_STAIR_DOWN
    frect(14, x0, 97, x1, 103, REMAN_FLOOR)
    unit_anchor(12, "cmp1_condo_%02d_anchor" % (i + 1), x0, 97, x1, 103)
    unit_anchor(13, "cmp1_condo_%02d_anchor" % (i + 7), x0, 97, x1, 103)
for i, (x0, x1) in enumerate(C1_S):
    d = ((x0 + 5, 110),)
    shell(12, x0, 110, x1, 115, REMAN_WALL, REMAN_FLOOR, doors=d)
    T[12][C1_STAIRS_S[i][1]][C1_STAIRS_S[i][0]] = OAK_STAIR_UP
    shell(13, x0, 110, x1, 115, REMAN_WALL, REMAN_FLOOR)
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
shell(11, 184, 76, 190, 93, BRICK_WALL, OAK_FLOOR, doors=[(184, 84)])  # c05 (2-story)
T[11][80][186] = OAK_STAIR_UP
shell(12, 184, 76, 190, 93, BRICK_WALL, OAK_FLOOR)  # c06 upper
T[12][80][186] = OAK_STAIR_DOWN
unit_anchor(11, "cmp4_condo_05_anchor", 184, 76, 190, 93)
unit_anchor(12, "cmp4_condo_06_anchor", 184, 76, 190, 93)
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
    mk(z, "script_anchor", "hovel_%02d_anchor" % n, cx, cy)

# Band B east field: goat pen
border(12, 146, 109, 158, 114, TRUDGEON_WALL)
T[12][109][151] = 0                                 # gate
mk(12, "script_anchor", "pen_goats_anchor", 152, 111)
# Band C well plaza
trect(13, 101, 120, 102, 121, GRANITE_WALL)
mk(13, "script_anchor", "well_gallows_row_anchor", 101, 122)

# ======================================================================
# 6. Patrol, muster, exits (blueprint section 6; script_anchors only —
# the importer supports only light_source/script_anchor marker classes)
# ======================================================================
mk(13, "script_anchor", "patrol_post_rise_top", 75, 118)
mk(11, "script_anchor", "patrol_post_rise_foot", 75, 34)
mk(11, "script_anchor", "patrol_post_tarwalk_west", 30, 30)
mk(11, "script_anchor", "patrol_post_tarwalk_mid", 100, 30)
mk(13, "script_anchor", "exit_saltgate_road", 75, 126)
mk(11, "script_anchor", "exit_coast_road_west", 2, 30)
mk(11, "script_anchor", "exit_shambles_east", 189, 30)
mk(13, "script_anchor", "exit_abbey_road", 133, 126)

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
