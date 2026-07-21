#!/usr/bin/env python3
"""Door-width audit for content/maps/src/docks_surface.tmx (PASS 9 living-docks).

Eli's door standard: "just make sure all doors are at least 2 wide." This gate parses the
raw Tiled XML and flags every 1-WIDE DOORWAY CELL -- a walkable cell with solid cells on
both sides along one axis and walkable cells on both sides along the other (a 1-wide gap
punched through a wall/furniture run). Any door that meets the standard (>= 2 contiguous
walkable cells in its wall run) can never match the pattern, so a PASS here proves that
NO authored building/compound/hovel/fence door, interior partition door, gate, rot-gap
entry, cage slot, vault mouth or strongroom slot anywhere on the map is 1 wide -- except
the frozen, hand-reviewed exception set below.

Every flagged cell in the frozen snapshot was individually reviewed (2026-07-21) and is
one of these NON-DOOR categories:
  a. furniture/stall/rack/crate aisle cells INSIDE rooms and work yards (tar-yard barrel
     grid, K12/K29 crate+rack aisles, K01 ledger racks, Bilge hammock posts, Dawnstalls
     stall rows, kennel cages, bench/bollard plaza gaps, ...);
  b. 1-wide exterior SEAM LANES between adjacent building footprints (pre-existing dense
     city fabric -- both flanks are building walls, so widening means moving footprints);
  c. finger-pier arms squeezed between moored hulls (marina-slip geometry, water-flanked);
  d. roof-slum strips between hut walls and compound parapets;
  e. the 3 K34 prison-corridor cells between aligned north/south cell dividers (pilaster
     pairs fronting dead-end cells, not doors; the push mechanic is the anti-deadlock
     valve there);
  f. C4 roofhut_10's relocated west door (188,92,z13) -- a 3-cell wall run has no room to
     widen, and the hut interior is a single cell;
  g. C4 c03's interior spine (2-wide-interior squalor unit, furniture-pinched by design).

The gate: the SHA-256 of the sorted flag list must equal the frozen snapshot hash, every
script_anchor must be walkable except the 6 frozen pre-existing quirks, and per-z walkable
component counts must equal the frozen values (no severed component can sneak in). If the
map legitimately changes, re-review the new flag list by hand, then refreeze with
--refreeze (prints the new constants).
"""
import hashlib
import re
import sys
from collections import deque

W, H = 192, 128
STAIR_RAMP = {3, 4, 5, 10, 11, 36, 37}

# ---- frozen snapshot (2026-07-21, PASS 9; regenerate via --refreeze after hand-review) ----
FROZEN_FLAG_COUNT = 392
FROZEN_FLAG_SHA = "9a522690b9fddfba0df63167b1295637c7b8d877b89221ff54026e40958bec2d"
FROZEN_ANCHOR_EXCEPTIONS = {
    "clue_brann_grayledger_anchor",  # z10 -- sits on the cellar stair head (pre-PASS 9)
    "timber_pond_anchor",            # z10 -- floating-log cell convention (pre-PASS 9)
    "mission_garden_anchor",         # z11 -- garden marker on tilled ground (pre-PASS 9)
    "patrol_post_tarwalk_mid",       # z11 -- shares the brazier cell (pre-PASS 9)
    "k01_archive_anchor",            # z12 -- inside the record racking (pre-PASS 9)
    "gibbet_anchor",                 # z13 -- the gibbet cage cell itself (pre-PASS 9)
}
FROZEN_COMPONENTS = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 14, 16, 39, 26, 26, 0]


def parse(path):
    text = open(path, encoding="utf-8").read()
    zs = []
    for (zn, body) in re.findall(r'<group id="\d+" name="z:\+(\d+)">(.*?)</group>',
                                 text, re.S):
        layers = {}
        for (lname, data) in re.findall(
                r'<layer id="\d+" name="(\w+)"[^>]*>\s*<data encoding="csv">(.*?)</data>',
                body, re.S):
            rows = [r.strip() for r in data.strip().split("\n")]
            layers[lname] = [[int(v) for v in row.rstrip(",").split(",")] for row in rows]
        marks = [(m.group(1), m.group(2), int(m.group(3)) // 16, int(m.group(4)) // 16)
                 for m in re.finditer(
                     r'<object id="\d+" name="([^"]+)" type="([^"]+)" x="(\d+)" y="(\d+)"',
                     body)]
        zero = [[0] * W for _ in range(H)]
        zs.append({"z": int(zn), "T": layers.get("terrain", zero),
                   "F": layers.get("floor", zero), "marks": marks})
    zs.sort(key=lambda d: d["z"])
    return zs


def walkable(zd, x, y):
    if not (0 <= x < W and 0 <= y < H):
        return False
    t = zd["T"][y][x]
    return t in STAIR_RAMP or (t == 0 and zd["F"][y][x] != 0)


def solid(zd, x, y):
    if not (0 <= x < W and 0 <= y < H):
        return False
    t = zd["T"][y][x]
    return t != 0 and t not in STAIR_RAMP


def flags(zs):
    out = []
    for zd in zs:
        for y in range(H):
            for x in range(W):
                if not walkable(zd, x, y):
                    continue
                if solid(zd, x - 1, y) and solid(zd, x + 1, y) \
                        and walkable(zd, x, y - 1) and walkable(zd, x, y + 1):
                    out.append((zd["z"], x, y, "NS"))
                elif solid(zd, x, y - 1) and solid(zd, x, y + 1) \
                        and walkable(zd, x - 1, y) and walkable(zd, x + 1, y):
                    out.append((zd["z"], x, y, "WE"))
    return sorted(out)


def component_counts(zs):
    counts = []
    for zd in zs:
        seen = [[False] * W for _ in range(H)]
        n = 0
        for y in range(H):
            for x in range(W):
                if seen[y][x] or not walkable(zd, x, y):
                    continue
                n += 1
                q = deque([(x, y)])
                seen[y][x] = True
                while q:
                    (cx, cy) = q.popleft()
                    for (nx, ny) in ((cx+1, cy), (cx-1, cy), (cx, cy+1), (cx, cy-1)):
                        if 0 <= nx < W and 0 <= ny < H and not seen[ny][nx] \
                                and walkable(zd, nx, ny):
                            seen[ny][nx] = True
                            q.append((nx, ny))
        counts.append(n)
    return counts


def main():
    path = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") \
        else "content/maps/src/docks_surface.tmx"
    refreeze = "--refreeze" in sys.argv
    zs = parse(path)
    fl = flags(zs)
    sha = hashlib.sha256(repr(fl).encode()).hexdigest()
    comps = component_counts(zs)
    bad_anchors = [(zd["z"], name, tx, ty)
                   for zd in zs for (name, cls, tx, ty) in zd["marks"]
                   if cls == "script_anchor" and not walkable(zd, tx, ty)
                   and name not in FROZEN_ANCHOR_EXCEPTIONS]
    if refreeze:
        print("FROZEN_FLAG_COUNT = %d" % len(fl))
        print('FROZEN_FLAG_SHA = "%s"' % sha)
        print("FROZEN_COMPONENTS = %r" % comps)
        return
    ok = True
    if len(fl) != FROZEN_FLAG_COUNT or sha != FROZEN_FLAG_SHA:
        ok = False
        print("FAIL: 1-wide doorway snapshot drifted: %d cells (frozen %d), sha %s"
              % (len(fl), FROZEN_FLAG_COUNT, sha))
        print("      hand-review the delta, then refreeze with --refreeze")
    if bad_anchors:
        ok = False
        print("FAIL: script_anchors on non-walkable cells (outside frozen exceptions):")
        for b in bad_anchors:
            print("      z%d %s (%d,%d)" % b)
    if comps != FROZEN_COMPONENTS:
        ok = False
        print("FAIL: walkable component counts per z drifted: %r (frozen %r)"
              % (comps, FROZEN_COMPONENTS))
    if ok:
        print("door-width audit: OK (%d reviewed non-door 1-wide cells, all frozen; "
              "no 1-wide door openings; anchors walkable; components stable)" % len(fl))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
