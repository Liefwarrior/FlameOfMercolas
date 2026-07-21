#!/usr/bin/env python3
"""gen_actor_sprites.py - emit content/art/sprites/sprites.png + sprite-index.json,
the UNIFIED tag-queryable sprite index: actor sprites (authored here) plus the
face-part entries merged in from gen_face_parts.py's committed output pair
(content/art/faces/face-parts-index.json + face-parts.png).

Actor sprites for the Granadad register (GRANADAD ART UNIFIED SPEC v1,
sections 2-4; DECISIONS.md Art register row, Eli 2026-07-13 FOURTH revision:
flat per-z-slice rendering retained, actors as REAL SPRITES from a
tag-queryable SpriteIndex, faces composed from tile/sprite parts). All art
ORIGINAL - zero copied pixels; every color is a MERCOLAS-24 palette entry;
background transparent.

Unified merge (integration ruling: ONE SpriteIndex serves actor sprites AND
face parts via tags): actor cells fill the top rows, the face-part sheet is
appended below with every face cell row offset by the actor row count. Merge
invariants validated hard: no id collision, actor/face tag vocabularies
fully disjoint (so no actorQueries pool nor face_* pool can cross-match),
face entries otherwise byte-preserved. Rerun order: gen_face_parts.py first
(if faces changed), then this script.

Contract (spec section 3.1): 16x16 canvas, single south-facing sprite,
1px N0 outline on the silhouette (drawn by expansion: every transparent
pixel 4-adjacent to a filled one becomes N0), humanoid silhouette <= 12px
wide x <= 15px tall with feet on rows 14-15, >= 3 distinct non-outline
shades, and the tile pack's no-flat-fill rule (no monochrome axis-aligned
rectangle >= 6x5 or 5x6).

Deterministic: byte-identical rerun. Every sprite is fully hand-authored as
a character grid (no `random` module, no RNG at all, no timestamps). PNG:
optimize=False, compress_level=9, no ancillary chunks. JSON: indent-2
style, LF newlines, UTF-8 no BOM, sprites serialized one per line in
ascending ASCII id order (the order the SpriteIndex validator enforces).

Sheet packing (spec section 2.1): entries in ascending ASCII id order with
a shelf packer - shelf height = entry cell-height, new shelf when the row
cannot fit the entry. All actor sprites are 1x1 cells, so this degenerates
to row-major fill, 16 columns.
"""

import json
import os
import re
from PIL import Image

# --------------------------------------------------------------------------
# paths
# --------------------------------------------------------------------------

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT_DIR = os.path.join(REPO, "content", "art", "sprites")
PNG_PATH = os.path.join(OUT_DIR, "sprites.png")
JSON_PATH = os.path.join(OUT_DIR, "sprite-index.json")

# gen_face_parts.py's committed output pair - the merge INPUT (canonical for
# the face tests' golden/raster fixtures; merged into the unified index here).
FACES_DIR = os.path.join(REPO, "content", "art", "faces")
FACE_JSON_PATH = os.path.join(FACES_DIR, "face-parts-index.json")
FACE_PNG_PATH = os.path.join(FACES_DIR, "face-parts.png")

T = 16          # cell edge, px
COLS = 16       # sheet grid width, cells

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

# Skin ramps [shadow, base, light] (unified spec section 4.5).
SKIN = {
    "pale": ("E4", "B1", "B2"),
    "tan":  ("E3", "E5", "B1"),
    "dark": ("E1", "E3", "E4"),
}

# --------------------------------------------------------------------------
# grid helpers. A sprite is authored as 16 strings of 16 chars; a legend maps
# each char to a palette key ('.' = transparent). Role chars shared by the
# humanoid template:
#   h hair   s skin base   t skin shadow   e eye (N0)
#   A garment base   B garment shadow   C garment light
#   b belt   g leg   f foot
# Design-specific chars are declared per legend below.
# --------------------------------------------------------------------------


def parse(rows, legend):
    """rows: 16 strings of 16 chars -> 16x16 grid of palette keys / None."""
    assert len(rows) == T, f"need {T} rows, got {len(rows)}"
    g = []
    for y, row in enumerate(rows):
        assert len(row) == T, f"row {y} is {len(row)} chars, need {T}"
        line = []
        for x, ch in enumerate(row):
            if ch == ".":
                line.append(None)
            else:
                key = legend[ch]
                assert key in PAL, f"({x},{y}): '{ch}' -> unknown palette key {key}"
                line.append(key)
        g.append(line)
    return g


def outline(g):
    """1px N0 silhouette outline by EXPANSION: every empty pixel 4-adjacent
    to a filled one becomes N0 (unified spec section 3.1). Keeps the authored
    colors intact instead of eating the outer ring."""
    out = [row[:] for row in g]
    for y in range(T):
        for x in range(T):
            if g[y][x] is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < T and 0 <= ny < T and g[ny][nx] is not None:
                    out[y][x] = "N0"
                    break
    return out


def patch(g, pts):
    """Apply [(x, y, paletteKeyOrNone), ...] onto a parsed grid."""
    for x, y, key in pts:
        assert key is None or key in PAL, f"patch ({x},{y}): bad key {key}"
        g[y][x] = key
    return g


def hum_legend(skin, hair, garment, extra=None):
    """Humanoid legend: skin ramp name, hair key, garment (base, shadow,
    light) plus per-design extras."""
    sh, base, light = SKIN[skin]
    leg = {
        "h": hair, "s": base, "t": sh, "l": light, "e": "N0",
        "A": garment[0], "B": garment[1], "C": garment[2],
    }
    if extra:
        leg.update(extra)
    return leg


# --------------------------------------------------------------------------
# humanoid template (pre-outline content: cols 4-11, rows 2-14; the
# expansion outline then spans cols 3-12 / rows 1-15 -> 10 wide, 14 tall,
# feet land on rows 14(+15 outline) as pinned).
# --------------------------------------------------------------------------

HUMAN = [
    "................",
    "................",
    ".....hhhhhh.....",
    ".....hlllsh.....",
    ".....sesses.....",
    ".....ssttss.....",
    "......tttt......",
    "....AAAAAAAA....",
    "....BABAABAB....",
    "....BAABBAAB....",
    "....BABAABAB....",
    "....sbbbbbbs....",
    ".....gg..gg.....",
    ".....gg..gg.....",
    ".....ff..ff.....",
    "................",
]

# Robed template: one garment column head-to-hem, folded hands, no legs.
ROBE = [
    "................",
    "................",
    ".....hhhhhh.....",
    ".....hlllsh.....",
    ".....sesses.....",
    ".....ssttss.....",
    "......tttt......",
    "....AAAAAAAA....",
    "....BAWAAWAB....",
    "....BAWAAWAB....",
    "....BAWssWAB....",
    "....BAWAAWAB....",
    "....BAWAAWAB....",
    "....BAWAAWAB....",
    "....AAAAAAAA....",
    "................",
]

# Hunched vagrant template: head sits lower, shoulders uneven, ragged hem,
# bare feet.
VAGRANT = [
    "................",
    "................",
    "................",
    ".....hhhhhh.....",
    ".....hlllsh.....",
    ".....sesses.....",
    ".....ssttss.....",
    "....A.tttt.A....",
    "....AAAAAAAA....",
    "....BAPAAPAB....",
    "....BAAPPAAB....",
    "....APAAAAPB....",
    "....B.AAAA.B....",
    ".....A.AA.A.....",
    ".....ss..ss.....",
    "................",
]


def human(skin, hair, garment, extra=None, patches=(), rows=HUMAN):
    g = parse(rows, hum_legend(skin, hair, garment, extra))
    return patch(g, patches)


# --------------------------------------------------------------------------
# the 25 sprites (unified spec section 3.2 inventory). Ascending ASCII id
# order — the packer and the index serializer both walk this order.
# --------------------------------------------------------------------------

def sp_cat_0():
    # Wharf cat: side-stance, pricked ears, Y1 amber eye, B1 moon-grey chest,
    # tail curled up right (beast food channel pass). NOT tagged vermin — the
    # feral (gull) actorQueries pool is a superset match and must not gain it.
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "...A..A.........",
        "...AAAA.....A...",
        "...AeAA.....A...",
        "...ACCA....AB...",
        "....AABAAAAB....",
        "....ABAABAA.....",
        "....AABABAA.....",
        "....AAAAAAA.....",
        "....AB..AB......",
        "....AB..AB......",
        "................",
    ]
    return parse(rows, {"A": "G4", "B": "G3", "C": "B1", "e": "Y1"})


def sp_disciple_0():
    # Street acolyte: ragged E2 robe, cowl up, shadowed face, R2 armband,
    # ragged hem with bare feet showing.
    rows = [
        "................",
        "................",
        ".....hhhhhh.....",
        ".....hhhhhh.....",
        ".....htetth.....",
        ".....htttth.....",
        "......tttt......",
        "....AAAAAAAA....",
        "....RAABBAAB....",
        "....RABAABAB....",
        "....BAAssAAB....",
        "....BABAABAB....",
        "....BAABBAAB....",
        "....A.BAAB.A....",
        ".....tt..tt.....",
        "................",
    ]
    return parse(rows, hum_legend("tan", "E1", ("E2", "E1", "E3"),
                                  {"R": "R2"}))


def sp_disciple_1():
    # Shaved head, R2 flame brand on the brow, same ragged robe, rope belt.
    rows = [
        "................",
        "................",
        ".....ssssss.....",
        ".....slRlss.....",
        ".....sesses.....",
        ".....ssttss.....",
        "......tttt......",
        "....AAAAAAAA....",
        "....BABAABAB....",
        "....BAABBAAB....",
        "....BAAssAAB....",
        "....WWWWWWWW....",
        "....BAABBAAB....",
        "....A.BAAB.A....",
        ".....tt..tt.....",
        "................",
    ]
    return parse(rows, hum_legend("dark", "E1", ("E2", "E1", "E3"),
                                  {"R": "R2", "W": "E4"}))


def sp_dog_0():
    # Dock dog, brown: side-stance quadruped, head raised left, pricked
    # ears, E5 chest bib, tail curled up right.
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "...A.A..........",
        "...AAAA.........",
        "..nAeAA......A..",
        "...CCAAA....AA..",
        "....WAAAAAAAA...",
        "....WWABBAAA....",
        "....AWAAAABA....",
        "....AAAAAAAA....",
        "....AB..AB.B....",
        "....AB..AB.B....",
        "................",
    ]
    return parse(rows, {"A": "E3", "B": "E2", "C": "E4", "W": "E5",
                        "e": "N0", "n": "N0"})


def sp_dog_1():
    # Grey cur: floppy ears, lean ribs, tail low.
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "...AA...........",
        "..nAAAA.........",
        "..AAeAAA........",
        "...CCAAAAAAAA...",
        "....AABABABAA...",
        "....ABABABABA...",
        "....AAAAAAAA.A..",
        "....AB..AB..A...",
        "....AB..AB..B...",
        "................",
    ]
    return parse(rows, {"A": "G3", "B": "G2", "C": "G4",
                        "e": "N0", "n": "N0"})


def sp_feral_dog_0():
    # Feral dog: skulking low, head level with the spine, R3 eye, N0 rib
    # shadows through E2 hide, tail straight out.
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "...A.A..........",
        "..nAAAA.....A...",
        "...AeAAAAAAAA.A.",
        "...CCArArArAAA..",
        "....AArArArA....",
        "....AB...AB.....",
        "....AB...AB.....",
        "................",
    ]
    return parse(rows, {"A": "E2", "B": "E1", "C": "E3", "r": "N0",
                        "e": "R3", "n": "N0"})


def sp_goat_0():
    # Grey goat: bone horns, droop ears, beard, shaggy dithered flank.
    rows = [
        "................",
        "................",
        "....H......H....",
        "....HH....HH....",
        ".....HAAAAH.....",
        "....BAeAAeAB....",
        "....B.ACCA.B....",
        "......ACCA......",
        ".....AAWWAA.....",
        "....AABAABAA....",
        "....ABAABAABA...",
        "....AABAABAA.A..",
        "....ABAAAABA....",
        ".....AB..BA.....",
        ".....BB..BB.....",
        "................",
    ]
    return parse(rows, {"A": "G3", "B": "G2", "C": "G4", "H": "B1",
                        "W": "G4", "e": "N0"})


def sp_goat_1():
    # Pale nanny goat: B1 coat, B2 lights, G4 shading.
    rows = [
        "................",
        "................",
        "....H......H....",
        "....HH....HH....",
        ".....HAAAAH.....",
        "....BAeAAeAB....",
        "....B.ACCA.B....",
        "......ACCA......",
        ".....AAWWAA.....",
        "....AABAABAA....",
        "....ABAACAABA...",
        "....AACAABAA.A..",
        "....ABAAAABA....",
        ".....AB..BA.....",
        ".....BB..BB.....",
        "................",
    ]
    return parse(rows, {"A": "B1", "B": "G4", "C": "B2", "H": "E5",
                        "W": "B2", "e": "N0"})


GUARD_MAIL = ("G2", "G1", "G3")


def sp_guard_0():
    # Watchman: open helm, R2 tabard, leather belt w/ Y1 buckle, spear right.
    rows = [
        "................",
        "................",
        ".....CCCCCC.p...",
        ".....CssssC.p...",
        ".....sesses.i...",
        ".....ssttss.i...",
        "......tttt..i...",
        "....CAARRAACi...",
        "....BAARRAABi...",
        "....BAARRAABi...",
        "....BAARRAABi...",
        "....swwYwwwsi...",
        ".....AA..AA.i...",
        ".....AA..AA.i...",
        ".....ff..ff.....",
        "................",
    ]
    return parse(rows, hum_legend("tan", "G2", GUARD_MAIL,
                                  {"R": "R2", "Y": "Y1", "w": "E1",
                                   "i": "E3", "p": "C2", "f": "E1"}))


def sp_guard_1():
    # Closed helm (N0 eye slit), spear left, pale hands.
    rows = [
        "................",
        "................",
        "...p.CCCCCC.....",
        "...p.CCCCCC.....",
        "...i.CeeeeC.....",
        "...i.CCCCCC.....",
        "...i..CCCC......",
        "...iCAARRAAC....",
        "...iBAARRAAB....",
        "...iBAARRAAB....",
        "...iBAARRAAB....",
        "...iswwYwwws....",
        "...i.AA..AA.....",
        "...i.AA..AA.....",
        ".....ff..ff.....",
        "................",
    ]
    return parse(rows, hum_legend("pale", "G2", GUARD_MAIL,
                                  {"R": "R2", "Y": "Y1", "w": "E1",
                                   "i": "E3", "p": "C2", "f": "E1"}))


def sp_guard_2():
    # Crested helm (R2), kite shield on the left arm, dark skin, no spear.
    rows = [
        "................",
        "................",
        ".....CRRRRC.....",
        ".....CssssC.....",
        ".....sesses.....",
        ".....ssttss.....",
        "......tttt......",
        "..KKCAARRAAC....",
        "..KKKAARRAAB....",
        "..KDKAARRAAB....",
        "..KKKAARRAAB....",
        "..KKswwYwwws....",
        "...K.AA..AA.....",
        ".....AA..AA.....",
        ".....ff..ff.....",
        "................",
    ]
    return parse(rows, hum_legend("dark", "G2", GUARD_MAIL,
                                  {"R": "R2", "Y": "Y1", "w": "E1",
                                   "K": "G4", "D": "R2", "f": "E1"}))


LABORER_CLOTH = ("E3", "E2", "E5")


def sp_laborer_0():
    # Serf: brown hair, tan skin, patched work tunic, rope belt.
    return human("tan", "E2", LABORER_CLOTH,
                 extra={"b": "E5", "g": "E2", "f": "E1"})


def sp_laborer_1():
    # Black hair, dark skin, darker cloth.
    return human("dark", "G1", ("E2", "E1", "E4"),
                 extra={"b": "E5", "g": "E1", "f": "N0"})


def sp_laborer_2():
    # Grey hair, pale, grain sack over the left shoulder.
    g = human("pale", "G3", LABORER_CLOTH,
              extra={"b": "E5", "g": "E2", "f": "E1"})
    sack = [(3, 6, "E4"), (4, 6, "E4"), (2, 7, "E4"), (3, 7, "E5"),
            (4, 7, "E4"), (2, 8, "E4"), (3, 8, "E4"), (4, 8, "E3"),
            (2, 9, "E3"), (3, 9, "E4"), (3, 10, "E3")]
    return patch(g, sack)


def sp_keeper_0():
    # Animal keeper: laborer build + shepherd's crook on the right.
    g = human("tan", "E1", LABORER_CLOTH,
              extra={"b": "E5", "g": "E2", "f": "E1"})
    crook = [(11, 2, "E3"), (12, 2, "E3"), (12, 3, "E3"),
             (12, 4, "E3"), (12, 5, "E3"), (12, 6, "E3"), (12, 7, "E3"),
             (12, 8, "E3"), (12, 9, "E3"), (12, 10, "E3"), (12, 11, "E3"),
             (12, 12, "E3"), (12, 13, "E3")]
    return patch(g, crook)


def sp_keeper_1():
    # Straw hat (thatch ramp), crook, dark skin, lighter tunic.
    g = human("dark", "E1", ("E4", "E3", "E5"),
              extra={"b": "E1", "g": "E2", "f": "E1"})
    hat = [(6, 2, "E4"), (7, 2, "E4"), (8, 2, "E4"), (9, 2, "E4"),
           (5, 2, None), (10, 2, None),
           (4, 3, "E5"), (5, 3, "E5"), (6, 3, "E5"), (7, 3, "E5"),
           (8, 3, "E5"), (9, 3, "E5"), (10, 3, "E5"), (11, 3, "E5")]
    crook = [(11, 2, "E3"), (12, 2, "E3"), (12, 3, "E3"),
             (12, 4, "E3"), (12, 5, "E3"), (12, 6, "E3"), (12, 7, "E3"),
             (12, 8, "E3"), (12, 9, "E3"), (12, 10, "E3"), (12, 11, "E3"),
             (12, 12, "E3"), (12, 13, "E3")]
    return patch(g, hat + crook)


def sp_merchant_0():
    # Shopkeeper: E4 coat, bone apron, Y1 buckle, combed brown hair.
    rows = [
        "................",
        "................",
        ".....hhhhhh.....",
        ".....hlllsh.....",
        ".....sesses.....",
        ".....ssttss.....",
        "......tttt......",
        "....AAAAAAAA....",
        "....BAWWWWAB....",
        "....BAWWWWAB....",
        "....BAWWWWAB....",
        "....swwYwwws....",
        ".....gg..gg.....",
        ".....gg..gg.....",
        ".....ff..ff.....",
        "................",
    ]
    return parse(rows, hum_legend("pale", "E2", ("E4", "E3", "E5"),
                                  {"W": "B1", "Y": "Y1", "w": "E1",
                                   "g": "E2", "f": "E1"}))


def sp_merchant_1():
    # Balding, grey-bearded, no apron; Y1 trim on the coat collar.
    rows = [
        "................",
        "................",
        ".....tssst......",
        ".....slllst.....",
        ".....sesses.....",
        ".....KsttsK.....",
        "......KKKK......",
        "....AYAAAAYA....",
        "....BABAABAB....",
        "....BAABBAAB....",
        "....BABAABAB....",
        "....swwYwwws....",
        ".....gg..gg.....",
        ".....gg..gg.....",
        ".....ff..ff.....",
        "................",
    ]
    return parse(rows, hum_legend("pale", "E2", ("E4", "E3", "E5"),
                                  {"K": "G3", "Y": "Y1", "w": "E1",
                                   "g": "E2", "f": "E1"}))


def sp_mouse_0():
    # Quay mouse: tiny dust-brown body low to the boards, one big ear, E5
    # nose + thin tail left (beast food channel pass). NOT tagged vermin —
    # see sp_cat_0's note (the gull pool must not gain it).
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "......AA.AA.....",
        ".....ABAAAAe....",
        "...t.ABBAAAnn...",
        "..tt..AB.AA.....",
        "................",
    ]
    return parse(rows, {"A": "E4", "B": "E3", "t": "E5", "n": "E5", "e": "N0"})


def sp_priest_0():
    # Priest of the Flame: G1 vestment, hood up, R2 stole, Y1 medallion.
    rows = [
        "................",
        "................",
        ".....hhhhhh.....",
        ".....hhhhhh.....",
        ".....hesseh.....",
        ".....hsttsh.....",
        "......tttt......",
        "....AAAAAAAA....",
        "....BARAARAB....",
        "....BARYARAB....",
        "....BARssRAB....",
        "....BARAARAB....",
        "....BARAARAB....",
        "....BARAARAB....",
        "....AAAAAAAA....",
        "................",
    ]
    return parse(rows, hum_legend("tan", "G1", ("G1", "N0", "G2"),
                                  {"R": "R2", "Y": "Y1"}))


def sp_priest_1():
    # Bare-headed elder: grey hair, R2 skullcap, R2-trimmed hem.
    rows = [
        "................",
        "................",
        ".....RRRRRR.....",
        ".....hlllsh.....",
        ".....sesses.....",
        ".....ssttss.....",
        "......tttt......",
        "....AAAAAAAA....",
        "....BARAARAB....",
        "....BARYARAB....",
        "....BARssRAB....",
        "....BARAARAB....",
        "....BARAARAB....",
        "....BARAARAB....",
        "....RRRRRRRR....",
        "................",
    ]
    return parse(rows, hum_legend("pale", "G3", ("G1", "N0", "G2"),
                                  {"R": "R2", "Y": "Y1"}))


def sp_rat_0():
    # Wharf rat: low G2 body, head right, R3 eye, E5 tail sweeping left.
    rows = [
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        "................",
        ".....AAAA.A.....",
        "....ABBAAAAA....",
        "...ABBBAAAAeA...",
        "...ABBBAAAAnn...",
        "....AABAAAA.....",
        "..ttt.B..B......",
        "................",
    ]
    return parse(rows, {"A": "G3", "B": "G2", "e": "R3", "n": "E5",
                        "t": "E5"})


def sp_vagrant_0():
    # Wastrel: hunched, patched G2 cloak, bare feet.
    return parse(VAGRANT, hum_legend("tan", "E2", ("G2", "G1", "G3"),
                                     {"P": "E2"}))


def sp_vagrant_1():
    # Mud-brown rags with mossy V2 patches, dark skin.
    return parse(VAGRANT, hum_legend("dark", "N0", ("E2", "E1", "E3"),
                                     {"P": "V2"}))


def sp_vagrant_2():
    # Hooded, pale, leaning on a stick.
    rows = [
        "................",
        "................",
        "................",
        ".....hhhhhh.....",
        ".....hhhhhh.....",
        ".....hesseh.....",
        ".....ssttss.....",
        "....A.tttt.A....",
        "....AAAAAAAA.k..",
        "....BAPAAPAB.k..",
        "....BAAPPAAB.k..",
        "....APAAAAPB.k..",
        "....B.AAAA.Bk...",
        ".....A.AA.A.k...",
        ".....ss..ss.k...",
        "................",
    ]
    return parse(rows, hum_legend("pale", "G2", ("G2", "G1", "G3"),
                                  {"P": "E2", "k": "E3"}))


# id -> (paint function, tags). Ascending ASCII id order is asserted below.
SPRITES = [
    ("actor_cat_0", sp_cat_0, ["actor", "beast", "cat"]),
    ("actor_disciple_0", sp_disciple_0, ["actor", "humanoid", "clergy", "ragged"]),
    ("actor_disciple_1", sp_disciple_1, ["actor", "humanoid", "clergy", "ragged"]),
    ("actor_dog_0", sp_dog_0, ["actor", "beast", "livestock", "dog"]),
    ("actor_dog_1", sp_dog_1, ["actor", "beast", "livestock", "dog"]),
    ("actor_feral_dog_0", sp_feral_dog_0, ["actor", "beast", "vermin"]),
    ("actor_goat_0", sp_goat_0, ["actor", "beast", "livestock", "goat"]),
    ("actor_goat_1", sp_goat_1, ["actor", "beast", "livestock", "goat"]),
    ("actor_guard_0", sp_guard_0, ["actor", "humanoid", "guard", "armored"]),
    ("actor_guard_1", sp_guard_1, ["actor", "humanoid", "guard", "armored"]),
    ("actor_guard_2", sp_guard_2, ["actor", "humanoid", "guard", "armored"]),
    ("actor_keeper_0", sp_keeper_0, ["actor", "humanoid", "laborer", "keeper"]),
    ("actor_keeper_1", sp_keeper_1, ["actor", "humanoid", "laborer", "keeper"]),
    ("actor_laborer_0", sp_laborer_0, ["actor", "humanoid", "laborer"]),
    ("actor_laborer_1", sp_laborer_1, ["actor", "humanoid", "laborer"]),
    ("actor_laborer_2", sp_laborer_2, ["actor", "humanoid", "laborer"]),
    ("actor_merchant_0", sp_merchant_0, ["actor", "humanoid", "merchant"]),
    ("actor_merchant_1", sp_merchant_1, ["actor", "humanoid", "merchant"]),
    ("actor_mouse_0", sp_mouse_0, ["actor", "beast", "mouse"]),
    ("actor_priest_0", sp_priest_0, ["actor", "humanoid", "clergy", "robed"]),
    ("actor_priest_1", sp_priest_1, ["actor", "humanoid", "clergy", "robed"]),
    ("actor_rat_0", sp_rat_0, ["actor", "beast", "vermin"]),
    ("actor_vagrant_0", sp_vagrant_0, ["actor", "humanoid", "vagrant", "ragged"]),
    ("actor_vagrant_1", sp_vagrant_1, ["actor", "humanoid", "vagrant", "ragged"]),
    ("actor_vagrant_2", sp_vagrant_2, ["actor", "humanoid", "vagrant", "ragged"]),
]

HUMANOID_IDS = {sid for sid, _, tags in SPRITES if "humanoid" in tags}

# Actor type id -> tag query (unified spec section 3.2 — this table IS the
# spec's actorQueries). Every entry must resolve non-empty at load; the
# SpriteIndex validator makes boot fail otherwise.
ACTOR_QUERIES = [
    ("animal", ["actor", "beast", "livestock"]),
    ("animal_keeper", ["actor", "humanoid", "laborer", "keeper"]),
    ("cat", ["actor", "beast", "cat"]),
    ("disciple_of_the_flame", ["actor", "humanoid", "clergy", "ragged"]),
    ("feral", ["actor", "beast", "vermin"]),
    ("militia_watch", ["actor", "humanoid", "guard"]),
    ("mouse", ["actor", "beast", "mouse"]),
    ("priest_of_the_flame", ["actor", "humanoid", "clergy", "robed"]),
    ("serf", ["actor", "humanoid", "laborer"]),
    ("shopkeeper", ["actor", "humanoid", "merchant"]),
    ("wastrel", ["actor", "humanoid", "vagrant"]),
]

# Minimum variant counts per type (unified spec section 3.2, placeholders).
MIN_VARIANTS = {
    "animal": 4, "animal_keeper": 2, "cat": 1, "disciple_of_the_flame": 2,
    "feral": 2, "militia_watch": 3, "mouse": 1, "priest_of_the_flame": 2,
    "serf": 3, "shopkeeper": 2, "wastrel": 3,
}

# --------------------------------------------------------------------------
# validation (hard failures abort with nonzero exit)
# --------------------------------------------------------------------------

TOKEN = re.compile(r"^[a-z0-9_]+$")


def find_flat_rect(g):
    """No monochrome COLORED axis-aligned rect >= 6x5 / 5x6 (transparent
    runs are fine — this is the tile pack's no-flat-fill rule on sprites)."""
    for (mw, mh) in ((6, 5), (5, 6)):
        for y0 in range(T - mh + 1):
            for x0 in range(T - mw + 1):
                c = g[y0][x0]
                if c is None:
                    continue
                if all(g[y0 + dy][x0 + dx] == c
                       for dy in range(mh) for dx in range(mw)):
                    return x0, y0, mw, mh, c
    return None


def validate_sprite(sid, g):
    errors = []
    filled = [(x, y) for y in range(T) for x in range(T) if g[y][x] is not None]
    if not filled:
        return [f"{sid}: empty sprite"]
    xs = [x for x, _ in filled]
    ys = [y for _, y in filled]
    w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    if sid in HUMANOID_IDS:
        if w > 12:
            errors.append(f"{sid}: humanoid width {w} > 12")
        if h > 15:
            errors.append(f"{sid}: humanoid height {h} > 15")
    if max(ys) not in (14, 15):
        errors.append(f"{sid}: lowest filled row {max(ys)} not on 14-15")
    shades = {g[y][x] for x, y in filled}
    if len(shades - {"N0"}) < 3:
        errors.append(f"{sid}: only {len(shades - {'N0'})} non-outline shades")
    hit = find_flat_rect(g)
    if hit:
        errors.append(f"{sid}: flat {hit[2]}x{hit[3]} {hit[4]} rect at "
                      f"({hit[0]},{hit[1]})")
    # outline coverage: every filled pixel on the silhouette edge is N0
    for x, y in filled:
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            inside = 0 <= nx < T and 0 <= ny < T
            if (not inside or g[ny][nx] is None) and g[y][x] != "N0":
                errors.append(f"{sid}: silhouette pixel ({x},{y}) {g[y][x]} "
                              "not outlined N0")
                break
    return errors


def validate_index(errors):
    ids = [sid for sid, _, _ in SPRITES]
    if ids != sorted(ids):
        errors.append("sprite ids not in ascending ASCII order")
    if len(set(ids)) != len(ids):
        errors.append("duplicate sprite ids")
    for sid, _, tags in SPRITES:
        if not TOKEN.match(sid):
            errors.append(f"bad id token: {sid}")
        for t in tags:
            if not TOKEN.match(t):
                errors.append(f"{sid}: bad tag token: {t}")
    tag_sets = {sid: set(tags) for sid, _, tags in SPRITES}
    for type_id, q in ACTOR_QUERIES:
        pool = [sid for sid in ids if set(q) <= tag_sets[sid]]
        need = MIN_VARIANTS[type_id]
        if len(pool) < need:
            errors.append(f"actorQueries.{type_id}: {len(pool)} candidates "
                          f"< min {need} ({pool})")


# --------------------------------------------------------------------------
# face-part merge (unified index)
# --------------------------------------------------------------------------

def load_face_parts(errors):
    """Loads and sanity-checks gen_face_parts.py's committed pair. Returns
    (entries, face_rows, face_img); appends to errors on any defect."""
    try:
        with open(FACE_JSON_PATH, encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, ValueError) as e:
        errors.append(f"face merge: cannot read {FACE_JSON_PATH}: {e}")
        return [], 0, None
    try:
        img = Image.open(FACE_PNG_PATH).convert("RGBA")
    except OSError as e:
        errors.append(f"face merge: cannot read {FACE_PNG_PATH}: {e}")
        return [], 0, None
    if doc.get("schemaVersion") != 1:
        errors.append("face merge: face-parts-index schemaVersion != 1")
    if doc.get("tilePx") != T or doc.get("columns") != COLS:
        errors.append(f"face merge: face sheet grid {doc.get('columns')}x@"
                      f"{doc.get('tilePx')}px incompatible with {COLS}x@{T}px")
    face_rows = doc.get("rows", 0)
    if img.size != (COLS * T, face_rows * T):
        errors.append(f"face merge: face-parts.png {img.size} != declared "
                      f"{COLS * T}x{face_rows * T}")
    entries = doc.get("sprites", [])
    ids = [e["id"] for e in entries]
    if ids != sorted(ids) or len(set(ids)) != len(ids):
        errors.append("face merge: face ids not unique/ascending")
    return entries, face_rows, img


def validate_merge(face_entries, errors):
    """Merge invariants: no id collision; actor/face tag vocabularies fully
    disjoint (guarantees every actorQueries pool and every face_* tag pool
    resolves identically over the unified index as over its source index)."""
    actor_ids = {sid for sid, _, _ in SPRITES}
    actor_tags = {t for _, _, tags in SPRITES for t in tags}
    for e in face_entries:
        if e["id"] in actor_ids:
            errors.append(f"face merge: id collision: {e['id']}")
        if not e["id"].startswith("face_"):
            errors.append(f"face merge: face id lacks face_ prefix: {e['id']}")
        overlap = actor_tags & set(e["tags"])
        if overlap:
            errors.append(f"face merge: {e['id']} shares actor tags {sorted(overlap)}")
    combined = sorted(actor_ids) + [e["id"] for e in face_entries]
    if combined != sorted(combined):
        errors.append("face merge: unified id order not ascending ASCII")


# --------------------------------------------------------------------------
# sheet + index emission
# --------------------------------------------------------------------------

def build():
    errors = []
    validate_index(errors)
    rows_needed = (len(SPRITES) + COLS - 1) // COLS
    img = Image.new("RGBA", (COLS * T, rows_needed * T), (0, 0, 0, 0))
    px = img.load()
    cells = {}
    grids = {}
    # Shelf packer, ascending id order; all entries 1x1 -> row-major fill.
    idx = 0
    for sid, paint, _tags in SPRITES:
        g = outline(paint())
        errors.extend(validate_sprite(sid, g))
        col, row = idx % COLS, idx // COLS
        cells[sid] = (col, row)
        grids[sid] = g
        for y in range(T):
            for x in range(T):
                key = g[y][x]
                if key is not None:
                    px[col * T + x, row * T + y] = PAL[key] + (255,)
        idx += 1
    return img, cells, grids, rows_needed, errors


def census(img, errors):
    """Palette census over the FULL unified sheet: every pixel MERCOLAS-24
    opaque or fully transparent (covers merged face rows too)."""
    px = img.load()
    allowed = {v + (255,) for v in PAL.values()} | {(0, 0, 0, 0)}
    for y in range(img.height):
        for x in range(img.width):
            if px[x, y] not in allowed:
                errors.append(f"off-palette pixel at ({x},{y}): {px[x, y]}")


def face_entry_line(entry, row_offset):
    """One face entry as a JSON line, fields byte-preserved except the cell
    row offset (and the fixed key order id/cell/w/h/weight/tags)."""
    col, row = entry["cell"]
    fields = ['"id": "%s"' % entry["id"], '"cell": [%d, %d]' % (col, row + row_offset)]
    if "w" in entry:
        fields.append('"w": %d' % entry["w"])
    if "h" in entry:
        fields.append('"h": %d' % entry["h"])
    if "weight" in entry:
        fields.append('"weight": %d' % entry["weight"])
    fields.append('"tags": [%s]' % ", ".join('"%s"' % t for t in entry["tags"]))
    return "    { %s }" % ", ".join(fields)


def write_index(cells, rows_needed, face_entries, face_rows):
    out = []
    a = out.append
    a("{")
    a('  "schemaVersion": 1,')
    a('  "provenance": "Generated by tools/scripts/gen_actor_sprites.py - do not hand-edit; '
      "rerun the generator (byte-identical output; rerun gen_face_parts.py first if faces "
      "changed). GRANADAD ART UNIFIED SPEC v1 sections 2-4 / DECISIONS.md Art register row "
      "(Eli 2026-07-13, FOURTH revision): actors as real sprites from a tag-queryable "
      "SpriteIndex, faces composed from tile/sprite parts; ORIGINAL pixel art, MERCOLAS-24 "
      'palette, zero copied pixels. Kenney pack stays in-repo as the committed fallback.",')
    a('  "notes": "THE unified index: ONE SpriteIndex serves actor sprites AND face parts '
      "via tags (integration merge; the actor/face tag vocabularies are validated disjoint "
      "so neither side's pools can cross-match). Actor entries are authored in this script; "
      "face-part entries are merged verbatim from content/art/faces/face-parts-index.json "
      "(gen_face_parts.py, canonical for the face golden/raster tests) with cell rows "
      "offset below the actor rows. Single south-facing sprite per actor entry (v0 ruling - "
      'no facing variants, no animation).",')
    a('  "sheet": "art/sprites/sprites.png",')
    a('  "tilePx": 16,')
    a('  "columns": %d, "rows": %d,' % (COLS, rows_needed + face_rows))
    a('  "sprites": [')
    for sid, _paint, tags in SPRITES:
        col, row = cells[sid]
        a('    { "id": "%s", "cell": [%d, %d], "tags": [%s] },'
          % (sid, col, row, ", ".join('"%s"' % t for t in tags)))
    for i, entry in enumerate(face_entries):
        comma = "," if i < len(face_entries) - 1 else ""
        a(face_entry_line(entry, rows_needed) + comma)
    a("  ],")
    a('  "actorQueries": {')
    for i, (type_id, q) in enumerate(ACTOR_QUERIES):
        comma = "," if i < len(ACTOR_QUERIES) - 1 else ""
        a('    "%s": [%s]%s' % (type_id, ", ".join('"%s"' % t for t in q), comma))
    a("  }")
    a("}")
    a("")
    with open(JSON_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out))


def write_preview(img, path, scale=8):
    """Self-grading aid only (never committed): zoomed montage on G1."""
    bg = Image.new("RGBA", (img.width * scale, img.height * scale),
                   PAL["G1"] + (255,))
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    bg.alpha_composite(big)
    bg.save(path, "PNG")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    img, cells, grids, rows_needed, errors = build()
    face_entries, face_rows, face_img = load_face_parts(errors)
    validate_merge(face_entries, errors)
    unified = None
    if face_img is not None and not errors:
        unified = Image.new("RGBA", (COLS * T, (rows_needed + face_rows) * T),
                            (0, 0, 0, 0))
        unified.alpha_composite(img, (0, 0))
        unified.alpha_composite(face_img, (0, rows_needed * T))
        census(unified, errors)
    if errors:
        for e in errors:
            print("FAIL " + e)
        raise SystemExit(1)
    unified.save(PNG_PATH, "PNG", optimize=False, compress_level=9)
    write_index(cells, rows_needed, face_entries, face_rows)
    print(f"ok: {len(SPRITES)} actor sprites + {len(face_entries)} face parts "
          f"on a {COLS}x{rows_needed + face_rows} unified sheet")
    print("wrote " + PNG_PATH)
    print("wrote " + JSON_PATH)
    preview = os.environ.get("SPRITE_PREVIEW")
    if preview:
        write_preview(img, preview)
        print("wrote " + preview)


if __name__ == "__main__":
    main()
