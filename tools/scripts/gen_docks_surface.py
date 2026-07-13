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

frect(11, 0, 26, 79, 33, GRANITE_FLOOR)     # Tarwalk A: quay apron
frect(11, 80, 26, 129, 33, BRICK_FLOOR)     # Tarwalk A: east half
frect(11, 130, 30, 163, 35, BRICK_FLOOR)    # Tarwalk B (bends inland)
for y in range(26, 34):                     # Tarwalk C: broken paving checker
    for x in range(164, 192):
        F[11][y][x] = BRICK_FLOOR if (x + y) % 2 == 0 else DIRT_FLOOR
frect(11, 4, 60, 147, 65, BRICK_FLOOR)      # Ropewynd, paved reach
frect(11, 148, 60, 177, 65, DIRT_FLOOR)     # Ropewynd, paving gives out
frect(11, 32, 34, 35, 59, BRICK_FLOOR)      # Herring Lane
frect(12, 80, 97, 188, 100, BRICK_FLOOR)    # Terrace Walk (placeholder)
frect(11, 72, 26, 79, 95, BRICK_FLOOR)      # Saltgate Rise, Band A leg
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

# K16 Drowned-Name Wall (niche shrine against the seawall)
cells(11, [(94, 26), (95, 26)], GRANITE_WALL)
mk(11, "script_anchor", "shrine_drowned_name_wall_anchor", 94, 27)
mk(11, "light_source", "lamp_shrine_candles", 94, 27, luminance=8)

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
# K01 The Weighhouse (grand, 2-story granite; tile roof; signal mast)
shell(11, 58, 34, 71, 51, GRANITE_WALL, BRICK_FLOOR,
      doors=[(63, 34), (64, 34), (71, 42), (71, 43)])
trect(11, 63, 40, 64, 41, STEEL_WALL)               # tariff scale
for y in range(35, 51):                             # ledger-room partition
    T[11][y][66] = OAK_WALL
T[11][38][66] = 0
trect(11, 60, 38, 62, 38, OAK_WALL)                 # counter
T[11][49][60] = OAK_STAIR_UP
shell(12, 58, 34, 71, 51, GRANITE_WALL, GRANITE_FLOOR)
T[12][49][60] = OAK_STAIR_DOWN
frect(13, 58, 34, 71, 51, BRICK_FLOOR)              # tile roof
mk(13, "light_source", "lamp_weighhouse_mast", 64, 35, luminance=26)
mk(11, "script_anchor", "business_k01_weighhouse_anchor", 64, 42)
mk(11, "script_anchor", "clue_c2_weighhouse_ledger", 68, 37)

# K02 Impound Yard (steel spike fence, watchman shed, crates, dog)
border(11, 58, 53, 71, 59, STEEL_WALL)
for g in ((64, 53), (65, 53)):
    T[11][g[1]][g[0]] = 0
shell(11, 58, 55, 61, 58, OAK_WALL, DIRT_FLOOR, doors=[(61, 56)])
cells(11, [(66, 55), (68, 57), (69, 54)], OAK_WALL)
mk(11, "script_anchor", "business_k02_impound_anchor", 65, 56)
mk(11, "script_anchor", "impound_dog_anchor", 62, 57)

# K03 The Gilded Gull (large 2-story tavern)
shell(11, 114, 34, 129, 49, GRANITE_WALL, OAK_FLOOR, doors=[(121, 34), (122, 34)])
trect(11, 117, 40, 124, 40, GRANITE_WALL)           # bar
for y in range(35, 49):                             # snug partition
    T[11][y][126] = OAK_WALL
T[11][37][126] = 0
T[11][44][126] = 0
cells(11, [(116, 37), (119, 37), (116, 43), (119, 43), (122, 46), (117, 46)], OAK_WALL)
T[11][47][128] = OAK_STAIR_UP
shell(12, 114, 34, 129, 49, OAK_WALL, OAK_FLOOR)
for y in range(35, 49):                             # guest-room cross partitions
    T[12][y][121] = OAK_WALL
for x in range(115, 129):
    T[12][41][x] = OAK_WALL
T[12][38][121] = 0
T[12][41][118] = 0
T[12][41][124] = 0
T[12][47][128] = OAK_STAIR_DOWN
frect(13, 114, 34, 129, 49, THATCH_FLOOR)
mk(11, "light_source", "lamp_gull_door", 121, 33, luminance=18)
mk(11, "light_source", "lamp_gull_bar", 120, 40, luminance=16)
mk(11, "script_anchor", "business_k03_gilded_gull_anchor", 120, 42)

# K04 The Bilge (mid tavern + hammock loft)
shell(11, 100, 34, 113, 45, TRUDGEON_WALL, OAK_FLOOR, doors=[(105, 34), (106, 34)])
trect(11, 103, 38, 108, 38, OAK_WALL)               # bar
cells(11, [(101, 41), (110, 41)], OAK_WALL)         # tables
T[11][43][111] = OAK_STAIR_UP
shell(12, 100, 34, 113, 45, TRUDGEON_WALL, OAK_FLOOR)
cells(12, [(102, 38), (105, 38), (108, 38), (102, 42), (105, 42), (108, 42)], OAK_WALL)
T[12][43][111] = OAK_STAIR_DOWN
frect(13, 100, 34, 113, 45, THATCH_FLOOR)
mk(11, "light_source", "lamp_bilge_door", 105, 33, luminance=14)
mk(11, "script_anchor", "business_k04_bilge_anchor", 106, 39)

# K05 The Lantern Room (crossroads tavern, neutral ground)
shell(11, 58, 66, 71, 79, GRANITE_WALL, OAK_FLOOR,
      doors=[(63, 66), (64, 66), (71, 71), (71, 72)])
cells(11, [(59, 73), (60, 73)], GRANITE_WALL)       # hearth
trect(11, 62, 74, 65, 74, OAK_WALL)                 # counter
cells(11, [(60, 69), (63, 70), (66, 69), (67, 74)], OAK_WALL)
T[11][77][69] = OAK_STAIR_UP
shell(12, 58, 66, 71, 79, OAK_WALL, OAK_FLOOR)
for x in range(59, 71):                             # landlady's rooms
    T[12][72][x] = OAK_WALL
T[12][72][64] = 0
T[12][77][69] = OAK_STAIR_DOWN
frect(13, 58, 66, 71, 79, THATCH_FLOOR)
mk(11, "light_source", "lamp_lantern_room", 63, 65, luminance=22)
mk(11, "script_anchor", "business_k05_lantern_room_anchor", 64, 72)

# K07 The Ropewalk (the district's longest sightline; fire tier High)
shell(11, 4, 82, 69, 91, TRUDGEON_WALL, DIRT_FLOOR,
      doors=[(4, 86), (4, 87), (69, 86), (69, 87)])
cells(11, [(12, 86), (20, 87), (28, 86), (36, 87), (44, 86), (52, 87), (60, 86)],
      OAK_WALL)                                     # rope-laying posts
frect(12, 4, 82, 69, 91, THATCH_FLOOR)
shell(11, 60, 92, 68, 95, THATCH_WALL, DIRT_FLOOR, skip_sides=("n",))  # hemp lean-to
frect(12, 60, 92, 68, 95, THATCH_FLOOR)             # DEV: lean-to roof (unspecified)
mk(11, "script_anchor", "business_k07_ropewalk_anchor", 36, 86)

# K08 Brann's Chandlery (+ the gray-ledger cellar)
shell(11, 24, 66, 37, 79, OAK_WALL, OAK_FLOOR, doors=[(29, 66), (30, 66)])
for x in range(25, 37):                             # stockroom partition
    T[11][73][x] = OAK_WALL
T[11][73][32] = 0
# DEV: blueprint's oil barrel at (35,77) dropped — that cell is the cellar stair.
cells(11, [(25, 75), (25, 77), (27, 76), (35, 75)], OAK_WALL)
shell(10, 30, 72, 36, 78, GRANITE_WALL, GRANITE_FLOOR)   # cellar carve
cells(10, [(31, 73), (31, 77), (33, 74), (33, 76)], OAK_WALL)
T[10][77][35] = OAK_STAIR_UP
T[11][77][35] = OAK_STAIR_DOWN
frect(12, 24, 66, 37, 79, THATCH_FLOOR)
mk(11, "script_anchor", "business_k08_branns_anchor", 30, 70)
mk(10, "script_anchor", "clue_brann_grayledger_anchor", 33, 75)
mk(11, "light_source", "lamp_branns_door", 29, 65, luminance=14)

# K09 Pitchfield (tar yard, west fire-sort; getilia-soaked firebreak fence)
border(11, 2, 36, 29, 58, GETILIA_WALL)
for gx in range(14, 18):
    T[11][36][gx] = 0                               # gate
shell(11, 6, 44, 15, 52, TRUDGEON_WALL, DIRT_FLOOR, skip_sides=("n",))  # cauldron shed
cells(11, [(9, 48), (12, 48)], GRANITE_WALL)        # cauldrons
frect(12, 6, 44, 15, 52, BRICK_FLOOR)               # tile roof at the tar yard
for bx in (20, 22, 24, 26, 28):                     # tar barrel grid
    for by in (40, 43, 46, 49, 52, 55):
        if (bx, by) != (24, 46):                    # aisle
            T[11][by][bx] = OAK_WALL
mk(11, "light_source", "lamp_pitchfield_cauldron", 10, 48, luminance=16)
mk(11, "script_anchor", "business_k09_pitchfield_anchor", 15, 46)

# K10 Dawnstalls (open-air fish market)
for sy in (38, 42, 46):
    trect(11, 40, sy, 43, sy, TRUDGEON_WALL)
    trect(11, 48, sy, 51, sy, TRUDGEON_WALL)
T[11][40][46] = GRANITE_WALL                        # auction block
mk(11, "script_anchor", "business_k10_dawnstalls_anchor", 46, 41)
mk(11, "script_anchor", "muster_dawnstalls_anchor", 46, 37)

# K11 Salt Row (gutting sheds + smokehouses)
for (sx0, sx1) in ((38, 42), (44, 48), (50, 54)):
    shell(11, sx0, 50, sx1, 53, TRUDGEON_WALL, DIRT_FLOOR, skip_sides=("n",))
for (mx0, mx1, d, hx) in ((40, 43, (41, 55), 42), (48, 51, (49, 55), 50)):
    shell(11, mx0, 55, mx1, 59, GRANITE_WALL, DIRT_FLOOR, doors=[d])
    T[11][58][hx] = GRANITE_WALL                    # contained hearth
mk(11, "script_anchor", "business_k11_saltrow_anchor", 46, 52)

# K12 The King's Bond (bonded warehouse: brick, one double door, no windows)
shell(11, 82, 34, 97, 49, BRICK_WALL, BRICK_FLOOR, doors=[(88, 34), (89, 34)])
trect(11, 84, 44, 85, 45, OAK_WALL)                 # crates
trect(11, 92, 40, 93, 41, OAK_WALL)
frect(12, 82, 34, 97, 49, BRICK_FLOOR)
mk(11, "script_anchor", "business_k12_kingsbond_anchor", 89, 41)
mk(11, "script_anchor", "watch_bond_post_anchor", 89, 33)

# K13 The Drowned Hold (condemned hulk; Gullet-only entry; no lamps)
shell(11, 178, 34, 190, 56, TRUDGEON_WALL, doors=[(178, 50)])
for rot in ((183, 34), (188, 56), (178, 41)):       # rot gaps in the shell
    T[11][rot[1]][rot[0]] = 0
for y in range(35, 56):                             # oak floor, sagging NE quadrant
    for x in range(179, 190):
        if 184 <= x <= 189 and 36 <= y <= 43:
            F[11][y][x] = 0                         # truly OPEN: drops to the undercellar
        else:
            F[11][y][x] = OAK_FLOOR
for d in ((178, 50), (183, 34), (178, 41), (188, 56)):
    F[11][d[1]][d[0]] = OAK_FLOOR                   # thresholds under the gaps
cells(11, [(180, 45), (182, 52), (186, 50), (188, 47)], OAK_WALL)  # debris
T[11][54][188] = OAK_STAIR_UP
shell(12, 178, 46, 190, 56, TRUDGEON_WALL, OAK_FLOOR)  # 2nd story, south half only
F[12][49][181] = 0                                  # floor holes
F[12][53][185] = 0
T[12][54][188] = OAK_STAIR_DOWN
# (no roof: z13 stays empty — open to sky)
for y in range(36, 45):                             # undercellar stub z10
    for x in range(180, 189):
        T[10][y][x] = 0
        F[10][y][x] = GRANITE_FLOOR
        if x >= 186:
            FL[10][y][x] = WATER2                   # permanent shin-wet east rooms
mk(11, "script_anchor", "clue_c3_drowned_hold", 184, 48)
mk(10, "script_anchor", "dungeon_seam_drowned_hold", 184, 40)

# K14 Wrackhouse (salvage house)
shell(11, 164, 34, 175, 45, TRUDGEON_WALL, DIRT_FLOOR, doors=[(168, 34), (169, 34)])
trect(11, 165, 40, 167, 40, OAK_WALL)               # salvage racks
trect(11, 172, 40, 174, 40, OAK_WALL)
T[11][36][174] = STEEL_WALL                         # the diving bell
for x in range(165, 175):                           # stockroom partition
    T[11][41][x] = OAK_WALL
T[11][41][169] = 0
frect(12, 164, 34, 175, 45, TRUDGEON_FLOOR)
mk(11, "script_anchor", "business_k14_wrackhouse_anchor", 169, 38)
mk(11, "script_anchor", "clue_wrackhouse_salvage_anchor", 170, 43)

# K15 Fenner's Pawn (deliberately cramped; caged counter)
shell(11, 122, 52, 129, 59, BRICK_WALL, BRICK_FLOOR, doors=[(125, 52)])
for x in range(123, 129):                           # cage partition with slot
    T[11][55][x] = STEEL_WALL
T[11][55][126] = 0
T[11][58][128] = OAK_WALL                           # strongbox
frect(12, 122, 52, 129, 59, BRICK_FLOOR)
mk(11, "script_anchor", "business_k15_fenners_anchor", 125, 57)
mk(11, "script_anchor", "fenner_sign_anchor", 125, 51)
mk(11, "light_source", "lamp_fenners_door", 125, 51, luminance=10)

# K17 Mission of the Flame (+ garden)
shell(11, 82, 66, 99, 81, GRANITE_WALL, BRICK_FLOOR,
      doors=[(88, 66), (89, 66), (82, 74), (82, 75)])
for y in range(67, 81):                             # chapel partition
    T[11][y][91] = OAK_WALL
T[11][72][91] = 0
trect(11, 84, 70, 87, 70, OAK_WALL)                 # alms-hall tables
trect(11, 84, 73, 87, 73, OAK_WALL)
for x in range(83, 91):                             # dormitory partition
    T[11][76][x] = OAK_WALL
T[11][76][86] = 0
for x in range(92, 99):                             # back room (the body)
    T[11][76][x] = OAK_WALL
T[11][76][95] = 0
frect(12, 82, 66, 99, 81, THATCH_FLOOR)
mk(11, "script_anchor", "business_k17_mission_anchor", 88, 72)
mk(11, "script_anchor", "mission_bunks_anchor", 86, 79)
mk(11, "script_anchor", "clue_c1_mission_backroom", 95, 79)
mk(11, "light_source", "lamp_mission_night", 88, 65, luminance=22)
mk(11, "script_anchor", "mission_garden_anchor", 90, 88)

# K18 Squall's Bathhouse (real pooled water)
shell(11, 102, 66, 113, 79, GRANITE_WALL, GRANITE_FLOOR, doors=[(107, 66), (108, 66)])
cells(11, [(104, 76), (106, 76)], STEEL_WALL)       # boilers
T[11][76][103] = GRANITE_WALL                       # hearth
for y in range(69, 74):
    for x in range(105, 111):
        FL[11][y][x] = WATER2                       # the pools
frect(12, 102, 66, 113, 79, BRICK_FLOOR)
mk(11, "script_anchor", "business_k18_bathhouse_anchor", 107, 74)

# K19 The Rows (22x8 flophouse)
shell(11, 100, 52, 121, 59, TRUDGEON_WALL, DIRT_FLOOR, doors=[(104, 52), (105, 52)])
for hx in (102, 105, 108, 111, 114, 117, 120):      # hammock posts
    T[11][54][hx] = OAK_WALL
    T[11][57][hx] = OAK_WALL
T[11][53][101] = OAK_WALL                           # landlord counter
frect(12, 100, 52, 121, 59, THATCH_FLOOR)
mk(11, "script_anchor", "business_k19_rows_anchor", 110, 55)

# K21 Saltgate Watch-Post (Band C: ground z13, roof z14)
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

# K22 Netmenders' Arcade (leaky colonnade fronting Dawnstalls)
cells(11, [(38, 35), (42, 35), (46, 35), (50, 35), (53, 35)], OAK_WALL)
frect(12, 38, 34, 53, 35, THATCH_FLOOR)
mk(11, "script_anchor", "business_k22_netmenders_anchor", 45, 34)

# K23 Cooper & Blockmaker (sawdust register; open double doors)
shell(11, 40, 66, 55, 77, TRUDGEON_WALL, DIRT_FLOOR,
      doors=[(46, 66), (47, 66), (48, 66), (49, 66)])
trect(11, 41, 74, 42, 75, OAK_WALL)                 # barrel stacks
trect(11, 52, 68, 53, 69, OAK_WALL)
trect(11, 44, 72, 47, 72, OAK_WALL)                 # workbench
frect(12, 40, 66, 55, 77, THATCH_FLOOR)
mk(11, "script_anchor", "business_k23_coopers_anchor", 47, 71)

# K24 The Eel-Pots (lantern-lit night stalls on the Tarwalk)
for i, sx in enumerate((84, 96, 104, 112)):
    trect(11, sx, 30, sx + 1, 31, TRUDGEON_WALL)
    mk(11, "light_source", "lamp_eelpot_%02d" % (i + 1), sx + 1, 32, luminance=18)
mk(11, "script_anchor", "business_k24_eelpots_anchor", 97, 31)

# K25 Kennel Row. DEV: shrunk to y48-56 (blueprint's y48-59 crossed the Gullet
# "Bottom" link rows y57-59); shed y52-56 open east; the y56 cage pair moved to
# y55 and kennel_dog_anchor_03 (172,57)->(172,54).
border(11, 164, 48, 175, 56, TRUDGEON_WALL)
for g in ((168, 48), (169, 48)):
    T[11][g[1]][g[0]] = 0
shell(11, 164, 52, 169, 56, TRUDGEON_WALL, DIRT_FLOOR, skip_sides=("e",))
cells(11, [(171, 50), (173, 50), (171, 53), (173, 53), (171, 55), (173, 55)],
      STEEL_WALL)
mk(11, "script_anchor", "business_k25_kennelrow_anchor", 168, 55)
mk(11, "script_anchor", "kennel_dog_anchor_01", 166, 55)
mk(11, "script_anchor", "kennel_dog_anchor_02", 172, 51)
mk(11, "script_anchor", "kennel_dog_anchor_03", 172, 54)

# Unkeyed texture business: Sailmaker's Loft (2-story)
shell(11, 6, 66, 21, 77, TRUDGEON_WALL, OAK_FLOOR, doors=[(12, 66), (13, 66)])
T[11][75][19] = OAK_STAIR_UP
shell(12, 6, 66, 21, 77, TRUDGEON_WALL, OAK_FLOOR)
T[12][75][19] = OAK_STAIR_DOWN
frect(13, 6, 66, 21, 77, THATCH_FLOOR)
mk(11, "script_anchor", "business_sailmaker_anchor", 13, 71)

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
